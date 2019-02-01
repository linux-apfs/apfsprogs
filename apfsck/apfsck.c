/*
 *  apfsprogs/apfsck/apfsck.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include "globals.h"
#include "stats.h"
#include "super.h"

int fd;
struct stats *stats;

/**
 * usage - Print usage information and exit
 * @path: path to this program
 */
static void usage(char *path)
{
	fprintf(stderr, "usage: %s device\n", path);
	exit(1);
}

/**
 * parse_filesystem - Parse the filesystem looking for corruption
 */
static void parse_filesystem(void)
{
	stats = calloc(1, sizeof(*stats));
	if (!stats) {
		perror(NULL);
		exit(1);
	}

	parse_super();
}

int main(int argc, char *argv[])
{
	char *filename;

	if (argc != 2)
		usage(argv[0]);
	filename = argv[1];

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		perror(NULL);
		exit(1);
	}

	parse_filesystem();
	return 0;
}
