/*
 * linux/fs/amnfs/xattr_user.c
 * Handler for extended user attributes.
 *
 * Copyright (C) 2001 by Andreas Gruenbacher, <a.gruenbacher@computer.org>
 */

#include <linux/init.h>
#include <linux/string.h>
#include "amnfs.h"
#include "xattr.h"

static size_t
amnfs_xattr_user_list(struct dentry *dentry, char *list, size_t list_size,
		const char *name, size_t name_len, int type)
{
	const size_t prefix_len = XATTR_USER_PREFIX_LEN;
	const size_t total_len = prefix_len + name_len + 1;

	if (!test_opt(dentry->d_sb, XATTR_USER))
		return 0;

	if (list && total_len <= list_size) {
		memcpy(list, XATTR_USER_PREFIX, prefix_len);
		memcpy(list+prefix_len, name, name_len);
		list[prefix_len + name_len] = '\0';
	}
	return total_len;
}

static int
amnfs_xattr_user_get(struct dentry *dentry, const char *name,
		void *buffer, size_t size, int type)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	if (!test_opt(dentry->d_sb, XATTR_USER))
		return -EOPNOTSUPP;
	return amnfs_xattr_get(d_inode(dentry), AMNFS_XATTR_INDEX_USER,
			      name, buffer, size);
}

static int
amnfs_xattr_user_set(struct dentry *dentry, const char *name,
		const void *value, size_t size, int flags, int type)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	if (!test_opt(dentry->d_sb, XATTR_USER))
		return -EOPNOTSUPP;

	return amnfs_xattr_set(d_inode(dentry), AMNFS_XATTR_INDEX_USER,
			      name, value, size, flags);
}

const struct xattr_handler amnfs_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.list	= amnfs_xattr_user_list,
	.get	= amnfs_xattr_user_get,
	.set	= amnfs_xattr_user_set,
};
