/*
 *  linux/mm/page.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * page.s contains the low-level page-exception code.
 * the real work is done in mm.c
 */
.global page_fault

/* 
 * when page fault, CPU push error code into stack, and CR2 store the page fault
 * address
 */
page_fault:
	xchgl %eax, (%esp)  # get error code and store eax to (%esp)
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10, %edx    # change to kernel data seg.
	mov %dx, %ds
	mov %dx, %es
	mov %dx, %fs
	movl %cr2, %edx	    # get page fault address
	pushl %edx
	pushl %eax
	testl $1, %eax	    # test present bit (bit0) of error code
	jne 1f		    # jump if present bit is set
	call do_no_page
	jmp 2f
1:	call do_wp_page
2:	addl $8, %esp	    # skip two param
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret
