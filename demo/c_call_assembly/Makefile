all:
	as -o callee.o callee.s
	gcc -o caller caller.c callee.o
	./caller
clean:
	rm -rf caller.o callee.o caller
