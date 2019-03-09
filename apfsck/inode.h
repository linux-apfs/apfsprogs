/*
 *  apfsprogs/apfsck/inode.h
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _INODE_H
#define _INODE_H

#include "types.h"

struct apfs_inode_key;
struct apfs_sibling_link_key;

/* Inode numbers for special inodes */
#define APFS_INVALID_INO_NUM		0

#define APFS_ROOT_DIR_PARENT		1	/* Root directory parent */
#define APFS_ROOT_DIR_INO_NUM		2	/* Root directory */
#define APFS_PRIV_DIR_INO_NUM		3	/* Private directory */
#define APFS_SNAP_DIR_INO_NUM		6	/* Snapshots metadata */

/* Smallest inode number available for user content */
#define APFS_MIN_USER_INO_NUM		16

/* Inode internal flags */
#define APFS_INODE_IS_APFS_PRIVATE		0x00000001
#define APFS_INODE_MAINTAIN_DIR_STATS		0x00000002
#define APFS_INODE_DIR_STATS_ORIGIN		0x00000004
#define APFS_INODE_PROT_CLASS_EXPLICIT		0x00000008
#define APFS_INODE_WAS_CLONED			0x00000010
#define APFS_INODE_FLAG_UNUSED			0x00000020
#define APFS_INODE_HAS_SECURITY_EA		0x00000040
#define APFS_INODE_BEING_TRUNCATED		0x00000080
#define APFS_INODE_HAS_FINDER_INFO		0x00000100
#define APFS_INODE_IS_SPARSE			0x00000200
#define APFS_INODE_WAS_EVER_CLONED		0x00000400
#define APFS_INODE_ACTIVE_FILE_TRIMMED		0x00000800
#define APFS_INODE_PINNED_TO_MAIN		0x00001000
#define APFS_INODE_PINNED_TO_TIER2		0x00002000
#define APFS_INODE_HAS_RSRC_FORK		0x00004000
#define APFS_INODE_NO_RSRC_FORK			0x00008000
#define APFS_INODE_ALLOCATION_SPILLEDOVER	0x00010000

/* Masks for internal flags */
#define APFS_VALID_INTERNAL_INODE_FLAGS		0x0001ffdf
#define APFS_INODE_INHERITED_INTERNAL_FLAGS	(APFS_INODE_MAINTAIN_DIR_STATS)
#define APFS_INDOE_CLONED_INTERNAL_FLAGS	(APFS_INODE_HAS_RSRC_FORK \
						| APFS_INODE_NO_RSRC_FORK \
						| APFS_INODE_HAS_FINDER_INFO)
#define APFS_INODE_PINNED_MASK			(APFS_INODE_PINNED_TO_MAIN \
						| APFS_INODE_PINNED_TO_TIER2)

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

/* Extended field flags */
#define APFS_XF_DATA_DEPENDENT		0x01
#define APFS_XF_DO_NOT_COPY		0x02
#define APFS_XF_RESERVED_4		0x04
#define APFS_XF_CHILDREN_INHERIT	0x08
#define APFS_XF_USER_FIELD		0x10
#define APFS_XF_SYSTEM_FIELD		0x20
#define APFS_XF_RESERVED_40		0x40
#define APFS_XF_RESERVED_80		0x80

/* Constants for extended fields */
#define APFS_MIN_DOC_ID 3	/* Smallest not reserved document id */

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

/*
 * Structure of the value for a sibling link record.  These are used to
 * list the hard links for a given inode.
 */
struct apfs_sibling_val {
	__le64 parent_id;
	__le16 name_len;
	u8 name[0];
} __packed;

#define INODE_TABLE_BUCKETS	512	/* So the hash table array fits in 4k */

/* Flags for the bitmap of seen system xattrs (i_xattr_bmap) */
#define XATTR_BMAP_SYMLINK	0x01	/* Symlink target xattr */

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
	u64		i_flags;	/* Internal flags */
	u32		i_rdev;		/* Device ID */
	char		*i_name;	/* Name of primary link */

	/* Inode stats measured by the fsck */
	u8		i_xattr_bmap;	/* Bitmap of system xattrs for inode */
	u32		i_child_count;	/* Number of children of directory */
	u32		i_link_count;	/* Number of dentries for file */
	char		*i_first_name;	/* Name of first dentry encountered */
	struct sibling	*i_siblings;	/* Linked list of siblings for inode */

	struct inode	*i_next;	/* Next inode in linked list */
};

/*
 * Sibling link data in memory
 */
struct sibling {
	struct sibling	*s_next;	/* Next sibling in linked list */
	u64		s_id;		/* Sibling id */
	bool		s_checked;	/* Has this sibling been checked? */

	u64		s_parent_ino;	/* Inode number for parent */
	u16		s_name_len;	/* Name length */
	u8		s_name[0];	/* Name */
};

extern struct inode **alloc_inode_table();
extern void free_inode_table(struct inode **table);
extern struct inode *get_inode(u64 ino, struct inode **table);
extern void check_inode_ids(u64 ino, u64 parent_ino);
extern void parse_inode_record(struct apfs_inode_key *key,
			       struct apfs_inode_val *val, int len);
extern struct sibling *get_sibling(u64 id, int namelen, struct inode *inode);
extern void set_or_check_sibling(u64 parent_id, int namelen, u8 *name,
				 struct sibling *sibling);
extern void parse_sibling_record(struct apfs_sibling_link_key *key,
				 struct apfs_sibling_val *val, int len);
extern void check_xfield_flags(u8 flags);

#endif	/* _INODE_H */
