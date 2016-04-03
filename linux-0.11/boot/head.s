/*
 *  linux/boot/head.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  head.s contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00000000, which is also where
 * the page directory will exist. The startup code here will be overwritten by
 * the page directory table and page table eventually.
 */

KERNEL_DATA_SEG = 0x10 	# selector of kernel data seg.
			# 0x10: 2th item of gdt, RPL: 0

.global idt, gdt, pg_dir, tmp_floppy_area, startup_32

.text
pg_dir:	    # address of page dir table is at 0x0
startup_32:
	movl	$KERNEL_DATA_SEG, %eax	# let ds,es,fs,gs has the same segment
	mov 	%ax, %ds
	mov 	%ax, %es
	mov 	%ax, %fs
	mov 	%ax, %gs
	lss 	stack_start, %esp	# "lss" will update ss and esp

	call	setup_idt
	call 	setup_gdt

	movl 	$KERNEL_DATA_SEG, %eax	# we have chang gdt, so need reload all
	mov 	%ax, %ds		# the segment registers. CS was already
	mov 	%ax, %es		# reloaded during 'setup_gdt'
	mov 	%ax, %fs
	mov 	%ax, %gs
	lss 	stack_start, %esp

	xorl 	%eax, %eax
1:	incl 	%eax			# check that A20 really IS enabled
	movl 	%eax, 0x000000		# write value to [0x000000]
	cmpl	%eax, 0x100000		# if A20 is enabled, [0x100000]
	je 	1b			# will not equal with [0x000000]
					# eventually
/*
 * NOTE! 486 should set bit 16, to check for kernel want to write a page which
 * is user-owned read-only page. Then it would be unnecessary with the
 * "verify_area()"-calls, it is used for implement "copy on write"
 *
 * 486 users probably also want to set the NE (#5) bit also, so as to use
 * int 16 for math errors.
 */
	movl 	%cr0, %eax		# check math chip
	andl 	$0x80000011, %eax	# Save PG,PE,ET
	/* "orl $0x10020,%eax" here for 486 might be good */
	orl 	$2, %eax		# set MP to assume we have x87 co-processor
	movl 	%eax, %cr0
	call 	check_x87
	jmp 	after_page_tables

/*
 * We depend on ET to be correct. This checks for 287/387.
 */
check_x87:
	fninit			/* fninit and fstsw are instruction of 287/387 */
	fstsw 	%ax		/* if has 287/387, al should be zero after */
	cmpb 	$0, %al		/* execute fstsw instruction */
	je 1f			/* currently we have FPU(387) on QEMU emulator */
	
	/* 
	 * no coprocessor: have to set EM bits. On QEMU, following will not
	 * be executed
	 */
	movl 	%cr0, %eax
	xorl 	$6, %eax	/* reset MP, set EM */
	movl 	%eax, %cr0
	ret

.align 4 /* has coprocessors, set 287/387 to protect mode */
1:	.byte 0xdb, 0xe4		/* "fsetpm" for 287, ignored by 387 */
	ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to ignore_int, interrupt gates.
 *  It then loads idt. Everything that wants to install itself in the idt-table
 *  may do so themselves. Interrupts are enabled at other place, when we can be
 *  relatively sure everything is ok. This routine will be over-written by the
 *  page tables.
 */
setup_idt:
	lea 	ignore_int, %edx	# lea: load effective address
	movl 	$0x00080000, %eax
	movw 	%dx, %ax		/* selector = 0x0008 */
	movw 	$0x8e00, %dx		/* 0x8e00: inter gate, DPL 0, present */

	lea 	idt, %edi
	mov 	$256, %ecx
rp_sidt:
	movl 	%eax, (%edi)
	movl 	%edx, 4(%edi)
	addl 	$8, %edi
	dec 	%ecx
	jne 	rp_sidt

	lidt 	idt_descr
	ret

/*
 *  setup_gdt
 *
 *  This routines sets up a new gdt and loads it. Only two entries are currently
 *  built, the same ones that were built in init.s. The routine is VERY
 *  complicated at two whole lines, so this rather long comment is certainly
 *  needed :-).
 *
 *  This routine will be overwritten by the page tables.
 */
setup_gdt:
	lgdt	gdt_descr
	ret

/*
 * I put the kernel page tables right after the page directory,
 * using 4 of them to span 16 MB of physical memory. People with
 * more than 16MB will have to expand this.
 */
.org 0x1000
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000
/*
 * tmp_floppy_area is used by the floppy-driver when DMA cannot
 * reach to a buffer-block. It needs to be aligned, so that it isn't
 * on a 64kB border.
 */
tmp_floppy_area:
	.fill 1024, 1, 0

# main func seems not use 3 push $0, so mark it
after_page_tables:
#	pushl 	$0		# These are the parameters to main :-)
#	pushl 	$0		# envp, argv, argc
#	pushl 	$0
	pushl 	$L6		# return address for main, if it decides to.
	pushl 	$start_kernel
	jmp	setup_paging
L6:	jmp	L6		# main should never return here, but
				# just in case, we know what happens.

/* This is the default interrupt "handler" :-) */
int_msg:
	.asciz "Unknown interrupt\n"	# asciz is just like ascii, but each
					# string is followed by a zero byte

.align 4
ignore_int:
	pushl 	%eax
	pushl 	%ecx
	pushl 	%edx
	push 	%ds
	push 	%es
	push 	%fs

	# change segment to kernel data segment
	movl 	$KERNEL_DATA_SEG, %eax
	mov 	%ax, %ds
	mov 	%ax, %es
	mov 	%ax, %fs
	pushl 	$int_msg	# int_msg is param of print();
	call 	printk		# printk() is implemented in C code
	popl 	%eax

	pop 	%fs
	pop 	%es
	pop 	%ds
	popl 	%edx
	popl 	%ecx
	popl 	%eax
	iret

/*
 * Setup_paging
 *
 * This routine sets up paging by setting the page bit in cr0. The page tables
 * are set up, identity-mapping the first 16MB. The pager assumes that no
 * illegal addresses are produced (ie > 4MB on a 4MB machine).
 *
 * NOTE! Although all physical memory should be identity mapped by this routine,
 * only the kernel page functions use the > 1MB addresses directly. All "normal"
 * functions use just the lower 1MB, or the local data space, which will be
 * mapped to some other place - mm keeps track of that.
 *
 * For those with more memory than 16 MB - tough luck. I've not got it, why
 * should you :-) The source is here. Change it. (Seriously - it shouldn't be
 * too difficult. Mostly change some constants etc. I left it at 16MB, as my
 * machine even cannot be extended past that (ok, but it was cheap :-)
 *
 * I've tried to show which constants to change by having some kind of marker at
 * them (search for "16MB"), but I won't guarantee that's all :-(
 */
.align 4
setup_paging:
	movl 	$1024 * 5, %ecx		/* 5 pages - pg_dir + 4 page tables */
	xorl 	%eax, %eax
	xorl 	%edi, %edi		/* pg_dir is at 0x0 */
	cld
	rep
		stosl			/* clean 5 pages */
/* 
 * Format of page dir entry and page table entry:
 * 31 ............ 12 11~9 8 7 6 5 4 3 2 1 0
 *  page frame addr    AVL 0 0 D A 0 0 U R P
 *                                     / /
 *                                     S W
 *  P: present
 *  R/W: read/write	0: R/E, 1:R/W/E
 *  U/S: user/supervisor 0: only 0, 1, 2 user can access, 1: all user can access
 *  A: accessd
 *  D: dirty, when CPU exec write command to this page, will set D to 1
 * */
	/* ---- setup page directory table ---- */
	/* 
	 * set kernel code to user (bit2 set to 1) is because task 0 and
	 * task 1's code is also inside the kernel code, so we need to let these
	 * two user space process can access memory
	 */
	/* $pg0 + 7 = 0x00001007, $pg1 + 7 = 0x00002007, and so on */
	movl 	$pg0 + 7, pg_dir	/* set present bit/user r/w */
	movl 	$pg1 + 7, pg_dir + 4	/*  --------- " " --------- */
	movl 	$pg2 + 7, pg_dir + 8	/*  --------- " " --------- */
	movl 	$pg3 + 7, pg_dir + 12	/*  --------- " " --------- */

	/* ---- setup 4 page table ---- */
	/* begins from last page table entry of last page table */
	movl 	$pg3 + 4092, %edi	/* edi = address of last page entry */
	movl 	$0xfff007, %eax		/* 16MB - 4096 + 7 (r/w user,p)=>last */
	std
1:	stosl			/* fill pages backwards - more efficient :-) */
	subl 	$0x1000, %eax		/* stos: store eax value to es:edi */
	jge 	1b			/* jump if eax >= 0 */
	cld				/* setup done, reset direction flag */

	/* load address of page directory table into CR3 */
	xorl 	%eax, %eax		/* pg_dir is at 0x0000 */
	movl 	%eax, %cr3		/* load addr of pg_dir into CR3 */
					/* movl to CR3 will refresh TLB */

	/* enable CPU paging function */
	movl 	%cr0, %eax		/* set paging (PG) bit */
	orl 	$0x80000000, %eax	/* this also flushes prefetch-queue */
	movl 	%eax, %cr0
	ret				/* this "ret" will call "start_kernel"*/
					/* function defined in C code */
.align 4
.word 0				# in order let following "idt" align to 4 byte
idt_descr:
	.word 256 * 8 - 1	# idt contains 256 entries
	.long idt

.align 4
.word 0
gdt_descr:
	.word 256 * 8 - 1	# so does gdt (not that that's any
	.long gdt		# magic number, but it works for me :^)

.align 8
idt:	.fill 256, 8, 0		# idt table, will initialize at setup_idt func

gdt:	.quad 0x0000000000000000	/* NULL descriptor */
	/* 16MB kernel R/E code size from 0x0, DPL 0 */
	.quad 0x00c09a0000000fff
	/* 16MB kernel R/W data size from 0x0, DPL 0 */
	.quad 0x00c0920000000fff
	.quad 0x0000000000000000	/* TEMPORARY - don't use */
	.fill 252, 8, 0			/* space for LDT's and TSS's etc */
