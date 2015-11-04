/*
 *  linux/fs/amnfs/symlink.c
 *
 * Only fast symlinks left here - the rest is done by generic code. AV, 1999
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/symlink.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  amnfs symlink handling code
 */

#include "amnfs.h"
#include "xattr.h"

const struct inode_operations amnfs_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= page_follow_link_light,
	.put_link	= page_put_link,
	.setattr	= amnfs_setattr,
#ifdef CONFIG_AMNFS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= amnfs_listxattr,
	.removexattr	= generic_removexattr,
#endif
};
 
const struct inode_operations amnfs_fast_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= simple_follow_link,
	.setattr	= amnfs_setattr,
#ifdef CONFIG_AMNFS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= amnfs_listxattr,
	.removexattr	= generic_removexattr,
#endif
};
