SYSWRITE = 4
.global mywrite, myadd
.text
mywrite:
	push	%rbp
	mov	%rsp, %rbp
	push	%rbx
	mov	%edi, -0x4(%rbp)
	mov	%esi, -0x8(%rbp)
	mov	%edx, -0x12(%rbp)

	mov	-0x4(%rbp), %rbx
	mov	-0x8(%rbp), %rcx
	mov	-0x12(%rbp), %edx
	mov	$SYSWRITE, %rax
	int	$0x80
	pop	%rbx
	pop	%rbp
	retq
# void myadd(int a, int b, int *res)
myadd:
	push	%rbp
	mov	%rsp, %rbp
	mov	%edi, -0x4(%rbp)
	mov	%esi, -0x8(%rbp)
	mov	%rdx, -0x10(%rbp)
	mov	-0x8(%rbp), %eax	# eax = b
	mov	-0x4(%rbp), %edx	# edx = a
	add	%eax, %edx		# edx = a + b
	mov	-0x10(%rbp), %rax
	mov	%edx, (%rax)
	pop	%rbp
	retq
