/*
 *	dvblb_forward.c
 *
 *	DVBLoopback Copyright Alan Nisota 2006
 *
 *	Portions of this code also have copyright:
 *	Video Loopback Copyright Jeroen Vreeken (pe1rxq@amsat.org), 2000
 *
 *	Published under the GNU Public License.
 *	DVBLoopback is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	DVBLoopback is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with Foobar; if not, write to the Free Software
 *	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <linux/version.h>      /* >= 2.6.14 LINUX_VERSION_CODE */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/cdev.h>
#include <linux/err.h>
#include "dvblb_internal.h"

#define nums2minor(num,type,id) ((num << 6) | (id << 4) | type)

// forward to correct device
static struct file *find_forwardmap(struct dvblb_devinfo *lbdev,
                                    struct dvb_device **f)
{
	int i;
	for(i = 0; i < DVBLB_MAXFD; i++)
		if(lbdev->forwardmap[i].f == f)
			return lbdev->forwardmap[i].map;
	return ERR_PTR(-1);
}

static int set_forwardmap(struct dvblb_devinfo *lbdev, struct dvb_device **f,
                          struct file *map) {
	int i;
	for(i = 0; i < DVBLB_MAXFD; i++)
		if(lbdev->forwardmap[i].f == NULL) {
			lbdev->forwardmap[i].f = f;
			lbdev->forwardmap[i].map = map;
			return i;
		}
	return -1;
}

static void clear_forwardmap(struct dvblb_devinfo *lbdev, struct dvb_device **f)
{
	int i;
	for(i = 0; i < DVBLB_MAXFD; i++)
		if(lbdev->forwardmap[i].f == f) {
			lbdev->forwardmap[i].f = NULL;
			return;
		}
}

int dvblb_forward_open(struct dvblb_devinfo *lbdev, struct inode *inode,
                       struct file *f)
{
//	int minor;
//	struct inode *tmpinode;
//	struct dentry *dentry;
	struct file *ftmp;
	char tmpstr[35];

	if (! inode->i_cdev || ! inode->i_cdev->ops ||
	    ! inode->i_cdev->ops->open) {
		printk("dvblb_forward_open: "
		       "device wasn't correctly initialized\n");
		return -EFAULT;
	}
	sprintf(tmpstr, "/dev/dvb/adapter%d/%s%d", lbdev->forward_dev->adapter->num, dnames[lbdev->forward_dev->type], lbdev->forward_dev->id);
	/* printk("linking to %s\n", tmpstr); */
	ftmp = filp_open(tmpstr, f->f_flags, f->f_mode);

	/* It would be nice to find a way to open the device without relying
	   on a fixed path, but the attempt below certainly isn't it */
/*
	minor = nums2minor(lbdev->forward_dev->adapter->num,
	                   lbdev->forward_dev->type, lbdev->forward_dev->id);
	tmpinode = iget_locked(inode->i_sb, MKDEV(DVB_MAJOR, minor));
	if (! tmpinode)
		return -EFAULT;
	if (is_bad_inode(tmpinode)) {
		iput(tmpinode);
		return -EFAULT;
	}
	dentry = d_alloc_anon(tmpinode);
	if (!dentry) {
		iput(tmpinode);
		return -EFAULT;
	}
	ftmp = dentry_open(dentry, f->f_vfsmnt, f->f_flags);
*/
	if (!ftmp || IS_ERR(ftmp)) {
		int fd = PTR_ERR(ftmp);
		printk("open failed: %d\n", fd);
		return fd;
	}
	if (! ftmp->f_op || ! lbdev->forward_dev->fops ||
	    ftmp->f_op->open !=  lbdev->forward_dev->fops->open) {
		printk("DVB device din't initialize correctly\n");
		filp_close(ftmp, NULL);
		return -EFAULT;
	}
	if (set_forwardmap(lbdev, f->private_data, ftmp) == -1) {
		printk("Didn't find a valid forwardmap\n");
		filp_close(ftmp, NULL);
		return -EFAULT;
	}
	return 0;
}

int dvblb_forward_release(struct dvblb_devinfo *lbdev, struct file *f)
{
	struct file *ftmp = find_forwardmap(lbdev, f->private_data);
	if (!ftmp || IS_ERR(ftmp))
		return -EFAULT;
	if (lbdev->forward_dev->fops &&lbdev->forward_dev->fops->release) {
		filp_close(ftmp, NULL);
		clear_forwardmap(lbdev, f->private_data);
	}
	return -EFAULT;
}

ssize_t dvblb_forward_write(struct dvblb_devinfo *lbdev, struct file *f,
                            const char *buf, size_t count, loff_t *offset)
{
	struct file *ftmp = find_forwardmap(lbdev, f->private_data);
	if (!ftmp || IS_ERR(ftmp))
		return -EFAULT;
	if (lbdev->forward_dev->fops &&lbdev->forward_dev->fops->write)
		return lbdev->forward_dev->fops->write(
		           ftmp, buf, count, offset);
	return -EFAULT;
}

ssize_t dvblb_forward_read (struct dvblb_devinfo *lbdev, struct file *f,
                            char * buf, size_t count, loff_t *offset)
{
	struct file *ftmp = find_forwardmap(lbdev, f->private_data);
	if (!ftmp || IS_ERR(ftmp))
		return -EFAULT;
	if (lbdev->forward_dev->fops &&lbdev->forward_dev->fops->read)
		return lbdev->forward_dev->fops->read(
		           ftmp, buf, count, offset);
	return -EFAULT;
}

int dvblb_forward_ioctl(struct dvblb_devinfo *lbdev, struct file *f,
                        unsigned int cmd, unsigned long arg)
{
	struct file *ftmp = find_forwardmap(lbdev, f->private_data);
	if (!ftmp || IS_ERR(ftmp))
		return -EFAULT;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
	if (lbdev->forward_dev->fops &&lbdev->forward_dev->fops->unlocked_ioctl)
		return lbdev->forward_dev->fops->unlocked_ioctl(
		           ftmp, cmd, arg);
#else
	if (lbdev->forward_dev->fops &&lbdev->forward_dev->fops->ioctl)
		return lbdev->forward_dev->fops->ioctl(
		           ftmp->f_dentry->d_inode, ftmp, cmd, arg);
#endif
	return -EFAULT;
}

int dvblb_forward_mmap(struct dvblb_devinfo *lbdev, struct file *f,
                       struct vm_area_struct *vma)
{
	struct file *ftmp = find_forwardmap(lbdev, f->private_data);
	if (!ftmp || IS_ERR(ftmp))
		return -EFAULT;
	if (lbdev->forward_dev->fops &&lbdev->forward_dev->fops->mmap)
		return lbdev->forward_dev->fops->mmap(ftmp, vma);
	return -EFAULT;
}

int dvblb_forward_poll(struct dvblb_devinfo *lbdev, struct file *f,
                       struct poll_table_struct *wait)
{
	struct file *ftmp = find_forwardmap(lbdev, f->private_data);
	if (!ftmp || IS_ERR(ftmp))
		return -EFAULT;
	if (lbdev->forward_dev->fops &&lbdev->forward_dev->fops->poll)
		return lbdev->forward_dev->fops->poll(ftmp, wait);
	return -EFAULT;
}
