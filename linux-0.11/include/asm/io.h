#define outb(value, port) \
__asm__("outb %%al, %%dx" \
	:: "a" (value), "d" (port))

#define inb(port) ({ \
unsigned char _v; \
__asm__ volatile( \
	"inb %%dx, %%al" \
	: "=a" (_v): "d" (port)); \
_v; \
})

#define outb_p(value, port) \
__asm__("outb %%al, %%dx\n\t" \
	"jmp 1f\n\t" \
	"1:jmp 1f\n\t" \
	"1:" \
	:: "a" (value), "d" (port))

#define inb_p(port) ({ \
unsigned char _v; \
__asm__ volatile( \
	"inb %%dx, %%al\n\t" \
	"jmp 1f\n\t" \
	"1:jmp 1f\n\t" \
	"1:" \
	: "=a" (_v): "d" (port)); \
_v; \
})

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */
#define CMOS_READ(addr) ({ \
	outb_p(addr, 0x70); \
	inb_p(0x71); \
})
