/*
 *  apfsprogs/apfsck/stats.h
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _STATS_H
#define _STATS_H

#include "types.h"

/*
 * Structure that gathers filesystem stats as they are measured.
 */
struct stats {
	u64 s_omap_key_count;		/* Number of keys in container omap */
	struct vol_stats **vol_stats;	/* Pointer to array of volume stats */
};

/*
 * Structure that gathers volume stats as they are measured.
 */
struct vol_stats {
	u64 v_omap_key_count;	/* Number of keys in the volume object map */
	u64 cat_key_count;	/* Number of keys in the volume catalog */
	int cat_longest_key;	/* Length of longest key in catalog */
	int cat_longest_val;	/* Length of longest value in catalog */
};

#endif	/* _STATS_H */
