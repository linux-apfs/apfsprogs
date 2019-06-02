/*
 *  apfsprogs/mkapfs/spaceman.c
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <apfs/raw.h>
#include <apfs/types.h>
#include "mkapfs.h"
#include "object.h"
#include "spaceman.h"

/**
 * make_spaceman - Make the space manager for the container
 * @bno: block number to use
 * @oid: object id
 */
void make_spaceman(u64 bno, u64 oid)
{
	struct apfs_spaceman_phys *sm = get_zeroed_block(bno);

	set_object_header(&sm->sm_o, oid,
			  APFS_OBJ_EPHEMERAL | APFS_OBJECT_TYPE_SPACEMAN,
			  APFS_OBJECT_TYPE_INVALID);
	munmap(sm, param->blocksize);
}
