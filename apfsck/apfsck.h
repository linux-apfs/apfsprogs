/*
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _APFSCK_H
#define _APFSCK_H

#include <stdbool.h>

/* Declarations for global variables */
extern unsigned int options;		/* Command line options */
extern struct super_block *sb;		/* Filesystem superblock */
extern struct volume_superblock *vsb;	/* Volume superblock */
extern int fd_main;			/* File descriptor for the main device */
extern int fd_tier2;			/* File descriptor for the tier 2 device, if any */
extern bool ongoing_query;		/* Are we currently running a query? */

/* Option flags */
#define	OPT_REPORT_CRASH	1 /* Report on-disk signs of a past crash */
#define OPT_REPORT_UNKNOWN	2 /* Report unknown or unsupported features */
#define OPT_REPORT_WEIRD	4 /* Report issues that may not be corruption */

extern __attribute__((noreturn, format(printf, 2, 3)))
		void report(const char *context, const char *message, ...);
extern void report_crash(const char *context);
extern void report_unknown(const char *feature);
extern void report_weird(const char *context);
extern __attribute__((noreturn)) void system_error(void);

#include <sys/mman.h>
#include <unistd.h>
#include <apfs/raw.h>
#include <apfs/types.h>

/* Forwards the mmap() call to the proper device of a fusion drive */
static inline void *apfs_mmap(void *addr, size_t length, int prot, int flags, u64 offset)
{
	/*
	 * TODO: check that the block is in the correct device for all callers
	 * where that is known.
	 */
	if (offset >= APFS_FUSION_TIER2_DEVICE_BYTE_ADDR) {
		if (fd_tier2 == -1)
			report(NULL, "Address in missing tier 2 device.");
		offset -= APFS_FUSION_TIER2_DEVICE_BYTE_ADDR;
		return mmap(addr, length, prot, flags, fd_tier2, (off_t)offset);
	}
	return mmap(addr, length, prot, flags, fd_main, (off_t)offset);
}

/* Forwards the pread() call to the proper device of a fusion drive */
static inline ssize_t apfs_pread(void *buf, size_t count, u64 offset)
{
	/*
	 * TODO: check that the block is in the correct device for all callers
	 * where that is known.
	 */
	if (offset >= APFS_FUSION_TIER2_DEVICE_BYTE_ADDR) {
		if (fd_tier2 == -1)
			report(NULL, "Address in missing tier 2 device.");
		offset -= APFS_FUSION_TIER2_DEVICE_BYTE_ADDR;
		return pread(fd_tier2, buf, count, (off_t)offset);
	}
	return pread(fd_main, buf, count, (off_t)offset);
}

#endif	/* _APFSCK_H */
