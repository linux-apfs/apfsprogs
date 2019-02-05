/*
 *  apfsprogs/apfsck/apfsck.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include "apfsck.h"
#include "super.h"

int fd;

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
 * report - Report the issue discovered and exit
 * @context: structure where corruption was found (can be NULL)
 * @message: format string with a short explanation
 */
__attribute__((noreturn, format(printf, 2, 3)))	void report(const char *context,
							    const char *message,
							    ...)
{
	char buf[128];
	va_list args;

	va_start(args, message);
	vsnprintf(buf, sizeof(buf), message, args);
	va_end(args);

	if (context)
		printf("%s: %s\n", context, buf);
	else
		printf("%s\n", buf);

	exit(1);
}

/**
 * parse_filesystem - Parse the filesystem looking for corruption
 */
static void parse_filesystem(void)
{
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
