/*
 *  apfsprogs/mkapfs/super.c
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <apfs/raw.h>
#include "mkapfs.h"
#include "super.h"

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

	munmap(sb_copy, param->blocksize);
}
