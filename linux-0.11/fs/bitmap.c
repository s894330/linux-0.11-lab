/*
 *  linux/fs/bitmap.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* bitmap.c contains the code that handles the inode and block bitmaps */
#include <string.h>

#include <linux/sched.h>
#include <linux/kernel.h>

#define clear_block(addr) \
	__asm__ volatile( \
		"cld\n\t" \
		"rep\n\t" \
		"stosl" \
		::"a" (0), "c" (BLOCK_SIZE / 4), "D" ((long)(addr)))

/* set addr offset nr bit to 1 and return original bit value */
#define set_bit(nr, addr) ({\
	register int res ; \
	__asm__ volatile( \
		"btsl %2, %3\n\t" /* store bit value of %3 offset %2 into CF,*/\
				  /* and set this bit to 1 */ \
		"setb %%al"	  /* set value of CF into al */ \
		:"=a" (res):"0" (0), "r" (nr), "m" (*(addr))); \
	res; \
})

#define clear_bit(nr, addr) ({\
	register int res ; \
	__asm__ volatile( \
		"btrl %2, %3\n\t" /* store bit value of %3 offset %2 into CF,*/\
				  /* and set this bit to 0 */ \
		"setnb %%al"	  /* set ~value of CF into al */ \
		:"=a" (res):"0" (0), "r" (nr), "m" (*(addr))); \
	res; \
})

#define find_first_zero(addr) ({ \
	int __res; \
	__asm__ volatile ( \
		"cld\n\t" \
		"1:lodsl\n\t" /* load ds:esi into eax, then increment esi */ \
		"notl %%eax\n\t" \
		"bsfl %%eax, %%edx\n\t" /* bit scan forward for first set bit*/\
					/* and store index into edx */ \
		"je 2f\n\t" \
		"addl %%edx, %%ecx\n\t" \
		"jmp 3f\n\t" \
		"2:addl $32, %%ecx\n\t" \
		"cmpl $8192, %%ecx\n\t" \
		"jl 1b\n\t" /* jump if ecx < 8192 */ \
		"3:" \
		:"=c" (__res):"c" (0), "S" (addr)); \
	__res; \
})

void free_block(int dev, int block)
{
	struct super_block *sb;
	struct buffer_head *bh;

	if (!(sb = get_super(dev)))
		panic("trying to free block on nonexistent device");

	if (block < sb->s_firstdatazone || block >= sb->s_nzones)
		panic("trying to free block not in datazone");

	bh = get_from_hash_table(dev, block);
	if (bh) {
		if (bh->b_count != 1) {
			printk("trying to free block (%04x:%d), count=%d\n",
				dev, block, bh->b_count);
			return;
		}

		bh->b_dirt = 0;
		bh->b_uptodate = 0;
		brelse(bh);
	}

	block -= sb->s_firstdatazone - 1 ;
	if (clear_bit(block & 8191, sb->s_zmap[block / 8192]->b_data)) {
		printk("block (%04x:%d) ", dev, block + sb->s_firstdatazone -1);
		panic("free_block: bit already cleared");
	}

	sb->s_zmap[block / 8192]->b_dirt = 1;
}

int new_block(int dev)
{
	struct buffer_head *bh;
	struct super_block *sb;
	int i, j;

	if (!(sb = get_super(dev)))
		panic("trying to get new block from nonexistant device");

	j = 8192;
	for (i = 0; i < Z_MAP_SLOTS; i++) {
		if ((bh = sb->s_zmap[i]))
			if ((j = find_first_zero(bh->b_data)) < 8192)
				break;
	}

	if (i >= 8 || !bh || j >= 8192)
		return 0;

	if (set_bit(j, bh->b_data))
		panic("new_block: bit already set");

	bh->b_dirt = 1;
	j += i * 8192 + sb->s_firstdatazone - 1;
	if (j >= sb->s_nzones)
		return 0;

	if (!(bh = getblk(dev, j)))
		panic("new_block: cannot get block");

	if (bh->b_count != 1)
		panic("new block: count is != 1");

	clear_block(bh->b_data);
	bh->b_uptodate = 1;
	bh->b_dirt = 1;
	brelse(bh);

	return j;
}

void free_inode(struct m_inode *inode)
{
	struct super_block *sb;
	struct buffer_head *bh;

	if (!inode)
		return;

	if (!inode->i_dev) {
		memset(inode, 0, sizeof(*inode));
		return;
	}

	if (inode->i_count > 1) {
		printk("trying to free inode with count=%d\n", inode->i_count);
		panic("free_inode");
	}

	if (inode->i_nlinks)
		panic("trying to free inode with links");

	if (!(sb = get_super(inode->i_dev)))
		panic("trying to free inode on nonexistent device");

	if (inode->i_num < 1 || inode->i_num > sb->s_ninodes)
		panic("trying to free inode 0 or nonexistant inode");

	if (!(bh = sb->s_imap[inode->i_num >> 13]))
		panic("nonexistent imap in superblock");

	if (clear_bit(inode->i_num & 8191, bh->b_data))
		printk("free_inode: bit already cleared.\n");

	bh->b_dirt = 1;
	memset(inode, 0, sizeof(*inode));
}

struct m_inode *new_inode(int dev)
{
	struct m_inode *inode;
	struct super_block *sb;
	struct buffer_head *bh;
	int i, j;

	if (!(inode = get_empty_inode()))
		return NULL;

	if (!(sb = get_super(dev)))
		panic("new_inode with unknown device");

	j = 8192;
	for (i = 0; i < I_MAP_SLOTS; i++) {
		if ((bh = sb->s_imap[i]))
			if ((j = find_first_zero(bh->b_data)) < 8192)
				break;
	}

	if (!bh || j >= 8192 || j + i * 8192 > sb->s_ninodes) {
		iput(inode);
		return NULL;
	}

	if (set_bit(j, bh->b_data))
		panic("new_inode: bit already set");

	bh->b_dirt = 1;
	inode->i_count = 1;
	inode->i_nlinks = 1;
	inode->i_dev = dev;
	inode->i_uid = current->euid;
	inode->i_gid = current->egid;
	inode->i_dirt = 1;
	inode->i_num = j + i * 8192;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;

	return inode;
}
