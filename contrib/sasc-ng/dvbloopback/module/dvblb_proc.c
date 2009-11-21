/*
 *	dvblb_proc.c
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

#include <linux/proc_fs.h>
#include "dvblb_internal.h"

static struct proc_dir_entry *procdir;

static int dvblb_procfs_read(char *page, char **start, off_t off, int count,
                             int *eof, void *data)
{
	struct dvblb_devinfo *lbdev = (struct dvblb_devinfo *)data;
	int val;
	if (lbdev == NULL)
		return 0;
	val = (lbdev->forward_dev) ? 1 : 0;
	return sprintf(page, "%03d", val);
}

static int dvblb_procfs_write(struct file *file, const char *buffer,
                              unsigned long count, void *data)
{
	char str[10];
	int val, v1, v2, v3, fm;
	struct dvblb_devinfo *lbdev = (struct dvblb_devinfo *)data;
	if (lbdev == NULL)
		return count;
	if (lbdev->parent->link == -1)
		return count;
	if (count > 10)
		count = 10;
	if (copy_from_user(str, buffer, count)) {
		return -EFAULT;
	}
	val = simple_strtoul(str, NULL, 0);
	v1 = val /100;
	v2 = (val - v1*100) / 10;
	v3 = val - v1*100 - v2*10;
	if (v1 < 0 || v1 > 2) 
		return -EFAULT;
	if((fm = inuse_filemap(lbdev))) {
		int type = lbdev->lb_dev->type;
		printk("dvbloopback: Can't change forward on adapter%d."
		       " Device %s still has %d users!\n",
		       lbdev->parent->adapter.num, dnames[type], fm);
		return count;
	}
	if (v3 == 1) {
		struct list_head *entry;
		list_for_each (entry, lbdev->parent->adapter_ll) {
			struct dvb_adapter *adap;
			adap = list_entry (entry, struct dvb_adapter,
			                   list_head);
			if (adap->num == lbdev->parent->link) {
				struct list_head *entry0;
				list_for_each (entry0,
				               &adap->device_list) {
					struct dvb_device *dev;
					dev = list_entry (entry0,
					       struct dvb_device, list_head);
					if (dev->type == lbdev->lb_dev->type) {
						lbdev->forward_dev = dev;
						return count;
					}
				}
			}
		}
	} else if (v3 == 0) {
		lbdev->forward_dev = NULL;
	}
	return count;
}

static int dvblb_procfs_adapter_read(char *page, char **start, off_t off,
                                     int count, int *eof, void *data)
{
	struct dvblb *dvblb = (struct dvblb *)data;
	if (dvblb == NULL)
		return 0;
	return sprintf(page, "%d", dvblb->link);
}

static int dvblb_procfs_adapter_write(struct file *file, const char *buffer,
                                      unsigned long count, void *data)
{
	char str[10];
	int val, i, fm;
	struct dvblb *dvbdev = (struct dvblb *)data;
	if (dvbdev == NULL)
		return count;
	if (count > 10)
		count = 10;
	if (copy_from_user(str, buffer, count)) {
		return -EFAULT;
	}
	val = simple_strtol(str, NULL, 0);

	if(val == -999) {
		/*This is a debug case.  Try to force close all open fds
		  This is known not to be very reliable, but better than
		  nothing
		*/
		for(i = 0; i < DVBLB_NUM_DEVS; i++) {
			while((fm = inuse_filemap(&dvbdev->devinfo[i]))) {
				filp_close(dvbdev->devinfo[i].dbgfilemap[fm],
				           NULL);
				dvbdev->devinfo[i].filemap[fm] = NULL;
			}
		}
		return count;
	}
	for(i = 0; i < DVBLB_NUM_DEVS; i++) {
		if(dvbdev->devinfo[i].forward_dev == NULL)
			continue;
		if((fm = inuse_filemap(&dvbdev->devinfo[i]))) {
			int type = dvbdev->devinfo[i].lb_dev->type;
			printk("dvbloopback: Can't change forward on adapter%d."
			       " Device %s still has %d users!\n",
			       dvbdev->adapter.num, dnames[type], fm);
			return count;
		}
	}
	for(i = 0; i < DVBLB_NUM_DEVS; i++)
		dvbdev->devinfo[i].forward_dev = NULL;
	dvbdev->link = val;
	return count;
}

int dvblb_remove_procfs(struct proc_dir_entry *pdir,
                        struct proc_dir_entry *parent)
{
	char name[20];
	memcpy(name, pdir->name, pdir->namelen);
	name[pdir->namelen] = '\0';
	// printk("Removing proc: %s\n", name);
	remove_proc_entry(name, parent);
	return 0;
}
EXPORT_SYMBOL(dvblb_remove_procfs);

int dvblb_init_procfs_device(struct dvblb *dvblb, struct dvblb_devinfo *lbdev)
{
	int type = lbdev->lb_dev->type;
	lbdev->procfile = create_proc_entry(dnames[type], 0644, dvblb->procdir);
	if (lbdev->procfile == NULL)
		return -ENOMEM;
	lbdev->procfile->data = lbdev;
	lbdev->procfile->read_proc = dvblb_procfs_read;
	lbdev->procfile->write_proc = dvblb_procfs_write;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
	lbdev->procfile->owner = THIS_MODULE;
#endif
	return 0;
}
EXPORT_SYMBOL(dvblb_init_procfs_device);

int dvblb_init_procfs_adapter(struct dvblb *dvblb)
{
	char name[10];
	sprintf(name, "adapter%d", dvblb->adapter.num);
	dvblb->procdir = proc_mkdir(name, procdir);
	if (dvblb->procdir == NULL)
		return -ENOMEM;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
	dvblb->procdir->owner = THIS_MODULE;
#endif
	dvblb->procfile = create_proc_entry("adapter", 0644, dvblb->procdir);
	if (dvblb->procfile == NULL) {
		dvblb_remove_procfs(dvblb->procdir, procdir);
		return -ENOMEM;
	}
	dvblb->procfile->data = dvblb;
	dvblb->procfile->read_proc = dvblb_procfs_adapter_read;
	dvblb->procfile->write_proc = dvblb_procfs_adapter_write;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
	dvblb->procfile->owner = THIS_MODULE;
#endif
	dvblb->init |= DVBLB_STATUS_PROC;

	return 0;
}
EXPORT_SYMBOL(dvblb_init_procfs_adapter);

int dvblb_remove_procfs_adapter(struct dvblb *dvblb)
{
	if(dvblb->init & DVBLB_STATUS_PROC) {
		dvblb_remove_procfs(dvblb->procfile, dvblb->procdir);
		dvblb_remove_procfs(dvblb->procdir, procdir);
		printk("removing dvblb proc adapter\n");
	}
	dvblb->init &= ~DVBLB_STATUS_PROC;
	printk("dvblb init = %x\n", dvblb->init);
	return 0;
}
EXPORT_SYMBOL(dvblb_remove_procfs_adapter);

int dvblb_init_procfs(void)
{
	procdir = proc_mkdir("dvbloopback", NULL);
	if (procdir == NULL)
		return -ENOMEM;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
	procdir->owner = THIS_MODULE;
#endif
	return 0;
}
EXPORT_SYMBOL(dvblb_init_procfs);

int dvblb_uninit_procfs(void)
{
	dvblb_remove_procfs(procdir, NULL);
	return 0;
}
EXPORT_SYMBOL(dvblb_uninit_procfs);

