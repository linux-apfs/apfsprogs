/*
 *  apfsprogs/apfsck/xattr.h
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _XATTR_H
#define _XATTR_H

#include "types.h"
#include "inode.h"

struct apfs_xattr_key;

/* Extended attributes constants */
#define APFS_XATTR_MAX_EMBEDDED_SIZE	3804

/* Extended attributes names */
#define APFS_XATTR_NAME_SYMLINK		"com.apple.fs.symlink"
#define APFS_XATTR_NAME_COMPRESSED	"com.apple.decmpfs"

/* Extended attributes flags */
enum {
	APFS_XATTR_DATA_STREAM		= 0x00000001,
	APFS_XATTR_DATA_EMBEDDED	= 0x00000002,
	APFS_XATTR_FILE_SYSTEM_OWNED	= 0x00000004,
	APFS_XATTR_RESERVED_8		= 0x00000008,
};

#define APFS_XATTR_VALID_FLAGS 0x0000000f

/*
 * Structure of the value of an extended attributes record
 */
struct apfs_xattr_val {
	__le16 flags;
	__le16 xdata_len;
	u8 xdata[0];
} __packed;

/*
 * Structure used to store the data of an extended attributes record
 */
struct apfs_xattr_dstream {
	__le64 xattr_obj_id;
	struct apfs_dstream dstream;
} __packed;

extern void parse_xattr_record(struct apfs_xattr_key *key,
			       struct apfs_xattr_val *val, int len);

#endif	/* _XATTR_H */
