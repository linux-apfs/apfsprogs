/*
 *  apfsprogs/apfsck/stats.h
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _STATS_H
#define _STATS_H

/*
 * Structure that gathers filesystem stats as they are measured.
 */
struct stats {
	struct vol_stats **vol_stats;	/* Pointer to array of volume stats */
};

/*
 * Structure that gathers volume stats as they are measured.
 */
struct vol_stats {
	int cat_longest_key;	/* Length of longest key in catalog */
	int cat_longest_val;	/* Length of longest value in catalog */
};

#endif	/* _STATS_H */
