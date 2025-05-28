/*
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <apfs/parameters.h>
#include <apfs/raw.h>
#include <apfs/types.h>
#include "btree.h"
#include "mkapfs.h"
#include "object.h"
#include "spaceman.h"

struct device_info {
	u64 block_count;
	u64 chunk_count;
	u32 cib_count;
	u32 cab_count;

	u32 cib_addr_base_off;	/* Offset of the cib/cab address in spaceman */

	u64 first_cib;		/* Block number for first chunk-info block */
	u64 first_cab;		/* Block number for first cib address block */

	u64 used_blocks_end;	/* Block right after the last one we allocate */
	u64 used_chunks_end;	/* Chunk right after the last one we allocate */

	u64 first_chunk_bmap;	/* Block number for the first chunk's bitmap */
};

/* Extra information about the space manager */
static struct spaceman_info {
	struct device_info dev_info[APFS_SD_COUNT];
	u64 total_chunk_count;
	u32 total_cib_count;
	u32 total_cab_count;

	u64 ip_blocks;
	u32 ip_bm_size;
	u32 ip_bmap_blocks;
	u64 ip_base;
	u32 bm_addr_off;	/* Offset of bitmap address in the spaceman */
	u32 bm_free_next_off;	/* Offset of free_next in the spaceman */
} sm_info = {0};

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
 * spaceman_size - Calculate the size of the spaceman object in bytes
 */
u32 spaceman_size(void)
{
	struct device_info *main_dev = NULL, *tier2_dev = NULL;
	int entry_count;

	main_dev = &sm_info.dev_info[APFS_SD_MAIN];
	tier2_dev = &sm_info.dev_info[APFS_SD_TIER2];

	entry_count = 0;
	if (main_dev->cab_count > 1)
		entry_count += main_dev->cab_count;
	else
		entry_count += main_dev->cib_count;
	if (tier2_dev->cab_count > 1)
		entry_count += tier2_dev->cab_count;
	else
		entry_count += tier2_dev->cib_count;

	/*
	 * The spaceman must have room for the addresses of all device cibs (or
	 * cabs) for each of the devices. Some containers require extra blocks
	 * to store this stuff.
	 */
	return DIV_ROUND_UP(entry_count * sizeof(__le64) + main_dev->cib_addr_base_off, param->blocksize) * param->blocksize;
}

/**
 * count_used_blocks_in_chunk - Calculate number of allocated blocks in a chunk
 * @dev:	device for the chunk
 * @chunkno:	chunk number to check
 */
static u32 count_used_blocks_in_chunk(struct device_info *dev, u64 chunkno)
{
	u32 first_chunk_ip_blocks;

	if (chunkno >= dev->used_chunks_end)
		return 0;

	/* The tier 2 device only has a superblock */
	if (dev->used_blocks_end == 1)
		return 1;

	/* The internal pool may not fit whole in the chunk */
	first_chunk_ip_blocks = MIN(sm_info.ip_blocks, blocks_per_chunk() - sm_info.ip_base);

	if (chunkno == 0) {
		u32 blocks = 0;

		/* This stuff always goes in the first chunk */
		blocks += 1;			/* Block zero */
		blocks += CPOINT_DESC_BLOCKS;	/* Checkpoint descriptor blocks */
		blocks += CPOINT_DATA_BLOCKS;	/* Checkpoint data blocks */
		blocks += 2;			/* Container object map and its root */
		blocks += 6;			/* Volume superblock and its trees */
		blocks += sm_info.ip_bmap_blocks; /* Internal pool bitmap blocks */
		if (fd_tier2 != -1)
			blocks += 2; /* Fusion middle-tree and writeback cache */

		blocks += first_chunk_ip_blocks;
		return blocks;
	}

	/* Later chunks are only needed for the rest of the internal pool */
	if (chunkno != dev->used_chunks_end - 1)
		return blocks_per_chunk();

	/* Last chunk */
	return (sm_info.ip_blocks - first_chunk_ip_blocks) % blocks_per_chunk();
}

/**
 * count_used_blocks - Count the blocks used by the mkfs in a device
 * @dev: the device
 */
static u32 count_used_blocks(struct device_info *dev)
{
	u32 blocks = 0;
	u64 chunkno;

	for (chunkno = 0; chunkno < dev->used_chunks_end; ++chunkno)
		blocks += count_used_blocks_in_chunk(dev, chunkno);
	return blocks;
}

/**
 * bmap_mark_as_used - Mark a range as used in the allocation bitmap
 * @bitmap:	allocation bitmap for the first chunk
 * @paddr:	first block number
 * @length:	block count
 */
static void bmap_mark_as_used(u64 *bitmap, u64 paddr, u64 length)
{
	u64 *byte;
	u64 flag;
	u64 i;

	for (i = paddr; i < paddr + length; ++i) {
		byte = bitmap + i / 64;
		flag = 1ULL << i % 64;
		*byte |= flag;
	}
}

/**
 * make_main_alloc_bitmap - Make the allocation bitmap for the main device
 */
static void make_main_alloc_bitmap(void)
{
	struct device_info *dev = NULL;
	void *bmap = NULL;

	dev = &sm_info.dev_info[APFS_SD_MAIN];
	bmap = get_zeroed_blocks(dev->used_chunks_end);

	/* Block zero */
	bmap_mark_as_used(bmap, 0, 1);
	/* Checkpoint descriptor blocks */
	bmap_mark_as_used(bmap, CPOINT_DESC_BASE, CPOINT_DESC_BLOCKS);
	/* Checkpoint data blocks */
	bmap_mark_as_used(bmap, CPOINT_DATA_BASE, CPOINT_DATA_BLOCKS);
	/* Container object map and its root */
	bmap_mark_as_used(bmap, MAIN_OMAP_BNO, 2);
	/* Volume superblock and its trees */
	bmap_mark_as_used(bmap, FIRST_VOL_BNO, 6);
	/* Internal pool bitmap blocks */
	bmap_mark_as_used(bmap, IP_BMAP_BASE, sm_info.ip_bmap_blocks);
	/* Internal pool blocks */
	bmap_mark_as_used(bmap, sm_info.ip_base, sm_info.ip_blocks);
	/* Fusion drive stuff */
	if (fd_tier2 != -1) {
		bmap_mark_as_used(bmap, FUSION_MT_BNO, 1);
		bmap_mark_as_used(bmap, FUSION_WBC_FIRST_BNO, 1);
	}

	apfs_writeall(bmap, dev->used_chunks_end, dev->first_chunk_bmap);
}

/**
 * make_tier2_alloc_bitmap - Make the allocation bitmap for the tier 2 device
 */
static void make_tier2_alloc_bitmap(void)
{
	struct device_info *dev = NULL;
	void *bmap = NULL;

	dev = &sm_info.dev_info[APFS_SD_TIER2];
	bmap = get_zeroed_blocks(dev->used_chunks_end);

	/* Block zero */
	bmap_mark_as_used(bmap, 0, 1);

	apfs_writeall(bmap, dev->used_chunks_end, dev->first_chunk_bmap);
}

/*
 * Offsets into the spaceman block for a non-versioned container; the values
 * have been borrowed from a test image.
 */
#define BITMAP_XID_OFF		0x150	/* Transaction id for the ip bitmap */

/**
 * make_chunk_info - Write a chunk info structure
 * @dev:	device getting made
 * @chunk:	pointer to the raw chunk info structure
 * @start:	first block number for the chunk
 *
 * Returns the first block number for the next chunk.
 */
static u64 make_chunk_info(struct device_info *dev, struct apfs_chunk_info *chunk, u64 start)
{
	u64 remaining_blocks = dev->block_count - start;
	u64 chunkno = start / blocks_per_chunk();
	u32 block_count, free_count;

	chunk->ci_xid = cpu_to_le64(MKFS_XID);
	chunk->ci_addr = cpu_to_le64(start);

	/* Later chunks are just holes */
	if (start < dev->used_blocks_end)
		chunk->ci_bitmap_addr = cpu_to_le64(dev->first_chunk_bmap + chunkno);

	block_count = blocks_per_chunk();
	if (remaining_blocks < block_count) /* This is the final chunk */
		block_count = remaining_blocks;
	chunk->ci_block_count = cpu_to_le32(block_count);

	free_count = block_count - count_used_blocks_in_chunk(dev, chunkno);
	chunk->ci_free_count = cpu_to_le32(free_count);

	start += block_count;
	return start;
}

/**
 * make_chunk_info_block - Make a chunk-info block
 * @dev:	device getting made
 * @bno:	block number for the chunk-info block
 * @index:	index of the chunk-info block
 * @start:	first block number for the first chunk
 *
 * Returns the first block number for the first chunk of the next cib.
 */
static u64 make_chunk_info_block(struct device_info *dev, u64 bno, int index, u64 start)
{
	struct apfs_chunk_info_block *cib = get_zeroed_block();
	int i;

	cib->cib_index = cpu_to_le32(index);
	for (i = 0; i < chunks_per_cib(); ++i) {
		if (start == dev->block_count) /* No more chunks in device */
			break;
		start = make_chunk_info(dev, &cib->cib_chunk_info[i], start);
	}
	cib->cib_chunk_info_count = cpu_to_le32(i);

	set_object_header(&cib->cib_o, param->blocksize, bno,
			  APFS_OBJ_PHYSICAL | APFS_OBJECT_TYPE_SPACEMAN_CIB,
			  APFS_OBJECT_TYPE_INVALID);
	apfs_writeall(cib, 1, bno);

	return start;
}

/**
 * make_cib_addr_block - Make a cib address block
 * @dev:	device getting made
 * @bno:	block number for the chunk-info block
 * @index:	index of the chunk-info block
 * @start:	first block number for the first chunk
 *
 * Returns the first block number for the first chunk of the next cib.
 */
static u64 make_cib_addr_block(struct device_info *dev, u64 bno, int index, u64 start)
{
	struct apfs_cib_addr_block *cab = get_zeroed_block();
	int i;

	cab->cab_index = cpu_to_le32(index);
	for (i = 0; i < cibs_per_cab(); ++i) {
		int cib_index;
		u64 cib_bno;

		if (start == dev->block_count) /* No more chunks in device */
			break;

		cib_index = cibs_per_cab() * index + i;
		cib_bno = dev->first_cib + cib_index;
		cab->cab_cib_addr[i] = cpu_to_le64(cib_bno);
		start = make_chunk_info_block(dev, cib_bno, cib_index, start);
	}
	cab->cab_cib_count = cpu_to_le32(i);

	set_object_header(&cab->cab_o, param->blocksize, bno,
			  APFS_OBJ_PHYSICAL | APFS_OBJECT_TYPE_SPACEMAN_CAB,
			  APFS_OBJECT_TYPE_INVALID);
	apfs_writeall(cab, 1, bno);

	return start;
}

/**
 * make_single_device - Make a spaceman device structure
 * @sm:		pointer to the on-disk spaceman structure
 * @which:	device to make
 */
static void make_single_device(struct apfs_spaceman_phys *sm, enum smdev which)
{
	struct apfs_spaceman_device *dev = NULL;
	struct device_info *devinfo = NULL;
	u64 start = 0;
	int i;

	dev = &sm->sm_dev[which];
	devinfo = &sm_info.dev_info[which];

	dev->sm_block_count = cpu_to_le64(devinfo->block_count);
	dev->sm_chunk_count = cpu_to_le64(devinfo->chunk_count);
	dev->sm_cib_count = cpu_to_le32(devinfo->cib_count);
	dev->sm_cab_count = cpu_to_le32(devinfo->cab_count);
	dev->sm_free_count = cpu_to_le64(devinfo->block_count - count_used_blocks(devinfo));

	dev->sm_addr_offset = cpu_to_le32(devinfo->cib_addr_base_off);
	if (!devinfo->cab_count) {
		__le64 *cib_addr = (void *)sm + devinfo->cib_addr_base_off;
		for (i = 0; i < devinfo->cib_count; ++i) {
			cib_addr[i] = cpu_to_le64(devinfo->first_cib + i);
			start = make_chunk_info_block(devinfo, devinfo->first_cib + i, i, start);
		}
	} else {
		__le64 *cab_addr = (void *)sm + devinfo->cib_addr_base_off;
		for (i = 0; i < devinfo->cab_count; ++i) {
			cab_addr[i] = cpu_to_le64(devinfo->first_cab + i);
			start = make_cib_addr_block(devinfo, devinfo->first_cab + i, i, start);
		}
	}
}

/**
 * make_devices - Make the spaceman device structures
 * @sm: pointer to the on-disk spaceman structure
 */
static void make_devices(struct apfs_spaceman_phys *sm)
{
	make_single_device(sm, APFS_SD_MAIN);
	make_single_device(sm, APFS_SD_TIER2);
}

/**
 * make_ip_free_queue - Make an empty free queue for the internal pool
 * @fq:	free queue structure
 */
static void make_ip_free_queue(struct apfs_spaceman_free_queue *fq)
{
	fq->sfq_tree_oid = cpu_to_le64(IP_FREE_QUEUE_OID);
	make_empty_btree_root(eph_info.ip_free_queue_bno, IP_FREE_QUEUE_OID,
			      APFS_OBJECT_TYPE_SPACEMAN_FREE_QUEUE);
	fq->sfq_oldest_xid = 0;
	fq->sfq_tree_node_limit = cpu_to_le16(ip_fq_node_limit(sm_info.total_chunk_count));
}

/**
 * make_main_free_queue - Make an empty free queue for the main device
 * @fq:	free queue structure
 */
static void make_main_free_queue(struct apfs_spaceman_free_queue *fq)
{
	fq->sfq_tree_oid = cpu_to_le64(MAIN_FREE_QUEUE_OID);
	make_empty_btree_root(eph_info.main_free_queue_bno, MAIN_FREE_QUEUE_OID,
			      APFS_OBJECT_TYPE_SPACEMAN_FREE_QUEUE);
	fq->sfq_oldest_xid = 0;
	fq->sfq_tree_node_limit = cpu_to_le16(main_fq_node_limit(param->main_blkcnt));
}

/**
 * make_tier2_free_queue - Make an empty free queue for the tier 2 device
 * @fq:	free queue structure
 */
static void make_tier2_free_queue(struct apfs_spaceman_free_queue *fq)
{
	fq->sfq_tree_oid = cpu_to_le64(TIER2_FREE_QUEUE_OID);
	make_empty_btree_root(eph_info.tier2_free_queue_bno, TIER2_FREE_QUEUE_OID,
			      APFS_OBJECT_TYPE_SPACEMAN_FREE_QUEUE);
	fq->sfq_oldest_xid = 0;
	fq->sfq_tree_node_limit = cpu_to_le16(main_fq_node_limit(param->tier2_blkcnt));
}

/**
 * make_ip_bitmap - Make the allocation bitmap for the internal pool
 */
static void make_ip_bitmap(void)
{
	void *bmap = get_zeroed_blocks(sm_info.ip_bm_size);
	struct device_info *main_dev = NULL, *tier2_dev = NULL;

	main_dev = &sm_info.dev_info[APFS_SD_MAIN];
	tier2_dev = &sm_info.dev_info[APFS_SD_TIER2];

	/* Cib address blocks */
	bmap_mark_as_used(bmap, main_dev->first_cab - sm_info.ip_base, main_dev->cab_count);
	bmap_mark_as_used(bmap, tier2_dev->first_cab - sm_info.ip_base, tier2_dev->cab_count);
	/* Chunk-info blocks */
	bmap_mark_as_used(bmap, main_dev->first_cib - sm_info.ip_base, main_dev->cib_count);
	bmap_mark_as_used(bmap, tier2_dev->first_cib - sm_info.ip_base, tier2_dev->cib_count);
	/* Allocation bitmap block */
	bmap_mark_as_used(bmap, main_dev->first_chunk_bmap - sm_info.ip_base, main_dev->used_chunks_end);
	bmap_mark_as_used(bmap, tier2_dev->first_chunk_bmap - sm_info.ip_base, tier2_dev->used_chunks_end);

	apfs_writeall(bmap, sm_info.ip_bm_size, IP_BMAP_BASE);
}

/**
 * make_ip_bm_free_next - Set up the free_next list for the internal pool
 * @addr: pointer to the beginning of the field
 */
static void make_ip_bm_free_next(__le16 *addr)
{
	int i;

	/*
	 * Free ip bitmap blocks are kept in a linked list. For the mkfs this
	 * just means that they get marked with numbers that are one above
	 * their index, except for the tail block which gets the invalid index
	 * 0xFFFF. Blocks in use are not part of the list, so they also get
	 * 0xFFFF.
	 */
	for (i = 0; i < sm_info.ip_bm_size; ++i)
		addr[i] = cpu_to_le16(APFS_SPACEMAN_IP_BM_INDEX_INVALID);
	for (i = sm_info.ip_bm_size; i < sm_info.ip_bmap_blocks - 1; i++)
		addr[i] = cpu_to_le16(i + 1);
	addr[sm_info.ip_bmap_blocks - 1] = cpu_to_le16(APFS_SPACEMAN_IP_BM_INDEX_INVALID);
}

/**
 * make_internal_pool - Make the internal pool of the space manager
 * @sm: pointer to the on-disk spaceman structure
 */
static void make_internal_pool(struct apfs_spaceman_phys *sm)
{
	int i;
	__le64 *addr;
	__le16 *bm_off_addr;

	sm->sm_ip_bm_tx_multiplier =
				cpu_to_le32(APFS_SPACEMAN_IP_BM_TX_MULTIPLIER);
	sm->sm_ip_block_count = cpu_to_le64(sm_info.ip_blocks);
	sm->sm_ip_base = cpu_to_le64(sm_info.ip_base);
	sm->sm_ip_bm_size_in_blocks = cpu_to_le32(sm_info.ip_bm_size);

	sm->sm_ip_bm_block_count = cpu_to_le32(sm_info.ip_bmap_blocks);
	sm->sm_ip_bm_base = cpu_to_le64(IP_BMAP_BASE);
	for (i = 0; i < sm_info.ip_bmap_blocks; ++i)
		apfs_writeall(get_zeroed_block(), 1, IP_BMAP_BASE + i);

	/* The current bitmaps are the first in the ring */
	sm->sm_ip_bitmap_offset = cpu_to_le32(sm_info.bm_addr_off);
	bm_off_addr = (void *)sm + sm_info.bm_addr_off;
	for (i = 0; i < sm_info.ip_bm_size; ++i)
		bm_off_addr[i] = cpu_to_le16(i);
	sm->sm_ip_bm_free_head = cpu_to_le16(sm_info.ip_bm_size);
	sm->sm_ip_bm_free_tail = cpu_to_le16(sm_info.ip_bmap_blocks - 1);

	sm->sm_ip_bm_xid_offset = cpu_to_le32(BITMAP_XID_OFF);
	addr = (void *)sm + BITMAP_XID_OFF;
	for (i = 0; i < sm_info.ip_bm_size; ++i)
		addr[i] = cpu_to_le64(MKFS_XID);

	sm->sm_ip_bm_free_next_offset = cpu_to_le32(sm_info.bm_free_next_off);
	make_ip_bm_free_next((void *)sm + sm_info.bm_free_next_off);

	make_ip_bitmap();
}

/**
 * calculate_dev_info - Precalculate chunk/cib/cab counts for a device
 * @dev:	device info to set
 * @which:	which device is this?
 */
static void calculate_dev_info(struct device_info *dev, enum smdev which)
{
	dev->block_count = which == APFS_SD_MAIN ? param->main_blkcnt : param->tier2_blkcnt;
	dev->chunk_count = DIV_ROUND_UP(dev->block_count, blocks_per_chunk());
	dev->cib_count = DIV_ROUND_UP(dev->chunk_count, chunks_per_cib());
	dev->cab_count = DIV_ROUND_UP(dev->cib_count, cibs_per_cab());
	if (dev->cab_count == 1)
		dev->cab_count = 0;

	/* Put some limit on cab count to avoid overflow issues */
	if (dev->cab_count > 1000)
		fatal("device is too big");
}

/**
 * set_spaceman_info - Calculate the value of all fields of sm_info
 */
void set_spaceman_info(void)
{
	struct device_info *main_dev = NULL, *tier2_dev = NULL;

	main_dev = &sm_info.dev_info[APFS_SD_MAIN];
	tier2_dev = &sm_info.dev_info[APFS_SD_TIER2];
	calculate_dev_info(main_dev, APFS_SD_MAIN);
	calculate_dev_info(tier2_dev, APFS_SD_TIER2);
	sm_info.total_chunk_count = main_dev->chunk_count + tier2_dev->chunk_count;
	sm_info.total_cib_count = main_dev->cib_count + tier2_dev->cib_count;
	sm_info.total_cab_count = main_dev->cab_count + tier2_dev->cab_count;
	sm_info.ip_blocks = (sm_info.total_chunk_count + sm_info.total_cib_count + sm_info.total_cab_count) * 3;
	/* Just a rough limit in case tier 2 is huge */
	if (sm_info.ip_blocks > param->main_blkcnt / 2)
		fatal("internal pool too big for the main device");

	/*
	 * We have 16 ip bitmaps; each of them maps the whole ip and may span
	 * multiple blocks.
	 */
	sm_info.ip_bm_size = DIV_ROUND_UP(sm_info.ip_blocks, blocks_per_chunk());
	sm_info.ip_bmap_blocks = 16 * sm_info.ip_bm_size;
	sm_info.ip_base = IP_BMAP_BASE + sm_info.ip_bmap_blocks;

	/* We have one xid for each of the ip bitmaps */
	sm_info.bm_addr_off = BITMAP_XID_OFF + sizeof(__le64) * sm_info.ip_bm_size;
	sm_info.bm_free_next_off = sm_info.bm_addr_off + ROUND_UP(sizeof(__le16) * sm_info.ip_bm_size, sizeof(__le64));
	main_dev->cib_addr_base_off = sm_info.bm_free_next_off + sm_info.ip_bmap_blocks * sizeof(__le16);
	if (main_dev->cab_count)
		tier2_dev->cib_addr_base_off = main_dev->cib_addr_base_off + main_dev->cab_count * sizeof(__le64);
	else
		tier2_dev->cib_addr_base_off = main_dev->cib_addr_base_off + main_dev->cib_count * sizeof(__le64);

	/* Only the ip size matters, all other used blocks come before it */
	main_dev->used_blocks_end = sm_info.ip_base + sm_info.ip_blocks;
	main_dev->used_chunks_end = DIV_ROUND_UP(main_dev->used_blocks_end, blocks_per_chunk());
	/* Tier 2 is empty except for block zero */
	tier2_dev->used_blocks_end = fd_tier2 != -1 ? 1 : 0;
	tier2_dev->used_chunks_end = fd_tier2 != -1 ? 1 : 0;

	/*
	 * Put the chunk bitmaps at the beginning of the internal pool, and
	 * the cibs right after them, followed by the cabs if any. Then the
	 * same for the tier 2 device, if it exists.
	 */
	main_dev->first_chunk_bmap = sm_info.ip_base;
	main_dev->first_cib = main_dev->first_chunk_bmap + main_dev->used_chunks_end;
	main_dev->first_cab = main_dev->first_cib + main_dev->cib_count;
	tier2_dev->first_chunk_bmap = main_dev->first_cab + main_dev->cab_count;
	tier2_dev->first_cib = tier2_dev->first_chunk_bmap + tier2_dev->used_chunks_end;
	tier2_dev->first_cab = tier2_dev->first_cib + tier2_dev->cib_count;
}

/**
 * make_spaceman - Make the space manager for the container
 * @bno: block number to use
 * @oid: object id
 */
void make_spaceman(u64 bno, u64 oid)
{
	struct apfs_spaceman_phys *sm = NULL;

	sm = get_zeroed_blocks(spaceman_size() / param->blocksize);

	sm->sm_block_size = cpu_to_le32(param->blocksize);
	sm->sm_blocks_per_chunk = cpu_to_le32(blocks_per_chunk());
	sm->sm_chunks_per_cib = cpu_to_le32(chunks_per_cib());
	sm->sm_cibs_per_cab = cpu_to_le32(cibs_per_cab());

	make_devices(sm);
	make_ip_free_queue(&sm->sm_fq[APFS_SFQ_IP]);
	make_main_free_queue(&sm->sm_fq[APFS_SFQ_MAIN]);
	if (fd_tier2 != -1)
		make_tier2_free_queue(&sm->sm_fq[APFS_SFQ_TIER2]);
	make_internal_pool(sm);
	make_main_alloc_bitmap();
	if (fd_tier2 != -1)
		make_tier2_alloc_bitmap();

	set_object_header(&sm->sm_o, spaceman_size(), oid,
			  APFS_OBJ_EPHEMERAL | APFS_OBJECT_TYPE_SPACEMAN,
			  APFS_OBJECT_TYPE_INVALID);
	apfs_writeall(sm, spaceman_size() / param->blocksize, bno);
}
