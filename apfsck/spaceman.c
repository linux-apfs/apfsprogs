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
 * parse_chunk_info_block - Parse and check a chunk-info block
 * @cib: the raw chunk-info block
 */
static void parse_chunk_info_block(struct apfs_chunk_info_block *cib)
{
	if (cib->cib_index) /* The cib's index in the nonexistent cab */
		report("Chunk-info block", "wrong index.");
}

/**
 * get_cib_address - Get the block number of a device's cib (or of its cab)
 * @sm:		pointer to the raw space manager
 * @offset:	offset of the cib address in @sm
 */
static u64 get_cib_address(struct apfs_spaceman_phys *sm, u32 offset)
{
	char *addr_p = (char *)sm + offset;

	if (offset & 0x7)
		report("Spaceman device", "address is not aligned to 8 bytes.");
	return *((u64 *)addr_p);
}

/**
 * parse_spaceman_main_device - Parse and check the spaceman main device struct
 * @sm: pointer to the raw space manager
 */
static void parse_spaceman_main_device(struct apfs_spaceman_phys *sm)
{
	struct apfs_spaceman_device *dev = &sm->sm_dev[APFS_SD_MAIN];
	struct object obj;
	void *raw; /* May be a cib or a cab */
	u32 addr_off;

	addr_off = le64_to_cpu(dev->sm_addr_offset);
	raw = read_object(get_cib_address(sm, addr_off), NULL, &obj);
	if (obj.type != APFS_OBJECT_TYPE_SPACEMAN_CIB) {
		/* No chunk-info address blocks have been encountered so far */
		if (obj.type == APFS_OBJECT_TYPE_SPACEMAN_CAB)
			report_unknown("Chunk-info address block");
		else
			report("Chunk-info block", "wrong object type.");
	}
	if (obj.subtype != APFS_OBJECT_TYPE_INVALID)
		report("Chunk-info block", "wrong object subtype.");

	parse_chunk_info_block(raw);
	munmap(raw, sb->s_blocksize);

	if (dev->sm_reserved || dev->sm_reserved2)
		report("Spaceman device", "non-zero padding.");
}

/**
 * check_spaceman_tier2_device - Check that the second-tier device is empty
 * @sm: pointer to the raw space manager
 */
static void check_spaceman_tier2_device(struct apfs_spaceman_phys *sm)
{
	struct apfs_spaceman_device *dev = &sm->sm_dev[APFS_SD_TIER2];
	u32 addr_off;

	addr_off = le64_to_cpu(dev->sm_addr_offset);
	if (get_cib_address(sm, addr_off)) /* Empty device has no cib */
		report_unknown("Fusion drive");

	if (dev->sm_block_count || dev->sm_chunk_count || dev->sm_cib_count ||
	    dev->sm_cab_count || dev->sm_free_count)
		report_unknown("Fusion drive");
	if (dev->sm_reserved || dev->sm_reserved2)
		report("Spaceman device", "non-zero padding.");
}

/**
 * check_spaceman - Check the space manager structures for a container
 * @oid: ephemeral object id for the spaceman structure
 */
void check_spaceman(u64 oid)
{
	struct object obj;
	struct apfs_spaceman_phys *raw;
	u32 flags;

	raw = read_ephemeral_object(oid, &obj);
	if (obj.type != APFS_OBJECT_TYPE_SPACEMAN)
		report("Space manager", "wrong object type.");
	if (obj.subtype != APFS_OBJECT_TYPE_INVALID)
		report("Space manager", "wrong object subtype.");

	if (le32_to_cpu(raw->sm_block_size) != sb->s_blocksize)
		report("Space manager", "wrong block size.");
	parse_spaceman_chunk_counts(raw);

	parse_spaceman_main_device(raw);
	check_spaceman_tier2_device(raw);

	/* TODO: handle the undocumented 'versioned' flag */
	flags = le32_to_cpu(raw->sm_flags);
	if ((flags & APFS_SM_FLAGS_VALID_MASK) != flags)
		report("Space manager", "invalid flag in use.");

	munmap(raw, sb->s_blocksize);
}
