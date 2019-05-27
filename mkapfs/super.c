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
 * make_container - Make the whole filesystem
 * @param: parameters for the filesystem
 */
void make_container(struct parameters *param)
{
	struct apfs_nx_superblock *sb_copy;

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

	munmap(sb_copy, param->blocksize);
}
