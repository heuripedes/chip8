
all:
	gcc -Wall -std=gnu99 -g3 -o chip8 chip8.c main.c -lcursesw

test: all test.chp
	chipper test

