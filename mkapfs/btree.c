/*
 *  apfsprogs/mkapfs/btree.c
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <apfs/raw.h>
#include "btree.h"
#include "mkapfs.h"
#include "object.h"

/**
 * set_omap_info - Set the info footer for an object map node
 * @info:	pointer to the on-disk info footer
 * @nkeys:	number of records in the omap
 */
static void set_omap_info(struct apfs_btree_info *info, int nkeys)
{
	info->bt_fixed.bt_flags = cpu_to_le32(APFS_BTREE_PHYSICAL);
	info->bt_fixed.bt_node_size = cpu_to_le32(param->blocksize);
	info->bt_fixed.bt_key_size = cpu_to_le32(sizeof(struct apfs_omap_key));
	info->bt_fixed.bt_val_size = cpu_to_le32(sizeof(struct apfs_omap_val));
	info->bt_longest_key = cpu_to_le32(sizeof(struct apfs_omap_key));
	info->bt_longest_val = cpu_to_le32(sizeof(struct apfs_omap_val));
	info->bt_key_count = cpu_to_le64(nkeys);
	info->bt_node_count = cpu_to_le64(1); /* Only one node: the root */
}

/* Constants used in managing the size of a node's table of contents */
#define BTREE_TOC_ENTRY_INCREMENT	8
#define BTREE_TOC_ENTRY_MAX_UNUSED	(2 * BTREE_TOC_ENTRY_INCREMENT)

/**
 * make_main_omap_root - Make the root node of the container's object map
 * @bno: block number to use
 */
static void make_main_omap_root(u64 bno)
{
	struct apfs_btree_node_phys *root;
	struct apfs_omap_key *key;
	struct apfs_omap_val *val;
	struct apfs_kvoff *kvoff;
	int toc_len, key_len, val_len, free_len;
	int head_len = sizeof(*root);
	int info_len = sizeof(struct apfs_btree_info);

	root = mmap(NULL, param->blocksize, PROT_READ | PROT_WRITE,
		    MAP_SHARED, fd, bno * param->blocksize);
	if (root == MAP_FAILED)
		system_error();
	memset(root, 0, param->blocksize);

	root->btn_flags = cpu_to_le16(APFS_BTNODE_ROOT | APFS_BTNODE_LEAF |
				      APFS_BTNODE_FIXED_KV_SIZE);

	/* Just one volume for now */
	root->btn_nkeys = cpu_to_le32(1);
	toc_len = BTREE_TOC_ENTRY_MAX_UNUSED * sizeof(*kvoff);
	key_len = 1 * sizeof(*key);
	val_len = 1 * sizeof(*val);

	/* Location of the one record */
	key = (void *)root + head_len + toc_len;
	val = (void *)root + param->blocksize - info_len - val_len;
	kvoff = (void *)root + head_len;
	kvoff->k = 0;
	kvoff->v = cpu_to_le16(val_len);

	/* Set the key and value for the one record */
	key->ok_oid = cpu_to_le64(FIRST_VOL_OID);
	key->ok_xid = cpu_to_le64(MKFS_XID);
	val->ov_size = cpu_to_le32(param->blocksize); /* Only size supported */
	val->ov_paddr = cpu_to_le64(FIRST_VOL_BNO);

	root->btn_table_space.off = 0;
	root->btn_table_space.len = cpu_to_le16(toc_len);

	free_len = param->blocksize -
		   head_len - toc_len - key_len - val_len - info_len;
	root->btn_free_space.off = cpu_to_le16(key_len);
	root->btn_free_space.len = cpu_to_le16(free_len);

	/* No fragmentation */
	root->btn_key_free_list.off = cpu_to_le16(APFS_BTOFF_INVALID);
	root->btn_key_free_list.len = 0;
	root->btn_val_free_list.off = cpu_to_le16(APFS_BTOFF_INVALID);
	root->btn_val_free_list.len = 0;

	set_omap_info((void *)root + param->blocksize - info_len, 1);
	set_object_header(&root->btn_o, MAIN_OMAP_ROOT_BNO,
			  APFS_OBJECT_TYPE_BTREE | APFS_OBJ_PHYSICAL,
			  APFS_OBJECT_TYPE_OMAP);
	munmap(root, param->blocksize);
}

/**
 * make_omap_btree - Make an object map
 * @bno:	block number to use
 * @is_vol:	is this the object map for a volume?
 */
void make_omap_btree(u64 bno, bool is_vol)
{
	struct apfs_omap_phys *omap;

	omap = mmap(NULL, param->blocksize, PROT_READ | PROT_WRITE,
		    MAP_SHARED, fd, bno * param->blocksize);
	if (omap == MAP_FAILED)
		system_error();
	memset(omap, 0, param->blocksize);

	if (!is_vol)
		omap->om_flags = cpu_to_le32(APFS_OMAP_MANUALLY_MANAGED);
	omap->om_tree_type = cpu_to_le32(APFS_OBJECT_TYPE_BTREE |
					 APFS_OBJ_PHYSICAL);
	omap->om_snapshot_tree_type = cpu_to_le32(APFS_OBJECT_TYPE_BTREE |
						  APFS_OBJ_PHYSICAL);
	omap->om_tree_oid = cpu_to_le64(MAIN_OMAP_ROOT_BNO);
	if (!is_vol)
		make_main_omap_root(MAIN_OMAP_ROOT_BNO);

	set_object_header(&omap->om_o, bno,
			  APFS_OBJ_PHYSICAL | APFS_OBJECT_TYPE_OMAP,
			  APFS_OBJECT_TYPE_INVALID);
	munmap(omap, param->blocksize);
}
