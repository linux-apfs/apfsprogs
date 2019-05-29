/*
 *  apfsprogs/mkapfs/super.c
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <apfs/raw.h>
#include "btree.h"
#include "mkapfs.h"
#include "object.h"
#include "super.h"

/**
 * set_uuid - Set a UUID field
 * @field:	on-disk field to set
 * @uuid:	pointer to the UUID string in standard format
 */
static void set_uuid(char *field, char *uuid)
{
	int ret;
	char *stdformat = "%2hhx%2hhx%2hhx%2hhx-%2hhx%2hhx-%2hhx%2hhx-"
			  "%2hhx%2hhx-%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx";

	ret = sscanf(uuid, stdformat, &field[0], &field[1], &field[2],
				      &field[3], &field[4], &field[5],
				      &field[6], &field[7], &field[8],
				      &field[9], &field[10], &field[11],
				      &field[12], &field[13], &field[14],
				      &field[15]);
	if (ret == 16)
		return;

	printf("Please provide a UUID in standard format.\n");
	exit(1);
}

/**
 * set_checkpoint_areas - Set all sb fields describing the checkpoint areas
 * @sb: pointer to the superblock copy on disk
 */
static void set_checkpoint_areas(struct apfs_nx_superblock *sb)
{
	u32 desc_blocks;
	u64 data_base;

	/* TODO: this should change with the container size, but how much? */
	desc_blocks = 64;

	/* First set the checkpoint descriptor area fields */
	sb->nx_xp_desc_base = cpu_to_le64(CPOINT_DESC_BASE);
	sb->nx_xp_desc_blocks = cpu_to_le32(desc_blocks);
	/* The first two blocks hold the superblock and the mappings */
	sb->nx_xp_desc_len = cpu_to_le32(2);
	sb->nx_xp_desc_next = cpu_to_le32(2);
	sb->nx_xp_desc_index = 0;

	data_base = CPOINT_DESC_BASE + desc_blocks; /* After the descriptors */

	/* Now set the checkpoint data area fields */
	sb->nx_xp_data_base = cpu_to_le64(data_base);
	sb->nx_xp_data_blocks = cpu_to_le32(5904); /* Also hardcoded for now */
	/* Room for the space manager, the two free queues, and the reaper */
	sb->nx_xp_data_len = cpu_to_le32(4);
	sb->nx_xp_data_next = cpu_to_le32(4);
	sb->nx_xp_data_index = 0;
}

/**
 * get_max_volumes - Calculate the maximum number of volumes for the container
 * @size: the container size, in bytes
 */
static u32 get_max_volumes(u64 size)
{
	u32 max_vols;

	/* Divide by 512 MiB and round up, as the reference requires */
	max_vols = DIV_ROUND_UP(size, 512 * 1024 * 1024);
	if (max_vols > APFS_NX_MAX_FILE_SYSTEMS)
		max_vols = APFS_NX_MAX_FILE_SYSTEMS;
	return max_vols;
}

/**
 * set_ephemeral_info - Set the container's array of ephemeral info
 * @info: pointer to the nx_ephemeral_info array on the container superblock
 */
static void set_ephemeral_info(__le64 *info)
{
	/* TODO: add support for small containers */
	u64 min_block_count = APFS_NX_EPH_MIN_BLOCK_COUNT;

	/* Only the first entry is documented, leave the others as zero */
	*info = cpu_to_le64((min_block_count << 32)
			    | (APFS_NX_MAX_FILE_SYSTEM_EPH_STRUCTS << 16)
			    | APFS_NX_EPH_INFO_VERSION_1);
}

/**
 * make_volume - Make a volume
 * @bno: block number for the volume superblock
 * @oid: object id for the volume superblock
 */
static void make_volume(u64 bno, u64 oid)
{
	struct apfs_superblock *vsb = get_zeroed_block(bno);

	vsb->apfs_magic = cpu_to_le32(APFS_MAGIC);

	set_object_header(&vsb->apfs_o, oid,
			  APFS_OBJ_VIRTUAL | APFS_OBJECT_TYPE_FS,
			  APFS_OBJECT_TYPE_INVALID);
	munmap(vsb, param->blocksize);
}

/**
 * make_cpoint_map_block - Make the mapping block for the one checkpoint
 * @bno: block number to use
 */
static void make_cpoint_map_block(u64 bno)
{
	struct apfs_checkpoint_map_phys *block = get_zeroed_block(bno);

	block->cpm_flags = cpu_to_le32(APFS_CHECKPOINT_MAP_LAST);
	block->cpm_count = 0; /* For the moment, we leave it empty */

	set_object_header(&block->cpm_o, bno,
			  APFS_OBJ_PHYSICAL | APFS_OBJECT_TYPE_CHECKPOINT_MAP,
			  APFS_OBJECT_TYPE_INVALID);
	munmap(block, param->blocksize);
}

/**
 * make_container - Make the whole filesystem
 */
void make_container(void)
{
	struct apfs_nx_superblock *sb_copy;
	u64 size = param->blocksize * param->block_count;

	sb_copy = get_zeroed_block(APFS_NX_BLOCK_NUM);

	sb_copy->nx_magic = cpu_to_le32(APFS_NX_MAGIC);
	sb_copy->nx_block_size = cpu_to_le32(param->blocksize);
	sb_copy->nx_block_count = cpu_to_le64(param->block_count);

	/* We only support version 2 of APFS */
	sb_copy->nx_incompatible_features |=
					cpu_to_le64(APFS_NX_INCOMPAT_VERSION2);

	set_uuid(sb_copy->nx_uuid, param->uuid);

	/* Leave some room for the objects created by the mkfs */
	sb_copy->nx_next_oid = cpu_to_le64(APFS_OID_RESERVED_COUNT + 100);
	sb_copy->nx_next_xid = cpu_to_le64(MKFS_XID + 1);

	set_checkpoint_areas(sb_copy);

	sb_copy->nx_spaceman_oid = cpu_to_le64(SPACEMAN_OID);
	sb_copy->nx_reaper_oid = cpu_to_le64(REAPER_OID);
	sb_copy->nx_omap_oid = cpu_to_le64(MAIN_OMAP_BNO);
	make_omap_btree(MAIN_OMAP_BNO, false /* is_vol */);

	sb_copy->nx_max_file_systems = cpu_to_le32(get_max_volumes(size));
	sb_copy->nx_fs_oid[0] = cpu_to_le64(FIRST_VOL_OID);
	make_volume(FIRST_VOL_BNO, FIRST_VOL_OID);

	set_ephemeral_info(&sb_copy->nx_ephemeral_info[0]);

	make_cpoint_map_block(CPOINT_DESC_BASE);

	set_object_header(&sb_copy->nx_o, APFS_OID_NX_SUPERBLOCK,
			  APFS_OBJ_EPHEMERAL | APFS_OBJECT_TYPE_NX_SUPERBLOCK,
			  APFS_OBJECT_TYPE_INVALID);
	munmap(sb_copy, param->blocksize);
}
