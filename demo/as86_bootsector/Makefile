# as86
#   -0: start with 16-bit code segment, warn for all instructions > 8086
#   -a: enable partial compatibility with Minix asld
# ld86
#   -s: strip symbols

all:
	as86 -0 -a -o boot.o boot.s
	ld86 -0 -s -o boot boot.o
	dd bs=32 if=boot of=boot.img skip=1
	bochs -f bochs.conf

clean:
	rm -rf boot.o boot boot.img
