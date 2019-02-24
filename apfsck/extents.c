/*
 *  apfsprogs/apfsck/extents.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include "apfsck.h"
#include "extents.h"
#include "key.h"
#include "super.h"

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
}
