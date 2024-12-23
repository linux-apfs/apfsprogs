/*
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <apfs/parameters.h>
#include <apfs/raw.h>
#include "apfsck.h"
#include "btree.h"
#include "key.h"
#include "object.h"
#include "spaceman.h"
#include "super.h"

/**
 * block_in_ip - Does this block belong to the internal pool?
 * @bno: block number to check
 */
static inline bool block_in_ip(u64 bno)
{
	struct spaceman *sm = &sb->s_spaceman;
	u64 start = sm->sm_ip_base;
	u64 end = start + sm->sm_ip_block_count;

	return bno >= start && bno < end;
}

/**
 * range_in_ip - Is this range included in the internal pool?
 * @paddr:	first block of the range
 * @length:	length of the range
 */
static bool range_in_ip(u64 paddr, u64 length)
{
	u64 last = paddr + length - 1;
	bool first_in_ip = block_in_ip(paddr);
	bool last_in_ip = block_in_ip(last);

	if ((first_in_ip && !last_in_ip) || (!first_in_ip && last_in_ip))
		report("Free queue record", "internal pool is overrun.");
	return first_in_ip;
}

/**
 * bmap_mark_as_used - Set a range to ones in a bitmap
 * @bitmap:	the bitmap
 * @paddr:	first block number
 * @length:	block count
 *
 * Checks that an address range is still zeroed in the given bitmap, and then
 * switches those bits.
 */
static void bmap_mark_as_used(u64 *bitmap, u64 paddr, u64 length)
{
	u64 *byte;
	u64 flag;
	u64 i;

	for (i = paddr; i < paddr + length; ++i) {
		byte = bitmap + i / 64;
		flag = 1ULL << i % 64;
		if (*byte & flag)
			report(NULL /* context */, "A block is used twice.");
		*byte |= flag;
	}
}

/**
 * ip_bmap_mark_as_used - Mark a range as used in the ip allocation bitmap
 * @paddr:	first block number
 * @length:	block count
 *
 * Checks that the given address range is still marked as free in the internal
 * pool's allocation bitmap, and then switches those bits.
 */
void ip_bmap_mark_as_used(u64 paddr, u64 length)
{
	if (!range_in_ip(paddr, length))
		report(NULL /* context */, "Out-of-range ip block number.");
	paddr -= sb->s_spaceman.sm_ip_base;
	bmap_mark_as_used(sb->s_ip_bitmap, paddr, length);
}

/**
 * container_bmap_mark_as_used - Mark a range as used in the allocation bitmap
 * @paddr:	first block number
 * @length:	block count
 *
 * Checks that the given address range is still marked as free in the
 * container's allocation bitmap, and then switches those bits.
 */
void container_bmap_mark_as_used(u64 paddr, u64 length)
{
	u64 tier2_blkno, max_dev_blkcnt;
	bool tier2;
	void *bitmap = NULL;

	tier2_blkno = APFS_FUSION_TIER2_DEVICE_BYTE_ADDR >> sb->s_blocksize_bits;
	tier2 = paddr >= tier2_blkno;
	if (tier2)
		paddr -= tier2_blkno;
	max_dev_blkcnt = tier2 ? sb->s_max_tier2_blkcnt : sb->s_max_main_blkcnt;
	bitmap = tier2 ? sb->s_tier2_bitmap : sb->s_main_bitmap;

	/* Avoid out-of-bounds writes to the allocation bitmap */
	if (paddr + length > max_dev_blkcnt || paddr + length < paddr)
		report(NULL /* context */, "Out-of-range block number.");

	bmap_mark_as_used(bitmap, paddr, length);
}

/**
 * parse_spaceman_chunk_counts - Parse spaceman fields for chunk-related counts
 * @raw: pointer to the raw spaceman structure
 *
 * Checks the counts of blocks per chunk, chunks per cib, and cibs per cab, and
 * reads them into the in-memory container superblock.
 */
static void parse_spaceman_chunk_counts(struct apfs_spaceman_phys *raw)
{
	struct spaceman *sm = &sb->s_spaceman;
	int chunk_info_size = sizeof(struct apfs_chunk_info);
	int cib_size = sizeof(struct apfs_chunk_info_block);
	int cab_size = sizeof(struct apfs_cib_addr_block);

	sm->sm_blocks_per_chunk = le32_to_cpu(raw->sm_blocks_per_chunk);
	if (sm->sm_blocks_per_chunk != 8 * sb->s_blocksize)
		/* One bitmap block for each chunk */
		report("Space manager", "wrong count of blocks per chunk.");

	sm->sm_chunks_per_cib = (sb->s_blocksize - cib_size) / chunk_info_size;
	if (le32_to_cpu(raw->sm_chunks_per_cib) != sm->sm_chunks_per_cib)
		report("Space manager", "wrong count of chunks per cib.");

	sm->sm_cibs_per_cab = (sb->s_blocksize - cab_size) / sizeof(__le64);
	if (le32_to_cpu(raw->sm_cibs_per_cab) != sm->sm_cibs_per_cab)
		report("Space manager", "wrong count of cibs per cab.");
}

/**
 * read_chunk_bitmap - Read a chunk's bitmap into memory
 * @addr: first block number for the chunk
 * @bmap: block number for the chunk's bitmap, or zero if the chunk is all free
 * @dev: device that owns this chunk
 *
 * Returns a pointer to the chunk's bitmap, read into its proper position
 * within the in-memory bitmap for the container.
 */
static void *read_chunk_bitmap(u64 addr, u64 bmap, struct spaceman_device *dev)
{
	struct spaceman *sm = &sb->s_spaceman;
	ssize_t read_bytes;
	void *buf, *ret;
	size_t count;
	off_t offset;
	u32 chunk_number;

	assert(dev->sm_bitmap);

	/* Prevent out-of-bounds writes to sm->sm_bitmap */
	if (addr & (sm->sm_blocks_per_chunk - 1))
		report("Chunk-info", "chunk address isn't multiple of size.");
	chunk_number = addr / sm->sm_blocks_per_chunk;
	if (addr >= sb->s_block_count)
		report("Chunk-info", "chunk address is out of bounds.");

	ret = buf = dev->sm_bitmap + chunk_number * sb->s_blocksize;
	if (!bmap) /* The whole chunk is free, so leave this block as zero */
		return ret;

	count = sb->s_blocksize;
	offset = bmap * sb->s_blocksize;
	do {
		read_bytes = apfs_pread(buf, count, offset);
		if (read_bytes < 0)
			system_error();
		buf += read_bytes;
		count -= read_bytes;
		offset += read_bytes;
	} while (read_bytes > 0);

	/* Mark the bitmap block as used in the actual allocation bitmap */
	ip_bmap_mark_as_used(bmap, 1 /* length */);
	return ret;
}

/**
 * count_chunk_free - Count the free blocks in a chunk
 * @bmap: pointer to the chunk's bitmap
 * @blks: number of blocks in the chunk
 */
static int count_chunk_free(void *bmap, u32 blks)
{
	unsigned long long *curr, *end;
	int free = blks;

	end = bmap + sb->s_blocksize;
	for (curr = bmap; curr < end; ++curr)
		free -= __builtin_popcountll(*curr);
	return free;
}

/**
 * parse_chunk_info - Parse and check a chunk info structure
 * @chunk:	pointer to the raw chunk info structure
 * @is_last:	is this the last chunk of the device?
 * @start:	expected first block number for the chunk
 * @xid:	on return, the transaction id of the chunk
 * @dev:	device that owns this chunk
 *
 * Returns the first block number for the next chunk.
 */
static u64 parse_chunk_info(struct apfs_chunk_info *chunk, bool is_last, u64 start, u64 *xid, struct spaceman_device *dev)
{
	struct spaceman *sm = &sb->s_spaceman;
	u32 block_count;
	void *bitmap;
	u32 free_count;

	block_count = le32_to_cpu(chunk->ci_block_count);
	if (!block_count)
		report("Chunk-info", "has no blocks.");
	if (block_count > sm->sm_blocks_per_chunk)
		report("Chunk-info", "too many blocks.");
	if (!is_last && block_count != sm->sm_blocks_per_chunk)
		report("Chunk-info", "too few blocks.");
	dev->sm_blocks += block_count;

	if (le64_to_cpu(chunk->ci_addr) != start)
		report("Chunk-info block", "chunks are not consecutive.");
	bitmap = read_chunk_bitmap(start, le64_to_cpu(chunk->ci_bitmap_addr), dev);

	free_count = le32_to_cpu(chunk->ci_free_count);
	if (free_count != count_chunk_free(bitmap, block_count))
		report("Chunk-info", "wrong count of free blocks.");
	dev->sm_free += free_count;

	*xid = le64_to_cpu(chunk->ci_xid);
	if (!*xid)
		report("Chunk-info", "bad transaction id.");
	return start + block_count;
}

/**
 * parse_chunk_info_block - Parse and check a chunk-info block
 * @bno:	block number of the chunk-info block
 * @index:	index of the chunk-info block
 * @start:	expected first block number for the first chunk
 * @xid_p:	on return, the transaction id of the cib (ignored if NULL)
 * @dev:	device that owns this cib
 *
 * Returns the first block number for the first chunk of the next cib.
 */
static u64 parse_chunk_info_block(u64 bno, u32 index, u64 start, u64 *xid_p, struct spaceman_device *dev)
{
	struct spaceman *sm = &sb->s_spaceman;
	struct object obj;
	struct apfs_chunk_info_block *cib;
	u32 chunk_count;
	bool last_cib = index == dev->sm_cib_count - 1;
	u64 max_chunk_xid = 0;
	int i;

	cib = read_object(bno, NULL, &obj);
	if (obj.type != APFS_OBJECT_TYPE_SPACEMAN_CIB)
		report("Chunk-info block", "wrong object type.");
	if (obj.subtype != APFS_OBJECT_TYPE_INVALID)
		report("Chunk-info block", "wrong object subtype.");
	if (obj.xid > sm->sm_xid) /* Cib address is stored in the spaceman */
		report("Chunk-info block", "xid is more recent than spaceman.");

	if (le32_to_cpu(cib->cib_index) != index)
		report("Chunk-info block", "wrong index.");

	chunk_count = le32_to_cpu(cib->cib_chunk_info_count);
	if (!chunk_count)
		report("Chunk-info block", "has no chunks.");
	if (chunk_count > sm->sm_chunks_per_cib)
		report("Chunk-info block", "too many chunks.");
	if (!last_cib && chunk_count != sm->sm_chunks_per_cib)
		report("Chunk-info block", "too few chunks.");
	dev->sm_chunks += chunk_count;

	for (i = 0; i < chunk_count; ++i) {
		bool last_block = false;
		u64 chunk_xid;

		if (last_cib && i == chunk_count - 1)
			last_block = true;
		start = parse_chunk_info(&cib->cib_chunk_info[i], last_block, start, &chunk_xid, dev);

		if (chunk_xid > obj.xid)
			report("Chunk-info", "xid is too recent.");
		if (chunk_xid > max_chunk_xid)
			max_chunk_xid = chunk_xid;
	}
	if (obj.xid != max_chunk_xid) /* Cib only changes if a chunk changes */
		report("Chunk-info block", "xid is too recent.");
	if (xid_p)
		*xid_p = obj.xid;

	munmap(cib, obj.size);
	return start;
}

/**
 * parse_cib_addr_block - Parse and check a chunk-info address block
 * @bno:	block number of the chunk-info address block
 * @index:	index of the chunk-info address block
 * @start:	expected first block number for the first chunk
 * @dev:	device that owns this cab
 *
 * Returns the first block number for the first chunk of the next cab.
 */
static u64 parse_cib_addr_block(u64 bno, u32 index, u64 start, struct spaceman_device *dev)
{
	struct spaceman *sm = &sb->s_spaceman;
	struct object obj;
	struct apfs_cib_addr_block *cab = NULL;
	u32 cib_count;
	bool last_cab = index == dev->sm_cab_count - 1;
	u64 max_cib_xid = 0;
	int i;

	cab = read_object(bno, NULL, &obj);
	if (obj.type != APFS_OBJECT_TYPE_SPACEMAN_CAB)
		report("Cib address block", "wrong object type.");
	if (obj.subtype != APFS_OBJECT_TYPE_INVALID)
		report("Cib address block", "wrong object subtype.");
	if (obj.xid > sm->sm_xid) /* Cab address is stored in the spaceman */
		report("Cib address block", "xid is more recent than spaceman.");

	if (le32_to_cpu(cab->cab_index) != index)
		report("Cib address block", "wrong index.");

	cib_count = le32_to_cpu(cab->cab_cib_count);
	if (!cib_count)
		report("Cib address block", "has no cibs.");
	if (cib_count > sm->sm_cibs_per_cab)
		report("Cib address block", "too many cibs.");
	if (!last_cab && cib_count != sm->sm_cibs_per_cab)
		report("Cib address block", "too few cibs.");
	dev->sm_cibs += cib_count;

	for (i = 0; i < cib_count; ++i) {
		u64 cib_xid;

		start = parse_chunk_info_block(le64_to_cpu(cab->cab_cib_addr[i]), sm->sm_cibs_per_cab * index + i, start, &cib_xid, dev);

		if (cib_xid > obj.xid)
			report("Chunk-info block", "xid is too recent.");
		if (cib_xid > max_cib_xid)
			max_cib_xid = cib_xid;
	}
	if (obj.xid != max_cib_xid) /* Cab only changes if a cib changes */
		report("Cib address block", "xid is too recent.");

	munmap(cab, obj.size);
	return start;
}

/**
 * spaceman_val_from_off - Get the value stored on a given spaceman offset
 * @raw:	pointer to the raw space manager
 * @offset:	offset of the value in @raw
 *
 * This is not in the official documentation and I didn't figure it out myself.
 * Credit should go to Joachim Metz: <https://github.com/libyal/libfsapfs>.
 *
 * TODO: check that no values found by this function overlap with each other.
 */
static u64 spaceman_val_from_off(struct apfs_spaceman_phys *raw, u32 offset)
{
	struct spaceman *sm = &sb->s_spaceman;
	char *value_p = (char *)raw + offset;

	assert(sm->sm_struct_size);

	if (offset & 0x7)
		report("Spaceman", "offset is not aligned to 8 bytes.");
	if (offset < sm->sm_struct_size)
		report("Spaceman", "offset overlaps with structure.");
	if (offset >= sm->sm_obj_size || offset + sizeof(u64) > sm->sm_obj_size)
		report("Spaceman", "offset is out of bounds.");
	return *((u64 *)value_p);
}

/**
 * spaceman_16_from_off - Get the 16 bits stored on a given spaceman offset
 * @raw:	pointer to the raw space manager
 * @offset:	offset of the value in @raw
 *
 * TODO: check that no values found by this function overlap with each other.
 */
static u16 spaceman_16_from_off(struct apfs_spaceman_phys *raw, u32 offset)
{
	struct spaceman *sm = &sb->s_spaceman;
	char *value_p = (char *)raw + offset;

	assert(sm->sm_struct_size);

	if (offset & 0x1)
		report("Spaceman", "offset is not aligned to 2 bytes.");
	if (offset < sm->sm_struct_size)
		report("Spaceman", "offset overlaps with structure.");
	if (offset >= sm->sm_obj_size || offset + sizeof(u16) > sm->sm_obj_size)
		report("Spaceman", "offset is out of bounds.");
	return *((u16 *)value_p);
}

/**
 * spaceman_16_array_from_off - Get a pointer to the array on a spaceman offset
 * @raw:	pointer to the raw space manager
 * @offset:	offset of the array of 16-bit entries in @raw
 * @len:	number of entries in the array
 *
 * TODO: check that no values found by this function overlap with each other,
 * and also with spaceman_val_from_off()/spaceman_16_from_off().
 */
static __le16 *spaceman_16_array_from_off(struct apfs_spaceman_phys *raw, u32 offset, u32 len)
{
	struct spaceman *sm = &sb->s_spaceman;
	__le16 *array_p = (void *)raw + offset;

	assert(sm->sm_struct_size);

	if (offset & 0x1)
		report("Spaceman", "offset is not aligned to 2 bytes.");
	if (offset < sm->sm_struct_size)
		report("Spaceman", "offset overlaps with structure.");
	if (len > UINT32_MAX / 10 || offset > UINT32_MAX / 10)
		report("Spaceman", "length or offset of array are not sane.");
	if (offset >= sm->sm_obj_size || offset + sizeof(__le16) * len > sm->sm_obj_size)
		report("Spaceman", "offset is out of bounds.");
	return array_p;
}

/**
 * parse_spaceman_device - Parse and check a spaceman's device struct
 * @raw:	pointer to the raw space manager
 * @which:	device to check
 */
static void parse_spaceman_device(struct apfs_spaceman_phys *raw, enum smdev which)
{
	struct spaceman *sm = &sb->s_spaceman;
	struct spaceman_device *dev = &sm->sm_dev[which];
	struct apfs_spaceman_device *rawdev = &raw->sm_dev[which];
	u64 max_blkcnt;
	u32 cab_count;
	u32 addr_off;
	u64 start = 0;
	int i;

	dev->sm_cab_count = le32_to_cpu(rawdev->sm_cab_count);
	dev->sm_cib_count = le32_to_cpu(rawdev->sm_cib_count);
	dev->sm_chunk_count = le64_to_cpu(rawdev->sm_chunk_count);
	dev->sm_block_count = le64_to_cpu(rawdev->sm_block_count);
	dev->sm_free_count = le64_to_cpu(rawdev->sm_free_count);

	sm->sm_total_chunk_count += dev->sm_chunk_count;
	sm->sm_total_cib_count += dev->sm_cib_count;
	sm->sm_total_cab_count += dev->sm_cab_count;
	sm->sm_total_free_count += dev->sm_free_count;

	max_blkcnt = which == APFS_SD_MAIN ? sb->s_max_main_blkcnt : sb->s_max_tier2_blkcnt;
	if (dev->sm_block_count > max_blkcnt)
		report("Spaceman device", "block count too big for device.");
	if (dev->sm_chunk_count != DIV_ROUND_UP(dev->sm_block_count, sm->sm_blocks_per_chunk))
		report("Spaceman device", "wrong count of chunks.");
	if (dev->sm_cib_count != DIV_ROUND_UP(dev->sm_chunk_count, sm->sm_chunks_per_cib))
		report("Spaceman device", "wrong count of chunk-info blocks.");

	cab_count = DIV_ROUND_UP(dev->sm_cib_count, sm->sm_cibs_per_cab);
	/* CABs are not used unless at least one can be filled */
	if (cab_count == 1)
		cab_count = 0;
	if (dev->sm_cab_count != cab_count)
		report("Spaceman device", "wrong count of chunk-info address blocks.");

	/* All bitmaps will need to be read into memory */
	dev->sm_bitmap = calloc(dev->sm_chunk_count, sb->s_blocksize);
	if (!dev->sm_bitmap)
		system_error();

	/*
	 * TODO: is it mandatory that sm_addr_off for the tier2 device comes
	 * right after the end of the same array for the main device?
	 */
	addr_off = le32_to_cpu(rawdev->sm_addr_offset);
	if (!dev->sm_cab_count) {
		/* If CABs are not used, the spaceman just lists the CIBs */
		for (i = 0; i < dev->sm_cib_count; ++i) {
			u64 bno = spaceman_val_from_off(raw, addr_off + i * sizeof(u64));
			start = parse_chunk_info_block(bno, i, start, NULL /* xid_p */, dev);
		}
	} else {
		for (i = 0; i < dev->sm_cab_count; ++i) {
			u64 bno = spaceman_val_from_off(raw, addr_off + i * sizeof(u64));
			start = parse_cib_addr_block(bno, i, start, dev);
		}
		if (dev->sm_cib_count != dev->sm_cibs)
			report("Spaceman device", "bad total number of cibs.");
	}

	if (dev->sm_chunk_count != dev->sm_chunks)
		report("Spaceman device", "bad total number of chunks.");
	if (dev->sm_block_count != dev->sm_blocks)
		report("Spaceman device", "bad total number of blocks.");
	if (dev->sm_free_count != dev->sm_free)
		report("Spaceman device", "bad total number of free blocks.");

	if (rawdev->sm_reserved || rawdev->sm_reserved2)
		report("Spaceman device", "non-zero padding.");
}

struct alloc_zone {
	struct alloc_zone *next;	/* Next entry in linked list */
	u16 id;				/* Zone id */
	u64 start;			/* Start of zone */
	u64 end;			/* End of zone */
};

static struct alloc_zone *alloc_zone_list = NULL;

static void check_alloc_zone_sanity(u64 start, u64 end)
{
	if (start & (sb->s_blocksize - 1))
		report("Allocation zone", "start isn't multiple of block size.");
	if (end & (sb->s_blocksize - 1))
		report("Allocation zone", "end isn't multiple of block size.");
	if (start >= end)
		report("Allocation zone", "invalid range.");
}

/* Puts alloc zones in a list to check for overlap */
static void check_new_alloc_zone(u16 id, u64 start, u64 end)
{
	struct alloc_zone **zone_p = NULL;
	struct alloc_zone *zone = NULL;
	struct alloc_zone *new = NULL;

	check_alloc_zone_sanity(start, end);

	zone_p = &alloc_zone_list;
	zone = *zone_p;
	while (zone) {
		if (zone->id == id)
			report("Allocation zones", "repeated id.");
		if (start < zone->end && end > zone->start)
			report("Allocations zones", "overlapping ranges.");
		zone_p = &zone->next;
		zone = *zone_p;
	}

	new = calloc(1, sizeof(*new));
	if (!new)
		system_error();
	new->id = id;
	new->start = start;
	new->end = end;
	*zone_p = new;
}

static void free_checked_alloc_zones(void)
{
	struct alloc_zone *curr = alloc_zone_list;

	alloc_zone_list = NULL;
	while (curr) {
		struct alloc_zone *next = NULL;

		next = curr->next;
		curr->next = NULL;
		free(curr);
		curr = next;
	}
}

/* If old zones are reported, just check that the index is valid */
static void check_prev_alloc_zones(struct apfs_spaceman_allocation_zone_info_phys *az)
{
	struct apfs_spaceman_allocation_zone_boundaries *azb = NULL;
	u16 prev_index;
	int j;

	prev_index = le16_to_cpu(az->saz_previous_boundary_index);
	if (prev_index > APFS_SM_ALLOCZONE_NUM_PREVIOUS_BOUNDARIES)
		report("Allocation zones", "out-of-range previous index.");

	for (j = 0; j < APFS_SM_ALLOCZONE_NUM_PREVIOUS_BOUNDARIES; ++j) {
		azb = &az->saz_previous_boundaries[j];

		if (prev_index == 0) {
			/* No previous zones should be reported */
			if (azb->saz_zone_start || azb->saz_zone_end)
				report("Previous allocation zones", "missing index.");
			continue;
		}

		if (!azb->saz_zone_start && !azb->saz_zone_end) {
			/* No zone reported in this slot */
			if (j == prev_index - 1 && !azb->saz_zone_start)
				report("Allocation zones", "latest is missing.");
			continue;
		}

		check_alloc_zone_sanity(le64_to_cpu(azb->saz_zone_start), le64_to_cpu(azb->saz_zone_end));
	}
}

/**
 * check_spaceman_datazone - Check the spaceman allocation zones
 * @dz: pointer to the raw datazone structure
 *
 * Allocation zones are undocumented, so we can't do much more than report them
 * as unsupported if they are in use.
 */
static void check_spaceman_datazone(struct apfs_spaceman_datazone_info_phys *dz)
{
	int i, dev;

	for (dev = 0; dev < APFS_SD_COUNT; ++dev) {
		for (i = 0; i < APFS_SM_DATAZONE_ALLOCZONE_COUNT; ++i) {
			struct apfs_spaceman_allocation_zone_info_phys *az = NULL;
			struct apfs_spaceman_allocation_zone_boundaries *azb = NULL;

			az = &dz->sdz_allocation_zones[dev][i];
			azb = &az->saz_current_boundaries;

			if (az->saz_zone_id) {
				if (dev != APFS_SD_MAIN)
					report_unknown("Fusion drive");
				check_new_alloc_zone(le16_to_cpu(az->saz_zone_id), le64_to_cpu(azb->saz_zone_start), le64_to_cpu(azb->saz_zone_end));
			} else if (azb->saz_zone_start || azb->saz_zone_end) {
				report("Allocation zone", "has no id.");
			}

			if (az->saz_reserved)
				report("Datazone", "reserved field in use.");

			check_prev_alloc_zones(az);
		}
		free_checked_alloc_zones();
	}
}

/**
 * check_spaceman_tier2_fq_is_null - Check that a tier 2 free queue is unset
 * @sfq_raw: raw free queue to check
 */
static void check_spaceman_free_queue_is_null(struct apfs_spaceman_free_queue *sfq_raw)
{
	if (sfq_raw->sfq_count || sfq_raw->sfq_tree_oid || sfq_raw->sfq_oldest_xid || sfq_raw->sfq_tree_node_limit)
		report("Tier 2 free queue", "should not be set.");
}

/**
 * check_spaceman_free_queue_dev - Check the free queue for a given device
 * @sfq_raw:	raw free queue to check
 * @which:	device for the queue
 */
static void check_spaceman_free_queue_dev(struct apfs_spaceman_free_queue *sfq_raw, enum smdev which)
{
	struct spaceman *sm = &sb->s_spaceman;
	struct spaceman_device *dev = NULL;
	struct free_queue *fq = NULL;
	int idx;

	if (which == APFS_SD_TIER2 && !apfs_is_fusion_drive())
		return check_spaceman_free_queue_is_null(sfq_raw);

	dev = &sm->sm_dev[which];
	idx = which == APFS_SD_MAIN ? APFS_SFQ_MAIN : APFS_SFQ_TIER2;

	dev->sm_dev_fq = parse_free_queue_btree(le64_to_cpu(sfq_raw->sfq_tree_oid), idx);
	fq = dev->sm_dev_fq;

	if (le64_to_cpu(sfq_raw->sfq_count) != fq->sfq_count)
		report("Spaceman free queue", "wrong block count.");
	if (le64_to_cpu(sfq_raw->sfq_oldest_xid) != fq->sfq_oldest_xid)
		report("Spaceman free queue", "oldest xid is wrong.");
	if (le16_to_cpu(sfq_raw->sfq_tree_node_limit) < fq->sfq_btree.node_count)
		report("Spaceman free queue", "node count above limit.");
	if (le16_to_cpu(sfq_raw->sfq_tree_node_limit) != main_fq_node_limit(dev->sm_blocks))
		report("Spaceman free queue", "wrong node limit.");
}

/**
 * check_spaceman_free_queues - Check the spaceman free queues
 * @sfq: pointer to the raw free queue array
 */
static void check_spaceman_free_queues(struct apfs_spaceman_free_queue *sfq)
{
	struct spaceman *sm = &sb->s_spaceman;
	int i;

	for (i = 0; i < APFS_SFQ_COUNT; ++i) {
		if (sfq[i].sfq_pad16 || sfq[i].sfq_pad32)
			report("Spaceman free queue", "non-zero padding.");
		if (sfq[i].sfq_reserved)
			report("Spaceman free queue", "reserved field in use.");
	}

	sm->sm_ip_fq = parse_free_queue_btree(
				le64_to_cpu(sfq[APFS_SFQ_IP].sfq_tree_oid), APFS_SFQ_IP);
	if (le64_to_cpu(sfq[APFS_SFQ_IP].sfq_count) != sm->sm_ip_fq->sfq_count)
		report("Spaceman free queue", "wrong block count.");
	if (le64_to_cpu(sfq[APFS_SFQ_IP].sfq_oldest_xid) !=
					sm->sm_ip_fq->sfq_oldest_xid)
		report("Spaceman free queue", "oldest xid is wrong.");
	if (le16_to_cpu(sfq[APFS_SFQ_IP].sfq_tree_node_limit) <
					sm->sm_ip_fq->sfq_btree.node_count)
		report("Spaceman free queue", "node count above limit.");
	if (le16_to_cpu(sfq[APFS_SFQ_IP].sfq_tree_node_limit) != ip_fq_node_limit(sm->sm_total_chunk_count))
		report("Spaceman free queue", "wrong node limit.");

	check_spaceman_free_queue_dev(&sfq[APFS_SFQ_MAIN], APFS_SD_MAIN);
	check_spaceman_free_queue_dev(&sfq[APFS_SFQ_TIER2], APFS_SD_TIER2);
}

/**
 * compare_container_bitmaps_dev - Verify a device's allocation bitmap
 * @which: device to check
 */
static void compare_container_bitmaps_dev(enum smdev which)
{
	struct spaceman *sm = &sb->s_spaceman;
	struct spaceman_device *dev = NULL;
	u64 *reported_bmap = NULL, *real_bmap = NULL;
	unsigned long long bmap_size;

	dev = &sm->sm_dev[which];
	if (dev->sm_chunk_count == 0) /* No tier 2 device */
		return;

	reported_bmap = dev->sm_bitmap;
	real_bmap = which == APFS_SD_MAIN ? sb->s_main_bitmap : sb->s_tier2_bitmap;
	bmap_size = sb->s_blocksize * dev->sm_chunk_count;

	if (memcmp(reported_bmap, real_bmap, bmap_size) != 0)
		report("Space manager", "bad allocation bitmap.");
}

/**
 * check_ip_free_next - Check the free_next field for the internal pool
 * @free_next:	free_next list to check
 * @free_head:	first block in the linked list of free blocks
 * @free_tail:	last block in the linked list of free blocks
 * @bmap_count:	total number of ip bitmaps
 *
 * Returns the number of used entries.
 */
static int check_ip_free_next(__le16 *free_next, u16 free_head, u16 free_tail, u32 bmap_count)
{
	u16 curr, next;
	int free_count = 0;

	/*
	 * Entries for free ip bitmap blocks are a linked list, where each one
	 * gives the index of the next one. The rest of the blocks are used;
	 * they aren't part of the list, so their entries are set to the
	 * invalid index 0xFFFF.
	 */
	curr = free_tail;
	next = free_head;
	do {
		if (curr >= bmap_count || next >= bmap_count)
			report("Internal pool", "free bitmaps are out-of-bounds.");
		if (++free_count >= bmap_count)
			report("Internal pool", "free bitmap list loops.");
		curr = next;
		next = le16_to_cpu(free_next[curr]);
	} while (curr != free_tail);

	/*
	 * The tail itself is also set to the invalid index because the linked
	 * list ends there. Its block is free, though.
	 */
	if (le16_to_cpu(free_next[free_tail]) != APFS_SPACEMAN_IP_BM_INDEX_INVALID)
		report("Free ip bitmaps list", "free tail is not used.");

	return bmap_count - free_count;
}

/**
 * read_ip_bitmap_block - Read a single internal pool bitmap block into memory
 * @bmap_base:	first block of the bitmap ring
 * @bmap_len:	length of the bitmap ring
 * @bmap_off:	offset of the block to read in the ring
 * @bmap:	on return, the whole ip bitmap
 */
static void read_ip_bitmap_block(u64 bmap_base, u32 bmap_len, u16 bmap_off, char *bmap)
{
	char *curr_blk = NULL;
	u64 bno = bmap_base + bmap_off % bmap_len;

	curr_blk = apfs_mmap(NULL, sb->s_blocksize, PROT_READ, MAP_PRIVATE, bno * sb->s_blocksize);
	if (curr_blk == MAP_FAILED)
		system_error();

	memcpy(bmap, curr_blk, sb->s_blocksize);

	munmap(curr_blk, sb->s_blocksize);
	curr_blk = NULL;
}

/**
 * parse_ip_bitmap_list - Check consistency of the internal pool bitmap list
 * @raw:	pointer to the raw space manager
 * @bmap:	on return, set the whole ip bitmap here
 */
static void parse_ip_bitmap_list(struct apfs_spaceman_phys *raw, char *bmap)
{
	u64 bmap_base = le64_to_cpu(raw->sm_ip_bm_base);
	u16 bmap_off;
	u32 bmap_length = le32_to_cpu(raw->sm_ip_bm_block_count);
	u32 bm_size_in_blocks = le32_to_cpu(raw->sm_ip_bm_size_in_blocks);
	u16 free_head, free_tail, used_count;
	__le16 *free_next = NULL;
	u16 *used_bitmaps = NULL;
	u32 i, j;

	/*
	 * The bitmap area is a ring structure that keeps both the currently
	 * valid ip bitmaps and some older versions. I don't know the reason
	 * for this.
	 */

	if (bmap_length > APFS_SPACEMAN_IP_BM_BLOCK_COUNT_MAX)
		report("Internal pool", "bitmap list is too long.");

	if (bmap_length != 16 * bm_size_in_blocks)
		report("Space manager", "ip doesn't have 16 bitmap copies.");

	free_head = le16_to_cpu(raw->sm_ip_bm_free_head);
	free_tail = le16_to_cpu(raw->sm_ip_bm_free_tail);

	free_next = spaceman_16_array_from_off(raw, le32_to_cpu(raw->sm_ip_bm_free_next_offset), bmap_length);
	used_count = check_ip_free_next((__le16 *)free_next, free_head, free_tail, bmap_length);
	if (used_count != bm_size_in_blocks)
		report("Internal pool", "incorrect count of used blocks.");

	container_bmap_mark_as_used(bmap_base, bmap_length);

	used_bitmaps = calloc(used_count + 1, sizeof(*used_bitmaps));
	if (!used_bitmaps)
		system_error();
	used_bitmaps[0] = free_tail;
	for (i = 0; i < bm_size_in_blocks; ++i) {
		bmap_off = spaceman_16_from_off(raw, le32_to_cpu(raw->sm_ip_bitmap_offset) + i * sizeof(bmap_off));
		if (bmap_off >= bmap_length)
			report("Internal pool", "bitmap block is out-of-bounds.");
		if (le16_to_cpu(free_next[bmap_off]) != APFS_SPACEMAN_IP_BM_INDEX_INVALID)
			report("Internal pool", "used bitmap marked as free.");
		read_ip_bitmap_block(bmap_base, bmap_length, bmap_off, bmap + i * sb->s_blocksize);

		/*
		 * Make sure that the same bitmap blocks aren't being reused
		 * for different ip chunks. Remember that the first used bitmap
		 * is the one for free_tail, set outside this loop.
		 */
		for (j = 0; j < i + 1; ++j) {
			if (used_bitmaps[j] == bmap_off)
				report("Internal pool", "same bitmap used twice.");
		}
		used_bitmaps[i + 1] = bmap_off;
	}
	free(used_bitmaps);
	used_bitmaps = NULL;
}

/**
 * check_ip_bitmap_blocks - Check that the bitmap blocks are properly zeroed
 * @raw: pointer to the raw space manager
 *
 * For most internal pool bitmap blocks this is the only check needed; the
 * current one also needs to be compared against the actual allocation bitmap.
 */
static void check_ip_bitmap_blocks(struct apfs_spaceman_phys *raw)
{
	u64 bmap_base = le64_to_cpu(raw->sm_ip_bm_base);
	u32 bmap_length = le32_to_cpu(raw->sm_ip_bm_block_count);
	u64 pool_blocks = le64_to_cpu(raw->sm_ip_block_count);
	int i;

	/*
	 * These zeroed-tail checks don't really make sense with multiblock ip
	 * bitmaps, because we can't know for sure which ones were tails and
	 * which ones were used in full.
	 */
	if (le32_to_cpu(raw->sm_ip_bm_size_in_blocks) != 1)
		return;

	for (i = 0; i < bmap_length; ++i) {
		char *bmap;
		int edge, j;

		bmap = apfs_mmap(NULL, sb->s_blocksize, PROT_READ, MAP_PRIVATE, (bmap_base + i) * sb->s_blocksize);
		if (bmap == MAP_FAILED)
			system_error();

		/*
		 * The edge is the last byte inside the allocation bitmap;
		 * everything that comes afterwards must be zeroed.
		 */
		edge = pool_blocks / 8;
		for (j = pool_blocks % 8; j < 8; ++j) {
			u8 flag = 1 << j;

			if (bmap[edge] & flag)
				report("Internal pool", "non-zeroed bitmap.");
		}
		for (j = edge + 1; j < sb->s_blocksize; ++j) {
			if (bmap[j])
				report("Internal pool", "non-zeroed bitmap.");
		}

		munmap(bmap, sb->s_blocksize);
	}
}

/**
 * check_internal_pool - Check the internal pool of blocks
 * @raw:	pointer to the raw space manager
 */
static void check_internal_pool(struct apfs_spaceman_phys *raw)
{
	struct spaceman *sm = &sb->s_spaceman;
	char *pool_bmap;
	u64 pool_base = le64_to_cpu(raw->sm_ip_base);
	u64 pool_blocks = le64_to_cpu(raw->sm_ip_block_count);
	u64 ip_chunk_count = le32_to_cpu(raw->sm_ip_bm_size_in_blocks);
	u64 i;

	if ((sm->sm_total_chunk_count + sm->sm_total_cib_count + sm->sm_total_cab_count) * 3 != pool_blocks)
		report("Space manager", "wrong size of internal pool.");

	pool_bmap = calloc(ip_chunk_count, sb->s_blocksize);
	if (!pool_bmap)
		system_error();
	parse_ip_bitmap_list(raw, pool_bmap);

	if (memcmp(pool_bmap, sb->s_ip_bitmap, ip_chunk_count * sb->s_blocksize))
		report("Space manager", "bad ip allocation bitmap.");
	container_bmap_mark_as_used(pool_base, pool_blocks);

	free(pool_bmap);
	pool_bmap = NULL;

	if (le32_to_cpu(raw->sm_ip_bm_tx_multiplier) !=
					APFS_SPACEMAN_IP_BM_TX_MULTIPLIER)
		report("Space manager", "bad tx multiplier for internal pool.");

	for (i = 0; i < ip_chunk_count; ++i) {
		u64 xid;

		xid = spaceman_val_from_off(raw, le32_to_cpu(raw->sm_ip_bm_xid_offset) + i * sizeof(xid));
		if (xid > sb->s_xid)
			report("Internal pool", "bad transaction id.");
	}

	check_ip_bitmap_blocks(raw);
}

/**
 * check_spaceman - Check the space manager structures for a container
 * @oid: ephemeral object id for the spaceman structure
 */
void check_spaceman(u64 oid)
{
	struct spaceman *sm = &sb->s_spaceman;
	struct object obj;
	struct apfs_spaceman_phys *raw;
	u64 ip_chunk_count;
	u32 flags;

	raw = read_ephemeral_object(oid, &obj);
	if (obj.type != APFS_OBJECT_TYPE_SPACEMAN)
		report("Space manager", "wrong object type.");
	if (obj.subtype != APFS_OBJECT_TYPE_INVALID)
		report("Space manager", "wrong object subtype.");
	sm->sm_xid = obj.xid;
	sm->sm_obj_size = obj.size;

	sm->sm_ip_base = le64_to_cpu(raw->sm_ip_base);
	sm->sm_ip_block_count = le64_to_cpu(raw->sm_ip_block_count);
	ip_chunk_count = DIV_ROUND_UP(sm->sm_ip_block_count, 8 * sb->s_blocksize);
	if (ip_chunk_count != le32_to_cpu(raw->sm_ip_bm_size_in_blocks))
		report("Space manager", "bad ip bm size.");
	sb->s_ip_bitmap = calloc(ip_chunk_count, sb->s_blocksize);
	if (!sb->s_ip_bitmap)
		system_error();

	flags = le32_to_cpu(raw->sm_flags);
	if ((flags & APFS_SM_FLAGS_VALID_MASK) != flags)
		report("Space manager", "invalid flag in use.");
	if (flags & APFS_SM_FLAG_VERSIONED) {
		sm->sm_struct_size = le32_to_cpu(raw->sm_struct_size);
		if (sm->sm_struct_size != sizeof(*raw))
			report("Space manager", "wrong reported struct size.");
		check_spaceman_datazone(&raw->sm_datazone);
	} else {
		/* Some fields are missing in the non-versioned structure */
		sm->sm_struct_size = sizeof(*raw) - sizeof(raw->sm_datazone) -
				     sizeof(raw->sm_struct_size) -
				     sizeof(raw->sm_version);
	}

	if (le32_to_cpu(raw->sm_block_size) != sb->s_blocksize)
		report("Space manager", "wrong block size.");
	parse_spaceman_chunk_counts(raw);

	parse_spaceman_device(raw, APFS_SD_MAIN);
	parse_spaceman_device(raw, APFS_SD_TIER2);
	if (sb->s_block_count != sm->sm_dev[APFS_SD_MAIN].sm_block_count + sm->sm_dev[APFS_SD_TIER2].sm_block_count)
		report("Spaceman devices", "wrong block count.");

	check_spaceman_free_queues(raw->sm_fq);
	check_internal_pool(raw);
	free(sb->s_ip_bitmap);

	if (le64_to_cpu(raw->sm_fs_reserve_block_count) != sm->sm_reserve_block_num)
		report("Space manager", "wrong block reservation total.");
	if (le64_to_cpu(raw->sm_fs_reserve_alloc_count) != sm->sm_reserve_alloc_num)
		report("Space manager", "wrong reserve block allocation total.");
	if (sm->sm_reserve_block_num - sm->sm_reserve_alloc_num > sm->sm_total_free_count)
		report("Space manager", "block reservation not respected.");

	compare_container_bitmaps_dev(APFS_SD_MAIN);
	compare_container_bitmaps_dev(APFS_SD_TIER2);
	free(raw);
}

/**
 * parse_free_queue_record - Parse a free queue record and check for corruption
 * @key:	pointer to the raw key
 * @val:	pointer to the raw value
 * @len:	length of the raw value
 * @btree:	the free queue btree structure
 *
 * Internal consistency of @key must be checked before calling this function.
 */
void parse_free_queue_record(struct apfs_spaceman_free_queue_key *key,
			     void *val, int len, struct btree *btree)
{
	struct free_queue *sfq = (struct free_queue *)btree;
	u64 paddr, length, xid;
	bool inside_ip;

	if (!len) {
		length = 1; /* Ghost records are for one block long extents */
	} else if (len == 8) {
		__le64 *val64 = (__le64 *)val;

		length = le64_to_cpu(*val64);
		if (!length)
			report("Free queue record", "length is zero.");
		if (length == 1)
			report("Free queue record", "value is unnecessary.");
	} else {
		report("Free queue record", "wrong size of value.");
	}
	sfq->sfq_count += length;

	paddr = le64_to_cpu(key->sfqk_paddr);
	inside_ip = range_in_ip(paddr, length);
	if (sfq->sfq_index == APFS_SFQ_IP && !inside_ip)
		report("Free queue record", "range should be inside the IP.");
	if (sfq->sfq_index != APFS_SFQ_IP && inside_ip)
		report("Free queue record", "range should be outside the IP.");

	xid = le64_to_cpu(key->sfqk_xid);
	if (xid > sb->s_xid)
		report("Free queue record", "bad transaction id.");
	if (!sfq->sfq_oldest_xid || xid < sfq->sfq_oldest_xid)
		sfq->sfq_oldest_xid = xid;

	/*
	 * These blocks are free, but still not marked as such.  The point
	 * seems to be the preservation of recent checkpoints.
	 */
	if (inside_ip)
		ip_bmap_mark_as_used(paddr, length);
	else
		container_bmap_mark_as_used(paddr, length);
}
