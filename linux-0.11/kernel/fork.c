/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <string.h>
#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

extern void write_verify(unsigned long address);

long last_pid = 0;

void verify_area(void *addr, int size)
{
	unsigned long start;
	int offset;

	start = (unsigned long)addr;
	offset = (start & 0xfff) + size;
	start &= 0xfffff000;

	/* change page addr to linear addr */
	start += get_base(current->ldt[2]);

	/* verify one page(4KB) each time */
	while (offset > 0) {
		offset -= 4096;
		write_verify(start);
		start += 0x1000;    /* move to next page table entry */
	}
}

int copy_mem(int nr, struct task_struct *p)
{
	unsigned long old_data_base, new_data_base, data_limit;
	unsigned long old_code_base, new_code_base, code_limit;

	code_limit = get_limit(TASK_CODE_SEG);
	data_limit = get_limit(TASK_DATA_SEG);

	old_code_base = get_base(current->ldt[1]);
	old_data_base = get_base(current->ldt[2]);

	if (old_data_base != old_code_base)
		panic("We don't support separate I&D");

	if (data_limit < code_limit)
		panic("Bad data_limit");

	/* set newly create process's new code/data base */
	new_data_base = new_code_base = nr * 0x4000000;	/* 0x4000000 = 64MB */
	p->start_code = new_code_base;
	set_base(p->ldt[1], new_code_base);
	set_base(p->ldt[2], new_data_base);

	/* copy page table and setup it according to new data base */
	if (copy_page_tables(old_data_base, new_data_base, data_limit)) {
		printk("free_page_tables: from copy_mem\n");
		free_page_tables(new_data_base, data_limit);
		return -ENOMEM;
	}

	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
int copy_process(int nr, long ebp, long edi, long esi, long gs, long none,
		long ebx, long ecx, long edx,
		long fs, long es, long ds,
		long eip, long cs, long eflags, long esp, long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;

	p = (struct task_struct *)get_free_page();
	if (!p)
		return -EAGAIN;

	task[nr] = p;
	
	// NOTE!: the following statement now work with gcc 4.3.2 now, and you
	// must compile _THIS_ memcpy without no -O of gcc.#ifndef GCC4_3
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */

	/* modify copied data */
	p->state = TASK_UNINTERRUPTIBLE;
	p->pid = last_pid;
	p->father = current->pid;
	p->counter = p->priority;
	p->signal = 0;
	p->alarm = 0;
	p->leader = 0;		/* process leadership doesn't inherit */
	p->utime = p->stime = 0;
	p->cutime = p->cstime = 0;
	p->start_time = jiffies;

	/* refine tss data */
	p->tss.back_link = 0;
	p->tss.esp0 = (long)p + PAGE_SIZE;  /* esp = end of new allocate page */
	p->tss.ss0 = KERNEL_DATA_SEG;
	p->tss.eip = eip;
	p->tss.eflags = eflags;
	p->tss.eax = 0;	/* this is why new process will return 0 from fork() */
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;   /* new task use the same stack with old task */
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);
	/* offset 32KB of TSS segment is the I/O permission map of this task */ 
	p->tss.trace_bitmap = 0x80000000;

	//TODO still not understand this "if" condition
	if (last_task_used_math == current)
		__asm__("clts; fnsave %0"::"m" (p->tss.i387));

	/* setup page dir table and page table */
	if (copy_mem(nr, p)) {
		task[nr] = NULL;
		free_page((long)p);
		return -EAGAIN;
	}

	for (i = 0; i < NR_OPEN; i++) {
		if ((f = p->filp[i]))
			f->f_count++;
	}

	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;

	/* setup task nr's TSS and LDS into gdt */
	set_tss_desc(gdt + FIRST_TSS_ENTRY + nr * 2, &(p->tss));
	set_ldt_desc(gdt + FIRST_LDT_ENTRY + nr * 2, &(p->ldt));

	p->state = TASK_RUNNING;	/* do this last, just in case */

	return last_pid;
}

/* find least unused pid and first unused task[] */
int find_empty_process(void)
{
	int i;

repeat:
	if ((++last_pid) < 0)
		last_pid = 1;

	/* loop check if there is any task already using last_pid */
	for(i = 0; i < NR_TASKS; i++)
		if (task[i] && task[i]->pid == last_pid)
			goto repeat;

	/* find the first unused task_struct[] */
	for(i = 1; i < NR_TASKS; i++)
		if (!task[i])
			return i;

	return -EAGAIN;
}
