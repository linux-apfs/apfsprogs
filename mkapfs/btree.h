/*
 *  apfsprogs/mkapfs/btree.h
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _BTREE_H
#define _BTREE_H

#include <apfs/types.h>

extern void make_omap_btree(u64 bno, bool is_vol);

#endif	/* _BTREE_H */
