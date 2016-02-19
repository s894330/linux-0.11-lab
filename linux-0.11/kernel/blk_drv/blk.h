#ifndef _BLK_H
#define _BLK_H

#define NR_BLK_DEV	7
/*
 * NR_REQUEST is the number of entries in the request-queue.
 * NOTE that writes may use only the low 2/3 of these: reads
 * take precedence.
 *
 * 32 seems to be a reasonable number: enough to get some benefit
 * from the elevator-mechanism, but not so much as to lock a lot of
 * buffers when they are in the queue. 64 seems to be too many (easily
 * long pauses in reading when heavy writing/syncing is going on)
 */
#define NR_REQUEST	32

/*
 * Ok, this is an expanded form so that we can use the same request for paging
 * requests when that is implemented. In paging, 'bh' is NULL, and 'waiting' is
 * used to wait for read/write completion.
 */
struct request {
	int dev;		/* -1 if no request */
	int cmd;		/* READ or WRITE */
	int errors;
	unsigned long start_sector;	/* sector number start to read */
	unsigned long nr_sectors;
	char *buffer;
	struct task_struct *waiting;
	struct buffer_head *bh;
	struct request *next;
};

/*
 * This is used in the elevator algorithm: Note that
 * reads always go before writes. This is natural: reads
 * are much more time-critical than writes.
 */
/* check which is first than the other */
#define IN_ORDER(s1, s2) \
	((s1)->cmd < (s2)->cmd || /* read is first than write */ \
	 ((s1)->cmd == (s2)->cmd && \
	  ((s1)->dev < (s2)->dev || /* min dev number is first */ \
	   /* min start sector number is first*/ \
	   ((s1)->dev == (s2)->dev && (s1)->start_sector < (s2)->start_sector))))

struct blk_dev_struct {
	void (*request_fn)(void);
	struct request *current_request;
};

extern struct blk_dev_struct blk_dev[NR_BLK_DEV];
extern struct request request[NR_REQUEST];
extern struct task_struct *wait_for_request;

#ifdef MAJOR_NR

/*
 * Add entries as needed. Currently the only block devices
 * supported are hard-disks and floppies.
 */

#if (MAJOR_NR == 1)
/* ram disk */
#define DEVICE_NAME "ramdisk"
#define DEVICE_REQUEST do_rd_request
#define DEVICE_NR(device) ((device) & 7)
#define DEVICE_ON(device) 
#define DEVICE_OFF(device)

#elif (MAJOR_NR == 2)
/* floppy */
#define DEVICE_NAME "floppy"
#define DEVICE_INTR do_floppy
#define DEVICE_REQUEST do_fd_request
#define DEVICE_NR(device) ((device) & 3)
#define DEVICE_ON(device) floppy_on(DEVICE_NR(device))
#define DEVICE_OFF(device) floppy_off(DEVICE_NR(device))

#elif (MAJOR_NR == 3)
/* harddisk */
#define DEVICE_NAME "harddisk"
#define DEVICE_INTR do_hd
#define DEVICE_REQUEST do_hd_request
#define DEVICE_NR(device) (MINOR(device) / 5)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#else
/* unknown blk device */
#error "unknown blk device"

#endif	/* MAJOR_NR */

#define CURRENT_REQ (blk_dev[MAJOR_NR].current_request)
#define CURRENT_DEV DEVICE_NR(CURRENT_REQ->dev)

#ifdef DEVICE_INTR
void (*DEVICE_INTR)(void) = NULL;
#endif	/* DEVICE_INTR */
static void (DEVICE_REQUEST)(void);

/* 
 * "static inline" means "we have to have this function, if you use it but
 * don't inline it, then make a static version of it in this compilation unit"
 */
static inline void unlock_buffer(struct buffer_head *bh)
{
	if (!bh->b_lock)
		printk(DEVICE_NAME ": free buffer being unlocked\n");

	bh->b_lock = 0;
	wake_up(&bh->b_wait);
}

static inline void end_request(int uptodate)
{
	DEVICE_OFF(CURRENT_REQ->dev);

	if (CURRENT_REQ->bh) {
		CURRENT_REQ->bh->b_uptodate = uptodate;
		unlock_buffer(CURRENT_REQ->bh);
	}

	if (!uptodate) {
		printk(DEVICE_NAME " I/O error\n\r");
		printk("dev %04x, block %d\n\r", CURRENT_REQ->dev,
			CURRENT_REQ->bh->b_blocknr);
	}

	wake_up(&CURRENT_REQ->waiting);
	wake_up(&wait_for_request);
	CURRENT_REQ->dev = -1;
	CURRENT_REQ = CURRENT_REQ->next;
}

#define CHECK_REQUEST \
repeat: \
	if (!CURRENT_REQ) \
		return; \
	if (MAJOR(CURRENT_REQ->dev) != MAJOR_NR) \
		panic(DEVICE_NAME ": request list destroyed"); \
	if (CURRENT_REQ->bh) { \
		if (!CURRENT_REQ->bh->b_lock) \
			panic(DEVICE_NAME ": block not locked"); \
	}

#endif	/* MAJOR_NR */

#endif	/* _BLK_H */
