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
 * the page directory eventually.
 */

KERNEL_DATA_SEG = 0x10 	# selector of kernel data seg.
			# 0x10: 2th item of gdt, RPL: 0

.global idt, gdt, pg_dir, tmp_floppy_area
.global startup_32

.text
pg_dir:
startup_32:
	movl	$KERNEL_DATA_SEG, %eax	# let ds,es,fs,gs has the same segment
	mov 	%ax, %ds
	mov 	%ax, %es
	mov 	%ax, %fs
	mov 	%ax, %gs
	lss 	stack_start, %esp

	call	setup_idt
	call 	setup_gdt

	movl 	$KERNEL_DATA_SEG, %eax	# need reload all the segment registers
	mov 	%ax, %ds		# after changing gdt. CS was already
	mov 	%ax, %es		# reloaded in 'setup_gdt'
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
 * NOTE! 486 should set bit 16, to check for write-protect in supervisor
 * mode. Then it would be unnecessary with the "verify_area()"-calls.
 * It is used for implement "copy on write"
 * 486 users probably want to set the NE (#5) bit also, so as to use
 * int 16 for math errors.
 */
	movl 	%cr0, %eax		# check math chip
	andl 	$0x80000011, %eax	# Save PG,PE,ET
/* "orl $0x10020,%eax" here for 486 might be good */
	orl 	$2, %eax		# set MP
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
	je 1f
	/* no coprocessor: have to set EM bits */
	movl 	%cr0, %eax
	xorl 	$6, %eax	/* reset MP, set EM */
	movl 	%eax, %cr0
	ret

.align 4 /* has coprocessors, set 287/s87 to protect mode */
1:	.byte 0xdb, 0xe4		/* "fsetpm" for 287, ignored by 387 */
	ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It then loads
 *  idt. Everything that wants to install itself
 *  in the idt-table may do so themselves. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok. This routine will be over-
 *  written by the page tables.
 */
setup_idt:
	lea 	ignore_int, %edx	# lea: load effective address
	movl 	$0x00080000, %eax
	movw 	%dx, %ax		/* selector = 0x0008 = cs */
	movw 	$0x8e00, %dx		/* 0x8e00: inter gate, dpl=0, present */

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
 *  This routines sets up a new gdt and loads it.
 *  Only two entries are currently built, the same
 *  ones that were built in init.s. The routine
 *  is VERY complicated at two whole lines, so this
 *  rather long comment is certainly needed :-).
 *  This routine will be overwritten by the page tables.
 */
setup_gdt:
	lgdt	gdt_descr
	ret

/*
 * I put the kernel page tables right after the page directory,
 * using 4 of them to span 16 Mb of physical memory. People with
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

# main func seems not use 3 $0, so mark it
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
	.asciz "Unknown interrupt\n"

.align 4
ignore_int:
	pushl 	%eax
	pushl 	%ecx
	pushl 	%edx
	push 	%ds
	push 	%es
	push 	%fs

	movl 	$KERNEL_DATA_SEG, %eax
	mov 	%ax, %ds
	mov 	%ax, %es
	mov 	%ax, %fs
	pushl 	$int_msg	# int_msg is param of print();
	call 	printk		# printk() implemented in C code
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
 * This routine sets up paging by setting the page bit
 * in cr0. The page tables are set up, identity-mapping
 * the first 16MB. The pager assumes that no illegal
 * addresses are produced (ie >4Mb on a 4Mb machine).
 *
 * NOTE! Although all physical memory should be identity
 * mapped by this routine, only the kernel page functions
 * use the >1Mb addresses directly. All "normal" functions
 * use just the lower 1Mb, or the local data space, which
 * will be mapped to some other place - mm keeps track of
 * that.
 *
 * For those with more memory than 16 Mb - tough luck. I've
 * not got it, why should you :-) The source is here. Change
 * it. (Seriously - it shouldn't be too difficult. Mostly
 * change some constants etc. I left it at 16Mb, as my machine
 * even cannot be extended past that (ok, but it was cheap :-)
 * I've tried to show which constants to change by having
 * some kind of marker at them (search for "16Mb"), but I
 * won't guarantee that's all :-( )
 */
.align 4
setup_paging:
	movl 	$1024 * 5, %ecx		/* 5 pages - pg_dir + 4 page tables */
	xorl 	%eax, %eax
	xorl 	%edi, %edi		/* pg_dir is at 0x000 */
	cld
	rep
		stosl
/* 
 * Format of page dir and page:
 * 31 ............ 12 11~9 8 7 6 5 4 3 2 1 0
 *   page fram addr    AVL 0 0 D A 0 0 U R P
 *                                     / /
 *                                     S W
 *  P: present
 *  R/W: read/write	0: r/e, 1:r/w/e
 *  U/S: user/supervisor 0: only 0, 1, 2 user can access, 1: all user can access
 *  A: accessd
 *  D: dirty, when CPU exec write command at this page, will set D to 1
 * */
	/* $pg0 + 7 = 0x00001007, $pg1 + 7 = 0x00002007, and so on */
	movl 	$pg0 + 7, pg_dir	/* set present bit/user r/w */
	movl 	$pg1 + 7, pg_dir + 4	/*  --------- " " --------- */
	movl 	$pg2 + 7, pg_dir + 8	/*  --------- " " --------- */
	movl 	$pg3 + 7, pg_dir + 12	/*  --------- " " --------- */

	/* assign physical memory address mapping into 4 page table */
	movl 	$pg3 + 4092, %edi	/* edi = address of last page entry */
	movl 	$0xfff007, %eax		/* 16MB - 4096 + 7 (r/w user,p)=>last */
	std				/* physical page address */
1:	stosl			/* fill pages backwards - more efficient :-) */
	subl 	$0x1000, %eax		/* stos: store eax value to es:edi */
	jge 	1b			/* jump if eax >= 0 */
	cld				/* setup done, reset direction flag */

	xorl 	%eax, %eax		/* pg_dir is at 0x0000 */
	movl 	%eax, %cr3		/* load addr of pg_dir into cr3 */
					/* movl to cr3 will refresh TLB */
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
	.quad 0x00c09a0000000fff	/* kernel code seg., 16MB, dpl 0 */
	.quad 0x00c0920000000fff	/* kernel data seg., 16MB, dpl 0 */
	.quad 0x0000000000000000	/* TEMPORARY - don't use */
	.fill 252, 8, 0			/* space for LDT's and TSS's etc */
