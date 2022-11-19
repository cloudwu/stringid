CFLAGS=-Wall -g

all : test.exe

test.exe : stringid.c
	gcc -o $@ $^ $(CFLAGS)

clean :
	rm -f test.exe