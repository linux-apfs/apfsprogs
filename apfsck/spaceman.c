/*
 *  apfsprogs/apfsck/spaceman.c
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <sys/mman.h>
#include "apfsck.h"
#include "object.h"
#include "spaceman.h"
#include "super.h"

/**
 * parse_spaceman_chunk_counts - Parse spaceman fields for chunk-related counts
 * @sm: pointer to the raw spaceman structure
 *
 * Checks the counts of blocks per chunk, chunks per cib, and cibs per cab, and
 * reads them into the in-memory container superblock.
 */
static void parse_spaceman_chunk_counts(struct apfs_spaceman_phys *sm)
{
	int chunk_info_size = sizeof(struct apfs_chunk_info);
	int cib_size = sizeof(struct apfs_chunk_info_block);
	int cab_size = sizeof(struct apfs_cib_addr_block);

	sb->sm_blocks_per_chunk = le32_to_cpu(sm->sm_blocks_per_chunk);
	if (sb->sm_blocks_per_chunk != 8 * sb->s_blocksize)
		/* One bitmap block for each chunk */
		report("Space manager", "wrong count of blocks per chunk.");

	sb->sm_chunks_per_cib = (sb->s_blocksize - cib_size) / chunk_info_size;
	if (le32_to_cpu(sm->sm_chunks_per_cib) != sb->sm_chunks_per_cib)
		report("Space manager", "wrong count of chunks per cib.");

	sb->sm_cibs_per_cab = (sb->s_blocksize - cab_size) / sizeof(__le64);
	if (le32_to_cpu(sm->sm_cibs_per_cab) != sb->sm_cibs_per_cab)
		report("Space manager", "wrong count of cibs per cab.");
}

/**
 * check_spaceman - Check the space manager structures for a container
 * @oid: ephemeral object id for the spaceman structure
 */
void check_spaceman(u64 oid)
{
	struct object obj;
	struct apfs_spaceman_phys *raw;

	raw = read_ephemeral_object(oid, &obj);
	if (obj.type != APFS_OBJECT_TYPE_SPACEMAN)
		report("Space manager", "wrong object type.");
	if (obj.subtype != APFS_OBJECT_TYPE_INVALID)
		report("Space manager", "wrong object subtype.");

	if (le32_to_cpu(raw->sm_block_size) != sb->s_blocksize)
		report("Space manager", "wrong block size.");
	parse_spaceman_chunk_counts(raw);

	munmap(raw, sb->s_blocksize);
}
