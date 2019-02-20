/*
 *  apfsprogs/apfsck/types.h
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 *
 * Definitions that make it easier to reuse code from the kernel module.
 */

#ifndef _TYPES_H
#define _TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EAGAIN	1
#define ENODATA	2

#define __packed	__attribute__((packed))

#ifdef __CHECKER__
#define __bitwise	__attribute__((bitwise))
#define __force		__attribute__((force))
#else /* __CHECKER */
#define __bitwise
#define __force
#endif /* __CHECKER__ */

#define likely(x)	__builtin_expect(!!(x), 1)

typedef uint8_t		u8;
typedef uint16_t	u16;
typedef uint32_t	u32;
typedef uint64_t	u64;

/* We only support little-endian, at least for now */
typedef uint16_t __bitwise __le16;
typedef uint32_t __bitwise __le32;
typedef uint64_t __bitwise __le64;

#define cpu_to_le16(x)	((__force __le16)(u16)(x))
#define le16_to_cpu(x)	((__force u16)(__le16)(x))
#define cpu_to_le32(x)	((__force __le32)(u32)(x))
#define le32_to_cpu(x)	((__force u32)(__le32)(x))
#define cpu_to_le64(x)	((__force __le64)(u64)(x))
#define le64_to_cpu(x)	((__force u64)(__le64)(x))

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define __ROUND_MASK(x, y) ((__typeof__(x))((y)-1))
#define ROUND_UP(x, y) ((((x)-1) | __ROUND_MASK(x, y))+1)

typedef u32 unicode_t;

#endif	/* _TYPES_H */
