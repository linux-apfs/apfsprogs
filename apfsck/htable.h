/*
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _HTABLE_H
#define _HTABLE_H

#include <apfs/types.h>
#include "btree.h"
#include "extents.h"
#include "inode.h"
#include "key.h"
#include "super.h"

#define HTABLE_BUCKETS	512	/* So the hash table array fits in 4k */

/*
 * Structure of the common header for hash table entries
 */
struct htable_entry_header {
	union htable_entry	*h_next;	/* Next entry in linked list */
	u64			h_id;		/* Catalog object id of entry */
};

/* State of the in-memory listed cnid structure */
#define CNID_UNUSED		0 /* The cnid is unused */
#define CNID_USED		1 /* The cnid is used, and can't be reused */
#define CNID_DSTREAM_ALLOWED	2 /* The cnid can be reused by one dstream */

/*
 * Structure used to register each catalog node id (cnid) that has been seen,
 * and check that they are not repeated.
 */
struct listed_cnid {
	/* Hash table entry header (struct htable_entry_header) */
	struct {
		union htable_entry	*c_next;
		u64			c_id;
	};
	u8				c_state;
};

/*
 * Generic hash table entry
 */
union htable_entry {
	struct htable_entry_header	header;		/* Common header */
	struct inode			inode;		/* Inode data */
	struct dstream			dstream;	/* Dstream data */
	struct listed_cnid		listed_cnid;	/* Catalog id data */
	struct extent			extent;		/* Extent data */
	struct cpoint_map		mapping;	/* Checkpoint map */
	struct omap_record		omap_rec;	/* Object map record */
};

extern union htable_entry **alloc_htable();
extern void free_htable(union htable_entry **table,
			void (*free_entry)(union htable_entry *));
extern union htable_entry *get_htable_entry(u64 id, int size,
					    union htable_entry **table);
extern void free_cnid_table(union htable_entry **table);
extern struct listed_cnid *get_listed_cnid(u64 id);

#endif	/* _HTABLE_H */
