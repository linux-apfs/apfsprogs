/*
 *  apfsprogs/mkapfs/btree.c
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <apfs/raw.h>
#include "btree.h"
#include "mkapfs.h"
#include "object.h"

/**
 * make_omap_btree - Make an object map
 * @bno:	block number to use
 * @is_vol:	is this the object map for a volume?
 */
void make_omap_btree(u64 bno, bool is_vol)
{
	struct apfs_omap_phys *omap;

	omap = mmap(NULL, param->blocksize, PROT_READ | PROT_WRITE,
		    MAP_SHARED, fd, bno * param->blocksize);
	if (omap == MAP_FAILED)
		system_error();
	memset(omap, 0, param->blocksize);

	if (!is_vol)
		omap->om_flags = cpu_to_le32(APFS_OMAP_MANUALLY_MANAGED);
	omap->om_tree_type = cpu_to_le32(APFS_OBJECT_TYPE_BTREE |
					 APFS_OBJ_PHYSICAL);
	omap->om_snapshot_tree_type = cpu_to_le32(APFS_OBJECT_TYPE_BTREE |
						  APFS_OBJ_PHYSICAL);
	omap->om_tree_oid = cpu_to_le64(MAIN_OMAP_ROOT_BNO);

	set_object_header(&omap->om_o, bno,
			  APFS_OBJ_PHYSICAL | APFS_OBJECT_TYPE_OMAP,
			  APFS_OBJECT_TYPE_INVALID);
	munmap(omap, param->blocksize);
}
