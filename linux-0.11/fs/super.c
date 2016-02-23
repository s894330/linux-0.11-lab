/*
 *  linux/fs/super.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include <errno.h>
#include <sys/stat.h>

int sync_dev(int dev);
void wait_for_keypress(void);

/* test_bit uses setb, as gas doesn't recognize setc */
#define test_bit(bitnr, addr) ({ \
	register int __res; \
	__asm__("bt %2, %3\n\t" /* store bit value of %3 offset %2 into CF */ \
		"setb %%al" /* set al according to value of CF, CF = 1 => al = 1 */ \
		:"=a" (__res):"a" (0), "r" (bitnr), "m" (*(addr))); \
	__res; \
})

struct super_block super_block[NR_SUPER];
/* this is initialized in init/main.c */
int ROOT_DEV = 0;

static void lock_super(struct super_block *sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sb->s_lock = 1;
	sti();
}

static void unlock_super(struct super_block *sb)
{
	cli();
	sb->s_lock = 0;
	wake_up(&(sb->s_wait));
	sti();
}

static void wait_on_super(struct super_block *sb)
{
	cli();	/* this cli() only effect current process */
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sti();
}

struct super_block *get_super(int dev)
{
	struct super_block *s;

	if (!dev)
		return NULL;

	/* check if dev's super block is already in super_block[] */
	s = super_block;
	while (s < super_block + NR_SUPER) {
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)
				return s;
			/* 
			 * s->s_dev has been changed during wait_on_super, loop
			 * check again
			 */
			s = super_block;
		} else {
			s++;
		}
	}

	return NULL;
}

void put_super(int dev)
{
	struct super_block * sb;
	/* struct m_inode * inode;*/
	int i;

	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}
	if (!(sb = get_super(dev)))
		return;
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}
	lock_super(sb);
	sb->s_dev = 0;
	for(i=0;i<I_MAP_SLOTS;i++)
		brelse(sb->s_imap[i]);
	for(i=0;i<Z_MAP_SLOTS;i++)
		brelse(sb->s_zmap[i]);
	unlock_super(sb);
	return;
}

static void show_super_block_detail(struct super_block *s)
{
	printk("    --- super block of 0x%x detail ---\n"
		"\tmagic: 0x%x\n"
		"\tinode numbers: %d\n"
		"\tzone numbers: %d\n"
		"\timap numbers: %d\n"
		"\tzmap numbers: %d\n"
		"\tfirst data zone number: %d\n"
		"\tlog2(zone size/block size): %d\n"
		"\tfile maximum size: %ld\n",
		s->s_dev, s->s_magic, s->s_ninodes, s->s_nzones,
		s->s_imap_blocks, s->s_zmap_blocks, s->s_firstdatazone,
		s->s_log_zone_size, s->s_max_size);	
}

/* read super block and imap/zmap blocks into buffer_head */
static struct super_block *read_super(int dev)
{
	struct super_block *s;
	struct buffer_head *bh;
	int i, block;

	if (!dev)
		return NULL;

	check_disk_change(dev);

	if ((s = get_super(dev)))
		return s;

	/* find first unused super_block[] item */
	for (s = super_block;; s++) {
		if (s >= super_block + NR_SUPER)
			return NULL;
		if (!s->s_dev)	/* found unused one, return */
			break;
	}

	s->s_dev = dev;
	s->s_isuper = NULL;
	s->s_imount = NULL;
	s->s_time = 0;
	s->s_read_only = 0;
	s->s_dirt = 0;

	lock_super(s);
	if (!(bh = bread(dev, 1))) {	/* read super block from disk */
		s->s_dev = 0;
		unlock_super(s);
		return NULL;
	}

	/* copy super block data into super_block[] */
	*((struct d_super_block *)s) = *((struct d_super_block *)bh->b_data);
	brelse(bh);

	show_super_block_detail(s);

	/* currently only support MINIX 1.0 file system */
	if (s->s_magic != SUPER_MAGIC) {
		s->s_dev = 0;
		unlock_super(s);
		return NULL;
	}

	for (i = 0; i < I_MAP_SLOTS; i++)
		s->s_imap[i] = NULL;

	for (i = 0; i < Z_MAP_SLOTS; i++)
		s->s_zmap[i] = NULL;

	block = 2;  /* skip boot block and super block */

	/* read imap */
	for (i = 0; i < s->s_imap_blocks; i++) {
		if ((s->s_imap[i] = bread(dev, block)))
			block++;
		else
			break;
	}

	/* read zmap */
	for (i = 0; i < s->s_zmap_blocks; i++) {
		if ((s->s_zmap[i] = bread(dev, block)))
			block++;
		else
			break;
	}

	/* 
	 * if total readed block not the same as super block described, release
	 * all
	 */
	if (block != 2 + s->s_imap_blocks + s->s_zmap_blocks) {
		for(i = 0; i < I_MAP_SLOTS; i++)
			brelse(s->s_imap[i]);

		for(i = 0;i < Z_MAP_SLOTS; i++)
			brelse(s->s_zmap[i]);

		s->s_dev = 0;
		unlock_super(s);
		return NULL;
	}

	/* "0" inode can not be used, mark to 1 */
	s->s_imap[0]->b_data[0] |= 1;
	s->s_zmap[0]->b_data[0] |= 1;
	unlock_super(s);

	return s;
}

int sys_umount(char * dev_name)
{
	struct m_inode * inode;
	struct super_block * sb;
	int dev;

	if (!(inode=namei(dev_name)))
		return -ENOENT;
	dev = inode->i_zone[0];
	if (!S_ISBLK(inode->i_mode)) {
		iput(inode);
		return -ENOTBLK;
	}
	iput(inode);
	if (dev==ROOT_DEV)
		return -EBUSY;
	if (!(sb=get_super(dev)) || !(sb->s_imount))
		return -ENOENT;
	if (!sb->s_imount->i_mount)
		printk("Mounted inode has i_mount=0\n");
	for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)
		if (inode->i_dev==dev && inode->i_count)
				return -EBUSY;
	sb->s_imount->i_mount=0;
	iput(sb->s_imount);
	sb->s_imount = NULL;
	iput(sb->s_isuper);
	sb->s_isuper = NULL;
	put_super(dev);
	sync_dev(dev);
	return 0;
}

int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
	struct m_inode * dev_i, * dir_i;
	struct super_block * sb;
	int dev;

	if (!(dev_i=namei(dev_name)))
		return -ENOENT;
	dev = dev_i->i_zone[0];
	if (!S_ISBLK(dev_i->i_mode)) {
		iput(dev_i);
		return -EPERM;
	}
	iput(dev_i);
	if (!(dir_i=namei(dir_name)))
		return -ENOENT;
	if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
		iput(dir_i);
		return -EBUSY;
	}
	if (!S_ISDIR(dir_i->i_mode)) {
		iput(dir_i);
		return -EPERM;
	}
	if (!(sb=read_super(dev))) {
		iput(dir_i);
		return -EBUSY;
	}
	if (sb->s_imount) {
		iput(dir_i);
		return -EBUSY;
	}
	if (dir_i->i_mount) {
		iput(dir_i);
		return -EPERM;
	}
	sb->s_imount=dir_i;
	dir_i->i_mount=1;
	dir_i->i_dirt=1;		/* NOTE! we don't iput(dir_i) */
	return 0;			/* we do that in umount */
}

/* read super block (block 1) and root inode (inode 1) */
void mount_root(void)
{
	int i, free;
	struct super_block *super;
	struct m_inode *minode;

	if (sizeof(struct d_inode) != 32)
		panic("bad i-node size");

	for(i = 0; i < NR_FILE; i++)
		file_table[i].f_count = 0;

	if (MAJOR(ROOT_DEV) == 2) {
		printk("Insert root floppy and press ENTER");
		wait_for_keypress();
	}

	for(super = &super_block[0]; super < &super_block[NR_SUPER]; super++) {
		super->s_dev = 0;
		super->s_lock = 0;
		super->s_wait = NULL;
	}

	if (!(super = read_super(ROOT_DEV)))
		panic("Unable to mount root");

	if (!(minode = iget(ROOT_DEV, ROOT_INO)))
		panic("Unable to read root i-node");

	/* 
	 * NOTE! it is logically used 4 times, not 1. 
	 * 4 times: super->s_isuper, super->s_imount, current->pwd, current->root.
	 * i_count will initial to 1 when iget(), so we need +3 manually
	 */
	minode->i_count += 3 ;
	super->s_isuper = super->s_imount = minode;
	current->pwd = minode;
	current->root = minode;

	/* ---- show block/inode informations ---- */
	/* check free block nrs */
	free = 0;
	i = super->s_nzones;
	while (--i >= 0) {
		if (!test_bit(i & 8191, super->s_zmap[i >> 13]->b_data))
			free++;
	}
	printk("%d/%d free blocks\n", free, super->s_nzones);

	/* check free inode nrs */
	free = 0;
	i = super->s_ninodes + 1; /* include inode 0 */
	while (--i >= 0) {
		if (!test_bit(i & 8191, super->s_imap[i >> 13]->b_data))
			free++;
	}
	printk("%d/%d free inodes\n", free, super->s_ninodes);
}
