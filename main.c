#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <curses.h>
#include <signal.h>
#include <unistd.h>
#include <locale.h>

#include "chip8.h"

static chip8_t *c8 = NULL;

#if 0
void kbd_poll(uint8_t kbd[16], uintptr_t data)
{
    memset(kbd, 0, 16);
    int rk = getch();

    if (rk != ERR)
    {
        if (rk >= '0' && rk <= '9')
            rk -= '0';
        else if (rk >= 'a' && rk <= 'f')
            rk = (rk - 'a') + 10;
        else if (rk >= 'A' && rk <= 'F')
            rk = (rk - 'A') + 10;

        kbd[rk] = 1;
    }
}

void quit()
{
    if (c8)
    {
        c8_free(c8);
        endwin();
        c8 = NULL;
    }
}

#define swap16 __builtin_bswap16

int main(int argc, char *argv[])
{
    uint8_t data[CHIP8_RAM_SIZE];
    size_t size;
    c8 = c8_new();

    size = fread(data, 1, sizeof(data), fopen(argv[1], "rb"));

    c8_load(c8, data, size);
    c8_set_poll(c8, kbd_poll, 0);

    atexit(quit);
    signal(SIGINT, (__sighandler_t)quit);

    setlocale(LC_ALL, "");

    initscr();
    cbreak();
    keypad(NULL, true);
    timeout(0);

    while (1)
    {
        c8_run(c8, 0);

        move(0, 0);
        for (int row = 0; row < CHIP8_VIDEO_ROWS; ++row)
        {
            for (int col = 0; col < CHIP8_VIDEO_COLS; ++col)
            {
                if (c8->vram[row * CHIP8_VIDEO_COLS + col])
                    printw("#");
                else
                    printw(" ");
            }
            printw("\n");
        }

        mvprintw(0, 65, "pc:%04x = %04x", c8->pc, c8->ram[c8->pc-2]);
        mvprintw(1, 65, "sp:%04x = %04x", c8->stack_ptr, swap16(*(uint16_t*)&c8->ram[c8->stack_ptr]));
        mvprintw(2, 65, "i :%04x = %02x", c8->i, c8->ram[c8->i]);
        mvprintw(3, 65, "dt: %02x st: %02x", c8->dt, c8->st);

        for (int i = 0, y=4; i < 8; i++, y++)
            mvprintw(y, 65, "v%1x: %02x v%1x: %02x", i, c8->v[i], i+8, c8->v[i+8]);
        refresh();

        mvprintw(16, 65, "%08x", c8->state);

        usleep(16666);
    }

    return 0;
}
#else

void kbd_poll(uint8_t kbd[16], uintptr_t data)
{
    memset(kbd, 0, 16);
    int rk = getch();

    if (rk != ERR)
    {
        if (rk >= '0' && rk <= '9')
            rk -= '0';
        else if (rk >= 'a' && rk <= 'f')
            rk = (rk - 'a') + 10;
        else if (rk >= 'A' && rk <= 'F')
            rk = (rk - 'A') + 10;

        kbd[rk] = 1;
    }
}

void quit()
{
    if (c8)
    {
        c8_free(c8);
        endwin();
        c8 = NULL;
    }
}

#define swap16 __builtin_bswap16

int main(int argc, char *argv[])
{
    uint8_t data[CHIP8_RAM_SIZE];
    size_t size;
    c8 = c8_new();

    size = fread(data, 1, sizeof(data), fopen(argv[1], "rb"));

    c8_load(c8, data, size);
    c8_set_poll(c8, kbd_poll, 0);

    while (!(c8->state & CHIP8_STATE_HALT))
    {
        c8_run(c8, 100);

        printf("pc:%04x = %04x\n", c8->pc, c8->ram[c8->pc-2]);
        printf("sp:%04x = %04x\n", c8->stack_ptr, swap16(*(uint16_t*)&c8->ram[c8->stack_ptr]));
        printf("i :%04x = %02x\n", c8->i, c8->ram[c8->i]);
        printf("dt: %02x st: %02x\n", c8->dt, c8->st);

        for (int i = 0, y=4; i < 8; i++, y++)
            printf("v%1x: %02x v%1x: %02x\n", i, c8->v[i], i+8, c8->v[i+8]);

        usleep(16666);
        break;
    }

    return 0;
}
#endif


