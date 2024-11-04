/*
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <linux/fs.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <apfs/raw.h>
#include "mkapfs.h"
#include "super.h"
#include "version.h"

int fd_main = -1;
int fd_tier2 = -1;
struct parameters *param;
static char *progname;

/**
 * usage - Print usage information and exit
 */
static void usage(void)
{
	fprintf(stderr,
		"usage: %s [-L label] [-U UUID] [-u UUID] [-F tier2] [-sv] "
		"device [blocks]\n",
		progname);
	exit(EXIT_FAILURE);
}

/**
 * version - Print version information and exit
 */
static void version(void)
{
	if (*GIT_COMMIT)
		printf("mkapfs %s\n", GIT_COMMIT);
	else
		printf("mkapfs - unknown git commit id\n");
	exit(EXIT_FAILURE);
}

/**
 * system_error - Print a system error message and exit
 */
__attribute__((noreturn)) void system_error(void)
{
	perror(progname);
	exit(EXIT_FAILURE);
}

/**
 * fatal - Print a message and exit with an error code
 * @message: text to print
 */
__attribute__((noreturn)) void fatal(const char *message)
{
	fprintf(stderr, "%s: %s\n", progname, message);
	exit(EXIT_FAILURE);
}

/**
 * get_device_size - Get the block count for a given device or image
 * @device_fd:	file descriptor for the device
 * @blocksize:	the filesystem blocksize
 */
static u64 get_device_size(int device_fd, unsigned int blocksize)
{
	struct stat buf;
	u64 size;

	if (fstat(device_fd, &buf))
		system_error();

	if ((buf.st_mode & S_IFMT) == S_IFREG)
		return buf.st_size / blocksize;

	if (ioctl(device_fd, BLKGETSIZE64, &size))
		system_error();
	return size / blocksize;
}

static u64 get_main_device_size(unsigned int blocksize)
{
	return get_device_size(fd_main, blocksize);
}

static u64 get_tier2_device_size(unsigned int blocksize)
{
	if (fd_tier2 == -1)
		return 0;
	return get_device_size(fd_tier2, blocksize);
}

/**
 * get_random_uuid - Get a random UUID string in standard format
 *
 * Returns a pointer to the string.
 */
static char *get_random_uuid(void)
{
	char *uuid;
	ssize_t ret;

	/* Length of a null-terminated UUID standard format string */
	uuid = malloc(37);
	if (!uuid)
		system_error();

	/* Linux provides randomly generated UUIDs at /proc */
	do {
		int uuid_fd;

		uuid_fd = open("/proc/sys/kernel/random/uuid", O_RDONLY);
		if (uuid_fd == -1)
			system_error();

		ret = read(uuid_fd, uuid, 36);
		if (ret == -1)
			system_error();

		close(uuid_fd);
	} while (ret != 36);

	/* Put a null-termination, just in case */
	uuid[36] = 0;
	return uuid;
}

/**
 * complete_parameters - Set all uninitialized parameters to their defaults
 *
 * Also runs any needed checks on the parameters provided by the user.
 */
static void complete_parameters(void)
{
	if (!param->blocksize)
		param->blocksize = APFS_NX_DEFAULT_BLOCK_SIZE;

	param->main_blkcnt = get_main_device_size(param->blocksize);
	param->tier2_blkcnt = get_tier2_device_size(param->blocksize);
	if (param->block_count) {
		if (param->block_count > param->main_blkcnt) {
			fprintf(stderr, "%s: device is not big enough\n", progname);
			exit(EXIT_FAILURE);
		}
		param->main_blkcnt = param->block_count;
	} else {
		param->block_count = param->main_blkcnt + param->tier2_blkcnt;
	}
	if (param->main_blkcnt * param->blocksize < 512 * 1024) {
		fprintf(stderr, "%s: such tiny containers are not supported\n",
			progname);
		exit(EXIT_FAILURE);
	}
	if (param->tier2_blkcnt && param->tier2_blkcnt * param->blocksize < 512 * 1024) {
		/* TODO: is this really a problem for tier 2? */
		fprintf(stderr, "%s: tier 2 is too small\n", progname);
		exit(1);
	}

	/* Every volume must have a label; use the same default as Apple */
	if (!param->label || !*param->label)
		param->label = "untitled";

	/* Make sure the volume label fits, along with its null termination */
	if (strlen(param->label) + 1 > APFS_VOLNAME_LEN) {
		fprintf(stderr, "%s: volume label is too long\n", progname);
		exit(EXIT_FAILURE);
	}

	if (!param->main_uuid)
		param->main_uuid = get_random_uuid();
	if (!param->vol_uuid)
		param->vol_uuid = get_random_uuid();
	if (fd_tier2 != -1)
		param->fusion_uuid = get_random_uuid();
}

int main(int argc, char *argv[])
{
	char *filename;

	progname = argv[0];
	param = calloc(1, sizeof(*param));
	if (!param)
		system_error();

	while (1) {
		int opt = getopt(argc, argv, "L:U:u:szvF:");

		if (opt == -1)
			break;

		switch (opt) {
		case 'L':
			param->label = optarg;
			break;
		case 'U':
			param->main_uuid = optarg;
			break;
		case 'u':
			param->vol_uuid = optarg;
			break;
		case 's':
			param->case_sensitive = true;
			break;
		case 'z':
			param->norm_sensitive = true;
			break;
		case 'v':
			version();
		case 'F':
			fd_tier2 = open(optarg, O_RDWR);
			if (fd_tier2 == -1)
				system_error();
			break;
		default:
			usage();
		}
	}

	if (optind == argc - 2) {
		filename = argv[optind];
		/* TODO: reject malformed numbers? */
		param->block_count = atoll(argv[optind + 1]);
	} else if (optind == argc - 1) {
		filename = argv[optind];
	} else {
		usage();
	}

	if (param->block_count && fd_tier2 != -1)
		fatal("block count can't be specified for a fusion drive");

	fd_main = open(filename, O_RDWR);
	if (fd_main == -1)
		system_error();
	complete_parameters();

	make_container();
	return 0;
}
