/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "refresh_TLB()"s - I wasn't doing enough of them.
 */

#include <signal.h>

#include <asm/system.h>

#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>

void do_exit(long code);

static inline void oom(void)
{
	printk("out of memory\n");
	do_exit(SIGSEGV);
}

#define refresh_TLB() \
__asm__("movl %%eax, %%cr3"::"a" (0))

/* these are not to be changed without changing head.s etc */
#define LOW_MEM 0x100000    /* 1MB */
#define PAGING_MEMORY (15 * 1024 * 1024)
#define TOTAL_PAGES (PAGING_MEMORY >> 12)
#define MAP_NR(addr) (((addr) - LOW_MEM) >> 12)
#define USED 100
#define UNUSED 0

#define CODE_SPACE(addr) ((((addr) + 4095) & ~4095) < \
	current->start_code + current->end_code)

static long HIGH_MEMORY = 0;

#define copy_page(from, to) \
	__asm__("cld; rep; movsl"::"S" (from), "D" (to), "c" (1024))

static unsigned char memory_map[TOTAL_PAGES] = {0};

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 */
unsigned long get_free_page(void)
{
	/*
	 * C language provides the storage class "register" so that the
	 * programmer can ``suggest'' to the compiler that particular automatic
	 * variables should be allocated to CPU registers, if possible
	 */
	register unsigned long __res;

	/* get first unused page in memory_map[] and clean it, then return */
	__asm__("std; repne; scasb\n\t"	    /* repeat compare es:edi with al */
		"jne 1f\n\t"
		"movb $1, 1(%%edi)\n\t"	    /* set memory_map[edi+1] to 1 */
		"sall $12, %%ecx\n\t"	    /* shift left, ecx * 4KB */
		"addl %2, %%ecx\n\t"	    /* add memory base (LOW_MEM) */
		"movl %%ecx, %%edx\n\t"	    /* edx = real physical mem addr */
		"movl $1024, %%ecx\n\t"
		"leal 4092(%%edx), %%edi\n\t"	/* edi = addr of (edx + 4092) */
		"rep; stosl\n\t"    /* store eax -> es:edi repeat ecx count */
		"movl %%edx, %%eax\n\t"	    /* eax = addr of physical memory */
		"1:cld"
		:"=a" (__res)
		:"0" (0), "i" (LOW_MEM), "c" (TOTAL_PAGES),
		"D" (memory_map + TOTAL_PAGES - 1));

	return __res;
}

/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 */
void free_page(unsigned long addr)
{
	if (addr < LOW_MEM)
		return;
	if (addr >= HIGH_MEMORY)
		panic("trying to free nonexistent page");

	addr -= LOW_MEM;
	addr >>= 12;
	if (memory_map[addr]) {
		memory_map[addr]--;
		return;
	}
	memory_map[addr] = 0;
	panic("trying to free free page");
}

/*
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 */
int free_page_tables(unsigned long from, unsigned long size)
{
	unsigned long *pg_table;
	unsigned long *dir, nr;

	/* the page which we want to free must align to 4MB */
	if (from & 0x3fffff)
		panic("free_page_tables called with wrong alignment");
	if (!from)
		panic("Trying to free up swapper(kernel) memory space");

	/* 
	 * calculate page nrs which we need to free. For example, if
	 * size = 4.01MB, then (size + 0x3fffff) >> 22 = 2
	 */
	size = (size + 0x3fffff) >> 22;
	dir = (unsigned long *)((from >> 22) * 4); /* _pg_dir = 0 */
	for (; size-- > 0; dir++) {
		if (!(1 & *dir))    /* page table not exist, skip */
			continue;
		pg_table = (unsigned long *)(0xfffff000 & *dir);
		for (nr = 0; nr < 1024; nr++) {
			if (1 & *pg_table)
				free_page(0xfffff000 & *pg_table);
			*pg_table = 0;
			pg_table++;
		}
		/* free page table itself */
		free_page(0xfffff000 & *dir);
		*dir = 0;
	}
	refresh_TLB();
	return 0;
}

/*
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 MB-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 */
int copy_page_tables(unsigned long from, unsigned long to, long size)
{
	unsigned long *from_page_table;
	unsigned long *to_page_table;
	unsigned long this_page;
	unsigned long *from_dir, *to_dir;
	unsigned long nr;

	/* make sure align to 4MB, because one page table entry map to 4MB */
	if ((from & 0x3fffff) || (to & 0x3fffff))
		panic("copy_page_tables called with wrong alignment");

	/* get page dir entry address, because one entry is 4 byte, so we * 4 */
	from_dir = (unsigned long *)((from >> 22) * 4); /* _pg_dir = 0 */
	to_dir = (unsigned long *)((to >> 22) * 4);
	
	/* 
	 * caculate nr of page table which need to copy, one page map 4MB(and
	 * need one page dir entry map to it), so it should >> 22
	 */
	size = ((unsigned)(size + 0x3fffff)) >> 22;

	for(; size-- > 0; from_dir++, to_dir++) {
		if (1 & *to_dir)
			panic("copy_page_tables: already exist");

		if (!(1 & *from_dir))	/* page table not exist, skip */
			continue;

		/* fetch page table address */
		from_page_table = (unsigned long *)(0xfffff000 & *from_dir);
		
		/* create new page for page table */
		if (!(to_page_table = (unsigned long *)get_free_page()))
			return -1;	/* Out of memory, see freeing */

		/* set new created page property to “User, R/W, Present" */
		*to_dir = ((unsigned long)to_page_table) | 7;

		/* 
		 * if we copy page tables from task 0, only copy 160 entry,
		 * because task0 only has 640KB data size
		*/
		nr = (from == 0) ? 0xa0 : 1024;
		for (; nr-- > 0; from_page_table++, to_page_table++) {
			this_page = *from_page_table;

			if (!(1 & this_page)) /* current page not exist, skip */
				continue;

			/* mark page to read only */
			this_page &= ~2;

			/* 
			 * both from_page and to_page share the same 4KB memory
			 * page, but to_page mark as read only
			 * */
			*to_page_table = this_page;

			/* 
			 * if this_page > LOW_MEM, also mark from_page to read
			 * only, and update memory_map[]
			 * */
			if (this_page > LOW_MEM) {
				*from_page_table = this_page;
				this_page -= LOW_MEM;
				this_page >>= 12;
				memory_map[this_page]++;
			}
		}
	}

	refresh_TLB();

	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
unsigned long put_page(unsigned long page, unsigned long address)
{
	unsigned long tmp, *pdt_entry, *pgt_entry;

	/* NOTE !!! This uses the fact that _pg_dir=0 */
	if (page < LOW_MEM || page >= HIGH_MEMORY)
		printk("Trying to put page %p at %p\n", page, address);

	if (memory_map[(page - LOW_MEM) >> 12] != 1)
		printk("memory_map disagrees with %p at %p\n", page, address);

	pdt_entry = (unsigned long *)((address >> 22) * 4);
	if ((*pdt_entry) & 1) {
		pgt_entry = (unsigned long *)(0xfffff000 & *pdt_entry);
	} else {
		if (!(tmp = get_free_page()))
			return 0;
		*pdt_entry = tmp | 7;
		pgt_entry = (unsigned long *)tmp;
	}

	pgt_entry[(address >> 12) & 0x3ff] = page | 7;

	/* no need for refresh_TLB */
	return page;
}

void un_wp_page(unsigned long *page_addr)
{
	unsigned long old_page, new_page;

	/* if page is not shared (memory_map[] = 1), change property to R/W */
	old_page = 0xfffff000 & *page_addr;
	if (old_page >= LOW_MEM && memory_map[MAP_NR(old_page)] == 1) {
		*page_addr |= 2;
		refresh_TLB();
		return;
	}

	/* mamory page is shread with other process, need create new one */
	if (!(new_page = get_free_page()))
		oom();

	if (old_page >= LOW_MEM)
		memory_map[MAP_NR(old_page)]--;

	*page_addr = new_page | 7;
	refresh_TLB();
	copy_page(old_page, new_page);
}	

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * If it's in code space we exit with a segment error.
 */
void do_wp_page(unsigned long error_code, unsigned long address)
{
#if 0
/* we cannot do this yet: the estdio library writes to code space */
/* stupid, stupid. I really want the libc.a from GNU */
	if (CODE_SPACE(address))
		do_exit(SIGSEGV);
#endif
	unsigned long pgt_addr, pgt_entry_offset;

	pgt_addr = *((unsigned long *)(((address >> 22) & 0x3ff) * 4));
	pgt_addr &= 0xfffff000;

	pgt_entry_offset = ((address >> 12) & 0x3ff) * 4;

	un_wp_page((unsigned long *)(pgt_addr + pgt_entry_offset));
}

void write_verify(unsigned long address)
{
	unsigned long pgt_addr;	/* page table address */
	unsigned long page_addr;

	pgt_addr = *((unsigned long *)((address >> 22) * 4));

	/* 
	 * if pgt_addr not exist, CPU will cause page not found error, thus
	 * call do_no_page routine, so we can directly return
	 */
	if (!(pgt_addr & 1))
		return;

	page_addr = (pgt_addr & 0xfffff000) + ((address >> 12) & 0x3ff) * 4;
	if ((*(unsigned long *)page_addr & 3) == 1)  /* non-writeable, present */
		un_wp_page((unsigned long *)page_addr);
	return;
}

void get_empty_page(unsigned long address)
{
	unsigned long tmp;

	if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
		free_page(tmp);		/* 0 is ok - ignored */
		oom();
	}
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable.
 */
static int try_to_share(unsigned long address, struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	from_page = to_page = ((address>>20) & 0xffc);
	from_page += ((p->start_code>>20) & 0xffc);
	to_page += ((current->start_code>>20) & 0xffc);
/* is there a page-directory at from? */
	from = *(unsigned long *) from_page;
	if (!(from & 1))
		return 0;
	from &= 0xfffff000;
	from_page = from + ((address>>10) & 0xffc);
	phys_addr = *(unsigned long *) from_page;
/* is the page clean and present? */
	if ((phys_addr & 0x41) != 0x01)
		return 0;
	phys_addr &= 0xfffff000;
	if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
		return 0;
	to = *(unsigned long *) to_page;
	if (!(to & 1)) {
		if ((to = get_free_page()))
			*(unsigned long *) to_page = to | 7;
		else
			oom();
	}
	to &= 0xfffff000;
	to_page = to + ((address>>10) & 0xffc);
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
/* share them: write-protect */
	*(unsigned long *) from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	refresh_TLB();
	phys_addr -= LOW_MEM;
	phys_addr >>= 12;
	memory_map[phys_addr]++;
	return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */
static int share_page(unsigned long address)
{
	struct task_struct ** p;

	if (!current->executable)
		return 0;
	if (current->executable->i_count < 2)
		return 0;
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p)
			continue;
		if (current == *p)
			continue;
		if ((*p)->executable != current->executable)
			continue;
		if (try_to_share(address,*p))
			return 1;
	}
	return 0;
}

void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block,i;

	address &= 0xfffff000;
	tmp = address - current->start_code;
	if (!current->executable || tmp >= current->end_data) {
		get_empty_page(address);
		return;
	}
	if (share_page(tmp))
		return;
	if (!(page = get_free_page()))
		oom();
/* remember that 1 block is used for header */
	block = 1 + tmp/BLOCK_SIZE;
	for (i=0 ; i<4 ; block++,i++)
		nr[i] = bmap(current->executable,block);
	bread_page(page,current->executable->i_dev,nr);
	i = tmp + 4096 - current->end_data;
	tmp = page + 4096;
	while (i-- > 0) {
		tmp--;
		*(char *)tmp = 0;
	}
	if (put_page(page,address))
		return;
	free_page(page);
	oom();
}

/* 
 * initial memory_map[] array according to real physical memory size, each
 * memory_map[i] correspond one 4KB physical page,
 * 0 means unused, > 0 means used
 */
void memory_init(long memory_start, long memory_end)
{
	int i;

	HIGH_MEMORY = memory_end;

	for (i = 0; i < TOTAL_PAGES; i++)
		memory_map[i] = USED;

	i = MAP_NR(memory_start);
	memory_end -= memory_start;
	memory_end >>= 12;

	/* according phsical memory size to adjust memory_map[i] value */
	while (memory_end > 0) {
		memory_map[i] = UNUSED;
		memory_end--;
		i++;
	}
}

void calc_mem(void)
{
	int i,j,k,free=0;
	long * pg_tbl;

	for(i=0 ; i<TOTAL_PAGES ; i++)
		if (!memory_map[i]) free++;
	printk("%d pages free (of %d)\n\r",free,TOTAL_PAGES);
	for(i=2 ; i<1024 ; i++) {
		if (1&pg_dir[i]) {
			pg_tbl=(long *) (0xfffff000 & pg_dir[i]);
			for(j=k=0 ; j<1024 ; j++)
				if (pg_tbl[j]&1)
					k++;
			printk("Pg-dir[%d] uses %d pages\n",i,k);
		}
	}
}
