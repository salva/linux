/*
 *  linux/include/linux/amnfs_fs.h
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/include/linux/minix_fs.h
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#ifndef _LINUX_AMNFS_FS_H
#define _LINUX_AMNFS_FS_H

#include <linux/types.h>
#include <linux/magic.h>

#define AMNFS_NAME_LEN 255

/*
 * Maximal count of links to a file
 */
#define AMNFS_LINK_MAX		32000

#define AMNFS_SB_MAGIC_OFFSET	0x38
#define AMNFS_SB_BLOCKS_OFFSET	0x04
#define AMNFS_SB_BSIZE_OFFSET	0x18

static inline u64 amnfs_image_size(void *amnfs_sb)
{
	__u8 *p = amnfs_sb;
	if (*(__le16 *)(p + AMNFS_SB_MAGIC_OFFSET) != cpu_to_le16(AMNFS_SUPER_MAGIC))
		return 0;
	return (u64)le32_to_cpup((__le32 *)(p + AMNFS_SB_BLOCKS_OFFSET)) <<
		le32_to_cpup((__le32 *)(p + AMNFS_SB_BSIZE_OFFSET));
}

#endif	/* _LINUX_AMNFS_FS_H */
