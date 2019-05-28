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

/* Hardcoded transaction ids */
#define MKFS_XID	1

/* Hardcoded object ids */
#define	SPACEMAN_OID	APFS_OID_RESERVED_COUNT
#define REAPER_OID	(SPACEMAN_OID + 1)

/* Hardcoded block numbers */
#define MAIN_OMAP_BNO	20000

/* Declarations for global variables */
extern struct parameters *param;	/* Filesystem parameters */
extern int fd;				/* File descriptor for the device */

extern __attribute__((noreturn)) void system_error(void);

#endif	/* _MKAPFS_H */
