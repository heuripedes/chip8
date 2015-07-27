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
static uint8_t vram[VIDEO_ROWS * (VIDEO_COLS)];
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
        0xF0, 0x10, 0xF0, 0x10, 0xF0,
        0x90, 0x90, 0xF0, 0x10, 0x10,
        0xF0, 0x80, 0xF0, 0x10, 0xF0,
        0xF0, 0x80, 0xF0, 0x90, 0xF0,
        0xF0, 0x10, 0x20, 0x40, 0x40,
        0xF0, 0x90, 0xF0, 0x90, 0xF0,
        0xF0, 0x90, 0xF0, 0x10, 0xF0,
        0xF0, 0x90, 0xF0, 0x90, 0x90,
        0xE0, 0x90, 0xE0, 0x90, 0xE0,
        0xF0, 0x80, 0x80, 0x80, 0xF0,
        0xE0, 0x90, 0x90, 0x90, 0xE0,
        0xF0, 0x80, 0xF0, 0x80, 0xF0,
        0xF0, 0x80, 0xF0, 0x80, 0x80,
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
    refresh();
//    usleep(16000);
}

void v8_draw(uint8_t x, uint8_t y, uint8_t n)
{
    x &= 0x3f;
    y &= 0x1f;

    for (unsigned i = 0; i < n && y < VIDEO_ROWS; ++i)
    {
        uint8_t src  = ram[regs.i+i] >> 4;
        uint8_t *dst = &vram[y++ * VIDEO_COLS+x];
        for (unsigned j = 0; j < 4 && (x+j) < VIDEO_COLS; ++j)
        {
            uint8_t bit = (src >> (3-j)) & 1;

            if (!bit && dst[j])
                regs.vf = 1;

            dst[j] ^= bit;
        }
    }
}

uint8_t i8_getch(int test)
{
    uint8_t key;

    if (test < 0)
    {
        timeout(-1);
        key = getch();
//        printf("\a");
        timeout(0);
    }
    else
    {
        key = getch();
        if (key == ERR)
            key = 0xff; // is this ok?
    }
    if (key >= '0' && key <= '9')
        key -= '0';
    else if (key >= 'a' && key <= 'f')
        key = (key - 'a') + 10;
    else if (key >= 'A' && key <= 'F')
        key = (key - 'A') + 10;

    if (test >= 0 && key == test)
    {
//        printf("\a");
        key = 1;
    }

    return key;
}

#define NIBBLE(m, n) (((m) >> ((n) * 4)) & 0x000f)
#define N(m)      NIBBLE(m, 0)
#define NN(m)     ((m) & 0x00ff)
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
    uint16_t instr = ram_read16(regs.pc);
    last_pc = regs.pc;
    regs.pc += 2;

    move(33, 0);
    printw("instr: %04x pc: %04x\n", instr, regs.pc);

    switch (instr >> 12)
    {
    case 0x0: // these call host functions
        switch (NNN(instr))
        {
        case 0x00e0: // cls
            v8_clear();
            break;
        case 0x00ee: // ret
            regs.pc = stack[--regs.sp];
            break;
        default: // sys addr
        {
            uint16_t addr = NNN(instr);
            if (addr == last_pc)
                c8_halt();
            regs.pc = addr;
            break;
        }
        }
        break;
    case 0x1: // jp nnn
        regs.pc = NNN(instr);
        if (NNN(instr) == last_pc)
            c8_halt();
        break;
    case 0x2: // call nnn
        stack[regs.sp++] = regs.pc;
        regs.pc = NNN(instr);
        if (NNN(instr) == last_pc)
            c8_halt();
        break;
    case 0x3: // se vx, kk
        if (N2V(instr, 2) == NN(instr))
            regs.pc += 2;
        break;
    case 0x4: // sne vx, kk
        if (N2V(instr, 2) != NN(instr))
            regs.pc += 2;
        break;
    case 0x5: // se vx, vy
        if (N2V(instr, 2) == N2V(instr, 1))
            regs.pc += 2;
        break;
    case 0x6: // ld vx, kk
        N2V(instr, 2) = NN(instr);
        break;
    case 0x7: // add vx, kk
        N2V(instr, 2) += NN(instr);
        break;
    case 0x8: // op vx, vy
    {
        uint32_t uval;
        int32_t  ival;
        switch (instr & 0x000f)
        {
        case 0x0: // ld
             N2V(instr, 2) = N2V(instr, 1);
            break;
        case 0x1: // or
            N2V(instr, 2) = N2V(instr, 2) | N2V(instr, 1);
            break;
        case 0x2: // and
            N2V(instr, 2) = N2V(instr, 2) & N2V(instr, 1);
            break;
        case 0x3: // xor
            N2V(instr, 2) = N2V(instr, 2) ^ N2V(instr, 1);
            break;
        case 0x4: // add
            uval = (uint32_t)N2V(instr, 2) + (uint32_t)N2V(instr, 1);
            N2V(instr, 2) += N2V(instr, 1);
            regs.vf = !!(uval & 0xff00);
            break;
        case 0x5: // sub
            ival = (int32_t)N2V(instr, 2) - (int32_t)N2V(instr, 1);
            regs.vf = ival > 0;
            N2V(instr, 2) -= N2V(instr, 1);
            break;
        case 0x6: // shr
            regs.vf = N2V(instr, 1) & 1;
            N2V(instr, 2) = N2V(instr, 1) >> 1;
            break;
        case 0x7: // subn
            ival = (int32_t)N2V(instr, 1) - (int32_t)N2V(instr, 2);
            N2V(instr, 2) = N2V(instr, 1) - N2V(instr, 2);
            regs.vf = ival > 0;
            break;
        case 0xe: // shl
            regs.vf = N2V(instr, 1) >> 7;
            N2V(instr, 2) = N2V(instr, 1) << 1;
            break;
        default:
            invalid_instruction(instr);
        }
        break;
    }
    case 0x9: // sne vx, vy
        if (N2V(instr, 2) != N2V(instr, 1))
            regs.pc += 2;
        break;
    case 0xa: // ld i, nnn
        regs.i = NNN(instr);
        break;
    case 0xb: // jp v0, addr
    {
        uint16_t addr = NNN(instr) + regs.v0;

        if (addr == last_pc)
            c8_halt();
        regs.pc = addr;
        break;
    }
    case 0xc: // rnd vx, byte
        N2V(instr, 2) = rand() & NN(instr);
        break;
    case 0xd: // drw vx, vy, nibble
        v8_draw(N2V(instr, 2), N2V(instr, 1), N(instr));
        break;
    case 0xe: // op vx
        switch (instr & 0xff)
        {
        case 0x9e: // skp vx
            if (i8_getch(N2V(instr, 2)))
                regs.pc += 2;
            break;
        case 0xa1: // sknp vx
            if (!i8_getch(N2V(instr, 2)))
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
            N2V(instr, 2) = regs.dt;
            break;
        case 0x0a: // ld vx, key
            N2V(instr, 2) = i8_getch(-1);
            break;
        case 0x15: // ld dt, vx
            regs.dt = N2V(instr, 2);
            break;
        case 0x18: // ld st, vx
            regs.st = N2V(instr, 2);
            break;
        case 0x1e: // add i, vx
            regs.i = NNN(regs.i + N2V(instr, 2));
            break;
        case 0x29: // ld f, vx
            regs.i = FONT_ADDR + N2V(instr, 2) * 5;
            break;
        case 0x33: // ld b, vx
            ram[NNN(regs.i+0)] = N2V(instr, 2) / 100;
            ram[NNN(regs.i+1)] = N2V(instr, 2) / 10 % 10;
            ram[NNN(regs.i+2)] = N2V(instr, 2) % 10;
            break;
        case 0x55: // ld [i], vx
            for (int i = 0; i < NIBBLE(instr, 2)+1; ++i)
                ram[NNN(regs.i+i)] = regs.v[i];
            regs.i = NNN(regs.i+NIBBLE(instr, 2)+1);
            break;
        case 0x65: // ld vx, [i]
            for (int i = 0; i < NIBBLE(instr, 2)+1; ++i)
                regs.v[i] = ram[NNN(regs.i+i)];
            regs.i = NNN(regs.i+NIBBLE(instr, 2)+1);
            break;
        default:
            invalid_instruction(instr);
        }
        break;
    default:
        invalid_instruction(instr);
    }

    cycles++;

    if (cycles % 2933 == 0)
    {
        if (regs.dt)
            regs.dt--;

        if (regs.st)
            regs.st--;
    }

    printw("i:%04x sp:%04x dt:%04x st:%04x\n", regs.i, regs.sp, regs.dt, regs.st);
    for (unsigned i = 0; i < 16; ++i)
        printw("%01x:%02x ", i, regs.v[i]);
    printw("\n");

    v8_swap();

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

