/*
  File: fs/amnfs/acl.h

  (C) 2001 Andreas Gruenbacher, <a.gruenbacher@computer.org>
*/

#include <linux/posix_acl_xattr.h>

#define AMNFS_ACL_VERSION	0x0001

typedef struct {
	__le16		e_tag;
	__le16		e_perm;
	__le32		e_id;
} amnfs_acl_entry;

typedef struct {
	__le16		e_tag;
	__le16		e_perm;
} amnfs_acl_entry_short;

typedef struct {
	__le32		a_version;
} amnfs_acl_header;

static inline size_t amnfs_acl_size(int count)
{
	if (count <= 4) {
		return sizeof(amnfs_acl_header) +
		       count * sizeof(amnfs_acl_entry_short);
	} else {
		return sizeof(amnfs_acl_header) +
		       4 * sizeof(amnfs_acl_entry_short) +
		       (count - 4) * sizeof(amnfs_acl_entry);
	}
}

static inline int amnfs_acl_count(size_t size)
{
	ssize_t s;
	size -= sizeof(amnfs_acl_header);
	s = size - 4 * sizeof(amnfs_acl_entry_short);
	if (s < 0) {
		if (size % sizeof(amnfs_acl_entry_short))
			return -1;
		return size / sizeof(amnfs_acl_entry_short);
	} else {
		if (s % sizeof(amnfs_acl_entry))
			return -1;
		return s / sizeof(amnfs_acl_entry) + 4;
	}
}

#ifdef CONFIG_AMNFS_POSIX_ACL

/* acl.c */
extern struct posix_acl *amnfs_get_acl(struct inode *inode, int type);
extern int amnfs_set_acl(struct inode *inode, struct posix_acl *acl, int type);
extern int amnfs_init_acl (struct inode *, struct inode *);

#else
#include <linux/sched.h>
#define amnfs_get_acl	NULL
#define amnfs_set_acl	NULL

static inline int amnfs_init_acl (struct inode *inode, struct inode *dir)
{
	return 0;
}
#endif

