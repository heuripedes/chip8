
OBJECTS=chip8.o main.o sljit/sljitLir.o
CFLAGS=-Wall -std=gnu99 -g3 -O0 -DSLJIT_CONFIG_AUTO=1

.PHONY: all

all: $(OBJECTS)
	$(CC) -o chip8 $(OBJECTS) -lcursesw

chip8.o: chip8_naive.h chip8_ci.h chip8_dyn.h

$(OBJECTS): Makefile

clean:
	-rm -f $(OBJECTS) chip8

test: all test.chp
	chipper test

