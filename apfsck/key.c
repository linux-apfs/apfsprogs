/*
 *  apfsprogs/apfsck/key.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include "types.h"
#include "key.h"

/**
 * keycmp - Compare two keys
 * @sb:		filesystem superblock
 * @k1, @k2:	keys to compare
 *
 * returns   0 if @k1 and @k2 are equal
 *	   < 0 if @k1 comes before @k2 in the btree
 *	   > 0 if @k1 comes after @k2 in the btree
 */
int keycmp(struct super_block *sb, struct key *k1, struct key *k2)
{
	if (k1->id != k2->id)
		return k1->id < k2->id ? -1 : 1;
	if (k1->type != k2->type)
		return k1->type < k2->type ? -1 : 1;
	if (k1->number != k2->number)
		return k1->number < k2->number ? -1 : 1;
	if (!k1->name) /* Keys of this type have no name */
		return 0;

	return 0; /* No need to compare names yet */
}

/**
 * read_omap_key - Parse an on-disk object map key
 * @raw:	pointer to the raw key
 * @size:	size of the raw key
 * @key:	key structure to store the result
 */
void read_omap_key(void *raw, int size, struct key *key)
{
	if (size != sizeof(struct apfs_omap_key)) {
		printf("Wrong size of key in object map.\n");
		exit(1);
	}

	key->id = le64_to_cpu(((struct apfs_omap_key *)raw)->ok_oid);
	key->type = 0;
	key->number = 0;
	key->name = NULL;
}
