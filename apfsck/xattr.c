/*
 *  apfsprogs/apfsck/xattr.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include "apfsck.h"
#include "key.h"
#include "types.h"
#include "xattr.h"

/**
 * check_xattr_flags - Check basic consistency of xattr flags
 * @flags: the flags
 */
static void check_xattr_flags(u16 flags)
{
	if ((flags & APFS_XATTR_VALID_FLAGS) != flags)
		report("Xattr record", "invalid flags in use.");
	if (flags & APFS_XATTR_RESERVED_8)
		report("Xattr record", "reserved flag in use.");

	if ((bool)(flags & APFS_XATTR_DATA_STREAM) ==
	    (bool)(flags & APFS_XATTR_DATA_EMBEDDED))
		report("Xattr record", "must be either embedded or dstream.");
}

/**
 * parse_xattr_record - Parse a xattr record value and check for corruption
 * @key:	pointer to the raw key
 * @val:	pointer to the raw value
 * @len:	length of the raw value
 *
 * Internal consistency of @key must be checked before calling this function.
 */
void parse_xattr_record(struct apfs_xattr_key *key,
			struct apfs_xattr_val *val, int len)
{
	u16 flags;

	if (len < sizeof(*val))
		report("Xattr record", "value is too small.");
	len -= sizeof(*val);

	flags = le16_to_cpu(val->flags);
	check_xattr_flags(flags);

	if (flags & APFS_XATTR_DATA_STREAM) {
		if (len != sizeof(struct apfs_xattr_dstream))
			report("Xattr record",
			       "bad length for dstream structure.");
		if (len != le16_to_cpu(val->xdata_len))
			/* Never seems to happen, but the docs don't ban it */
			report_weird("Xattr data length for dstream structure");
	} else {
		if (len != le16_to_cpu(val->xdata_len))
			report("Xattr record", "bad length for embedded data.");
	}
}
