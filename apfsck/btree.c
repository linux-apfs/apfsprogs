/*
 *  apfsprogs/apfsck/btree.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "apfsck.h"
#include "btree.h"
#include "key.h"
#include "object.h"
#include "super.h"
#include "types.h"

/**
 * node_is_valid - Check basic sanity of the node index
 * @node:	node to check
 *
 * Verifies that the node index fits in a single block, and that the number
 * of records fits in the index. Without this check a crafted filesystem could
 * pretend to have too many records, and calls to node_locate_key() and
 * node_locate_data() would read beyond the limits of the node.
 */
static bool node_is_valid(struct node *node)
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
 * @oid:	object id for the node
 * @btree:	tree structure, with the omap_root already set
 *
 * Returns a pointer to the resulting node structure.
 */
static struct node *read_node(u64 oid, struct btree *btree)
{
	struct apfs_btree_node_phys *raw;
	struct node *node;
	u64 bno;

	if (btree_is_omap(btree))
		bno = oid;
	else
		bno = omap_lookup_block(btree->omap_root, oid);

	raw = mmap(NULL, sb->s_blocksize, PROT_READ, MAP_PRIVATE,
		   fd, bno * sb->s_blocksize);
	if (raw == MAP_FAILED) {
		perror(NULL);
		exit(1);
	}

	node = malloc(sizeof(*node));
	if (!node) {
		perror(NULL);
		exit(1);
	}
	node->btree = btree;

	node->flags = le16_to_cpu(raw->btn_flags);
	node->records = le32_to_cpu(raw->btn_nkeys);
	node->key = sizeof(*raw) + le16_to_cpu(raw->btn_table_space.off)
				+ le16_to_cpu(raw->btn_table_space.len);
	node->free = node->key + le16_to_cpu(raw->btn_free_space.off);
	node->data = node->free + le16_to_cpu(raw->btn_free_space.len);

	if (oid != le64_to_cpu(raw->btn_o.o_oid))
		report("B-tree node", "wrong object id in block 0x%llx.",
		       (unsigned long long)bno);

	node->object.oid = oid;
	node->object.block_nr = bno;

	if (!obj_verify_csum(&raw->btn_o)) {
		report("B-tree node", "bad checksum in block 0x%llx.",
		       (unsigned long long)bno);
	}
	if (!node_is_valid(node)) {
		report("B-tree node", "block 0x%llx is not sane.",
		       (unsigned long long)bno);
	}

	node->raw = raw;
	return node;
}

/**
 * node_free - Free a node structure
 * @node: node to free
 *
 * This function works under the assumption that the node flags are not
 * corrupted, but we are not yet checking that (TODO).
 */
static void node_free(struct node *node)
{
	if (node_is_root(node))
		return;	/* The root nodes are needed by the sb until the end */
	munmap(node->raw, sb->s_blocksize);
	free(node);
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
	struct apfs_btree_node_phys *raw;
	int len;

	if (index >= node->records)
		report("B-tree node", "requested index out-of-bounds.");

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

	if (*off + len > sb->s_blocksize)
		report("B-tree", "key is out-of-bounds.");
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
	struct apfs_btree_node_phys *raw;
	int len;

	if (index >= node->records)
		report("B-tree", "requested index out-of-bounds.");

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

	if (*off < 0 || *off + len > sb->s_blocksize)
		report("B-tree", "value is out-of-bounds.");
	return len;
}

/**
 * parse_subtree - Parse a subtree and check for corruption
 * @root:	root node of the subtree
 * @last_key:	parent key, that must come before all the keys in this subtree;
 *		on return, this will hold the last key of this subtree, that
 *		must come before the next key of the parent node
 */
static void parse_subtree(struct node *root, struct key *last_key)
{
	struct btree *btree = root->btree;
	struct key curr_key;
	int i;

	if (node_is_leaf(root))
		btree->key_count += root->records;
	++btree->node_count;

	if (btree_is_omap(btree) && !node_has_fixed_kv_size(root))
		report("Object map", "key size should be fixed.");
	if (!btree_is_omap(btree) && node_has_fixed_kv_size(root))
		report("Catalog", "key size should not be fixed.");

	for (i = 0; i < root->records; ++i) {
		struct node *child;
		void *raw = root->raw;
		int off, len;
		u64 child_id;

		len = node_locate_key(root, i, &off);
		if (len > btree->longest_key)
			btree->longest_key = len;

		if (btree_is_omap(btree))
			read_omap_key(raw + off, len, &curr_key);
		else
			read_cat_key(raw + off, len, &curr_key);

		if (keycmp(last_key, &curr_key) > 0)
			report("B-tree", "keys are out of order.");

		if (i != 0 && node_is_leaf(root) &&
		    !keycmp(last_key, &curr_key))
			report("B-tree", "leaf keys are repeated.");
		*last_key = curr_key;

		len = node_locate_data(root, i, &off);

		if (node_is_leaf(root)) {
			if (len > btree->longest_val)
				btree->longest_val = len;
			continue;
		}

		if (len != 8)
			report("B-tree", "wrong size of nonleaf record value.");
		child_id = le64_to_cpu(*(__le64 *)(raw + off));
		child = read_node(child_id, btree);

		parse_subtree(child, last_key);
		free(child);
	}
}

/**
 * check_btree_footer - Check that btree_info matches the collected stats
 * @btree: b-tree to check
 */
static void check_btree_footer(struct btree *btree)
{
	struct node *root = btree->root;
	struct apfs_btree_info *info;
	char *ctx;

	ctx = btree_is_omap(btree) ? "Object map" : "Catalog";

	/* Flags are not part of the footer, but this check fits best here */
	if (!node_is_root(root))
		report(ctx, "wrong flag in root node.");

	info = (void *)root->raw + sb->s_blocksize - sizeof(*info);
	if (le32_to_cpu(info->bt_fixed.bt_node_size) != sb->s_blocksize)
		report(ctx, "nodes with more than a block are not supported.");

	if (le64_to_cpu(info->bt_key_count) != btree->key_count)
		report(ctx, "wrong key count in info footer.");
	if (le64_to_cpu(info->bt_node_count) != btree->node_count)
		report(ctx, "wrong node count in info footer.");

	if (btree_is_omap(btree)) {
		if (le32_to_cpu(info->bt_fixed.bt_key_size) !=
					sizeof(struct apfs_omap_key))
			report(ctx, "wrong key size in info footer.");

		if (le32_to_cpu(info->bt_fixed.bt_val_size) !=
					sizeof(struct apfs_omap_val))
			report(ctx, "wrong value size in info footer.");

		if (le32_to_cpu(info->bt_longest_key) !=
					sizeof(struct apfs_omap_key))
			report(ctx, "wrong maximum key size in info footer.");

		if (le32_to_cpu(info->bt_longest_val) !=
					sizeof(struct apfs_omap_val))
			report(ctx, "wrong maximum value size in info footer.");
	} else {
		/* This is a catalog tree */
		if (le32_to_cpu(info->bt_fixed.bt_key_size) != 0)
			report(ctx, "key size should not be set.");
		if (le32_to_cpu(info->bt_fixed.bt_val_size) != 0)
			report(ctx, "value size should not be set.");
		if (le32_to_cpu(info->bt_longest_key) < btree->longest_key)
			report(ctx, "wrong maximum key size in info footer.");
		if (le32_to_cpu(info->bt_longest_val) < btree->longest_val)
			report(ctx, "wrong maximum value size in info footer.");
	}
}

/**
 * parse_cat_btree - Parse a catalog tree and check for corruption
 * @oid:	object id for the catalog root
 * @omap_root:	root of the object map for the b-tree
 *
 * Returns a pointer to the btree struct for the catalog.
 */
struct btree *parse_cat_btree(u64 oid, struct node *omap_root)
{
	struct btree *cat;
	struct key last_key = {0};

	cat = calloc(1, sizeof(*cat));
	if (!cat) {
		perror(NULL);
		exit(1);
	}

	cat->omap_root = omap_root;
	cat->root = read_node(oid, cat);

	parse_subtree(cat->root, &last_key);

	check_btree_footer(cat);
	return cat;
}

/**
 * parse_omap_btree - Parse an object map and check for corruption
 * @oid:	object id for the omap
 *
 * Returns a pointer to the btree struct for the omap.
 */
struct btree *parse_omap_btree(u64 oid)
{
	struct apfs_omap_phys *raw;
	struct btree *omap;
	struct key last_key = {0};

	raw = mmap(NULL, sb->s_blocksize, PROT_READ, MAP_PRIVATE,
		   fd, oid * sb->s_blocksize);
	if (raw == MAP_FAILED) {
		perror(NULL);
		exit(1);
	}

	/* Many checks are missing, of course */
	if (!obj_verify_csum(&raw->om_o))
		report("Object map", "bad checksum.");
	if (oid != le64_to_cpu(raw->om_o.o_oid))
		report("Object map", "wrong object id.");

	omap = calloc(1, sizeof(*omap));
	if (!omap) {
		perror(NULL);
		exit(1);
	}
	omap->omap_root = NULL; /* The omap doesn't have an omap of its own */
	omap->root = read_node(le64_to_cpu(raw->om_tree_oid), omap);

	parse_subtree(omap->root, &last_key);

	check_btree_footer(omap);
	return omap;
}

/**
 * child_from_query - Read the child id found by a successful nonleaf query
 * @query:	the query that found the record
 *
 * Returns the child id in the nonleaf node record.
 */
static u64 child_from_query(struct query *query)
{
	void *raw = query->node->raw;

	/* This check is actually redundant, at least for now */
	if (query->len != 8) /* The data on a nonleaf node is the child id */
		report("B-tree", "wrong size of nonleaf record value.");

	return le64_to_cpu(*(__le64 *)(raw + query->off));
}

/**
 * bno_from_query - Read the block number found by a successful omap query
 * @query:	the query that found the record
 *
 * Returns the block number in the omap record after performing a basic
 * sanity check.
 */
static u64 bno_from_query(struct query *query)
{
	struct apfs_omap_val *omap_val;
	void *raw = query->node->raw;

	if (query->len != sizeof(*omap_val))
		report("Object map record", "wrong size of value.");

	omap_val = (struct apfs_omap_val *)(raw + query->off);
	return le64_to_cpu(omap_val->ov_paddr);
}

/**
 * omap_lookup_block - Find the block number of a b-tree node from its id
 * @tbl:	Root of the object map to be searched
 * @id:		id of the node
 *
 * Returns the block number.
 */
u64 omap_lookup_block(struct node *tbl, u64 id)
{
	struct query *query;
	struct key key;
	u64 block;

	query = alloc_query(tbl, NULL /* parent */);

	init_omap_key(id, &key);
	query->key = &key;
	query->flags |= QUERY_OMAP | QUERY_EXACT;

	if (btree_query(&query)) { /* Omap queries shouldn't fail */
		report("Object map", "record missing for id 0x%llx.",
		       (unsigned long long)id);
	}

	block = bno_from_query(query);
	free_query(query);
	return block;
}

/**
 * alloc_query - Allocates a query structure
 * @node:	node to be searched
 * @parent:	query for the parent node
 *
 * Callers other than btree_query() should set @parent to NULL, and @node
 * to the root of the b-tree. They should also initialize most of the query
 * fields themselves; when @parent is not NULL the query will inherit them.
 *
 * Returns the allocated query.
 */
struct query *alloc_query(struct node *node, struct query *parent)
{
	struct query *query;

	query = malloc(sizeof(*query));
	if (!query) {
		perror(NULL);
		exit(1);
	}

	query->node = node;
	query->key = parent ? parent->key : NULL;
	query->flags = parent ? parent->flags & ~(QUERY_DONE | QUERY_NEXT) : 0;
	query->parent = parent;
	/* Start the search with the last record and go backwards */
	query->index = node->records;
	query->depth = parent ? parent->depth + 1 : 0;

	return query;
}

/**
 * free_query - Free a query structure
 * @query:	query to free
 *
 * Also frees the ancestor queries, if they are kept.
 */
void free_query(struct query *query)
{
	while (query) {
		struct query *parent = query->parent;

		node_free(query->node);
		free(query);
		query = parent;
	}
}

/**
 * key_from_query - Read the current key from a query structure
 * @query:	the query, with @query->key_off and @query->key_len already set
 * @key:	return parameter for the key
 *
 * Reads the key into @key after some basic sanity checks.
 */
static void key_from_query(struct query *query, struct key *key)
{
	void *raw = query->node->raw;
	void *raw_key = (void *)(raw + query->key_off);

	switch (query->flags & QUERY_TREE_MASK) {
	case QUERY_CAT:
		read_cat_key(raw_key, query->key_len, key);
		break;
	case QUERY_OMAP:
		read_omap_key(raw_key, query->key_len, key);
		break;
	default:
		report(NULL, "Bug!");
	}

	if (query->flags & QUERY_MULTIPLE) {
		/* A multiple query must ignore these fields */
		key->number = 0;
		key->name = NULL;
	}
}

/**
 * node_next - Find the next matching record in the current node
 * @query:	multiple query in execution
 *
 * Returns 0 on success, -EAGAIN if the next record is in another node, and
 * -ENODATA if no more matching records exist.
 */
static int node_next(struct query *query)
{
	struct node *node = query->node;
	struct key curr_key;
	int cmp;
	u64 bno = node->object.block_nr;

	if (query->flags & QUERY_DONE)
		/* Nothing left to search; the query failed */
		return -ENODATA;

	if (!query->index) /* The next record may be in another node */
		return -EAGAIN;
	--query->index;

	query->key_len = node_locate_key(node, query->index, &query->key_off);
	key_from_query(query, &curr_key);

	cmp = keycmp(&curr_key, query->key);

	if (cmp > 0)
		report("B-tree", "records are out of order.");

	if (cmp != 0 && node_is_leaf(node) && query->flags & QUERY_EXACT)
		return -ENODATA;

	query->len = node_locate_data(node, query->index, &query->off);
	if (query->len == 0) {
		report("B-tree", "corrupted record value in node 0x%llx.",
		       (unsigned long long)bno);
	}

	if (cmp != 0) {
		/*
		 * This is the last entry that can be relevant in this node.
		 * Keep searching the children, but don't return to this level.
		 */
		query->flags |= QUERY_DONE;
	}

	return 0;
}

/**
 * node_query - Execute a query on a single node
 * @query:	the query to execute
 *
 * The search will start at index @query->index, looking for the key that comes
 * right before @query->key, according to the order given by keycmp().
 *
 * The @query->index will be updated to the last index checked. This is
 * important when searching for multiple entries, since the query may need
 * to remember where it was on this level. If we are done with this node, the
 * query will be flagged as QUERY_DONE, and the search will end in failure
 * as soon as we return to this level. The function may also return -EAGAIN,
 * to signal that the search should go on in a different branch.
 *
 * On success returns 0; the offset of the data within the block will be saved
 * in @query->off, and its length in @query->len. The function checks that this
 * length fits within the block; callers must use the returned value to make
 * sure they never operate outside its bounds.
 *
 * -ENODATA will be returned if no appropriate entry was found.
 */
static int node_query(struct query *query)
{
	struct node *node = query->node;
	int left, right;
	int cmp;
	u64 bno = node->object.block_nr;

	if (query->flags & QUERY_NEXT)
		return node_next(query);

	/* Search by bisection */
	cmp = 1;
	left = 0;
	do {
		struct key curr_key;
		if (cmp > 0) {
			right = query->index - 1;
			if (right < left)
				return -ENODATA;
			query->index = (left + right) / 2;
		} else {
			left = query->index;
			query->index = DIV_ROUND_UP(left + right, 2);
		}

		query->key_len = node_locate_key(node, query->index,
						 &query->key_off);
		key_from_query(query, &curr_key);

		cmp = keycmp(&curr_key, query->key);
		if (cmp == 0 && !(query->flags & QUERY_MULTIPLE))
			break;
	} while (left != right);

	if (cmp > 0)
		return -ENODATA;

	if (cmp != 0 && node_is_leaf(query->node) && query->flags & QUERY_EXACT)
		return -ENODATA;

	if (query->flags & QUERY_MULTIPLE) {
		if (cmp != 0) /* Last relevant entry in level */
			query->flags |= QUERY_DONE;
		query->flags |= QUERY_NEXT;
	}

	query->len = node_locate_data(node, query->index, &query->off);
	if (query->len == 0) {
		report("B-tree", "corrupted record value in node 0x%llx.",
		       (unsigned long long)bno);
	}
	return 0;
}

/**
 * btree_query - Execute a query on a b-tree
 * @query:	the query to execute
 *
 * Searches the b-tree starting at @query->index in @query->node, looking for
 * the record corresponding to @query->key.
 *
 * Returns 0 in case of success and sets the @query->len, @query->off and
 * @query->index fields to the results of the query. @query->node will now
 * point to the leaf node holding the record.
 *
 * In case of failure returns -ENODATA.
 */
int btree_query(struct query **query)
{
	struct node *node = (*query)->node;
	struct query *parent;
	struct btree *btree = node->btree;
	u64 child_id;
	int err;

next_node:
	if ((*query)->depth >= 12) {
		/* This is the maximum depth allowed by the module */
		report("B-tree", "is too deep.");
	}

	err = node_query(*query);
	if (err == -EAGAIN) {
		if (!(*query)->parent) /* We are at the root of the tree */
			return -ENODATA;

		/* Move back up one level and continue the query */
		parent = (*query)->parent;
		(*query)->parent = NULL; /* Don't free the parent */
		free_query(*query);
		*query = parent;
		goto next_node;
	}
	if (err)
		return err;
	if (node_is_leaf((*query)->node)) /* All done */
		return 0;

	/* Now go a level deeper and search the child */
	child_id = child_from_query(*query);
	node = read_node(child_id, btree);

	if ((*query)->flags & QUERY_MULTIPLE) {
		/*
		 * We are looking for multiple entries, so we must remember
		 * the parent node and index to continue the search later.
		 */
		*query = alloc_query(node, *query);
	} else {
		/* Reuse the same query structure to search the child */
		node_free((*query)->node);
		(*query)->node = node;
		(*query)->index = node->records;
		(*query)->depth++;
	}
	goto next_node;
}
