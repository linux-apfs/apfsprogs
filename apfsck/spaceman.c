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
 * reads them into the in-memory container superblock.  Also calculates the
 * total number of chunks and cibs in the container.
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

	sb->sm_chunk_count = DIV_ROUND_UP(sb->s_block_count,
					  sb->sm_blocks_per_chunk);
	sb->sm_cib_count = DIV_ROUND_UP(sb->sm_chunk_count,
					sb->sm_chunks_per_cib);
}

/**
 * parse_chunk_info_block - Parse and check a chunk-info block
 * @bno:	block number of the chunk-info block
 * @index:	index of the chunk-info block
 */
static void parse_chunk_info_block(u64 bno, int index)
{
	struct object obj;
	struct apfs_chunk_info_block *cib;

	cib = read_object(bno, NULL, &obj);
	if (obj.type != APFS_OBJECT_TYPE_SPACEMAN_CIB)
		report("Chunk-info block", "wrong object type.");
	if (obj.subtype != APFS_OBJECT_TYPE_INVALID)
		report("Chunk-info block", "wrong object subtype.");

	if (le32_to_cpu(cib->cib_index) != index)
		report("Chunk-info block", "wrong index.");
	munmap(cib, sb->s_blocksize);
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
	if (offset >= sb->s_blocksize || offset + sizeof(u64) > sb->s_blocksize)
		report("Spaceman device", "address is out of bounds.");
	return *((u64 *)addr_p);
}

/**
 * parse_spaceman_main_device - Parse and check the spaceman main device struct
 * @sm: pointer to the raw space manager
 */
static void parse_spaceman_main_device(struct apfs_spaceman_phys *sm)
{
	struct apfs_spaceman_device *dev = &sm->sm_dev[APFS_SD_MAIN];
	u32 addr_off;
	int i;

	if (dev->sm_cab_count)
		report_unknown("Chunk-info address block");
	if (le32_to_cpu(dev->sm_cib_count) != sb->sm_cib_count)
		report("Spaceman device", "wrong count of chunk-info blocks.");
	if (le32_to_cpu(dev->sm_chunk_count) != sb->sm_chunk_count)
		report("Spaceman device", "wrong count of chunks.");
	if (le32_to_cpu(dev->sm_block_count) != sb->s_block_count)
		report("Spaceman device", "wrong block count.");

	addr_off = le64_to_cpu(dev->sm_addr_offset);
	for (i = 0; i < sb->sm_cib_count; ++i) {
		u64 bno = get_cib_address(sm, addr_off + i * sizeof(u64));

		parse_chunk_info_block(bno, i);
	}

	if (dev->sm_reserved || dev->sm_reserved2)
		report("Spaceman device", "non-zero padding.");
}

/**
 * check_spaceman_tier2_device - Check that the second-tier device is empty
 * @sm: pointer to the raw space manager
 */
static void check_spaceman_tier2_device(struct apfs_spaceman_phys *sm)
{
	struct apfs_spaceman_device *main_dev = &sm->sm_dev[APFS_SD_MAIN];
	struct apfs_spaceman_device *dev = &sm->sm_dev[APFS_SD_TIER2];
	u32 addr_off, main_addr_off;

	addr_off = le64_to_cpu(dev->sm_addr_offset);
	main_addr_off = le32_to_cpu(main_dev->sm_addr_offset);
	if (addr_off != main_addr_off + sb->sm_cib_count * sizeof(u64))
		report("Spaceman device", "not consecutive address offsets.");
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
