/*
 *  linux/kernel/exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>

int sys_pause(void);
int sys_close(int fd);

void release(struct task_struct *p)
{
	int i;

	if (!p)
		return;

	for (i = 1; i < NR_TASKS; i++) {
		if (task[i] == p) {
			task[i] = NULL;
			free_page((long)p);
			schedule();
			return;
		}
	}
	panic("trying to release non-existent task");
}

static inline int send_sig(long sig, struct task_struct *p, int priv)
{
	if (!p || sig < 1 || sig > 32)
		return -EINVAL;

	if (priv || (current->euid == p->euid) || suser())
		p->signal |= (1 << (sig - 1));
	else
		return -EPERM;

	return 0;
}

static void kill_session(void)
{
	struct task_struct **p = task + NR_TASKS;
	
	while (--p > &FIRST_TASK) {
		if (*p && (*p)->session == current->session)
			(*p)->signal |= 1 << (SIGHUP - 1);
	}
}

/*
 * XXX need to check permissions needed to send signals to process
 * groups, etc. etc.  kill() permissions semantics are tricky!
 */
int sys_kill(int pid,int sig)
{
	struct task_struct **p = NR_TASKS + task;
	int err, retval = 0;

	if (!pid) while (--p > &FIRST_TASK) {
		if (*p && (*p)->pgrp == current->pid) 
			if ((err=send_sig(sig,*p,1)))
				retval = err;
	} else if (pid>0) while (--p > &FIRST_TASK) {
		if (*p && (*p)->pid == pid) 
			if ((err=send_sig(sig,*p,0)))
				retval = err;
	} else if (pid == -1) while (--p > &FIRST_TASK) {
		if ((err = send_sig(sig,*p,0)))
			retval = err;
	} else while (--p > &FIRST_TASK)
		if (*p && (*p)->pgrp == -pid)
			if ((err = send_sig(sig,*p,0)))
				retval = err;
	return retval;
}

static void tell_father(int father_pid)
{
	int i;

	if (father_pid) {
		for (i = 0; i < NR_TASKS; i++) {
			if (!task[i])
				continue;
			if (task[i]->pid != father_pid)
				continue;
			task[i]->signal |= (1 << (SIGCHLD - 1));
			return;
		}
	}

	/* 
	 * if we don't find any fathers, we just release ourselves, this is not
	 * really OK. Must change it to make father 1
	 */
	printk("BAD BAD - no father found\n");
	release(current);
}

int do_exit(long code)
{
	int i;

	free_page_tables(get_base(current->ldt[1]), get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]), get_limit(0x17));

	for (i = 0; i < NR_TASKS; i++) {
		if (task[i] && task[i]->father == current->pid) {
			task[i]->father = 1;
			if (task[i]->state == TASK_ZOMBIE) {
				/* 
				 * cast unused return value to void is to
				 * explicitly show other "developers" that you
				 * know this function returns but you're
				 * explicitly ignoring it
				 *
				 * cast to void is costless. It is only
				 * information for compiler how to treat it
				 */
				/* assumption task[1] is always init */
				(void)send_sig(SIGCHLD, task[1], 1);
			}
		}
	}

	for (i = 0; i < NR_OPEN; i++) {
		if (current->filp[i])
			sys_close(i);
	}

	iput(current->pwd);
	current->pwd = NULL;
	iput(current->root);
	current->root = NULL;
	iput(current->executable);
	current->executable = NULL;

	if (current->leader && current->tty >= 0)
		tty_table[current->tty].pgrp = 0;
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	if (current->leader)
		kill_session();

	current->state = TASK_ZOMBIE;
	current->exit_code = code;
	tell_father(current->father);
	schedule();
	return -1;	/* just to suppress warnings */
}

int sys_exit(int error_code)
{
	/* low 8 bit is used for wait()/waitpid(), so we need shift left */
	return do_exit((error_code & 0xff) << 8);
}

int sys_waitpid(pid_t pid, unsigned long *stat_addr, int options)
{
	int flag, code;
	struct task_struct **p;

	/* 
	 * we are in kernel space, stat_addr is stay in user space which we want
	 * to write, but the page protect mechanism and copy-on-write will be
	 * useless if we are in privilege 0 code, so we need to do this by hand
	 */
	verify_area(stat_addr, 4);
repeat:
	flag = 0;
	for(p = &LAST_TASK; p > &FIRST_TASK; --p) {
		if (!*p || *p == current)
			continue;
		if ((*p)->father != current->pid)
			continue;

		/* now, we found current process's child */
		if (pid > 0) {
			if ((*p)->pid != pid)
				continue;
		} else if (!pid) {
			if ((*p)->pgrp != current->pgrp)
				continue;
		} else if (pid != -1) {
			if ((*p)->pgrp != -pid)
				continue;
		}

		switch ((*p)->state) {
		case TASK_STOPPED:
			if (!(options & WUNTRACED))
				continue;
			put_fs_long(0x7f, stat_addr);
			return (*p)->pid;
		case TASK_ZOMBIE:
			current->cutime += (*p)->utime;
			current->cstime += (*p)->stime;
			flag = (*p)->pid;
			code = (*p)->exit_code;
			release(*p);
			put_fs_long(code, stat_addr);
			return flag;
		default:
			flag = 1;
			continue;
		}
	}

	if (flag) {
		/* 
		 * if WNOHANG is set, means we should directly return if no
		 * child process is at stopped/zombie state
		 */
		if (options & WNOHANG)
			return 0;
		current->state = TASK_INTERRUPTIBLE;
		schedule();

		if (!(current->signal &= ~(1 << (SIGCHLD - 1))))
			goto repeat;
		else	/* got signal during waitpid, directly return */
			return -EINTR;
	}

	return -ECHILD;
}


