.code16
# rewrite with AT&T syntax by falcon <wuzhangjin@gmail.com> at 081012
#
# SYS_SIZE is the number of clicks (16 bytes) to be loaded.
# 0x3000 is 0x30000 bytes = 192kB, more than enough for current
# versions of linux
#
.set SYSSIZE, 0x3000
#
#	bootsect.s		(C) 1991 Linus Torvalds
#
# bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves
# iself out of the way to address 0x90000, and jumps there.
#
# It then loads 'setup' directly after itself (0x90200), and the system
# at 0x10000, using BIOS interrupts. 
#
# NOTE! currently system is at most 8*65536 bytes long. This should be no
# problem, even in the future. I want to keep it simple. This 512 kB
# kernel size should be enough, especially as this doesn't contain the
# buffer cache as in minix
#
# The loader has been made as simple as possible, and continuos
# read errors will result in a unbreakable loop. Reboot by hand. It
# loads pretty fast by getting whole sectors at a time whenever possible.

.set BOOTLEN, 1			# nr of bootset-sector
.set SETUPLEN, 4		# nr of setup-sectors
.set BOOTSEG, 0x07c0		# original address of boot-sector
.set INITSEG, 0x9000		# we move boot here - out of the way
.set SETUPSEG, 0x9020		# setup starts here
.set SYSSEG, 0x1000		# system loaded at 0x10000 (65536).
.set ENDSEG, SYSSEG + SYSSIZE	# where to stop loading

# ROOT_DEV:	0x000 - same type of floppy as boot.
#		0x301 - first partition on first drive etc
#
# above is old naming rule about device number
# already using new rule after linux 0.95
# ROOT_DEV = major << 8 + minor
# major: 1-memory, 2-floppy, 3-harddisk, 4-ttyx, 5-tty, 6-...
# 0x301 = 1st partition on 1st drive
.set ROOT_DEV, 0x301

.global _start, begtext, begdata, begbss, endtext, enddata, endbss

.text
begtext:
.data
begdata:
.bss
begbss:

.text
	ljmp    $BOOTSEG, $_start   # cs = 0x07c0
_start:
	mov	$BOOTSEG, %ax
	mov	%ax, %ds	    # ds = 0x07c0
	mov	$INITSEG, %ax
	mov	%ax, %es	    # es = 0x9000
	mov	$256, %cx
	sub	%si, %si	    # source addr ds:si = 0x07c0:0x0000 = 0x7c00
	sub	%di, %di	    # dest addr es:di = 0x9000:0x0000 = 0x90000
	rep			# repeat 256 count, 2byte/count, total = 512byte
		movsw
	ljmp	$INITSEG, $go	    # cs = 0x9000
go:	mov	%cs, %ax	    # ds = es = ss = cs = 0x9000
	mov	%ax, %ds
	mov	%ax, %es
	# put stack at 0x9ff00. Make sure ss:sp must > 0x90200 + <setup size>
	mov	%ax, %ss
	mov	$0xff00, %sp	    # arbitrary value >> 512

# load the setup-sectors directly after the bootblock.
# Note that 'es' is already set up.
load_setup:
	mov	$0x0000, %dx	    # dl: drive 0, dh: head 0 (1st floppy)
	mov	$0x0002, %cx	    # cl: sector 2 (start from 1), ch: track 0
	mov	$0x0200, %bx	    # address = 512,offset in INITSEG to load to
	.set    AX, 0x0200 + SETUPLEN
        mov     $AX, %ax	    # service 2 + nr of sectors to read
	int	$0x13		    # read it to es:bx (0x9000:0x0200)
	jnc	ok_load_setup	    # ok - continue (jump if no error)

	# load setup sector fail, reset disk and load again
	mov	$0x0000, %dx	    # dl: drive 0 (1st floppy)
	mov	$0x0000, %ax	    # ah: service 0 (reset the diskette)
	int	$0x13
	jmp	load_setup

ok_load_setup:
	# Get disk drive parameters, specifically nr of sectors/track
	mov	$0x00, %dl	# dl: drive 0 (1st floppy)
	mov	$0x0800, %ax	# AH = 8 is get drive parameters
	int	$0x13		# if get drive parameters success
				# ah = return code, dh = numbers of heads
				# cx[7:6] = numbers of cylinders (tracks)
				# cx[5:0] = numbers of sectors per track
				# es:di=pointer to floppy drive parameter table
	mov	$0x00, %ch	# bacause max number of sectors per track 
				# will not beyond 0xff(255), cl can totally
				# handle, so set ch to 0x00 is safe
	mov	%cx, %cs:sectorspt + 0	# %cs means sectorspt is in %cs
	mov	$INITSEG, %ax	# because es has changed when we using int 0x13
	mov	%ax, %es	# to get drive param, so set it back to 0x9000

	# Print some inane message
	mov	$0x03, %ah		# service 3: read page 0 (bh) cursor pos
	xor	%bh, %bh		# stroed at dh: row, dl: column
	int	$0x10
	
	mov	$20, %cx		# write string, totally 20 byte
	mov	$0x0007, %bx		# page 0 (bh), color mode 7 (light gray)
	mov     $msg1, %bp		# es:bp = offset of string
	mov	$0x1301, %ax		# write string (ah), mode 1: move cursor
	int	$0x10

	# ok, we've written the message,
	# now we want to load the system (at 0x10000)
	mov	$SYSSEG, %ax		# ax = 0x1000
	mov	%ax, %es		# segment of 0x010000
	call	read_it

	# read kernel ok, print " done" message
	mov	$INITSEG, %ax
	mov	%ax, %es
	mov	$0x03, %ah		# service 3: read cursor pos, stored at:
	xor	%bh, %bh		# dh: row, dl: column
	int	$0x10
	
	mov	$9, %cx			# write string, totally 20 byte
	mov	$0x0007, %bx		# page 0 (bh), color mode 7 (light gray)
	mov     $msg2, %bp		# es:bp = offset of string
	mov	$0x1301, %ax		# write string (ah), mode 1: move cursor
	int	$0x10

	call	kill_motor

# After that we check which root-device to use. If the device is
# defined (!= 0), nothing is done and the given device is used.
# Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
# on the number of sectors that the BIOS reports currently
	mov	%cs:root_dev + 0, %ax
	cmp	$0, %ax
	jne	root_defined

	# root_dev not defined, begin auto detect
	mov	%cs:sectorspt + 0, %bx
	mov	$0x0208, %ax		# /dev/ps0 - 1.2Mb
	cmp	$15, %bx		# if sectorspt = 15, it is 1.2MB floppy
	je	root_defined
	mov	$0x021c, %ax		# /dev/PS0 - 1.44Mb
	cmp	$18, %bx		# if sectorspt = 18, it is 1.44MB floppy
	je	root_defined
undef_root:
	jmp undef_root
root_defined:
	mov	%ax, %cs:root_dev + 0	# store root_dev

# after that (everyting loaded), we jump to
# the setup-routine loaded directly after
# the bootblock:

	ljmp	$SETUPSEG, $0		# cs = 0x9020

# This routine loads the system at address 0x10000, making sure
# no 64kB boundaries are crossed. We try to load it as fast as
# possible, loading whole tracks whenever we can.
#
# in:	es - starting address segment (normally 0x1000)
#
sreaded:.word BOOTLEN + SETUPLEN    # sectors already readed of current track
chead:	.word 0			    # current head
ctrack:	.word 0			    # current track

read_it:
	mov	%es, %ax	# ax = 0x1000
	test	$0x0fff, %ax	# ax & 0x0fff, if = 0 => zf =1, if != 0, zf = 0
die:	jne 	die		# es must be at 64kB boundary
				# jne: jump if zf = 0
	xor 	%bx, %bx	# bx is starting address within segment
rp_read:
	mov 	%es, %ax
 	cmp 	$ENDSEG, %ax	# have we loaded all yet?
	jb	ok1_read	# jb: jump if ax < $ENDSEG(0x4000)
	ret

# calculate currently need read of sector numbers and store in ax
ok1_read:
	mov	%cs:sectorspt + 0, %ax	# ax = sectors per track
	sub	sreaded, %ax	# ax = unreaded sector numbers of current track
	mov	%ax, %cx
	shl	$9, %cx		# change sector to bytes(cx = cx * 512)
	add	%bx, %cx
	jnc 	ok2_read	# jump if cx + bx < segment size (64KB)
	je 	ok2_read	# jump if cx + bx = segemnt size (64KB)
	
	# cx + bx will overflow the segment size (64KB), set ax to remind sector
	# numbers which we can fill in this segment
	xor 	%ax, %ax
	sub 	%bx, %ax
	shr 	$9, %ax

# now, ax store the nr of sectors we should read into current segment
ok2_read:
	call 	read_track
	mov 	%ax, %cx	# cx = nr sectors readed of this time read_track
	add 	sreaded, %ax	# ax = all readed sectors on current track
	cmp 	%cs:sectorspt + 0, %ax
	jne 	ok3_read	# jump if still has unreaded sectors on current
				# track
	mov 	$1, %ax
	sub 	chead, %ax
	jne 	ok4_read

	# both head 0 and head 1 are readed, add track number
	incw    ctrack
ok4_read:
	mov	%ax, chead
	xor	%ax, %ax
ok3_read:
	mov	%ax, sreaded
	shl	$9, %cx
	add	%cx, %bx    # update offset address of next byte to read to
	jnc	rp_read	    # repeat read if current segment still has space

	# current segment is full, move to next segment
	mov	%es, %ax
	add	$0x1000, %ax
	mov	%ax, %es
	xor	%bx, %bx
	jmp	rp_read

read_track:
	push	%ax
	push	%bx
	push	%cx
	push	%dx
	mov	ctrack, %dx
	mov	sreaded, %cx
	inc	%cx		# cl = sector number (start from 1) to read
	mov	%dl, %ch	# ch = track number
	mov	chead, %dx
	mov	%dl, %dh	# dh = head number
	mov	$0, %dl
	and	$0x0100, %dx	# make sure head number not > 1
	mov	$2, %ah		# service 2 (ah): read sectors
	int	$0x13
	jc	bad_rt
	pop	%dx
	pop	%cx
	pop	%bx
	pop	%ax
	ret
# error occurred when read_track, reset diskette and read again
bad_rt:	mov	$0, %ax
	mov	$0, %dx
	int	$0x13
	pop	%dx
	pop	%cx
	pop	%bx
	pop	%ax
	jmp	read_track

#/*
# * This procedure turns off the floppy drive motor, so
# * that we enter the kernel in a known state, and
# * don't have to worry about it later.
# */
kill_motor:
	push	%dx
	mov	$0x3f2, %dx  # 0x3f2: digital control port of floppy (8bit)
			     # bit 0, 1: device number to be selected
			     # bit 2   : reset FDC IC (low active)
			     # bit 3   : enable FDC interrupt and DMA
			     # bit 4~7 : turn on the motor of floppy 0~3
	mov	$0, %al
	outsb		    #  output byte al to port number 0x03f2
	pop	%dx
	ret

sectorspt:
	.word 0

msg1:
	.byte 13, 10
	.ascii "Loading system ..."

msg2:
	.ascii " done"
	.byte 13, 10, 13, 10

	.org 508
root_dev:
	.word ROOT_DEV
boot_flag:
	.word 0xAA55
	
	.text
	endtext:
	.data
	enddata:
	.bss
	endbss:
