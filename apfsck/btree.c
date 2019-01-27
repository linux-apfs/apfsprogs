/*
 *  apfsprogs/apfsck/btree.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "btree.h"
#include "key.h"
#include "object.h"
#include "super.h"
#include "types.h"

/**
 * node_is_valid - Check basic sanity of the node index
 * @sb:		filesystem superblock
 * @node:	node to check
 *
 * Verifies that the node index fits in a single block, and that the number
 * of records fits in the index. Without this check a crafted filesystem could
 * pretend to have too many records, and calls to node_locate_key() and
 * node_locate_data() would read beyond the limits of the node.
 */
static bool node_is_valid(struct super_block *sb, struct node *node)
{
	int records = node->records;
	int index_size = node->key - sizeof(struct apfs_btree_node_phys);
	int entry_size;

	if (!records) /* Empty nodes could keep a multiple query spinning */
		return false;

	if (node->key > sb->s_blocksize)
		return false;

	entry_size = (node_has_fixed_kv_size(node)) ?
		sizeof(struct apfs_kvoff) : sizeof(struct apfs_kvloc);

	return records * entry_size <= index_size;
}

/**
 * read_node - Read a node header from disk
 * @sb:		filesystem superblock
 * @block:	number of the block where the node is stored
 * @fd:		file descriptor for the device
 *
 * Returns a pointer to the resulting apfs_node structure.
 */
static struct node *read_node(struct super_block *sb, u64 block, int fd)
{
	struct apfs_btree_node_phys *raw;
	struct node *node;

	raw = mmap(NULL, sb->s_blocksize, PROT_READ, MAP_PRIVATE,
		   fd, block * sb->s_blocksize);
	if (raw == MAP_FAILED) {
		perror(NULL);
		exit(1);
	}

	node = malloc(sizeof(*node));
	if (!node) {
		perror(NULL);
		exit(1);
	}

	node->flags = le16_to_cpu(raw->btn_flags);
	node->records = le32_to_cpu(raw->btn_nkeys);
	node->key = sizeof(*raw) + le16_to_cpu(raw->btn_table_space.off)
				+ le16_to_cpu(raw->btn_table_space.len);
	node->free = node->key + le16_to_cpu(raw->btn_free_space.off);
	node->data = node->free + le16_to_cpu(raw->btn_free_space.len);

	node->object.sb = sb;
	node->object.block_nr = block;
	node->object.oid = le64_to_cpu(raw->btn_o.o_oid);

	if (!obj_verify_csum(sb, &raw->btn_o)) {
		printf("Bad checksum for node in block 0x%llx\n",
		       (unsigned long long)block);
		exit(1);
	}
	if (!node_is_valid(sb, node)) {
		printf("Node in block 0x%llx is not sane\n",
		       (unsigned long long)block);
		exit(1);
	}

	node->raw = raw;
	return node;
}

/**
 * node_locate_key - Locate the key of a node record
 * @node:	node to be searched
 * @index:	number of the entry to locate
 * @off:	on return will hold the offset in the block
 *
 * Returns the length of the key. The function checks that this length fits
 * within the block; callers must use the returned value to make sure they
 * never operate outside its bounds.
 */
static int node_locate_key(struct node *node, int index, int *off)
{
	struct super_block *sb = node->object.sb;
	struct apfs_btree_node_phys *raw;
	int len;

	if (index >= node->records) {
		printf("Requested index out-of-bounds\n");
		exit(1);
	}

	raw = node->raw;
	if (node_has_fixed_kv_size(node)) {
		struct apfs_kvoff *entry;

		entry = (struct apfs_kvoff *)raw->btn_data + index;
		len = 16;
		/* Translate offset in key area to offset in block */
		*off = node->key + le16_to_cpu(entry->k);
	} else {
		/* These node types have variable length keys and data */
		struct apfs_kvloc *entry;

		entry = (struct apfs_kvloc *)raw->btn_data + index;
		len = le16_to_cpu(entry->k.len);
		/* Translate offset in key area to offset in block */
		*off = node->key + le16_to_cpu(entry->k.off);
	}

	if (*off + len > sb->s_blocksize) {
		printf("B-tree key is out-of-bounds\n");
		exit(1);
	}
	return len;
}

/**
 * node_locate_data - Locate the data of a node record
 * @node:	node to be searched
 * @index:	number of the entry to locate
 * @off:	on return will hold the offset in the block
 *
 * Returns the length of the data. The function checks that this length fits
 * within the block; callers must use the returned value to make sure they
 * never operate outside its bounds.
 */
static int node_locate_data(struct node *node, int index, int *off)
{
	struct super_block *sb = node->object.sb;
	struct apfs_btree_node_phys *raw;
	int len;

	if (index >= node->records) {
		printf("Requested index out-of-bounds\n");
		exit(1);
	}

	raw = node->raw;
	if (node_has_fixed_kv_size(node)) {
		/* These node types have fixed length keys and data */
		struct apfs_kvoff *entry;

		entry = (struct apfs_kvoff *)raw->btn_data + index;
		/* Node type decides length */
		len = node_is_leaf(node) ? 16 : 8;
		/*
		 * Data offsets are counted backwards from the end of the
		 * block, or from the beginning of the footer when it exists
		 */
		if (node_is_root(node)) /* has footer */
			*off = sb->s_blocksize - sizeof(struct apfs_btree_info)
					- le16_to_cpu(entry->v);
		else
			*off = sb->s_blocksize - le16_to_cpu(entry->v);
	} else {
		/* These node types have variable length keys and data */
		struct apfs_kvloc *entry;

		entry = (struct apfs_kvloc *)raw->btn_data + index;
		len = le16_to_cpu(entry->v.len);
		/*
		 * Data offsets are counted backwards from the end of the
		 * block, or from the beginning of the footer when it exists
		 */
		if (node_is_root(node)) /* has footer */
			*off = sb->s_blocksize - sizeof(struct apfs_btree_info)
					- le16_to_cpu(entry->v.off);
		else
			*off = sb->s_blocksize - le16_to_cpu(entry->v.off);
	}

	if (*off < 0 || *off + len > sb->s_blocksize) {
		printf("B-tree value is out-of-bounds\n");
		exit(1);
	}
	return len;
}

/**
 * parse_omap_subtree - Parse an omap subtree and check for corruption
 * @sb:		superblock structure
 * @root:	root node of the subtree
 * @last_key:	parent key, that must come before all the keys in this subtree;
 *		on return, this will hold the last key of this subtree, that
 *		must come before the next key of the parent node
 * @fd:		file descriptor for the device
 */
static void parse_omap_subtree(struct super_block *sb, struct node *root,
			       struct key *last_key, int fd)
{
	struct key curr_key;
	int i;

	for (i = 0; i < root->records; ++i) {
		struct node *child;
		void *raw = root->raw;
		int off, len;
		u64 child_id;

		len = node_locate_key(root, i, &off);
		read_omap_key(raw + off, len, &curr_key);

		if (keycmp(sb, last_key, &curr_key) > 0) {
			printf("Omap node keys are out of order.\n");
			exit(1);
		}
		if (i != 0 && node_is_leaf(root) &&
		    !keycmp(sb, last_key, &curr_key)) {
			printf("Omap leaf keys are repeated.\n");
			exit(1);
		}
		*last_key = curr_key;

		if (node_is_leaf(root))
			continue;

		len = node_locate_data(root, i, &off);
		if (len != 8) {
			printf("Wrong size of nonleaf record value.\n");
			exit(1);
		}
		child_id = le64_to_cpu(*(__le64 *)(raw + off));

		child = read_node(sb, child_id, fd);
		if (child->object.block_nr != child->object.oid) {
			printf("Wrong object id on omap node\n");
			exit(1);
		}

		parse_omap_subtree(sb, child, last_key, fd);
		free(child);
	}
}

/**
 * parse_omap_btree - Parse an object map and check for corruption
 * @sb:		superblock structure
 * @oid:	object id for the omap
 * @fd:		file descriptor for the device
 *
 * Returns a pointer to the root node of the omap.
 */
struct node *parse_omap_btree(struct super_block *sb, u64 oid, int fd)
{
	struct apfs_omap_phys *raw;
	struct node *root;
	struct key last_key = {0};

	raw = mmap(NULL, sb->s_blocksize, PROT_READ, MAP_PRIVATE,
		   fd, oid * sb->s_blocksize);
	if (raw == MAP_FAILED) {
		perror(NULL);
		exit(1);
	}

	/* Many checks are missing, of course */
	if (!obj_verify_csum(sb, &raw->om_o)) {
		printf("Bad checksum for object map\n");
		exit(1);
	}
	if (oid != le64_to_cpu(raw->om_o.o_oid)) {
		printf("Wrong object id on object map\n");
		exit(1);
	}

	root = read_node(sb, le64_to_cpu(raw->om_tree_oid), fd);
	parse_omap_subtree(sb, root, &last_key, fd);
	return root;
}
