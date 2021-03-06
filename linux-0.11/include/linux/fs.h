/*
 * This file has definitions for some important file table
 * structures etc.
 */

#ifndef _FS_H
#define _FS_H

#include <sys/types.h>

/* devices are as follows: (same as minix, so we can use the minix
 * file system. These are major numbers.)
 *
 * 0 - unused (nodev)
 * 1 - /dev/mem
 * 2 - /dev/fd
 * 3 - /dev/hd
 * 4 - /dev/ttyx
 * 5 - /dev/tty
 * 6 - /dev/lp
 * 7 - unnamed pipes
 */

#define IS_SEEKABLE(x) ((x)>=1 && (x)<=3)

#define READ 0
#define WRITE 1
#define READA 2		/* read-ahead - don't pause */
#define WRITEA 3	/* "write-ahead" - silly, but somewhat useful */

void buffer_init(long buffer_end);

#define MAJOR(a) (((unsigned)(a)) >> 8)
#define MINOR(a) ((a) & 0xff)

#define NAME_LEN 14
#define ROOT_INO 1

#define I_MAP_SLOTS 8
#define Z_MAP_SLOTS 8
#define SUPER_MAGIC 0x137f

#define NR_OPEN 20
#define NR_INODE 32
#define NR_FILE 64
#define NR_SUPER 8
#define NR_HASH 307
#define NR_BUFFERS nr_buffers
#define BLOCK_SIZE 1024
#define BLOCK_SIZE_BITS 10
#ifndef NULL
#define NULL ((void *) 0)
#endif

#define INODES_PER_BLOCK ((BLOCK_SIZE) / (sizeof(struct d_inode)))
#define DIR_ENTRIES_PER_BLOCK ((BLOCK_SIZE) / (sizeof(struct dir_entry)))

#define PIPE_HEAD(inode) ((inode).i_zone[0])
#define PIPE_TAIL(inode) ((inode).i_zone[1])
#define PIPE_SIZE(inode) ((PIPE_HEAD(inode)-PIPE_TAIL(inode))&(PAGE_SIZE-1))
#define PIPE_EMPTY(inode) (PIPE_HEAD(inode)==PIPE_TAIL(inode))
#define PIPE_FULL(inode) (PIPE_SIZE(inode)==(PAGE_SIZE-1))
#define INC_PIPE(head) \
__asm__("incl %0; andl $4095, %0"::"m" (head))

typedef char buffer_block[BLOCK_SIZE];

struct buffer_head {
	char *b_data;			/* pointer to data block (1024 bytes) */
	unsigned long b_blocknr;	/* block number */
	unsigned short b_dev;		/* device (0 = free) */
	unsigned char b_uptodate;
	unsigned char b_dirt;		/* 0-clean, 1-dirty */
	unsigned char b_count;		/* users using this block */
	unsigned char b_lock;		/* 0 - ok, 1 -locked */
	struct task_struct *b_wait;
	struct buffer_head *b_prev;
	struct buffer_head *b_next;
	struct buffer_head *b_prev_free;
	struct buffer_head *b_next_free;
};

/* 32 bytes */
struct d_inode {
	unsigned short i_mode;	    /* file type and rwx bit */
	unsigned short i_uid;	    /* user who owns the file */
	unsigned long i_size;	    /* nr of bytes in the file */
	unsigned long i_time;	    /* time of last modified since 1970/1/1 */
	unsigned char i_gid;	    /* specify owner's group */
	unsigned char i_nlinks;	    /* hard link nrs */
	unsigned short i_zone[9];   /* i_zone[0]~i_zone[6] are direct access */
				    /* i_zone[7] is indirect access */
				    /* i_zone[8] is double indirect access */
				    /* for device file(/dev/), zone[0] store */
				    /* the device nr */
};

struct m_inode {
	unsigned short i_mode;
	unsigned short i_uid;
	unsigned long i_size;
	unsigned long i_mtime;
	unsigned char i_gid;
	unsigned char i_nlinks;
	unsigned short i_zone[9];
	/* these are in memory also */
	struct task_struct *i_wait;
	unsigned long i_atime;
	unsigned long i_ctime;
	unsigned short i_dev;
	unsigned short i_num;
	unsigned short i_count;
	unsigned char i_lock;
	unsigned char i_dirt;
	unsigned char i_pipe;
	unsigned char i_mount;
	unsigned char i_seek;
	unsigned char i_update;
};

struct file {
	unsigned short f_mode;	    /* the same as i_mode in struct d_inode */
	unsigned short f_flags;	    /* flags used in open() */
	unsigned short f_count;	    /* reference count */
	struct m_inode *f_inode;    /* file corresponding inode */
	off_t f_pos;		    /* current read/write position */
};

struct super_block {
	unsigned short s_ninodes;
	unsigned short s_nzones;
	unsigned short s_imap_blocks;
	unsigned short s_zmap_blocks;
	unsigned short s_firstdatazone;
	unsigned short s_log_zone_size;
	unsigned long s_max_size;
	unsigned short s_magic;
	/* These are only in memory */
	struct buffer_head *s_imap[I_MAP_SLOTS];
	struct buffer_head *s_zmap[Z_MAP_SLOTS];
	unsigned short s_dev;
	struct m_inode *s_isuper;
	struct m_inode *s_imount;
	unsigned long s_time;
	struct task_struct *s_wait;
	unsigned char s_lock;
	unsigned char s_read_only;
	unsigned char s_dirt;
};

/* 
 * super block struct in disk, in MINIX 1.0 fs, 1 zone = 1 block = 1024KB
 */
struct d_super_block {
	unsigned short s_ninodes;	/* inode nrs */
	unsigned short s_nzones;	/* zone nrs */
	unsigned short s_imap_blocks;	/* block nrs used by inode bit map */
	unsigned short s_zmap_blocks;	/* block nrs used by zone bit map */
	unsigned short s_firstdatazone;	/* first data zone */
	unsigned short s_log_zone_size;	/* log2(zone size/block size) */
	unsigned long s_max_size;	/* maximum file size */
	unsigned short s_magic;		/* file system magic nr */
};

struct dir_entry {
	unsigned short inode_nr;
	char name[NAME_LEN];
};

extern struct m_inode inode_table[NR_INODE];
extern struct file file_table[NR_FILE];
extern struct super_block super_block[NR_SUPER];
extern struct buffer_head * start_buffer;
extern int nr_buffers;

extern void check_disk_change(int dev);
extern int floppy_change(unsigned int nr);
extern int ticks_to_floppy_on(unsigned int dev);
extern void floppy_on(unsigned int dev);
extern void floppy_off(unsigned int dev);
extern void truncate(struct m_inode * inode);
extern void sync_inodes(void);
extern void wait_on(struct m_inode * inode);
extern int bmap(struct m_inode * inode,int block);
extern int create_block(struct m_inode * inode,int block);
extern struct m_inode * namei(const char * pathname);
extern int open_namei(const char * pathname, int flag, int mode,
	struct m_inode ** res_inode);
extern void iput(struct m_inode * inode);
extern struct m_inode * iget(int dev,int nr);
extern struct m_inode * get_empty_inode(void);
extern struct m_inode * get_pipe_inode(void);
extern struct buffer_head *get_from_hash_table(int dev, int block);
extern struct buffer_head * getblk(int dev, int block);
extern void ll_rw_block(int rw, struct buffer_head * bh);
extern void brelse(struct buffer_head * buf);
extern struct buffer_head * bread(int dev,int block);
extern void bread_page(unsigned long addr,int dev,int b[4]);
extern struct buffer_head * breada(int dev,int block,...);
extern int new_block(int dev);
extern void free_block(int dev, int block);
extern struct m_inode * new_inode(int dev);
extern void free_inode(struct m_inode * inode);
extern int sync_dev(int dev);
extern struct super_block * get_super(int dev);
extern int ROOT_DEV;

extern void mount_root(void);

#endif
