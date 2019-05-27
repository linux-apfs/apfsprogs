/*
 *  apfsprogs/mkapfs/mkapfs.h
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _MKAPFS_H
#define _MKAPFS_H

/* Filesystem parameters */
struct parameters {
	unsigned long	blocksize;	/* Block size */
	u64		block_count;	/* Number of blocks in the container */
	char		*uuid;		/* UUID in standard format */
};

/* Declarations for global variables */
extern int fd;				/* File descriptor for the device */

extern __attribute__((noreturn)) void system_error(void);

#endif	/* _MKAPFS_H */
