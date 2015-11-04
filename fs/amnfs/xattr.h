/*
  File: linux/amnfs_xattr.h

  On-disk format of extended attributes for the amnfs filesystem.

  (C) 2001 Andreas Gruenbacher, <a.gruenbacher@computer.org>
*/

#include <linux/init.h>
#include <linux/xattr.h>

/* Magic value in attribute blocks */
#define AMNFS_XATTR_MAGIC		0xEA020000

/* Maximum number of references to one attribute block */
#define AMNFS_XATTR_REFCOUNT_MAX		1024

/* Name indexes */
#define AMNFS_XATTR_INDEX_USER			1
#define AMNFS_XATTR_INDEX_POSIX_ACL_ACCESS	2
#define AMNFS_XATTR_INDEX_POSIX_ACL_DEFAULT	3
#define AMNFS_XATTR_INDEX_TRUSTED		4
#define	AMNFS_XATTR_INDEX_LUSTRE			5
#define AMNFS_XATTR_INDEX_SECURITY	        6

struct amnfs_xattr_header {
	__le32	h_magic;	/* magic number for identification */
	__le32	h_refcount;	/* reference count */
	__le32	h_blocks;	/* number of disk blocks used */
	__le32	h_hash;		/* hash value of all attributes */
	__u32	h_reserved[4];	/* zero right now */
};

struct amnfs_xattr_entry {
	__u8	e_name_len;	/* length of name */
	__u8	e_name_index;	/* attribute name index */
	__le16	e_value_offs;	/* offset in disk block of value */
	__le32	e_value_block;	/* disk block attribute is stored on (n/i) */
	__le32	e_value_size;	/* size of attribute value */
	__le32	e_hash;		/* hash value of name and value */
	char	e_name[0];	/* attribute name */
};

#define AMNFS_XATTR_PAD_BITS		2
#define AMNFS_XATTR_PAD		(1<<AMNFS_XATTR_PAD_BITS)
#define AMNFS_XATTR_ROUND		(AMNFS_XATTR_PAD-1)
#define AMNFS_XATTR_LEN(name_len) \
	(((name_len) + AMNFS_XATTR_ROUND + \
	sizeof(struct amnfs_xattr_entry)) & ~AMNFS_XATTR_ROUND)
#define AMNFS_XATTR_NEXT(entry) \
	( (struct amnfs_xattr_entry *)( \
	  (char *)(entry) + AMNFS_XATTR_LEN((entry)->e_name_len)) )
#define AMNFS_XATTR_SIZE(size) \
	(((size) + AMNFS_XATTR_ROUND) & ~AMNFS_XATTR_ROUND)

# ifdef CONFIG_AMNFS_XATTR

extern const struct xattr_handler amnfs_xattr_user_handler;
extern const struct xattr_handler amnfs_xattr_trusted_handler;
extern const struct xattr_handler amnfs_xattr_security_handler;

extern ssize_t amnfs_listxattr(struct dentry *, char *, size_t);

extern int amnfs_xattr_get(struct inode *, int, const char *, void *, size_t);
extern int amnfs_xattr_set(struct inode *, int, const char *, const void *, size_t, int);

extern void amnfs_xattr_delete_inode(struct inode *);
extern void amnfs_xattr_put_super(struct super_block *);

extern int init_amnfs_xattr(void);
extern void exit_amnfs_xattr(void);

extern const struct xattr_handler *amnfs_xattr_handlers[];

# else  /* CONFIG_AMNFS_XATTR */

static inline int
amnfs_xattr_get(struct inode *inode, int name_index,
	       const char *name, void *buffer, size_t size)
{
	return -EOPNOTSUPP;
}

static inline int
amnfs_xattr_set(struct inode *inode, int name_index, const char *name,
	       const void *value, size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static inline void
amnfs_xattr_delete_inode(struct inode *inode)
{
}

static inline void
amnfs_xattr_put_super(struct super_block *sb)
{
}

static inline int
init_amnfs_xattr(void)
{
	return 0;
}

static inline void
exit_amnfs_xattr(void)
{
}

#define amnfs_xattr_handlers NULL

# endif  /* CONFIG_AMNFS_XATTR */

#ifdef CONFIG_AMNFS_SECURITY
extern int amnfs_init_security(struct inode *inode, struct inode *dir,
			      const struct qstr *qstr);
#else
static inline int amnfs_init_security(struct inode *inode, struct inode *dir,
				     const struct qstr *qstr)
{
	return 0;
}
#endif
