/*
 *  linux/fs/exec.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * #!-checking implemented by tytso.
 */

/*
 * Demand-loading implemented 01.12.91 - no need to read anything but
 * the header into memory. The inode of the executable is put into
 * "current->executable", and page faults do the actual loading. Clean.
 *
 * Once more I can proudly say that linux stood up to being changed: it
 * was less than 2 hours work to get demand-loading completely implemented.
 */

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <a.out.h>

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/segment.h>

extern int sys_exit(int exit_code);
extern int sys_close(int fd);

/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB !
 */
#define MAX_ARG_PAGES 32

/*
 * create_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
static unsigned long *create_tables(char *p, int argc, int envc)
{
	unsigned long *argv, *envp;
	unsigned long *sp;

	/* set sp to the last param stack address */
	sp = (unsigned long *)(0xfffffffc & (unsigned long)p);
	sp -= envc + 1; /* 1 is the place of <NULL> char */
	envp = sp;
	sp -= argc + 1;	/* 1 is the place of <NULL> char */
	argv = sp;

	put_fs_long((unsigned long)envp, --sp);
	put_fs_long((unsigned long)argv, --sp);
	put_fs_long((unsigned long)argc, --sp);

	while (argc-- > 0) {
		put_fs_long((unsigned long)p, argv++);
		while (get_fs_byte(p++))
			/* nothing */ ;
	}
	put_fs_long(0, argv);	/* add <NULL> char */

	while (envc-- > 0) {
		put_fs_long((unsigned long)p, envp++);
		while (get_fs_byte(p++))
			/* nothing */ ;
	}
	put_fs_long(0, envp);	/* add <NULL> char */

	return sp;
}

/*
 * count() counts the number of arguments/envelopes, the last item of argv/envp
 * is NULL
 */
static int count(char **argv)
{
	int i = 0;
	char **tmp;

	if ((tmp = argv)) {
		while (get_fs_long((unsigned long *)(tmp++)))
			i++;
	}

	return i;
}

/*
 * 'copy_string()' copies argument/envelope strings from user memory to
 * free pages in kernel memory. These are in a format ready to be put directly
 * into the top of new user memory.
 *
 * Modified by TYT, 11/24/91 to add the from_kmem argument, which specifies
 * whether the string and the string array are from user or kernel segments:
 * 
 * from_kmem     argv *        argv **
 *    0          user space    user space
 *    1          kernel space  user space
 *    2          kernel space  kernel space
 * 
 * We do this by playing games with the fs segment register.  Since it
 * it is expensive to load a segment register, we try to avoid calling
 * set_fs() unless we absolutely have to.
 */
static unsigned long copy_strings(int argc, char **argv, unsigned long *page,
	unsigned long p, int from_kmem)
{
	char *tmp, *pag = NULL;
	int len, offset = 0;
	unsigned long old_fs, new_fs;

	if (!p)
		return 0;	/* bullet-proofing */

	new_fs = get_ds();
	old_fs = get_fs();

	if (from_kmem == 2)
		set_fs(new_fs);

	/* copy parameters one by one, start from last parameter */
	while (argc > 0) {
		argc--;
		if (from_kmem == 1)
			set_fs(new_fs);

		tmp = (char *)get_fs_long(((unsigned long *)argv) + argc);
		if (!tmp)
			panic("argc is wrong");

		if (from_kmem == 1)
			set_fs(old_fs);

		/* calculate parameter lenght */
		len = 0;		/* remember zero-padding */
		do {
			len++;
		} while (get_fs_byte(tmp++));

		if (p - len < 0) {	/* this shouldn't happen - 128kB */
			set_fs(old_fs);
			return 0;
		}

		/* copy parameter char by char, start from last char */
		while (len) {
			--p; --tmp; --len;
			/* 
			 * check if we alreay fill out PAGE_SIZE data, thus need
			 * to change to next page
			 */
			if (--offset < 0) {
				offset = p % PAGE_SIZE;
				if (from_kmem == 2)
					set_fs(old_fs);

				if (!(pag = (char *)page[p / PAGE_SIZE]) &&
					!(pag = (char *)(page[p / PAGE_SIZE] =
				      get_free_page()))) 
					return 0;

				if (from_kmem == 2)
					set_fs(new_fs);

			}
			*(pag + offset) = get_fs_byte(tmp);
		}
	}

	if (from_kmem == 2)
		set_fs(old_fs);

	return p;
}

static unsigned long change_ldt(unsigned long text_size, unsigned long *page)
{
	unsigned long code_limit, data_limit, code_base, data_base;
	int i;

	code_limit = text_size + PAGE_SIZE -1;
	code_limit &= 0xFFFFF000;
	data_limit = 0x4000000;	/* 64MB */

	code_base = get_base(current->ldt[1]);
	data_base = code_base;

	set_base(current->ldt[1], code_base);
	set_limit(current->ldt[1], code_limit);
	set_base(current->ldt[2], data_base);
	set_limit(current->ldt[2], data_limit);

	/* make sure fs points to the NEW data segment */
	__asm__("pushl $0x17; pop %%fs":);

	data_base += data_limit;
	for (i = MAX_ARG_PAGES - 1; i >= 0; i--) {
		data_base -= PAGE_SIZE;
		if (page[i])
			put_page(page[i], data_base);
	}

	return data_limit;
}

/*
 * 'do_execve()' executes a new program.
 */
int do_execve(unsigned long *eip, long tmp, char *filename, char **argv,
	char **envp)
{
	struct m_inode *inode;
	struct buffer_head *bh;
	struct exec ex;
	unsigned long page[MAX_ARG_PAGES];
	int i, argc, envc;
	int e_uid, e_gid;
	int retval;
	int sh_bang = 0;
	unsigned long p = PAGE_SIZE * MAX_ARG_PAGES - 4;

	/* check caller's cs */
	if ((0xffff & eip[1]) != 0x000f)
		panic("execve called from supervisor mode");

	for (i = 0; i < MAX_ARG_PAGES; i++)
		page[i] = 0;

	if (!(inode = namei(filename)))		/* get executables inode */
		return -ENOENT;

	argc = count(argv);
	envc = count(envp);
	
restart_interp:
	/* ---- check inode property first ---- */
	if (!S_ISREG(inode->i_mode)) {	/* must be regular file */
		retval = -EACCES;
		goto exec_error2;
	}

	/* 
	 * if S_ISUID is set, normal uesr can execute privilege user's program,
	 * ths same meaning of S_ISGID
	 */
	i = inode->i_mode;
	e_uid = (i & S_ISUID) ? inode->i_uid : current->euid;
	e_gid = (i & S_ISGID) ? inode->i_gid : current->egid;

	/* check if has right to execute */
	if (inode->i_uid == current->euid)  /* file belong current user */
		i >>= 6;
	else if (inode->i_gid == current->egid)	/* file belong current group */
		i >>= 3;
	else	/* file belong others */
		/* nothing */ ;

	/* only file with "x" or root user and mode has "x" can execute file */
	if (!(i & 1) && !((inode->i_mode & 0111) && suser())) {
		retval = -ENOEXEC;
		goto exec_error2;
	}

	/* ---- check file header second ---- */
	/* read first data block of file */
	if (!(bh = bread(inode->i_dev, inode->i_zone[0]))) {
		retval = -EACCES;
		goto exec_error2;
	}

	/* read exec-header */
	ex = *((struct exec *)bh->b_data);

	/* check if is script file(begin with #!) */
	if ((bh->b_data[0] == '#') && (bh->b_data[1] == '!') && (!sh_bang)) {
		/*
		 * This section does the #! interpretation.
		 * Sorta complicated, but hopefully it will work.  -TYT
		 */

		char buf[1023], *cp, *interp, *i_name, *i_arg;
		unsigned long old_fs;

		strncpy(buf, bh->b_data+2, 1022);
		brelse(bh);
		iput(inode);
		buf[1022] = '\0';
		if ((cp = strchr(buf, '\n'))) {
			*cp = '\0';
			for (cp = buf; (*cp == ' ') || (*cp == '\t'); cp++);
		}
		if (!cp || *cp == '\0') {
			retval = -ENOEXEC; /* No interpreter name found */
			goto exec_error1;
		}
		interp = i_name = cp;
		i_arg = 0;
		for ( ; *cp && (*cp != ' ') && (*cp != '\t'); cp++) {
 			if (*cp == '/')
				i_name = cp+1;
		}
		if (*cp) {
			*cp++ = '\0';
			i_arg = cp;
		}
		/*
		 * OK, we've parsed out the interpreter name and
		 * (optional) argument.
		 */
		if (sh_bang++ == 0) {
			p = copy_strings(envc, envp, page, p, 0);
			p = copy_strings(--argc, argv+1, page, p, 0);
		}
		/*
		 * Splice in (1) the interpreter's name for argv[0]
		 *           (2) (optional) argument to interpreter
		 *           (3) filename of shell script
		 *
		 * This is done in reverse order, because of how the
		 * user environment and arguments are stored.
		 */
		p = copy_strings(1, &filename, page, p, 1);
		argc++;
		if (i_arg) {
			p = copy_strings(1, &i_arg, page, p, 2);
			argc++;
		}
		p = copy_strings(1, &i_name, page, p, 2);
		argc++;
		if (!p) {
			retval = -ENOMEM;
			goto exec_error1;
		}
		/*
		 * OK, now restart the process with the interpreter's inode.
		 */
		old_fs = get_fs();
		set_fs(get_ds());
		if (!(inode=namei(interp))) { /* get executables inode */
			set_fs(old_fs);
			retval = -ENOENT;
			goto exec_error1;
		}
		set_fs(old_fs);
		goto restart_interp;
	}

	brelse(bh);

	/* linux-0.11 only support ZMAGIC executable file format */
	if (N_MAGIC(ex) != ZMAGIC || ex.a_trsize || ex.a_drsize ||
		ex.a_text + ex.a_data + ex.a_bss > 0x3000000 ||	/* len > 50MB */
		inode->i_size < ex.a_text + ex.a_data + ex.a_syms + N_TXTOFF(ex)) {
		retval = -ENOEXEC;
		goto exec_error2;
	}

	/* TODO need to understand the meaning of N_TXTOFF */
	if (N_TXTOFF(ex) != BLOCK_SIZE) {
		printk("%s: N_TXTOFF != BLOCK_SIZE. See a.out.h.", filename);
		retval = -ENOEXEC;
		goto exec_error2;
	}

	if (!sh_bang) {
		p = copy_strings(envc, envp, page, p, 0);
		p = copy_strings(argc, argv, page, p, 0);
		if (!p) {
			retval = -ENOMEM;
			goto exec_error2;
		}
	}
	
	/* OK, This is the point of no return */
	if (current->executable)
		iput(current->executable);
	current->executable = inode;

	/* init sa_handler to NULL */
	for (i = 0; i < NSIG; i++) {
		if (current->sigaction[i].sa_handler != SIG_IGN)
			current->sigaction[i].sa_handler = NULL;
	}

	/* close previous fd according to "close_on_exec" bitmap */
	for (i = 0; i < NR_OPEN; i++) {
		if ((current->close_on_exec >> i) & 1)
			sys_close(i);
	}
	current->close_on_exec = 0;

	free_page_tables(get_base(current->ldt[1]), get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]), get_limit(0x17));

	if (last_task_used_math == current)
		last_task_used_math = NULL;
	current->used_math = 0;

	p += change_ldt(ex.a_text, page) - PAGE_SIZE * MAX_ARG_PAGES;
	p = (unsigned long)create_tables((char *)p, argc, envc);

	current->end_code = ex.a_text;
	current->end_data = ex.a_text + ex.a_data;
	current->brk = ex.a_text + ex.a_data + ex.a_bss;
	current->start_stack = p & 0xfffff000;
	current->euid = e_uid;
	current->egid = e_gid;

	/* 
	 * if code + data length not align to page size(4KB), fill 0 to the rest
	 */
	i = ex.a_text + ex.a_data;
	while (i & 0xfff)
		put_fs_byte(0, (char *)(i++));

	eip[0] = ex.a_entry;		/* eip, magic happens :-) */
	eip[3] = p;			/* esp, stack pointer */

	return 0;

exec_error2:
	iput(inode);
exec_error1:
	for (i = 0; i < MAX_ARG_PAGES; i++)
		free_page(page[i]);

	return retval;
}
