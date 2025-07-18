/*
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <apfs/parameters.h>
#include <apfs/raw.h>
#include "btree.h"
#include "mkapfs.h"
#include "object.h"
#include "spaceman.h"
#include "super.h"
#include "version.h"

struct ephemeral_info eph_info = {0};

/* String to identify the program and its version */
#define MKFS_ID_STRING	"mkapfs by eafer (" GIT_COMMIT ")"

/**
 * set_uuid - Set a UUID field
 * @field:	on-disk field to set
 * @uuid:	pointer to the UUID string in standard format
 */
static void set_uuid(char *field, char *uuid)
{
	int ret;
	char *stdformat = "%2hhx%2hhx%2hhx%2hhx-%2hhx%2hhx-%2hhx%2hhx-"
			  "%2hhx%2hhx-%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx";

	ret = sscanf(uuid, stdformat, &field[0], &field[1], &field[2],
				      &field[3], &field[4], &field[5],
				      &field[6], &field[7], &field[8],
				      &field[9], &field[10], &field[11],
				      &field[12], &field[13], &field[14],
				      &field[15]);
	if (ret == 16)
		return;

	fatal("please provide a UUID in standard format");
}

/**
 * zero_area - Wipe an area on disk
 * @start:	first block of the area
 * @blocks:	block count for the area
 */
static void zero_area(u64 start, u64 blocks)
{
	u64 bno;
	void *block;

	for (bno = start; bno < start + blocks; ++bno) {
		block = get_zeroed_block();
		apfs_writeall(block, 1, bno);
	}
}

/**
 * set_checkpoint_areas - Set all sb fields describing the checkpoint areas
 * @sb: pointer to the superblock copy on disk
 */
static void set_checkpoint_areas(struct apfs_nx_superblock *sb)
{
	/* First set the checkpoint descriptor area fields */
	sb->nx_xp_desc_base = cpu_to_le64(CPOINT_DESC_BASE);
	sb->nx_xp_desc_blocks = cpu_to_le32(CPOINT_DESC_BLOCKS);
	/* The first two blocks hold the superblock and the mappings */
	sb->nx_xp_desc_len = cpu_to_le32(2);
	sb->nx_xp_desc_next = cpu_to_le32(2);
	sb->nx_xp_desc_index = 0;

	/* Now set the checkpoint data area fields */
	sb->nx_xp_data_base = cpu_to_le64(CPOINT_DATA_BASE);
	sb->nx_xp_data_blocks = cpu_to_le32(CPOINT_DATA_BLOCKS);
	/*
	 * Room for the space manager, the two free queues and the reaper. Also
	 * for the fusion writeback cache and the tier 2 free queue, which
	 * usually don't exist.
	 */
	sb->nx_xp_data_len = cpu_to_le32(eph_info.total_blkcnt);
	sb->nx_xp_data_next = cpu_to_le32(eph_info.total_blkcnt);
	sb->nx_xp_data_index = 0;
}

/**
 * get_max_volumes - Calculate the maximum number of volumes for the container
 * @size: the container size, in bytes
 */
static u32 get_max_volumes(u64 size)
{
	u32 max_vols;

	/* Divide by 512 MiB and round up, as the reference requires */
	max_vols = DIV_ROUND_UP(size, 512 * 1024 * 1024);
	if (max_vols > APFS_NX_MAX_FILE_SYSTEMS)
		max_vols = APFS_NX_MAX_FILE_SYSTEMS;
	return max_vols;
}

/**
 * set_ephemeral_info - Set the container's array of ephemeral info
 * @info: pointer to the nx_ephemeral_info array on the container superblock
 */
static void set_ephemeral_info(__le64 *info)
{
	u64 container_size;
	u64 min_block_count;

	container_size = param->block_count * param->blocksize;
	if (container_size < 128 * 1024 * 1024)
		min_block_count = main_fq_node_limit(param->block_count);
	else
		min_block_count = APFS_NX_EPH_MIN_BLOCK_COUNT;

	/* Only the first entry is documented, leave the others as zero */
	*info = cpu_to_le64((min_block_count << 32)
			    | (APFS_NX_MAX_FILE_SYSTEM_EPH_STRUCTS << 16)
			    | APFS_NX_EPH_INFO_VERSION_1);
}

/**
 * set_meta_crypto - Set a volume's meta_crypto field
 * @wmcs: the structure to set
 */
static void set_meta_crypto(struct apfs_wrapped_meta_crypto_state *wmcs)
{
	wmcs->major_version = cpu_to_le16(APFS_WMCS_MAJOR_VERSION);
	wmcs->minor_version = cpu_to_le16(APFS_WMCS_MINOR_VERSION);
	wmcs->cpflags = 0; /* No flags exist yet */
	wmcs->persistent_class = cpu_to_le32(APFS_PROTECTION_CLASS_F);
	wmcs->key_os_version = 0; /* Not sure what to do with this on Linux */
	wmcs->key_revision = cpu_to_le16(1);
}

/**
 * make_volume - Make a volume
 * @bno: block number for the volume superblock
 * @oid: object id for the volume superblock
 */
static void make_volume(u64 bno, u64 oid)
{
	struct apfs_superblock *vsb = get_zeroed_block();

	vsb->apfs_magic = cpu_to_le32(APFS_MAGIC);

	vsb->apfs_features = cpu_to_le64(APFS_FEATURE_HARDLINK_MAP_RECORDS);
	if (param->norm_sensitive)
		vsb->apfs_incompatible_features = 0;
	else if (param->case_sensitive)
		vsb->apfs_incompatible_features =
			cpu_to_le64(APFS_INCOMPAT_NORMALIZATION_INSENSITIVE);
	else
		vsb->apfs_incompatible_features =
			cpu_to_le64(APFS_INCOMPAT_CASE_INSENSITIVE);

	/* Encryption is not supported, but this still needs to be set */
	set_meta_crypto(&vsb->apfs_meta_crypto);

	/* We won't create any user inodes */
	vsb->apfs_next_obj_id = cpu_to_le64(APFS_MIN_USER_INO_NUM);

	set_uuid(vsb->apfs_vol_uuid, param->vol_uuid);
	vsb->apfs_fs_flags = cpu_to_le64(APFS_FS_UNENCRYPTED);

	assert(strlen(MKFS_ID_STRING) < sizeof(vsb->apfs_formatted_by.id));
	strcpy((char *)vsb->apfs_formatted_by.id, MKFS_ID_STRING);
	vsb->apfs_formatted_by.timestamp = cpu_to_le64(get_timestamp());
	vsb->apfs_formatted_by.last_xid = cpu_to_le64(MKFS_XID);

	strcpy((char *)vsb->apfs_volname, param->label);
	vsb->apfs_next_doc_id = cpu_to_le32(APFS_MIN_DOC_ID);

	vsb->apfs_root_tree_type = cpu_to_le32(APFS_OBJ_VIRTUAL |
					       APFS_OBJECT_TYPE_BTREE);
	vsb->apfs_extentref_tree_type = cpu_to_le32(APFS_OBJ_PHYSICAL |
						    APFS_OBJECT_TYPE_BTREE);
	vsb->apfs_snap_meta_tree_type = cpu_to_le32(APFS_OBJ_PHYSICAL |
						    APFS_OBJECT_TYPE_BTREE);

	vsb->apfs_omap_oid = cpu_to_le64(FIRST_VOL_OMAP_BNO);
	make_omap_btree(FIRST_VOL_OMAP_BNO, true /* is_vol */);
	vsb->apfs_root_tree_oid = cpu_to_le64(FIRST_VOL_CAT_ROOT_OID);
	make_cat_root(FIRST_VOL_CAT_ROOT_BNO, FIRST_VOL_CAT_ROOT_OID);

	/* The snapshot metadata and extent reference trees are empty */
	vsb->apfs_extentref_tree_oid = cpu_to_le64(FIRST_VOL_EXTREF_ROOT_BNO);
	make_empty_btree_root(FIRST_VOL_EXTREF_ROOT_BNO,
			      FIRST_VOL_EXTREF_ROOT_BNO,
			      APFS_OBJECT_TYPE_BLOCKREFTREE);
	vsb->apfs_snap_meta_tree_oid = cpu_to_le64(FIRST_VOL_SNAP_ROOT_BNO);
	make_empty_btree_root(FIRST_VOL_SNAP_ROOT_BNO, FIRST_VOL_SNAP_ROOT_BNO,
			      APFS_OBJECT_TYPE_SNAPMETATREE);

	/* The root nodes of all four trees, plus the object map structure */
	vsb->apfs_fs_alloc_count = cpu_to_le64(5);

	set_object_header(&vsb->apfs_o, param->blocksize, oid,
			  APFS_OBJ_VIRTUAL | APFS_OBJECT_TYPE_FS,
			  APFS_OBJECT_TYPE_INVALID);
	apfs_writeall(vsb, 1, bno);
}

/**
 * make_cpoint_map_block - Make the mapping block for the one checkpoint
 * @bno: block number to use
 */
static void make_cpoint_map_block(u64 bno)
{
	struct apfs_checkpoint_map_phys *block = get_zeroed_block();
	struct apfs_checkpoint_mapping *map;
	int idx = 0;

	block->cpm_flags = cpu_to_le32(APFS_CHECKPOINT_MAP_LAST);

	/* Set the checkpoint mapping for the reaper */
	map = &block->cpm_map[idx++];
	map->cpm_type = cpu_to_le32(APFS_OBJ_EPHEMERAL |
				    APFS_OBJECT_TYPE_NX_REAPER);
	map->cpm_subtype = cpu_to_le32(APFS_OBJECT_TYPE_INVALID);
	map->cpm_size = cpu_to_le32(param->blocksize);
	map->cpm_oid = cpu_to_le64(REAPER_OID);
	map->cpm_paddr = cpu_to_le64(eph_info.reaper_bno);

	/* Set the checkpoint mapping for the space manager */
	map = &block->cpm_map[idx++];
	map->cpm_type = cpu_to_le32(APFS_OBJ_EPHEMERAL |
				    APFS_OBJECT_TYPE_SPACEMAN);
	map->cpm_subtype = cpu_to_le32(APFS_OBJECT_TYPE_INVALID);
	map->cpm_size = cpu_to_le32(eph_info.spaceman_sz);
	map->cpm_oid = cpu_to_le64(SPACEMAN_OID);
	map->cpm_paddr = cpu_to_le64(eph_info.spaceman_bno);

	/* Set the checkpoint mapping for the internal-pool free queue root */
	map = &block->cpm_map[idx++];
	map->cpm_type = cpu_to_le32(APFS_OBJ_EPHEMERAL |
				    APFS_OBJECT_TYPE_BTREE);
	map->cpm_subtype = cpu_to_le32(APFS_OBJECT_TYPE_SPACEMAN_FREE_QUEUE);
	map->cpm_size = cpu_to_le32(param->blocksize);
	map->cpm_oid = cpu_to_le64(IP_FREE_QUEUE_OID);
	map->cpm_paddr = cpu_to_le64(eph_info.ip_free_queue_bno);

	/* Set the checkpoint mapping for the main device free queue root */
	map = &block->cpm_map[idx++];
	map->cpm_type = cpu_to_le32(APFS_OBJ_EPHEMERAL |
				    APFS_OBJECT_TYPE_BTREE);
	map->cpm_subtype = cpu_to_le32(APFS_OBJECT_TYPE_SPACEMAN_FREE_QUEUE);
	map->cpm_size = cpu_to_le32(param->blocksize);
	map->cpm_oid = cpu_to_le64(MAIN_FREE_QUEUE_OID);
	map->cpm_paddr = cpu_to_le64(eph_info.main_free_queue_bno);

	if (fd_tier2 != -1) {
		/* Set the checkpoint mapping for the tier 2 free queue root */
		map = &block->cpm_map[idx++];
		map->cpm_type = cpu_to_le32(APFS_OBJ_EPHEMERAL | APFS_OBJECT_TYPE_BTREE);
		map->cpm_subtype = cpu_to_le32(APFS_OBJECT_TYPE_SPACEMAN_FREE_QUEUE);
		map->cpm_size = cpu_to_le32(param->blocksize);
		map->cpm_oid = cpu_to_le64(TIER2_FREE_QUEUE_OID);
		map->cpm_paddr = cpu_to_le64(eph_info.tier2_free_queue_bno);

		/* Set the checkpoint mapping for the fusion wb cache state */
		map = &block->cpm_map[idx++];
		map->cpm_type = cpu_to_le32(APFS_OBJ_EPHEMERAL | APFS_OBJECT_TYPE_NX_FUSION_WBC);
		map->cpm_subtype = cpu_to_le32(APFS_OBJECT_TYPE_INVALID);
		map->cpm_size = cpu_to_le32(param->blocksize);
		map->cpm_oid = cpu_to_le64(FUSION_WBC_OID);
		map->cpm_paddr = cpu_to_le64(eph_info.fusion_wbc_bno);
	}

	block->cpm_count = cpu_to_le32(idx);

	set_object_header(&block->cpm_o, param->blocksize, bno,
			  APFS_OBJ_PHYSICAL | APFS_OBJECT_TYPE_CHECKPOINT_MAP,
			  APFS_OBJECT_TYPE_INVALID);
	apfs_writeall(block, 1, bno);
}

/**
 * make_cpoint_superblock - Make the one checkpoint superblock
 * @bno:	block number to use
 * @sb_copy:	copy of the superblock at block zero
 *
 * For now just copies @sb_copy into @bno.  TODO: figure out what to do with
 * the nx_counters array.
 */
static void make_cpoint_superblock(u64 bno, struct apfs_nx_superblock *sb_copy)
{
	struct apfs_nx_superblock *sb = get_zeroed_block();

	memcpy(sb, sb_copy, sizeof(*sb));
	apfs_writeall(sb, 1, bno);
}

/**
 * make_tier2_superblock - Make the superblock copy in the tier 2 device
 * @sb: copy of the superblock at block zero of the main device
 */
static void make_tier2_superblock(struct apfs_nx_superblock *sb)
{
	struct apfs_nx_superblock *tier2_sb = NULL;

	tier2_sb = get_zeroed_block();
	memcpy(tier2_sb, sb, sizeof(*sb));

	/* The top bit is used to tell apart the main and tier 2 devices */
	tier2_sb->nx_fusion_uuid[15] |= 0x01;

	/* Update the checksum */
	set_object_header(&tier2_sb->nx_o, param->blocksize, APFS_OID_NX_SUPERBLOCK,
			  APFS_OBJ_EPHEMERAL | APFS_OBJECT_TYPE_NX_SUPERBLOCK,
			  APFS_OBJECT_TYPE_INVALID);
	apfs_writeall(tier2_sb, 1, APFS_FUSION_TIER2_DEVICE_BYTE_ADDR / param->blocksize);
}

/**
 * make_empty_reaper - Make an empty reaper
 * @bno: block number to use
 * @oid: object id
 */
static void make_empty_reaper(u64 bno, u64 oid)
{
	struct apfs_nx_reaper_phys *reaper = get_zeroed_block();

	reaper->nr_next_reap_id = cpu_to_le64(1);
	reaper->nr_flags = cpu_to_le32(APFS_NR_BHM_FLAG);
	reaper->nr_state_buffer_size = cpu_to_le32(param->blocksize -
						   sizeof(*reaper));

	set_object_header(&reaper->nr_o, param->blocksize, oid,
			  APFS_OBJ_EPHEMERAL | APFS_OBJECT_TYPE_NX_REAPER,
			  APFS_OBJECT_TYPE_INVALID);
	apfs_writeall(reaper, 1, bno);
}

/**
 * make_empty_fusion_wbc_state - Make the state struct for en empty fusion wbc
 * @bno: block number to use
 * @oid: object id
 */
static void make_empty_fusion_wbc_state(u64 bno, u64 oid)
{
	struct apfs_fusion_wbc_phys *wbc = get_zeroed_block();

	wbc->fwp_version = cpu_to_le64(0x70);

	set_object_header(&wbc->fwp_objHdr, param->blocksize, oid, APFS_OBJ_EPHEMERAL | APFS_OBJECT_TYPE_NX_FUSION_WBC, APFS_OBJECT_TYPE_INVALID);
	apfs_writeall(wbc, 1, bno);
}

/**
 * set_ephemeral_bnos - Decide the block numbers for the ephemeral objects
 */
static void set_ephemeral_bnos(void)
{
	eph_info.reaper_bno = CPOINT_DATA_BASE;
	eph_info.spaceman_bno = eph_info.reaper_bno + 1;
	eph_info.spaceman_sz = spaceman_size();
	eph_info.spaceman_blkcnt = eph_info.spaceman_sz / param->blocksize;
	eph_info.ip_free_queue_bno = eph_info.spaceman_bno + eph_info.spaceman_blkcnt;
	eph_info.main_free_queue_bno = eph_info.ip_free_queue_bno + 1;
	eph_info.total_blkcnt = eph_info.main_free_queue_bno - eph_info.reaper_bno + 1;
	if (fd_tier2 != -1) {
		eph_info.tier2_free_queue_bno = eph_info.main_free_queue_bno + 1;
		eph_info.fusion_wbc_bno = eph_info.tier2_free_queue_bno + 1;
		eph_info.total_blkcnt += 2;
	}

	/* Just a rough limit in case tier 2 is huge */
	if (eph_info.total_blkcnt >= CPOINT_DATA_BLOCKS / 4)
		fatal("space manager too big for the main device");
}

/**
 * make_container - Make the whole filesystem
 */
void make_container(void)
{
	struct apfs_nx_superblock *sb_copy = NULL;
	u64 size = param->blocksize * param->block_count;

	sb_copy = get_zeroed_block();

	sb_copy->nx_magic = cpu_to_le32(APFS_NX_MAGIC);
	sb_copy->nx_block_size = cpu_to_le32(param->blocksize);
	sb_copy->nx_block_count = cpu_to_le64(param->block_count);

	/* We only support version 2 of APFS */
	sb_copy->nx_incompatible_features |= cpu_to_le64(APFS_NX_INCOMPAT_VERSION2);
	if (param->tier2_blkcnt)
		sb_copy->nx_incompatible_features |= cpu_to_le64(APFS_NX_INCOMPAT_FUSION);

	set_uuid(sb_copy->nx_uuid, param->main_uuid);
	if (param->tier2_blkcnt) {
		set_uuid(sb_copy->nx_fusion_uuid, param->fusion_uuid);
		/* This bit will get flipped for the tier 2 superblock */
		sb_copy->nx_fusion_uuid[15] &= ~0x01;
	}

	/* Leave some room for the objects created by the mkfs */
	sb_copy->nx_next_oid = cpu_to_le64(APFS_OID_RESERVED_COUNT + 100);
	sb_copy->nx_next_xid = cpu_to_le64(MKFS_XID + 1);

	/*
	 * We need to know the spaceman size before we can decide the location
	 * of the other ephemeral objects.
	 */
	set_spaceman_info();
	set_ephemeral_bnos();

	sb_copy->nx_spaceman_oid = cpu_to_le64(SPACEMAN_OID);
	make_spaceman(eph_info.spaceman_bno, SPACEMAN_OID);
	sb_copy->nx_reaper_oid = cpu_to_le64(REAPER_OID);
	make_empty_reaper(eph_info.reaper_bno, REAPER_OID);
	sb_copy->nx_omap_oid = cpu_to_le64(MAIN_OMAP_BNO);
	make_omap_btree(MAIN_OMAP_BNO, false /* is_vol */);
	if (fd_tier2 != -1) {
		sb_copy->nx_fusion_mt_oid = cpu_to_le64(FUSION_MT_BNO);
		make_empty_btree_root(FUSION_MT_BNO, FUSION_MT_BNO, APFS_OBJECT_TYPE_FUSION_MIDDLE_TREE);
		sb_copy->nx_fusion_wbc_oid = cpu_to_le64(FUSION_WBC_OID);
		make_empty_fusion_wbc_state(eph_info.fusion_wbc_bno, FUSION_WBC_OID);
		sb_copy->nx_fusion_wbc.pr_start_paddr = cpu_to_le64(FUSION_WBC_FIRST_BNO);
		/* TODO: figure out the formula for the block count */
		sb_copy->nx_fusion_wbc.pr_block_count = cpu_to_le64(1);
	}

	set_checkpoint_areas(sb_copy);

	sb_copy->nx_max_file_systems = cpu_to_le32(get_max_volumes(size));
	sb_copy->nx_fs_oid[0] = cpu_to_le64(FIRST_VOL_OID);
	make_volume(FIRST_VOL_BNO, FIRST_VOL_OID);

	set_ephemeral_info(&sb_copy->nx_ephemeral_info[0]);

	set_object_header(&sb_copy->nx_o, param->blocksize, APFS_OID_NX_SUPERBLOCK,
			  APFS_OBJ_EPHEMERAL | APFS_OBJECT_TYPE_NX_SUPERBLOCK,
			  APFS_OBJECT_TYPE_INVALID);

	/*
	 * If the disk was ever formatted as APFS, valid checkpoint superblocks
	 * may still remain.  Wipe the area to avoid mounting them by mistake.
	 */
	zero_area(CPOINT_DESC_BASE, CPOINT_DESC_BLOCKS);
	make_cpoint_map_block(CPOINT_MAP_BNO);
	make_cpoint_superblock(CPOINT_SB_BNO, sb_copy);
	if (fd_tier2 != -1)
		make_tier2_superblock(sb_copy);

	apfs_writeall(sb_copy, 1, APFS_NX_BLOCK_NUM);
}
