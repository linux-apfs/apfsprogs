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
