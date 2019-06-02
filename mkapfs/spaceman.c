/*
 *  apfsprogs/mkapfs/spaceman.c
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
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

/*
 * Offsets into the spaceman block for a non-versioned container; the values
 * have been borrowed from a test image.
 */
#define BITMAP_XID_OFF		0x150	/* Transaction id for the ip bitmap */
#define BITMAP_OFF		0x158	/* Address of the ip bitmap */
#define BITMAP_FREE_NEXT_OFF	0x160	/* No idea */
#define CIB_ADDR_BASE_OFF	0x180	/* First cib address for main device */

/**
 * make_devices - Make the spaceman device structures
 * @sm: pointer to the on-disk spaceman structure
 */
static void make_devices(struct apfs_spaceman_phys *sm)
{
	struct apfs_spaceman_device *dev = &sm->sm_dev[APFS_SD_MAIN];
	u64 chunk_count = DIV_ROUND_UP(param->block_count, blocks_per_chunk());
	u32 cib_count = DIV_ROUND_UP(chunk_count, chunks_per_cib());
	__le64 *cib_addr;
	int i;

	/*
	 * We must have room for the addresses of all main device cibs, plus
	 * an extra offset for tier 2.
	 */
	if (cib_count + 1 >
	    (param->blocksize - CIB_ADDR_BASE_OFF) / sizeof(__le64)) {
		printf("Large containers are not yet supported.\n");
		exit(1);
	}

	dev->sm_block_count = cpu_to_le64(param->block_count);
	dev->sm_chunk_count = cpu_to_le64(chunk_count);
	dev->sm_cib_count = cpu_to_le32(cib_count);
	dev->sm_cab_count = 0; /* Not supported, hence the block count limit */

	/* Pretend the whole device is free for now; we'll reduce this later */
	dev->sm_free_count = cpu_to_le64(param->block_count);

	dev->sm_addr_offset = cpu_to_le32(CIB_ADDR_BASE_OFF);
	cib_addr = (void *)sm + CIB_ADDR_BASE_OFF;
	for (i = 0; i < cib_count; ++i)
		cib_addr[i] = cpu_to_le64(FIRST_CIB_BNO + i);

	/* For the tier2 device, just set the offset; the address is null */
	dev = &sm->sm_dev[APFS_SD_TIER2];
	dev->sm_addr_offset = cpu_to_le32(CIB_ADDR_BASE_OFF +
					  cib_count * sizeof(__le64));
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

	make_devices(sm);

	set_object_header(&sm->sm_o, oid,
			  APFS_OBJ_EPHEMERAL | APFS_OBJECT_TYPE_SPACEMAN,
			  APFS_OBJECT_TYPE_INVALID);
	munmap(sm, param->blocksize);
}
