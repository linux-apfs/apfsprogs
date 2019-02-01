/*
 *  apfsprogs/apfsck/super.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "btree.h"
#include "globals.h"
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

	if (le32_to_cpu(msb_raw->nx_magic) != APFS_NX_MAGIC) {
		printf("Wrong magic on block zero.\n");
		exit(1);
	}
	if (!obj_verify_csum(&msb_raw->nx_o)) {
		printf("Bad checksum for block zero.\n");
		exit(1);
	}

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
	u64 xid;
	u64 desc_base;
	u32 desc_blocks;
	int i;

	/* Read the superblock from the last clean unmount */
	msb_raw = read_super_copy();

	/* We want to mount the latest valid checkpoint among the descriptors */
	desc_base = le64_to_cpu(msb_raw->nx_xp_desc_base);
	if (desc_base >> 63 != 0) {
		/* The highest bit is set when checkpoints are not contiguous */
		printf("Checkpoint descriptor tree not yet supported.\n");
		exit(1);
	}
	desc_blocks = le32_to_cpu(msb_raw->nx_xp_desc_blocks);
	if (desc_blocks > 10000) { /* Arbitrary loop limit, is it enough? */
		printf("Too many checkpoint descriptors?\n");
		exit(1);
	}

	/* Now we go through the checkpoints one by one */
	sb->s_raw = NULL;
	xid = le64_to_cpu(msb_raw->nx_o.o_xid);
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
		if (le64_to_cpu(desc_raw->nx_o.o_xid) < xid)
			continue; /* Old */
		if (!obj_verify_csum(&desc_raw->nx_o))
			continue; /* Corrupted */

		xid = le64_to_cpu(desc_raw->nx_o.o_xid);
		if (sb->s_raw)
			munmap(sb->s_raw, sb->s_blocksize);
		sb->s_raw = desc_raw;
		desc_raw = NULL;
	}

	if (!sb->s_raw) {
		printf("Missing latest checkpoint superblock.\n");
		exit(1);
	}
	/* TODO: the latest checkpoint and block zero are somehow different? */
	if (xid != le64_to_cpu(msb_raw->nx_o.o_xid))
		printf("The filesystem was not unmounted cleanly.\n");
	munmap(msb_raw, sb->s_blocksize);
}

/**
 * map_volume_super - Find the volume superblock and map it into memory
 * @vol:	volume number
 *
 * Returns the in-memory location of the volume superblock, or NULL if there
 * is no volume with this number.
 */
static struct apfs_superblock *map_volume_super(int vol)
{
	struct apfs_nx_superblock *msb_raw = sb->s_raw;
	struct apfs_superblock *vsb_raw;
	u64 vol_id;
	u64 vsb_bno;

	vol_id = le64_to_cpu(msb_raw->nx_fs_oid[vol]);
	if (vol_id == 0)
		return NULL;

	vsb_bno = omap_lookup_block(sb->s_omap_root, vol_id);

	vsb_raw = mmap(NULL, sb->s_blocksize, PROT_READ, MAP_PRIVATE,
		       fd, vsb_bno * sb->s_blocksize);
	if (vsb_raw == MAP_FAILED) {
		perror(NULL);
		exit(1);
	}

	if (le64_to_cpu(vsb_raw->apfs_o.o_oid) != vol_id) {
		printf("Wrong object id for volume superblock.\n");
		exit(1);
	}
	if (le32_to_cpu(vsb_raw->apfs_magic) != APFS_MAGIC) {
		printf("Wrong magic in volume superblock.\n");
		exit(1);
	}
	if (!obj_verify_csum(&vsb_raw->apfs_o)) {
		printf("Bad checksum for volume superblock.\n");
		exit(1);
	}

	return vsb_raw;
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
	sb->s_omap_root = parse_omap_btree(le64_to_cpu(sb->s_raw->nx_omap_oid));

	for (vol = 0; vol < APFS_NX_MAX_FILE_SYSTEMS; ++vol) {
		struct apfs_superblock *vsb_raw;

		vsb_raw = map_volume_super(vol);
		if (!vsb_raw)
			break;

		vsb = malloc(sizeof(*vsb));
		if (!vsb) {
			perror(NULL);
			exit(1);
		}
		vsb->v_raw = vsb_raw;

		/* Check for corruption in the volume object map... */
		vsb->v_omap_root = parse_omap_btree(
				le64_to_cpu(vsb_raw->apfs_omap_oid));
		/* ...and in the catalog tree */
		vsb->v_cat_root = parse_cat_btree(
				le64_to_cpu(vsb_raw->apfs_root_tree_oid),
				vsb->v_omap_root);

		sb->s_volumes[vol] = vsb;
	}

	return;
}
