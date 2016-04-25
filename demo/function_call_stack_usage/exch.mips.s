	.file	1 "main.c"
	.section .mdebug.abi32
	.previous
	.gnu_attribute 4, 3
	.abicalls
	.text
	.align	2
	.globl	swap
	.set	nomips16
	.ent	swap
	.type	swap, @function
swap:
	.frame	$fp,24,$31		# vars= 8, regs= 1/0, args= 0, gp= 8
	.mask	0x40000000,-4
	.fmask	0x00000000,0
	.set	noreorder
	.set	nomacro
	
	addiu	$sp,$sp,-24
	sw	$fp,20($sp)
	move	$fp,$sp
	sw	$4,24($fp)
	sw	$5,28($fp)
	lw	$2,24($fp)
	lw	$2,0($2)
	sw	$2,8($fp)
	lw	$2,28($fp)
	lw	$3,0($2)
	lw	$2,24($fp)
	sw	$3,0($2)
	lw	$2,28($fp)
	lw	$3,8($fp)
	sw	$3,0($2)
	move	$sp,$fp
	lw	$fp,20($sp)
	addiu	$sp,$sp,24
	j	$31
	nop

	.set	macro
	.set	reorder
	.end	swap
	.size	swap, .-swap
	.align	2
	.globl	main
	.set	nomips16
	.ent	main
	.type	main, @function
main:
	.frame	$fp,40,$31		# vars= 8, regs= 2/0, args= 16, gp= 8
	.mask	0xc0000000,-4
	.fmask	0x00000000,0
	.set	noreorder
	.set	nomacro
	
	addiu	$sp,$sp,-40
	sw	$31,36($sp)
	sw	$fp,32($sp)
	move	$fp,$sp
	li	$2,16			# 0x10
	sw	$2,24($fp)
	li	$2,32			# 0x20
	sw	$2,28($fp)
	addiu	$2,$fp,28
	addiu	$3,$fp,24
	move	$4,$3
	move	$5,$2
	.option	pic0
	jal	swap
	nop

	.option	pic2
	lw	$3,24($fp)
	lw	$2,28($fp)
	subu	$2,$3,$2
	move	$sp,$fp
	lw	$31,36($sp)
	lw	$fp,32($sp)
	addiu	$sp,$sp,40
	j	$31
	nop

	.set	macro
	.set	reorder
	.end	main
	.size	main, .-main
	.ident	"GCC: (GNU) 4.4.5-1.5.5p4"
