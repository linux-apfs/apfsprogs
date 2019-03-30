/*
 *  apfsprogs/apfsck/extents.c
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "apfsck.h"
#include "extents.h"
#include "htable.h"
#include "inode.h"
#include "key.h"
#include "super.h"

/**
 * check_dstream_stats - Verify the stats gathered by the fsck vs the metadata
 * @dstream: dstream structure to check
 */
static void check_dstream_stats(struct dstream *dstream)
{
	if (!dstream->d_references)
		report("Data stream", "has no references.");
	if (dstream->d_id < APFS_MIN_USER_INO_NUM)
		report("Data stream", "invalid or reserved id.");
	if (dstream->d_id >= vsb->v_next_obj_id)
		report("Data stream", "free id in use.");

	if (dstream->d_obj_type == APFS_TYPE_XATTR) {
		if (dstream->d_seen || dstream->d_references != 1)
			report("Data stream", "xattrs can't be cloned.");
	} else {
		if (!dstream->d_seen)
			report("Data stream", "missing reference count.");
		if (dstream->d_refcnt != dstream->d_references)
			report("Data stream", "bad reference count.");
	}

	if (dstream->d_bytes < dstream->d_size)
		report("Data stream", "some extents are missing.");
	if (dstream->d_bytes != dstream->d_alloced_size)
		report("Data stream", "wrong allocated space.");
}

/**
 * free_dstream - Free a dstream structure after performing some final checks
 * @entry: the entry to free
 */
static void free_dstream(union htable_entry *entry)
{
	struct dstream *dstream = &entry->dstream;
	struct listed_cnid *cnid;

	/* The dstreams must be freed before the cnids */
	assert(vsb->v_cnid_table);

	/* To check for reuse, put all filesystem object ids in a list */
	cnid = get_listed_cnid(dstream->d_id);
	if (cnid->c_state == CNID_USED)
		report("Catalog", "a filesystem object id was used twice.");
	cnid->c_state = CNID_USED;

	check_dstream_stats(dstream);
	free(entry);
}

/**
 * free_dstream_table - Free the dstream hash table and all its dentries
 * @table: table to free
 */
void free_dstream_table(union htable_entry **table)
{
	free_htable(table, free_dstream);
}

/**
 * get_dstream - Find or create a dstream structure in the dstream hash table
 * @id:		id of the dstream
 *
 * Returns the dstream structure, after creating it if necessary.
 */
struct dstream *get_dstream(u64 id)
{
	union htable_entry *entry;

	entry = get_htable_entry(id, sizeof(struct dstream),
				 vsb->v_dstream_table);
	return &entry->dstream;
}

/**
 * parse_extent_record - Parse an extent record value and check for corruption
 * @key:	pointer to the raw key
 * @val:	pointer to the raw value
 * @len:	length of the raw value
 *
 * Internal consistency of @key must be checked before calling this function.
 */
void parse_extent_record(struct apfs_file_extent_key *key,
			 struct apfs_file_extent_val *val, int len)
{
	struct dstream *dstream;
	u64 length, flags;

	if (len != sizeof(*val))
		report("Extent record", "wrong size of value.");

	/* TODO: checks for crypto_id */
	length = le64_to_cpu(val->len_and_flags) & APFS_FILE_EXTENT_LEN_MASK;
	if (!length)
		report("Extent record", "length is zero.");
	if (length & (sb->s_blocksize - 1))
		report("Extent record", "length isn't multiple of block size.");

	flags = le64_to_cpu(val->len_and_flags) & APFS_FILE_EXTENT_FLAG_MASK;
	if (flags)
		report("Extent record", "no flags should be set.");

	dstream = get_dstream(cat_cnid(&key->hdr));
	if (dstream->d_bytes != le64_to_cpu(key->logical_addr))
		report("Data stream", "extents are not consecutive.");
	dstream->d_bytes += length;

	if (!le64_to_cpu(val->phys_block_num)) /* This is a hole */
		dstream->d_sparse_bytes += length;
}

/**
 * parse_dstream_id_record - Parse a dstream id record value
 * @key:	pointer to the raw key
 * @val:	pointer to the raw value
 * @len:	length of the raw value
 *
 * Internal consistency of @key must be checked before calling this function.
 */
void parse_dstream_id_record(struct apfs_dstream_id_key *key,
			     struct apfs_dstream_id_val *val, int len)
{
	struct dstream *dstream;

	if (len != sizeof(*val))
		report("Dstream id record", "wrong size of value.");

	dstream = get_dstream(cat_cnid(&key->hdr));
	dstream->d_seen = true;
	dstream->d_refcnt = le32_to_cpu(val->refcnt);
}

/**
 * parse_phys_ext_record - Parse and check a physical extent record value
 * @key:	pointer to the raw key
 * @val:	pointer to the raw value
 * @len:	length of the raw value
 *
 * Internal consistency of @key must be checked before calling this function.
 */
void parse_phys_ext_record(struct apfs_phys_ext_key *key,
			   struct apfs_phys_ext_val *val, int len)
{
	u8 kind;
	u64 length;

	if (len != sizeof(*val))
		report("Physical extent record", "wrong size of value.");

	kind = le64_to_cpu(val->len_and_kind) >> APFS_PEXT_KIND_SHIFT;
	if (kind != APFS_KIND_NEW && kind != APFS_KIND_UPDATE)
		report("Physical extent record", "invalid kind");
	if (kind == APFS_KIND_UPDATE)
		report_unknown("Snapshots");

	length = le64_to_cpu(val->len_and_kind) & APFS_PEXT_LEN_MASK;
	if (!length)
		report("Physical extent record", "has no blocks.");

	if (!val->refcnt)
		report("Physical extent record", "should have been deleted.");
}
