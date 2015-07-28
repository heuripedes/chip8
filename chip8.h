#ifndef CHIP8_H
#define CHIP8_H

#include <stdint.h>
#include <stdbool.h>

#define CHIP8_RAM_SIZE   4096
#define CHIP8_STACK_SIZE 12
#define CHIP8_VIDEO_ROWS 32
#define CHIP8_VIDEO_COLS 64
#define CHIP8_CLOCK      1760000
#define CHIP8_FONT_ADDR  (0x200-(5*16))

enum {
    CHIP8_STATE_STACK   = 1, /* stack overflow or underflow */
    CHIP8_STATE_SEGMENT = 2, /* segmentation fault */
    CHIP8_STATE_ILEGAL  = 4, /* illegal instruction */
    CHIP8_STATE_HALT    = 8
};

typedef void (*chip8_poll_t)(uint8_t kbd[16], uintptr_t data);

typedef struct
{
    uint16_t pc;

    /* registers */
    uint16_t i;
    uint8_t v[16];
    uint8_t dt, st;

    uint8_t  *ram;
    uint8_t  *vram;

    uint8_t   stack_ptr;
    uint16_t *stack;

    void     *cache;

    uint8_t kbd[16];

    chip8_poll_t poll;
    uintptr_t    poll_data;

    unsigned state;
    unsigned cycles;
    unsigned run_time;

} chip8_t;

chip8_t *c8_new(void);
void     c8_free(chip8_t *c8);
void     c8_load(chip8_t *c8, uint8_t *data, size_t size);
void     c8_set_poll(chip8_t *c8, chip8_poll_t poll, uintptr_t data);
void     c8_run(chip8_t *c8, unsigned cycles);

#endif // CHIP8_H

