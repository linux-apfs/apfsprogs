/*
 * Copyright (C) 2024 Ernesto A. Fernández <ernesto@corellium.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <apfs/checksum.h>
#include <apfs/raw.h>
#include "version.h"

static char *progname = NULL;

static int dev_fd = -1;
static unsigned long s_blocksize;
static u64 nx_xid;

/**
 * usage - Print usage information and exit
 */
static void usage(void)
{
	fprintf(stderr, "usage: %s [-v] device\n", progname);
	exit(EXIT_FAILURE);
}

/**
 * version - Print version information and exit
 */
static void version(void)
{
	if (*GIT_COMMIT) {
		printf("apfs-label %s\n", GIT_COMMIT);
		exit(EXIT_SUCCESS);
	} else {
		printf("apfs-label - unknown git commit id\n");
		exit(EXIT_FAILURE);
	}
}

/**
 * system_error - Print a system error message and exit
 */
static __attribute__((noreturn)) void system_error(void)
{
	perror(progname);
	exit(EXIT_FAILURE);
}

/**
 * fatal - Print a message and exit with an error code
 * @message: text to print
 */
static __attribute__((noreturn)) void fatal(const char *message)
{
	fprintf(stderr, "%s: %s\n", progname, message);
	exit(EXIT_FAILURE);
}

static void *readall(int fd, size_t count, off_t offset)
{
	void *buf = NULL;
	size_t copied;
	ssize_t ret;

	buf = malloc(count);
	if (!buf)
		system_error();

	copied = 0;
	while (count > 0) {
		ret = pread(fd, buf + copied, count, offset + copied);
		if (ret < 0)
			system_error();
		count -= ret;
		copied += ret;
	}
	return buf;
}

/**
 * read_super_copy - Read the copy of the container superblock in block 0
 *
 * Sets s_blocksize and returns a pointer to the raw superblock in memory.
 */
static struct apfs_nx_superblock *read_super_copy(void)
{
	struct apfs_nx_superblock *msb_raw = NULL;
	int bsize_tmp;

	/*
	 * For now assume a small blocksize, we only need it so that we can
	 * read the actual blocksize from disk.
	 */
	bsize_tmp = APFS_NX_DEFAULT_BLOCK_SIZE;

	msb_raw = readall(dev_fd, bsize_tmp, APFS_NX_BLOCK_NUM * bsize_tmp);
	if (le32_to_cpu(msb_raw->nx_magic) != APFS_NX_MAGIC)
		fatal("not an apfs container");
	s_blocksize = le32_to_cpu(msb_raw->nx_block_size);
	if (s_blocksize < 4096)
		fatal("reported blocksize is too small");

	if (s_blocksize != bsize_tmp) {
		free(msb_raw);

		msb_raw = readall(dev_fd, s_blocksize, APFS_NX_BLOCK_NUM * s_blocksize);
	}
	return msb_raw;
}

static int obj_verify_csum(struct apfs_obj_phys *obj)
{
	return le64_to_cpu(obj->o_cksum) == fletcher64((char *)obj + APFS_MAX_CKSUM_SIZE, s_blocksize - APFS_MAX_CKSUM_SIZE);
}

/**
 * read_latest_super - Read the latest checkpoint superblock
 * @base:	base of the checkpoint descriptor area
 * @blocks:	block count for the checkpoint descriptor area
 */
static struct apfs_nx_superblock *read_latest_super(u64 base, u32 blocks)
{
	struct apfs_nx_superblock *latest = NULL, *current = NULL;
	u64 xid = 0;
	u64 bno;

	for (bno = base; bno < base + blocks; ++bno) {
		current = readall(dev_fd, s_blocksize, bno * s_blocksize);

		if (le32_to_cpu(current->nx_magic) != APFS_NX_MAGIC)
			goto next; /* Not a superblock */
		if (le64_to_cpu(current->nx_o.o_xid) <= xid)
			goto next; /* Old */
		if (!obj_verify_csum(&current->nx_o))
			goto next; /* Corrupted */

		xid = le64_to_cpu(current->nx_o.o_xid);
		if (latest)
			free(latest);
		latest = current;
		current = NULL;
next:
		if (current)
			free(current);
	}

	if (!latest)
		fatal("no valid superblock in checkpoint area.");
	nx_xid = xid;
	return latest;
}

static struct apfs_nx_superblock *read_super(void)
{
	struct apfs_nx_superblock *msb = NULL;
	u64 desc_base;
	u32 desc_blocks;

	msb = read_super_copy();
	desc_base = le64_to_cpu(msb->nx_xp_desc_base);
	if (desc_base >> 63 != 0) {
		/* The highest bit is set when checkpoints are not contiguous */
		fatal("checkpoint descriptor tree not yet supported.");
	}
	desc_blocks = le32_to_cpu(msb->nx_xp_desc_blocks);
	if (desc_blocks > 10000) /* Arbitrary loop limit, is it enough? */
		fatal("too many checkpoint descriptors?");
	free(msb);
	msb = NULL;

	return read_latest_super(desc_base, desc_blocks);
}

static struct apfs_btree_node_phys *omap_bno_to_root(u64 omap_bno)
{
	struct apfs_omap_phys *omap = NULL;
	struct apfs_btree_node_phys *root = NULL;
	u64 root_bno;

	omap = readall(dev_fd, s_blocksize, omap_bno * s_blocksize);
	root_bno = le64_to_cpu(omap->om_tree_oid);
	free(omap);

	root = readall(dev_fd, s_blocksize, root_bno * s_blocksize);

	/* I don't think this can happen so I don't support it for now */
	if ((le16_to_cpu(root->btn_flags) & APFS_BTNODE_LEAF) == 0)
		fatal("container omap isn't a single node");
	return root;
}

static void omap_node_locate_key(struct apfs_btree_node_phys *node, int index, int *off)
{
	struct apfs_kvoff *entry;
	int keys_start, len;

	if (index >= APFS_NX_MAX_FILE_SYSTEMS)
		fatal("node index is out of bounds");
	if ((le16_to_cpu(node->btn_flags) & APFS_BTNODE_FIXED_KV_SIZE) == 0)
		fatal("omap root should have fixed length keys/values");

	keys_start = sizeof(*node) + le16_to_cpu(node->btn_table_space.off) + le16_to_cpu(node->btn_table_space.len);

	entry = (struct apfs_kvoff *)node->btn_data + index;
	*off = keys_start + le16_to_cpu(entry->k);
	len = 16;

	if (*off + len > s_blocksize)
		fatal("omap key out of bounds");
}

static void omap_node_locate_val(struct apfs_btree_node_phys *node, int index, int *off)
{
	struct apfs_kvoff *entry;
	int len;

	if (index >= APFS_NX_MAX_FILE_SYSTEMS)
		fatal("node index is out of bounds");
	if ((le16_to_cpu(node->btn_flags) & APFS_BTNODE_FIXED_KV_SIZE) == 0)
		fatal("omap root should have fixed length keys/values");

	entry = (struct apfs_kvoff *)node->btn_data + index;
	*off = s_blocksize - sizeof(struct apfs_btree_info) - le16_to_cpu(entry->v);
	len = 16;

	if (*off < 0 || *off + len > s_blocksize)
		fatal("omap value out of bounds");
}

static int omap_keycmp(struct apfs_omap_key *k1, struct apfs_omap_key *k2)
{
	if (le64_to_cpu(k1->ok_oid) != le64_to_cpu(k2->ok_oid))
		return le64_to_cpu(k1->ok_oid) < le64_to_cpu(k2->ok_oid) ? -1 : 1;
	if (le64_to_cpu(k1->ok_xid) != le64_to_cpu(k2->ok_xid))
		return le64_to_cpu(k1->ok_xid) < le64_to_cpu(k2->ok_xid) ? -1 : 1;
	return 0;
}

static u64 omap_lookup(struct apfs_btree_node_phys *node, u64 oid)
{
	struct apfs_omap_key target_key;
	struct apfs_omap_key *curr_key = NULL;
	struct apfs_omap_val *value = NULL;
	int key_off, val_off;
	int index, left, right;
	int cmp;

	target_key.ok_oid = cpu_to_le64(oid);
	target_key.ok_xid = cpu_to_le64(nx_xid);

	index = le32_to_cpu(node->btn_nkeys);
	if (index > APFS_NX_MAX_FILE_SYSTEMS)
		fatal("too many records in container omap");

	/* Search by bisection */
	cmp = 1;
	left = 0;
	do {
		if (cmp > 0) {
			right = index - 1;
			if (right < left)
				fatal("missing omap record for volume");
			index = (left + right) / 2;
		} else {
			left = index;
			index = DIV_ROUND_UP(left + right, 2);
		}

		omap_node_locate_key(node, index, &key_off);
		curr_key = (void *)node + key_off;
		cmp = omap_keycmp(curr_key, &target_key);
		if (cmp == 0)
			break;
	} while (left != right);

	if (cmp > 0)
		fatal("missing omap record for volume");

	omap_node_locate_val(node, index, &val_off);
	value = (void *)node + val_off;
	return le64_to_cpu(value->ov_paddr);
}

/**
 * list_labels - Find all volumes in the device and print their labels
 */
static void list_labels(void)
{
	struct apfs_nx_superblock *msb = NULL;
	struct apfs_superblock *vsb = NULL;
	struct apfs_btree_node_phys *omap = NULL;
	u64 vol_id, vol_bno;
	int i;

	msb = read_super();
	omap = omap_bno_to_root(le64_to_cpu(msb->nx_omap_oid));

	for (i = 0; i < APFS_NX_MAX_FILE_SYSTEMS; i++) {
		vol_id = le64_to_cpu(msb->nx_fs_oid[i]);
		/* I seem to recall some images had holes in the array */
		if (vol_id == 0)
			continue;
		vol_bno = omap_lookup(omap, vol_id);
		vsb = readall(dev_fd, s_blocksize, vol_bno * s_blocksize);
		if (vsb->apfs_volname[APFS_VOLNAME_LEN - 1])
			fatal("volume label is not properly null-terminated");
		printf("%d\t%s\n", i, vsb->apfs_volname);
		free(vsb);
	}

	free(omap);
	free(msb);
}

int main(int argc, char *argv[])
{
	if (argc == 0)
		exit(EXIT_FAILURE);
	progname = argv[0];

	static const struct option long_options[] = {
		{ .name = "version", .has_arg = no_argument , .flag = NULL, .val = 'v' },
		{ 0 },
	};

	while (1) {
		int opt_index = 0;
		int opt = getopt_long(argc, argv, "v", long_options, &opt_index);

		if (opt == -1)
			break;

		switch (opt) {
		case 'v':
			version();
		default:
			usage();
		}
	}

	if (optind != argc - 1)
		usage();

	const char *filename = argv[optind];

	dev_fd = open(filename, O_RDONLY);
	if (dev_fd == -1)
		system_error();

	list_labels();
	return 0;
}
