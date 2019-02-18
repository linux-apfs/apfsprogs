/*
 *  apfsprogs/apfsck/inode.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdlib.h>
#include <stdio.h>
#include "apfsck.h"
#include "inode.h"
#include "key.h"
#include "super.h"
#include "types.h"

/**
 * alloc_inode_table - Allocates and returns an empty inode hash table
 */
struct inode **alloc_inode_table(void)
{
	struct inode **table;

	table = calloc(INODE_TABLE_BUCKETS, sizeof(*table));
	if (!table) {
		perror(NULL);
		exit(1);
	}
	return table;
}

/**
 * free_inode_table - Free the inode hash table and all its inodes
 * @table: table to free
 */
void free_inode_table(struct inode **table)
{
	struct inode *current;
	struct inode *next;
	int i;

	for (i = 0; i < INODE_TABLE_BUCKETS; ++i) {
		current = table[i];
		while (current) {
			next = current->i_next;
			free(current);
			current = next;
		}
	}
	free(table);
}

/**
 * get_inode - Find or create an inode structure in a hash table
 * @ino:	inode number
 * @table:	the hash table
 *
 * Returns the inode structure, after creating it if necessary.
 */
static struct inode *get_inode(u64 ino, struct inode **table)
{
	int index = ino % INODE_TABLE_BUCKETS; /* Trivial hash function */
	struct inode **entry_p = table + index;
	struct inode *entry = *entry_p;
	struct inode *new;

	/* Inodes are ordered by ino in each linked list */
	while (entry) {
		if (ino == entry->i_ino)
			return entry;
		if (ino < entry->i_ino)
			break;

		entry_p = &entry->i_next;
		entry = *entry_p;
	}

	new = malloc(sizeof(*new));
	if (!new) {
		perror(NULL);
		exit(1);
	}

	new->i_seen = false;
	new->i_ino = ino;
	new->i_next = entry;
	*entry_p = new;
	return new;
}

/**
 * parse_inode_record - Parse an inode record value and check for corruption
 * @key:	pointer to the raw key
 * @val:	pointer to the raw value
 * @len:	length of the raw value
 *
 * Internal consistency of @key must be checked before calling this function.
 */
void parse_inode_record(struct apfs_inode_key *key,
			struct apfs_inode_val *val, int len)
{
	struct inode *inode;

	if (len < sizeof(*val))
		report("Inode record", "value is too small.");

	inode = get_inode(cat_cnid(&key->hdr), vsb->v_inode_table);
	if (inode->i_seen)
		report("Catalog", "inode numbers are repeated.");
	inode->i_seen = true;
}
