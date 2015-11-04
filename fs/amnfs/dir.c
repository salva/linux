/*
 *  linux/fs/amnfs/dir.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/dir.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  amnfs directory handling functions
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *
 * All code that works with directory layout had been switched to pagecache
 * and moved here. AV
 */

#include "amnfs.h"
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/swap.h>

typedef struct amnfs_dir_entry_2 amnfs_dirent;

/*
 * Tests against MAX_REC_LEN etc were put in place for 64k block
 * sizes; if that is not possible on this arch, we can skip
 * those tests and speed things up.
 */
static inline unsigned amnfs_rec_len_from_disk(__le16 dlen)
{
	unsigned len = le16_to_cpu(dlen);

#if (PAGE_CACHE_SIZE >= 65536)
	if (len == AMNFS_MAX_REC_LEN)
		return 1 << 16;
#endif
	return len;
}

static inline __le16 amnfs_rec_len_to_disk(unsigned len)
{
#if (PAGE_CACHE_SIZE >= 65536)
	if (len == (1 << 16))
		return cpu_to_le16(AMNFS_MAX_REC_LEN);
	else
		BUG_ON(len > (1 << 16));
#endif
	return cpu_to_le16(len);
}

/*
 * amnfs uses block-sized chunks. Arguably, sector-sized ones would be
 * more robust, but we have what we have
 */
static inline unsigned amnfs_chunk_size(struct inode *inode)
{
	return inode->i_sb->s_blocksize;
}

static inline void amnfs_put_page(struct page *page)
{
	kunmap(page);
	page_cache_release(page);
}

/*
 * Return the offset into page `page_nr' of the last valid
 * byte in that page, plus one.
 */
static unsigned
amnfs_last_byte(struct inode *inode, unsigned long page_nr)
{
	unsigned last_byte = inode->i_size;

	last_byte -= page_nr << PAGE_CACHE_SHIFT;
	if (last_byte > PAGE_CACHE_SIZE)
		last_byte = PAGE_CACHE_SIZE;
	return last_byte;
}

static int amnfs_commit_chunk(struct page *page, loff_t pos, unsigned len)
{
	struct address_space *mapping = page->mapping;
	struct inode *dir = mapping->host;
	int err = 0;

	dir->i_version++;
	block_write_end(NULL, mapping, pos, len, len, page, NULL);

	if (pos+len > dir->i_size) {
		i_size_write(dir, pos+len);
		mark_inode_dirty(dir);
	}

	if (IS_DIRSYNC(dir)) {
		err = write_one_page(page, 1);
		if (!err)
			err = sync_inode_metadata(dir, 1);
	} else {
		unlock_page(page);
	}

	return err;
}

static void amnfs_check_page(struct page *page, int quiet)
{
	struct inode *dir = page->mapping->host;
	struct super_block *sb = dir->i_sb;
	unsigned chunk_size = amnfs_chunk_size(dir);
	char *kaddr = page_address(page);
	u32 max_inumber = le32_to_cpu(AMNFS_SB(sb)->s_es->s_inodes_count);
	unsigned offs, rec_len;
	unsigned limit = PAGE_CACHE_SIZE;
	amnfs_dirent *p;
	char *error;

	if ((dir->i_size >> PAGE_CACHE_SHIFT) == page->index) {
		limit = dir->i_size & ~PAGE_CACHE_MASK;
		if (limit & (chunk_size - 1))
			goto Ebadsize;
		if (!limit)
			goto out;
	}
	for (offs = 0; offs <= limit - AMNFS_DIR_REC_LEN(1); offs += rec_len) {
		p = (amnfs_dirent *)(kaddr + offs);
		rec_len = amnfs_rec_len_from_disk(p->rec_len);

		if (unlikely(rec_len < AMNFS_DIR_REC_LEN(1)))
			goto Eshort;
		if (unlikely(rec_len & 3))
			goto Ealign;
		if (unlikely(rec_len < AMNFS_DIR_REC_LEN(p->name_len)))
			goto Enamelen;
		if (unlikely(((offs + rec_len - 1) ^ offs) & ~(chunk_size-1)))
			goto Espan;
		if (unlikely(le32_to_cpu(p->inode) > max_inumber))
			goto Einumber;
	}
	if (offs != limit)
		goto Eend;
out:
	SetPageChecked(page);
	return;

	/* Too bad, we had an error */

Ebadsize:
	if (!quiet)
		amnfs_error(sb, __func__,
			"size of directory #%lu is not a multiple "
			"of chunk size", dir->i_ino);
	goto fail;
Eshort:
	error = "rec_len is smaller than minimal";
	goto bad_entry;
Ealign:
	error = "unaligned directory entry";
	goto bad_entry;
Enamelen:
	error = "rec_len is too small for name_len";
	goto bad_entry;
Espan:
	error = "directory entry across blocks";
	goto bad_entry;
Einumber:
	error = "inode out of bounds";
bad_entry:
	if (!quiet)
		amnfs_error(sb, __func__, "bad entry in directory #%lu: : %s - "
			"offset=%lu, inode=%lu, rec_len=%d, name_len=%d",
			dir->i_ino, error, (page->index<<PAGE_CACHE_SHIFT)+offs,
			(unsigned long) le32_to_cpu(p->inode),
			rec_len, p->name_len);
	goto fail;
Eend:
	if (!quiet) {
		p = (amnfs_dirent *)(kaddr + offs);
		amnfs_error(sb, "amnfs_check_page",
			"entry in directory #%lu spans the page boundary"
			"offset=%lu, inode=%lu",
			dir->i_ino, (page->index<<PAGE_CACHE_SHIFT)+offs,
			(unsigned long) le32_to_cpu(p->inode));
	}
fail:
	SetPageChecked(page);
	SetPageError(page);
}

static struct page * amnfs_get_page(struct inode *dir, unsigned long n,
				   int quiet)
{
	struct address_space *mapping = dir->i_mapping;
	struct page *page = read_mapping_page(mapping, n, NULL);
	if (!IS_ERR(page)) {
		kmap(page);
		if (!PageChecked(page))
			amnfs_check_page(page, quiet);
		if (PageError(page))
			goto fail;
	}
	return page;

fail:
	amnfs_put_page(page);
	return ERR_PTR(-EIO);
}

/*
 * NOTE! unlike strncmp, amnfs_match returns 1 for success, 0 for failure.
 *
 * len <= AMNFS_NAME_LEN and de != NULL are guaranteed by caller.
 */
static inline int amnfs_match (int len, const char * const name,
					struct amnfs_dir_entry_2 * de)
{
	if (len != de->name_len)
		return 0;
	if (!de->inode)
		return 0;
	return !memcmp(name, de->name, len);
}

/*
 * p is at least 6 bytes before the end of page
 */
static inline amnfs_dirent *amnfs_next_entry(amnfs_dirent *p)
{
	return (amnfs_dirent *)((char *)p +
			amnfs_rec_len_from_disk(p->rec_len));
}

static inline unsigned 
amnfs_validate_entry(char *base, unsigned offset, unsigned mask)
{
	amnfs_dirent *de = (amnfs_dirent*)(base + offset);
	amnfs_dirent *p = (amnfs_dirent*)(base + (offset&mask));
	while ((char*)p < (char*)de) {
		if (p->rec_len == 0)
			break;
		p = amnfs_next_entry(p);
	}
	return (char *)p - base;
}

static unsigned char amnfs_filetype_table[AMNFS_FT_MAX] = {
	[AMNFS_FT_UNKNOWN]	= DT_UNKNOWN,
	[AMNFS_FT_REG_FILE]	= DT_REG,
	[AMNFS_FT_DIR]		= DT_DIR,
	[AMNFS_FT_CHRDEV]	= DT_CHR,
	[AMNFS_FT_BLKDEV]	= DT_BLK,
	[AMNFS_FT_FIFO]		= DT_FIFO,
	[AMNFS_FT_SOCK]		= DT_SOCK,
	[AMNFS_FT_SYMLINK]	= DT_LNK,
};

#define S_SHIFT 12
static unsigned char amnfs_type_by_mode[S_IFMT >> S_SHIFT] = {
	[S_IFREG >> S_SHIFT]	= AMNFS_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]	= AMNFS_FT_DIR,
	[S_IFCHR >> S_SHIFT]	= AMNFS_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]	= AMNFS_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]	= AMNFS_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]	= AMNFS_FT_SOCK,
	[S_IFLNK >> S_SHIFT]	= AMNFS_FT_SYMLINK,
};

static inline void amnfs_set_de_type(amnfs_dirent *de, struct inode *inode)
{
	umode_t mode = inode->i_mode;
	if (AMNFS_HAS_INCOMPAT_FEATURE(inode->i_sb, AMNFS_FEATURE_INCOMPAT_FILETYPE))
		de->file_type = amnfs_type_by_mode[(mode & S_IFMT)>>S_SHIFT];
	else
		de->file_type = 0;
}

static int
amnfs_readdir(struct file *file, struct dir_context *ctx)
{
	loff_t pos = ctx->pos;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	unsigned int offset = pos & ~PAGE_CACHE_MASK;
	unsigned long n = pos >> PAGE_CACHE_SHIFT;
	unsigned long npages = dir_pages(inode);
	unsigned chunk_mask = ~(amnfs_chunk_size(inode)-1);
	unsigned char *types = NULL;
	int need_revalidate = file->f_version != inode->i_version;

	if (pos > inode->i_size - AMNFS_DIR_REC_LEN(1))
		return 0;

	if (AMNFS_HAS_INCOMPAT_FEATURE(sb, AMNFS_FEATURE_INCOMPAT_FILETYPE))
		types = amnfs_filetype_table;

	for ( ; n < npages; n++, offset = 0) {
		char *kaddr, *limit;
		amnfs_dirent *de;
		struct page *page = amnfs_get_page(inode, n, 0);

		if (IS_ERR(page)) {
			amnfs_error(sb, __func__,
				   "bad page in #%lu",
				   inode->i_ino);
			ctx->pos += PAGE_CACHE_SIZE - offset;
			return PTR_ERR(page);
		}
		kaddr = page_address(page);
		if (unlikely(need_revalidate)) {
			if (offset) {
				offset = amnfs_validate_entry(kaddr, offset, chunk_mask);
				ctx->pos = (n<<PAGE_CACHE_SHIFT) + offset;
			}
			file->f_version = inode->i_version;
			need_revalidate = 0;
		}
		de = (amnfs_dirent *)(kaddr+offset);
		limit = kaddr + amnfs_last_byte(inode, n) - AMNFS_DIR_REC_LEN(1);
		for ( ;(char*)de <= limit; de = amnfs_next_entry(de)) {
			if (de->rec_len == 0) {
				amnfs_error(sb, __func__,
					"zero-length directory entry");
				amnfs_put_page(page);
				return -EIO;
			}
			if (de->inode) {
				unsigned char d_type = DT_UNKNOWN;

				if (types && de->file_type < AMNFS_FT_MAX)
					d_type = types[de->file_type];

				if (!dir_emit(ctx, de->name, de->name_len,
						le32_to_cpu(de->inode),
						d_type)) {
					amnfs_put_page(page);
					return 0;
				}
			}
			ctx->pos += amnfs_rec_len_from_disk(de->rec_len);
		}
		amnfs_put_page(page);
	}
	return 0;
}

/*
 *	amnfs_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the page in which the entry was found (as a parameter - res_page),
 * and the entry itself. Page is returned mapped and unlocked.
 * Entry is guaranteed to be valid.
 */
struct amnfs_dir_entry_2 *amnfs_find_entry (struct inode * dir,
			struct qstr *child, struct page ** res_page)
{
	const char *name = child->name;
	int namelen = child->len;
	unsigned reclen = AMNFS_DIR_REC_LEN(namelen);
	unsigned long start, n;
	unsigned long npages = dir_pages(dir);
	struct page *page = NULL;
	struct amnfs_inode_info *ei = AMNFS_I(dir);
	amnfs_dirent * de;
	int dir_has_error = 0;

	if (npages == 0)
		goto out;

	/* OFFSET_CACHE */
	*res_page = NULL;

	start = ei->i_dir_start_lookup;
	if (start >= npages)
		start = 0;
	n = start;
	do {
		char *kaddr;
		page = amnfs_get_page(dir, n, dir_has_error);
		if (!IS_ERR(page)) {
			kaddr = page_address(page);
			de = (amnfs_dirent *) kaddr;
			kaddr += amnfs_last_byte(dir, n) - reclen;
			while ((char *) de <= kaddr) {
				if (de->rec_len == 0) {
					amnfs_error(dir->i_sb, __func__,
						"zero-length directory entry");
					amnfs_put_page(page);
					goto out;
				}
				if (amnfs_match (namelen, name, de))
					goto found;
				de = amnfs_next_entry(de);
			}
			amnfs_put_page(page);
		} else
			dir_has_error = 1;

		if (++n >= npages)
			n = 0;
		/* next page is past the blocks we've got */
		if (unlikely(n > (dir->i_blocks >> (PAGE_CACHE_SHIFT - 9)))) {
			amnfs_error(dir->i_sb, __func__,
				"dir %lu size %lld exceeds block count %llu",
				dir->i_ino, dir->i_size,
				(unsigned long long)dir->i_blocks);
			goto out;
		}
	} while (n != start);
out:
	return NULL;

found:
	*res_page = page;
	ei->i_dir_start_lookup = n;
	return de;
}

struct amnfs_dir_entry_2 * amnfs_dotdot (struct inode *dir, struct page **p)
{
	struct page *page = amnfs_get_page(dir, 0, 0);
	amnfs_dirent *de = NULL;

	if (!IS_ERR(page)) {
		de = amnfs_next_entry((amnfs_dirent *) page_address(page));
		*p = page;
	}
	return de;
}

ino_t amnfs_inode_by_name(struct inode *dir, struct qstr *child)
{
	ino_t res = 0;
	struct amnfs_dir_entry_2 *de;
	struct page *page;
	
	de = amnfs_find_entry (dir, child, &page);
	if (de) {
		res = le32_to_cpu(de->inode);
		amnfs_put_page(page);
	}
	return res;
}

static int amnfs_prepare_chunk(struct page *page, loff_t pos, unsigned len)
{
	return __block_write_begin(page, pos, len, amnfs_get_block);
}

/* Releases the page */
void amnfs_set_link(struct inode *dir, struct amnfs_dir_entry_2 *de,
		   struct page *page, struct inode *inode, int update_times)
{
	loff_t pos = page_offset(page) +
			(char *) de - (char *) page_address(page);
	unsigned len = amnfs_rec_len_from_disk(de->rec_len);
	int err;

	lock_page(page);
	err = amnfs_prepare_chunk(page, pos, len);
	BUG_ON(err);
	de->inode = cpu_to_le32(inode->i_ino);
	amnfs_set_de_type(de, inode);
	err = amnfs_commit_chunk(page, pos, len);
	amnfs_put_page(page);
	if (update_times)
		dir->i_mtime = dir->i_ctime = CURRENT_TIME_SEC;
	AMNFS_I(dir)->i_flags &= ~AMNFS_BTREE_FL;
	mark_inode_dirty(dir);
}

/*
 *	Parent is locked.
 */
int amnfs_add_link (struct dentry *dentry, struct inode *inode)
{
	struct inode *dir = d_inode(dentry->d_parent);
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	unsigned chunk_size = amnfs_chunk_size(dir);
	unsigned reclen = AMNFS_DIR_REC_LEN(namelen);
	unsigned short rec_len, name_len;
	struct page *page = NULL;
	amnfs_dirent * de;
	unsigned long npages = dir_pages(dir);
	unsigned long n;
	char *kaddr;
	loff_t pos;
	int err;

	/*
	 * We take care of directory expansion in the same loop.
	 * This code plays outside i_size, so it locks the page
	 * to protect that region.
	 */
	for (n = 0; n <= npages; n++) {
		char *dir_end;

		page = amnfs_get_page(dir, n, 0);
		err = PTR_ERR(page);
		if (IS_ERR(page))
			goto out;
		lock_page(page);
		kaddr = page_address(page);
		dir_end = kaddr + amnfs_last_byte(dir, n);
		de = (amnfs_dirent *)kaddr;
		kaddr += PAGE_CACHE_SIZE - reclen;
		while ((char *)de <= kaddr) {
			if ((char *)de == dir_end) {
				/* We hit i_size */
				name_len = 0;
				rec_len = chunk_size;
				de->rec_len = amnfs_rec_len_to_disk(chunk_size);
				de->inode = 0;
				goto got_it;
			}
			if (de->rec_len == 0) {
				amnfs_error(dir->i_sb, __func__,
					"zero-length directory entry");
				err = -EIO;
				goto out_unlock;
			}
			err = -EEXIST;
			if (amnfs_match (namelen, name, de))
				goto out_unlock;
			name_len = AMNFS_DIR_REC_LEN(de->name_len);
			rec_len = amnfs_rec_len_from_disk(de->rec_len);
			if (!de->inode && rec_len >= reclen)
				goto got_it;
			if (rec_len >= name_len + reclen)
				goto got_it;
			de = (amnfs_dirent *) ((char *) de + rec_len);
		}
		unlock_page(page);
		amnfs_put_page(page);
	}
	BUG();
	return -EINVAL;

got_it:
	pos = page_offset(page) +
		(char*)de - (char*)page_address(page);
	err = amnfs_prepare_chunk(page, pos, rec_len);
	if (err)
		goto out_unlock;
	if (de->inode) {
		amnfs_dirent *de1 = (amnfs_dirent *) ((char *) de + name_len);
		de1->rec_len = amnfs_rec_len_to_disk(rec_len - name_len);
		de->rec_len = amnfs_rec_len_to_disk(name_len);
		de = de1;
	}
	de->name_len = namelen;
	memcpy(de->name, name, namelen);
	de->inode = cpu_to_le32(inode->i_ino);
	amnfs_set_de_type (de, inode);
	err = amnfs_commit_chunk(page, pos, rec_len);
	dir->i_mtime = dir->i_ctime = CURRENT_TIME_SEC;
	AMNFS_I(dir)->i_flags &= ~AMNFS_BTREE_FL;
	mark_inode_dirty(dir);
	/* OFFSET_CACHE */
out_put:
	amnfs_put_page(page);
out:
	return err;
out_unlock:
	unlock_page(page);
	goto out_put;
}

/*
 * amnfs_delete_entry deletes a directory entry by merging it with the
 * previous entry. Page is up-to-date. Releases the page.
 */
int amnfs_delete_entry (struct amnfs_dir_entry_2 * dir, struct page * page )
{
	struct inode *inode = page->mapping->host;
	char *kaddr = page_address(page);
	unsigned from = ((char*)dir - kaddr) & ~(amnfs_chunk_size(inode)-1);
	unsigned to = ((char *)dir - kaddr) +
				amnfs_rec_len_from_disk(dir->rec_len);
	loff_t pos;
	amnfs_dirent * pde = NULL;
	amnfs_dirent * de = (amnfs_dirent *) (kaddr + from);
	int err;

	while ((char*)de < (char*)dir) {
		if (de->rec_len == 0) {
			amnfs_error(inode->i_sb, __func__,
				"zero-length directory entry");
			err = -EIO;
			goto out;
		}
		pde = de;
		de = amnfs_next_entry(de);
	}
	if (pde)
		from = (char*)pde - (char*)page_address(page);
	pos = page_offset(page) + from;
	lock_page(page);
	err = amnfs_prepare_chunk(page, pos, to - from);
	BUG_ON(err);
	if (pde)
		pde->rec_len = amnfs_rec_len_to_disk(to - from);
	dir->inode = 0;
	err = amnfs_commit_chunk(page, pos, to - from);
	inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
	AMNFS_I(inode)->i_flags &= ~AMNFS_BTREE_FL;
	mark_inode_dirty(inode);
out:
	amnfs_put_page(page);
	return err;
}

/*
 * Set the first fragment of directory.
 */
int amnfs_make_empty(struct inode *inode, struct inode *parent)
{
	struct page *page = grab_cache_page(inode->i_mapping, 0);
	unsigned chunk_size = amnfs_chunk_size(inode);
	struct amnfs_dir_entry_2 * de;
	int err;
	void *kaddr;

	if (!page)
		return -ENOMEM;

	err = amnfs_prepare_chunk(page, 0, chunk_size);
	if (err) {
		unlock_page(page);
		goto fail;
	}
	kaddr = kmap_atomic(page);
	memset(kaddr, 0, chunk_size);
	de = (struct amnfs_dir_entry_2 *)kaddr;
	de->name_len = 1;
	de->rec_len = amnfs_rec_len_to_disk(AMNFS_DIR_REC_LEN(1));
	memcpy (de->name, ".\0\0", 4);
	de->inode = cpu_to_le32(inode->i_ino);
	amnfs_set_de_type (de, inode);

	de = (struct amnfs_dir_entry_2 *)(kaddr + AMNFS_DIR_REC_LEN(1));
	de->name_len = 2;
	de->rec_len = amnfs_rec_len_to_disk(chunk_size - AMNFS_DIR_REC_LEN(1));
	de->inode = cpu_to_le32(parent->i_ino);
	memcpy (de->name, "..\0", 4);
	amnfs_set_de_type (de, inode);
	kunmap_atomic(kaddr);
	err = amnfs_commit_chunk(page, 0, chunk_size);
fail:
	page_cache_release(page);
	return err;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
int amnfs_empty_dir (struct inode * inode)
{
	struct page *page = NULL;
	unsigned long i, npages = dir_pages(inode);
	int dir_has_error = 0;

	for (i = 0; i < npages; i++) {
		char *kaddr;
		amnfs_dirent * de;
		page = amnfs_get_page(inode, i, dir_has_error);

		if (IS_ERR(page)) {
			dir_has_error = 1;
			continue;
		}

		kaddr = page_address(page);
		de = (amnfs_dirent *)kaddr;
		kaddr += amnfs_last_byte(inode, i) - AMNFS_DIR_REC_LEN(1);

		while ((char *)de <= kaddr) {
			if (de->rec_len == 0) {
				amnfs_error(inode->i_sb, __func__,
					"zero-length directory entry");
				printk("kaddr=%p, de=%p\n", kaddr, de);
				goto not_empty;
			}
			if (de->inode != 0) {
				/* check for . and .. */
				if (de->name[0] != '.')
					goto not_empty;
				if (de->name_len > 2)
					goto not_empty;
				if (de->name_len < 2) {
					if (de->inode !=
					    cpu_to_le32(inode->i_ino))
						goto not_empty;
				} else if (de->name[1] != '.')
					goto not_empty;
			}
			de = amnfs_next_entry(de);
		}
		amnfs_put_page(page);
	}
	return 1;

not_empty:
	amnfs_put_page(page);
	return 0;
}

const struct file_operations amnfs_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate	= amnfs_readdir,
	.unlocked_ioctl = amnfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= amnfs_compat_ioctl,
#endif
	.fsync		= amnfs_fsync,
};
