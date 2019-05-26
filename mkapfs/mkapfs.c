/*
 *  apfsprogs/mkapfs/mkapfs.c
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "mkapfs.h"

int fd;
static char *progname;

/**
 * usage - Print usage information and exit
 */
static void usage(void)
{
	fprintf(stderr, "usage: %s [-v] device\n", progname);
	exit(1);
}

/**
 * version - Print version information and exit
 */
static void version(void)
{
	printf("mkapfs version 0.1\n");
	exit(1);
}

/**
 * system_error - Print a system error message and exit
 */
__attribute__((noreturn)) void system_error(void)
{
	perror(progname);
	exit(1);
}

int main(int argc, char *argv[])
{
	char *filename;

	progname = argv[0];
	while (1) {
		int opt = getopt(argc, argv, "v");

		if (opt == -1)
			break;

		switch (opt) {
		case 'v':
			version();
		default:
			usage();
		}
	}

	if (optind != argc - 1)
		usage();
	filename = argv[optind];

	fd = open(filename, O_RDONLY);
	if (fd == -1)
		system_error();

	/* For now, do nothing */
	return 0;
}
