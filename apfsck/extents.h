/*
 *  apfsprogs/apfsck/extents.h
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _EXTENTS_H
#define _EXTENTS_H

#include "types.h"

struct apfs_file_extent_key;
struct apfs_dstream_id_key;

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

/*
 * Structure of a data stream record
 */
struct apfs_dstream_id_val {
	__le32 refcnt;
} __packed;

#define DSTREAM_TABLE_BUCKETS	512	/* So the hash table array fits in 4k */

/*
 * Dstream data in memory
 */
struct dstream {
	/* Hash table entry header (struct htable_entry_header from htable.h) */
	struct {
		union htable_entry	*d_next;
		u64			d_id;
	};

	u8		d_obj_type;	/* Type of the owner objects */
	bool		d_seen;		/* Has the dstream record been seen? */

	/* Dstream stats read from the dstream structures */
	u64		d_size;		/* Dstream size */
	u64		d_alloced_size;	/* Dstream size, including unused */
	u32		d_refcnt;	/* Reference count */

	/* Dstream stats measured by the fsck */
	u64		d_bytes;	/* Size of the extents read so far */
	u64		d_sparse_bytes;	/* Size of the holes read so far */
	u32		d_references;	/* Number of references to dstream */
};

extern void free_dstream_table(union htable_entry **table);
extern struct dstream *get_dstream(u64 ino);
extern void parse_extent_record(struct apfs_file_extent_key *key,
				struct apfs_file_extent_val *val, int len);
extern void parse_dstream_id_record(struct apfs_dstream_id_key *key,
				    struct apfs_dstream_id_val *val, int len);

#endif	/* _EXTENTS_H */
