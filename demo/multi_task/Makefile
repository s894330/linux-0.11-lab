all:
	as86 -0 -a -o boot.o boot.s
	ld86 -0 -s -o boot boot.o
	as --32 -o head.o head.s
	ld -m elf_i386 -Ttext 0 -e startup_32 -s -x -M head.o -o system > System.map
	dd bs=32 if=boot of=boot.img skip=1
	objcopy -O binary system head
	cat head >> boot.img
	bochs -f ../as86_bootsector/bochs.conf

clean:
	rm -rf boot.o boot boot.img head.o head system System.map
