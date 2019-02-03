/*
 *  apfsprogs/apfsck/object.c
 *
 * Author: Gabriel Krisman Bertazi <krisman@collabora.co.uk>
 *
 * Checksum routines for an APFS object
 */

#include "apfsck.h"
#include "object.h"
#include "super.h"

static u64 fletcher64(void *addr, unsigned long len)
{
	__le32 *buff = addr;
	u64 sum1 = 0;
	u64 sum2 = 0;
	u64 c1, c2;
	int i;

	for (i = 0; i < len/sizeof(u32); i++) {
		sum1 += le32_to_cpu(buff[i]);
		sum2 += sum1;
	}

	c1 = sum1 + sum2;
	c1 = 0xFFFFFFFF - c1 % 0xFFFFFFFF;
	c2 = sum1 + c1;
	c2 = 0xFFFFFFFF - c2 % 0xFFFFFFFF;

	return (c2 << 32) | c1;
}

int obj_verify_csum(struct apfs_obj_phys *obj)
{
	return  (le64_to_cpu(obj->o_cksum) ==
		 fletcher64((char *) obj + APFS_MAX_CKSUM_SIZE,
			    sb->s_blocksize - APFS_MAX_CKSUM_SIZE));
}
