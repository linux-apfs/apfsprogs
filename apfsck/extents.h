/*
 *  apfsprogs/apfsck/extents.h
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _EXTENTS_H
#define _EXTENTS_H

#include "types.h"

struct apfs_file_extent_key;

/* File extent records */
#define APFS_FILE_EXTENT_LEN_MASK	0x00ffffffffffffffULL
#define APFS_FILE_EXTENT_FLAG_MASK	0xff00000000000000ULL
#define APFS_FILE_EXTENT_FLAG_SHIFT	56

/*
 * Structure of a file extent record
 */
struct apfs_file_extent_val {
	__le64 len_and_flags;
	__le64 phys_block_num;
	__le64 crypto_id;
} __packed;

#define DSTREAM_TABLE_BUCKETS	512	/* So the hash table array fits in 4k */

/*
 * Dstream data in memory
 */
struct dstream {
	u64		d_id;		/* Id of the dstream */
	u8		d_obj_type;	/* Type of the owner objects */

	/* Dstream stats read from the dstream structure */
	u64		d_size;		/* Dstream size */
	u64		d_alloced_size;	/* Dstream size, including unused */

	/* Dstream stats measured by the fsck */
	u64		d_bytes;	/* Size of the extents read so far */
	u64		d_sparse_bytes;	/* Size of the holes read so far */

	struct dstream	*d_next;	/* Next dstream in linked list */
};

extern struct dstream **alloc_dstream_table();
extern void free_dstream_table(struct dstream **table);
extern struct dstream *get_dstream(u64 ino, struct dstream **table);
extern void parse_extent_record(struct apfs_file_extent_key *key,
				struct apfs_file_extent_val *val, int len);

#endif	/* _EXTENTS_H */
