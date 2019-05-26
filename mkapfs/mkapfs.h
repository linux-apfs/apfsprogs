/*
 *  apfsprogs/mkapfs/mkapfs.h
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _MKAPFS_H
#define _MKAPFS_H

/* Declarations for global variables */
extern int fd;				/* File descriptor for the device */

extern __attribute__((noreturn)) void system_error(void);

#endif	/* _MKAPFS_H */
