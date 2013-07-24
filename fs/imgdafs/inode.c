/*
 * Copyright (C) 2008-2013 Imagination Technologies Ltd.
 * Licensed under the GPL
 *
 * Based on hostfs for UML.
 *
 */

#include <linux/fs.h>
#include <linux/magic.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/slab.h>

#include <asm/da.h>
#include <asm/hwthread.h>

#include "imgdafs.h"

static int fscall(int in_system_call, int in_arg1, int in_arg2, int in_arg3,
		  int in_arg4, int in_arg5, int *fserrno)
{
	register int arg1            asm("D1Ar1") = in_arg1;
	register int arg2            asm("D0Ar2") = in_arg2;
	register int arg3            asm("D1Ar3") = in_arg3;
	register int arg4            asm("D0Ar4") = in_arg4;
	register int arg5            asm("D1Ar5") = in_arg5;
	register int system_call     asm("D0Ar6") = in_system_call;
	register int result          asm("D0Re0");
	register int errno           asm("D1Re0");

	asm volatile (
		"MSETL	[A0StP++], %7,%5,%3\n\t"
		"ADD	A0StP, A0StP, #8\n\t"
		"SWITCH	#0x0C00208\n\t"
		"GETL	%0, %1, [A0StP+#-8]\n\t"
		"SUB	A0StP, A0StP, #(4*6)+8\n\t"
		: "=r" (result),
		  "=r" (errno)
		: "r" (arg1),
		  "r" (arg2),
		  "r" (arg3),
		  "r" (arg4),
		  "r" (arg5),
		  "r" (system_call)
		: "memory");

	if (fserrno)
		*fserrno = errno;

	return result;
}

struct dafs_inode_info {
	int fd;
	fmode_t mode;
	struct inode vfs_inode;
};

static inline struct dafs_inode_info *DAFS_I(struct inode *inode)
{
	return container_of(inode, struct dafs_inode_info, vfs_inode);
}

#define FILE_DAFS_I(file) DAFS_I(file_inode(file))

static int dafs_d_delete(const struct dentry *dentry)
{
	return 1;
}

static const struct dentry_operations dafs_dentry_ops = {
	.d_delete		= dafs_d_delete,
};

static const struct inode_operations dafs_dir_iops;

static char *__dentry_name(struct dentry *dentry, char *name)
{
	char *p = dentry_path_raw(dentry, name, PATH_MAX);
	char *root;
	size_t len;

	root = dentry->d_sb->s_fs_info;
	len = strlen(root);
	if (IS_ERR(p)) {
		__putname(name);
		return NULL;
	}

	strlcpy(name, root, PATH_MAX);
	if (len > p - name) {
		__putname(name);
		return NULL;
	}
	if (p > name + len) {
		char *s = name + len;
		while ((*s++ = *p++) != '\0')
			;
	}
	return name;
}

static char *dentry_name(struct dentry *dentry)
{
	char *name = __getname();
	if (!name)
		return NULL;

	return __dentry_name(dentry, name);
}

static int stat_file(const char *path, struct da_stat *p, int fd)
{
	int ret;
	int fserrno;
	memset(p, 0, sizeof(*p));

	if (fd >= 0) {
		ret = fscall(DA_OP_FSTAT, fd, (int)p, 0, 0, 0, &fserrno);
		if (ret < 0) {
			/* Some versions of Codescape do not fill out errno. */
			if (ret < 0 && fserrno == 0)
				fserrno = ENOENT;
			return -fserrno;
		}
	} else {
		ret = fscall(DA_OP_STAT, (int)path, (int)p, strlen(path), 0, 0,
			     &fserrno);
		if (ret < 0) {
			/* Some versions of Codescape do not fill out errno. */
			if (ret < 0 && fserrno == 0)
				fserrno = ENOENT;
			return -fserrno;
		}
	}

	return 0;
}

static struct inode *dafs_iget(struct super_block *sb)
{
	struct inode *inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	return inode;
}

static struct inode *dafs_alloc_inode(struct super_block *sb)
{
	struct dafs_inode_info *hi;

	hi = kmalloc(sizeof(*hi), GFP_KERNEL);
	if (hi == NULL)
		return NULL;

	hi->fd = -1;
	hi->mode = 0;
	inode_init_once(&hi->vfs_inode);
	return &hi->vfs_inode;
}

static void close_file(void *stream)
{
	int fd = *((int *) stream);

	fscall(DA_OP_CLOSE, fd, 0, 0, 0, 0, NULL);
}

static void dafs_evict_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);
	clear_inode(inode);
	if (DAFS_I(inode)->fd != -1) {
		close_file(&DAFS_I(inode)->fd);
		DAFS_I(inode)->fd = -1;
	}
}

static void dafs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kfree(DAFS_I(inode));
}

static void dafs_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, dafs_i_callback);
}

static const struct super_operations dafs_sbops = {
	.alloc_inode	= dafs_alloc_inode,
	.drop_inode	= generic_delete_inode,
	.evict_inode	= dafs_evict_inode,
	.destroy_inode	= dafs_destroy_inode,
};

static int open_dir(char *path, struct da_finddata *finddata, int *fserrno)
{
	int len = strlen(path);
	char *buf;
	int ret;

	buf = kmalloc(len + 3, GFP_KERNEL);
	if (!buf) {
		*fserrno = ENOMEM;
		return -1;
	}

	strcpy(buf, path);
	if (buf[len - 1] != '/')
		strcat(buf, "/*");
	else
		strcat(buf, "*");

	ret = fscall(DA_OP_FINDFIRST, (int)buf, (int)finddata, 0, 0, 0,
		     fserrno);
	kfree(buf);
	return ret;
}

static void close_dir(int handle)
{
	fscall(DA_OP_FINDCLOSE, handle, 0, 0, 0, 0, NULL);
}

static int read_dir(int handle, struct da_finddata *finddata)
{
	return fscall(DA_OP_FINDNEXT, handle, (int)finddata, 0, 0, 0, NULL);
}

static int dafs_readdir(struct file *file, void *ent, filldir_t filldir)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	char *name;
	int handle;
	int fserrno;
	unsigned long long next, ino;
	int error = 0;
	struct da_finddata finddata;

	name = dentry_name(file->f_path.dentry);
	if (name == NULL)
		return -ENOMEM;
	handle = open_dir(name, &finddata, &fserrno);
	__putname(name);
	if (handle == -1)
		return -fserrno;

	next = 1;

	if (file->f_pos == 0) {
		error = (*filldir)(ent, ".", file->f_pos + 1,
				   file->f_pos, inode->i_ino,
				   DT_DIR);
		if (error < 0)
			goto out;
		file->f_pos++;
	}

	while (1) {
		error = read_dir(handle, &finddata);
		if (error)
			break;

		if (next >= file->f_pos) {
			size_t len = strlen(finddata.name);
			ino = iunique(sb, 100);
			error = (*filldir)(ent, finddata.name, len,
					   file->f_pos, ino,
					   (finddata.attrib & _A_SUBDIR) ?
					    DT_DIR : DT_REG);
			if (error)
				break;
			file->f_pos++;
		}
		next++;
	}
out:
	close_dir(handle);
	return 0;
}

static int dafs_file_open(struct inode *ino, struct file *file)
{
	static DEFINE_MUTEX(open_mutex);
	char *name;
	fmode_t mode, fmode;
	int flags = 0, r = 0, w = 0, fd;
	int cpu;

	fmode = file->f_mode & (FMODE_READ | FMODE_WRITE);
	if ((fmode & DAFS_I(ino)->mode) == fmode)
		return 0;

	mode = ino->i_mode & (DA_S_IWUSR | DA_S_IRUSR);

	mode |= DAFS_I(ino)->mode;

	DAFS_I(ino)->mode |= fmode;
	if (DAFS_I(ino)->mode & FMODE_READ)
		r = 1;
	if (DAFS_I(ino)->mode & FMODE_WRITE) {
		w = 1;
		r = 1;
	}

retry:
	if (r && !w)
		flags |= DA_O_RDONLY;
	else if (!r && w)
		flags |= DA_O_WRONLY;
	else if (r && w)
		flags |= DA_O_RDWR;

	if (file->f_flags & O_CREAT)
		flags |= DA_O_CREAT;

	if (file->f_flags & O_TRUNC)
		flags |= DA_O_TRUNC;

	/*
	 * Set the affinity for this file handle to all CPUs. If we
	 * don't do this then, if the process that opened the file
	 * migrates to a different cpu, the FileServer will not accept
	 * the file handle.
	 */
	for_each_possible_cpu(cpu) {
		u8 hwthread = cpu_2_hwthread_id[cpu];
		flags |= (1 << (DA_O_AFFINITY_SHIFT + hwthread));
	}

	name = dentry_name(file->f_path.dentry);
	if (name == NULL)
		return -ENOMEM;

	fd = fscall(DA_OP_OPEN, (int)name, flags, mode, strlen(name), 0, NULL);
	__putname(name);
	if (fd < 0)
		return fd;

	mutex_lock(&open_mutex);
	/* somebody else had handled it first? */
	if ((mode & DAFS_I(ino)->mode) == mode) {
		mutex_unlock(&open_mutex);
		return 0;
	}
	if ((mode | DAFS_I(ino)->mode) != mode) {
		mode |= DAFS_I(ino)->mode;
		mutex_unlock(&open_mutex);
		close_file(&fd);
		goto retry;
	}
	DAFS_I(ino)->fd = fd;
	DAFS_I(ino)->mode = mode;
	mutex_unlock(&open_mutex);

	return 0;
}

static const struct file_operations dafs_file_fops = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.splice_read	= generic_file_splice_read,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.write		= do_sync_write,
	.mmap		= generic_file_mmap,
	.open		= dafs_file_open,
	.release	= NULL,
};

static const struct file_operations dafs_dir_fops = {
	.llseek		= generic_file_llseek,
	.readdir	= dafs_readdir,
	.read		= generic_read_dir,
};

static int read_file(int fd, unsigned long long *offset, const char *buf,
		     int len)
{
	int n;
	int fserrno;

	n = fscall(DA_OP_PREAD, fd, (int)buf, len, (int)*offset, 0, &fserrno);

	if (n < 0)
		return -fserrno;

	return n;
}

static int write_file(int fd, unsigned long long *offset, const char *buf,
		      int len)
{
	int n;
	int fserrno;

	n = fscall(DA_OP_PWRITE, fd, (int)buf, len, (int)*offset, 0, &fserrno);

	if (n < 0)
		return -fserrno;

	return n;
}

static int dafs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;
	char *buffer;
	unsigned long long base;
	int count = PAGE_CACHE_SIZE;
	int end_index = inode->i_size >> PAGE_CACHE_SHIFT;
	int err;

	if (page->index >= end_index)
		count = inode->i_size & (PAGE_CACHE_SIZE-1);

	buffer = kmap(page);
	base = ((unsigned long long) page->index) << PAGE_CACHE_SHIFT;

	err = write_file(DAFS_I(inode)->fd, &base, buffer, count);
	if (err != count) {
		ClearPageUptodate(page);
		goto out;
	}

	if (base > inode->i_size)
		inode->i_size = base;

	if (PageError(page))
		ClearPageError(page);
	err = 0;

 out:
	kunmap(page);

	unlock_page(page);
	return err;
}

static int dafs_readpage(struct file *file, struct page *page)
{
	char *buffer;
	long long start;
	int err = 0;

	start = (long long) page->index << PAGE_CACHE_SHIFT;
	buffer = kmap(page);
	err = read_file(FILE_DAFS_I(file)->fd, &start, buffer,
			PAGE_CACHE_SIZE);
	if (err < 0)
		goto out;

	memset(&buffer[err], 0, PAGE_CACHE_SIZE - err);

	flush_dcache_page(page);
	SetPageUptodate(page);
	if (PageError(page))
		ClearPageError(page);
	err = 0;
 out:
	kunmap(page);
	unlock_page(page);
	return err;
}

static int dafs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;

	*pagep = grab_cache_page_write_begin(mapping, index, flags);
	if (!*pagep)
		return -ENOMEM;
	return 0;
}

static int dafs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;
	void *buffer;
	unsigned from = pos & (PAGE_CACHE_SIZE - 1);
	int err;

	buffer = kmap(page);
	err = write_file(FILE_DAFS_I(file)->fd, &pos, buffer + from, copied);
	kunmap(page);

	if (!PageUptodate(page) && err == PAGE_CACHE_SIZE)
		SetPageUptodate(page);

	/*
	 * If err > 0, write_file has added err to pos, so we are comparing
	 * i_size against the last byte written.
	 */
	if (err > 0 && (pos > inode->i_size))
		inode->i_size = pos;
	unlock_page(page);
	page_cache_release(page);

	return err;
}

static const struct address_space_operations dafs_aops = {
	.writepage	= dafs_writepage,
	.readpage	= dafs_readpage,
	.set_page_dirty	= __set_page_dirty_nobuffers,
	.write_begin	= dafs_write_begin,
	.write_end	= dafs_write_end,
};

static int read_name(struct inode *ino, char *name)
{
	dev_t rdev;
	struct da_stat st;
	int err = stat_file(name, &st, -1);
	if (err)
		return err;

	/* No valid maj and min from DA.*/
	rdev = MKDEV(0, 0);

	switch (st.st_mode & S_IFMT) {
	case S_IFDIR:
		ino->i_op = &dafs_dir_iops;
		ino->i_fop = &dafs_dir_fops;
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		init_special_inode(ino, st.st_mode & S_IFMT, rdev);
		break;

	case S_IFLNK:
	default:
		ino->i_fop = &dafs_file_fops;
		ino->i_mapping->a_ops = &dafs_aops;
	}

	ino->i_ino = st.st_ino;
	ino->i_mode = st.st_mode;
	set_nlink(ino, st.st_nlink);

	i_uid_write(ino, st.st_uid);
	i_gid_write(ino, st.st_gid);
	ino->i_atime.tv_sec = st.st_atime;
	ino->i_atime.tv_nsec = 0;
	ino->i_mtime.tv_sec = st.st_mtime;
	ino->i_mtime.tv_nsec = 0;
	ino->i_ctime.tv_sec = st.st_ctime;
	ino->i_ctime.tv_nsec = 0;
	ino->i_size = st.st_size;
	ino->i_blocks = st.st_blocks;
	return 0;
}

static int dafs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		       bool excl)
{
	struct inode *inode;
	char *name;
	int error, fd;
	int damode;
	int creat_flags = DA_O_TRUNC | DA_O_CREAT | DA_O_WRONLY;
	int cpu;

	inode = dafs_iget(dir->i_sb);
	if (IS_ERR(inode)) {
		error = PTR_ERR(inode);
		goto out;
	}

	damode = mode & (DA_S_IWUSR | DA_S_IRUSR);

	error = -ENOMEM;
	name = dentry_name(dentry);
	if (name == NULL)
		goto out_put;

	/*
	 * creat() will only create text mode files on a Windows host
	 * at present.  Replicate the creat() functionality with an
	 * open() call, which always creates binary files. Set the
	 * affinity to all hardware threads.
	 */
	for_each_possible_cpu(cpu) {
		u8 hwthread = cpu_2_hwthread_id[cpu];
		creat_flags |= (1 << (DA_O_AFFINITY_SHIFT + hwthread));
	}

	fd = fscall(DA_OP_OPEN, (int)name, creat_flags, damode, strlen(name),
		    0, NULL);
	if (fd < 0)
		error = fd;
	else
		error = read_name(inode, name);

	kfree(name);
	if (error)
		goto out_put;

	DAFS_I(inode)->fd = fd;
	DAFS_I(inode)->mode = FMODE_READ | FMODE_WRITE;
	d_instantiate(dentry, inode);
	return 0;

 out_put:
	iput(inode);
 out:
	return error;
}

static struct dentry *dafs_lookup(struct inode *ino, struct dentry *dentry,
				  unsigned int flags)
{
	struct inode *inode;
	char *name;
	int err;

	inode = dafs_iget(ino->i_sb);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}

	err = -ENOMEM;
	name = dentry_name(dentry);
	if (name == NULL)
		goto out_put;

	err = read_name(inode, name);

	__putname(name);
	if (err == -ENOENT) {
		iput(inode);
		inode = NULL;
	} else if (err)
		goto out_put;

	d_add(dentry, inode);
	return NULL;

 out_put:
	iput(inode);
 out:
	return ERR_PTR(err);
}

static int dafs_link(struct dentry *to, struct inode *ino, struct dentry *from)
{
	char *from_name, *to_name;
	int err;

	from_name = dentry_name(from);
	if (from_name == NULL)
		return -ENOMEM;
	to_name = dentry_name(to);
	if (to_name == NULL) {
		__putname(from_name);
		return -ENOMEM;
	}
	err = -EINVAL;
	__putname(from_name);
	__putname(to_name);
	return err;
}

static int dafs_unlink(struct inode *ino, struct dentry *dentry)
{
	char *file;
	int err;
	int fserrno;

	file = dentry_name(dentry);
	if (file == NULL)
		return -ENOMEM;

	err = fscall(DA_OP_UNLINK, (int)file, 0, 0, 0, 0, &fserrno);
	__putname(file);
	if (err)
		return -fserrno;
	return 0;
}

static int do_mkdir(const char *file, int mode)
{
	int err;
	int fserrno;

	err = fscall(DA_OP_MKDIR, (int)file, mode, strlen(file), 0, 0,
		     &fserrno);
	if (err)
		return -fserrno;
	return 0;
}

static int dafs_mkdir(struct inode *ino, struct dentry *dentry, umode_t mode)
{
	char *file;
	int err;

	file = dentry_name(dentry);
	if (file == NULL)
		return -ENOMEM;
	err = do_mkdir(file, mode);
	__putname(file);
	return err;
}

static int do_rmdir(const char *file)
{
	int err;
	int fserrno;

	err = fscall(DA_OP_RMDIR, (int)file, strlen(file), 0, 0, 0, &fserrno);
	if (err)
		return -fserrno;
	return 0;
}

static int dafs_rmdir(struct inode *ino, struct dentry *dentry)
{
	char *file;
	int err;

	file = dentry_name(dentry);
	if (file == NULL)
		return -ENOMEM;
	err = do_rmdir(file);
	__putname(file);
	return err;
}

static int dafs_rename(struct inode *from_ino, struct dentry *from,
		  struct inode *to_ino, struct dentry *to)
{
	char *from_name, *to_name;
	int err;

	from_name = dentry_name(from);
	if (from_name == NULL)
		return -ENOMEM;
	to_name = dentry_name(to);
	if (to_name == NULL) {
		__putname(from_name);
		return -ENOMEM;
	}
	err = -EINVAL;
	__putname(from_name);
	__putname(to_name);
	return err;
}

static const struct inode_operations dafs_dir_iops = {
	.create		= dafs_create,
	.lookup		= dafs_lookup,
	.link		= dafs_link,
	.unlink		= dafs_unlink,
	.mkdir		= dafs_mkdir,
	.rmdir		= dafs_rmdir,
	.rename		= dafs_rename,
};

static char *host_root_path = ".";

static int dafs_fill_sb_common(struct super_block *sb, void *d, int silent)
{
	struct inode *root_inode;
	int err;

	sb->s_blocksize = 1024;
	sb->s_blocksize_bits = 10;
	sb->s_magic = IMGDAFS_SUPER_MAGIC;
	sb->s_op = &dafs_sbops;
	sb->s_d_op = &dafs_dentry_ops;
	sb->s_maxbytes = MAX_LFS_FILESIZE;

	err = -ENOMEM;

	root_inode = new_inode(sb);
	if (!root_inode)
		goto out;

	err = read_name(root_inode, host_root_path);
	if (err)
		goto out_put;

	err = -ENOMEM;
	sb->s_fs_info = host_root_path;
	sb->s_root = d_make_root(root_inode);
	if (sb->s_root == NULL)
		goto out;

	return 0;

out_put:
	iput(root_inode);
out:
	return err;
}

static struct dentry *dafs_read_sb(struct file_system_type *type,
				   int flags, const char *dev_name,
				   void *data)
{
	if (!metag_da_enabled())
		return ERR_PTR(-ENODEV);
	return mount_nodev(type, flags, data, dafs_fill_sb_common);
}

static struct file_system_type dafs_type = {
	.owner		= THIS_MODULE,
	.name		= "imgdafs",
	.mount		= dafs_read_sb,
	.kill_sb	= kill_anon_super,
	.fs_flags	= 0,
};
MODULE_ALIAS_FS("imgdafs");

static int __init init_dafs(void)
{
	return register_filesystem(&dafs_type);
}

static void __exit exit_dafs(void)
{
	unregister_filesystem(&dafs_type);
}

module_init(init_dafs)
module_exit(exit_dafs)
MODULE_LICENSE("GPL");
