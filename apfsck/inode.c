/*
 *  apfsprogs/apfsck/inode.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "apfsck.h"
#include "inode.h"
#include "key.h"
#include "super.h"
#include "types.h"

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
 * free_inode_table - Free the inode hash table and all its inodes
 * @table: table to free
 */
void free_inode_table(struct inode **table)
{
	struct inode *current;
	struct inode *next;
	int i;

	for (i = 0; i < INODE_TABLE_BUCKETS; ++i) {
		current = table[i];
		while (current) {
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
static struct inode *get_inode(u64 ino, struct inode **table)
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

	new = malloc(sizeof(*new));
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
 * parse_xfields - Parse and check an inode extended fields
 * @xblob:	pointer to the beginning of the xfields in the inode value
 * @len:	length of the xfields
 *
 * Internal consistency of @key must be checked before calling this function.
 */
static void parse_xfields(struct apfs_xf_blob *xblob, int len)
{
	struct apfs_x_field *xfield;
	char *xval;
	int xcount;
	int i;

	if (len == 0) /* No extended fields */
		return;

	len -= sizeof(*xblob);
	if (len < 0)
		report("Inode records", "no room for extended fields.");
	xcount = le16_to_cpu(xblob->xf_num_exts);

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

		switch (xfield[i].x_type) {
		case APFS_INO_EXT_TYPE_FS_UUID:
			xlen = 16;
			break;
		case APFS_INO_EXT_TYPE_SNAP_XID:
		case APFS_INO_EXT_TYPE_DELTA_TREE_OID:
		case APFS_INO_EXT_TYPE_PREV_FSIZE:
		case APFS_INO_EXT_TYPE_SPARSE_BYTES:
			xlen = 8;
			break;
		case APFS_INO_EXT_TYPE_DOCUMENT_ID:
		case APFS_INO_EXT_TYPE_FINDER_INFO:
		case APFS_INO_EXT_TYPE_RDEV:
			xlen = 4;
			break;
		case APFS_INO_EXT_TYPE_NAME:
			xlen = strnlen(xval, len - 1) + 1;
			if (xval[xlen - 1] != 0)
				report("Inode xfield",
				       "name with no null termination");
			break;
		case APFS_INO_EXT_TYPE_DSTREAM:
			xlen = sizeof(struct apfs_dstream);
			break;
		case APFS_INO_EXT_TYPE_DIR_STATS_KEY:
			xlen = sizeof(struct apfs_dir_stats_val);
			break;
		case APFS_INO_EXT_TYPE_RESERVED_6:
		case APFS_INO_EXT_TYPE_RESERVED_9:
		case APFS_INO_EXT_TYPE_RESERVED_12:
			report("Inode xfield", "reserved type in use.");
			break;
		default:
			report("Inode xfield", "invalid type.");
		}

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

	if (len < sizeof(*val))
		report("Inode record", "value is too small.");

	inode = get_inode(cat_cnid(&key->hdr), vsb->v_inode_table);
	if (inode->i_seen)
		report("Catalog", "inode numbers are repeated.");
	inode->i_seen = true;

	if (inode->i_ino < APFS_MIN_USER_INO_NUM) {
		switch (inode->i_ino) {
		case APFS_INVALID_INO_NUM:
		case APFS_ROOT_DIR_PARENT:
			report("Inode record", "invalid inode number.");
		case APFS_ROOT_DIR_INO_NUM:
		case APFS_PRIV_DIR_INO_NUM:
		case APFS_SNAP_DIR_INO_NUM:
			/* All children of this fake parent? TODO: check this */
			if (le64_to_cpu(val->parent_id) != APFS_ROOT_DIR_PARENT)
				report("Root inode record", "bad parent id");
			break;
		default:
			report("Inode record", "reserved inode number.");
		}
	}

	inode->i_mode = le16_to_cpu(val->mode);
	switch (inode->i_mode & S_IFMT) {
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

	if (le16_to_cpu(val->pad1) || le64_to_cpu(val->pad2))
		report("Inode record", "padding should be zeroes.");

	parse_xfields((struct apfs_xf_blob *)val->xfields, len - sizeof(*val));
}
