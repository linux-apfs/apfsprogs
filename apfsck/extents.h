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
struct apfs_phys_ext_key;

/* Physical extent records */
#define APFS_PEXT_LEN_MASK	0x0fffffffffffffffULL
#define APFS_PEXT_KIND_MASK	0xf000000000000000ULL
#define APFS_PEXT_KIND_SHIFT	60

/* The kind of a physical extent record */
enum {
	APFS_KIND_ANY		= 0,
	APFS_KIND_NEW		= 1,
	APFS_KIND_UPDATE	= 2,
	APFS_KIND_DEAD		= 3,
	APFS_KIND_UPDATE_REFCNT	= 4,

	APFS_KIND_INVALID	= 255 /* This is weird, won't fit in 4 bits */
};

/*
 * Structure of a physical extent record
 */
struct apfs_phys_ext_val {
	__le64 len_and_kind;
	__le64 owning_obj_id;
	__le32 refcnt;
} __packed;

/*
 * Physical extent record data in memory
 */
struct extref_record {
	u64 phys_addr;	/* First block number */
	u64 blocks;	/* Block count */
	u64 owner;	/* Owning object id */
	u32 refcnt;	/* Reference count */
};

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

/*
 * Physical extent data in memory
 */
struct extent {
	/* Hash table entry header (struct htable_entry_header from htable.h) */
	struct {
		union htable_entry	*e_next;
		u64			e_bno;	/* First physical block */
	};

	u8		e_obj_type;	/* Type of the owner objects */

	/* Extent stats read from the physical extent structure */
	u32		e_refcnt;	/* Reference count */

	/* Extent stats measured by the fsck */
	u32		e_references;	/* Number of references to extent */
	u64		e_latest_owner;	/* Last owner counted on e_references */
};

/*
 * Structure used to register each physical extent for a dstream, so that the
 * references can later be counted.  The same extent structure might be shared
 * by several dstreams.
 */
struct listed_extent {
	u64			paddr;	 /* Physical address for the extent */
	struct listed_extent	*next;	 /* Next entry in linked list */
};

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

	/* Linked list of physical extents for dstream */
	struct listed_extent *d_extents;

	u8		d_obj_type;	/* Type of the owner objects */
	u64		d_owner;	/* Owner id for the extentref tree */
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
extern void free_extent_table(union htable_entry **table);
extern struct dstream *get_dstream(u64 ino);
extern void parse_extent_record(struct apfs_file_extent_key *key,
				struct apfs_file_extent_val *val, int len);
extern void parse_dstream_id_record(struct apfs_dstream_id_key *key,
				    struct apfs_dstream_id_val *val, int len);
extern u64 parse_phys_ext_record(struct apfs_phys_ext_key *key,
				 struct apfs_phys_ext_val *val, int len);

#endif	/* _EXTENTS_H */
