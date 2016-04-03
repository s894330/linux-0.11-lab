; this file demo the boot sector

; set various variable as  global variable for ld86 link use
.global begtext, begdata, begbss, endtext, enddata, endbss

BOOTSEG = 0x07c0

.text					; switch to .text section
begtext:				; define a variable
.data					; switch to .data section
begdata:
.bss
begbss:

.text
entry start				; tell ld the entry point
start:
	jmpi	go, BOOTSEG		; before this line execute,
					; cs:ip = 0x0000:0x7c00, i.e. the cs
					; value is not set, jmpi can let cs be
					; set. After this line executed,
					; cs:ip = 0x07c0:0x0005
go:	mov	ax, cs			; ax = 0x07c0
	mov	ds, ax			; ds = es = cs = 0x07c0
	mov	es, ax

	mov	cx, #20			; string length, include CR/LF
	mov	dx, #0x10ae		; locations to show the string
	mov	bx, #0x000c		; color of string
	mov	bp, #msg1		; address of string
	mov	ax, #0x1301		; func 0x13, sub func 0x01 of int0x10
	int	0x10
loop1:	jmp	loop1

msg1:	.ascii	"Loading System ..."
	.byte	13, 10			; ascii of CR/LF
.org 510				; move to address 510
	.word	0xaa55
.text
endtext:
.data
enddata:
.bss
endbss:
