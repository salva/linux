/*
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
#include <linux/fs.h>
#include <linux/amnfs_fs.h>
#include <linux/blockgroup_lock.h>
#include <linux/percpu_counter.h>
#include <linux/rbtree.h>

/* XXX Here for now... not interested in restructing headers JUST now */

/* data type for block offset of block group */
typedef int amnfs_grpblk_t;

/* data type for filesystem-wide blocks number */
typedef unsigned long amnfs_fsblk_t;

#define E2FSBLK "%lu"

struct amnfs_reserve_window {
	amnfs_fsblk_t		_rsv_start;	/* First byte reserved */
	amnfs_fsblk_t		_rsv_end;	/* Last byte reserved or 0 */
};

struct amnfs_reserve_window_node {
	struct rb_node	 	rsv_node;
	__u32			rsv_goal_size;
	__u32			rsv_alloc_hit;
	struct amnfs_reserve_window	rsv_window;
};

struct amnfs_block_alloc_info {
	/* information about reservation window */
	struct amnfs_reserve_window_node	rsv_window_node;
	/*
	 * was i_next_alloc_block in amnfs_inode_info
	 * is the logical (file-relative) number of the
	 * most-recently-allocated block in this file.
	 * We use this for detecting linearly ascending allocation requests.
	 */
	__u32			last_alloc_logical_block;
	/*
	 * Was i_next_alloc_goal in amnfs_inode_info
	 * is the *physical* companion to i_next_alloc_block.
	 * it the the physical block number of the block which was most-recentl
	 * allocated to this file.  This give us the goal (target) for the next
	 * allocation when we detect linearly ascending requests.
	 */
	amnfs_fsblk_t		last_alloc_physical_block;
};

#define rsv_start rsv_window._rsv_start
#define rsv_end rsv_window._rsv_end

/*
 * second extended-fs super-block data in memory
 */
struct amnfs_sb_info {
	unsigned long s_frag_size;	/* Size of a fragment in bytes */
	unsigned long s_frags_per_block;/* Number of fragments per block */
	unsigned long s_inodes_per_block;/* Number of inodes per block */
	unsigned long s_frags_per_group;/* Number of fragments in a group */
	unsigned long s_blocks_per_group;/* Number of blocks in a group */
	unsigned long s_inodes_per_group;/* Number of inodes in a group */
	unsigned long s_itb_per_group;	/* Number of inode table blocks per group */
	unsigned long s_gdb_count;	/* Number of group descriptor blocks */
	unsigned long s_desc_per_block;	/* Number of group descriptors per block */
	unsigned long s_groups_count;	/* Number of groups in the fs */
	unsigned long s_overhead_last;  /* Last calculated overhead */
	unsigned long s_blocks_last;    /* Last seen block count */
	struct buffer_head * s_sbh;	/* Buffer containing the super block */
	struct amnfs_super_block * s_es;	/* Pointer to the super block in the buffer */
	struct buffer_head ** s_group_desc;
	unsigned long  s_mount_opt;
	unsigned long s_sb_block;
	kuid_t s_resuid;
	kgid_t s_resgid;
	unsigned short s_mount_state;
	unsigned short s_pad;
	int s_addr_per_block_bits;
	int s_desc_per_block_bits;
	int s_inode_size;
	int s_first_ino;
	spinlock_t s_next_gen_lock;
	u32 s_next_generation;
	unsigned long s_dir_count;
	u8 *s_debts;
	struct percpu_counter s_freeblocks_counter;
	struct percpu_counter s_freeinodes_counter;
	struct percpu_counter s_dirs_counter;
	struct blockgroup_lock *s_blockgroup_lock;
	/* root of the per fs reservation window tree */
	spinlock_t s_rsv_window_lock;
	struct rb_root s_rsv_window_root;
	struct amnfs_reserve_window_node s_rsv_window_head;
	/*
	 * s_lock protects against concurrent modifications of s_mount_state,
	 * s_blocks_last, s_overhead_last and the content of superblock's
	 * buffer pointed to by sbi->s_es.
	 *
	 * Note: It is used in amnfs_show_options() to provide a consistent view
	 * of the mount options.
	 */
	spinlock_t s_lock;
};

static inline spinlock_t *
sb_bgl_lock(struct amnfs_sb_info *sbi, unsigned int block_group)
{
	return bgl_lock_ptr(sbi->s_blockgroup_lock, block_group);
}

/*
 * Define AMNFS_DEBUG to produce debug messages
 */
#undef AMNFS_DEBUG

/*
 * Define AMNFS_RESERVATION to reserve data blocks for expanding files
 */
#define AMNFS_DEFAULT_RESERVE_BLOCKS     8
/*max window size: 1024(direct blocks) + 3([t,d]indirect blocks) */
#define AMNFS_MAX_RESERVE_BLOCKS         1027
#define AMNFS_RESERVE_WINDOW_NOT_ALLOCATED 0
/*
 * The second extended file system version
 */
#define AMNFS_DATE		"95/08/09"
#define AMNFS_VERSION		"0.5b"

/*
 * Debug code
 */
#ifdef AMNFS_DEBUG
#	define amnfs_debug(f, a...)	{ \
					printk ("AMNFS-fs DEBUG (%s, %d): %s:", \
						__FILE__, __LINE__, __func__); \
				  	printk (f, ## a); \
					}
#else
#	define amnfs_debug(f, a...)	/**/
#endif

/*
 * Special inode numbers
 */
#define	AMNFS_BAD_INO		 1	/* Bad blocks inode */
#define AMNFS_ROOT_INO		 2	/* Root inode */
#define AMNFS_BOOT_LOADER_INO	 5	/* Boot loader inode */
#define AMNFS_UNDEL_DIR_INO	 6	/* Undelete directory inode */

/* First non-reserved inode for old amnfs filesystems */
#define AMNFS_GOOD_OLD_FIRST_INO	11

static inline struct amnfs_sb_info *AMNFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

/*
 * Macro-instructions used to manage several block sizes
 */
#define AMNFS_MIN_BLOCK_SIZE		1024
#define	AMNFS_MAX_BLOCK_SIZE		4096
#define AMNFS_MIN_BLOCK_LOG_SIZE		  10
#define AMNFS_BLOCK_SIZE(s)		((s)->s_blocksize)
#define	AMNFS_ADDR_PER_BLOCK(s)		(AMNFS_BLOCK_SIZE(s) / sizeof (__u32))
#define AMNFS_BLOCK_SIZE_BITS(s)		((s)->s_blocksize_bits)
#define	AMNFS_ADDR_PER_BLOCK_BITS(s)	(AMNFS_SB(s)->s_addr_per_block_bits)
#define AMNFS_INODE_SIZE(s)		(AMNFS_SB(s)->s_inode_size)
#define AMNFS_FIRST_INO(s)		(AMNFS_SB(s)->s_first_ino)

/*
 * Macro-instructions used to manage fragments
 */
#define AMNFS_MIN_FRAG_SIZE		1024
#define	AMNFS_MAX_FRAG_SIZE		4096
#define AMNFS_MIN_FRAG_LOG_SIZE		  10
#define AMNFS_FRAG_SIZE(s)		(AMNFS_SB(s)->s_frag_size)
#define AMNFS_FRAGS_PER_BLOCK(s)		(AMNFS_SB(s)->s_frags_per_block)

/*
 * Structure of a blocks group descriptor
 */
struct amnfs_group_desc
{
	__le32	bg_block_bitmap;		/* Blocks bitmap block */
	__le32	bg_inode_bitmap;		/* Inodes bitmap block */
	__le32	bg_inode_table;		/* Inodes table block */
	__le16	bg_free_blocks_count;	/* Free blocks count */
	__le16	bg_free_inodes_count;	/* Free inodes count */
	__le16	bg_used_dirs_count;	/* Directories count */
	__le16	bg_pad;
	__le32	bg_reserved[3];
};

/*
 * Macro-instructions used to manage group descriptors
 */
#define AMNFS_BLOCKS_PER_GROUP(s)	(AMNFS_SB(s)->s_blocks_per_group)
#define AMNFS_DESC_PER_BLOCK(s)		(AMNFS_SB(s)->s_desc_per_block)
#define AMNFS_INODES_PER_GROUP(s)	(AMNFS_SB(s)->s_inodes_per_group)
#define AMNFS_DESC_PER_BLOCK_BITS(s)	(AMNFS_SB(s)->s_desc_per_block_bits)

/*
 * Constants relative to the data blocks
 */
#define	AMNFS_NDIR_BLOCKS		12
#define	AMNFS_IND_BLOCK			AMNFS_NDIR_BLOCKS
#define	AMNFS_DIND_BLOCK			(AMNFS_IND_BLOCK + 1)
#define	AMNFS_TIND_BLOCK			(AMNFS_DIND_BLOCK + 1)
#define	AMNFS_N_BLOCKS			(AMNFS_TIND_BLOCK + 1)

/*
 * Inode flags (GETFLAGS/SETFLAGS)
 */
#define	AMNFS_SECRM_FL			FS_SECRM_FL	/* Secure deletion */
#define	AMNFS_UNRM_FL			FS_UNRM_FL	/* Undelete */
#define	AMNFS_COMPR_FL			FS_COMPR_FL	/* Compress file */
#define AMNFS_SYNC_FL			FS_SYNC_FL	/* Synchronous updates */
#define AMNFS_IMMUTABLE_FL		FS_IMMUTABLE_FL	/* Immutable file */
#define AMNFS_APPEND_FL			FS_APPEND_FL	/* writes to file may only append */
#define AMNFS_NODUMP_FL			FS_NODUMP_FL	/* do not dump file */
#define AMNFS_NOATIME_FL			FS_NOATIME_FL	/* do not update atime */
/* Reserved for compression usage... */
#define AMNFS_DIRTY_FL			FS_DIRTY_FL
#define AMNFS_COMPRBLK_FL		FS_COMPRBLK_FL	/* One or more compressed clusters */
#define AMNFS_NOCOMP_FL			FS_NOCOMP_FL	/* Don't compress */
#define AMNFS_ECOMPR_FL			FS_ECOMPR_FL	/* Compression error */
/* End compression flags --- maybe not all used */	
#define AMNFS_BTREE_FL			FS_BTREE_FL	/* btree format dir */
#define AMNFS_INDEX_FL			FS_INDEX_FL	/* hash-indexed directory */
#define AMNFS_IMAGIC_FL			FS_IMAGIC_FL	/* AFS directory */
#define AMNFS_JOURNAL_DATA_FL		FS_JOURNAL_DATA_FL /* Reserved for ext3 */
#define AMNFS_NOTAIL_FL			FS_NOTAIL_FL	/* file tail should not be merged */
#define AMNFS_DIRSYNC_FL			FS_DIRSYNC_FL	/* dirsync behaviour (directories only) */
#define AMNFS_TOPDIR_FL			FS_TOPDIR_FL	/* Top of directory hierarchies*/
#define AMNFS_RESERVED_FL		FS_RESERVED_FL	/* reserved for amnfs lib */

#define AMNFS_FL_USER_VISIBLE		FS_FL_USER_VISIBLE	/* User visible flags */
#define AMNFS_FL_USER_MODIFIABLE		FS_FL_USER_MODIFIABLE	/* User modifiable flags */

/* Flags that should be inherited by new inodes from their parent. */
#define AMNFS_FL_INHERITED (AMNFS_SECRM_FL | AMNFS_UNRM_FL | AMNFS_COMPR_FL |\
			   AMNFS_SYNC_FL | AMNFS_NODUMP_FL |\
			   AMNFS_NOATIME_FL | AMNFS_COMPRBLK_FL |\
			   AMNFS_NOCOMP_FL | AMNFS_JOURNAL_DATA_FL |\
			   AMNFS_NOTAIL_FL | AMNFS_DIRSYNC_FL)

/* Flags that are appropriate for regular files (all but dir-specific ones). */
#define AMNFS_REG_FLMASK (~(AMNFS_DIRSYNC_FL | AMNFS_TOPDIR_FL))

/* Flags that are appropriate for non-directories/regular files. */
#define AMNFS_OTHER_FLMASK (AMNFS_NODUMP_FL | AMNFS_NOATIME_FL)

/* Mask out flags that are inappropriate for the given type of inode. */
static inline __u32 amnfs_mask_flags(umode_t mode, __u32 flags)
{
	if (S_ISDIR(mode))
		return flags;
	else if (S_ISREG(mode))
		return flags & AMNFS_REG_FLMASK;
	else
		return flags & AMNFS_OTHER_FLMASK;
}

/*
 * ioctl commands
 */
#define	AMNFS_IOC_GETFLAGS		FS_IOC_GETFLAGS
#define	AMNFS_IOC_SETFLAGS		FS_IOC_SETFLAGS
#define	AMNFS_IOC_GETVERSION		FS_IOC_GETVERSION
#define	AMNFS_IOC_SETVERSION		FS_IOC_SETVERSION
#define	AMNFS_IOC_GETRSVSZ		_IOR('f', 5, long)
#define	AMNFS_IOC_SETRSVSZ		_IOW('f', 6, long)

/*
 * ioctl commands in 32 bit emulation
 */
#define AMNFS_IOC32_GETFLAGS		FS_IOC32_GETFLAGS
#define AMNFS_IOC32_SETFLAGS		FS_IOC32_SETFLAGS
#define AMNFS_IOC32_GETVERSION		FS_IOC32_GETVERSION
#define AMNFS_IOC32_SETVERSION		FS_IOC32_SETVERSION

/*
 * Structure of an inode on the disk
 */
struct amnfs_inode {
	__le16	i_mode;		/* File mode */
	__le16	i_uid;		/* Low 16 bits of Owner Uid */
	__le32	i_size;		/* Size in bytes */
	__le32	i_atime;	/* Access time */
	__le32	i_ctime;	/* Creation time */
	__le32	i_mtime;	/* Modification time */
	__le32	i_dtime;	/* Deletion Time */
	__le16	i_gid;		/* Low 16 bits of Group Id */
	__le16	i_links_count;	/* Links count */
	__le32	i_blocks;	/* Blocks count */
	__le32	i_flags;	/* File flags */
	union {
		struct {
			__le32  l_i_reserved1;
		} linux1;
		struct {
			__le32  h_i_translator;
		} hurd1;
		struct {
			__le32  m_i_reserved1;
		} masix1;
	} osd1;				/* OS dependent 1 */
	__le32	i_block[AMNFS_N_BLOCKS];/* Pointers to blocks */
	__le32	i_generation;	/* File version (for NFS) */
	__le32	i_file_acl;	/* File ACL */
	__le32	i_dir_acl;	/* Directory ACL */
	__le32	i_faddr;	/* Fragment address */
	union {
		struct {
			__u8	l_i_frag;	/* Fragment number */
			__u8	l_i_fsize;	/* Fragment size */
			__u16	i_pad1;
			__le16	l_i_uid_high;	/* these 2 fields    */
			__le16	l_i_gid_high;	/* were reserved2[0] */
			__u32	l_i_reserved2;
		} linux2;
		struct {
			__u8	h_i_frag;	/* Fragment number */
			__u8	h_i_fsize;	/* Fragment size */
			__le16	h_i_mode_high;
			__le16	h_i_uid_high;
			__le16	h_i_gid_high;
			__le32	h_i_author;
		} hurd2;
		struct {
			__u8	m_i_frag;	/* Fragment number */
			__u8	m_i_fsize;	/* Fragment size */
			__u16	m_pad1;
			__u32	m_i_reserved2[2];
		} masix2;
	} osd2;				/* OS dependent 2 */
};

#define i_size_high	i_dir_acl

#define i_reserved1	osd1.linux1.l_i_reserved1
#define i_frag		osd2.linux2.l_i_frag
#define i_fsize		osd2.linux2.l_i_fsize
#define i_uid_low	i_uid
#define i_gid_low	i_gid
#define i_uid_high	osd2.linux2.l_i_uid_high
#define i_gid_high	osd2.linux2.l_i_gid_high
#define i_reserved2	osd2.linux2.l_i_reserved2

/*
 * File system states
 */
#define	AMNFS_VALID_FS			0x0001	/* Unmounted cleanly */
#define	AMNFS_ERROR_FS			0x0002	/* Errors detected */

/*
 * Mount flags
 */
#define AMNFS_MOUNT_CHECK		0x000001  /* Do mount-time checks */
#define AMNFS_MOUNT_OLDALLOC		0x000002  /* Don't use the new Orlov allocator */
#define AMNFS_MOUNT_GRPID		0x000004  /* Create files with directory's group */
#define AMNFS_MOUNT_DEBUG		0x000008  /* Some debugging messages */
#define AMNFS_MOUNT_ERRORS_CONT		0x000010  /* Continue on errors */
#define AMNFS_MOUNT_ERRORS_RO		0x000020  /* Remount fs ro on errors */
#define AMNFS_MOUNT_ERRORS_PANIC		0x000040  /* Panic on errors */
#define AMNFS_MOUNT_MINIX_DF		0x000080  /* Mimics the Minix statfs */
#define AMNFS_MOUNT_NOBH			0x000100  /* No buffer_heads */
#define AMNFS_MOUNT_NO_UID32		0x000200  /* Disable 32-bit UIDs */
#define AMNFS_MOUNT_XATTR_USER		0x004000  /* Extended user attributes */
#define AMNFS_MOUNT_POSIX_ACL		0x008000  /* POSIX Access Control Lists */
#define AMNFS_MOUNT_XIP			0x010000  /* Obsolete, use DAX */
#define AMNFS_MOUNT_USRQUOTA		0x020000  /* user quota */
#define AMNFS_MOUNT_GRPQUOTA		0x040000  /* group quota */
#define AMNFS_MOUNT_RESERVATION		0x080000  /* Preallocation */
#ifdef CONFIG_FS_DAX
#define AMNFS_MOUNT_DAX			0x100000  /* Direct Access */
#else
#define AMNFS_MOUNT_DAX			0
#endif


#define clear_opt(o, opt)		o &= ~AMNFS_MOUNT_##opt
#define set_opt(o, opt)			o |= AMNFS_MOUNT_##opt
#define test_opt(sb, opt)		(AMNFS_SB(sb)->s_mount_opt & \
					 AMNFS_MOUNT_##opt)
/*
 * Maximal mount counts between two filesystem checks
 */
#define AMNFS_DFL_MAX_MNT_COUNT		20	/* Allow 20 mounts */
#define AMNFS_DFL_CHECKINTERVAL		0	/* Don't use interval check */

/*
 * Behaviour when detecting errors
 */
#define AMNFS_ERRORS_CONTINUE		1	/* Continue execution */
#define AMNFS_ERRORS_RO			2	/* Remount fs read-only */
#define AMNFS_ERRORS_PANIC		3	/* Panic */
#define AMNFS_ERRORS_DEFAULT		AMNFS_ERRORS_CONTINUE

/*
 * Structure of the super block
 */
struct amnfs_super_block {
	__le32	s_inodes_count;		/* Inodes count */
	__le32	s_blocks_count;		/* Blocks count */
	__le32	s_r_blocks_count;	/* Reserved blocks count */
	__le32	s_free_blocks_count;	/* Free blocks count */
	__le32	s_free_inodes_count;	/* Free inodes count */
	__le32	s_first_data_block;	/* First Data Block */
	__le32	s_log_block_size;	/* Block size */
	__le32	s_log_frag_size;	/* Fragment size */
	__le32	s_blocks_per_group;	/* # Blocks per group */
	__le32	s_frags_per_group;	/* # Fragments per group */
	__le32	s_inodes_per_group;	/* # Inodes per group */
	__le32	s_mtime;		/* Mount time */
	__le32	s_wtime;		/* Write time */
	__le16	s_mnt_count;		/* Mount count */
	__le16	s_max_mnt_count;	/* Maximal mount count */
	__le16	s_magic;		/* Magic signature */
	__le16	s_state;		/* File system state */
	__le16	s_errors;		/* Behaviour when detecting errors */
	__le16	s_minor_rev_level; 	/* minor revision level */
	__le32	s_lastcheck;		/* time of last check */
	__le32	s_checkinterval;	/* max. time between checks */
	__le32	s_creator_os;		/* OS */
	__le32	s_rev_level;		/* Revision level */
	__le16	s_def_resuid;		/* Default uid for reserved blocks */
	__le16	s_def_resgid;		/* Default gid for reserved blocks */
	/*
	 * These fields are for AMNFS_DYNAMIC_REV superblocks only.
	 *
	 * Note: the difference between the compatible feature set and
	 * the incompatible feature set is that if there is a bit set
	 * in the incompatible feature set that the kernel doesn't
	 * know about, it should refuse to mount the filesystem.
	 * 
	 * e2fsck's requirements are more strict; if it doesn't know
	 * about a feature in either the compatible or incompatible
	 * feature set, it must abort and not try to meddle with
	 * things it doesn't understand...
	 */
	__le32	s_first_ino; 		/* First non-reserved inode */
	__le16   s_inode_size; 		/* size of inode structure */
	__le16	s_block_group_nr; 	/* block group # of this superblock */
	__le32	s_feature_compat; 	/* compatible feature set */
	__le32	s_feature_incompat; 	/* incompatible feature set */
	__le32	s_feature_ro_compat; 	/* readonly-compatible feature set */
	__u8	s_uuid[16];		/* 128-bit uuid for volume */
	char	s_volume_name[16]; 	/* volume name */
	char	s_last_mounted[64]; 	/* directory where last mounted */
	__le32	s_algorithm_usage_bitmap; /* For compression */
	/*
	 * Performance hints.  Directory preallocation should only
	 * happen if the AMNFS_COMPAT_PREALLOC flag is on.
	 */
	__u8	s_prealloc_blocks;	/* Nr of blocks to try to preallocate*/
	__u8	s_prealloc_dir_blocks;	/* Nr to preallocate for dirs */
	__u16	s_padding1;
	/*
	 * Journaling support valid if EXT3_FEATURE_COMPAT_HAS_JOURNAL set.
	 */
	__u8	s_journal_uuid[16];	/* uuid of journal superblock */
	__u32	s_journal_inum;		/* inode number of journal file */
	__u32	s_journal_dev;		/* device number of journal file */
	__u32	s_last_orphan;		/* start of list of inodes to delete */
	__u32	s_hash_seed[4];		/* HTREE hash seed */
	__u8	s_def_hash_version;	/* Default hash version to use */
	__u8	s_reserved_char_pad;
	__u16	s_reserved_word_pad;
	__le32	s_default_mount_opts;
 	__le32	s_first_meta_bg; 	/* First metablock block group */
	__u32	s_reserved[190];	/* Padding to the end of the block */
};

/*
 * Codes for operating systems
 */
#define AMNFS_OS_LINUX		0
#define AMNFS_OS_HURD		1
#define AMNFS_OS_MASIX		2
#define AMNFS_OS_FREEBSD		3
#define AMNFS_OS_LITES		4

/*
 * Revision levels
 */
#define AMNFS_GOOD_OLD_REV	0	/* The good old (original) format */
#define AMNFS_DYNAMIC_REV	1 	/* V2 format w/ dynamic inode sizes */

#define AMNFS_CURRENT_REV	AMNFS_GOOD_OLD_REV
#define AMNFS_MAX_SUPP_REV	AMNFS_DYNAMIC_REV

#define AMNFS_GOOD_OLD_INODE_SIZE 128

/*
 * Feature set definitions
 */

#define AMNFS_HAS_COMPAT_FEATURE(sb,mask)			\
	( AMNFS_SB(sb)->s_es->s_feature_compat & cpu_to_le32(mask) )
#define AMNFS_HAS_RO_COMPAT_FEATURE(sb,mask)			\
	( AMNFS_SB(sb)->s_es->s_feature_ro_compat & cpu_to_le32(mask) )
#define AMNFS_HAS_INCOMPAT_FEATURE(sb,mask)			\
	( AMNFS_SB(sb)->s_es->s_feature_incompat & cpu_to_le32(mask) )
#define AMNFS_SET_COMPAT_FEATURE(sb,mask)			\
	AMNFS_SB(sb)->s_es->s_feature_compat |= cpu_to_le32(mask)
#define AMNFS_SET_RO_COMPAT_FEATURE(sb,mask)			\
	AMNFS_SB(sb)->s_es->s_feature_ro_compat |= cpu_to_le32(mask)
#define AMNFS_SET_INCOMPAT_FEATURE(sb,mask)			\
	AMNFS_SB(sb)->s_es->s_feature_incompat |= cpu_to_le32(mask)
#define AMNFS_CLEAR_COMPAT_FEATURE(sb,mask)			\
	AMNFS_SB(sb)->s_es->s_feature_compat &= ~cpu_to_le32(mask)
#define AMNFS_CLEAR_RO_COMPAT_FEATURE(sb,mask)			\
	AMNFS_SB(sb)->s_es->s_feature_ro_compat &= ~cpu_to_le32(mask)
#define AMNFS_CLEAR_INCOMPAT_FEATURE(sb,mask)			\
	AMNFS_SB(sb)->s_es->s_feature_incompat &= ~cpu_to_le32(mask)

#define AMNFS_FEATURE_COMPAT_DIR_PREALLOC	0x0001
#define AMNFS_FEATURE_COMPAT_IMAGIC_INODES	0x0002
#define EXT3_FEATURE_COMPAT_HAS_JOURNAL		0x0004
#define AMNFS_FEATURE_COMPAT_EXT_ATTR		0x0008
#define AMNFS_FEATURE_COMPAT_RESIZE_INO		0x0010
#define AMNFS_FEATURE_COMPAT_DIR_INDEX		0x0020
#define AMNFS_FEATURE_COMPAT_ANY			0xffffffff

#define AMNFS_FEATURE_RO_COMPAT_SPARSE_SUPER	0x0001
#define AMNFS_FEATURE_RO_COMPAT_LARGE_FILE	0x0002
#define AMNFS_FEATURE_RO_COMPAT_BTREE_DIR	0x0004
#define AMNFS_FEATURE_RO_COMPAT_ANY		0xffffffff

#define AMNFS_FEATURE_INCOMPAT_COMPRESSION	0x0001
#define AMNFS_FEATURE_INCOMPAT_FILETYPE		0x0002
#define EXT3_FEATURE_INCOMPAT_RECOVER		0x0004
#define EXT3_FEATURE_INCOMPAT_JOURNAL_DEV	0x0008
#define AMNFS_FEATURE_INCOMPAT_META_BG		0x0010
#define AMNFS_FEATURE_INCOMPAT_ANY		0xffffffff

#define AMNFS_FEATURE_COMPAT_SUPP	AMNFS_FEATURE_COMPAT_EXT_ATTR
#define AMNFS_FEATURE_INCOMPAT_SUPP	(AMNFS_FEATURE_INCOMPAT_FILETYPE| \
					 AMNFS_FEATURE_INCOMPAT_META_BG)
#define AMNFS_FEATURE_RO_COMPAT_SUPP	(AMNFS_FEATURE_RO_COMPAT_SPARSE_SUPER| \
					 AMNFS_FEATURE_RO_COMPAT_LARGE_FILE| \
					 AMNFS_FEATURE_RO_COMPAT_BTREE_DIR)
#define AMNFS_FEATURE_RO_COMPAT_UNSUPPORTED	~AMNFS_FEATURE_RO_COMPAT_SUPP
#define AMNFS_FEATURE_INCOMPAT_UNSUPPORTED	~AMNFS_FEATURE_INCOMPAT_SUPP

/*
 * Default values for user and/or group using reserved blocks
 */
#define	AMNFS_DEF_RESUID		0
#define	AMNFS_DEF_RESGID		0

/*
 * Default mount options
 */
#define AMNFS_DEFM_DEBUG		0x0001
#define AMNFS_DEFM_BSDGROUPS	0x0002
#define AMNFS_DEFM_XATTR_USER	0x0004
#define AMNFS_DEFM_ACL		0x0008
#define AMNFS_DEFM_UID16		0x0010
    /* Not used by amnfs, but reserved for use by ext3 */
#define EXT3_DEFM_JMODE		0x0060 
#define EXT3_DEFM_JMODE_DATA	0x0020
#define EXT3_DEFM_JMODE_ORDERED	0x0040
#define EXT3_DEFM_JMODE_WBACK	0x0060

/*
 * Structure of a directory entry
 */

struct amnfs_dir_entry {
	__le32	inode;			/* Inode number */
	__le16	rec_len;		/* Directory entry length */
	__le16	name_len;		/* Name length */
	char	name[];			/* File name, up to AMNFS_NAME_LEN */
};

/*
 * The new version of the directory entry.  Since AMNFS structures are
 * stored in intel byte order, and the name_len field could never be
 * bigger than 255 chars, it's safe to reclaim the extra byte for the
 * file_type field.
 */
struct amnfs_dir_entry_2 {
	__le32	inode;			/* Inode number */
	__le16	rec_len;		/* Directory entry length */
	__u8	name_len;		/* Name length */
	__u8	file_type;
	char	name[];			/* File name, up to AMNFS_NAME_LEN */
};

/*
 * Ext2 directory file types.  Only the low 3 bits are used.  The
 * other bits are reserved for now.
 */
enum {
	AMNFS_FT_UNKNOWN		= 0,
	AMNFS_FT_REG_FILE	= 1,
	AMNFS_FT_DIR		= 2,
	AMNFS_FT_CHRDEV		= 3,
	AMNFS_FT_BLKDEV		= 4,
	AMNFS_FT_FIFO		= 5,
	AMNFS_FT_SOCK		= 6,
	AMNFS_FT_SYMLINK		= 7,
	AMNFS_FT_MAX
};

/*
 * AMNFS_DIR_PAD defines the directory entries boundaries
 *
 * NOTE: It must be a multiple of 4
 */
#define AMNFS_DIR_PAD		 	4
#define AMNFS_DIR_ROUND 			(AMNFS_DIR_PAD - 1)
#define AMNFS_DIR_REC_LEN(name_len)	(((name_len) + 8 + AMNFS_DIR_ROUND) & \
					 ~AMNFS_DIR_ROUND)
#define AMNFS_MAX_REC_LEN		((1<<16)-1)

static inline void verify_offsets(void)
{
#define A(x,y) BUILD_BUG_ON(x != offsetof(struct amnfs_super_block, y));
	A(AMNFS_SB_MAGIC_OFFSET, s_magic);
	A(AMNFS_SB_BLOCKS_OFFSET, s_blocks_count);
	A(AMNFS_SB_BSIZE_OFFSET, s_log_block_size);
#undef A
}

/*
 * amnfs mount options
 */
struct amnfs_mount_options {
	unsigned long s_mount_opt;
	kuid_t s_resuid;
	kgid_t s_resgid;
};

/*
 * second extended file system inode data in memory
 */
struct amnfs_inode_info {
	__le32	i_data[15];
	__u32	i_flags;
	__u32	i_faddr;
	__u8	i_frag_no;
	__u8	i_frag_size;
	__u16	i_state;
	__u32	i_file_acl;
	__u32	i_dir_acl;
	__u32	i_dtime;

	/*
	 * i_block_group is the number of the block group which contains
	 * this file's inode.  Constant across the lifetime of the inode,
	 * it is used for making block allocation decisions - we try to
	 * place a file's data blocks near its inode block, and new inodes
	 * near to their parent directory's inode.
	 */
	__u32	i_block_group;

	/* block reservation info */
	struct amnfs_block_alloc_info *i_block_alloc_info;

	__u32	i_dir_start_lookup;
#ifdef CONFIG_AMNFS_XATTR
	/*
	 * Extended attributes can be read independently of the main file
	 * data. Taking i_mutex even when reading would cause contention
	 * between readers of EAs and writers of regular file data, so
	 * instead we synchronize on xattr_sem when reading or changing
	 * EAs.
	 */
	struct rw_semaphore xattr_sem;
#endif
	rwlock_t i_meta_lock;

	/*
	 * truncate_mutex is for serialising amnfs_truncate() against
	 * amnfs_getblock().  It also protects the internals of the inode's
	 * reservation data structures: amnfs_reserve_window and
	 * amnfs_reserve_window_node.
	 */
	struct mutex truncate_mutex;
	struct inode	vfs_inode;
	struct list_head i_orphan;	/* unlinked but open inodes */
#ifdef CONFIG_QUOTA
	struct dquot *i_dquot[MAXQUOTAS];
#endif
};

/*
 * Inode dynamic state flags
 */
#define AMNFS_STATE_NEW			0x00000001 /* inode is newly created */


/*
 * Function prototypes
 */

/*
 * Ok, these declarations are also in <linux/kernel.h> but none of the
 * amnfs source programs needs to include it so they are duplicated here.
 */

static inline struct amnfs_inode_info *AMNFS_I(struct inode *inode)
{
	return container_of(inode, struct amnfs_inode_info, vfs_inode);
}

/* balloc.c */
extern int amnfs_bg_has_super(struct super_block *sb, int group);
extern unsigned long amnfs_bg_num_gdb(struct super_block *sb, int group);
extern amnfs_fsblk_t amnfs_new_block(struct inode *, unsigned long, int *);
extern amnfs_fsblk_t amnfs_new_blocks(struct inode *, unsigned long,
				unsigned long *, int *);
extern void amnfs_free_blocks (struct inode *, unsigned long,
			      unsigned long);
extern unsigned long amnfs_count_free_blocks (struct super_block *);
extern unsigned long amnfs_count_dirs (struct super_block *);
extern void amnfs_check_blocks_bitmap (struct super_block *);
extern struct amnfs_group_desc * amnfs_get_group_desc(struct super_block * sb,
						    unsigned int block_group,
						    struct buffer_head ** bh);
extern void amnfs_discard_reservation (struct inode *);
extern int amnfs_should_retry_alloc(struct super_block *sb, int *retries);
extern void amnfs_init_block_alloc_info(struct inode *);
extern void amnfs_rsv_window_add(struct super_block *sb, struct amnfs_reserve_window_node *rsv);

/* dir.c */
extern int amnfs_add_link (struct dentry *, struct inode *);
extern ino_t amnfs_inode_by_name(struct inode *, struct qstr *);
extern int amnfs_make_empty(struct inode *, struct inode *);
extern struct amnfs_dir_entry_2 * amnfs_find_entry (struct inode *,struct qstr *, struct page **);
extern int amnfs_delete_entry (struct amnfs_dir_entry_2 *, struct page *);
extern int amnfs_empty_dir (struct inode *);
extern struct amnfs_dir_entry_2 * amnfs_dotdot (struct inode *, struct page **);
extern void amnfs_set_link(struct inode *, struct amnfs_dir_entry_2 *, struct page *, struct inode *, int);

/* ialloc.c */
extern struct inode * amnfs_new_inode (struct inode *, umode_t, const struct qstr *);
extern void amnfs_free_inode (struct inode *);
extern unsigned long amnfs_count_free_inodes (struct super_block *);
extern void amnfs_check_inodes_bitmap (struct super_block *);
extern unsigned long amnfs_count_free (struct buffer_head *, unsigned);

/* inode.c */
extern struct inode *amnfs_iget (struct super_block *, unsigned long);
extern int amnfs_write_inode (struct inode *, struct writeback_control *);
extern void amnfs_evict_inode(struct inode *);
extern int amnfs_get_block(struct inode *, sector_t, struct buffer_head *, int);
extern int amnfs_setattr (struct dentry *, struct iattr *);
extern void amnfs_set_inode_flags(struct inode *inode);
extern void amnfs_get_inode_flags(struct amnfs_inode_info *);
extern int amnfs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		       u64 start, u64 len);

/* ioctl.c */
extern long amnfs_ioctl(struct file *, unsigned int, unsigned long);
extern long amnfs_compat_ioctl(struct file *, unsigned int, unsigned long);

/* namei.c */
struct dentry *amnfs_get_parent(struct dentry *child);

/* super.c */
extern __printf(3, 4)
void amnfs_error(struct super_block *, const char *, const char *, ...);
extern __printf(3, 4)
void amnfs_msg(struct super_block *, const char *, const char *, ...);
extern void amnfs_update_dynamic_rev (struct super_block *sb);
extern void amnfs_write_super (struct super_block *);

/*
 * Inodes and files operations
 */

/* dir.c */
extern const struct file_operations amnfs_dir_operations;

/* file.c */
extern int amnfs_fsync(struct file *file, loff_t start, loff_t end,
		      int datasync);
extern const struct inode_operations amnfs_file_inode_operations;
extern const struct file_operations amnfs_file_operations;

/* inode.c */
extern const struct address_space_operations amnfs_aops;
extern const struct address_space_operations amnfs_nobh_aops;

/* namei.c */
extern const struct inode_operations amnfs_dir_inode_operations;
extern const struct inode_operations amnfs_special_inode_operations;

/* symlink.c */
extern const struct inode_operations amnfs_fast_symlink_inode_operations;
extern const struct inode_operations amnfs_symlink_inode_operations;

static inline amnfs_fsblk_t
amnfs_group_first_block_no(struct super_block *sb, unsigned long group_no)
{
	return group_no * (amnfs_fsblk_t)AMNFS_BLOCKS_PER_GROUP(sb) +
		le32_to_cpu(AMNFS_SB(sb)->s_es->s_first_data_block);
}

#define amnfs_set_bit	__test_and_set_bit_le
#define amnfs_set_bit_atomic ext2_set_bit_atomic
#define amnfs_clear_bit	__test_and_clear_bit_le
#define amnfs_clear_bit_atomic ext2_clear_bit_atomic
#define amnfs_test_bit	test_bit_le
#define amnfs_find_first_zero_bit	find_first_zero_bit_le
#define amnfs_find_next_zero_bit		find_next_zero_bit_le
