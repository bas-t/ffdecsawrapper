#ifndef DVBLOOPBACK_H
#define DVBLOOPBACK_H

#include <linux/version.h>

#define DVBLB_MAXFD 96

static const char * const dnames[] = {
        "video", "audio", "sec", "frontend", "demux", "dvr", "ca",
        "net", "osd"
};

typedef enum dvblb_type {
	DVBLB_OPEN,
	DVBLB_CLOSE,
	DVBLB_READ,
	DVBLB_WRITE,
	DVBLB_POLL,
} dvblb_type_t;

enum {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
	DVBLB_CMD_OPEN = 0,
#else
	//pawel5870: Workaround for kernel 2.6.31
	DVBLB_CMD_OPEN = 4,
#endif
	DVBLB_CMD_CLOSE,
	DVBLB_CMD_READ,
	DVBLB_CMD_WRITE,
	DVBLB_CMD_POLL,
	DVBLB_CMD_ASYNC,
	DVBLB_MAX_CMDS,
};

struct dvblb_custommsg {
	dvblb_type_t	type;
	union {
		unsigned int mode;
		size_t       count;
	} u;
};

struct dvblb_pollmsg {
	int count;
	void *file[DVBLB_MAXFD];
};
#endif

