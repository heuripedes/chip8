
all: test
	gcc -Wall -std=gnu99 -g3 -o chip8 chip8.c -lcurses

test: test.chp
	chipper test

