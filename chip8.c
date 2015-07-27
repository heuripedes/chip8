#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <curses.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#define RAM_SIZE 0x1000
#define STACK_SIZE 1024
#define VIDEO_ROWS 32
#define VIDEO_COLS 64
#define FONT_ADDR  (0x200-(5*16))

static uint8_t ram[RAM_SIZE];
static uint8_t vram[VIDEO_ROWS * VIDEO_COLS];
static uint16_t stack[STACK_SIZE];
static struct {
    union {
        struct {
            uint8_t v0, v1, v2, v3;
            uint8_t v4, v5, v6, v7;
            uint8_t v8, v9, va, vb;
            uint8_t vc, vd, ve, vf;
        };

        uint8_t v[16]; /* general purpose */
    };

    uint16_t i;
    uint16_t pc;
    uint8_t sp;
    uint8_t dt, st;
} regs;

#define swap16 __builtin_bswap16

void v8_quit(void)
{
    endwin();
}

uint16_t ram_read16(uint16_t addr)
{
    if (addr >= RAM_SIZE || addr < 0x200)
    {
        v8_quit();
        fprintf(stderr, "invalid read of %u\n", addr);
        abort();
    }
    return swap16(*(uint16_t*)&ram[addr]);
}

void c8_fault(void)
{
    v8_quit();
    fprintf(stderr, "chip-8 fault:\n\n");
}

uint16_t invalid_instruction(uint16_t instr)
{
    c8_fault();
    fprintf(stderr, "invalid instruction: %04x. pc=%u\n", instr, regs.pc-2);
    abort();
}


void on_sigint()
{
    v8_quit();
    exit(0);
}

void v8_init(void)
{
    static uint8_t font[] = {
        0xf0, 0x90, 0x90, 0x90, 0xf0,
        0x20, 0x60, 0x20, 0x20, 0x70,
        0xf0, 0x10, 0xf0, 0x80, 0xf0,
        0xf0, 0x10, 0xf0, 0x10, 0xf0,
        0x90, 0x90, 0xf0, 0x10, 0x10,
        0xf0, 0x80, 0xf0, 0x10, 0xf0,
        0xf0, 0x80, 0xf0, 0x90, 0xf0,
        0xf0, 0x10, 0x20, 0x40, 0x40,
        0xf0, 0x90, 0xf0, 0x90, 0xf0,
        0xf0, 0x90, 0xf0, 0x10, 0xf0,
        0xf0, 0x90, 0xf0, 0x90, 0x90,
        0xe0, 0x90, 0xe0, 0x90, 0xe0,
        0xf0, 0x80, 0x80, 0x80, 0xf0,
        0xe0, 0x90, 0x90, 0x90, 0xe0,
        0xf0, 0x80, 0xf0, 0x80, 0xf0,
        0xf0, 0x80, 0xf0, 0x80, 0x80,
    };

    memcpy(&ram[FONT_ADDR], font, sizeof(font));

    signal(SIGINT, (__sighandler_t)on_sigint);
    initscr();
    cbreak();
    keypad(NULL, true);
    timeout(0);
}

void v8_clear(void)
{
    memset(vram, 0, sizeof(vram));
}

void v8_swap(void)
{
    move(0, 0);
    for (int row = 0; row < VIDEO_ROWS; ++row)
    {
        for (int col = 0; col < VIDEO_COLS; ++col)
        {
            if (vram[row * VIDEO_COLS + col])
                printw("#");
            else
                printw(" ");
        }
        printw("\n");
    }

    mvprintw(0, 65, "pc:%04x = %04x", regs.pc, ram[regs.pc-2]);
    mvprintw(1, 65, "sp:%04x = %04x", regs.sp, swap16(*(uint16_t*)&ram[regs.sp]));
    mvprintw(2, 65, "i :%04x = %02x", regs.i, ram[regs.i]);
    mvprintw(3, 65, "dt: %02x st: %02x", regs.dt, regs.st);

    for (int i = 0, y=4; i < 8; i++, y++)
        mvprintw(y, 65, "v%1x: %02x v%1x: %02x", i, regs.v[i], i+8, regs.v[i+8]);
    refresh();
}

void v8_draw(uint8_t x, uint8_t y, uint8_t nrows)
{
    x &= 0x3f;
    y &= 0x1f;

    for (unsigned row = 0; row < nrows; ++row)
    {
        uint8_t  src = ram[regs.i+row];
        for (unsigned col = 0; col < 8; ++col)
        {
            if (src & 0x80)
            {
                if (x + col >= VIDEO_COLS || y + row >= VIDEO_ROWS)
                    continue;
                uint8_t *dst = &vram[(y + row) * VIDEO_COLS + (x+col)];

                *dst ^= (src >> 7);
                regs.vf = !*dst;
            }

            src <<= 1;
        }
    }
}

static uint8_t kbd[16];
void i8_poll(void)
{
    memset(kbd, 0, sizeof(kbd));
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

int i8_getch(int test)
{

    if (test < 0)
    {
        for (int i = 0; i < 16; ++i)
        {
            if (kbd[i])
                return i;
        }

        return -1;
    }
    else
        return kbd[test & 0xf];
}

#define NIBBLE(m, n) (((m) >> ((n) * 4)) & 0x000f)
#define N(m)      NIBBLE(m, 0)
#define KK(m)     ((m) & 0x00ff)
#define NNN(m)    ((m) & 0x0fff)
#define N2V(m, n) regs.v[NIBBLE(m, n)]

void c8_halt(void)
{
    return;
    v8_quit();
    fprintf(stderr, "HALTED\n");

    fprintf(stderr, "i:%04x sp:%04x dt:%04x st:%04x\n", regs.i, regs.sp, regs.dt, regs.st);
    for (unsigned i = 0; i < 16; ++i)
        fprintf(stderr, "%01x:%02x ", i, regs.v[i]);
    fprintf(stderr, "\n");

    exit(0);
}

void c8_step()
{
    static uint16_t last_pc = 0;
    static unsigned cycles  = 0;
    const uint16_t instr = ram_read16(regs.pc);
    last_pc = regs.pc;
    regs.pc += 2;

    const uint8_t x    = (instr >> 8) & 0x0f;
    const uint8_t y    = (instr >> 4) & 0xf;
    const uint8_t kk   = instr & 0x00ff;
    const uint16_t nnn = instr & 0x0fff;

    uint8_t *vx = &regs.v[x];
    uint8_t *vy = &regs.v[y];

    if (last_pc == 610)
    {
        last_pc = last_pc + 0;
    }

    switch (instr >> 12)
    {
    case 0x0: // these call host functions
        switch (nnn)
        {
        case 0x00e0: // cls
            v8_clear();
            break;
        case 0x00ee: // ret
            regs.pc = stack[--regs.sp];
            break;
        default: // sys addr
        {
            uint16_t addr = nnn;
            if (addr == last_pc)
                c8_halt();
            regs.pc = addr;
            break;
        }
        }
        break;
    case 0x1: // jp nnn
        regs.pc = nnn;
        if (nnn == last_pc)
            c8_halt();
        break;
    case 0x2: // call nnn
        stack[regs.sp++] = regs.pc;
        regs.pc = nnn;
        if (nnn == last_pc)
            c8_halt();
        break;
    case 0x3: // se vx, kk
        if (*vx == kk)
            regs.pc += 2;
        break;
    case 0x4: // sne vx, kk
        if (*vx != kk)
            regs.pc += 2;
        break;
    case 0x5: // se vx, vy
        if (*vx == *vy)
            regs.pc += 2;
        break;
    case 0x6: // ld vx, kk
        *vx = kk;
        break;
    case 0x7: // add vx, kk
        *vx = *vx + kk;
        break;
    case 0x8: // op vx, vy
    {
        switch (instr & 0x000f)
        {
        case 0x0: // ld
            *vx = *vy;
            break;
        case 0x1: // or
            *vx = *vx | *vy;
            break;
        case 0x2: // and
            *vx = *vx & *vy;
            break;
        case 0x3: // xor
            *vx = *vx ^ *vy;
            break;
        case 0x4: // add
            regs.vf = ((uint32_t)*vx + (uint32_t)*vy) > 0xff;
            *vx     = *vx + *vy;
            break;
        case 0x5: // sub
            regs.vf = *vx >= *vy;
            *vx     = *vx - *vy;
            break;
        case 0x6: // shr
            // XXX: doc says vx = vy >> 1, vf = vy &1
            regs.vf = *vx & 1;
            *vx     = *vx >> 1;
            break;
        case 0x7: // subn
            regs.vf = *vy > *vx;
            *vx     = *vy - *vx;
            break;
        case 0xe: // shl
            // XXX: doc says vx = vy << 1, vf = vy >> 7
            regs.vf = *vx >> 7;
            *vx     = *vx << 1;
            break;
        default:
            invalid_instruction(instr);
        }
        break;
    }
    case 0x9: // sne vx, vy
        if (*vx != *vy)
            regs.pc += 2;
        break;
    case 0xa: // ld i, nnn
        regs.i = nnn;
        break;
    case 0xb: // jp v0, addr
    {
        uint16_t addr = nnn + regs.v0;

        if (addr == last_pc)
            c8_halt();
        regs.pc = addr;
        break;
    }
    case 0xc: // rnd vx, byte
        *vx = rand() & kk;
        break;
    case 0xd: // drw vx, vy, nibble
        v8_draw(*vx, *vy, N(instr));
        break;
    case 0xe: // op vx
        switch (instr & 0xff)
        {
        case 0x9e: // skp vx
            if (i8_getch(*vx))
                regs.pc += 2;
            break;
        case 0xa1: // sknp vx
            if (!i8_getch(*vx))
                regs.pc += 2;
            break;
        default:
            invalid_instruction(instr);
        }
        break;
    case 0xf: // op o1, o2
        switch (instr & 0xff)
        {
        case 0x07: // ld vx, dt
            *vx = regs.dt;
            break;
        case 0x0a: // ld vx, key
        {
            int result = i8_getch(-1);
            if (result < 0)
                regs.pc -= 2;
            else
                *vx = result;
            break;
        }
        case 0x15: // ld dt, vx
            regs.dt = *vx;
            break;
        case 0x18: // ld st, vx
            regs.st = *vx;
            break;
        case 0x1e: // add i, vx
            regs.vf = (regs.i + *vx) > 255; // XXX: undocumented
            regs.i  = regs.i + *vx;
            break;
        case 0x29: // ld f, vx
            regs.i = FONT_ADDR + *vx * 5;
            break;
        case 0x33: // ld b, vx
            ram[NNN(regs.i+0)] = *vx / 100;
            ram[NNN(regs.i+1)] = *vx / 10 % 10;
            ram[NNN(regs.i+2)] = *vx % 10;
            break;
        case 0x55: // ld [i], vx
            memcpy(ram+regs.i, regs.v, x+1);
            regs.i = regs.i+x+1;
            break;
        case 0x65: // ld vx, [i]
            memcpy(regs.v, ram+regs.i, x+1);
            regs.i = regs.i+x+1;
            break;
        default:
            invalid_instruction(instr);
        }
        break;
    default:
        invalid_instruction(instr);
    }

    cycles++;

    if (cycles % 29333 == 0)
    {
        if (regs.dt)
            regs.dt--;

        if (regs.st)
            regs.st--;
    }

    v8_swap();
    i8_poll();
}

int main(int argc, char *argv[])
{
    fread(&ram[512], 1, RAM_SIZE-512, fopen(argv[1], "rb"));
    regs.pc = 512;

    v8_init();

    clear();
    refresh();

    struct timespec ts = {0, 1E9/1.76E6};

    while (1)
    {
        c8_step();
        nanosleep(&ts, NULL);
    }

    v8_quit();
    return 0;
}

