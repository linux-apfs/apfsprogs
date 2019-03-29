/*
 *  apfsprogs/apfsck/super.c
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <assert.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "apfsck.h"
#include "btree.h"
#include "extents.h"
#include "htable.h"
#include "inode.h"
#include "object.h"
#include "types.h"
#include "super.h"

struct super_block *sb;
struct volume_superblock *vsb;

/**
 * read_super_copy - Read the copy of the container superblock in block 0
 *
 * Sets sb->s_blocksize and returns a pointer to the raw superblock in memory.
 */
static struct apfs_nx_superblock *read_super_copy(void)
{
	struct apfs_nx_superblock *msb_raw;
	int bsize_tmp;

	/*
	 * For now assume a small blocksize, we only need it so that we can
	 * read the actual blocksize from disk.
	 */
	bsize_tmp = APFS_NX_DEFAULT_BLOCK_SIZE;

	msb_raw = mmap(NULL, bsize_tmp, PROT_READ, MAP_PRIVATE,
		       fd, APFS_NX_BLOCK_NUM * bsize_tmp);
	if (msb_raw == MAP_FAILED) {
		perror(NULL);
		exit(1);
	}
	sb->s_blocksize = le32_to_cpu(msb_raw->nx_block_size);

	if (sb->s_blocksize != bsize_tmp) {
		munmap(msb_raw, bsize_tmp);

		msb_raw = mmap(NULL, sb->s_blocksize, PROT_READ, MAP_PRIVATE,
			       fd, APFS_NX_BLOCK_NUM * sb->s_blocksize);
		if (msb_raw == MAP_FAILED) {
			perror(NULL);
			exit(1);
		}
	}

	if (le32_to_cpu(msb_raw->nx_magic) != APFS_NX_MAGIC)
		report("Block zero", "wrong magic.");
	if (!obj_verify_csum(&msb_raw->nx_o))
		report("Block zero", "bad checksum.");
	if (le64_to_cpu(msb_raw->nx_o.o_oid) != APFS_OID_NX_SUPERBLOCK)
		report("Block zero", "bad object id.");

	return msb_raw;
}

/**
 * main_super_compare - Compare two copies of the container superblock
 * @desc: the superblock from the latest checkpoint
 * @copy: the superblock copy in block zero
 */
static void main_super_compare(struct apfs_nx_superblock *desc,
			       struct apfs_nx_superblock *copy)
{
	char *desc_bytes = (char *)desc;
	char *copy_bytes = (char *)copy;

	if (copy->nx_o.o_xid != desc->nx_o.o_xid)
		report_crash("Block zero");

	/*
	 * The nx_counters array doesn't always match.  Naturally, this means
	 * the checksum won't match either.
	 */
	if (memcmp(desc_bytes + 0x08, copy_bytes + 0x08, 0x3D8 - 0x08) ||
	    memcmp(desc_bytes + 0x4D8, copy_bytes + 0x4D8, 4096 - 0x4D8))
		report("Block zero", "fields don't match the checkpoint.");
}

/**
 * get_device_size - Get the block count of the device or image being checked
 * @blocksize: the filesystem blocksize
 */
static u64 get_device_size(unsigned int blocksize)
{
	struct stat buf;
	u64 size;

	if (fstat(fd, &buf)) {
		perror(NULL);
		exit(1);
	}

	if ((buf.st_mode & S_IFMT) == S_IFREG)
		return buf.st_size / blocksize;

	if (ioctl(fd, BLKGETSIZE64, &size)) {
		perror(NULL);
		exit(1);
	}
	return size / blocksize;
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
 * check_main_flags - Check consistency of container flags
 * @flags: the flags
 */
static void check_main_flags(u64 flags)
{
	if ((flags & APFS_NX_FLAGS_VALID_MASK) != flags)
		report("Container superblock", "invalid flag in use.");
	if (flags & (APFS_NX_RESERVED_1 | APFS_NX_RESERVED_2))
		report("Container superblock", "reserved flag in use.");
	if (flags & APFS_NX_CRYPTO_SW)
		report_unknown("Encryption");
}

/**
 * check_optional_main_features - Check the optional features of the container
 * @flags: the optional feature flags
 */
static void check_optional_main_features(u64 flags)
{
	if ((flags & APFS_NX_SUPPORTED_FEATURES_MASK) != flags)
		report("Container superblock", "unknown optional feature.");
	if (flags & APFS_NX_FEATURE_DEFRAG)
		report_unknown("Defragmentation");
	if (flags & APFS_NX_FEATURE_LCFD)
		report_unknown("Low-capacity fusion drive");
}

/**
 * check_rocompat_main_features - Check the ro compatible features of container
 * @flags: the read-only compatible feature flags
 */
static void check_rocompat_main_features(u64 flags)
{
	if ((flags & APFS_NX_SUPPORTED_ROCOMPAT_MASK) != flags)
		report("Container superblock", "unknown ro-compat feature.");
}

/**
 * check_incompat_main_features - Check the incompatible features of a container
 * @flags: the incompatible feature flags
 */
static void check_incompat_main_features(u64 flags)
{
	if ((flags & APFS_NX_SUPPORTED_INCOMPAT_MASK) != flags)
		report("Container superblock", "unknown incompatible feature.");
	if (flags & APFS_NX_INCOMPAT_VERSION1)
		report_unknown("APFS version 1");
	if (!(flags & APFS_NX_INCOMPAT_VERSION2))
		report_unknown("APFS versions other than 2");
	if (flags & APFS_NX_INCOMPAT_FUSION)
		report_unknown("Fusion drive");
}

/**
 * check_efi_information - Check the EFI info from the container superblock
 * @oid: the physical object id for the EFI driver record
 */
static void check_efi_information(u64 oid)
{
	struct apfs_nx_efi_jumpstart *efi;
	struct object obj;
	long long num_extents;
	u64 block_count = 0;
	u32 file_length;
	int i;

	if (!oid) /* Not all containers can be booted from, of course */
		return;

	efi = read_object(oid, NULL /* omap */, &obj);
	if (obj.type != APFS_OBJECT_TYPE_EFI_JUMPSTART)
		report("EFI info", "wrong object type.");
	if (obj.subtype != APFS_OBJECT_TYPE_INVALID)
		report("EFI info", "wrong object subtype.");

	if (le32_to_cpu(efi->nej_magic) != APFS_NX_EFI_JUMPSTART_MAGIC)
		report("EFI info", "wrong magic.");
	if (le32_to_cpu(efi->nej_version) != APFS_NX_EFI_JUMPSTART_VERSION)
		report("EFI info", "wrong version.");
	for (i = 0; i < 16; ++i)
		if (efi->nej_reserved[i])
			report("EFI info", "reserved field in use.");

	num_extents = le32_to_cpu(efi->nej_num_extents);
	if (sizeof(*efi) + num_extents * sizeof(efi->nej_rec_extents[0]) >
								sb->s_blocksize)
		report("EFI info", "number of extents cannot fit.");
	for (i = 0; i < num_extents; ++i) {
		struct apfs_prange *ext = &efi->nej_rec_extents[i];

		if (!ext->pr_block_count)
			report("EFI info", "empty extent.");
		block_count += le64_to_cpu(ext->pr_block_count);
	}

	file_length = le32_to_cpu(efi->nej_efi_file_len);
	if (!file_length)
		report("EFI info", "driver is empty.");
	if (file_length > block_count * sb->s_blocksize)
		report("EFI info", "driver doesn't fit in extents.");
	if (file_length <= (block_count - 1) * sb->s_blocksize)
		report("EFI info", "wasted space in driver extents.");
}

/**
 * check_ephemeral_information - Check the container's array of ephemeral info
 * @info: pointer to the nx_ephemeral_info array on the container superblock
 */
static void check_ephemeral_information(__le64 *info)
{
	u64 min_block_count;
	u64 container_size;
	int i;

	assert(sb->s_block_count);
	container_size = sb->s_block_count * sb->s_blocksize;

	/* TODO: support for small containers is very important */
	if (container_size < 128 * 1024 * 1024)
		report_unknown("Small container size");

	min_block_count = APFS_NX_EPH_MIN_BLOCK_COUNT;
	if (le64_to_cpu(info[0]) != ((min_block_count << 32)
				  | (APFS_NX_MAX_FILE_SYSTEM_EPH_STRUCTS << 16)
				  | APFS_NX_EPH_INFO_VERSION_1))
		report("Container superblock",
		       "bad first entry in ephemeral info.");

	/* Only the first entry in the info array is documented */
	for (i = 1; i < APFS_NX_EPH_INFO_COUNT; ++i)
		if (info[i])
			report_unknown("Ephemeral info array");
}

/**
 * map_main_super - Find the container superblock and map it into memory
 *
 * Sets sb->s_raw to the in-memory location of the main superblock.
 */
static void map_main_super(void)
{
	struct apfs_nx_superblock *msb_raw;
	struct apfs_nx_superblock *desc_raw = NULL;
	u64 desc_base;
	u32 desc_blocks;
	int i;

	/* Read the superblock from the last clean unmount */
	msb_raw = read_super_copy();

	/* We want to mount the latest valid checkpoint among the descriptors */
	desc_base = le64_to_cpu(msb_raw->nx_xp_desc_base);
	if (desc_base >> 63 != 0) {
		/* The highest bit is set when checkpoints are not contiguous */
		report("Block zero",
		       "checkpoint descriptor tree not yet supported.");
	}
	desc_blocks = le32_to_cpu(msb_raw->nx_xp_desc_blocks);
	if (desc_blocks > 10000) /* Arbitrary loop limit, is it enough? */
		report("Block zero", "too many checkpoint descriptors?");

	/* Now we go through the checkpoints one by one */
	sb->s_raw = NULL;
	sb->s_xid = le64_to_cpu(msb_raw->nx_o.o_xid);
	for (i = 0; i < desc_blocks; ++i) {
		if (desc_raw)
			munmap(desc_raw, sb->s_blocksize);

		desc_raw = mmap(NULL, sb->s_blocksize, PROT_READ, MAP_PRIVATE,
				fd, (desc_base + i) * sb->s_blocksize);
		if (desc_raw == MAP_FAILED) {
			perror(NULL);
			exit(1);
		}

		if (le32_to_cpu(desc_raw->nx_magic) != APFS_NX_MAGIC)
			continue; /* Not a superblock */
		if (le64_to_cpu(desc_raw->nx_o.o_xid) < sb->s_xid)
			continue; /* Old */
		if (!obj_verify_csum(&desc_raw->nx_o))
			continue; /* Corrupted */

		sb->s_xid = le64_to_cpu(desc_raw->nx_o.o_xid);
		if (sb->s_raw)
			munmap(sb->s_raw, sb->s_blocksize);
		sb->s_raw = desc_raw;
		desc_raw = NULL;
	}

	if (!sb->s_raw)
		report("Checkpoint descriptors", "latest is missing.");
	main_super_compare(sb->s_raw, msb_raw);

	if (le32_to_cpu(sb->s_raw->nx_block_size) != APFS_NX_DEFAULT_BLOCK_SIZE)
		report_unknown("Block size other than 4096");

	sb->s_block_count = le64_to_cpu(sb->s_raw->nx_block_count);
	if (!sb->s_block_count)
		report("Container superblock", "reports no block count.");
	if (sb->s_block_count > get_device_size(sb->s_blocksize))
		report("Container superblock", "too many blocks for device.");
	sb->s_max_vols = get_max_volumes(sb->s_block_count * sb->s_blocksize);
	if (sb->s_max_vols != le32_to_cpu(sb->s_raw->nx_max_file_systems))
		report("Container superblock", "bad maximum volume number.");

	check_main_flags(le64_to_cpu(sb->s_raw->nx_flags));
	check_optional_main_features(le64_to_cpu(sb->s_raw->nx_features));
	check_rocompat_main_features(le64_to_cpu(
				sb->s_raw->nx_readonly_compatible_features));
	check_incompat_main_features(le64_to_cpu(
				sb->s_raw->nx_incompatible_features));

	if (le32_to_cpu(sb->s_raw->nx_xp_desc_blocks) >> 31 ||
	    le32_to_cpu(sb->s_raw->nx_xp_data_blocks) >> 31 ||
	    le64_to_cpu(sb->s_raw->nx_xp_desc_base) >> 63 ||
	    le64_to_cpu(sb->s_raw->nx_xp_data_base) >> 63)
		report("Container superblock", "has checkpoint tree.");

	if (sb->s_raw->nx_test_type || sb->s_raw->nx_test_oid)
		report("Container superblock", "test field is set.");
	if (sb->s_raw->nx_blocked_out_prange.pr_block_count)
		report_unknown("Partition resizing");

	check_efi_information(le64_to_cpu(sb->s_raw->nx_efi_jumpstart));
	check_ephemeral_information(&sb->s_raw->nx_ephemeral_info[0]);

	for (i = 0; i < 16; ++i) {
		if (sb->s_raw->nx_fusion_uuid[i])
			report_unknown("Fusion drive");
	}

	/* Containers with no encryption may still have a value here, why? */
	if (sb->s_raw->nx_keylocker.pr_start_paddr ||
	    sb->s_raw->nx_keylocker.pr_block_count)
		report_weird("Container keybag");

	sb->s_next_oid = le64_to_cpu(sb->s_raw->nx_next_oid);
	if (sb->s_xid + 1 != le64_to_cpu(msb_raw->nx_next_xid))
		report("Container superblock", "next transaction id is wrong.");
	munmap(msb_raw, sb->s_blocksize);
}

/**
 * software_strlen - Calculate the length of a software info string
 * @str: the string
 *
 * Also checks that the string has a proper null-termination, and only null
 * characters afterwards.
 */
static int software_strlen(u8 *str)
{
	int length = strnlen((char *)str, APFS_MODIFIED_NAMELEN);
	u8 *end = str + APFS_MODIFIED_NAMELEN;

	if (length == APFS_MODIFIED_NAMELEN)
		report("Volume software id", "no NULL-termination.");
	for (str += length + 1; str != end; ++str) {
		if (*str)
			report("Volume software id", "goes on after NULL.");
	}
	return length;
}

/**
 * check_software_info - Check the fields reporting implementation info
 * @formatted_by: information about the software that created the volume
 * @modified_by:  information about the software that modified the volume
 */
static void check_software_information(struct apfs_modified_by *formatted_by,
				       struct apfs_modified_by *modified_by)
{
	struct apfs_modified_by *end_mod_by;
	int length;
	bool mods_over;
	u64 xid;

	mods_over = false;
	end_mod_by = modified_by + APFS_MAX_HIST;
	xid = sb->s_xid + 1; /* Last possible xid */

	for (; modified_by != end_mod_by; ++modified_by) {
		length = software_strlen(modified_by->id);
		if (!length &&
		    (modified_by->timestamp || modified_by->last_xid))
			report("Volume modification info", "entry without id.");

		if (mods_over) {
			if (length)
				report("Volume modification info",
				       "empty entry should end the list.");
			continue;
		}
		if (!length) {
			mods_over = true;
			continue;
		}

		/* The first entry must be the most recent */
		if (xid <= le64_to_cpu(modified_by->last_xid))
			report("Volume modification info",
			       "entries are not in order.");
		xid = le64_to_cpu(modified_by->last_xid);
	}

	length = software_strlen(formatted_by->id);
	if (!length)
		report("Volume superblock", "creation information is missing.");

	vsb->v_first_xid = le64_to_cpu(formatted_by->last_xid);
	if (xid <= vsb->v_first_xid)
		report("Volume creation info", "transaction is too recent.");
}

/**
 * check_volume_flags - Check consistency of volume flags
 * @flags: the flags
 */
static void check_volume_flags(u64 flags)
{
	if ((flags & APFS_FS_FLAGS_VALID_MASK) != flags)
		report("Volume superblock", "invalid flag in use.");
	if (flags & APFS_FS_RESERVED_4)
		report("Volume superblock", "reserved flag in use.");

	if (!(flags & APFS_FS_UNENCRYPTED))
		report_unknown("Encryption");
	else if (flags & (APFS_FS_EFFACEABLE | APFS_FS_ONEKEY))
		report("Volume superblock", "inconsistent crypto flags.");

	if (flags & (APFS_FS_SPILLEDOVER | APFS_FS_RUN_SPILLOVER_CLEANER))
		report_unknown("Fusion drive");
}

/**
 * check_optional_vol_features - Check the optional features of a volume
 * @flags: the optional feature flags
 */
static void check_optional_vol_features(u64 flags)
{
	if ((flags & APFS_SUPPORTED_FEATURES_MASK) != flags)
		report("Volume superblock", "unknown optional feature.");
	if (flags & APFS_FEATURE_DEFRAG_PRERELEASE)
		report("Volume superblock", "prerelease defrag enabled.");

	/* TODO: should be easy to support, but I need an image for testing */
	if (!(flags & APFS_FEATURE_HARDLINK_MAP_RECORDS))
		report_unknown("Volume without sibling map records");
}

/**
 * check_rocompat_vol_features - Check the ro compatible features of a volume
 * @flags: the read-only compatible feature flags
 */
static void check_rocompat_vol_features(u64 flags)
{
	if ((flags & APFS_SUPPORTED_ROCOMPAT_MASK) != flags)
		report("Volume superblock", "unknown ro compatible feature.");
}

/**
 * check_incompat_vol_features - Check the incompatible features of a volume
 * @flags: the incompatible feature flags
 */
static void check_incompat_vol_features(u64 flags)
{
	if ((flags & APFS_SUPPORTED_INCOMPAT_MASK) != flags)
		report("Volume superblock", "unknown incompatible feature.");
	if (flags & APFS_INCOMPAT_DATALESS_SNAPS)
		report_unknown("Dataless snapshots");
	if (flags & APFS_INCOMPAT_ENC_ROLLED)
		report_unknown("Change of encryption keys");

	/*
	 * I don't believe actual normalization-sensitive volumes exist, the
	 * normalization-insensitive flag just means case-sensitive.
	 */
	if ((bool)(flags & APFS_INCOMPAT_CASE_INSENSITIVE) !=
	    !(bool)(flags & APFS_INCOMPAT_NORMALIZATION_INSENSITIVE))
		report("Volume superblock", "normalization sensitive?");
}

/**
 * map_volume_super - Find the volume superblock and map it into memory
 * @vol:	volume number
 * @vsb:	volume superblock struct to receive the results
 *
 * Returns the in-memory location of the volume superblock, or NULL if there
 * is no volume with this number.
 */
static struct apfs_superblock *map_volume_super(int vol,
						struct volume_superblock *vsb)
{
	struct apfs_nx_superblock *msb_raw = sb->s_raw;
	char *vol_name;
	u64 vol_id;

	vol_id = le64_to_cpu(msb_raw->nx_fs_oid[vol]);
	if (vol_id == 0) {
		if (vol > sb->s_max_vols)
			report("Container superblock", "too many volumes.");
		for (++vol; vol < APFS_NX_MAX_FILE_SYSTEMS; ++vol)
			if (msb_raw->nx_fs_oid[vol])
				report("Container superblock",
				       "volume array goes on after NULL.");
		return NULL;
	}

	vsb->v_raw = read_object(vol_id, sb->s_omap->root, &vsb->v_obj);
	if (vsb->v_obj.type != APFS_OBJECT_TYPE_FS)
		report("Volume superblock", "wrong object type.");
	if (vsb->v_obj.subtype != APFS_OBJECT_TYPE_INVALID)
		report("Volume superblock", "wrong object subtype.");

	if (le32_to_cpu(vsb->v_raw->apfs_fs_index) != vol)
		report("Volume superblock", "wrong reported volume number.");
	if (le32_to_cpu(vsb->v_raw->apfs_magic) != APFS_MAGIC)
		report("Volume superblock", "wrong magic.");

	check_optional_vol_features(le64_to_cpu(vsb->v_raw->apfs_features));
	check_rocompat_vol_features(le64_to_cpu(
				vsb->v_raw->apfs_readonly_compatible_features));
	check_incompat_vol_features(le64_to_cpu(
				vsb->v_raw->apfs_incompatible_features));

	vsb->v_next_obj_id = le64_to_cpu(vsb->v_raw->apfs_next_obj_id);
	vsb->v_next_doc_id = le32_to_cpu(vsb->v_raw->apfs_next_doc_id);

	vol_name = (char *)vsb->v_raw->apfs_volname;
	if (strnlen(vol_name, APFS_VOLNAME_LEN) == APFS_VOLNAME_LEN)
		report("Volume superblock", "name lacks NULL-termination.");

	check_volume_flags(le64_to_cpu(vsb->v_raw->apfs_fs_flags));
	check_software_information(&vsb->v_raw->apfs_formatted_by,
				   &vsb->v_raw->apfs_modified_by[0]);

	/*
	 * The documentation suggests that other tree types could be possible,
	 * but I don't understand how that would work.
	 */
	if (le32_to_cpu(vsb->v_raw->apfs_root_tree_type) !=
				(APFS_OBJ_VIRTUAL | APFS_OBJECT_TYPE_BTREE))
		report("Volume superblock", "wrong type for catalog tree.");
	if (le32_to_cpu(vsb->v_raw->apfs_extentref_tree_type) !=
				(APFS_OBJ_PHYSICAL | APFS_OBJECT_TYPE_BTREE))
		report("Volume superblock", "wrong type for extentref tree.");
	if (le32_to_cpu(vsb->v_raw->apfs_snap_meta_tree_type) !=
				(APFS_OBJ_PHYSICAL | APFS_OBJECT_TYPE_BTREE))
		report("Volume superblock", "wrong type for snapshot tree.");

	if (le16_to_cpu(vsb->v_raw->reserved) != 0)
		report("Volume superblock", "reserved field is in use.");
	if (le64_to_cpu(vsb->v_raw->apfs_root_to_xid) != 0)
		report_unknown("Root from snapshot");
	if (le64_to_cpu(vsb->v_raw->apfs_er_state_oid) != 0)
		report_unknown("Encryption or decryption in progress");
	if (le64_to_cpu(vsb->v_raw->apfs_revert_to_xid) != 0)
		report_unknown("Revert to a snapshot");
	if (le64_to_cpu(vsb->v_raw->apfs_revert_to_sblock_oid) != 0)
		report_unknown("Revert to a volume superblock");

	return vsb->v_raw;
}

/**
 * parse_super - Parse the on-disk superblock and check for corruption
 */
void parse_super(void)
{
	int vol;

	sb = calloc(1, sizeof(*sb));
	if (!sb) {
		perror(NULL);
		exit(1);
	}

	map_main_super();
	/* Check for corruption in the container object map */
	sb->s_omap = parse_omap_btree(le64_to_cpu(sb->s_raw->nx_omap_oid));

	for (vol = 0; vol < APFS_NX_MAX_FILE_SYSTEMS; ++vol) {
		struct apfs_superblock *vsb_raw;

		vsb = calloc(1, sizeof(*vsb));
		if (!vsb) {
			perror(NULL);
			exit(1);
		}
		vsb->v_cnid_table = alloc_htable();
		vsb->v_dstream_table = alloc_htable();
		vsb->v_inode_table = alloc_htable();

		vsb_raw = map_volume_super(vol, vsb);
		if (!vsb_raw) {
			free(vsb);
			break;
		}

		/* Check for corruption in the volume object map... */
		vsb->v_omap = parse_omap_btree(
				le64_to_cpu(vsb_raw->apfs_omap_oid));
		/* ...and in the catalog tree */
		vsb->v_cat = parse_cat_btree(
				le64_to_cpu(vsb_raw->apfs_root_tree_oid),
				vsb->v_omap->root);

		free_inode_table(vsb->v_inode_table);
		vsb->v_inode_table = NULL;
		free_dstream_table(vsb->v_dstream_table);
		vsb->v_dstream_table = NULL;
		free_cnid_table(vsb->v_cnid_table);
		vsb->v_cnid_table = NULL;

		if (!vsb->v_has_root)
			report("Catalog", "the root directory is missing.");
		if (!vsb->v_has_priv)
			report("Catalog", "the private directory is missing.");

		if (le64_to_cpu(vsb_raw->apfs_num_files) !=
							vsb->v_file_count)
			/* Sometimes this is off by one.  TODO: why? */
			report_weird("File count in volume superblock");
		if (le64_to_cpu(vsb_raw->apfs_num_directories) !=
							vsb->v_dir_count)
			report("Volume superblock", "bad directory count.");
		if (le64_to_cpu(vsb_raw->apfs_num_symlinks) !=
							vsb->v_symlink_count)
			report("Volume superblock", "bad symlink count.");
		if (le64_to_cpu(vsb_raw->apfs_num_other_fsobjects) !=
							vsb->v_special_count)
			report("Volume superblock", "bad special file count.");

		sb->s_volumes[vol] = vsb;
	}

	return;
}
