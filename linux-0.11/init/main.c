/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#define __IN_MAIN__
#include <unistd.h>
#include <time.h>

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
/* 
 * GCC does not inline any functions when not optimizing unless you specify the
 * ‘always_inline’ attribute for the function
 */
static inline __attribute__((always_inline)) _syscall0(int, fork)
static inline __attribute__((always_inline)) _syscall0(int, pause)
static inline _syscall1(int, setup, void *, BIOS)
static inline _syscall0(int, sync)

#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

static char printbuf[1024];

extern int vsprintf();
extern void init(void);
extern void block_dev_init(void);
extern void char_dev_init(void);
extern void hd_init(void);
extern void floppy_init(void);
extern void memory_init(long start, long end);
extern long ramdisk_init(long mem_start, int length);
extern long kernel_mktime(struct tm * tm);
extern long startup_time;

/*
 * This is set up by the setup-routine at boot-time
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define HD_INFO (*(struct hd_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901fc)

/* data stored in CMOS is BCD codec 
 * using 1byte: high 4bit value a = a*10
 *		low 4bit value b = b
 * for example:
 *	14 = 0001 0100
 *	27 = 0010 0111
 * 
 * so we need transfer it
 */
#define BCD_TO_BIN(val) ((val) = ((val) & 0x0f) + ((val) >> 4) * 10)

static void time_init(void)
{
	struct tm time;

	do {					/* Register  Contents */
		time.tm_sec = CMOS_READ(0);	/* 0x00      Seconds */
		time.tm_min = CMOS_READ(2);	/* 0x02      Minutes */
		time.tm_hour = CMOS_READ(4);	/* 0x04      Hours */
		time.tm_mday = CMOS_READ(7);	/* 0x07      Day of Month */
		time.tm_mon = CMOS_READ(8);	/* 0x08      Month */
		time.tm_year = CMOS_READ(9);	/* 0x09      Year */
	} while (CMOS_READ(0) != time.tm_sec);

	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);

	/* startup_time = total passed seconds since from 1970/1/1 0:00:00 */
	startup_time = kernel_mktime(&time);
}

static long memory_end = 0;
static long buffer_memory_end = 0;
static long memory_start = 0;

/* 
 * each hard disk param table is 16byte, so hd_info can store 2 hd param table
 * data
 */
static struct hd_info {
	char dummy[32];
} hd_info;

void start_kernel(void)	/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
	/*
	 * Interrupts are still disabled. Do necessary setups, then
	 * enable them
	 */
 	ROOT_DEV = ORIG_ROOT_DEV;
 	hd_info = HD_INFO;
	memory_end = (1 << 20) + (EXT_MEM_K << 10); /* 1MB + EXT_MEM_K * 1KB */
	memory_end &= 0xfffff000;   /* ignore the last memory not align 4KB */

	/* if physical mem > 16MB, set it to 16MB */
	if (memory_end > 16 * 1024 * 1024)
		memory_end = 16 * 1024 * 1024;

	if (memory_end > 12 * 1024 * 1024) 
		buffer_memory_end = 4 * 1024 * 1024;
	else if (memory_end > 6 * 1024 * 1024)
		buffer_memory_end = 2 * 1024 * 1024;
	else
		buffer_memory_end = 1 * 1024 * 1024;

	memory_start = buffer_memory_end;
#ifdef RAMDISK	/* currently we don't use ramdisk. Nail 2016/2/4 */
	memory_start += ramdisk_init(memory_start, RAMDISK * 1024);
#endif
	memory_init(memory_start, memory_end);
	trap_init();
	block_dev_init();
	char_dev_init();
	tty_init();	/* we can use printk() only after tty_init() is done */
	time_init();
	schedule_init();
	buffer_init(buffer_memory_end);
	hd_init();
	floppy_init();
	/* 
	 * set interrupt flag(IF) to 1, CPU will begin handle maskable hardware
	 * interrupts
	 */
	sti();
	move_to_user_mode();

	/* now we are under task0's user process context */
	if (!fork())	/* we count on this going ok */
		init();

	/*
	 * NOTE!! For any other task, 'pause()' would mean we have to get a
	 * signal to awaken, but task0 is the sole exception (see 'schedule()')
	 * as task 0 gets activated at every idle moment (when no other tasks
	 * can run). For task0, 'pause()' just means we go check if some other
	 * task can run, if there is no other task can run, we return here.
	 */
	for(;;)
		pause();
}

static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsprintf(printbuf, fmt, args)
	va_end(args);

	write(1, printbuf, i);

	return i;
}

static char *argv_rc[] = {"/bin/sh", NULL};
static char *envp_rc[] = {"HOME=/", NULL};

static char *argv[] = {"-/bin/sh", NULL};
static char *envp[] = {"HOME=/usr/root", NULL};

void init(void)
{
	int pid, stat;

	setup((void *)&hd_info);
	/* 
	 * cast unused return value to void is to explicitly show other
	 * "developers" that you know this function returns but you're
	 * explicitly ignoring it.
	 *
	 * cast to void is costless. It is only information for compiler how to
	 * treat it
	 */
	(void)open("/dev/tty0", O_RDWR, 0);
	(void)dup(0);	/* duplicate fd 0 to fd 1(stdout) */
	(void)dup(0);	/* duplicate fd 0 to fd 2(stderr) */
	printf("%d buffer_head = %d bytes buffer space\n", NR_BUFFERS,
		NR_BUFFERS * BLOCK_SIZE);
	printf("Free mem: %d bytes\n", memory_end - memory_start);

	if (!(pid = fork())) {
		/* redirect stdin(fd 0) to /etc/rc */
		close(0);		
		if (open("/etc/rc", O_RDONLY, 0))
			_exit(1);
		execve("/bin/sh", argv_rc, envp_rc);
		_exit(2);
	}

	if (pid > 0) {
		while (pid != wait(&stat))
			/* nothing */;
	}

	/* child process pid 2 has died, re-create it */
	while (1) {
		if ((pid = fork()) < 0) {
			printf("Fork failed in init\n");
			continue;
		}

		if (!pid) {
			close(0);
			close(1);
			close(2);
			setsid();
			(void)open("/dev/tty0", O_RDWR, 0);
			(void)dup(0);
			(void)dup(0);
			_exit(execve("/bin/sh", argv, envp));
		}

		while (1)
			if (pid == wait(&stat))
				break;
		printf("\nchild %d died with code %04x\n", pid, stat);
		sync();
	}

	_exit(0);	/* NOTE! _exit, not exit() */
}
