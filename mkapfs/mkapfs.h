/*
 *  apfsprogs/mkapfs/mkapfs.h
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _MKAPFS_H
#define _MKAPFS_H

#include <string.h>
#include <sys/mman.h>

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
#define FIRST_VOL_OID	(REAPER_OID + 1)

/* Hardcoded block numbers */
#define MAIN_OMAP_BNO		20000
#define MAIN_OMAP_ROOT_BNO	20001
#define FIRST_VOL_BNO		20002

/* Declarations for global variables */
extern struct parameters *param;	/* Filesystem parameters */
extern int fd;				/* File descriptor for the device */

extern __attribute__((noreturn)) void system_error(void);

/**
 * get_zeroed_block - Map and zero a filesystem block
 * @bno: block number
 *
 * Returns a pointer to the mapped block; the caller must unmap it after use.
 */
static inline void *get_zeroed_block(u64 bno)
{
	void *block;

	block = mmap(NULL, param->blocksize, PROT_READ | PROT_WRITE,
		     MAP_SHARED, fd, bno * param->blocksize);
	if (block == MAP_FAILED)
		system_error();
	memset(block, 0, param->blocksize);
	return block;
}

#endif	/* _MKAPFS_H */
