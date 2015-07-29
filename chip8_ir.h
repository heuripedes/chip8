#include "chip8_private.h"

#define C8IR_OP_ARGS uintptr_t d, uintptr_t a, uintptr_t b
typedef void (*c8ir_op_t)(chip8_t *c8, C8IR_OP_ARGS);
typedef struct {
    c8ir_op_t op;
    uintptr_t d;
    uintptr_t a, b;
} c8ir_tac_t;



static c8ir_tac_t c8ir_pool[4096];
static c8ir_tac_t *c8ir_cache[4096];
static const c8ir_tac_t *c8_ir_trans(chip8_t *c8);

#define c8ir_def(name, code) static void c8ir_##name(chip8_t *c8, C8IR_OP_ARGS) { c8->pc += 2; code; }

static void c8_ir_step(chip8_t *c8)
{
    while (c8->cycles)
    {
        const c8ir_tac_t *tac = c8ir_cache[c8->pc];
        if (!tac)
            tac = c8_ir_trans(c8);

        tac->op(c8, tac->d, tac->a, tac->b);

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
}

c8ir_def(illegal,
    c8->state |= CHIP8_STATE_ILEGAL;
)

c8ir_def(cls,
    memset(c8->vram, 0, CHIP8_VIDEO_ROWS * CHIP8_VIDEO_COLS);
)

c8ir_def(pop,
    c8_pop(c8);
)

c8ir_def(jp,
    c8->pc = a;
)

c8ir_def(jp_nnn,
    c8->pc = *(uint8_t*)a + b;
)

c8ir_def(call,
    c8_push(c8);
    c8->pc = a;
)

c8ir_def(se_kk,
    if (*(uint8_t*)a == b)
        c8->pc += 2;
)

c8ir_def(sne_kk,
    if (*(uint8_t*)a != b)
        c8->pc += 2;
)

c8ir_def(se,
    uint8_t *vx = (uint8_t*)a;
    uint8_t *vy = (uint8_t*)b;
    if (*vx == *vy)
        c8->pc += 2;
)

c8ir_def(sne,
    uint8_t *vx = (uint8_t*)a;
    uint8_t *vy = (uint8_t*)b;
    if (*vx != *vy)
        c8->pc += 2;
)

c8ir_def(ld_kk,
    if ((uintptr_t)&c8->i == d)
        *(uint16_t*)d = a;
    else
        *(uint8_t*)d = a;
)

c8ir_def(ld,
    *(uint8_t*)d = *(uint8_t*)a;
)

c8ir_def(ld_f,
    *(uint16_t*)d = CHIP8_FONT_ADDR + *(uint8_t*)a * 5;
)

c8ir_def(ld_bcd,
    uint8_t v = *(uint8_t*)a;
    c8->ram[(c8->i+0) & 0xfff] = v / 100;
    c8->ram[(c8->i+1) & 0xfff] = v / 10 % 10;
    c8->ram[(c8->i+2) & 0xfff] = v % 10;

    c8ir_cache[(c8->i+0) & 0xfff] = NULL;
    c8ir_cache[(c8->i+1) & 0xfff] = NULL;
    c8ir_cache[(c8->i+2) & 0xfff] = NULL;
)

c8ir_def(ld_r2m,
    uint8_t x = (uint8_t*)a - c8->v;

    memset(&c8ir_cache[c8->i], 0, sizeof(c8ir_op_t) * x + 1);

    memcpy(&c8->ram[c8->i], (uint8_t*)a, x+1);
    c8->i = c8->i+x+1;
)

c8ir_def(ld_m2r,
    uint8_t x = (uint8_t*)a - c8->v;
    memcpy((uint8_t*)a, &c8->ram[c8->i], x+1);
    c8->i = c8->i+x+1;
)

c8ir_def(add_kk,
    *(uint8_t*)d = *(uint8_t*)a + b;
)

c8ir_def(or,
    *(uint8_t*)d = *(uint8_t*)a | *(uint8_t*)b;
)
c8ir_def(and,
    *(uint8_t*)d = *(uint8_t*)a & *(uint8_t*)b;
)
c8ir_def(xor,
    *(uint8_t*)d = *(uint8_t*)a ^ *(uint8_t*)b;
)

c8ir_def(add,
    if ((uintptr_t)&c8->i == a)
        *(uint16_t*)d = *(uint16_t*)a + *(uint8_t*)b;
    else
    {
        c8->v[15] = ((uint32_t)*(uint8_t*)a + (uint32_t)*(uint8_t*)b) > 0xff;
        *(uint8_t*)d = *(uint8_t*)a + *(uint8_t*)b;
    }
)

c8ir_def(sub,
    c8->v[15] = *(uint8_t*)a > *(uint8_t*)a;
    *(uint8_t*)d = *(uint8_t*)a - *(uint8_t*)b;
)

c8ir_def(shr,
    c8->v[15] = *(uint8_t*)a & 1;
    *(uint8_t*)d = *(uint8_t*)a >> 1;
)

c8ir_def(shl,
    c8->v[15] = *(uint8_t*)a >> 7;
    *(uint8_t*)d = *(uint8_t*)a << 1;
)

c8ir_def(rnd,
    *(uint8_t*)d = rand() & a;
)

c8ir_def(drw,
    c8_draw(c8, *(uint8_t*)d, *(uint8_t*)a, b);
)

c8ir_def(skp,
    c8->pc += c8->kbd[*(uint8_t*)a] * 2;
)

c8ir_def(sknp,
    c8->pc += (!c8->kbd[*(uint8_t*)a]) * 2;
)

c8ir_def(ld_key,
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
        *(uint8_t*)d = result;
)

static const c8ir_tac_t *c8_ir_trans(chip8_t *c8)
{
    c8ir_tac_t *tac      = &c8ir_pool[c8->pc];
    const uint16_t instr = c8->ram[c8->pc] << 8 | c8->ram[c8->pc + 1];
    const uint8_t x    = (instr >> 8) & 0x0f;
    const uint8_t y    = (instr >> 4) & 0xf;
    const uint8_t kk   = instr & 0x00ff;
    const uint16_t nnn = instr & 0x0fff;

    uint8_t *vx = &c8->v[x];
    uint8_t *vy = &c8->v[y];

    tac->op = c8ir_illegal;
    tac->d  = tac->a = tac->b = 0;

    switch (instr >> 12)
    {
    case 0x0: // these call host functions
        switch (nnn)
        {
        case 0x00e0: // cls
            tac->op = c8ir_cls;
            break;
        case 0x00ee: // ret
            tac->op = c8ir_pop;
            break;
        default: // sys addr
            tac->op = c8ir_jp;
            tac->a  = nnn;
            break;
        }
        break;
    case 0x1: // jp nnn
        tac->op = c8ir_jp;
        tac->a  = nnn;
        break;
    case 0x2: // call nnn
        tac->op = c8ir_call;
        tac->a  = nnn;
        break;
    case 0x3: // se vx, kk
        tac->op = c8ir_se_kk;
        tac->a  = (uintptr_t)vx;
        tac->b  = kk;
        break;
    case 0x4: // sne vx, kk
        tac->op = c8ir_sne_kk;
        tac->a  = (uintptr_t)vx;
        tac->b  = kk;
        break;
    case 0x5: // se vx, vy
        tac->op = c8ir_se;
        tac->a  = (uintptr_t)vx;
        tac->b  = (uintptr_t)vy;
        break;
    case 0x6: // ld vx, kk
        tac->op = c8ir_ld_kk;
        tac->d  = (uintptr_t)vx;
        tac->a  = kk;
        break;
    case 0x7: // add vx, kk
        tac->op = c8ir_add_kk;
        tac->d  = (uintptr_t)vx;
        tac->a  = (uintptr_t)vx;
        tac->b  = kk;
        break;
    case 0x8: // op vx, vy
    {
        switch (instr & 0x000f)
        {
        case 0x0: // ld
            tac->op = c8ir_ld;
            tac->d  = (uintptr_t)vx;
            tac->a  = (uintptr_t)vy;
            break;
        case 0x1: // or
            tac->op = c8ir_or;
            tac->d  = (uintptr_t)vx;
            tac->a  = (uintptr_t)vx;
            tac->b  = (uintptr_t)vy;
            break;
        case 0x2: // and
            tac->op = c8ir_and;
            tac->d  = (uintptr_t)vx;
            tac->a  = (uintptr_t)vx;
            tac->b  = (uintptr_t)vy;
            break;
        case 0x3: // xor
            tac->op = c8ir_xor;
            tac->d  = (uintptr_t)vx;
            tac->a  = (uintptr_t)vx;
            tac->b  = (uintptr_t)vy;
            break;
        case 0x4: // add
            tac->op = c8ir_add;
            tac->d  = (uintptr_t)vx;
            tac->a  = (uintptr_t)vx;
            tac->b  = (uintptr_t)vy;
            break;
        case 0x5: // sub
            tac->op = c8ir_sub;
            tac->d  = (uintptr_t)vx;
            tac->a  = (uintptr_t)vx;
            tac->b  = (uintptr_t)vy;
            break;
        case 0x6: // shr
            // XXX: doc says vx = vy >> 1, vf = vy &1
            tac->op = c8ir_shr;
            tac->d  = (uintptr_t)vx;
            tac->a  = (uintptr_t)vx;
            break;
        case 0x7: // subn
            tac->op = c8ir_sub;
            tac->d  = (uintptr_t)vx;
            tac->a  = (uintptr_t)vy;
            tac->b  = (uintptr_t)vx;
            break;
        case 0xe: // shl
            // XXX: doc says vx = vy << 1, vf = vy >> 7
            tac->op = c8ir_shl;
            tac->d  = (uintptr_t)vx;
            tac->a  = (uintptr_t)vx;
            break;
        }
        break;
    }
    case 0x9: // sne vx, vy
        tac->op = c8ir_sne;
        tac->a  = (uintptr_t)vx;
        tac->b  = (uintptr_t)vy;
        break;
    case 0xa: // ld i, nnn
        tac->op = c8ir_ld_kk;
        tac->d  = (uintptr_t)&c8->i;
        tac->a  = nnn;
        break;
    case 0xb: // jp v0, addr
        tac->op = c8ir_jp_nnn;
        tac->a  = (uintptr_t)vx;
        tac->b  = nnn;
        break;
    case 0xc: // rnd vx, byte
        tac->op = c8ir_rnd;
        tac->d  = (uintptr_t)vx;
        tac->a  = kk;
        break;
    case 0xd: // drw vx, vy, nibble
        tac->op = c8ir_drw;
        tac->d  = (uintptr_t)vx;
        tac->a  = (uintptr_t)vy;
        tac->b  = instr & 0xf;
        break;
    case 0xe: // op vx
        switch (instr & 0xff)
        {
        case 0x9e: // skp vx
            tac->op = c8ir_skp;
            tac->a  = (uintptr_t)vx;
            break;
        case 0xa1: // sknp vx
            tac->op = c8ir_sknp;
            tac->a  = (uintptr_t)vx;
            break;
        }
        break;
    case 0xf: // op o1, o2
        switch (instr & 0xff)
        {
        case 0x07: // ld vx, dt
            tac->op = c8ir_ld;
            tac->d  = (uintptr_t)vx;
            tac->a  = (uintptr_t)&c8->dt;
            break;
        case 0x0a: // ld vx, key
        {
            tac->op = c8ir_ld_key;
            tac->d  = (uintptr_t)vx;
            break;
        }
        case 0x15: // ld dt, vx
            tac->op = c8ir_ld;
            tac->d  = (uintptr_t)&c8->dt;
            tac->a  = (uintptr_t)vx;
            break;
        case 0x18: // ld st, vx
            tac->op = c8ir_ld;
            tac->d  = (uintptr_t)&c8->st;
            tac->a  = (uintptr_t)vx;
            break;
        case 0x1e: // add i, vx
            tac->op = c8ir_add;
            tac->d  = (uintptr_t)&c8->i;
            tac->a  = (uintptr_t)&c8->i;
            tac->b  = (uintptr_t)vx;
            break;
        case 0x29: // ld f, vx
            tac->op = c8ir_ld_f;
            tac->d  = (uintptr_t)&c8->i;
            tac->a  = (uintptr_t)vx;
            break;
        case 0x33: // ld b, vx
            tac->op = c8ir_ld_bcd;
            tac->a  = (uintptr_t)vx;
            break;
        case 0x55: // ld [i], vx
            tac->op = c8ir_ld_r2m;
            tac->a  = (uintptr_t)vx;
            break;
        case 0x65: // ld vx, [i]
            tac->op = c8ir_ld_m2r;
            tac->a  = (uintptr_t)vx;
            break;
        }
        break;
    }

    return c8ir_cache[c8->pc] = tac;
}


