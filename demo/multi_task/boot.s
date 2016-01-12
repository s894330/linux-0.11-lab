;boot.s
;1. load kernel to memory 0x10000 using BIOS interrupt
;2. move kernel from 0x10000 to 0x0
;3. into x86 protect mode, then jump to 0x0 begin execute kernel code

BOOTSEG = 0x07c0
SYSSEG = 0x1000
SYSLEN = 17	;max kernel floppy sector size (1 sector = 512 byte)

entry start
start:
	jmpi	go, #BOOTSEG	;BIOS load this code to 0x0000:7c00,
				;but before this code exeucte,
				;cs:ip = 0x0000:7c00, so we
				;need initialize cs. after this jmpi code
				;cs:ip = 0x07c0:0005, and the real address
				;become 0x7c00 + 0x0005 = 0x7c05
go:	mov	ax, cs		;let data segment and stack segment the same
	mov	ds, ax		;as code segment
	mov	ss, ax		;ds = ss = cs = 0x07c0
	mov	sp, #0x400	;sp = 1024 => ss:esp = 0x07c0:0400

;load kernel to memory 0x10000
load_system:			;using BIOS int 0x13 ah=0x02 to read sectors
				;from drive
	mov	dx, #0x0000	;dh: head, dl = 0x00 (1st floppy disk)
	mov	cx, #0x0002	;ch: cylinder, cl: sector number(start from 1)
	mov	ax, #SYSSEG	;ax = 0x1000
	mov	es, ax		;es:bx => buffer address to load to
				;es:bx = 0x1000:0 => real address = 0x10000
	xor	bx, bx		;clean bx (bx=0x0000)
	mov	ax, #0x200+SYSLEN ;ah = 0x02 means read sectors from drive
       				;al = sectors read count
	int	0x13		;results after int 0x13:
				;cf: set on error, clear if no error
				;ah: return code, al: actual sector read count
	jnc	ok_load		;jump if no error
die:	jmp	die

;move kernel to memory 0x0, totally move 8KB at max
ok_load:
	cli			;disable interrupt
	mov	ax, #SYSSEG	;ax = 0x1000
	mov	ds, ax		;ds = 0x1000
	xor	ax, ax		;ax = 0
	mov	es, ax		;es = 0
	mov	cx, #0x1000	;totally move 0x1000(4K) count,
				;2 byte/count = 8KB
	sub	si, si		;si = 0
	sub	di, di		;di = 0
	rep	
		movw		;reap copy word(2 byte) from ds:si to es:di
				;i.e. from 0x1000:0000 to 0x0000:0000
				;then from 0x1000:0002 to 0x0000:0002 and so on
				;until the count register(cx) or zero flag(zf)
				;matches a tested condition
;load idt and gdt
	mov	ax, #BOOTSEG	;ax = 0x07c0
	mov	ds, ax		;locate data segment (ds=0x07c0)
	lidt	idt_48		;lidt ds:<offset of idt_48>
	lgdt	gdt_48		;lgdt ds:<offset of gdt_48>
;jump into x86 protect mode
	mov	ax, #0x0001	;set pe to 1(bit 0) to enable x86 protect mode
	lmsw	ax		;load machine status word (cr0)
	jmpi	0, 8		;8 is the offset of gdt selector, one gdt selector
				;is 8 byte long

;glocal gdt content
gdt:	.word	0, 0, 0, 0	;first gdt entry is not used
	;code segment
	.word	0x07ff		;segment limit (= 8MB)	
	.word	0x0000		;segment base addr
	.word	0x9a00		;code segment, read/execute
	.word	0x00c0		;g=1 => 4KB, b=1 => 32 bit segment
	;data segment
	.word	0x07ff		;segment limit (= 8MB)	
	.word	0x0000		;segment base addr
	.word	0x9200		;data segment, read/write
	.word	0x00c0		;g=1 => 4KB, b=1 => 32 bit segment

idt_48:	.word	0		;length of idt
	.word	0, 0		;address of idt
gdt_48:	.word	0x07ff		;gdt length: 2048
	.word	0x7c00+gdt, 0	;address of gdt
.org 510
	.word	0xaa55		;boot sector must end with 0xaa55
