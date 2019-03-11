/*
 *  apfsprogs/apfsck/extents.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdlib.h>
#include <stdio.h>
#include "apfsck.h"
#include "extents.h"
#include "key.h"
#include "super.h"

/**
 * alloc_dstream_table - Allocates and returns an empty dstream hash table
 */
struct dstream **alloc_dstream_table(void)
{
	struct dstream **table;

	table = calloc(DSTREAM_TABLE_BUCKETS, sizeof(*table));
	if (!table) {
		perror(NULL);
		exit(1);
	}
	return table;
}

/**
 * check_dstream_stats - Verify the stats gathered by the fsck vs the metadata
 * @dstream: dstream structure to check
 */
static void check_dstream_stats(struct dstream *dstream)
{
	if (dstream->d_obj_type != APFS_TYPE_INODE)
		/* No checks for xattrs, for now */
		return;

	if (dstream->d_bytes < dstream->d_size)
		report("Data stream", "some extents are missing.");
	if (dstream->d_bytes != dstream->d_alloced_size)
		report("Data stream", "wrong allocated space.");
}

/**
 * free_dstream_table - Free the dstream hash table and all its dstreams
 * @table: table to free
 */
void free_dstream_table(struct dstream **table)
{
	struct dstream *current;
	struct dstream *next;
	int i;

	for (i = 0; i < DSTREAM_TABLE_BUCKETS; ++i) {
		current = table[i];
		while (current) {
			check_dstream_stats(current);

			next = current->d_next;
			free(current);
			current = next;
		}
	}
	free(table);
}

/**
 * get_dstream - Find or create a dstream structure in a hash table
 * @id:		id of the dstream
 * @table:	the hash table
 *
 * Returns the dstream structure, after creating it if necessary.
 */
struct dstream *get_dstream(u64 id, struct dstream **table)
{
	int index = id % DSTREAM_TABLE_BUCKETS; /* Trivial hash function */
	struct dstream **entry_p = table + index;
	struct dstream *entry = *entry_p;
	struct dstream *new;

	/* Dstreams are ordered by id in each linked list */
	while (entry) {
		if (id == entry->d_id)
			return entry;
		if (id < entry->d_id)
			break;

		entry_p = &entry->d_next;
		entry = *entry_p;
	}

	new = calloc(1, sizeof(*new));
	if (!new) {
		perror(NULL);
		exit(1);
	}

	new->d_id = id;
	new->d_next = entry;
	*entry_p = new;
	return new;
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
	if (length & (sb->s_blocksize - 1))
		report("Extent record", "length isn't multiple of block size.");
	flags = le64_to_cpu(val->len_and_flags) & APFS_FILE_EXTENT_FLAG_MASK;
	if (flags)
		report("Extent record", "no flags should be set.");

	dstream = get_dstream(cat_cnid(&key->hdr), vsb->v_dstream_table);
	if (dstream->d_bytes != le64_to_cpu(key->logical_addr))
		report("Data stream", "extents are not consecutive.");
	dstream->d_bytes += length;

	if (!le64_to_cpu(val->phys_block_num)) /* This is a hole */
		dstream->d_sparse_bytes += length;
}
