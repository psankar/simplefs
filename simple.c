/*
 * A Simple Filesystem for the Linux Kernel.
 *
 * Initial author: Sankar P <sankar.curiosity@gmail.com>
 * License: Creative Commons Zero License - http://creativecommons.org/publicdomain/zero/1.0/
 *
 * TODO: we need to split it into smaller files
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/jbd2.h>
#include <linux/parser.h>
#include <linux/blkdev.h>

#include "super.h"

#ifndef f_dentry
#define f_dentry f_path.dentry
#endif

/* A super block lock that must be used for any critical section operation on the sb,
 * such as: updating the free_blocks, inodes_count etc. */
static DEFINE_MUTEX(simplefs_sb_lock);
static DEFINE_MUTEX(simplefs_inodes_mgmt_lock);

/* FIXME: This can be moved to an in-memory structure of the simplefs_inode.
 * Because of the global nature of this lock, we cannot create
 * new children (without locking) in two different dirs at a time.
 * They will get sequentially created. If we move the lock
 * to a directory-specific way (by moving it inside inode), the
 * insertion of two children in two different directories can be
 * done in parallel */
static DEFINE_MUTEX(simplefs_directory_children_update_lock);

static struct kmem_cache *sfs_inode_cachep;

void simplefs_sb_sync(struct super_block *vsb)
{
	struct buffer_head *bh = NULL;
	struct simplefs_super_block *sb = SIMPLEFS_SB(vsb);

	bh = sb_bread(vsb, SIMPLEFS_SUPERBLOCK_BLOCK_NUMBER);
	BUG_ON(!bh);

	bh->b_data = (char *)sb;
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
}

struct simplefs_inode *simplefs_inode_search(struct super_block *sb,
		struct simplefs_inode *start,
		struct simplefs_inode *search)
{
	uint64_t count = 0;
	while (start->inode_no != search->inode_no
			&& count < SIMPLEFS_SB(sb)->inodes_count) {
		count++;
		start++;
	}

	if (start->inode_no == search->inode_no) {
		return start;
	}

	return NULL;
}

void simplefs_inode_add(struct super_block *vsb, struct simplefs_inode *inode)
{
	struct simplefs_super_block *sb = SIMPLEFS_SB(vsb);
	struct buffer_head *bh = NULL;
	struct simplefs_inode *inode_iterator = NULL;

	if (mutex_lock_interruptible(&simplefs_inodes_mgmt_lock)) {
		sfs_trace("Failed to acquire mutex lock\n");
		return;
	}

	bh = sb_bread(vsb, SIMPLEFS_INODESTORE_BLOCK_NUMBER);
	BUG_ON(!bh);

	inode_iterator = (struct simplefs_inode *)bh->b_data;

	if (mutex_lock_interruptible(&simplefs_sb_lock)) {
		sfs_trace("Failed to acquire mutex lock\n");
		return;
	}

	/* Append the new inode in the end in the inode store */
	inode_iterator += sb->inodes_count;

	memcpy(inode_iterator, inode, sizeof(struct simplefs_inode));
	sb->inodes_count++;

	mark_buffer_dirty(bh);
	simplefs_sb_sync(vsb);
	brelse(bh);

	mutex_unlock(&simplefs_sb_lock);
	mutex_unlock(&simplefs_inodes_mgmt_lock);
}

/* This function returns a blocknumber which is free.
 * The block will be removed from the freeblock list.
 *
 * In an ideal, production-ready filesystem, we will not be dealing with blocks,
 * and instead we will be using extents
 *
 * If for some reason, the file creation/deletion failed, the block number
 * will still be marked as non-free. You need fsck to fix this.*/
int simplefs_sb_get_a_freeblock(struct super_block *vsb, uint64_t * out)
{
	struct simplefs_super_block *sb = SIMPLEFS_SB(vsb);
	int i;
	int ret = 0;

	if (mutex_lock_interruptible(&simplefs_sb_lock)) {
		sfs_trace("Failed to acquire mutex lock\n");
		ret = -EINTR;
		goto end;
	}

	/* Loop until we find a free block. We start the loop from 3,
	 * as all prior blocks will always be in use */
	for (i = 3; i < SIMPLEFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++) {
		if (sb->free_blocks & (1 << i)) {
			break;
		}
	}

	if (unlikely(i == SIMPLEFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED)) {
		printk(KERN_ERR "No more free blocks available");
		ret = -ENOSPC;
		goto end;
	}

	*out = i;

	/* Remove the identified block from the free list */
	sb->free_blocks &= ~(1 << i);

	simplefs_sb_sync(vsb);

end:
	mutex_unlock(&simplefs_sb_lock);
	return ret;
}

static int simplefs_sb_get_objects_count(struct super_block *vsb,
					 uint64_t * out)
{
	struct simplefs_super_block *sb = SIMPLEFS_SB(vsb);

	if (mutex_lock_interruptible(&simplefs_inodes_mgmt_lock)) {
		sfs_trace("Failed to acquire mutex lock\n");
		return -EINTR;
	}
	*out = sb->inodes_count;
	mutex_unlock(&simplefs_inodes_mgmt_lock);

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
static int simplefs_iterate(struct file *filp, struct dir_context *ctx)
#else
static int simplefs_readdir(struct file *filp, void *dirent, filldir_t filldir)
#endif
{
	loff_t pos;
	struct inode *inode;
	struct super_block *sb;
	struct buffer_head *bh;
	struct simplefs_inode *sfs_inode;
	struct simplefs_dir_record *record;
	int i;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	pos = ctx->pos;
#else
	pos = filp->f_pos;
#endif
	inode = filp->f_dentry->d_inode;
	sb = inode->i_sb;

	if (pos) {
		/* FIXME: We use a hack of reading pos to figure if we have filled in all data.
		 * We should probably fix this to work in a cursor based model and
		 * use the tokens correctly to not fill too many data in each cursor based call */
		return 0;
	}

	sfs_inode = SIMPLEFS_INODE(inode);

	if (unlikely(!S_ISDIR(sfs_inode->mode))) {
		printk(KERN_ERR
		       "inode [%llu][%lu] for fs object [%s] not a directory\n",
		       sfs_inode->inode_no, inode->i_ino,
		       filp->f_dentry->d_name.name);
		return -ENOTDIR;
	}

	bh = sb_bread(sb, sfs_inode->data_block_number);
	BUG_ON(!bh);

	record = (struct simplefs_dir_record *)bh->b_data;
	for (i = 0; i < sfs_inode->dir_children_count; i++) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
		dir_emit(ctx, record->filename, SIMPLEFS_FILENAME_MAXLEN,
			record->inode_no, DT_UNKNOWN);
		ctx->pos += sizeof(struct simplefs_dir_record);
#else
		filldir(dirent, record->filename, SIMPLEFS_FILENAME_MAXLEN, pos,
			record->inode_no, DT_UNKNOWN);
		filp->f_pos += sizeof(struct simplefs_dir_record);
#endif
		pos += sizeof(struct simplefs_dir_record);
		record++;
	}
	brelse(bh);

	return 0;
}

/* This functions returns a simplefs_inode with the given inode_no
 * from the inode store, if it exists. */
struct simplefs_inode *simplefs_get_inode(struct super_block *sb,
					  uint64_t inode_no)
{
	struct simplefs_super_block *sfs_sb = SIMPLEFS_SB(sb);
	struct simplefs_inode *sfs_inode = NULL;
	struct simplefs_inode *inode_buffer = NULL;

	int i;
	struct buffer_head *bh;

	/* The inode store can be read once and kept in memory permanently while mounting.
	 * But such a model will not be scalable in a filesystem with
	 * millions or billions of files (inodes) */
	bh = sb_bread(sb, SIMPLEFS_INODESTORE_BLOCK_NUMBER);
	BUG_ON(!bh);

	sfs_inode = (struct simplefs_inode *)bh->b_data;

#if 0
	if (mutex_lock_interruptible(&simplefs_inodes_mgmt_lock)) {
		printk(KERN_ERR "Failed to acquire mutex lock %s +%d\n",
		       __FILE__, __LINE__);
		return NULL;
	}
#endif
	for (i = 0; i < sfs_sb->inodes_count; i++) {
		if (sfs_inode->inode_no == inode_no) {
			inode_buffer = kmem_cache_alloc(sfs_inode_cachep, GFP_KERNEL);
			memcpy(inode_buffer, sfs_inode, sizeof(*inode_buffer));

			break;
		}
		sfs_inode++;
	}
//      mutex_unlock(&simplefs_inodes_mgmt_lock);

	brelse(bh);
	return inode_buffer;
}

ssize_t simplefs_read(struct file * filp, char __user * buf, size_t len,
		      loff_t * ppos)
{
	/* After the commit dd37978c5 in the upstream linux kernel,
	 * we can use just filp->f_inode instead of the
	 * f->f_path.dentry->d_inode redirection */
	struct simplefs_inode *inode =
	    SIMPLEFS_INODE(filp->f_path.dentry->d_inode);
	struct buffer_head *bh;

	char *buffer;
	int nbytes;

	if (*ppos >= inode->file_size) {
		/* Read request with offset beyond the filesize */
		return 0;
	}

	bh = sb_bread(filp->f_path.dentry->d_inode->i_sb,
					    inode->data_block_number);

	if (!bh) {
		printk(KERN_ERR "Reading the block number [%llu] failed.",
		       inode->data_block_number);
		return 0;
	}

	buffer = (char *)bh->b_data;
	nbytes = min((size_t) inode->file_size, len);

	if (copy_to_user(buf, buffer, nbytes)) {
		brelse(bh);
		printk(KERN_ERR
		       "Error copying file contents to the userspace buffer\n");
		return -EFAULT;
	}

	brelse(bh);

	*ppos += nbytes;

	return nbytes;
}

/* Save the modified inode */
int simplefs_inode_save(struct super_block *sb, struct simplefs_inode *sfs_inode)
{
	struct simplefs_inode *inode_iterator;
	struct buffer_head *bh;

	bh = sb_bread(sb, SIMPLEFS_INODESTORE_BLOCK_NUMBER);
	BUG_ON(!bh);

	if (mutex_lock_interruptible(&simplefs_sb_lock)) {
		sfs_trace("Failed to acquire mutex lock\n");
		return -EINTR;
	}

	inode_iterator = simplefs_inode_search(sb,
		(struct simplefs_inode *)bh->b_data,
		sfs_inode);

	if (likely(inode_iterator)) {
		memcpy(inode_iterator, sfs_inode, sizeof(*inode_iterator));
		printk(KERN_INFO "The inode updated\n");

		mark_buffer_dirty(bh);
		sync_dirty_buffer(bh);
	} else {
		mutex_unlock(&simplefs_sb_lock);
		printk(KERN_ERR
		       "The new filesize could not be stored to the inode.");
		return -EIO;
	}

	brelse(bh);

	mutex_unlock(&simplefs_sb_lock);

	return 0;
}

/* FIXME: The write support is rudimentary. I have not figured out a way to do writes
 * from particular offsets (even though I have written some untested code for this below) efficiently. */
ssize_t simplefs_write(struct file * filp, const char __user * buf, size_t len,
		       loff_t * ppos)
{
	/* After the commit dd37978c5 in the upstream linux kernel,
	 * we can use just filp->f_inode instead of the
	 * f->f_path.dentry->d_inode redirection */
	struct inode *inode;
	struct simplefs_inode *sfs_inode;
	struct buffer_head *bh;
	struct super_block *sb;
	struct simplefs_super_block *sfs_sb;
	handle_t *handle;

	char *buffer;

	int retval;

	sb = filp->f_path.dentry->d_inode->i_sb;
	sfs_sb = SIMPLEFS_SB(sb);

	handle = jbd2_journal_start(sfs_sb->journal, 1);
	if (IS_ERR(handle))
		return PTR_ERR(handle);
	retval = generic_write_checks(filp, ppos, &len, 0);
	if (retval)
		return retval;

	inode = filp->f_path.dentry->d_inode;
	sfs_inode = SIMPLEFS_INODE(inode);

	bh = sb_bread(filp->f_path.dentry->d_inode->i_sb,
					    sfs_inode->data_block_number);

	if (!bh) {
		printk(KERN_ERR "Reading the block number [%llu] failed.",
		       sfs_inode->data_block_number);
		return 0;
	}
	buffer = (char *)bh->b_data;

	/* Move the pointer until the required byte offset */
	buffer += *ppos;

	retval = jbd2_journal_get_write_access(handle, bh);
	if (WARN_ON(retval)) {
		brelse(bh);
		sfs_trace("Can't get write access for bh\n");
		return retval;
	}

	if (copy_from_user(buffer, buf, len)) {
		brelse(bh);
		printk(KERN_ERR
		       "Error copying file contents from the userspace buffer to the kernel space\n");
		return -EFAULT;
	}
	*ppos += len;

	retval = jbd2_journal_dirty_metadata(handle, bh);
	if (WARN_ON(retval)) {
		brelse(bh);
		return retval;
	}
	handle->h_sync = 1;
	retval = jbd2_journal_stop(handle);
	if (WARN_ON(retval)) {
		brelse(bh);
		return retval;
	}

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	/* Set new size
	 * sfs_inode->file_size = max(sfs_inode->file_size, *ppos);
	 *
	 * FIXME: What to do if someone writes only some parts in between ?
	 * The above code will also fail in case a file is overwritten with
	 * a shorter buffer */
	if (mutex_lock_interruptible(&simplefs_inodes_mgmt_lock)) {
		sfs_trace("Failed to acquire mutex lock\n");
		return -EINTR;
	}
	sfs_inode->file_size = *ppos;
	retval = simplefs_inode_save(sb, sfs_inode);
	if (retval) {
		len = retval;
	}
	mutex_unlock(&simplefs_inodes_mgmt_lock);

	return len;
}

const struct file_operations simplefs_file_operations = {
	.read = simplefs_read,
	.write = simplefs_write,
};

const struct file_operations simplefs_dir_operations = {
	.owner = THIS_MODULE,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	.iterate = simplefs_iterate,
#else
	.readdir = simplefs_readdir,
#endif
};

struct dentry *simplefs_lookup(struct inode *parent_inode,
			       struct dentry *child_dentry, unsigned int flags);

static int simplefs_create(struct inode *dir, struct dentry *dentry,
			   umode_t mode, bool excl);

static int simplefs_mkdir(struct inode *dir, struct dentry *dentry,
			  umode_t mode);

static struct inode_operations simplefs_inode_ops = {
	.create = simplefs_create,
	.lookup = simplefs_lookup,
	.mkdir = simplefs_mkdir,
};

static int simplefs_create_fs_object(struct inode *dir, struct dentry *dentry,
				     umode_t mode)
{
	struct inode *inode;
	struct simplefs_inode *sfs_inode;
	struct super_block *sb;
	struct simplefs_inode *parent_dir_inode;
	struct buffer_head *bh;
	struct simplefs_dir_record *dir_contents_datablock;
	uint64_t count;
	int ret;

	if (mutex_lock_interruptible(&simplefs_directory_children_update_lock)) {
		sfs_trace("Failed to acquire mutex lock\n");
		return -EINTR;
	}
	sb = dir->i_sb;

	ret = simplefs_sb_get_objects_count(sb, &count);
	if (ret < 0) {
		mutex_unlock(&simplefs_directory_children_update_lock);
		return ret;
	}

	if (unlikely(count >= SIMPLEFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED)) {
		/* The above condition can be just == insted of the >= */
		printk(KERN_ERR
		       "Maximum number of objects supported by simplefs is already reached");
		mutex_unlock(&simplefs_directory_children_update_lock);
		return -ENOSPC;
	}

	if (!S_ISDIR(mode) && !S_ISREG(mode)) {
		printk(KERN_ERR
		       "Creation request but for neither a file nor a directory");
		mutex_unlock(&simplefs_directory_children_update_lock);
		return -EINVAL;
	}

	inode = new_inode(sb);
	if (!inode) {
		mutex_unlock(&simplefs_directory_children_update_lock);
		return -ENOMEM;
	}

	inode->i_sb = sb;
	inode->i_op = &simplefs_inode_ops;
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_ino = (count + SIMPLEFS_START_INO - SIMPLEFS_RESERVED_INODES + 1);

	sfs_inode = kmem_cache_alloc(sfs_inode_cachep, GFP_KERNEL);
	sfs_inode->inode_no = inode->i_ino;
	inode->i_private = sfs_inode;
	sfs_inode->mode = mode;

	if (S_ISDIR(mode)) {
		printk(KERN_INFO "New directory creation request\n");
		sfs_inode->dir_children_count = 0;
		inode->i_fop = &simplefs_dir_operations;
	} else if (S_ISREG(mode)) {
		printk(KERN_INFO "New file creation request\n");
		sfs_inode->file_size = 0;
		inode->i_fop = &simplefs_file_operations;
	}

	/* First get a free block and update the free map,
	 * Then add inode to the inode store and update the sb inodes_count,
	 * Then update the parent directory's inode with the new child.
	 *
	 * The above ordering helps us to maintain fs consistency
	 * even in most crashes
	 */
	ret = simplefs_sb_get_a_freeblock(sb, &sfs_inode->data_block_number);
	if (ret < 0) {
		printk(KERN_ERR "simplefs could not get a freeblock");
		mutex_unlock(&simplefs_directory_children_update_lock);
		return ret;
	}

	simplefs_inode_add(sb, sfs_inode);

	parent_dir_inode = SIMPLEFS_INODE(dir);
	bh = sb_bread(sb, parent_dir_inode->data_block_number);
	BUG_ON(!bh);

	dir_contents_datablock = (struct simplefs_dir_record *)bh->b_data;

	/* Navigate to the last record in the directory contents */
	dir_contents_datablock += parent_dir_inode->dir_children_count;

	dir_contents_datablock->inode_no = sfs_inode->inode_no;
	strcpy(dir_contents_datablock->filename, dentry->d_name.name);

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	if (mutex_lock_interruptible(&simplefs_inodes_mgmt_lock)) {
		mutex_unlock(&simplefs_directory_children_update_lock);
		sfs_trace("Failed to acquire mutex lock\n");
		return -EINTR;
	}

	parent_dir_inode->dir_children_count++;
	ret = simplefs_inode_save(sb, parent_dir_inode);
	if (ret) {
		mutex_unlock(&simplefs_inodes_mgmt_lock);
		mutex_unlock(&simplefs_directory_children_update_lock);

		/* TODO: Remove the newly created inode from the disk and in-memory inode store
		 * and also update the superblock, freemaps etc. to reflect the same.
		 * Basically, Undo all actions done during this create call */
		return ret;
	}

	mutex_unlock(&simplefs_inodes_mgmt_lock);
	mutex_unlock(&simplefs_directory_children_update_lock);

	inode_init_owner(inode, dir, mode);
	d_add(dentry, inode);

	return 0;
}

static int simplefs_mkdir(struct inode *dir, struct dentry *dentry,
			  umode_t mode)
{
	/* I believe this is a bug in the kernel, for some reason, the mkdir callback
	 * does not get the S_IFDIR flag set. Even ext2 sets is explicitly */
	return simplefs_create_fs_object(dir, dentry, S_IFDIR | mode);
}

static int simplefs_create(struct inode *dir, struct dentry *dentry,
			   umode_t mode, bool excl)
{
	return simplefs_create_fs_object(dir, dentry, mode);
}

static struct inode *simplefs_iget(struct super_block *sb, int ino)
{
	struct inode *inode;
	struct simplefs_inode *sfs_inode;

	sfs_inode = simplefs_get_inode(sb, ino);

	inode = new_inode(sb);
	inode->i_ino = ino;
	inode->i_sb = sb;
	inode->i_op = &simplefs_inode_ops;

	if (S_ISDIR(sfs_inode->mode))
		inode->i_fop = &simplefs_dir_operations;
	else if (S_ISREG(sfs_inode->mode) || ino == SIMPLEFS_JOURNAL_INODE_NUMBER)
		inode->i_fop = &simplefs_file_operations;
	else
		printk(KERN_ERR
					 "Unknown inode type. Neither a directory nor a file");

	/* FIXME: We should store these times to disk and retrieve them */
	inode->i_atime = inode->i_mtime = inode->i_ctime =
			current_time(inode);

	inode->i_private = sfs_inode;

	return inode;
}

struct dentry *simplefs_lookup(struct inode *parent_inode,
			       struct dentry *child_dentry, unsigned int flags)
{
	struct simplefs_inode *parent = SIMPLEFS_INODE(parent_inode);
	struct super_block *sb = parent_inode->i_sb;
	struct buffer_head *bh;
	struct simplefs_dir_record *record;
	int i;

	bh = sb_bread(sb, parent->data_block_number);
	BUG_ON(!bh);
	sfs_trace("Lookup in: ino=%llu, b=%llu\n",
				parent->inode_no, parent->data_block_number);

	record = (struct simplefs_dir_record *)bh->b_data;
	for (i = 0; i < parent->dir_children_count; i++) {
		sfs_trace("Have file: '%s' (ino=%llu)\n",
					record->filename, record->inode_no);

		if (!strcmp(record->filename, child_dentry->d_name.name)) {
			/* FIXME: There is a corner case where if an allocated inode,
			 * is not written to the inode store, but the inodes_count is
			 * incremented. Then if the random string on the disk matches
			 * with the filename that we are comparing above, then we
			 * will use an invalid uninitialized inode */

			struct inode *inode = simplefs_iget(sb, record->inode_no);
			inode_init_owner(inode, parent_inode, SIMPLEFS_INODE(inode)->mode);
			d_add(child_dentry, inode);
			return NULL;
		}
		record++;
	}

	printk(KERN_ERR
	       "No inode found for the filename [%s]\n",
	       child_dentry->d_name.name);

	return NULL;
}


/**
 * Simplest
 */
void simplefs_destory_inode(struct inode *inode)
{
	struct simplefs_inode *sfs_inode = SIMPLEFS_INODE(inode);

	printk(KERN_INFO "Freeing private data of inode %p (%lu)\n",
	       sfs_inode, inode->i_ino);
	kmem_cache_free(sfs_inode_cachep, sfs_inode);
}

static void simplefs_put_super(struct super_block *sb)
{
	struct simplefs_super_block *sfs_sb = SIMPLEFS_SB(sb);
	if (sfs_sb->journal)
		WARN_ON(jbd2_journal_destroy(sfs_sb->journal) < 0);
	sfs_sb->journal = NULL;
}

static const struct super_operations simplefs_sops = {
	.destroy_inode = simplefs_destory_inode,
	.put_super = simplefs_put_super,
};

static int simplefs_load_journal(struct super_block *sb, int devnum)
{
	struct journal_s *journal;
	char b[BDEVNAME_SIZE];
	dev_t dev;
	struct block_device *bdev;
	int hblock, blocksize, len;
	struct simplefs_super_block *sfs_sb = SIMPLEFS_SB(sb);

	dev = new_decode_dev(devnum);
	printk(KERN_INFO "Journal device is: %s\n", __bdevname(dev, b));

	bdev = blkdev_get_by_dev(dev, FMODE_READ|FMODE_WRITE|FMODE_EXCL, sb);
	if (IS_ERR(bdev))
		return 1;
	blocksize = sb->s_blocksize;
	hblock = bdev_logical_block_size(bdev);
	len = SIMPLEFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED;

	journal = jbd2_journal_init_dev(bdev, sb->s_bdev, 1, -1, blocksize);
	if (!journal) {
		printk(KERN_ERR "Can't load journal\n");
		return 1;
	}
	journal->j_private = sb;

	sfs_sb->journal = journal;

	return 0;
}
static int simplefs_sb_load_journal(struct super_block *sb, struct inode *inode)
{
	struct journal_s *journal;
	struct simplefs_super_block *sfs_sb = SIMPLEFS_SB(sb);

	journal = jbd2_journal_init_inode(inode);
	if (!journal) {
		printk(KERN_ERR "Can't load journal\n");
		return 1;
	}
	journal->j_private = sb;

	sfs_sb->journal = journal;

	return 0;
}

#define SIMPLEFS_OPT_JOURNAL_DEV 1
#define SIMPLEFS_OPT_JOURNAL_PATH 2
static const match_table_t tokens = {
	{SIMPLEFS_OPT_JOURNAL_DEV, "journal_dev=%u"},
	{SIMPLEFS_OPT_JOURNAL_PATH, "journal_path=%s"},
};
static int simplefs_parse_options(struct super_block *sb, char *options)
{
	substring_t args[MAX_OPT_ARGS];
	int token, ret, arg;
	char *p;

	while ((p = strsep(&options, ",")) != NULL) {
		if (!*p)
			continue;

		args[0].to = args[0].from = NULL;
		token = match_token(p, tokens, args);

		switch (token) {
			case SIMPLEFS_OPT_JOURNAL_DEV:
				if (args->from && match_int(args, &arg))
					return 1;
				printk(KERN_INFO "Loading journal devnum: %i\n", arg);
				if ((ret = simplefs_load_journal(sb, arg)))
					return ret;
				break;

			case SIMPLEFS_OPT_JOURNAL_PATH:
			{
				char *journal_path;
				struct inode *journal_inode;
				struct path path;

				BUG_ON(!(journal_path = match_strdup(&args[0])));
				ret = kern_path(journal_path, LOOKUP_FOLLOW, &path);
				if (ret) {
					printk(KERN_ERR "could not find journal device path: error %d\n", ret);
					kfree(journal_path);
				}

				journal_inode = path.dentry->d_inode;

				path_put(&path);
				kfree(journal_path);

				if (S_ISBLK(journal_inode->i_mode)) {
					unsigned long journal_devnum = new_encode_dev(journal_inode->i_rdev);
					if ((ret = simplefs_load_journal(sb, journal_devnum)))
						return ret;
				} else {
					/** Seems didn't work properly */
					if ((ret = simplefs_sb_load_journal(sb, journal_inode)))
						return ret;
				}

				break;
			}
		}
	}

	return 0;
}

/* This function, as the name implies, Makes the super_block valid and
 * fills filesystem specific information in the super block */
int simplefs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root_inode;
	struct buffer_head *bh;
	struct simplefs_super_block *sb_disk;
	int ret = -EPERM;

	bh = sb_bread(sb, SIMPLEFS_SUPERBLOCK_BLOCK_NUMBER);
	BUG_ON(!bh);

	sb_disk = (struct simplefs_super_block *)bh->b_data;

	printk(KERN_INFO "The magic number obtained in disk is: [%llu]\n",
	       sb_disk->magic);

	if (unlikely(sb_disk->magic != SIMPLEFS_MAGIC)) {
		printk(KERN_ERR
		       "The filesystem that you try to mount is not of type simplefs. Magicnumber mismatch.");
		goto release;
	}

	if (unlikely(sb_disk->block_size != SIMPLEFS_DEFAULT_BLOCK_SIZE)) {
		printk(KERN_ERR
		       "simplefs seem to be formatted using a non-standard block size.");
		goto release;
	}
	/** XXX: Avoid this hack, by adding one more sb wrapper, but non-disk */
	sb_disk->journal = NULL;

	printk(KERN_INFO
	       "simplefs filesystem of version [%llu] formatted with a block size of [%llu] detected in the device.\n",
	       sb_disk->version, sb_disk->block_size);

	/* A magic number that uniquely identifies our filesystem type */
	sb->s_magic = SIMPLEFS_MAGIC;

	/* For all practical purposes, we will be using this s_fs_info as the super block */
	sb->s_fs_info = sb_disk;

	sb->s_maxbytes = SIMPLEFS_DEFAULT_BLOCK_SIZE;
	sb->s_op = &simplefs_sops;

	root_inode = new_inode(sb);
	root_inode->i_ino = SIMPLEFS_ROOTDIR_INODE_NUMBER;
	inode_init_owner(root_inode, NULL, S_IFDIR);
	root_inode->i_sb = sb;
	root_inode->i_op = &simplefs_inode_ops;
	root_inode->i_fop = &simplefs_dir_operations;
	root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime =
	    current_time(root_inode);

	root_inode->i_private =
	    simplefs_get_inode(sb, SIMPLEFS_ROOTDIR_INODE_NUMBER);

	/* TODO: move such stuff into separate header. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
	sb->s_root = d_make_root(root_inode);
#else
	sb->s_root = d_alloc_root(root_inode);
	if (!sb->s_root)
		iput(root_inode);
#endif

	if (!sb->s_root) {
		ret = -ENOMEM;
		goto release;
	}

	if ((ret = simplefs_parse_options(sb, data)))
		goto release;

	if (!sb_disk->journal) {
		struct inode *journal_inode;
		journal_inode = simplefs_iget(sb, SIMPLEFS_JOURNAL_INODE_NUMBER);

		ret = simplefs_sb_load_journal(sb, journal_inode);
		goto release;
	}
	ret = jbd2_journal_load(sb_disk->journal);

release:
	brelse(bh);

	return ret;
}

static struct dentry *simplefs_mount(struct file_system_type *fs_type,
				     int flags, const char *dev_name,
				     void *data)
{
	struct dentry *ret;

	ret = mount_bdev(fs_type, flags, dev_name, data, simplefs_fill_super);

	if (unlikely(IS_ERR(ret)))
		printk(KERN_ERR "Error mounting simplefs");
	else
		printk(KERN_INFO "simplefs is succesfully mounted on [%s]\n",
		       dev_name);

	return ret;
}

static void simplefs_kill_superblock(struct super_block *sb)
{
	printk(KERN_INFO
	       "simplefs superblock is destroyed. Unmount succesful.\n");
	/* This is just a dummy function as of now. As our filesystem gets matured,
	 * we will do more meaningful operations here */

	kill_block_super(sb);
	return;
}

struct file_system_type simplefs_fs_type = {
	.owner = THIS_MODULE,
	.name = "simplefs",
	.mount = simplefs_mount,
	.kill_sb = simplefs_kill_superblock,
	.fs_flags = FS_REQUIRES_DEV,
};

static int simplefs_init(void)
{
	int ret;

	sfs_inode_cachep = kmem_cache_create("sfs_inode_cache",
	                                     sizeof(struct simplefs_inode),
	                                     0,
	                                     (SLAB_RECLAIM_ACCOUNT| SLAB_MEM_SPREAD),
	                                     NULL);
	if (!sfs_inode_cachep) {
		return -ENOMEM;
	}

	ret = register_filesystem(&simplefs_fs_type);
	if (likely(ret == 0))
		printk(KERN_INFO "Sucessfully registered simplefs\n");
	else
		printk(KERN_ERR "Failed to register simplefs. Error:[%d]", ret);

	return ret;
}

static void simplefs_exit(void)
{
	int ret;

	ret = unregister_filesystem(&simplefs_fs_type);
	kmem_cache_destroy(sfs_inode_cachep);

	if (likely(ret == 0))
		printk(KERN_INFO "Sucessfully unregistered simplefs\n");
	else
		printk(KERN_ERR "Failed to unregister simplefs. Error:[%d]",
		       ret);
}

module_init(simplefs_init);
module_exit(simplefs_exit);

MODULE_LICENSE("CC0");
MODULE_AUTHOR("Sankar P");
