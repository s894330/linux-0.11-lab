#define move_to_user_mode() \
__asm__("movl %%esp, %%eax\n\t" \
	"pushl $0x17\n\t"	    /* push task0 ss */			    \
	"pushl %%eax\n\t"	    /* push esp */			    \
	"pushfl\n\t"		    /* push eflags */			    \
	"pushl $0x0f\n\t"	    /* push task0 cs */			    \
	"pushl $1f\n\t"		    /* push eip */			    \
	"iret\n\t"		    /* from level 0 ret to level 3 */	    \
	"1:movl $0x17, %%eax\n\t"   /* first code executed in level 3 */    \
	"movw %%ax, %%ds\n\t" \
	"movw %%ax, %%es\n\t" \
	"movw %%ax, %%fs\n\t" \
	"movw %%ax, %%gs" \
	:::"ax")

#define sti() __asm__("sti":)
#define cli() __asm__("cli":)
#define nop() __asm__("nop":)
#define hang() __asm__("loop:	jmp loop":)

#define iret() __asm__("iret":)

/*
 * setup IDT gate entry
 *
 * format:
 * 31 ........... 16 15 14 13 12 11 ... 8 7 6 5 4 ... 0
 * ISR addr of 31~16 P   DPL  0    type   0 0 0
 *   seg. selector   ISR addr of 15~0
 */
#define _set_gate(gate_addr, type, DPL, ISR_addr) \
	__asm__("movw %%dx, %%ax\n\t" \
		"movw %0, %%dx\n\t" \
		"movl %%eax, %1\n\t" /* eax = <selector> + <0~15 ISR addr> */ \
		"movl %%edx, %2" /* edx = <16~31 ISR addr> + P + DPL + type */ \
		/* "i": immediate integer operand  */ \
		:: "i" ((short)(0x8000 + (DPL << 13) + (type << 8))), \
		/* "o": memory operand, but only if the address is offsettable */ \
		"o" (*((char *)(gate_addr))), \
		"o" (*((char *)(gate_addr) + 4)), \
		"d" ((char *)(ISR_addr)), "a" (0x00080000))

/* idt table is defined in head.s, 14: interrupt gate, 15: trap gate */
#define set_intr_gate(n, ISR_addr) _set_gate(&idt[n], 14, 0, ISR_addr)
#define set_trap_gate(n, ISR_addr) _set_gate(&idt[n], 15, 0, ISR_addr)
#define set_system_gate(n, ISR_addr) _set_gate(&idt[n], 15, 3, ISR_addr)

#define _set_seg_desc(gate_addr,type,dpl,base,limit) {\
	*(gate_addr) = ((base) & 0xff000000) | \
		(((base) & 0x00ff0000)>>16) | \
		((limit) & 0xf0000) | \
		((dpl)<<13) | \
		(0x00408000) | \
		((type)<<8); \
	*((gate_addr)+1) = (((base) & 0x0000ffff)<<16) | \
		((limit) & 0x0ffff); \
}

/* set TSS/LDT seg. to len 104 byte, and store corresponding address to GDT*/
#define _set_tssldt_desc(n, addr, type) \
	__asm__("movw $104, %1\n\t" /* seg. limit: 104 byte, because G is 0 */ \
		"movw %%ax, %2\n\t" /* <0~15> base addr */ \
		"rorl $16, %%eax\n\t" /* ror: rotate right */\
		"movb %%al, %3\n\t" /* <16~23> base addr */ \
		"movb $" type ", %4\n\t" \
		"movb $0x00, %5\n\t" /* set <16~19> len limit to 0 */ \
		"movb %%ah, %6\n\t" /* <24~31> base addr */ \
		"rorl $16, %%eax" \
		::"a" (addr), "m" (*(n)), "m" (*(n + 2)), "m" (*(n + 4)), \
		"m" (*(n + 5)), "m" (*(n + 6)), "m" (*(n + 7)))

/* 
 * 0x89: 32bit TSS system segment, DPL 0, present
 * 0x82: LDT system segment, DPL 0, present
 */
//TODO why cast addr to (int), by self test, cast to (long) seens work, too
//     even no cast also work, too
#define set_tss_desc(n, addr) _set_tssldt_desc(((char *)(n)), ((int)(addr)), "0x89")
#define set_ldt_desc(n, addr) _set_tssldt_desc(((char *)(n)), ((int)(addr)), "0x82")

