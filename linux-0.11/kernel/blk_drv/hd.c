/*
 *  linux/kernel/hd.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This is the low-level hd interrupt support. It traverses the
 * request-list, using interrupts to jump between functions. As
 * all the functions are called within interrupts, we may not
 * sleep. Special care is recommended.
 * 
 *  modified by Drew Eckhardt to check nr of hd's from the CMOS.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR 3
#include "blk.h"

/* Max read/write errors/sector */
#define MAX_ERRORS	7
#define MAX_HD		2

static void recal_intr(void);

static int recalibrate = 0;
static int reset = 0;

/*
 *  This struct defines the HD's and their types.
 */
struct hd_i_struct {
	int cyl;    /* cylinders numbers */
	int head;   /* head numbers */
	int sect;   /* sector numbers per track */
	int wpcom;  /* write pre-comp cylinder num */
	int lzone;  /* landing zone cylinder num */
	int ctl;    /* control byte */
		    /*	bit 0: not used */
		    /*	bit 1: reserved(0)(disable IRQ) */
		    /*	bit 2: permit resume */
		    /*	bit 3: set to 1 if head numbers > 8 */
		    /*	bit 4: not used(0) */
		    /*	bit 5: set to 1 if cyl + 1 has vendors bad block map */
		    /*	bit 6: forbid ECC retry */
		    /*	bit 7: forbid access retry */
};

#ifdef HD_TYPE
struct hd_i_struct hd_info[] = {HD_TYPE};
#define NR_HD ((sizeof(hd_info)) / (sizeof(struct hd_i_struct)))
#else
struct hd_i_struct hd_info[] = {
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0}
};
static int NR_HD = 0;
#endif

static struct hd_struct {
	long start_sect;
	long nr_sects;
} hd[5 * MAX_HD] = {{0, 0}};

/* Input (E)CX words from port DX into ES:[(E)DI] */
#define port_read(port, buf, nr) \
__asm__("cld; rep; insw"::"d" (port), "D" (buf), "c" (nr))

/* Output (E)CX words from DS:[(E)SI] to port DX */
#define port_write(port, buf, nr) \
__asm__("cld; rep; outsw"::"d" (port), "S" (buf), "c" (nr))

extern void hd_interrupt(void);
extern void ramdisk_load(void);

/* This may be used only once, enforced by 'static int callable' */
int sys_setup(void * BIOS)
{
	static int callable = 1;
	int i, drive;
	unsigned char cmos_disks;
	struct partition *p;
	struct buffer_head *bh;

	/* enforce this func execute only once */
	if (!callable)
		return -1;
	callable = 0;

#ifndef HD_TYPE	/* currenty not define HD_TYPE */
	printk("HD_TYPE no defined, get drive info from BIOS\n");
	for (drive = 0; drive < 2; drive++) {
		hd_info[drive].cyl = *(unsigned short *)BIOS;
		hd_info[drive].head = *(unsigned char *)(2 + BIOS);
		hd_info[drive].wpcom = *(unsigned short *)(5 + BIOS);
		hd_info[drive].ctl = *(unsigned char *)(8 + BIOS);
		hd_info[drive].lzone = *(unsigned short *)(12 + BIOS);
		hd_info[drive].sect = *(unsigned char *)(14 + BIOS);
		BIOS += 16; /* each drive param table is 16 byte long */
	}

	if (hd_info[1].cyl)
		NR_HD = 2;
	else
		NR_HD = 1;
#endif
	/* calculate hard disk total sectors */
	for (i = 0; i < NR_HD; i++) {
		hd[i * 5].start_sect = 0;
		hd[i * 5].nr_sects = 
			hd_info[i].head * hd_info[i].sect * hd_info[i].cyl;
	}

	/* 
	 * We querry CMOS about hard disks : it could be that we have a
	 * SCSI/ESDI/etc controller that is BIOS compatable with ST-506, and
	 * thus showing up in our BIOS table, but not register compatable, and
	 * therefore not present in CMOS.
	 *
	 * Furthurmore, we will assume that our ST-506 drives <if any> are the
	 * primary drives in the system, and the ones reflected as drive 1 or 2.
	 *
	 * The first drive is stored in the high nibble of CMOS byte 0x12, the
	 * second in the low nibble.  This will be either a 4 bit drive type or
	 * 0xf indicating use byte 0x19 for an 8 bit type, drive 1, 0x1a for
	 * drive 2 in CMOS.
	 *
	 * Needless to say, a non-zero value means we have an AT controller hard
	 * disk for that drive.
	 */
	if ((cmos_disks = CMOS_READ(0x12)) & 0xf0) {
		if (cmos_disks & 0x0f)
			NR_HD = 2;
		else
			NR_HD = 1;
	} else {
		NR_HD = 0;
	}

	printk("There are %d hard disk in system\n", NR_HD);

	/* reset un-exist hd[] structure */
	for (i = NR_HD; i < 2; i++) {
		hd[i * 5].start_sect = 0;
		hd[i * 5].nr_sects = 0;
	}

	/* fetch partiton info */
	for (drive = 0; drive < NR_HD; drive++) {
		/* read 1st block (Minix fs: 1 block = 2 sectors = 1KB) */
		if (!(bh = bread(0x300 + drive * 5, 0))) {
			printk("Unable to read partition table of drive %d\n",
				drive);
			panic("");
		}

		/* validate if it is the boot sector */
		if (bh->b_data[510] != 0x55 ||
			((unsigned char)bh->b_data[511]) != 0xaa) {
			printk("Bad partition table on drive %d\n", drive);
			panic("");
		}

		/* partition table is at 0x1be of first block */
		p = (void *)bh->b_data + 0x01be;

		/* save each partition's start/total sectors information */
		for (i = 1; i < 5; i++, p++) {
			hd[drive * 5 + i].start_sect = p->start_sect;
			hd[drive * 5 + i].nr_sects = p->nr_sects;
			printk("partition %d of HD %d start sector: %ld, nr_sects: %ld\n",
				i, drive + 1, p->start_sect, p->nr_sects);
		}

		brelse(bh);
	}
	
	if (NR_HD)
		printk("Read partition table%s ok.\n", (NR_HD > 1) ? "s" : "");

	/* currently we don't use ramdisk. Nail 2016/02/22 */
	ramdisk_load();

	printk("ROOT_DEV: 0x%x\n", ROOT_DEV);
	printk("Mounting root filesystem...\n");
	mount_root();

	return 0;
}

static int controller_ready(void)
{
	int retries = 100000;

	/* if controller is busy, bit 7 of HD_STATUS will set to 1 */
	while (--retries && (inb_p(HD_STATUS) & 0x80))
		/* nothing */;

	return retries;
}

/* check hard disk command result, "win" means winchester hard disk */
static int win_result(void)
{
	int i = inb_p(HD_STATUS);

	if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT))
		== (READY_STAT | SEEK_STAT))
		return 0; /* ok */

	/* if ERR_STAT bit is set, read error register */
	if (i & ERR_STAT)
		i = inb(HD_ERROR);

	return 1;
}

static void hd_out(unsigned int drive, unsigned int nsect,
		unsigned int start_sect, unsigned int head, unsigned int cyl,
		unsigned int cmd, void (*intr_addr)(void))
{
	register int port;

	if (drive > 1 || head > 15)
		panic("Trying to write bad sector");

	if (!controller_ready())
		panic("HD controller not ready");

	/* setup interrupt handler */
	do_hd = intr_addr;

	/* send contorl command first */
	outb_p(hd_info[drive].ctl, HD_CMD);

	/* fill param */
	port = HD_DATA;
	outb_p(hd_info[drive].wpcom >> 2, ++port);
	outb_p(nsect, ++port);
	outb_p(start_sect, ++port);
	outb_p(cyl, ++port);
	outb_p(cyl >> 8, ++port);
	outb_p(0xa0 | (drive << 4) | head, ++port);

	/* issue cmd(WIN_READ/WIN_WRITE/...) */
	outb(cmd, ++port);
}

static int drive_busy(void)
{
	unsigned int i;

	for (i = 0; i < 10000; i++)
		if (READY_STAT == (inb_p(HD_STATUS) & (BUSY_STAT|READY_STAT)))
			break;
	i = inb(HD_STATUS);
	i &= BUSY_STAT | READY_STAT | SEEK_STAT;
	if (i == (READY_STAT | SEEK_STAT))
		return(0);
	printk("HD controller times out\n\r");
	return(1);
}

static void reset_controller(void)
{
	int i;

	outb(4, HD_CMD);
	for(i = 0; i < 100; i++)
		nop();

	outb(hd_info[0].ctl & 0x0f, HD_CMD);

	if (drive_busy())
		printk("HD-controller still busy\n");

	if ((i = inb(HD_ERROR)) != 1)
		printk("HD-controller reset failed: %02x\n", i);
}

static void reset_hd(int nr)
{
	reset_controller();
	hd_out(nr, hd_info[nr].sect, hd_info[nr].sect, hd_info[nr].head - 1,
		hd_info[nr].cyl, WIN_SPECIFY, recal_intr);
}

void unexpected_hd_interrupt(void)
{
	printk("Unexpected HD interrupt\n");
}

static void bad_rw_intr(void)
{
	if (++CURRENT_REQ->errors >= MAX_ERRORS)
		end_request(0);

	if (CURRENT_REQ->errors > MAX_ERRORS / 2)
		reset = 1;
}

static void read_intr(void)
{
	/* check previous read command success or not */
	if (win_result()) {
		bad_rw_intr();
		do_hd_request();
		return;
	}

	/* read data from hard disk from data register */
	port_read(HD_DATA, CURRENT_REQ->buffer, 256);

	CURRENT_REQ->errors = 0;
	CURRENT_REQ->buffer += 512;
	CURRENT_REQ->start_sector++;

	if (--CURRENT_REQ->nr_sectors) {
		do_hd = &read_intr;
		return;
	}

	end_request(1);
	do_hd_request();
}

static void write_intr(void)
{
	if (win_result()) {
		bad_rw_intr();
		do_hd_request();
		return;
	}

	if (--CURRENT_REQ->nr_sectors) {
		CURRENT_REQ->start_sector++;
		CURRENT_REQ->buffer += 512;
		do_hd = &write_intr;
		port_write(HD_DATA, CURRENT_REQ->buffer, 256);
		return;
	}

	end_request(1);	    /* this time request done */
	do_hd_request();    /* do other hd request */
}

static void recal_intr(void)
{
	if (win_result())
		bad_rw_intr();
	do_hd_request();
}

void do_hd_request(void)
{
	int i, r = 0;
	unsigned int start_sector, dev, partition;
	unsigned int start_sec, head, cyl;
	unsigned int nsect;

	CHECK_REQUEST;
	partition = MINOR(CURRENT_REQ->dev);  /* get partition */
	start_sector = CURRENT_REQ->start_sector;
	nsect = CURRENT_REQ->nr_sectors;

	/* need to check if nsect is exceed the partition limit or not */
	if (partition >= 5 * NR_HD || nsect > hd[partition].nr_sects) {
		end_request(0);
		goto repeat;	/* repeat defined in blk.h */
	}

	start_sector += hd[partition].start_sect;
	dev = partition / 5;

	/* get cyl, head, start_sec number according start_sector */
	/* div result: EAX = Quotient, EDX = Remainder */
	/* 
	 * start_sector / sect nrs per track = total track number(start_sector) 
	 * ... remainder sector number(start_sec)
	 */
	__asm__("divl %4"
		:"=a" (start_sector), "=d" (start_sec)
		:"0" (start_sector), "1" (0), "r" (hd_info[dev].sect));

	/* totoal track number / total head nrs = cylinder number(cyl)
	 * ... head nr(head) */
	__asm__("divl %4"
		:"=a" (cyl), "=d" (head)
		:"0" (start_sector), "1" (0), "r" (hd_info[dev].head));

	start_sec++;

	if (reset) {
		reset = 0;
		recalibrate = 1;
		reset_hd(CURRENT_DEV);
		return;
	}

	if (recalibrate) {
		recalibrate = 0;
		hd_out(dev, hd_info[CURRENT_DEV].sect, 0, 0, 0, WIN_RESTORE, 
			recal_intr);
		return;
	}

	if (CURRENT_REQ->cmd == WRITE) {
		hd_out(dev, nsect, start_sec, head, cyl, WIN_WRITE, write_intr);

		/* wait DRQ_STAT signal */
		for(i = 0; i < 3000 && !(r = inb_p(HD_STATUS) & DRQ_STAT); i++)
			/* nothing */ ;

		if (!r) {
			bad_rw_intr();
			goto repeat;
		}

		/* write 512 byte (1 sector) to HD */
		port_write(HD_DATA, CURRENT_REQ->buffer, 256);
	} else if (CURRENT_REQ->cmd == READ) {
		hd_out(dev, nsect, start_sec, head, cyl, WIN_READ, read_intr);
	} else {
		panic("unknown hd-command");
	}
}

void hd_init(void)
{
	/* setup hd handler function */
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;	/* do_hd_request */

	/* enable hd interrupt (IRQ14) */
	set_intr_gate(0x2e, &hd_interrupt);
	outb_p(inb_p(0x21) & 0xfb, 0x21);
	outb(inb_p(0xa1) & 0xbf, 0xa1);
}
