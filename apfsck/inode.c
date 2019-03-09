/*
 *  apfsprogs/apfsck/inode.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "apfsck.h"
#include "extents.h"
#include "inode.h"
#include "key.h"
#include "super.h"
#include "types.h"

/**
 * check_inode_stats - Verify the stats gathered by the fsck vs the metadata
 * @inode: inode structure to check
 */
static void check_inode_stats(struct inode *inode)
{
	struct dstream *dstream;

	/* The inodes must be freed before the dstreams */
	assert(vsb->v_dstream_table);

	if ((inode->i_mode & S_IFMT) == S_IFDIR) {
		if (inode->i_link_count != 1)
			report("Inode record", "directory has hard links.");
		if (inode->i_nchildren != inode->i_child_count)
			report("Inode record", "wrong directory child count.");
	} else {
		if (inode->i_nlink != inode->i_link_count)
			report("Inode record", "wrong link count.");
	}

	dstream = get_dstream(inode->i_private_id, vsb->v_dstream_table);
	if (dstream->d_size < inode->i_size)
		report("Inode record", "some extents are missing.");
	if (dstream->d_size != inode->i_alloced_size)
		report("Inode record", "wrong allocated space for dstream.");
	if (dstream->d_sparse_bytes != inode->i_sparse_bytes)
		report("Inode record", "wrong count of sparse bytes.");

	if ((bool)(inode->i_xattr_bmap & XATTR_BMAP_SYMLINK) !=
	    (bool)((inode->i_mode & S_IFMT) == S_IFLNK))
		report("Inode record",
		       "symlink inode should come with target xattr.");
	if ((bool)(inode->i_xattr_bmap & XATTR_BMAP_RSRC_FORK) !=
	    (bool)(inode->i_flags & APFS_INODE_HAS_RSRC_FORK))
		report("Inode record", "wrong flag for resource fork.");
}

/**
 * alloc_inode_table - Allocates and returns an empty inode hash table
 */
struct inode **alloc_inode_table(void)
{
	struct inode **table;

	table = calloc(INODE_TABLE_BUCKETS, sizeof(*table));
	if (!table) {
		perror(NULL);
		exit(1);
	}
	return table;
}

/**
 * free_inode_names - Free all data on an inode's names
 * @inode: inode to free
 *
 * Frees the primary name and all sibling links, but not before running a few
 * remaining consistency checks.
 * remaining checking that
 * the number of listed siblings is correct, that all of them had both a record
 * and a dentry xfield, and that the primary link matches the primary name.
 */
static void free_inode_names(struct inode *inode)
{
	struct sibling *current = inode->i_siblings;
	struct sibling *next;
	u32 count = 0;

	if (!inode->i_name) /* Oddly, this seems to be always required */
		report("Inode record", "no name for primary link.");
	if (!inode->i_first_name)
		report("Catalog", "inode with no dentries.");

	if (current) {
		/* Primary link has lowest id, so it comes first in the list */
		if (strcmp(inode->i_name, (char *)current->s_name))
			report("Inode record", "wrong name for primary link.");
	} else {
		/* No siblings, so the primary link is the first and only */
		if (strcmp(inode->i_name, inode->i_first_name))
			report("Inode record", "wrong name for only link.");
	}
	free(inode->i_name);
	inode->i_name = NULL;
	free(inode->i_first_name);
	inode->i_first_name = NULL;

	while (current) {
		if (!current->s_checked)
			report("Catalog", "orphaned or missing sibling link.");
		next = current->s_next;
		free(current);
		current = next;
		++count;
	}

	/* Inodes with one link can have a sibling record, but don't need it */
	if (inode->i_link_count == 1 && count == 0)
		return;

	if (count != inode->i_link_count)
		report("Inode record",
		       "link count inconsistent with sibling records.");
}

/**
 * free_inode_table - Free the inode hash table and all its inodes
 * @table: table to free
 *
 * Also performs some consistency checks that can only be done after the whole
 * catalog has been parsed.
 */
void free_inode_table(struct inode **table)
{
	struct inode *current;
	struct inode *next;
	int i;

	for (i = 0; i < INODE_TABLE_BUCKETS; ++i) {
		current = table[i];
		while (current) {
			check_inode_stats(current);
			free_inode_names(current);

			next = current->i_next;
			free(current);
			current = next;
		}
	}
	free(table);
}

/**
 * get_inode - Find or create an inode structure in a hash table
 * @ino:	inode number
 * @table:	the hash table
 *
 * Returns the inode structure, after creating it if necessary.
 */
struct inode *get_inode(u64 ino, struct inode **table)
{
	int index = ino % INODE_TABLE_BUCKETS; /* Trivial hash function */
	struct inode **entry_p = table + index;
	struct inode *entry = *entry_p;
	struct inode *new;

	/* Inodes are ordered by ino in each linked list */
	while (entry) {
		if (ino == entry->i_ino)
			return entry;
		if (ino < entry->i_ino)
			break;

		entry_p = &entry->i_next;
		entry = *entry_p;
	}

	new = calloc(1, sizeof(*new));
	if (!new) {
		perror(NULL);
		exit(1);
	}

	new->i_seen = false;
	new->i_ino = ino;
	new->i_next = entry;
	*entry_p = new;
	return new;
}

/**
 * read_sparse_bytes_xfield - Parse and check an xfield that counts sparse bytes
 * @xval:	pointer to the xfield value
 * @len:	remaining length of the inode value
 * @inode:	struct to receive the results
 *
 * Returns the length of the xfield value.
 */
static int read_sparse_bytes_xfield(char *xval, int len, struct inode *inode)
{
	__le64 *sbytes;

	if (len < 8)
		report("Sparse bytes xfield", "doesn't fit in inode record.");
	sbytes = (__le64 *)xval;

	inode->i_sparse_bytes = le64_to_cpu(*sbytes);

	return sizeof(*sbytes);
}

/**
 * read_document_id_xfield - Parse and check a document id xfield
 * @xval:	pointer to the xfield value
 * @len:	remaining length of the inode value
 * @inode:	inode structure
 *
 * Returns the length of the xfield value.
 */
static int read_document_id_xfield(char *xval, int len, struct inode *inode)
{
	__le32 *id_raw;
	u32 id;

	if (len < 4)
		report("Document id xfield", "doesn't fit in inode record.");
	id_raw = (__le32 *)xval;
	id = le32_to_cpu(*id_raw);

	if (id < APFS_MIN_DOC_ID)
		report("Document id xfield", "invalid id in use.");
	if (id >= vsb->v_next_doc_id)
		report("Document id xfield", "free id in use.");

	return sizeof(*id_raw);
}

/**
 * read_rdev_xfield - Parse and check a device identifier xfield
 * @xval:	pointer to the xfield value
 * @len:	remaining length of the inode value
 * @inode:	struct to receive the results
 *
 * Returns the length of the xfield value.
 */
static int read_rdev_xfield(char *xval, int len, struct inode *inode)
{
	u16 filetype = inode->i_mode & S_IFMT;
	__le32 *rdev;

	assert(filetype); /* Mode must be set before parsing xfields */
	if (filetype != S_IFCHR && filetype != S_IFBLK)
		report("Inode record", "not device but has device identifier.");

	if (len < 4)
		report("Device ID xfield", "doesn't fit in inode record.");
	rdev = (__le32 *)xval;

	inode->i_rdev = le32_to_cpu(*rdev);
	if (!inode->i_rdev)
		report("Device ID xfield", "null ID in use.");

	return sizeof(*rdev);
}

/**
 * read_name_xfield - Parse a name xfield and check its consistency
 * @xval:	pointer to the xfield value
 * @len:	remaining length of the inode value
 * @inode:	struct to receive the results
 *
 * Returns the length of the xfield value.
 */
static int read_name_xfield(char *xval, int len, struct inode *inode)
{
	int xlen;

	xlen = strnlen(xval, len - 1) + 1;
	if (xval[xlen - 1] != 0)
		report("Name xfield", "name with no null termination");

	inode->i_name = malloc(xlen);
	if (!inode->i_name) {
		perror(NULL);
		exit(1);
	}
	strcpy(inode->i_name, xval);

	return xlen;
}

/**
 * read_dstream_xfield - Parse a dstream xfield and check its consistency
 * @xval:	pointer to the xfield value
 * @len:	remaining length of the inode value
 * @inode:	struct to receive the results
 *
 * Returns the length of the xfield value.
 */
static int read_dstream_xfield(char *xval, int len, struct inode *inode)
{
	struct apfs_dstream *dstream;

	if (len < sizeof(*dstream))
		report("Dstream xfield", "doesn't fit in inode record.");
	dstream = (struct apfs_dstream *)xval;

	inode->i_size = le64_to_cpu(dstream->size);
	inode->i_alloced_size = le64_to_cpu(dstream->alloced_size);

	return sizeof(*dstream);
}

/**
 * check_xfield_flags - Run common flag checks for all xfield types
 * @flags: flags to check
 */
void check_xfield_flags(u8 flags)
{
	if (flags & APFS_XF_RESERVED_4 || flags & APFS_XF_RESERVED_40 ||
	    flags & APFS_XF_RESERVED_80)
		report("Inode xfield", "reserved flag in use.");

	if (flags & APFS_XF_USER_FIELD && flags & APFS_XF_SYSTEM_FIELD)
		report("Inode xfield", "created by both user and kernel.");
}

/**
 * xbmap_set - Set an xfield type in the xfield bitmap
 * @bmap: the xfield bitmap
 * @type: the extended field type
 */
static inline void xbmap_set(u16 *bmap, u8 type)
{
	*bmap |= 1 << type;
}

/**
 * xbmap_test - Test if an xfield type is present in the xfield bitmap
 * @bmap: the xfield bitmap
 * @type: the extended field type
 */
static inline bool xbmap_test(u16 bmap, u8 type)
{
	return bmap & (1 << type);
}

/**
 * check_xfield_inode_flags - Check that xfields are consistent with inode flags
 * @bmap:	bitmap of xfield types seen in the inode
 * @flags:	inode flags
 */
static void check_xfield_inode_flags(u16 bmap, u64 flags)
{
	if (xbmap_test(bmap, APFS_INO_EXT_TYPE_DIR_STATS_KEY) !=
	    (bool)(flags & APFS_INODE_MAINTAIN_DIR_STATS))
		report("Inode record", "wrong setting for dir stats flag.");
	if (xbmap_test(bmap, APFS_INO_EXT_TYPE_SPARSE_BYTES) !=
	    (bool)(flags & APFS_INODE_IS_SPARSE))
		report("Inode record", "wrong setting for sparse flag.");

	/* Some inodes don't have finder info but still have the flag... */
	if (xbmap_test(bmap, APFS_INO_EXT_TYPE_FINDER_INFO) &&
	    !(flags & APFS_INODE_HAS_FINDER_INFO))
		report("Inode record", "wrong setting for finder info flag.");
	if (!xbmap_test(bmap, APFS_INO_EXT_TYPE_FINDER_INFO) &&
	    (flags & APFS_INODE_HAS_FINDER_INFO))
		report_weird("Finder info flag in inode record");
}

/**
 * parse_inode_xfields - Parse and check an inode extended fields
 * @xblob:	pointer to the beginning of the xfields in the inode value
 * @len:	length of the xfields
 * @inode:	struct to receive the results
 *
 * Internal consistency of @key must be checked before calling this function.
 */
static void parse_inode_xfields(struct apfs_xf_blob *xblob, int len,
				struct inode *inode)
{
	struct apfs_x_field *xfield;
	u16 type_bitmap = 0;
	char *xval;
	int xcount;
	int i;

	if (len == 0) { /* No extended fields */
		check_xfield_inode_flags(type_bitmap, inode->i_flags);
		return;
	}

	len -= sizeof(*xblob);
	if (len < 0)
		report("Inode records", "no room for extended fields.");

	xcount = le16_to_cpu(xblob->xf_num_exts);
	if (!xcount)
		report("Inode record", "xfield blob has no xfields.");

	xfield = (struct apfs_x_field *)xblob->xf_data;
	xval = (char *)xfield + xcount * sizeof(xfield[0]);
	len -= xcount * sizeof(xfield[0]);
	if (len < 0)
		report("Inode record", "number of xfields cannot fit.");

	/* The official reference seems to be wrong here */
	if (le16_to_cpu(xblob->xf_used_data) != len)
		report("Inode record", "value size incompatible with xfields.");

	for (i = 0; i < le16_to_cpu(xblob->xf_num_exts); ++i) {
		int xlen, xpad_len;
		u8 xflags = xfield[i].x_flags;

		check_xfield_flags(xflags);

		switch (xfield[i].x_type) {
		case APFS_INO_EXT_TYPE_FS_UUID:
			xlen = 16;
			report_unknown("UUID xfield");
			break;
		case APFS_INO_EXT_TYPE_PREV_FSIZE:
			xlen = 8;
			report_crash("Inode xfield");
			if (xflags != 0)
				report("Previous size xfield", "wrong flags.");
			break;
		case APFS_INO_EXT_TYPE_SNAP_XID:
			xlen = 8;
			report_unknown("Snapshot id xfield");
			break;
		case APFS_INO_EXT_TYPE_DELTA_TREE_OID:
			xlen = 8;
			report_unknown("Snapshot's extent delta list xfield");
			break;
		case APFS_INO_EXT_TYPE_SPARSE_BYTES:
			xlen = read_sparse_bytes_xfield(xval, len, inode);
			if (xflags != (APFS_XF_SYSTEM_FIELD |
				       APFS_XF_CHILDREN_INHERIT))
				report("Sparse bytes xfield", "wrong flags.");
			break;
		case APFS_INO_EXT_TYPE_DOCUMENT_ID:
			xlen = read_document_id_xfield(xval, len, inode);
			report_unknown("Document id xfield");
			break;
		case APFS_INO_EXT_TYPE_FINDER_INFO:
			xlen = 4;
			report_unknown("Finder info xfield");
			break;
		case APFS_INO_EXT_TYPE_RDEV:
			xlen = read_rdev_xfield(xval, len, inode);
			report_unknown("Device identifier xfield");
			break;
		case APFS_INO_EXT_TYPE_NAME:
			xlen = read_name_xfield(xval, len, inode);
			if (xflags != APFS_XF_DO_NOT_COPY)
				report("Name xfield", "wrong flags.");
			break;
		case APFS_INO_EXT_TYPE_DSTREAM:
			xlen = read_dstream_xfield(xval, len, inode);
			if (xflags != APFS_XF_SYSTEM_FIELD)
				report("Data stream xfield", "wrong flags.");
			break;
		case APFS_INO_EXT_TYPE_DIR_STATS_KEY:
			xlen = sizeof(struct apfs_dir_stats_val);
			report_unknown("Directory statistics xfield");
			break;
		case APFS_INO_EXT_TYPE_RESERVED_6:
		case APFS_INO_EXT_TYPE_RESERVED_9:
		case APFS_INO_EXT_TYPE_RESERVED_12:
			report("Inode xfield", "reserved type in use.");
			break;
		default:
			report("Inode xfield", "invalid type.");
		}

		if (xbmap_test(type_bitmap, xfield[i].x_type))
			report("Inode record", "two xfields of the same type.");
		xbmap_set(&type_bitmap, xfield[i].x_type);

		if (xlen != le16_to_cpu(xfield[i].x_size))
			report("Inode xfield", "wrong size");
		len -= xlen;
		xval += xlen;

		/* Attribute length is padded with zeroes to a multiple of 8 */
		xpad_len = ROUND_UP(xlen, 8) - xlen;
		len -= xpad_len;
		if (len < 0)
			report("Inode xfield", "does not fit in record value.");

		for (; xpad_len; ++xval, --xpad_len)
			if (*xval)
				report("Inode xfield", "non-zero padding.");
	}

	if (len)
		report("Inode record", "length of xfields does not add up.");

	check_xfield_inode_flags(type_bitmap, inode->i_flags);
}

/**
 * check_inode_internal_flags - Check basic consistency of inode flags
 * @flags: the flags
 */
static void check_inode_internal_flags(u64 flags)
{
	if ((flags & APFS_VALID_INTERNAL_INODE_FLAGS) != flags)
		report("Inode record", "invalid flags in use.");

	if ((flags & APFS_INODE_DIR_STATS_ORIGIN) &&
	    !(flags & APFS_INODE_MAINTAIN_DIR_STATS))
		report("Inode record", "incompatible directory stats flags.");
	if (flags & APFS_INODE_HAS_RSRC_FORK && flags & APFS_INODE_NO_RSRC_FORK)
		report("Inode record", "incompatible resource fork flags.");

	if (flags & APFS_INODE_BEING_TRUNCATED)
		report_crash("Inode internal flags");

	if (flags & APFS_INODE_PINNED_TO_MAIN ||
	    flags & APFS_INODE_PINNED_TO_TIER2 ||
	    flags & APFS_INODE_ALLOCATION_SPILLEDOVER)
		report_unknown("Fusion drive");
	if (flags & APFS_INODE_MAINTAIN_DIR_STATS)
		report_unknown("Directory statistics");
	if (flags & APFS_INODE_IS_APFS_PRIVATE)
		report_unknown("Private implementation inode");
}

/**
 * check_inode_ids - Check that an inode id is consistent with its parent id
 * @ino:	inode number
 * @parent_ino:	parent inode number
 */
void check_inode_ids(u64 ino, u64 parent_ino)
{
	if (ino < APFS_MIN_USER_INO_NUM) {
		switch (ino) {
		case APFS_INVALID_INO_NUM:
		case APFS_ROOT_DIR_PARENT:
			report("Inode record", "invalid inode number.");
		case APFS_ROOT_DIR_INO_NUM:
		case APFS_PRIV_DIR_INO_NUM:
		case APFS_SNAP_DIR_INO_NUM:
			/* All children of this fake parent? TODO: check this */
			if (parent_ino != APFS_ROOT_DIR_PARENT)
				report("Root inode record", "bad parent id");
			break;
		default:
			report("Inode record", "reserved inode number.");
		}
		return;
	}

	if (parent_ino < APFS_MIN_USER_INO_NUM) {
		switch (parent_ino) {
		case APFS_INVALID_INO_NUM:
			report("Inode record", "invalid parent inode number.");
		case APFS_ROOT_DIR_PARENT:
			report("Inode record", "root parent id for nonroot.");
		case APFS_ROOT_DIR_INO_NUM:
		case APFS_PRIV_DIR_INO_NUM:
		case APFS_SNAP_DIR_INO_NUM:
			/* These are fine */
			break;
		default:
			report("Inode record", "reserved parent inode number.");
		}
	}
}

/**
 * parse_inode_record - Parse an inode record value and check for corruption
 * @key:	pointer to the raw key
 * @val:	pointer to the raw value
 * @len:	length of the raw value
 *
 * Internal consistency of @key must be checked before calling this function.
 */
void parse_inode_record(struct apfs_inode_key *key,
			struct apfs_inode_val *val, int len)
{
	struct inode *inode;
	u16 mode, filetype;

	if (len < sizeof(*val))
		report("Inode record", "value is too small.");

	inode = get_inode(cat_cnid(&key->hdr), vsb->v_inode_table);
	if (inode->i_seen)
		report("Catalog", "inode numbers are repeated.");
	inode->i_seen = true;
	inode->i_private_id = le64_to_cpu(val->private_id);

	check_inode_ids(inode->i_ino, le64_to_cpu(val->parent_id));

	inode->i_flags = le64_to_cpu(val->internal_flags);
	check_inode_internal_flags(inode->i_flags);

	mode = le16_to_cpu(val->mode);
	filetype = mode & S_IFMT;

	/* A dentry may have already set the mode, but only the type bits */
	if (inode->i_mode && inode->i_mode != filetype)
		report("Inode record", "file mode doesn't match dentry type.");
	inode->i_mode = mode;

	switch (filetype) {
	case S_IFREG:
		vsb->v_file_count++;
		break;
	case S_IFDIR:
		if (inode->i_ino >= APFS_MIN_USER_INO_NUM)
			vsb->v_dir_count++;
		break;
	case S_IFLNK:
		vsb->v_symlink_count++;
		break;
	case S_IFSOCK:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO:
		vsb->v_special_count++;
		break;
	default:
		report("Inode record", "invalid file mode.");
	}

	inode->i_nlink = le32_to_cpu(val->nlink);

	if (le16_to_cpu(val->pad1) || le64_to_cpu(val->pad2))
		report("Inode record", "padding should be zeroes.");

	parse_inode_xfields((struct apfs_xf_blob *)val->xfields,
			    len - sizeof(*val), inode);

	if ((filetype == S_IFCHR || filetype == S_IFBLK) && !inode->i_rdev)
		report("Inode record", "device file with no device ID.");
}

/**
 * get_sibling - Find or create a sibling link structure for an inode
 * @id:		sibling id
 * @namelen:	length of the sibling name
 * @inode:	the inode
 *
 * Returns the sibling structure, after creating it if necessary.
 */
struct sibling *get_sibling(u64 id, int namelen, struct inode *inode)
{
	struct sibling **entry_p = &inode->i_siblings;
	struct sibling *entry = *entry_p;
	struct sibling *new;

	/* Siblings are ordered by id in the inode's linked list */
	while (entry) {
		if (id == entry->s_id)
			return entry;
		if (id < entry->s_id)
			break;

		entry_p = &entry->s_next;
		entry = *entry_p;
	}

	new = calloc(1, sizeof(*new) + namelen);
	if (!new) {
		perror(NULL);
		exit(1);
	}

	new->s_checked = false;
	new->s_id = id;
	new->s_next = entry;
	*entry_p = new;
	return new;
}

/**
 * set_or_check_sibling - Set or check the fields of a sibling structure
 * @parent_id:	parent id
 * @namelen:	length of the name
 * @name:	name of the sibling
 * @sibling:	the sibling structure
 *
 * When first called for @sibling, sets the three given fields.  On the second
 * call, checks that they are set to the correct values.
 */
void set_or_check_sibling(u64 parent_id, int namelen, u8 *name,
			  struct sibling *sibling)
{
	/* Whichever was read first, dentry or sibling, sets the fields */
	if (!sibling->s_name_len) {
		sibling->s_name_len = namelen;
		strcpy((char *)sibling->s_name, (char *)name);
		sibling->s_parent_ino = parent_id;
		return;
	}

	/* Fields already set, check them */
	if (sibling->s_name_len != namelen)
		report("Sibling record", "name length doesn't match dentry's.");
	if (strcmp((char *)sibling->s_name, (char *)name))
		report("Sibling record", "name doesn't match dentry's.");
	if (sibling->s_parent_ino != parent_id)
		report("Sibling record", "parent id doesn't match dentry's.");
	sibling->s_checked = true;
}

/**
 * parse_sibling_record - Parse and check a sibling link record value
 * @key:	pointer to the raw key
 * @val:	pointer to the raw value
 * @len:	length of the raw value
 *
 * Internal consistency of @key must be checked before calling this function.
 */
void parse_sibling_record(struct apfs_sibling_link_key *key,
			  struct apfs_sibling_val *val, int len)
{
	struct inode *inode;
	struct sibling *sibling;
	int namelen;

	if (len < sizeof(*val))
		report("Sibling link record", "value is too small.");
	namelen = le16_to_cpu(val->name_len);

	if (len != sizeof(*val) + namelen)
		report("Sibling link record", "wrong size of value.");
	if (val->name[namelen - 1] != 0)
		report("Sibling link record", "name lacks NULL-termination.");
	/* Name length doesn't need checking: it's the same for the dentry */

	inode = get_inode(cat_cnid(&key->hdr), vsb->v_inode_table);
	if (!inode->i_seen) /* The b-tree keys are in order */
		report("Sibling link record", "inode is missing");

	sibling = get_sibling(le64_to_cpu(key->sibling_id), namelen, inode);

	/* It seems that sibling ids come from the same pool as inode numbers */
	if (sibling->s_id < APFS_MIN_USER_INO_NUM)
		report("Sibling record", "invalid sibling id.");

	set_or_check_sibling(le64_to_cpu(val->parent_id), namelen, val->name,
			     sibling);
}
