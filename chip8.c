#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chip8.h"

chip8_t *c8_new(void)
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

    chip8_t *c8 = (chip8_t*)calloc(1, sizeof(chip8_t));
    c8->ram   = (uint8_t*)calloc(1,  CHIP8_RAM_SIZE);
    c8->vram  = (uint8_t*)calloc(1,  CHIP8_VIDEO_ROWS * CHIP8_VIDEO_COLS);
    c8->stack = (uint16_t*)calloc(1, CHIP8_STACK_SIZE * sizeof(uint16_t));
    c8->pc    = 512;

    memcpy(c8->vram, font, sizeof(font));

    return c8;
}

void c8_free(chip8_t *c8)
{
    free(c8->ram);
    free(c8->vram);
    free(c8->stack);

    if (c8->cache)
        free(c8->cache);

    memset(c8, 0, sizeof(*c8));
    free(c8);
}

void c8_load(chip8_t *c8, uint8_t *data, size_t size)
{
    c8->state = 0;
    c8->pc    = 0x200;

    memset(c8->vram, 0, CHIP8_VIDEO_ROWS * CHIP8_VIDEO_COLS);

    if (size > (CHIP8_STACK_SIZE-512))
        size = CHIP8_STACK_SIZE-512;

    memcpy(c8->ram+512, data, size);
}


void c8_set_poll(chip8_t *c8, chip8_poll_t poll, uintptr_t data)
{
    c8->poll      = poll;
    c8->poll_data = data;
}

static inline void c8_push(chip8_t *c8)
{
    if (c8->stack_ptr == CHIP8_STACK_SIZE)
    {
        c8->state    |= CHIP8_STATE_STACK;
        c8->stack_ptr = 0;
    }

    c8->stack[c8->stack_ptr++] = c8->pc;
}

static inline void c8_pop(chip8_t *c8)
{
    if (c8->stack_ptr == 0)
    {
        c8->state    |= CHIP8_STATE_STACK;
        c8->stack_ptr = CHIP8_STACK_SIZE;
    }

    c8->pc = c8->stack[--c8->stack_ptr];
}

static inline void c8_jump(chip8_t *c8, uint16_t addr)
{
    addr &= 0xfff;

    if (addr == c8->pc - 2)
        c8->state |= CHIP8_STATE_HALT;

    if (addr < 0x200)
        c8->state |= CHIP8_STATE_ILEGAL;

    c8->pc = addr;
}

static inline void c8_draw(chip8_t *c8, uint8_t x, uint8_t y, uint8_t nrows)
{
    x &= 0x3f;
    y &= 0x1f;

    for (unsigned row = 0; row < nrows; ++row)
    {
        uint8_t  src = c8->ram[c8->i+row];
        for (unsigned col = 0; col < 8; ++col)
        {
            if (src & 0x80)
            {
                if (x + col >= CHIP8_VIDEO_COLS || y + row >= CHIP8_VIDEO_ROWS)
                    continue;
                uint8_t *dst = &c8->vram[(y + row) * CHIP8_VIDEO_COLS + (x+col)];

                *dst ^= (src >> 7);
                c8->v[15] = !*dst;
            }

            src <<= 1;
        }
    }
}

static void c8_int_step(chip8_t *c8)
{
    const uint16_t instr = c8->ram[c8->pc] << 8 | c8->ram[c8->pc + 1];//((uint16_t)c8->ram[c8->pc] << 8) | (c8->ram[c8->pc+1]);
    c8->pc += 2;

    const uint8_t x    = (instr >> 8) & 0x0f;
    const uint8_t y    = (instr >> 4) & 0xf;
    const uint8_t kk   = instr & 0x00ff;
    const uint16_t nnn = instr & 0x0fff;

    uint8_t *vx = &c8->v[x];
    uint8_t *vy = &c8->v[y];

    switch (instr >> 12)
    {
    case 0x0: // these call host functions
        switch (nnn)
        {
        case 0x00e0: // cls
            memset(c8->vram, 0, CHIP8_VIDEO_ROWS * CHIP8_VIDEO_COLS);
            break;
        case 0x00ee: // ret
            c8_pop(c8);
            break;
        default: // sys addr
            c8_jump(c8, nnn);
            break;
        }
        break;
    case 0x1: // jp nnn
        c8_jump(c8, nnn);
        break;
    case 0x2: // call nnn
        c8_push(c8);
        c8_jump(c8, nnn);
        break;
    case 0x3: // se vx, kk
        if (*vx == kk)
            c8->pc += 2;
        break;
    case 0x4: // sne vx, kk
        if (*vx != kk)
            c8->pc += 2;
        break;
    case 0x5: // se vx, vy
        if (*vx == *vy)
            c8->pc += 2;
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
            c8->v[15] = ((uint32_t)*vx + (uint32_t)*vy) > 0xff;
            *vx     = *vx + *vy;
            break;
        case 0x5: // sub
            c8->v[15] = *vx >= *vy;
            *vx     = *vx - *vy;
            break;
        case 0x6: // shr
            // XXX: doc says vx = vy >> 1, vf = vy &1
            c8->v[15] = *vx & 1;
            *vx     = *vx >> 1;
            break;
        case 0x7: // subn
            c8->v[15] = *vy > *vx;
            *vx     = *vy - *vx;
            break;
        case 0xe: // shl
            // XXX: doc says vx = vy << 1, vf = vy >> 7
            c8->v[15] = *vx >> 7;
            *vx     = *vx << 1;
            break;
        default:
            c8->state |= CHIP8_STATE_ILEGAL;
        }
        break;
    }
    case 0x9: // sne vx, vy
        if (*vx != *vy)
            c8->pc += 2;
        break;
    case 0xa: // ld i, nnn
        c8->i = nnn;
        break;
    case 0xb: // jp v0, addr
        c8_jump(c8, nnn + c8->v[0]);
        break;
    case 0xc: // rnd vx, byte
        *vx = rand() & kk;
        break;
    case 0xd: // drw vx, vy, nibble
        c8_draw(c8, *vx, *vy, instr & 0xf);
        break;
    case 0xe: // op vx
        switch (instr & 0xff)
        {
        case 0x9e: // skp vx
            if (c8->kbd[*vx])
                c8->pc += 2;
            break;
        case 0xa1: // sknp vx
            if (!c8->kbd[*vx])
                c8->pc += 2;
            break;
        default:
            c8->state |= CHIP8_STATE_ILEGAL;
        }
        break;
    case 0xf: // op o1, o2
        switch (instr & 0xff)
        {
        case 0x07: // ld vx, dt
            *vx = c8->dt;
            break;
        case 0x0a: // ld vx, key
        {
            uint8_t result = 255;

            for (int i = 0; i < 16; ++i)
            {
                if (c8->kbd[i])
                {
                    result = i;
                    break;
                }
            }

            if (result == 255)
                c8->pc -= 2;
            else
                *vx = result;
            break;
        }
        case 0x15: // ld dt, vx
            c8->dt = *vx;
            break;
        case 0x18: // ld st, vx
            c8->st = *vx;
            break;
        case 0x1e: // add i, vx
            c8->v[15] = (c8->i + *vx) > 255; // XXX: undocumented
            c8->i  = c8->i + *vx;
            break;
        case 0x29: // ld f, vx
            c8->i = CHIP8_FONT_ADDR + *vx * 5;
            break;
        case 0x33: // ld b, vx
            c8->ram[(c8->i+0) & 0xfff] = *vx / 100;
            c8->ram[(c8->i+1) & 0xfff] = *vx / 10 % 10;
            c8->ram[(c8->i+2) & 0xfff] = *vx % 10;
            break;
        case 0x55: // ld [i], vx
            memcpy(&c8->ram[c8->i], c8->v, x+1);
            c8->i = c8->i+x+1;
            break;
        case 0x65: // ld vx, [i]
            memcpy(c8->v, &c8->ram[c8->i], x+1);
            c8->i = c8->i+x+1;
            break;
        default:
            c8->state |= CHIP8_STATE_ILEGAL;
        }
        break;

    default:
        c8->state |= CHIP8_STATE_ILEGAL;
    }

    c8->run_time++;
    c8->cycles--;

    if ((c8->run_time % (CHIP8_CLOCK/60)) == 0)
    {
        if (c8->dt)
            c8->dt--;

        if (c8->st)
            c8->st--;
    }

    c8->poll(c8->kbd, c8->poll_data);
}

void c8_run(chip8_t *c8, unsigned cycles)
{
    if (cycles == 0)
        cycles = CHIP8_CLOCK / 60;

    c8->cycles = cycles;

    while (c8->cycles)
        c8_int_step(c8);
}
