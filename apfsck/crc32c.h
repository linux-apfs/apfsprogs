/*
 *  apfsprogs/apfsck/crc32c.h
 *
 * Copyright (C) 2019 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 */

#ifndef _CRC32C_H
#define _CRC32C_H

#include <apfs/types.h>

extern u32 crc32c(u32 crc, const void *buf, int size);

#endif /* _CRC32C_H */
