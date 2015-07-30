#include "chip8_private.h"
#include "chip8_naive.h"
#include "chip8_ir.h"

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

    memcpy(&c8->ram[CHIP8_FONT_ADDR], font, sizeof(font));

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
    c8ir_invalidate(c8, 0, 4096);
}


void c8_set_poll(chip8_t *c8, chip8_poll_t poll, uintptr_t data)
{
    c8->poll      = poll;
    c8->poll_data = data;
}


void c8_run(chip8_t *c8, unsigned cycles)
{
    if (cycles == 0)
        cycles = CHIP8_CLOCK / 60;

    c8->cycles = cycles;

    if (1)
        while (c8->cycles)
            c8_naive_step(c8);
    else
        while (c8->cycles)
            c8_ir_step(c8);
}
