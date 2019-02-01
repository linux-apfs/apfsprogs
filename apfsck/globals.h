/*
 *  apfsprogs/apfsck/globals.h
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 *
 * Declarations for global variables.
 */

#ifndef _GLOBALS_H
#define _GLOBALS_H

struct super_block;
struct volume_superblock;

extern struct super_block *sb;		/* Filesystem superblock */
extern struct volume_superblock *vsb;	/* Volume superblock */
extern int fd;				/* File descriptor for the device */

#endif	/* _GLOBALS_H */
