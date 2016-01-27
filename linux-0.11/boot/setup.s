.code16
# rewrite with AT&T syntax by falcon <wuzhangjin@gmail.com> at 081012
#
#	setup.s		(C) 1991 Linus Torvalds
#
# setup.s is responsible for getting the system data from the BIOS,
# and putting them into the appropriate places in system memory.
# both setup.s and system has been loaded by the bootblock.
#
# This code asks the bios for memory/disk/other parameters, and
# puts them in a "safe" place: 0x90000-0x901FF, ie where the
# boot-block used to be. It is then up to the protected mode
# system to read them from there before the area is overwritten
# for buffer-blocks.
#

# NOTE! These had better be the same as in bootsect.s!
.set INITSEG, 0x9000	# we move boot here - out of the way
.set SYSSEG, 0x1000	# system loaded at 0x10000 (65536).
.set SETUPSEG, 0x9020	# this is the current segment

.global _start, begtext, begdata, begbss, endtext, enddata, endbss

.text
begtext:
.data
begdata:
.bss
begbss:

.text
	ljmp $SETUPSEG, $_start	# cs = 0x9020
_start:

	# ok, the read went well so we get current cursor position and save it
	# for posterity.
	mov	$INITSEG, %ax	# this is done in bootsect already, but...
	mov	%ax, %ds	# ds = 0x9000
	mov	$0x03, %ah	# read page 0 (bh) cursor pos, store at
	xor	%bh, %bh	# dh: row, dl: column
	int	$0x10
	mov	%dx, %ds:0	# save it in known place, console_init fetches it
				# from 0x90000.

	# Get memory size (get extended mem (outside 1MB), kB),
	# this method (88h) is create from 80286, and BIOS only can return
	# maximum extened memory to 16MB. So now there are other method to
	# replace this func
	mov	$0x88, %ah	# ax store the nr of 1K blocks of memory
	int	$0x15		# starting ad address 1MB
	mov	%ax, %ds:2	# [0x90002] = ax

	# Get video-card data:
	mov	$0x0f, %ah	# ah = 0x0f: get current video mode
	int	$0x10
	mov	%bx, %ds:4	# bh = active display page number
	mov	%ax, %ds:6	# al = video mode(03h color, 07h monochrome)
				# ah = window width(nr of column)

	# check for EGA/VGA and some config parameters
	mov	$0x12, %ah	# if not support 0x12 func, bl will still be
	mov	$0x10, %bl	# 0x10, linus using this to check.
	int	$0x10		# return value: ah = destroyed(?)
	mov	%ax, %ds:8	# bh = video state:
	mov	%bx, %ds:10	#	00h color mode in effect (I/O port 3dxh)
	mov	%cx, %ds:12	#	01h mono mode in effect (I/O port 3bxh)
				# bl = installed memory:
				#  00h = 64K, 01h = 128K, 02h = 192K, 03h = 256K
				# ch = feature connector bits
				# cl = switch settings

	# Get hd0 data (copy hd0 param table)
	mov	$0x0000, %ax
	mov	%ax, %ds	    # ds = 0x0000
	lds	%ds:4 * 0x41, %si   # lds: Reads a pointer from memory
				    # value stored in 4 * 0x41 is the address of
				    # hd0 param table,
	mov	$INITSEG, %ax
	mov	%ax, %es	    # es = 0x9000
	mov	$0x0080, %di
	mov	$0x10, %cx	    # copy 16 bytes from 0x0000:[[4 * 0x41]] to
	rep			    # 0x9000:0080
		movsb

	# Get hd1 data (copy hd1 param table)
	mov	$0x0000, %ax
	mov	%ax, %ds
	lds	%ds:4 * 0x46, %si
	mov	$INITSEG, %ax
	mov	%ax, %es
	mov	$0x0090, %di
	mov	$0x10, %cx
	rep
		movsb

	# Check that there IS a hd1 :-)
	mov	$0x1500, %ax	# ah = 15h: get disk type
	mov	$0x81, %dl	# dl = drive number (bit 7 set for hard disk)
	int	$0x13		# 0x80 means 1st HD, 0x81 means 2nd HD
	jc	no_disk1	# jump if error occurred
	cmp	$3, %ah		# ah: type code, 01h: floppy without change-line
	je	is_disk1	# 02h: floppy with change-line 03h: hard disk

# if disk 1(2nd disk) not exist, clear 16 byte from 0x9000:0090
no_disk1:
	mov	$INITSEG, %ax
	mov	%ax, %es
	mov	$0x0090, %di
	mov	$0x10, %cx
	mov	$0x00, %ax
	rep
		stosb

is_disk1:
	# now we want to move to protected mode ...
	cli			# no interrupts allowed ! 

	# first we move the system to it's rightful place
	mov	$0x0000, %ax
	cld			# 'direction'=0, "mov" will  moves forward

# each time move one segment size(64KB), totally move 8 segments(0x1000~0x8ffff)
do_move:
	mov	%ax, %es	# es = 0x0000 (destination segment)
	add	$0x1000, %ax
	cmp	$0x9000, %ax
	jz	end_move	# jump if ax = 0x9000
	mov	%ax, %ds	# source segment
	sub	%di, %di
	sub	%si, %si
	mov 	$0x8000, %cx
	rep
		movsw
	jmp	do_move

# now, kernel has been moved to 0x0000, then we load the segment descriptors
end_move:
	mov	$SETUPSEG, %ax	# right, forgot this at first. didn't work :-)
	mov	%ax, %ds	# ds = 0x9020
	lidt	idt_48		# load idt with 0,0
	lgdt	gdt_48		# load gdt with whatever appropriate

	# that was painless, now we enable A20

	# -- old method --
	#call	empty_8042	# 8042 is the keyboard controller
	#mov	$0xd1, %al	# command write
	#out	%al, $0x64
	#call	empty_8042
	#mov	$0xdf, %al	# A20 on
	#out	%al, $0x60
	#call	empty_8042

	# -- new method --
	inb     $0x92, %al	# open A20 line(Fast Gate A20).
	testb   $02, %al	# if bit 1 already set, skip write
	jnz	a20_ok
	orb     $0b00000010, %al
	andb    $0b11111110, %al    # bit 0 sometimes is write-only, and writing
	outb    %al, $0x92	    # a one there causes a reset, so prevent it.

# well, that went ok, I hope. Now we have to reprogram the interrupts :-(
# we put them right after the intel-reserved hardware interrupts, at
# int 0x20-0x2F. There they won't mess up anything. Sadly IBM really
# messed this up with the original PC, and they haven't been able to
# rectify it afterwards. Thus the bios puts interrupts at 0x08-0x0f,
# which is used for the internal hardware interrupts as well. We just
# have to reprogram the 8259's, and it isn't fun.

# 8259A-1 port: 20h~21h, 8259A-2 port: a0h~a1h
a20_ok:
	# ICW1
	mov	$0x11, %al		# initialization sequence(ICW1)
					# ICW4 needed(1), CASCADE mode,
					# Level-triggered
	out	%al, $0x20		# send it to 8259A-1
	.word	0x00eb, 0x00eb		# jmp $+2, jmp $+2 (delay for a while)
	out	%al, $0xa0		# and to 8259A-2
	.word	0x00eb, 0x00eb

	# ICW2
	mov	$0x20, %al		# start of hardware int's (0x20)(ICW2)
	out	%al, $0x21		# from 0x20-0x27
	.word	0x00eb, 0x00eb
	mov	$0x28, %al		# start of hardware int's 2 (0x28)
	out	%al, $0xa1		# from 0x28-0x2f
	.word	0x00eb, 0x00eb

	# ICW3
	mov	$0x04, %al		# 		IR 7654 3210
	out	%al, $0x21		# 8259-1 is master(0000 0100) --\
	.word	0x00eb, 0x00eb		#			 	|
	mov	$0x02, %al		#                        INT    /
	out	%al, $0xa1		# 8259-2 is slave(       010 --> 2)
	.word	0x00eb, 0x00eb

	# ICW4
	mov	$0x01, %al		# 8086 mode for both, al: mode 1: need
	out	%al, $0x21		# send EOI to re-enalbe interrupt
	.word	0x00eb, 0x00eb
	out	%al, $0xa1		# now, 8259A begin works
	.word	0x00eb, 0x00eb

	# OCW1
	mov	$0xff, %al		# mask off all interrupts for now
	out	%al, $0x21
	.word	0x00eb, 0x00eb
	out	%al, $0xa1

# well, that certainly wasn't fun :-(. Hopefully it works, and we don't
# need no steenking BIOS anyway (except for the initial loading :-).
# The BIOS-routine wants lots of unnecessary data, and it's less
# "interesting" anyway. This is how REAL programmers do it.
#
# Well, now's the time to actually move into protected mode. To make
# things as simple as possible, we do no register set-up or anything,
# we let the gnu-compiled 32-bit programs do that. We just jump to
# absolute address 0x00000, in 32-bit protected mode.
	# -- old method --
	#mov	$0x0001, %ax	# protected mode (PE) bit
	#lmsw	%ax		# This is it!
	
	# -- new method --
	mov	%cr0, %eax	# get machine status (cr0|MSW)	
	bts	$0, %eax	# turn on the PE-bit (bit 0)
				# bts: bit test and set
	mov	%eax, %cr0	# protection enabled
				
				# segment-descriptor        (INDEX:TI:RPL)
	.set	sel_cs0, 0x0008 # select for code segment 0 (  001: 0:00) 
	ljmp	$sel_cs0, $0	# jmp offset 0 of code segment 0 in gdt
				# this ljmp will clean the CPU pre-fetched
				# instruction

# This routine checks that the keyboard command queue is empty
# No timeout is used - if this hangs there is something wrong with
# the machine, and we probably couldn't proceed anyway.
empty_8042:
	.word	0x00eb, 0x00eb
	in	$0x64, %al	# 8042 status port
	test	$2, %al		# is input buffer full?
	jnz	empty_8042	# yes - loop
	ret

tmp_gdt:
	.word	0, 0, 0, 0	# dummy

	.word	0x07ff		# 8MB - limit = 2047 (2048 * 4KB = 8MB)
	.word	0x0000		# base address = 0
	.word	0x9a00		# code read/exec
	.word	0x00c0		# granularity = 4096, 386 (32bit)

	.word	0x07ff		# 8MB - limit = 2047 (2048 * 4KB = 8MB)
	.word	0x0000		# base address = 0
	.word	0x9200		# data read/write
	.word	0x00c0		# granularity = 4096, 386 (32bit)

idt_48:
	.word	0			# idt limit = 0
	.word	0, 0			# idt base = 0L

gdt_48:
	.word	0x7ff			# gdt limit = (2048-1), 256 GDT entries
	.word   0x0200 + tmp_gdt, 0x0009# gdt base = 0x90200 + tmp_gdt 
	
.text
endtext:
.data
enddata:
.bss
endbss:
