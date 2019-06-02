/*
 *  apfsprogs/mkapfs/spaceman.c
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <apfs/raw.h>
#include <apfs/types.h>
#include "mkapfs.h"
#include "object.h"
#include "spaceman.h"

/**
 * blocks_per_chunk - Calculate the number of blocks per chunk
 */
static inline u32 blocks_per_chunk(void)
{
	return 8 * param->blocksize; /* One bitmap block for each chunk */
}

/**
 * chunks_per_cib - Calculate the number of chunks per chunk-info block
 */
static inline u32 chunks_per_cib(void)
{
	int chunk_info_size = sizeof(struct apfs_chunk_info);
	int cib_size = sizeof(struct apfs_chunk_info_block);

	return (param->blocksize - cib_size) / chunk_info_size;
}

/**
 * cibs_per_cab - Calculate the count of chunk-info blocks per cib address block
 */
static inline u32 cibs_per_cab(void)
{
	int cab_size = sizeof(struct apfs_cib_addr_block);

	return (param->blocksize - cab_size) / sizeof(__le64);
}

/**
 * make_spaceman - Make the space manager for the container
 * @bno: block number to use
 * @oid: object id
 */
void make_spaceman(u64 bno, u64 oid)
{
	struct apfs_spaceman_phys *sm = get_zeroed_block(bno);

	sm->sm_block_size = cpu_to_le32(param->blocksize);
	sm->sm_blocks_per_chunk = cpu_to_le32(blocks_per_chunk());
	sm->sm_chunks_per_cib = cpu_to_le32(chunks_per_cib());
	sm->sm_cibs_per_cab = cpu_to_le32(cibs_per_cab());

	set_object_header(&sm->sm_o, oid,
			  APFS_OBJ_EPHEMERAL | APFS_OBJECT_TYPE_SPACEMAN,
			  APFS_OBJECT_TYPE_INVALID);
	munmap(sm, param->blocksize);
}
