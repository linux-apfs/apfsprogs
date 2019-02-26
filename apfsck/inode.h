/*
 *  apfsprogs/apfsck/inode.h
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _INODE_H
#define _INODE_H

#include "types.h"

struct apfs_inode_key;

/* Inode numbers for special inodes */
#define APFS_INVALID_INO_NUM		0

#define APFS_ROOT_DIR_PARENT		1	/* Root directory parent */
#define APFS_ROOT_DIR_INO_NUM		2	/* Root directory */
#define APFS_PRIV_DIR_INO_NUM		3	/* Private directory */
#define APFS_SNAP_DIR_INO_NUM		6	/* Snapshots metadata */

/* Smallest inode number available for user content */
#define APFS_MIN_USER_INO_NUM		16

/* File mode flags */
#define S_IFMT  00170000
#define S_IFSOCK 0140000
#define S_IFLNK	 0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

/*
 * Structure of an inode as stored as a B-tree value
 */
struct apfs_inode_val {
/*00*/	__le64 parent_id;
	__le64 private_id;
/*10*/	__le64 create_time;
	__le64 mod_time;
	__le64 change_time;
	__le64 access_time;
/*30*/	__le64 internal_flags;
	union {
		__le32 nchildren;
		__le32 nlink;
	};
	__le32 default_protection_class;
/*40*/	__le32 write_generation_counter;
	__le32 bsd_flags;
	__le32 owner;
	__le32 group;
/*50*/	__le16 mode;
	__le16 pad1;
	__le64 pad2;
/*5C*/	u8 xfields[];
} __packed;

/* Extended field types */
#define APFS_INO_EXT_TYPE_SNAP_XID 1
#define APFS_INO_EXT_TYPE_DELTA_TREE_OID 2
#define APFS_INO_EXT_TYPE_DOCUMENT_ID 3
#define APFS_INO_EXT_TYPE_NAME 4
#define APFS_INO_EXT_TYPE_PREV_FSIZE 5
#define APFS_INO_EXT_TYPE_RESERVED_6 6
#define APFS_INO_EXT_TYPE_FINDER_INFO 7
#define APFS_INO_EXT_TYPE_DSTREAM 8
#define APFS_INO_EXT_TYPE_RESERVED_9 9
#define APFS_INO_EXT_TYPE_DIR_STATS_KEY 10
#define APFS_INO_EXT_TYPE_FS_UUID 11
#define APFS_INO_EXT_TYPE_RESERVED_12 12
#define APFS_INO_EXT_TYPE_SPARSE_BYTES 13
#define APFS_INO_EXT_TYPE_RDEV 14

/*
 * Structure used to store the number and size of extended fields of an inode
 */
struct apfs_xf_blob {
	__le16 xf_num_exts;
	__le16 xf_used_data;
	u8 xf_data[];
} __packed;

/*
 * Structure used to store an inode's extended field
 */
struct apfs_x_field {
	u8 x_type;
	u8 x_flags;
	__le16 x_size;
} __packed;

/*
 * Structure of a data stream record
 */
struct apfs_dstream_id_val {
	__le32 refcnt;
} __packed;

/*
 * Structure used to store information about a data stream
 */
struct apfs_dstream {
	__le64 size;
	__le64 alloced_size;
	__le64 default_crypto_id;
	__le64 total_bytes_written;
	__le64 total_bytes_read;
} __packed;

/*
 * Structure used to store directory information
 */
struct apfs_dir_stats_val {
	__le64 num_children;
	__le64 total_size;
	__le64 chained_key;
	__le64 gen_count;
} __packed;

#define INODE_TABLE_BUCKETS	512	/* So the hash table array fits in 4k */

/*
 * Inode data in memory
 */
struct inode {
	u64		i_ino;		/* Inode number */
	u64		i_private_id;	/* Id of the inode's data stream */
	bool		i_seen;		/* Has this inode been seen? */

	/* Inode information read from its record (or from its dentries) */
	u16		i_mode;		/* File mode */
	union {
		u32	i_nchildren;	/* Number of children of directory */
		u32	i_nlink;	/* Number of hard links to file */
	};
	u64		i_size;		/* Inode size */
	u64		i_alloced_size;	/* Inode size, including unused */
	u64		i_sparse_bytes;	/* Number of sparse bytes */
	u32		i_rdev;		/* Device ID */

	/* Inode stats measured by the fsck */
	u32	i_child_count;		/* Number of children of directory */
	u32	i_link_count;		/* Number of dentries for file */

	struct inode	*i_next;	/* Next inode in linked list */
};

extern struct inode **alloc_inode_table();
extern void free_inode_table(struct inode **table);
extern struct inode *get_inode(u64 ino, struct inode **table);
extern void check_inode_ids(u64 ino, u64 parent_ino);
extern void parse_inode_record(struct apfs_inode_key *key,
			       struct apfs_inode_val *val, int len);

#endif	/* _INODE_H */
