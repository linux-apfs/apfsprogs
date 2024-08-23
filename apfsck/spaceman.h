/*
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _SPACEMAN_H
#define _SPACEMAN_H

#include <apfs/types.h>
#include "btree.h"
#include "object.h"

struct apfs_spaceman_free_queue_key;

struct spaceman_device {
	struct free_queue *sm_dev_fq; /* Free queue for the device */

	/* Device info read from the on-disk structures */
	void *sm_bitmap; /* Allocation bitmap */
	u32 sm_cib_count;
	u32 sm_cab_count;
	u64 sm_chunk_count;
	u64 sm_block_count;
	u64 sm_free_count;

	/* Device info measured by the fsck */
	u64 sm_chunks;	/* Number of chunks */
	u64 sm_cibs;	/* Number of chunk-info blocks */
	u64 sm_blocks;	/* Number of blocks */
	u64 sm_free;	/* Number of free blocks */
};

/* Space manager data in memory */
struct spaceman {
	struct free_queue *sm_ip_fq; /* Free queue for internal pool */
	int sm_struct_size; /* Size of the spaceman structure on disk */
	u32 sm_obj_size; /* Size of the spaceman object */

	struct spaceman_device sm_dev[APFS_SD_COUNT];

	/* Spaceman info read from the on-disk structures */
	u64 sm_xid;
	u32 sm_blocks_per_chunk;
	u32 sm_chunks_per_cib;
	u32 sm_cibs_per_cab;
	u64 sm_ip_base;
	u64 sm_ip_block_count;
	u32 sm_total_cib_count;
	u32 sm_total_cab_count;
	u64 sm_total_chunk_count;
	u64 sm_total_free_count;

	/* Spaceman info measured by the fsck */
	u64 sm_reserve_block_num; /* Blocks that are reserved for volumes */
	u64 sm_reserve_alloc_num; /* Blocks already alloced by those volumes */
};

/*
 * Free queue data in memory.  This is a subclass of struct btree; a free queue
 * btree can simply be cast to a free_queue structure.
 */
struct free_queue {
	/* This must always remain the first field */
	struct btree sfq_btree; /* B-tree structure for the free queue */
	int sfq_index;		/* Position in the free queue array */

	/* Free queue stats as measured by the fsck */
	u64 sfq_count;		/* Total count of free blocks in the queue */
	u64 sfq_oldest_xid;	/* First transaction id in the queue */
};

extern void container_bmap_mark_as_used(u64 paddr, u64 length);
extern void ip_bmap_mark_as_used(u64 paddr, u64 length);
extern void check_spaceman(u64 oid);
extern void parse_free_queue_record(struct apfs_spaceman_free_queue_key *key,
				    void *val, int len, struct btree *btree);

#endif	/* _SPACEMAN_H */
