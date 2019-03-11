/*
 *  apfsprogs/apfsck/apfsck.c
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
#include "apfsck.h"
#include "super.h"

int fd;
unsigned int options;

/**
 * usage - Print usage information and exit
 * @path: path to this program
 */
static void usage(char *path)
{
	fprintf(stderr, "usage: %s [-cuw] device\n", path);
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
 * report_crash - Report that a crash was discovered and exit
 * @context: structure with signs of a crash
 *
 * Does nothing unless the -c cli option was used.
 */
void report_crash(const char *context)
{
	if (options & OPT_REPORT_CRASH)
		report(context, "the filesystem was not unmounted cleanly.");
}

/**
 * report_unknown - Report the presence of unknown features and exit
 * @feature: the unsupported feature
 *
 * Does nothing unless the -u cli option was used.
 */
void report_unknown(const char *feature)
{
	if (options & OPT_REPORT_UNKNOWN)
		report(feature, "not supported.");
}

/**
 * report_weird - Report unexplained inconsistencies and exit
 * @context: structure where the inconsistency was found
 *
 * Does nothing unless the -w cli option was used.  This function should
 * be called when the specification, and common sense, appear to be in
 * contradiction with the behaviour of actual filesystems.
 */
void report_weird(const char *context)
{
	if (options & OPT_REPORT_WEIRD)
		report(context, "odd inconsistency (may not be corruption).");
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

	while (1) {
		int opt = getopt(argc, argv, "cuw");

		if (opt == -1)
			break;

		switch (opt) {
		case 'c':
			options |= OPT_REPORT_CRASH;
			break;
		case 'u':
			options |= OPT_REPORT_UNKNOWN;
			break;
		case 'w':
			options |= OPT_REPORT_WEIRD;
			break;
		default:
			usage(argv[0]);
		}
	}

	if (optind >= argc)
		usage(argv[0]);
	filename = argv[optind];

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		perror(NULL);
		exit(1);
	}

	parse_filesystem();
	return 0;
}
