#32 bit protect mode
#two task: A task print "A", B task print "B"
#will context switch when RTC interrupt

LATCH = 11931	# init count value of RTC, we want to have 100hz
		# so 1193182/100 ~= 11931 

#selector format is shown below:
#bit 15 ... 3  2   1 0
#    <offset>  TI  RPL
# TI: using gdt or ldt (0:gdt, 1:ldt)
# RPL: Requested Privilege Level (0~3)
KERN_DATA_SEL = 0x10 #selector of kernel data seg. (0x10 = 2th item of gdt, RPL: 0)
SCRN_SEL = 0x18	#selector of screen seg.     (0x18 = 3rd item of gdt, RPL:0)
TSS0_SEL = 0x20	#selector of task 0 tss seg. (0x20 = 4th item of gdt, RPL:0)
LDT0_SEL = 0x28	#selector of task 0 ldt seg. (0x28 = 5th item of gdt, RPL:0)
TSS1_SEL = 0X30	#selector of task 1 tss seg. (0x30 = 6th item of gdt, RPL:0)
LDT1_SEL = 0x38	#selector of task 1 ldt seg. (0x38 = 7th item of gdt, RPL:0)
TSS2_SEL = 0X40	#selector of task 2 tss seg. (0x40 = 8th item of gdt, RPL:0)
LDT2_SEL = 0x48	#selector of task 2 ldt seg. (0x48 = 9th item of gdt, RPL:0)

LDT_1_SEL = 0x0f#selector of 1th ldt item (RPL:3)
LDT_2_SEL = 0x17#selector of 2th ldt item (RPL:3)

#interrupt gate property
IGATE_DEFAULT_PROP = 0x8e00 #P = 1，DPL = 0
IGATE_PIT_PROP = 0x8e00 #P = 1，DPL = 0
IGATE_SYS_PROP = 0xef00 #P = 1, DPL = 3

#PIT variables
CHANNEL0 = 0x40	#channel 0 port number
CMD_REG = 0x43	#command port number
PIT_MODE3 = 0x36

#interrupt vector number
PIT_VEC_NUM = 0x08
SYS_VEC_NUM = 0x80
CMD_EOI = 0x20		#End of interrupt, this will re-enable the interrupt vec

#IRQ
IRQ0 = 0x20

#others
DESC_SIZE = 0x08	#descriptor size is 8 byte
SYSTEM_CALL = 0x80
DELAY_COUNT = 0x0fff
DISABLE_NT = 0xffffbfff	#disable NT flag of eflags

.global startup_32
.text
startup_32:				#from now, CPU is at 32bit protect mode
	movl	$KERN_DATA_SEL, %eax	#load kernel data seg. selector to eax
	mov	%ax, %ds		#setup data seg. register
	lss	init_stack, %esp	#lss: load pointer using ss, the offset
       					#of init_stack is placed in the esp and
				       	#the seg. is placed in ss
					#before this, ss:esp = 0x07c0:0400 (
					#setup by boot.s), after this line,
					#ss:esp = 0x0010:<offset of init_stack>
	call	setup_idt		#reinit idtr and gdtr (when using <call>
	call	setup_gdt		#cpu will push the address of next
					#instruction into stack and let esp
					#point to it)

	#because we reinit gdt, we should reinit all seg. register
	movl	$KERN_DATA_SEL, %eax	#load kernel data seg. selector to eax
	mov	%ax, %ds		#selector in gdt table
	mov	%ax, %es
	mov	%ax, %fs
	mov	%ax, %gs
	lss	init_stack, %esp

	#set PIT(Programmable Internal Timer) clock to 100 HZ
	movb	$PIT_MODE3, %al		#switch to mode 3
	movl	$CMD_REG, %edx
	outb	%al, %dx
	movl	$LATCH, %eax		#the PIT chip default clock speed is
	movl	$CHANNEL0, %edx		#1.19318MHZ,$LATCH = 11930
	outb	%al, %dx		#write LATCH to channel 0 will cause
	movb	%ah, %al		#1193180/11930 ~= 100 HZ
	outb	%al, %dx

	#setup timer interrupt and system call interrupt idt entry
	lea	timer_interrupt, %edx
	movl	$0x00080000, %eax
	movw	%dx, %ax		#eax = 0x0008+<16bit bottom address of
					#timer interrupt>
	movw	$IGATE_PIT_PROP, %dx	#edx = <16bit top address of timer
					#interrupt> + interrupte gate property
	movl	$PIT_VEC_NUM, %ecx	#locate address of PIT_VEC_NUM in idt
	lea	idt(, %ecx, DESC_SIZE), %esi
	movl	%eax, (%esi)
	movl	%edx, 4(%esi)

	lea	system_interrupt, %edx
	movw	%dx, %ax
	movw	$IGATE_SYS_PROP, %dx	#setup system call interrupt gate property
	movl	$SYS_VEC_NUM, %ecx	#using interrupt number 0x80
	lea	idt(, %ecx, DESC_SIZE), %esi
	movl	%eax, (%esi)
	movl	%edx, 4(%esi)

# unmask the timer interrupt.
#	movl $0x21, %edx
#	inb %dx, %al
#	andb $0xfe, %al
#	outb %al, %dx

# Move to user mode (task 0)
	pushfl				#save eflags
	andl	$DISABLE_NT, (%esp)	#set NT(Nested Task) bit to 0
	popfl				#restore eflags
	movl	$TSS0_SEL, %eax
	ltr	%ax			#load tss0 selector into tr register
	movl	$LDT0_SEL, %eax
	lldt	%ax
	movl	$0, current		#current = task 0
	sti				#enable interrupt

	#begin make a fake stack for return to task 0
	pushl	$LDT_2_SEL		#original ss selector
	pushl	$usr_stk0		#original esp
	pushfl				#original eflags
	pushl	$LDT_1_SEL		#original cs selector
	pushl	$task0			#original eip
	iret

#setup all interrupt descriptor (256 count) to defalut descriptor: ignore_idt
#interrupt gate descriptor format shown below:
#       bit 31 ... 16 15 14 13 12 11 10 9 8 7 6 5 4 ... 0
#          base 31~16 P   DPL  0  1  1  1 0 0 0 0  AVL
#       bit 31 ... 16 15 ....................... 0
#        seg. selector     base 15~0
setup_idt:
	lea	ignore_int, %edx	#lea (load effective address), load the
					#address of ignore_int into edx
	movl	$0x00080000, %eax	#eax = 0x00080000(kernel code seg. selector)
	movw	%dx, %ax		#eax = 0x0008000+<16bit bottom half of
					#ignore_int address>
	movw	$IGATE_DEFAULT_PROP, %dx#edx = <16bit top half of ignore_int
					#address>+interrupt gate prop
	lea	idt, %edi		#load address of idt into edi
	mov	$256, %ecx		#ecx=0x100(256)
rep_idt:				#fill idt descriptor 256 times
	movl	%eax, (%edi)
	movl	%edx, 4(%edi)
	addl	$DESC_SIZE, %edi
	dec	%ecx
	jne	rep_idt			#jump is zf = 0(i.e. zf not set)
	lidt	lidt_opcode		#after setup idt descriptor done
					#load idt_opcode into idtr register
	ret

setup_gdt:
	lgdt	lgdt_opcode
	ret

write_char:
	push	%gs
	pushl	%ebx
	mov	$SCRN_SEL, %ebx
	mov	%bx, %gs		#gs = selector of vga seg.
	movl	scr_loc, %ebx
	shl	$1, %ebx		#ebx shift left 1 bit, this is because
	movb	%al, %gs:(%ebx)		#every word need 2 byte space, 1 byte
	shr	$1, %ebx		#for word, 1 byte for word property
	incl	%ebx
	cmpl	$2000, %ebx		#80line * 25 column = 2000 words total
	jb	1f			#if ebx < 2000, jump to 1
	movl	$0, %ebx
1:	movl	%ebx, scr_loc
	popl	%ebx
	pop	%gs
	ret

.align 2
ignore_int:
	push	%ds
	pushl	%eax
	movl	$KERN_DATA_SEL, %eax
	mov	%ax, %ds
	movl	$90, %eax		#write "Z" to screen
	call	write_char
	popl	%eax
	pop	%ds
	iret

.align 2
timer_interrupt:
	push	%ds
	pushl	%eax
	movl	$KERN_DATA_SEL, %eax
	mov	%ax, %ds
	movb	$CMD_EOI, %al		#send EOI command will re-enable interrupt
	outb	%al, $IRQ0
	movl	$1, %eax		#if current is task1 jump to 1:
	cmpl	%eax, current
	je	1f
	movl	$2, %eax		#if current is task2 jump to 2:
	cmpl	%eax, current
	je	2f
	movl	$1, current		#current task is task0, swith to task1
	ljmp	$TSS1_SEL, $0		#after ljmp tss1, next line (jmp end)
	jmp	end			#will save to eip of tss0, then cpu jump
1:	movl	$2, current		#to execute task1's code, next time if
	ljmp	$TSS2_SEL, $0		#ljmp back to tss0, cpu will begin
	jmp	end			#execute "jmp end"
2:	movl	$0, current
	ljmp	$TSS0_SEL, $0
end:	popl	%eax
	pop	%ds
	iret

.align 2
system_interrupt:
	push	%ds
	pushl	%edx
	pushl	%ecx
	pushl	%ebx
	pushl	%eax
	movl	$KERN_DATA_SEL, %edx	#0x10 is selector of kernel data seg.
	mov	%dx, %ds
	call	write_char
	popl	%eax
	popl	%ebx
	popl	%ecx
	popl	%edx
	pop	%ds
	iret

current:.long	0
scr_loc:.long	0

.align 2
lidt_opcode:				#will load this variable into idtr
	.word	256*8-1			#length of idt
	.long	idt			#address of idt
lgdt_opcode:
	.word	(end_gdt-gdt)-1
	.long	gdt

#seg. descriptor format shown below:
#bit 31 ... 24 23 22  21 20  19 ... 16       15 14 13 12 11 ... 8 7 ... 0
#   base 31~24 G  D/B 0  avl seg limit 19~16 P   DPL  S    TYPE   base 23~10
#bit 31 ........................... 16       15 ....................... 0
#   base 15~0                                seg limit 15~0
.align 8
idt:	.fill	256, 8, 0		#create 256 idt descriptors, defalut
					#value is all zero
gdt:	.quad	0x0000000000000000	#first entry is not used
	#32bit R/E seg. base addr. 0x0, len 8MB, DPL 0
	.quad	0x00c09a00000007ff
	#32bit R/W seg. base addr. 0x0, len 8MB, DPL 0
	.quad	0x00c09200000007ff
	#32bit R/W seg. base addr. 0xb8000, len 8KB, DPL 0
	.quad	0x00c0920b80000002
	#32bit TSS, base addr. tss0, len 104(0x68)Byte, DPL 3
	.word	0x68, tss0, 0xe900, 0x0
	#LDT, base addr. ldt0, len 64(0x40)Byte, DPL 3
	.word	0x40, ldt0, 0xe200, 0x0
	.word	0x68, tss1, 0xe900, 0x0	#tss1 seg.
	.word	0x40, ldt1, 0xe200, 0x0	#ldt1 seg.
	.word	0x68, tss2, 0xe900, 0x0	#tss2 seg.
	.word	0x40, ldt2, 0xe200, 0x0	#ldt2 seg.
end_gdt:
	.fill	128, 4, 0		#512 byte kernel stack size
init_stack:
	#following two is used for lss instruction
	.long	init_stack		#store offset of kernel stack
	.word	KERN_DATA_SEL		#stroe kernel stack seg. selector

#ldt and tss for task 0
.align 8
ldt0:	.quad	0x0000000000000000	#first entry, not used
	#32bit R/E seg. base addr. 0x0, len 4MB, DPL 3
	.quad	0x00c0fa00000003ff	#code seg.
	#32bit R/W seg. base addr. 0x0, len 4MB, DPL 3
	.quad	0x00c0f200000003ff	#data seg.
tss0:	.long	0			#previous tss selector link
	.long	krn_stk0, KERN_DATA_SEL	#esp0, ss0
	.long	0, 0, 0, 0, 0		#esp1, ss1, esp2, ss2, cr3
	.long	0, 0, 0, 0, 0		#eip, eflags, eax, ecx, edx
	.long	0, 0, 0, 0, 0		#ebx, esp, ebp, esi, edi
	.long	0, 0, 0, 0, 0, 0	#es, cs, ss, ds, fs, gs
	.long	LDT0_SEL, 0x08000000	#ldt, I/O port permissions
	#The TSS contains a 16-bit pointer to I/O port permissions bitmap for
	#the current task. Usually set up by OS when a task is started,
	#specifies individual ports to which the program should have access.
	#"0" is stored if the program has permission to access a port,
	#else, "1" is stored.The feature operates as follows: when a program 
	#issues an x86 I/O port instruction (IN/OUT), the hardware will check
	#I/O privilege level (IOPL) to see if the program has access right.
	#If CPL <= IOPL the program does have I/O port access right.
	#The hardware will then check the I/O permissions bitmap in the TSS
	#to see if that program can access the specific port.
	.fill	128, 4, 0		#512 byte task 0 kernel stack size
krn_stk0:

#ldt and tss for task 1
.align 8
ldt1:	.quad	0x0000000000000000	#first entry, not used
	#32bit R/E seg. base addr. 0x0, len 4MB, DPL 3
	.quad	0x00c0fa00000003ff	#code seg.
	#32bit R/W seg. base addr. 0x0, len 4MB, DPL 3
	.quad	0x00c0f200000003ff	#data seg.
tss1:	.long	0			#previous tss selector link
	.long	krn_stk1, KERN_DATA_SEL	#esp0, ss0
	.long	0, 0, 0, 0, 0		#esp1, ss1, esp2, ss2, cr3
	.long	task1, 0x0200		#eip, eflags (0x0200 = enable interrupt)
	.long	0, 0, 0, 0		#eax, ecx, edx, ebx
	.long	usr_stk1, 0, 0, 0	#esp, ebp, esi, edi
	.long	LDT_2_SEL, LDT_1_SEL	#es, cs
	.long	LDT_2_SEL, LDT_2_SEL	#ss, ds
	.long	LDT_2_SEL, LDT_2_SEL	#fs, gs
	.long	LDT1_SEL, 0x08000000	#ldt item in gdt, trace bitmap
	.fill	128, 4, 0		#512 byte task 1 kernel stack size
krn_stk1:

#ldt and tss for task 2
.align 8
ldt2:	.quad	0x0000000000000000	#first entry, not used
	#32bit R/E seg. base addr. 0x0, len 4MB, DPL 3
	.quad	0x00c0fa00000003ff	#code seg.
	#32bit R/W seg. base addr. 0x0, len 4MB, DPL 3
	.quad	0x00c0f200000003ff	#data seg.
tss2:	.long	0			#previous tss selector link
	.long	krn_stk2, KERN_DATA_SEL	#esp0, ss0
	.long	0, 0, 0, 0, 0		#esp1, ss1, esp2, ss2, cr3
	.long	task2, 0x0200		#eip, eflags (0x0200 = enable interrupt)
	.long	0, 0, 0, 0		#eax, ecx, edx, ebx
	.long	usr_stk2, 0, 0, 0	#esp, ebp, esi, edi
	.long	LDT_2_SEL, LDT_1_SEL	#es, cs
	.long	LDT_2_SEL, LDT_2_SEL	#ss, ds
	.long	LDT_2_SEL, LDT_2_SEL	#fs, gs
	.long	LDT2_SEL, 0x08000000	#ldt item in gdt, trace bitmap
	.fill	128, 4, 0		#512 byte task 1 kernel stack size
krn_stk2:

#task 0's code
task0:
	movl	$LDT_2_SEL, %eax
	movw	%ax, %ds
	movb	$65, %al		#put ascii code of "A"(65) into al
	int	$SYSTEM_CALL
	movl	$DELAY_COUNT, %ecx	#delay for a while
1:	loop	1b
	jmp	task0
	.fill	128, 4, 0		#task 0 512 byte user stack size
usr_stk0:

#task 1's code
task1:
	movl	$LDT_2_SEL, %eax
	movw	%ax, %ds
	movb	$66, %al		#put ascii code of "B" (66) into al
	int	$SYSTEM_CALL
	movl	$DELAY_COUNT, %ecx
1:	loop	1b
	jmp	task1
	.fill	128, 4, 0		#task 1 512 byte user stack size
usr_stk1:

#task 2's code
task2:
	movl	$LDT_2_SEL, %eax
	movw	%ax, %ds
	movb	$67, %al		#put ascii code of "C" (67) into al
	int	$SYSTEM_CALL
	movl	$DELAY_COUNT, %ecx
1:	loop	1b
	jmp	task2
	.fill	128, 4, 0		#task 2 512 byte user stack size
usr_stk2:
