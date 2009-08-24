#include <linux/wait.h>
#include <linux/proc_fs.h>
#include "dvbdev.h"
#include "dvbloopback.h"

#define DVBLB_MAX_ADAPTERS 8

struct dvblb;

struct dvblb_fwdmap {
	struct dvb_device **f;
	struct file *map;
};

struct dvblb_devinfo {
	struct dvblb      *parent;
	struct dvb_device *lb_dev;
	struct dvb_device *user_dev;
	int                pid;
	unsigned long int  ioctlcmd;
	int                ioctlretval;
	unsigned char     *ioctldata;
	unsigned char     *ioctlretdata;
	unsigned int       ioctllen;
	unsigned int       ioctl_already_read;
	void              *ioctlfd;
	unsigned char     *buffer;
	unsigned long int  buflen;
	wait_queue_head_t  wait_ioctl;
	wait_queue_head_t  wait_virt_poll;
	struct mutex   lock_fake_ioctl;
	struct mutex   lock_ioctl;
	struct mutex   lock_buffer;

	wait_queue_head_t    wait_poll[DVBLB_MAXFD];
	unsigned long int    poll_waiting;
	struct dvb_device    *filemap[DVBLB_MAXFD];
	void                 *dbgfilemap[DVBLB_MAXFD];

	struct dvb_device   *forward_dev;
	struct dvblb_fwdmap  forwardmap[DVBLB_MAXFD];

	struct proc_dir_entry *procfile;
};

enum
{
	DVBLB_FRONTEND = 0,
	DVBLB_DEMUX    = 1,
	DVBLB_DVR      = 2,
	DVBLB_VIDEO    = 3,
	DVBLB_AUDIO    = 4,
	DVBLB_OSD      = 5,
	DVBLB_NUM_DEVS = 6
};
#define DVBLB_STATUS_FRONTEND (1 << DVBLB_FRONTEND)
#define DVBLB_STATUS_DEMUX    (1 << DVBLB_DEMUX)
#define DVBLB_STATUS_DVR      (1 << DVBLB_DVR)
#define DVBLB_STATUS_ADAPTER  0x0100U
#define DVBLB_STATUS_PROC     0x0200U

struct dvblb {
	struct dvb_adapter adapter;
	struct dvblb_devinfo devinfo[DVBLB_NUM_DEVS];

	struct proc_dir_entry *procdir;
	struct proc_dir_entry *procfile;
	int                    link;
	struct list_head *adapter_ll;
	unsigned int init;
};
extern int inuse_filemap(struct dvblb_devinfo *lbdev);

/* dvblb_proc.c */
extern int dvblb_remove_procfs(struct proc_dir_entry *pdir,
                        struct proc_dir_entry *parent);

extern int dvblb_init_procfs_device(struct dvblb *dvblb,
                                    struct dvblb_devinfo *lbdev);

extern int dvblb_init_procfs_adapter(struct dvblb *dvblb);

extern int dvblb_remove_procfs_adapter(struct dvblb *dvblb);

extern int dvblb_init_procfs(void);

extern int dvblb_uninit_procfs(void);

/* dvblb_forward.c */
extern int dvblb_forward_open(struct dvblb_devinfo *lbdev, struct inode *inode,
                       struct file *f);

extern int dvblb_forward_release(struct dvblb_devinfo *lbdev, struct file *f);

extern ssize_t dvblb_forward_write(struct dvblb_devinfo *lbdev, struct file *f,
                            const char *buf, size_t count, loff_t *offset);

extern ssize_t dvblb_forward_read (struct dvblb_devinfo *lbdev, struct file *f,
                            char * buf, size_t count, loff_t *offset);

extern int dvblb_forward_ioctl(struct dvblb_devinfo *lbdev, struct file *f,
                        unsigned int cmd, unsigned long arg);

extern int dvblb_forward_mmap(struct dvblb_devinfo *lbdev, struct file *f,
                       struct vm_area_struct *vma);

extern int dvblb_forward_poll(struct dvblb_devinfo *lbdev, struct file *f,
                       struct poll_table_struct *wait);

