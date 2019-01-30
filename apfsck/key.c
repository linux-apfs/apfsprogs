/*
 *  apfsprogs/apfsck/key.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crc32c.h"
#include "types.h"
#include "key.h"
#include "unicode.h"

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

/**
 * cat_type - Read the record type of a catalog key
 * @key: the raw catalog key
 *
 * The record type is stored in the last byte of the cnid field; this function
 * returns that value.
 */
static inline int cat_type(struct apfs_key_header *key)
{
	return (le64_to_cpu(key->obj_id_and_type) & APFS_OBJ_TYPE_MASK)
			>> APFS_OBJ_TYPE_SHIFT;
}

/**
 * cat_cnid - Read the cnid value on a catalog key
 * @key: the raw catalog key
 *
 * The cnid value shares the its field with the record type. This function
 * masks that part away and returns the result.
 */
static inline u64 cat_cnid(struct apfs_key_header *key)
{
	return le64_to_cpu(key->obj_id_and_type) & APFS_OBJ_ID_MASK;
}

/**
 * filename_cmp - Normalize and compare two APFS filenames
 * @sb:			filesystem superblock
 * @name1, @name2:	names to compare
 *
 * returns   0 if @name1 and @name2 are equal
 *	   < 0 if @name1 comes before @name2 in the btree
 *	   > 0 if @name1 comes after @name2 in the btree
 */
static int filename_cmp(struct super_block *sb, const char *name1,
						const char *name2)
{
	struct unicursor cursor1, cursor2;
	bool case_fold = true; /* For now */

	init_unicursor(&cursor1, name1);
	init_unicursor(&cursor2, name2);

	while (1) {
		unicode_t uni1, uni2;

		uni1 = normalize_next(&cursor1, case_fold);
		uni2 = normalize_next(&cursor2, case_fold);

		if (uni1 != uni2)
			return uni1 < uni2 ? -1 : 1;
		if (!uni1)
			return 0;
	}
}

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

	if (k1->type == APFS_TYPE_XATTR) {
		/* xattr names seem to be always case sensitive */
		return strcmp(k1->name, k2->name);
	}

	/* The assumption here is not the same as in the module... */
	return filename_cmp(sb, k1->name, k2->name);
}

/**
 * dentry_hash - Find the key hash for a given filename
 * @name: filename to hash
 */
static u32 dentry_hash(const char *name)
{
	struct unicursor cursor;
	bool case_fold = true; /* For now */
	u32 hash = 0xFFFFFFFF;
	int namelen;

	init_unicursor(&cursor, name);

	while (1) {
		unicode_t utf32;

		utf32 = normalize_next(&cursor, case_fold);
		if (!utf32)
			break;

		hash = crc32c(hash, &utf32, sizeof(utf32));
	}

	/* APFS counts the NULL termination for the filename length */
	namelen = cursor.utf8curr - name;

	return ((hash & 0x3FFFFF) << 10) | (namelen & 0x3FF);
}

/**
 * read_dir_rec_key - Parse an on-disk dentry key and check its consistency
 * @raw:	pointer to the raw key
 * @size:	size of the raw key
 * @key:	key structure to store the result
 */
static void read_dir_rec_key(void *raw, int size, struct key *key)
{
	struct apfs_drec_hashed_key *raw_key;
	int namelen;

	if (size < sizeof(struct apfs_drec_hashed_key) + 1) {
		printf("Wrong size for directory record key.\n");
		exit(1);
	}
	if (*((char *)raw + size - 1) != 0) {
		printf("Filename lacks NULL-termination.\n");
		exit(1);
	}
	raw_key = raw;

	key->number = le32_to_cpu(raw_key->name_len_and_hash);
	key->name = (char *)raw_key->name;

	if (key->number != dentry_hash(key->name)) {
		printf("Corrupted dentry hash.\n");
		exit(1);
	}

	namelen = key->number & 0x3FF;
	if (strlen(key->name) + 1 != namelen) {
		/* APFS counts the NULL termination for the filename length */
		printf("Wrong name length in dentry key.\n");
		exit(1);
	}
	if (size != sizeof(struct apfs_drec_hashed_key) + namelen) {
		printf("Size of dentry key doesn't match the name length.\n");
		exit(1);
	}
}

/**
 * read_cat_key - Parse an on-disk catalog key
 * @raw:	pointer to the raw key
 * @size:	size of the raw key
 * @key:	key structure to store the result
 */
void read_cat_key(void *raw, int size, struct key *key)
{
	if (size < sizeof(struct apfs_key_header)) {
		printf("Key too small in catalog tree.\n");
		exit(1);
	}
	key->id = cat_cnid((struct apfs_key_header *)raw);
	key->type = cat_type((struct apfs_key_header *)raw);

	switch (key->type) {
	case APFS_TYPE_DIR_REC:
		read_dir_rec_key(raw, size, key);
		return;
	case APFS_TYPE_XATTR:
		if (size < sizeof(struct apfs_xattr_key) + 1) {
			printf("Wrong size for xattr record key.\n");
			exit(1);
		}
		if (*((char *)raw + size - 1) != 0) {
			printf("Xattr name lacks NULL-termination.\n");
			exit(1);
		}
		key->number = 0;
		key->name = (char *)((struct apfs_xattr_key *)raw)->name;
		return;
	case APFS_TYPE_FILE_EXTENT:
		if (size != sizeof(struct apfs_file_extent_key)) {
			printf("Wrong size of key for extent record.\n");
			exit(1);
		}
		key->number = le64_to_cpu(
			((struct apfs_file_extent_key *)raw)->logical_addr);
		key->name = NULL;
		return;
	default:
		key->number = 0;
		key->name = NULL;
		return;
	}
}
