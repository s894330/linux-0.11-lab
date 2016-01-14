/*
 *  linux/kernel/printk.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * When in kernel-mode, we cannot use printf, as fs is liable to
 * point to 'interesting' things. Make a printf with fs-saving, and
 * all is well.
 */
#include <stdarg.h>
#include <stddef.h>

#include <linux/kernel.h>

static char buf[1024];

extern int vsprintf(char *buf, const char *fmt, va_list args);

int printk(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);	/* args = first va param of "..." */
	i = vsprintf(buf, fmt, args);
	va_end(args);

	/* 
	 * default data segment register used by tty_write() is fs which is used
	 * by user program, so we need save fs first and let fs = ds (which is
	 * the data segment used by kernel)
	 */
	__asm__("push %%fs\n\t"		/* save fs */
		"push %%ds\n\t"
		"pop %%fs\n\t"		/* fs = ds */
		"pushl %0\n\t"		/* push length of msg */
		"pushl $buf\n\t"	/* push address of buf */
		"pushl $0\n\t"		/* push 0 (means channel 0) */
		"call tty_write\n\t"
		"addl $8, %%esp\n\t"	/* skip 0 and buf param */
		"popl %0\n\t"		/* i = length of msg */
		"pop %%fs"		/* restore fs */
		::"r" (i)
		:"ax", "cx", "dx");	/* tell gcc that ax, cx, dx maybe */
					/* changed during this assembly call */
	return i;
}
