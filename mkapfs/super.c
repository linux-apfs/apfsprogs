/*
 *  apfsprogs/mkapfs/super.c
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <apfs/raw.h>
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
	u64 desc_base = APFS_NX_BLOCK_NUM + 1; /* Right after the super copy */
	u32 desc_blocks;
	u64 data_base;

	/* TODO: this should change with the container size, but how much? */
	desc_blocks = 64;

	/* First set the checkpoint descriptor area fields */
	sb->nx_xp_desc_base = cpu_to_le64(desc_base);
	sb->nx_xp_desc_blocks = cpu_to_le32(desc_blocks);
	/* The first two blocks hold the superblock and the mappings */
	sb->nx_xp_desc_len = cpu_to_le32(2);
	sb->nx_xp_desc_next = cpu_to_le32(2);
	sb->nx_xp_desc_index = 0;

	data_base = desc_base + desc_blocks; /* Right after the descriptors */

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
 * make_container - Make the whole filesystem
 */
void make_container(void)
{
	struct apfs_nx_superblock *sb_copy;
	u64 size = param->blocksize * param->block_count;

	sb_copy = mmap(NULL, param->blocksize, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, APFS_NX_BLOCK_NUM * param->blocksize);
	if (sb_copy == MAP_FAILED)
		system_error();
	memset(sb_copy, 0, param->blocksize);

	sb_copy->nx_magic = cpu_to_le32(APFS_NX_MAGIC);
	sb_copy->nx_block_size = cpu_to_le32(param->blocksize);
	sb_copy->nx_block_count = cpu_to_le64(param->block_count);

	/* We only support version 2 of APFS */
	sb_copy->nx_incompatible_features |=
					cpu_to_le64(APFS_NX_INCOMPAT_VERSION2);

	set_uuid(sb_copy->nx_uuid, param->uuid);

	/* Leave some room for the objects created by the mkfs */
	sb_copy->nx_next_oid = cpu_to_le64(APFS_OID_RESERVED_COUNT + 100);
	/* The first valid transaction is for the mkfs */
	sb_copy->nx_next_xid = cpu_to_le64(2);

	set_checkpoint_areas(sb_copy);

	sb_copy->nx_spaceman_oid = cpu_to_le64(SPACEMAN_OID);
	sb_copy->nx_omap_oid = cpu_to_le64(MAIN_OMAP_BNO);
	sb_copy->nx_reaper_oid = cpu_to_le64(REAPER_OID);

	sb_copy->nx_max_file_systems = cpu_to_le32(get_max_volumes(size));

	set_object_header(&sb_copy->nx_o, APFS_OID_NX_SUPERBLOCK,
			  APFS_OBJ_EPHEMERAL | APFS_OBJECT_TYPE_NX_SUPERBLOCK,
			  APFS_OBJECT_TYPE_INVALID);
	munmap(sb_copy, param->blocksize);
}
