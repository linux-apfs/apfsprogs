/*
 * Copyright (C) 2022 Ernesto A. Fernández <ernesto@corellium.com>
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <apfs/raw.h>
#include <apfs/types.h>
#include "apfsck.h"
#include "htable.h"
#include "key.h"
#include "snapshot.h"
#include "super.h"

/**
 * free_snap - Free a snapshot structure
 * @entry: the entry to free
 */
static void free_snap(struct htable_entry *entry)
{
	free(entry);
}

/**
 * free_snap_table - Free the snapshot hash table and all its entries
 * @table: table to free
 */
void free_snap_table(struct htable_entry **table)
{
	free_htable(table, free_snap);
}

/**
 * get_snapshot - Find or create a snapshot structure in the snapshot hash table
 * @xid: transaction id for the snapshot
 *
 * Returns the snapshot structure, after creating it if necessary.
 */
struct snapshot *get_snapshot(u64 xid)
{
	struct htable_entry *entry;

	entry = get_htable_entry(xid, sizeof(struct snapshot), vsb->v_snap_table);
	return (struct snapshot *)entry;
}

/**
 * parse_snap_name_record - Parse and check a snapshot name record value
 * @key:	pointer to the raw key
 * @val:	pointer to the raw value
 * @len:	length of the raw value
 *
 * Internal consistency of @key must be checked before calling this function.
 */
static void parse_snap_name_record(struct apfs_snap_name_key *key, struct apfs_snap_name_val *val, int len)
{
	struct snapshot *snap = NULL;

	if (len != sizeof(*val))
		report("Snapshot name record", "wrong length for value.");

	snap = get_snapshot(le64_to_cpu(val->snap_xid));
	if (snap->sn_name_seen)
		report("Snapshot tree", "snap with two name records.");
	snap->sn_name_seen = true;

	if (!snap->sn_meta_seen || !snap->sn_meta_name)
		report("Snapshot tree", "missing a metadata record.");
	if (strcmp((char *)key->name, snap->sn_meta_name) != 0)
		report("Snapshot tree", "inconsistent names for snapshot.");

	++vsb->v_snap_count;
}

/**
 * parse_snap_metadata_record - Parse and check a snapshot metadata record value
 * @key:	pointer to the raw key
 * @val:	pointer to the raw value
 * @len:	length of the raw value
 *
 * Internal consistency of @key must be checked before calling this function.
 */
static void parse_snap_metadata_record(struct apfs_snap_metadata_key *key, struct apfs_snap_metadata_val *val, int len)
{
	struct snapshot *snap = NULL;
	int namelen;

	if (len < sizeof(*val) + 1)
		report("Snapshot metadata record", "value is too small.");
	if (*((char *)val + len - 1) != 0)
		report("Snapshot metadata record", "name lacks NULL-termination.");

	namelen = le16_to_cpu(val->name_len);
	if (strlen((char *)val->name) + 1 != namelen)
		report("Snapshot metadata record", "wrong name length.");
	if (len != sizeof(*val) + namelen)
		report("Snapshot metadata record", "size of value doesn't match name length.");

	snap = get_snapshot(cat_cnid(&key->hdr));
	if (snap->sn_meta_seen)
		report("Snapshot tree", "snap with two metadata records.");
	snap->sn_meta_seen = true;

	snap->sn_meta_name = calloc(1, namelen);
	if (!snap->sn_meta_name)
		system_error();
	strcpy(snap->sn_meta_name, (char *)val->name);

	if (le32_to_cpu(val->extentref_tree_type) != (APFS_OBJ_PHYSICAL | APFS_OBJECT_TYPE_BTREE))
		report("Snapshot metadata", "wrong type for extentref tree.");
	if (val->flags)
		report_unknown("Snapshot flags");
}

/**
 * parse_snap_record - Parse and check a snapshot tree record value
 * @key:	pointer to the raw key
 * @val:	pointer to the raw value
 * @len:	length of the raw value
 *
 * Internal consistency of @key must be checked before calling this function.
 */
void parse_snap_record(void *key, void *val, int len)
{
	switch (cat_type(key)) {
	case APFS_TYPE_SNAP_METADATA:
		return parse_snap_metadata_record(key, val, len);
	case APFS_TYPE_SNAP_NAME:
		return parse_snap_name_record(key, val, len);
	default:
		report(NULL, "Bug!");
	}
}
