/*
 *  apfsprogs/apfsck/super.c
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "apfsck.h"
#include "btree.h"
#include "extents.h"
#include "htable.h"
#include "inode.h"
#include "object.h"
#include "types.h"
#include "super.h"

struct super_block *sb;
struct volume_superblock *vsb;

/**
 * read_super_copy - Read the copy of the container superblock in block 0
 *
 * Sets sb->s_blocksize and returns a pointer to the raw superblock in memory.
 */
static struct apfs_nx_superblock *read_super_copy(void)
{
	struct apfs_nx_superblock *msb_raw;
	int bsize_tmp;

	/*
	 * For now assume a small blocksize, we only need it so that we can
	 * read the actual blocksize from disk.
	 */
	bsize_tmp = APFS_NX_DEFAULT_BLOCK_SIZE;

	msb_raw = mmap(NULL, bsize_tmp, PROT_READ, MAP_PRIVATE,
		       fd, APFS_NX_BLOCK_NUM * bsize_tmp);
	if (msb_raw == MAP_FAILED) {
		perror(NULL);
		exit(1);
	}
	sb->s_blocksize = le32_to_cpu(msb_raw->nx_block_size);

	if (sb->s_blocksize != bsize_tmp) {
		munmap(msb_raw, bsize_tmp);

		msb_raw = mmap(NULL, sb->s_blocksize, PROT_READ, MAP_PRIVATE,
			       fd, APFS_NX_BLOCK_NUM * sb->s_blocksize);
		if (msb_raw == MAP_FAILED) {
			perror(NULL);
			exit(1);
		}
	}

	if (le32_to_cpu(msb_raw->nx_magic) != APFS_NX_MAGIC)
		report("Block zero", "wrong magic.");
	if (!obj_verify_csum(&msb_raw->nx_o))
		report("Block zero", "bad checksum.");
	if (le64_to_cpu(msb_raw->nx_o.o_oid) != APFS_OID_NX_SUPERBLOCK)
		report("Block zero", "bad object id.");

	return msb_raw;
}

/**
 * map_main_super - Find the container superblock and map it into memory
 *
 * Sets sb->s_raw to the in-memory location of the main superblock.
 */
static void map_main_super(void)
{
	struct apfs_nx_superblock *msb_raw;
	struct apfs_nx_superblock *desc_raw = NULL;
	u64 desc_base;
	u32 desc_blocks;
	int i;

	/* Read the superblock from the last clean unmount */
	msb_raw = read_super_copy();

	/* We want to mount the latest valid checkpoint among the descriptors */
	desc_base = le64_to_cpu(msb_raw->nx_xp_desc_base);
	if (desc_base >> 63 != 0) {
		/* The highest bit is set when checkpoints are not contiguous */
		report("Block zero",
		       "checkpoint descriptor tree not yet supported.");
	}
	desc_blocks = le32_to_cpu(msb_raw->nx_xp_desc_blocks);
	if (desc_blocks > 10000) /* Arbitrary loop limit, is it enough? */
		report("Block zero", "too many checkpoint descriptors?");

	/* Now we go through the checkpoints one by one */
	sb->s_raw = NULL;
	sb->s_xid = le64_to_cpu(msb_raw->nx_o.o_xid);
	for (i = 0; i < desc_blocks; ++i) {
		if (desc_raw)
			munmap(desc_raw, sb->s_blocksize);

		desc_raw = mmap(NULL, sb->s_blocksize, PROT_READ, MAP_PRIVATE,
				fd, (desc_base + i) * sb->s_blocksize);
		if (desc_raw == MAP_FAILED) {
			perror(NULL);
			exit(1);
		}

		if (le32_to_cpu(desc_raw->nx_magic) != APFS_NX_MAGIC)
			continue; /* Not a superblock */
		if (le64_to_cpu(desc_raw->nx_o.o_xid) < sb->s_xid)
			continue; /* Old */
		if (!obj_verify_csum(&desc_raw->nx_o))
			continue; /* Corrupted */

		sb->s_xid = le64_to_cpu(desc_raw->nx_o.o_xid);
		if (sb->s_raw)
			munmap(sb->s_raw, sb->s_blocksize);
		sb->s_raw = desc_raw;
		desc_raw = NULL;
	}

	if (!sb->s_raw)
		report("Checkpoint descriptors", "latest is missing.");
	/* TODO: the latest checkpoint and block zero are somehow different? */
	if (sb->s_xid != le64_to_cpu(msb_raw->nx_o.o_xid))
		report_crash("Block zero");
	if (sb->s_xid + 1 != le64_to_cpu(msb_raw->nx_next_xid))
		report("Container superblock", "next transaction id is wrong.");
	munmap(msb_raw, sb->s_blocksize);
}

/**
 * software_strlen - Calculate the length of a software info string
 * @str: the string
 *
 * Also checks that the string has a proper null-termination, and only null
 * characters afterwards.
 */
static int software_strlen(u8 *str)
{
	int length = strnlen((char *)str, APFS_MODIFIED_NAMELEN);
	u8 *end = str + APFS_MODIFIED_NAMELEN;

	if (length == APFS_MODIFIED_NAMELEN)
		report("Volume software id", "no NULL-termination.");
	for (str += length + 1; str != end; ++str) {
		if (*str)
			report("Volume software id", "goes on after NULL.");
	}
	return length;
}

/**
 * check_software_info - Check the fields reporting implementation info
 * @formatted_by: information about the software that created the volume
 * @modified_by:  information about the software that modified the volume
 */
static void check_software_information(struct apfs_modified_by *formatted_by,
				       struct apfs_modified_by *modified_by)
{
	struct apfs_modified_by *end_mod_by;
	int length;
	bool mods_over;
	u64 xid;

	mods_over = false;
	end_mod_by = modified_by + APFS_MAX_HIST;
	xid = sb->s_xid + 1; /* Last possible xid */

	for (; modified_by != end_mod_by; ++modified_by) {
		length = software_strlen(modified_by->id);
		if (!length &&
		    (modified_by->timestamp || modified_by->last_xid))
			report("Volume modification info", "entry without id.");

		if (mods_over) {
			if (length)
				report("Volume modification info",
				       "empty entry should end the list.");
			continue;
		}
		if (!length) {
			mods_over = true;
			continue;
		}

		/* The first entry must be the most recent */
		if (xid <= le64_to_cpu(modified_by->last_xid))
			report("Volume modification info",
			       "entries are not in order.");
		xid = le64_to_cpu(modified_by->last_xid);
	}

	length = software_strlen(formatted_by->id);
	if (!length)
		report("Volume superblock", "creation information is missing.");

	vsb->v_first_xid = le64_to_cpu(formatted_by->last_xid);
	if (xid <= vsb->v_first_xid)
		report("Volume creation info", "transaction is too recent.");
}

/**
 * map_volume_super - Find the volume superblock and map it into memory
 * @vol:	volume number
 * @vsb:	volume superblock struct to receive the results
 *
 * Returns the in-memory location of the volume superblock, or NULL if there
 * is no volume with this number.
 */
static struct apfs_superblock *map_volume_super(int vol,
						struct volume_superblock *vsb)
{
	struct apfs_nx_superblock *msb_raw = sb->s_raw;
	char *vol_name;
	u64 vol_id;

	vol_id = le64_to_cpu(msb_raw->nx_fs_oid[vol]);
	if (vol_id == 0)
		return NULL;

	vsb->v_raw = read_object(vol_id, sb->s_omap->root, &vsb->v_obj);
	if (vsb->v_obj.type != APFS_OBJECT_TYPE_FS)
		report("Volume superblock", "wrong object type.");
	if (vsb->v_obj.subtype != APFS_OBJECT_TYPE_INVALID)
		report("Volume superblock", "wrong object subtype.");

	if (le32_to_cpu(vsb->v_raw->apfs_magic) != APFS_MAGIC)
		report("Volume superblock", "wrong magic.");

	vsb->v_next_obj_id = le64_to_cpu(vsb->v_raw->apfs_next_obj_id);
	vsb->v_next_doc_id = le32_to_cpu(vsb->v_raw->apfs_next_doc_id);

	vol_name = (char *)vsb->v_raw->apfs_volname;
	if (strnlen(vol_name, APFS_VOLNAME_LEN) == APFS_VOLNAME_LEN)
		report("Volume superblock", "name lacks NULL-termination.");

	check_software_information(&vsb->v_raw->apfs_formatted_by,
				   &vsb->v_raw->apfs_modified_by[0]);

	if (le16_to_cpu(vsb->v_raw->reserved) != 0)
		report("Volume superblock", "reserved field is in use.");
	if (le64_to_cpu(vsb->v_raw->apfs_root_to_xid) != 0)
		report_unknown("Root from snapshot");

	return vsb->v_raw;
}

/**
 * parse_super - Parse the on-disk superblock and check for corruption
 */
void parse_super(void)
{
	int vol;

	sb = malloc(sizeof(*sb));
	if (!sb) {
		perror(NULL);
		exit(1);
	}

	map_main_super();
	/* Check for corruption in the container object map */
	sb->s_omap = parse_omap_btree(le64_to_cpu(sb->s_raw->nx_omap_oid));

	for (vol = 0; vol < APFS_NX_MAX_FILE_SYSTEMS; ++vol) {
		struct apfs_superblock *vsb_raw;

		vsb = calloc(1, sizeof(*vsb));
		if (!vsb) {
			perror(NULL);
			exit(1);
		}
		vsb->v_cnid_table = alloc_htable();
		vsb->v_dstream_table = alloc_htable();
		vsb->v_inode_table = alloc_htable();

		vsb_raw = map_volume_super(vol, vsb);
		if (!vsb_raw) {
			free(vsb);
			break;
		}

		/* Check for corruption in the volume object map... */
		vsb->v_omap = parse_omap_btree(
				le64_to_cpu(vsb_raw->apfs_omap_oid));
		/* ...and in the catalog tree */
		vsb->v_cat = parse_cat_btree(
				le64_to_cpu(vsb_raw->apfs_root_tree_oid),
				vsb->v_omap->root);

		free_inode_table(vsb->v_inode_table);
		vsb->v_inode_table = NULL;
		free_dstream_table(vsb->v_dstream_table);
		vsb->v_dstream_table = NULL;
		free_cnid_table(vsb->v_cnid_table);
		vsb->v_cnid_table = NULL;

		if (!vsb->v_has_root)
			report("Catalog", "the root directory is missing.");
		if (!vsb->v_has_priv)
			report("Catalog", "the private directory is missing.");

		if (le64_to_cpu(vsb_raw->apfs_num_files) !=
							vsb->v_file_count)
			/* Sometimes this is off by one.  TODO: why? */
			report_weird("File count in volume superblock");
		if (le64_to_cpu(vsb_raw->apfs_num_directories) !=
							vsb->v_dir_count)
			report("Volume superblock", "bad directory count.");
		if (le64_to_cpu(vsb_raw->apfs_num_symlinks) !=
							vsb->v_symlink_count)
			report("Volume superblock", "bad symlink count.");
		if (le64_to_cpu(vsb_raw->apfs_num_other_fsobjects) !=
							vsb->v_special_count)
			report("Volume superblock", "bad special file count.");

		sb->s_volumes[vol] = vsb;
	}

	return;
}
