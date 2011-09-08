/*
 *	dvbloopback.c
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
 *
 *
 *	This driver creates a virtual dvb adaper which can forward all I/O to
 *	userspace or to another DVB card.  Thus, it is possible to clone an
 *	existing DVB card (why would you do that?), or to pre-process all data
 *	in userspace before sending it on.  It could also be used, in theory to
 *	send arbitrary data (hand-crafted stream) to a DVB app.  In addition, a
 *	mixed mode is allowed.  Each device can be set independantly, and in
 *	fact each type of I/O can be set independantly (i.e. read, write, and
 *	ioctl).  The allowed modes are: forward to userspace, forward to
 *	alternate dvb adapter, forward to alternate dvb adapter and send to
 *	userspace (for monitor only).
 *	The driver creates two devices of each type <dev>0 is the new virtual
 *	device, and <dev>1 is the interface to userspace.
 *
 *	This code is based off the video-loopback driver
 *	http://www.lavrsen.dk/twiki/bin/view/Motion/VideoFourLinuxLoopbackDevice
 *
 */


#define DVBLOOPBACK_VERSION "0.0.1"

/* Include files common to 2.4 and 2.6 versions */
#include <linux/version.h>	/* >= 2.6.14 LINUX_VERSION_CODE */ 
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/dvb/ca.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/version.h>
#include <linux/vmalloc.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
#include <linux/sched.h>
#endif

#include "dvbdev.h"
#include "dvbdevwrap.h"
#include "dvblb_internal.h"

#define	N_BUFFS	2	/* Number of buffers used for pipes */

#define info(format, arg...) printk(KERN_INFO __FILE__ ": " format "\n" "", ## arg)

#define dprintk if (dvblb_debug) printk
#define dprintk2 if (dvblb_debug > 1) printk
#define dprintk3 if (dvblb_debug > 2) printk

static int dvblb_debug = 0;
static int num_adapters = 1;
module_param(dvblb_debug, int, S_IRUGO | S_IWUSR);
module_param(num_adapters, int, S_IRUGO);

static struct dvblb *dvblb_global;

static void *rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (!mem)
		return NULL;
	memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	adr = (unsigned long) mem;
	while (size > 0) {
		SetPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return mem;
}

static void rvfree(void *mem, unsigned long size)
{
	unsigned long adr;

	if (!mem)
		return;

	adr = (unsigned long) mem;
	while ((long) size > 0) {
		ClearPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	vfree(mem);
}

/* This is a copy of dvb_usercopy.  We need to do this because it isn't exported
   by dvbdev
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
static int dvblb_usercopy(struct file *file,
 		     unsigned int cmd, unsigned long arg,
		     int (*func)(struct file *file,
 		     unsigned int cmd, void *arg))
#else
static int dvblb_usercopy(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg,
		     int (*func)(struct inode *inode, struct file *file,
		     unsigned int cmd, void *arg))
#endif
{
	char    sbuf[128];
	void    *mbuf = NULL;
	void    *parg = NULL;
	int     err  = -EINVAL;
#if DVB_API_VERSION >=5
	struct  dtv_properties *tvps = NULL;
	struct  dtv_property *tvp = NULL;
#endif

	/*  Copy arguments into temp kernel buffer  */
	switch (_IOC_DIR(cmd)) {
	case _IOC_NONE:
		/*
		 * For this command, the pointer is actually an integer
		 * argument.
		 */
		parg = (void *) arg;
		break;
	case _IOC_READ: /* some v4l ioctls are marked wrong ... */
	case _IOC_WRITE:
	case (_IOC_WRITE | _IOC_READ):
		if (_IOC_SIZE(cmd) <= sizeof(sbuf)) {
			parg = sbuf;
		} else {
			/* too big to allocate from stack */
			mbuf = kmalloc(_IOC_SIZE(cmd),GFP_KERNEL);
			if (NULL == mbuf)
				return -ENOMEM;
			parg = mbuf;
		}

		err = -EFAULT;
		if (copy_from_user(parg, (void __user *)arg, _IOC_SIZE(cmd)))
			goto out;
#if DVB_API_VERSION >=5
		if ((cmd == FE_SET_PROPERTY) || (cmd == FE_GET_PROPERTY)) {
		    tvps = (struct dtv_properties __user *)arg;
		    tvp = (struct dtv_property *) kmalloc(tvps->num *
			sizeof(struct dtv_property), GFP_KERNEL);
		    if (!tvp){
			err = -ENOMEM;
			goto out;
		    }
		    if (copy_from_user(tvp, tvps->props, 
			(tvps->num) * sizeof(struct dtv_property))) {
			err = -EFAULT;
			goto out;
		    }
		    tvps = (struct dtv_properties __user *)parg;
		    tvps->props = tvp;
		    tvp = NULL;
		}
#endif
		break;
	}

	/* call driver */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
	if ((err = func(file, cmd, parg)) == -ENOIOCTLCMD)
#else
	if ((err = func(inode, file, cmd, parg)) == -ENOIOCTLCMD)
#endif
		err = -EINVAL;

	if (err < 0)
		goto out;

	/*  Copy results into user buffer  */
	switch (_IOC_DIR(cmd))
	{
	case _IOC_READ:
	case (_IOC_WRITE | _IOC_READ):
#if DVB_API_VERSION >=5
		if ((cmd == FE_GET_PROPERTY) || (cmd == FE_SET_PROPERTY)) 
		{
		    tvps = (struct dtv_properties __user *)arg;
		    tvp = tvps->props;
		    tvps = (struct dtv_properties __user *)parg;
		    if (copy_to_user(tvp, tvps->props, tvps->num * 
			    sizeof(struct dtv_property))) {
 			err = -EFAULT;
			goto out;
		    }
		    tvps->props = tvp;
		}
#endif
		if (copy_to_user((void __user *)arg, parg, _IOC_SIZE(cmd)))
			err = -EFAULT;
		break;
	}

out:
	kfree(mbuf);
	return err;
}

static int get_new_filemap(struct dvblb_devinfo *lbdev, int id) {
	int i;
	for(i = 0; i < DVBLB_MAXFD; i++)
	{
		if(lbdev->filemap[i] == NULL) {
			if(id)
				lbdev->filemap[i] = lbdev->user_dev;
			else
				lbdev->filemap[i] = lbdev->lb_dev;
			return i;
		}
	}
	return -1;
}

static int find_filemap(struct dvblb_devinfo *lbdev, struct dvb_device **fm) {
	int i;
	for(i = 0; i < DVBLB_MAXFD; i++)
		if(&lbdev->filemap[i] == fm)
			return i;
	return -1;
}

int inuse_filemap(struct dvblb_devinfo *lbdev) {
	int i, count = 0;
	for(i = 0; i < DVBLB_MAXFD; i++)
		if(lbdev->filemap[i] != NULL)
			count++;
	return count;
}

static int dvblb_fake_ioctl(struct dvblb_devinfo *lbdev, struct dvb_device **f,
                            unsigned long int cmd, void *arg)
{
	int ret = 0;
#if DVB_API_VERSION >=5
	struct dtv_properties *tvps = NULL;
	long unsigned int _ioctllen;
#endif
	dprintk2("dvblb_fake_ioctl (%d) : %lu\n", lbdev->parent->adapter.num,
	         0xFF & cmd);
	if (mutex_lock_interruptible(&lbdev->lock_fake_ioctl))
		return -ERESTARTSYS;
	if (mutex_lock_interruptible(&lbdev->lock_ioctl)) {
		mutex_unlock(&lbdev->lock_fake_ioctl);
		return -ERESTARTSYS;
	}
	//printk("Sleep up on cmd: %u\n", cmd);
	lbdev->ioctlcmd = cmd;
	lbdev->ioctl_already_read = 0;
	if (cmd < DVBLB_MAX_CMDS) {
		// Read data command
		lbdev->ioctllen = sizeof(struct dvblb_custommsg);
	} else {
		// Regular ioctl
		lbdev->ioctllen = _IOC_SIZE(cmd);
	}

	lbdev->ioctlfd = f;
	lbdev->ioctldata = arg;
	//kill_proc(lbdev->pid, SIGIO, 1);

	mutex_unlock(&lbdev->lock_ioctl);
	wake_up_interruptible(&lbdev->wait_virt_poll);
	while(1) {
	        ret = wait_event_interruptible_timeout(lbdev->wait_ioctl,
			 lbdev->ioctlcmd == ULONG_MAX, HZ);
		dprintk2("fake-ioctl wait (%lu) returned: %d/%lu\n", cmd,
		         ret, lbdev->ioctlcmd);
		if(ret >= 0 && lbdev->ioctlcmd == ULONG_MAX)
			break;
		if(cmd == DVBLB_CMD_CLOSE) {
			dprintk("Received a signal."
			        "  Resending DVBLB_CLOSE message\n");
			wait_event_timeout(lbdev->wait_ioctl,
		                   lbdev->ioctlcmd == ULONG_MAX, HZ);
			break;
		}
		if(ret < 0 || lbdev->filemap[0] == NULL) {
			/* userspace driver has closed it would be nice if we
			   could do something about it here */
			printk("dvblb_fake_ioctl interrupted: %lu\n",
			       lbdev->ioctlcmd);
			mutex_unlock(&lbdev->lock_fake_ioctl);
			return -1;
		}
		dprintk("dvbloopback: timeout waiting for userspace\n");
	}

	if (mutex_lock_interruptible(&lbdev->lock_ioctl)) {
		mutex_unlock(&lbdev->lock_fake_ioctl);
		return -ERESTARTSYS;
	}
	//printk("Woke up on cmd: %u\n", cmd);
	if (lbdev->ioctlcmd != ULONG_MAX) {
		mutex_unlock(&lbdev->lock_ioctl);
		mutex_unlock(&lbdev->lock_fake_ioctl);
		printk("dvblb_fake_ioctl failed: %lu\n",lbdev->ioctlcmd);
		return -1;
	}

	if (cmd < DVBLB_MAX_CMDS || ! (cmd & IOC_IN)){
#if DVB_API_VERSION >=5
		if ((cmd == FE_GET_PROPERTY)) {
		    tvps = (struct dtv_properties __user *)(lbdev->ioctlretdata + sizeof(int));
		    _ioctllen = lbdev->ioctllen + (tvps->num * sizeof(struct dtv_property));
		    memcpy(arg, lbdev->ioctlretdata + sizeof(int), _ioctllen);
		    tvps = (struct dtv_properties __user *)arg;
		    tvps->props = (struct dtv_property __user *)(arg + lbdev->ioctllen);
		}
		else
#endif
		    memcpy(arg, lbdev->ioctlretdata + sizeof(int), lbdev->ioctllen);
	}

	ret = lbdev->ioctlretval;
	mutex_unlock(&lbdev->lock_ioctl);
	mutex_unlock(&lbdev->lock_fake_ioctl);
	return ret;
}

static int dvblb_open(struct inode *inode, struct file *f)
{
	struct dvb_device *dvbdev;
	struct dvblb_devinfo *lbdev;
	int ret, map;

	dvbdev  = (struct dvb_device *) f->private_data;
	if (dvbdev == NULL) {
		printk("Failed to open device\n");
		return -EFAULT;
	}
	lbdev = (struct dvblb_devinfo *)dvbdev->priv;
	if (lbdev == NULL) {
		printk("Failed to find private data during open\n");
		return -EFAULT;
	}
	dprintk("dvblb_open %d%s%d\n", lbdev->parent->adapter.num,
	        dnames[dvbdev->type], dvbdev->id);

	if(dvbdev->id == 0) {
		/* This is the looped device */
		struct dvblb_custommsg ci;

		/*must update private_data so we can map fds properly */
		if (mutex_lock_interruptible(&lbdev->lock_ioctl))
			return -ERESTARTSYS;
		map = get_new_filemap(lbdev, 0);
		if (map < 0) {
			printk("dvblb_open %s: Failed to set filemap\n",
			       dnames[dvbdev->type]);
			mutex_unlock(&lbdev->lock_ioctl);
			return -EBUSY;
		}
		try_module_get(THIS_MODULE);
		lbdev->dbgfilemap[map] = f;
		f->private_data = &lbdev->filemap[map];
		lbdev->poll_waiting = 0;
		mutex_unlock(&lbdev->lock_ioctl);
		dprintk("dvblb_open %s: fd: %d\n", dnames[dvbdev->type], map);
		if (lbdev->forward_dev) {
			ret = dvblb_forward_open(lbdev, inode, f);
			if(ret < 0) {
				module_put(THIS_MODULE);
				lbdev->filemap[map] = NULL;
			}
			return ret;
		}

		if (lbdev->pid == -1) {
			module_put(THIS_MODULE);
			lbdev->filemap[map] = NULL;
			return -EFAULT;
		}
		ci.type = DVBLB_OPEN;
		ci.u.mode = f->f_flags;
		ret = dvblb_fake_ioctl(lbdev, &lbdev->filemap[map],
		                       DVBLB_CMD_OPEN, &ci);
		if(ret < 0) {
			module_put(THIS_MODULE);
			lbdev->filemap[map] = NULL;
		}

		return ret;
	}
	/* This is the userspace control device */
	if(lbdev->forward_dev)
		return -EFAULT;
	ret = dvb_generic_open(inode, f);
	if (ret >= 0) {
		lbdev->pid = current->pid;
		lbdev->ioctlcmd = ULONG_MAX;
		lbdev->ioctllen = 0;
		mutex_lock(&lbdev->lock_ioctl);
		memset(lbdev->filemap, 0, sizeof(lbdev->filemap));
		map = get_new_filemap(lbdev, 1);
		if (map < 0) {
			printk("dvblb_open %s: Failed to set filemap\n",
			       dnames[dvbdev->type]);
			mutex_unlock(&lbdev->lock_ioctl);
			return -EBUSY;
		}
		try_module_get(THIS_MODULE);
		f->private_data = &lbdev->filemap[map];
                lbdev->dbgfilemap[map] = f;
		lbdev->poll_waiting = 0;
		mutex_unlock(&lbdev->lock_ioctl);
		dprintk("dvblb_open %s: fd: %d\n", dnames[dvbdev->type], map);
	}
	return ret;
}

static int dvblb_release(struct inode *inode, struct file *f)
{
	struct dvb_device *dvbdev, **filemap;
	struct dvblb_devinfo *lbdev;
	int ret;

	filemap  = (struct dvb_device **) f->private_data;
	if(filemap == NULL) {
		printk("Failed to locate device\n");
		return -EFAULT;
	}
	dvbdev = *filemap;
	if (dvbdev == NULL) {
		printk("Failed to locate device\n");
		return -EFAULT;
	}
	lbdev = (struct dvblb_devinfo *)dvbdev->priv;
	if (lbdev == NULL) {
		printk("Failed to find private data during close\n");
		return -EFAULT;
	}
	dprintk("dvblb_release %d%s%d fd:%d\n", lbdev->parent->adapter.num,
	        dnames[dvbdev->type],
	        dvbdev->id, find_filemap(lbdev, filemap));

	if(dvbdev->id == 0) {
		/* This is the looped device */
		struct dvblb_custommsg ci;

		if (lbdev->forward_dev) {
			ret = dvblb_forward_release(lbdev, f);
			*filemap = NULL;
			goto out;
		}

		if (lbdev->pid == -1) {
			ret = -EFAULT;
			goto out;
		}
		ci.type = DVBLB_CLOSE;
		ret = dvblb_fake_ioctl(lbdev, filemap, DVBLB_CMD_CLOSE, &ci);
		if (mutex_lock_interruptible(&lbdev->lock_ioctl)) {
			ret = -ERESTARTSYS;
			goto out;
		}
		lbdev->poll_waiting = 0;
		*filemap = NULL;
		mutex_unlock(&lbdev->lock_ioctl);
		goto out;
	}
	/* This is the userspace control device */
	*filemap = NULL;
	f->private_data = dvbdev;
	ret = dvb_generic_release(inode, f);
	if (ret < 0) {
		goto out;
	}
	lbdev->pid = -1;
        if (mutex_lock_interruptible(&lbdev->lock_buffer)) {
                ret = -ERESTARTSYS;
                goto out;
        }
	if (lbdev->buffer) {
		rvfree(lbdev->buffer, lbdev->buflen*N_BUFFS);
		lbdev->buffer = NULL;
	}
	mutex_unlock(&lbdev->lock_buffer);
	ret = 0;
	goto out;

out:
	module_put(THIS_MODULE);
	return ret;
}

static ssize_t dvblb_write(struct file *f, const char *buf,
		size_t count, loff_t *offset)
{
	struct dvb_device *dvbdev, **filemap;
	struct dvblb_devinfo *lbdev;

	filemap  = (struct dvb_device **) f->private_data;
	if(filemap == NULL) {
		printk("Failed to locate device\n");
		return -EFAULT;
	}
	dvbdev = *filemap;
	if (dvbdev == NULL) {
		printk("Failed to locate device\n");
		return -EFAULT;
	}
	lbdev = (struct dvblb_devinfo *)dvbdev->priv;
	if (lbdev == NULL) {
		printk("Failed to find private data during close\n");
		return -EFAULT;
	}
	dprintk3("dvblb_write %d%s%d fd:%d\n", lbdev->parent->adapter.num,
	        dnames[dvbdev->type],
	        dvbdev->id, find_filemap(lbdev, filemap));

	if (lbdev->forward_dev)
		return dvblb_forward_write(lbdev, f, buf, count, offset);

	printk("Write not supported on loopback device\n");
	return -EFAULT;
}

static ssize_t dvblb_read (struct file *f, char * buf, size_t count, loff_t *offset)
{
	struct dvb_device *dvbdev, **filemap;
	struct dvblb_devinfo *lbdev;
#if DVB_API_VERSION >=5
	struct dtv_properties *tvps = NULL;
#endif
	unsigned long int _count = 0;

	filemap  = (struct dvb_device **) f->private_data;
	if(filemap == NULL) {
		printk("Failed to locate device\n");
		return -EFAULT;
	}
	dvbdev = *filemap;
	if (dvbdev == NULL) {
		printk("Failed to find device\n");
		return -EFAULT;
	}
	lbdev = (struct dvblb_devinfo *)dvbdev->priv;
	if (lbdev == NULL) {
		printk("Failed to find private data during read\n");
		return -EFAULT;
	}
	dprintk3("dvblb_read %d%s%d fd:%d\n", lbdev->parent->adapter.num,
	        dnames[dvbdev->type],
	        dvbdev->id, find_filemap(lbdev, filemap));
	if(dvbdev->id == 0) {
		/* This is the looped device */
		struct dvblb_custommsg ci;

		if (lbdev->forward_dev)
			return dvblb_forward_read(lbdev, f, buf, count, offset);

		ci.type = DVBLB_READ;
		ci.u.count = count;
		if (lbdev->pid == -1)
			return -EFAULT;
		if (mutex_lock_interruptible(&lbdev->lock_buffer))
			return -ERESTARTSYS;
		if(dvblb_fake_ioctl(lbdev, filemap, DVBLB_CMD_READ, &ci) < 0 ||
		   ! lbdev->buffer) {
			mutex_unlock(&lbdev->lock_buffer);
			return 0;
		}
		if (ci.u.count > lbdev->buflen)
			ci.u.count = lbdev->buflen;
		if (copy_to_user(buf, lbdev->buffer, ci.u.count)) {
			mutex_unlock(&lbdev->lock_buffer);
			return -EFAULT;
		}
		mutex_unlock(&lbdev->lock_buffer);
		dprintk3("Read %ld bytes\n", (long)ci.u.count);
		return ci.u.count;
	} else {
		/* This is the userspace control device */
		/* We pass ioctls to the userspace driver this way */
		unsigned long int cmd, base_size, size;
		base_size = sizeof(cmd) + sizeof(lbdev->ioctlfd);
		if (mutex_lock_interruptible(&lbdev->lock_ioctl))
			return -ERESTARTSYS;

		cmd = lbdev->ioctlcmd;
		size = (lbdev->ioctllen > sizeof(int)) ? lbdev->ioctllen :
		                                         sizeof(int);
#if DVB_API_VERSION >=5
		if ((cmd == FE_GET_PROPERTY) || (cmd == FE_SET_PROPERTY)){
		    tvps = (struct dtv_properties __user *)(lbdev->ioctldata);
		    _count = size + base_size + (tvps->num * sizeof(struct dtv_property));
		}
		else
#endif
		    _count = base_size + size;
		    
		if (count < _count || lbdev->ioctlcmd == ULONG_MAX ||
                    lbdev->ioctl_already_read) {
			mutex_unlock(&lbdev->lock_ioctl);
			return -EFAULT;
		}
		if (cmd == ULONG_MAX)
		    count = base_size;
		else
		    count = _count;

		if (copy_to_user(buf, &cmd, sizeof(cmd))) {
			mutex_unlock(&lbdev->lock_ioctl);
			return -EFAULT;
		}

		if (copy_to_user(buf + sizeof(cmd), &lbdev->ioctlfd,
		                 sizeof(lbdev->ioctlfd))) {
			mutex_unlock(&lbdev->lock_ioctl);
			return -EFAULT;
		}

		if (lbdev->ioctllen == 0) {
			if (copy_to_user(buf + base_size, &lbdev->ioctldata,
			                 size)) {
				mutex_unlock(&lbdev->lock_ioctl);
				return -EFAULT;
			}
			
		} else {
#if DVB_API_VERSION >=5
			if ((cmd == FE_GET_PROPERTY) || (cmd == FE_SET_PROPERTY)) {    
			    if (copy_to_user(buf + base_size + size, tvps->props,
			                 tvps->num * sizeof(struct dtv_property))) {
				mutex_unlock(&lbdev->lock_ioctl);
				return -EFAULT;
			    }
			    tvps->props = (struct dtv_property __user *)(buf + base_size + size);
			}
#endif
			if (copy_to_user(buf + base_size, lbdev->ioctldata,
			                 size)) {
				mutex_unlock(&lbdev->lock_ioctl);
				return -EFAULT;
			}
		}
		lbdev->ioctl_already_read = 1;
		mutex_unlock(&lbdev->lock_ioctl);
		return count;
	}
}

/* The following routine gets called by dvblb_usercopy (used to be 
   dvb_generic_ioctl) which is called by dvblb_ioctl for device-0.  It is
   used to forward ioctl commands back to the userspace application
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
static int dvblb_looped_ioctl(struct file *f,
 	unsigned int cmd, void *parg)
#else
static int dvblb_looped_ioctl(struct inode *inode, struct file *f,
	unsigned int cmd, void *parg)
#endif
{
	int ret;
        struct dvb_device *dvbdev, **filemap;
        struct dvblb_devinfo *lbdev;

	filemap  = (struct dvb_device **) f->private_data;
	if(filemap == NULL) {
		printk("Failed to locate device\n");
		return -EFAULT;
	}
	dvbdev = *filemap;
        if (dvbdev == NULL) {
                printk("Failed to locate device\n");
                return -EFAULT;
        }
        lbdev = (struct dvblb_devinfo *)dvbdev->priv;
        if (lbdev == NULL) {
                printk("Failed to find private data during ioctl\n");
                return -EFAULT;
        }
	dprintk("dvblb_ioctl %d%s%d fd:%d\n", lbdev->parent->adapter.num,
	        dnames[dvbdev->type],
	        dvbdev->id, find_filemap(lbdev, filemap));
	ret = dvblb_fake_ioctl(lbdev, filemap, cmd, parg);
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
static long dvblb_ioctl(struct file *f,
 	unsigned int cmd, unsigned long arg)
#else
static int dvblb_ioctl(struct inode *inode, struct file *f,
	unsigned int cmd, unsigned long arg)
#endif
{
	void * parg = (void *)arg;
	struct dvb_device *dvbdev, **filemap;
	struct dvblb_devinfo *lbdev;
#if DVB_API_VERSION >=5
	struct dtv_properties *tvps;
#endif
	int size;

	filemap  = (struct dvb_device **) f->private_data;
	if(filemap == NULL) {
		printk("Failed to locate device\n");
		return -EFAULT;
	}
	dvbdev = *filemap;
	if (dvbdev == NULL) {
		printk("Failed to locate device\n");
		return -EFAULT;
	}
	lbdev = (struct dvblb_devinfo *)dvbdev->priv;
	if (lbdev == NULL) {
		printk("Failed to find private data during ioctl\n");
		return -EFAULT;
	}
	if(dvbdev->id == 0) {
		/* This is the looped device */
		if (lbdev->forward_dev)
			return dvblb_forward_ioctl(lbdev, f, cmd, arg);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
		return dvblb_usercopy (f, cmd, arg,
 		                       dvbdev->kernel_ioctl);
#else
		return dvblb_usercopy (inode, f, cmd, arg,
		                       dvbdev->kernel_ioctl);
#endif
	}
	/* This is the userspace control device */
	dprintk2("dvblb_ioctl %d%s%d fd:%d cmd:%x\n",lbdev->parent->adapter.num,
	        dnames[dvbdev->type],
	        dvbdev->id, find_filemap(lbdev, filemap), cmd);
	if (cmd == DVBLB_CMD_ASYNC) {
		/* async command return */
		int pos, i;
                struct dvblb_pollmsg pollmsg;
		if (copy_from_user(&pollmsg, parg,
		                   sizeof(struct dvblb_pollmsg))) {
			printk("Failed to read pollmsg from ioctl\n");
			return -EFAULT;
		}
		if (mutex_lock_interruptible(&lbdev->lock_ioctl))
			return -ERESTARTSYS;
		if (pollmsg.count == 0) {
			/* Send wakeup to all waiting polls */
			/*
			printk("Waking all fds on %s\n", dnames[dvbdev->type]);
			*/
			lbdev->poll_waiting = 0;
			for (i = 0; i < DVBLB_MAXFD; i++)
				if(lbdev->filemap[i] != 0)
					wake_up_interruptible(
					                &lbdev->wait_poll[i]);
		} 
		for (i = 0; i < pollmsg.count; i++) {
			pos = find_filemap(lbdev, pollmsg.file[i]);
			if (pos >= 0) {
				/*
				printk("Sending wakeup command to %p, pos:%d\n",
				       lbdev->filemap[pos], pos);
				*/
				lbdev->poll_waiting &= ~(1<<pos);
				wake_up_interruptible(&lbdev->wait_poll[pos]);
			}
		}
		mutex_unlock(&lbdev->lock_ioctl);
		return 0;
	}
	if (mutex_lock_interruptible(&lbdev->lock_ioctl))
		return -ERESTARTSYS;
	if (cmd != lbdev->ioctlcmd) {
		// Incorrect response
		printk("dvbloopback: Got wrong response (%u != %lu)\n", cmd,
		       lbdev->ioctlcmd);
		mutex_unlock(&lbdev->lock_ioctl);
		return 0;
	}
	if (cmd < DVBLB_MAX_CMDS)
		size = sizeof(int) + sizeof(struct dvblb_custommsg);
	else if (cmd & IOC_IN)
		size = sizeof(int);
	else
		size = sizeof(int) + _IOC_SIZE(cmd);

	if (copy_from_user(lbdev->ioctlretdata, parg, size)) {
		mutex_unlock(&lbdev->lock_ioctl);
		return -EFAULT;
	}
#if DVB_API_VERSION >=5
	if (cmd == FE_GET_PROPERTY)
	{    
	    tvps = (struct dtv_properties __user *)(lbdev->ioctlretdata + sizeof(int));    
	    if (copy_from_user(lbdev->ioctlretdata + size, tvps->props,
	                 tvps->num * sizeof(struct dtv_property))) 
	    {
 		mutex_unlock(&lbdev->lock_ioctl);
 		return -EFAULT;
	    }
	    tvps = (struct dtv_properties __user *)(lbdev->ioctlretdata + sizeof(int));
	    tvps->props = (struct dtv_property __user *)(lbdev->ioctlretdata + size);
	    dprintk("%s() copy_from_user: cmd %u\n", __func__, tvps->props[0].cmd);
 	}
#endif

	lbdev->ioctlretval = *(int *)lbdev->ioctlretdata;
	lbdev->ioctlcmd = ULONG_MAX;
	mutex_unlock(&lbdev->lock_ioctl);
	wake_up(&lbdev->wait_ioctl);
	return 0;
}

static int dvblb_mmap(struct file *f, struct vm_area_struct *vma)
{
	struct dvb_device *dvbdev, **filemap;
	struct dvblb_devinfo *lbdev;
	unsigned long start = (unsigned long)vma->vm_start;
	long size = vma->vm_end - vma->vm_start;
	unsigned long page, pos;

	filemap  = (struct dvb_device **) f->private_data;
	if(filemap == NULL) {
		printk("Failed to locate device\n");
		return -EFAULT;
	}
	dvbdev = *filemap;
	if (dvbdev == NULL) {
		printk("Failed to locate device\n");
		return -EFAULT;
	}
	lbdev = (struct dvblb_devinfo *)dvbdev->priv;
	if (lbdev == NULL) {
		printk("Failed to find private data during mmap\n");
		return -EFAULT;
	}
	dprintk("dvblb_mmap %d%s%d fd:%d\n", lbdev->parent->adapter.num,
	        dnames[dvbdev->type],
	        dvbdev->id, find_filemap(lbdev, filemap));
	if(dvbdev->id == 0) {
		/* This is the looped device */
		if (lbdev->forward_dev)
			return dvblb_forward_mmap(lbdev, f, vma);

		return -EINVAL;
	}
	/* This is the userspace control device */
	if (!size)
		return -EINVAL;
	if (mutex_lock_interruptible(&lbdev->lock_buffer))
		return -ERESTARTSYS;
	if (lbdev->buffer)
		rvfree(lbdev->buffer, lbdev->buflen*N_BUFFS);
	lbdev->buflen=size;
	lbdev->buffer=rvmalloc(lbdev->buflen*N_BUFFS);

	if (lbdev->buffer == NULL) {
		mutex_unlock(&lbdev->lock_buffer);
		return -EINVAL;
	}

        if (size > (((N_BUFFS * lbdev->buflen) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))) {
		mutex_unlock(&lbdev->lock_buffer);
                return -EINVAL;
	}

	pos = (unsigned long)lbdev->buffer;
        while (size > 0) {
		page = vmalloc_to_pfn((void *)pos);
		if (remap_pfn_range(vma, start, page, PAGE_SIZE,
				 PAGE_SHARED)) {
			mutex_unlock(&lbdev->lock_buffer);
                        return -EAGAIN;
		}
                start += PAGE_SIZE;
		pos += PAGE_SIZE;
		size -= PAGE_SIZE;
        }
	mutex_unlock(&lbdev->lock_buffer);

	return 0;
}

static unsigned int dvblb_poll(struct file *f, struct poll_table_struct *wait)
{
	struct dvb_device *dvbdev, **filemap;
	struct dvblb_devinfo *lbdev;

	filemap  = (struct dvb_device **) f->private_data;
	if(filemap == NULL) {
		printk("Failed to locate device\n");
		return -EFAULT;
	}
	dvbdev = *filemap;
	if (dvbdev == NULL) {
		printk("Failed to locate device\n");
		return -EFAULT;
	}
	lbdev = (struct dvblb_devinfo *)dvbdev->priv;
	if (lbdev == NULL) {
		printk("Failed to find private data during poll\n");
		return -EFAULT;
	}
	dprintk3("dvblb_poll %d%s%d: fd:%d\n", lbdev->parent->adapter.num,
	         dnames[dvbdev->type],
	         dvbdev->id, find_filemap(lbdev, filemap));
	if(dvbdev->id == 0) {
		/* This is the looped device */
		struct dvblb_custommsg ci;
		int ret;
		int do_poll = 1;
		int pos;

		if (lbdev->forward_dev)
			return dvblb_forward_poll(lbdev, f, wait);

		if (lbdev->pid == -1) {
			printk("no pid found!\n");
			return -EFAULT;
		}
		pos = find_filemap(lbdev, filemap);
		if (pos == -1) {
			printk("dvblb_poll %s: Didn't find filemap!\n",
			       dnames[dvbdev->type]);
			return -EFAULT;
		}
		poll_wait(f, &lbdev->wait_poll[pos], wait);

		if (mutex_lock_interruptible(&lbdev->lock_ioctl))
			return -ERESTARTSYS;
		if(lbdev->poll_waiting & (1 << pos))
			do_poll = 0;
		else /* need to set it before the fake_ioctl to prevent race */
			lbdev->poll_waiting |= (1 << pos);
		mutex_unlock(&lbdev->lock_ioctl);

		if(do_poll) {
			ci.type = DVBLB_POLL;
			ci.u.mode = (wait == NULL) ? 0 : 1;
			ret = dvblb_fake_ioctl(lbdev, filemap, DVBLB_CMD_POLL,
			                       &ci);
			if(ret != 0) {
				if (mutex_lock_interruptible(&lbdev->lock_ioctl))
					return -ERESTARTSYS;
				lbdev->poll_waiting &= ~(1 << pos);
				mutex_unlock(&lbdev->lock_ioctl);
				if(ret < 0)
					return ret;
			}
			if (ci.u.mode & POLLIN)
				ci.u.mode |= POLLRDNORM;
		} else {
			ci.u.mode = 0;
			/*
			printk("skipping poll on %p (%d)\n", f, pos);
			*/
		}
	dprintk3("dvblb_poll %d%s%d: fd:%d returned: %d\n",
	         lbdev->parent->adapter.num,
	         dnames[dvbdev->type], dvbdev->id, pos, ci.u.mode);
		return(ci.u.mode);
	}

	poll_wait(f, &lbdev->wait_virt_poll, wait);
	if (mutex_lock_interruptible(&lbdev->lock_ioctl))
		return -ERESTARTSYS;
	if (lbdev->ioctlcmd != ULONG_MAX && ! lbdev->ioctl_already_read) {
		mutex_unlock(&lbdev->lock_ioctl);
		return (POLLIN | POLLPRI | POLLRDNORM);
	}
	mutex_unlock(&lbdev->lock_ioctl);
	return 0;
}

static struct file_operations dvbdev_looped_fops = {
	.owner		= THIS_MODULE,
	.open		= dvblb_open,
	.release	= dvblb_release,
	.read		= dvblb_read,
	.write		= dvblb_write,
	.poll		= dvblb_poll,
	.mmap		= dvblb_mmap,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
	.unlocked_ioctl	= dvblb_ioctl,
#else
	.ioctl		= dvblb_ioctl,
#endif
};

static struct dvb_device dvbdev_looped = {
        .priv = NULL,
        .users = DVBLB_MAXFD,
        .readers = 1,
        .writers = 1,
	.fops = &dvbdev_looped_fops,
	.kernel_ioctl  = dvblb_looped_ioctl,
};

static struct file_operations dvbdev_userspace_fops = {
	.owner		= THIS_MODULE,
	.open		= dvblb_open,
	.release	= dvblb_release,
	.read		= dvblb_read,
	.write		= dvblb_write,
	.poll		= dvblb_poll,
	.mmap		= dvblb_mmap,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
	.unlocked_ioctl	= dvblb_ioctl,
#else
	.ioctl		= dvblb_ioctl,
#endif
};

static struct dvb_device dvbdev_userspace = {
        .priv = NULL,
        .users = 1,
        .readers = 1,
        .writers = 1,
	.fops = &dvbdev_userspace_fops,
	.kernel_ioctl  = dvblb_looped_ioctl,
};

static int create_lb_dev(struct dvblb *dvblb, int dev_idx, int type)
{
	int ret;
	struct dvblb_devinfo *lbdev = &dvblb->devinfo[dev_idx];

// create loopback device (will be frontend0)
	ret = dvb_register_device(&dvblb->adapter, &lbdev->lb_dev,
	                    &dvbdev_looped, lbdev, type);
	if (ret != 0) {
		info("error registering device adapter%d",
		     dvblb->adapter.num);
		return ret;
	}
// create userspace device (will be frontend1)
	ret = dvb_register_device(&dvblb->adapter, &lbdev->user_dev,
	                    &dvbdev_userspace, lbdev, type);
	if (ret != 0) {
		info("error registering device adapter%d",
		     dvblb->adapter.num);
		dvb_unregister_device(lbdev->lb_dev);
		return ret;
	}
// initialize all data
	//lbdev->ioctldata=kmalloc(1024, GFP_KERNEL);
	//if(lbdev->ioctldata == NULL)
	//{
	//	dvb_unregister_device(lbdev->lb_dev);
	//	dvb_unregister_device(lbdev->user_dev);
	//	return -ENOMEM;
	//}
	lbdev->parent = dvblb;
	lbdev->ioctlretdata=kmalloc(5*1024, GFP_KERNEL);
	lbdev->pid = -1;
	lbdev->buffer = NULL;
	lbdev->buflen = 0;
	lbdev->ioctlcmd = ULONG_MAX;
	lbdev->ioctllen = 0;
	lbdev->ioctl_already_read = 1;
	lbdev->forward_dev = NULL;
	memset(lbdev->filemap, 0, sizeof(lbdev->filemap));
	memset(lbdev->forwardmap, 0, sizeof(lbdev->forwardmap));

	lbdev->poll_waiting = 0;
	dvblb_init_procfs_device(dvblb, lbdev);

	{
		int i;
		for(i = 0; i < DVBLB_MAXFD; i++)
			init_waitqueue_head(&lbdev->wait_poll[i]);
	}
	init_waitqueue_head(&lbdev->wait_virt_poll);
	init_waitqueue_head(&lbdev->wait_ioctl);
	mutex_init(&lbdev->lock_fake_ioctl);
	mutex_init(&lbdev->lock_ioctl);
	mutex_init(&lbdev->lock_buffer);
	dvblb->init |= (1 << dev_idx);
	return 0;
}

static int destroy_lb_dev(struct dvblb *dvblb, int dev_idx)
{
	if (0 == (dvblb->init & (1 << dev_idx)))
		return 0;
	dvb_unregister_device(dvblb->devinfo[dev_idx].lb_dev);
	dvb_unregister_device(dvblb->devinfo[dev_idx].user_dev);
	if (dvblb->devinfo[dev_idx].buffer)
		rvfree(dvblb->devinfo[dev_idx].buffer,
		       dvblb->devinfo[dev_idx].buflen*N_BUFFS);
	//kfree(lbdev->ioctldata);
	kfree(dvblb->devinfo[dev_idx].ioctlretdata);
	dvblb_remove_procfs(dvblb->devinfo[dev_idx].procfile, dvblb->procdir);
	dvblb->init &= ~(1 << dev_idx);
	return 0;
}

static int destroy_lb_adapter(struct dvblb *dvblb)
{
	destroy_lb_dev(dvblb, DVBLB_FRONTEND);
	destroy_lb_dev(dvblb, DVBLB_DEMUX);
	destroy_lb_dev(dvblb, DVBLB_DVR);
	destroy_lb_dev(dvblb, DVBLB_VIDEO);
	destroy_lb_dev(dvblb, DVBLB_AUDIO);
	destroy_lb_dev(dvblb, DVBLB_OSD);

	dvblb_remove_procfs_adapter(dvblb);
	dvb_unregister_adapter(&dvblb->adapter);
	dvblb->init &= ~DVBLB_STATUS_ADAPTER;

	return 0;
}
/****************************************************************************
 *	init stuff
 ****************************************************************************/


MODULE_AUTHOR("Alan Nisota");
MODULE_DESCRIPTION("DVB loopback device.");
MODULE_LICENSE("GPL");
MODULE_VERSION( DVBLOOPBACK_VERSION );

static struct platform_device *dvblb_basedev;
	
static int __init dvblb_init(void)
{
	int i,ret, failed;
	i=0;
	failed=0;

	dvblb_init_procfs();

	info("frontend loopback driver v"DVBLOOPBACK_VERSION);
	if (num_adapters < 1 || num_adapters > DVBLB_MAX_ADAPTERS) {
		printk("dvbloopback: param num_adapters=%d. Must be between"
		       " 1 and %d\n", num_adapters, DVBLB_MAX_ADAPTERS);
		return -EFAULT;
	}
	printk("dvbloopback: registering %d adapters\n", num_adapters);

	dvblb_global = kmalloc(sizeof(struct dvblb) * num_adapters, GFP_KERNEL);
	if (dvblb_global == NULL)
		return -ENOMEM;
	for(i=0; i < num_adapters; i++)
		dvblb_global[i].init = 0;
 
	dvblb_basedev = platform_device_alloc("dvbloopback", -1);
	if (!dvblb_basedev) {
		kfree(dvblb_global);
		return -ENOMEM;
	}
	ret = platform_device_add(dvblb_basedev);
	if (ret) {
		platform_device_put(dvblb_basedev);
		kfree(dvblb_global);
		return ret;
	}
	for(i=0; i < num_adapters; i++) {
		struct dvblb *this_adptr = &dvblb_global[i];
		// register new adapter (happens in main driver)
		if (wrap_dvb_reg_adapter(&this_adptr->adapter,
		                         "DVB-LOOPBACK", NULL) < 0) {
			failed = 1;
			break;
		}
		this_adptr->init |= DVBLB_STATUS_ADAPTER;

		// NOTE: we use the next line to fetch the start-node for the 
		// the adapter linked-list.  However it relies on us grabbing it
		// before another device is added (i.e. this adapter must be the
		// last on the list.
		if (0 == i) {
			this_adptr->adapter_ll =
			                this_adptr->adapter.list_head.next;
		} else {
			this_adptr->adapter_ll = dvblb_global[0].adapter_ll;
		}
		this_adptr->link = -1;
		dvblb_init_procfs_adapter(this_adptr);

		ret = create_lb_dev(this_adptr, DVBLB_FRONTEND,
		                    DVB_DEVICE_FRONTEND);
		if (ret != 0) {
			info("Failed to add loopback for adapter");
			failed = 1;
			break;
		}

		ret = create_lb_dev(this_adptr, DVBLB_DEMUX,
	                    DVB_DEVICE_DEMUX);
		if (ret != 0) {
			info("Failed to add loopback for adapter");
			failed = 1;
			break;
		}

		ret = create_lb_dev(this_adptr, DVBLB_DVR,
		                    DVB_DEVICE_DVR);
		if (ret != 0) {
			info("Failed to add loopback for adapter");
			failed = 1;
			break;
		}

		ret = create_lb_dev(this_adptr, DVBLB_VIDEO,
		                    DVB_DEVICE_VIDEO);
		if (ret != 0) {
			info("Failed to add loopback for adapter");
			failed = 1;
			break;
		}

		ret = create_lb_dev(this_adptr, DVBLB_AUDIO,
		                    DVB_DEVICE_AUDIO);
		if (ret != 0) {
			info("Failed to add loopback for adapter");
			failed = 1;
			break;
		}

		ret = create_lb_dev(this_adptr, DVBLB_OSD,
		                    DVB_DEVICE_OSD);
		if (ret != 0) {
			info("Failed to add loopback for adapter");
			failed = 1;
			break;
		}
	}

	if (failed) {
		for(i = 0; i < num_adapters; i++) {
			if(0 == dvblb_global[i].init)
				break;
			destroy_lb_adapter(&dvblb_global[i]);
		}
		platform_device_unregister(dvblb_basedev);
		kfree(dvblb_global);
		return -EFAULT;
	}
	return 0;
}

static void __exit cleanup_dvblb_module(void)
{
	int i;
	info("Unregistering ca loopback devices");
	for(i = 0; i < num_adapters; i++) {
		if(0 == dvblb_global[i].init)
			break;
		destroy_lb_adapter(&dvblb_global[i]);
	}
	platform_device_unregister(dvblb_basedev);
	kfree(dvblb_global);
	dvblb_uninit_procfs();
}

module_init(dvblb_init);
module_exit(cleanup_dvblb_module);
