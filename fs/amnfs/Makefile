#
# Makefile for the linux amnfs routines.
#

obj-$(CONFIG_AMNFS) += amnfs.o

amnfs-y := balloc.o dir.o file.o ialloc.o inode.o \
	  ioctl.o namei.o super.o symlink.o

amnfs-$(CONFIG_AMNFS_XATTR)	 += xattr.o xattr_user.o xattr_trusted.o
amnfs-$(CONFIG_AMNFS_POSIX_ACL) += acl.o
amnfs-$(CONFIG_AMNFS_SECURITY)	 += xattr_security.o
