
all: test
	gcc -Wall -g3 -o chip8 chip8.c -lcurses

test: test.chp
	chipper test

