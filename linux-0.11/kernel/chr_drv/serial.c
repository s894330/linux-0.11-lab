/*
 *  linux/kernel/serial.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	serial.c
 *
 * This module implements the rs232 io functions
 *	void rs_write(struct tty_struct * queue);
 *	void serial_init(void);
 * and all interrupts pertaining to serial IO.
 */

#include <linux/tty.h>
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>

#define WAKEUP_CHARS (TTY_BUF_SIZE/4)

extern void serial1_interrupt(void);
extern void serial2_interrupt(void);

/* set serial port property to 2400bps, 8N1 */
static void init_serial_port(int port)
{
	outb_p(0x80, port + 3);	/* set DLAB of line control reg */
	outb_p(0x30, port);	/* LS of divisor (48 -> 2400 bps) */
	outb_p(0x00, port + 1);	/* MS of divisor */
	outb_p(0x03, port + 3);	/* 8 bits, no parity, one stop bit */
	outb_p(0x0b, port + 4);	/* set DTR, RTS, OUT_2 */
	outb_p(0x0d, port + 1);	/* enable all intrs except writes */
	inb(port);	/* read data port to reset things (?) */
}

void serial_init(void)
{
	/* register tty1/tty2(IRQ4/IRQ3) ISR */
	set_intr_gate(0x24, serial1_interrupt);
	set_intr_gate(0x23, serial2_interrupt);

	/* init tty1/tty2 serial port */
	init_serial_port(tty_table[1].read_q.data);
	init_serial_port(tty_table[2].read_q.data);

	/* enable tty1(IRQ4)/tty2(IRQ3) interrupt */
	outb(inb_p(0x21) & 0xe7, 0x21);
}

/*
 * This routine gets called when tty_write has put something into
 * the write_queue. It must check wheter the queue is empty, and
 * set the interrupt register accordingly
 *
 *	void _rs_write(struct tty_struct * tty);
 */
void rs_write(struct tty_struct * tty)
{
	cli();
	if (!EMPTY(tty->write_q))
		outb(inb_p(tty->write_q.data+1)|0x02,tty->write_q.data+1);
	sti();
}
