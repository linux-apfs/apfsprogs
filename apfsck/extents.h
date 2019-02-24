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

extern void parse_extent_record(struct apfs_file_extent_key *key,
				struct apfs_file_extent_val *val, int len);

#endif	/* _EXTENTS_H */
