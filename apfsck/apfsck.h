/*
 *  apfsprogs/apfsck/apfsck.h
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _APFSCK_H
#define _APFSCK_H

/* Declarations for global variables */
extern struct super_block *sb;		/* Filesystem superblock */
extern struct volume_superblock *vsb;	/* Volume superblock */
extern int fd;				/* File descriptor for the device */

extern __attribute__((noreturn, format(printf, 2, 3)))
		void report(const char *context, const char *message, ...);

#endif	/* _APFSCK_H */
