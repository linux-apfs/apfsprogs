/*
 *  apfsprogs/apfsck/dir.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include "apfsck.h"
#include "dir.h"
#include "inode.h"
#include "key.h"
#include "super.h"

/**
 * parse_dentry_record - Parse a dentry record value and check for corruption
 * @key:	pointer to the raw key
 * @val:	pointer to the raw value
 * @len:	length of the raw value
 *
 * Internal consistency of @key must be checked before calling this function.
 */
void parse_dentry_record(struct apfs_drec_hashed_key *key,
			 struct apfs_drec_val *val, int len)
{
	u64 ino, parent_ino;
	struct inode *inode, *parent;

	if (len < sizeof(*val))
		report("Dentry record", "value is too small.");

	ino = le64_to_cpu(val->file_id);
	inode = get_inode(ino, vsb->v_inode_table);
	inode->i_link_count++;

	parent_ino = cat_cnid(&key->hdr);
	if (parent_ino != APFS_ROOT_DIR_PARENT) {
		parent = get_inode(parent_ino, vsb->v_inode_table);
		if (!parent->i_seen) /* The b-tree keys are in order */
			report("Dentry record", "parent inode missing");
		if ((parent->i_mode & S_IFMT) != S_IFDIR)
			report("Dentry record", "parent inode not directory.");
		parent->i_child_count++;
	}
}
