#ifndef CHIP8_PRIVATE_H
#define CHIP8_PRIVATE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CHIP8_H
#include "chip8.h"
#endif

typedef struct {
    uint8_t op:4;
    union {
        struct {
            uint8_t x:4;
            union {
                struct {
                    uint8_t y:4;
                    uint8_t n:4;
                };
                uint8_t kk;
            };
        };
        uint16_t nnn:12;
    };
} chip8_instr_t;

enum {
    CHIP8_V0,
    CHIP8_V1,
    CHIP8_V2,
    CHIP8_V3,
    CHIP8_V4,
    CHIP8_V5,
    CHIP8_V6,
    CHIP8_V7,
    CHIP8_V8,
    CHIP8_V9,
    CHIP8_VA,
    CHIP8_VB,
    CHIP8_VC,
    CHIP8_VD,
    CHIP8_VE,
    CHIP8_VF,

    CHIP8_LAST_V_REG = CHIP8_VF,
    CHIP8_NUM_V_REGS,

    CHIP8_I,
    CHIP8_PC,
    CHIP8_ST,
    CHIP8_DT,

    CHIP8_LAST_REG = CHIP8_DT
};

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

                *dst ^= 1;
                c8->v[15] = !*dst;
            }

            src <<= 1;
        }
    }
}

static inline void c8_clear(chip8_t *c8)
{
    memset(c8->vram, 0, CHIP8_VIDEO_ROWS*CHIP8_VIDEO_COLS);
}

static inline uint8_t c8_wait_key(chip8_t *c8)
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

    return result;
}


#endif // CHIP8_PRIVATE_H

