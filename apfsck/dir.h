/*
 *  apfsprogs/apfsck/dir.h
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _DIR_H
#define _DIR_H

#include "types.h"

struct apfs_drec_hashed_key;

/*
 * Structure of the value of a directory entry. This is the data in
 * the catalog nodes for record type APFS_TYPE_DIR_REC.
 */
struct apfs_drec_val {
	__le64 file_id;
	__le64 date_added;
	__le16 flags;
	u8 xfields[];
} __packed;

/* Extended field types */
#define APFS_DREC_EXT_TYPE_SIBLING_ID 1

extern void parse_dentry_record(struct apfs_drec_hashed_key *key,
				struct apfs_drec_val *val, int len);

#endif	/* _DIR_H */
