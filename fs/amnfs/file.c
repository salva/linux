/*
 *  linux/fs/amnfs/file.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  amnfs fs regular file handling primitives
 *
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 * 	(jj@sunsite.ms.mff.cuni.cz)
 */

#include <linux/time.h>
#include <linux/pagemap.h>
#include <linux/dax.h>
#include <linux/quotaops.h>
#include "amnfs.h"
#include "xattr.h"
#include "acl.h"

#ifdef CONFIG_FS_DAX
static int amnfs_dax_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	return dax_fault(vma, vmf, amnfs_get_block, NULL);
}

static int amnfs_dax_pmd_fault(struct vm_area_struct *vma, unsigned long addr,
						pmd_t *pmd, unsigned int flags)
{
	return dax_pmd_fault(vma, addr, pmd, flags, amnfs_get_block, NULL);
}

static int amnfs_dax_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	return dax_mkwrite(vma, vmf, amnfs_get_block, NULL);
}

static const struct vm_operations_struct amnfs_dax_vm_ops = {
	.fault		= amnfs_dax_fault,
	.pmd_fault	= amnfs_dax_pmd_fault,
	.page_mkwrite	= amnfs_dax_mkwrite,
	.pfn_mkwrite	= dax_pfn_mkwrite,
};

static int amnfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (!IS_DAX(file_inode(file)))
		return generic_file_mmap(file, vma);

	file_accessed(file);
	vma->vm_ops = &amnfs_dax_vm_ops;
	vma->vm_flags |= VM_MIXEDMAP | VM_HUGEPAGE;
	return 0;
}
#else
#define amnfs_file_mmap	generic_file_mmap
#endif

/*
 * Called when filp is released. This happens when all file descriptors
 * for a single struct file are closed. Note that different open() calls
 * for the same file yield different struct file structures.
 */
static int amnfs_release_file (struct inode * inode, struct file * filp)
{
	if (filp->f_mode & FMODE_WRITE) {
		mutex_lock(&AMNFS_I(inode)->truncate_mutex);
		amnfs_discard_reservation(inode);
		mutex_unlock(&AMNFS_I(inode)->truncate_mutex);
	}
	return 0;
}

int amnfs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	int ret;
	struct super_block *sb = file->f_mapping->host->i_sb;
	struct address_space *mapping = sb->s_bdev->bd_inode->i_mapping;

	ret = generic_file_fsync(file, start, end, datasync);
	if (ret == -EIO || test_and_clear_bit(AS_EIO, &mapping->flags)) {
		/* We don't really know where the IO error happened... */
		amnfs_error(sb, __func__,
			   "detected IO error when writing metadata buffers");
		ret = -EIO;
	}
	return ret;
}

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the amnfs filesystem.
 */
const struct file_operations amnfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.unlocked_ioctl = amnfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= amnfs_compat_ioctl,
#endif
	.mmap		= amnfs_file_mmap,
	.open		= dquot_file_open,
	.release	= amnfs_release_file,
	.fsync		= amnfs_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
};

const struct inode_operations amnfs_file_inode_operations = {
#ifdef CONFIG_AMNFS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= amnfs_listxattr,
	.removexattr	= generic_removexattr,
#endif
	.setattr	= amnfs_setattr,
	.get_acl	= amnfs_get_acl,
	.set_acl	= amnfs_set_acl,
	.fiemap		= amnfs_fiemap,
};
