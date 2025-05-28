/*
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _MKAPFS_H
#define _MKAPFS_H

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <apfs/raw.h>

/* Filesystem parameters */
struct parameters {
	unsigned long	blocksize;	/* Block size */
	u64		block_count;	/* Number of blocks in the container */
	u64		main_blkcnt;	/* Number of blocks in the main device */
	u64		tier2_blkcnt;	/* Number of blocks in the tier 2 device */
	char		*label;		/* Volume label */
	char		*main_uuid;	/* Container UUID in standard format */
	char		*vol_uuid;	/* Volume UUID in standard format */
	char		*fusion_uuid;	/* Fusion drive UUID in standard format */
	bool		case_sensitive;	/* Is the filesystem case-sensitive? */
	bool		norm_sensitive;	/* Is it normalization-sensitive? */
};

/* Declarations for global variables */
extern struct parameters *param;	/* Filesystem parameters */
extern int fd_main;			/* File descriptor for the main device */
extern int fd_tier2;			/* File descriptor for the tier 2 device, if any */

/* Hardcoded transaction ids */
#define MKFS_XID	1

/* Hardcoded object ids */
#define	SPACEMAN_OID		APFS_OID_RESERVED_COUNT
#define REAPER_OID		(SPACEMAN_OID + 1)
#define FIRST_VOL_OID		(REAPER_OID + 1)
#define FIRST_VOL_CAT_ROOT_OID	(FIRST_VOL_OID + 1)
#define	IP_FREE_QUEUE_OID	(FIRST_VOL_CAT_ROOT_OID + 1)
#define MAIN_FREE_QUEUE_OID	(IP_FREE_QUEUE_OID + 1)
#define TIER2_FREE_QUEUE_OID	(MAIN_FREE_QUEUE_OID + 1)
#define FUSION_WBC_OID		(TIER2_FREE_QUEUE_OID + 1)

/**
 * cpoint_desc_blocks - Calculate the number of checkpoint descriptor blocks
 *
 * TODO: what if the tier 2 device is much bigger than the main one?
 */
static inline u32 cpoint_desc_blocks(void)
{
	/*
	 * These numbers are a rough approximation of what I learned from
	 * testing newfs_apfs. I don't think the exact block counts matter.
	 */
	if (param->block_count < 512 * 1024 / 4) /* Up to 512M */
		return 8;
	if (param->block_count < 1024 * 1024 / 4) /* Up to 1G */
		return 12;
	if (param->block_count < 50 * 1024 * 1024 / 4) { /* Up to 50G */
		u32 off_512M = (param->block_count - 1024 * 1024 / 4) / (512 * 1024 / 4);
		return 20 + 60 * off_512M / 23;
	}

	/*
	 * From here on, newfs_apfs increases the number very slowly. I don't
	 * think it matters...
	 */
	return 280;
}

/**
 * cpoint_data_blocks - Calculate the number of checkpoint data blocks
 *
 * TODO: what if the tier 2 device is much bigger than the main one?
 */
static inline u32 cpoint_data_blocks(void)
{
	/* I got these numbers from testing newfs_apfs */
	if (param->block_count < 4545)
		return 52;
	if (param->block_count < 13633)
		return 124;
	if (param->block_count < 36353)
		return 160 + 36 * ((param->block_count - 13633) / 4544);
	if (param->block_count < 131777)
		return 308 + 4 * ((param->block_count - 36353) / 4544);
	if (param->block_count < 262144)
		return 648 + 4 * ((param->block_count - 131777) / 4544);
	if (param->block_count == 262144) /* 1G is a special case, odd */
		return 992;

	/*
	 * At this point I got bored and the sizes stop matching exactly. The
	 * official fsck doesn't care. TODO?
	 */
	if (param->block_count < 1048576) { /* Up to 4G */
		u32 off_512M = (param->block_count - 262144) / 131072;
		return 1248 + 488 * off_512M + 4 * (((long long)param->block_count - (261280 + off_512M * 131776)) / 2272);
	}
	if (param->block_count < 4063232) { /* Up to 10G */
		u32 off_512M = (param->block_count - 1048576) / 131072;
		return 4112 + 256 * off_512M;
	}
	if (param->block_count < 13107200) { /* Up to 50G */
		u32 off_512M = (param->block_count - 4063232) / 131072;
		return 10000 + 256 * off_512M;
	}

	/*
	 * From here on, newfs_apfs increases the number very slowly. I don't
	 * think it matters...
	 */
	return 27672;
}

/* Description of the checkpoint areas */
#define CPOINT_DESC_BASE	(APFS_NX_BLOCK_NUM + 1)
#define CPOINT_DESC_BLOCKS	cpoint_desc_blocks()
#define CPOINT_DATA_BASE	(CPOINT_DESC_BASE + CPOINT_DESC_BLOCKS)
#define CPOINT_DATA_BLOCKS	cpoint_data_blocks()
#define CPOINT_END		(CPOINT_DATA_BASE + CPOINT_DATA_BLOCKS)

/*
 * Some block numbers for ephemeral objects will need to be calculated on
 * runtime, so that they can all be kept consecutive and in the right order.
 */
extern struct ephemeral_info {
	u64 reaper_bno;
	u64 spaceman_bno;
	u32 spaceman_sz;
	u32 spaceman_blkcnt;
	u64 ip_free_queue_bno;
	u64 main_free_queue_bno;
	u64 tier2_free_queue_bno;
	u64 fusion_wbc_bno;
	u32 total_blkcnt;
} eph_info;

/* Hardcoded block numbers calculated from the checkpoint areas */
#define CPOINT_MAP_BNO			CPOINT_DESC_BASE
#define CPOINT_SB_BNO			(CPOINT_DESC_BASE + 1)
#define MAIN_OMAP_BNO			CPOINT_END
#define MAIN_OMAP_ROOT_BNO		(CPOINT_END + 1)
#define FIRST_VOL_BNO			(CPOINT_END + 2)
#define FIRST_VOL_OMAP_BNO		(CPOINT_END + 3)
#define FIRST_VOL_OMAP_ROOT_BNO		(CPOINT_END + 4)
#define FIRST_VOL_CAT_ROOT_BNO		(CPOINT_END + 5)
#define FIRST_VOL_EXTREF_ROOT_BNO	(CPOINT_END + 6)
#define FIRST_VOL_SNAP_ROOT_BNO		(CPOINT_END + 7)
#define FUSION_MT_BNO			(CPOINT_END + 8)
/* Just a single block for now (TODO) */
#define FUSION_WBC_FIRST_BNO		(CPOINT_END + 9)
/* First ip bitmap comes last because the size is not hardcoded */
#define IP_BMAP_BASE			(CPOINT_END + 10)

extern __attribute__((noreturn)) void system_error(void);
extern __attribute__((noreturn)) void fatal(const char *message);

/* Forwards the pwrite() call to the proper device of a fusion drive */
static inline void apfs_writeall(void *buf, u64 blkcnt, u64 bno)
{
	int proper_fd;
	off_t offset;
	size_t count, copied;
	ssize_t ret;

	offset = bno * param->blocksize;
	count = blkcnt * param->blocksize;

	proper_fd = fd_main;
	if (offset >= APFS_FUSION_TIER2_DEVICE_BYTE_ADDR) {
		if (fd_tier2 == -1)
			fatal("allocation attempted in missing tier 2 device.");
		offset -= APFS_FUSION_TIER2_DEVICE_BYTE_ADDR;
		proper_fd = fd_tier2;
	}

	copied = 0;
	while (count > 0) {
		ret = pwrite(proper_fd, buf + copied, count, offset + copied);
		if (ret < 0)
			system_error();
		count -= ret;
		copied += ret;
	}

	free(buf);
}

/**
 * get_zeroed_blocks - Return a number of zeroed blocks
 * @bno:	first block number
 * @count:	number of blocks
 */
static inline void *get_zeroed_blocks(u64 count)
{
	void *blocks;

	blocks = calloc(count, param->blocksize);
	if (!blocks)
		system_error();
	return blocks;
}

/**
 * get_zeroed_block - Return a zeroed block
 * @bno: block number
 */
static inline void *get_zeroed_block(void)
{
	return get_zeroed_blocks(1);
}

/**
 * get_timestamp - Get the current time in nanoseconds
 *
 * Calls clock_gettime(), so may not work with old versions of glibc.
 */
static inline u64 get_timestamp(void)
{
	struct timespec time;

	if (clock_gettime(CLOCK_REALTIME, &time))
		system_error();

	return (u64)time.tv_sec * NSEC_PER_SEC + time.tv_nsec;
}

#endif	/* _MKAPFS_H */
