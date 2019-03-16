/*
 *  apfsprogs/apfsck/htable.h
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _HTABLE_H
#define _HTABLE_H

#include "extents.h"
#include "inode.h"
#include "key.h"
#include "types.h"

#define HTABLE_BUCKETS	512	/* So the hash table array fits in 4k */

/*
 * Structure of the common header for hash table entries
 */
struct htable_entry_header {
	union htable_entry	*h_next;	/* Next entry in linked list */
	u64			h_id;		/* Catalog object id of entry */
};

/*
 * Generic hash table entry
 */
union htable_entry {
	struct htable_entry_header	header;		/* Common header */
	struct inode			inode;		/* Inode data */
	struct dstream			dstream;	/* Dstream data */
};

extern union htable_entry **alloc_htable();
extern void free_htable(union htable_entry **table,
			void (*free_entry)(union htable_entry *));
extern union htable_entry *get_htable_entry(u64 id, int size,
					    union htable_entry **table);

#endif	/* _HTABLE_H */
