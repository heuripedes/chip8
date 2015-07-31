#include "chip8_private.h"
#include <stddef.h>
#include <sys/mman.h>
#include <assert.h>
#include "sljit/sljitLir.h"

typedef void (*c8dyn_op_t)(chip8_t *c8);

typedef struct {
    struct sljit_compiler *c;
    c8dyn_op_t cache[4096];
} c8dyn_t;

#define c8dyn_def_begin(name) static void c8dyn_##name(chip8_t *c8, C8DYN_OP_ARGS) { c8->pc += 2;
#define c8dyn_def_end(name) }

static void c8dyn_invalidate(chip8_t *c8, uint16_t begin, uint16_t end)
{
    c8dyn_t *dyn = (c8dyn_t*)c8->dyn;

    for (uint16_t i = begin; i < end; ++i)
    {
        if (dyn->cache[i])
        {
            sljit_free_code((void*)dyn->cache[i]);
            dyn->cache[i] = NULL;
        }
    }
}

//c8dyn_def_begin(illegal)
//    c8->state |= CHIP8_STATE_ILEGAL;
//c8dyn_def_end()

//c8dyn_def_begin(cls)
//    memset(c8->vram, 0, CHIP8_VIDEO_ROWS * CHIP8_VIDEO_COLS);
//c8dyn_def_end()

//c8dyn_def_begin(pop)
//    c8_pop(c8);
//c8dyn_def_end()

//c8dyn_def_begin(jp)
//    c8_jump(c8, a);
//c8dyn_def_end()

//c8dyn_def_begin(jp_nnn)
//    c8_jump(c8, *(uint8_t*)a + b);
//c8dyn_def_end()

//c8dyn_def_begin(call)
//    c8_push(c8);
//    c8_jump(c8, a);
//c8dyn_def_end()

//c8dyn_def_begin(se_kk)
//    if (*(uint8_t*)a == b)
//        c8->pc += 2;
//c8dyn_def_end()

//c8dyn_def_begin(sne_kk)
//    if (*(uint8_t*)a != b)
//        c8->pc += 2;
//c8dyn_def_end()

//c8dyn_def_begin(se)
//    uint8_t *vx = (uint8_t*)a;
//    uint8_t *vy = (uint8_t*)b;
//    if (*vx == *vy)
//        c8->pc += 2;
//c8dyn_def_end()

//c8dyn_def_begin(sne)
//    uint8_t *vx = (uint8_t*)a;
//    uint8_t *vy = (uint8_t*)b;
//    if (*vx != *vy)
//        c8->pc += 2;
//c8dyn_def_end()

//c8dyn_def_begin(ld_kk)
//    if ((uintptr_t)&c8->i == d)
//        *(uint16_t*)d = a;
//    else
//        *(uint8_t*)d = a;
//c8dyn_def_end()

//c8dyn_def_begin(ld)
//    *(uint8_t*)d = *(uint8_t*)a;
//c8dyn_def_end()

//c8dyn_def_begin(ld_f)
//    *(uint16_t*)d = CHIP8_FONT_ADDR + *(uint8_t*)a * 5;
//c8dyn_def_end()

//c8dyn_def_begin(ld_bcd)
//    uint8_t v = *(uint8_t*)a;
//    c8->ram[(c8->i+0) & 0xfff] = v / 100;
//    c8->ram[(c8->i+1) & 0xfff] = v / 10 % 10;
//    c8->ram[(c8->i+2) & 0xfff] = v % 10;

//    c8dyn_invalidate(c8, c8->i, c8->i+2);
//c8dyn_def_end()

//c8dyn_def_begin(ld_r2m)
//    ptrdiff_t x = a - (uintptr_t)c8->v;
//    c8dyn_invalidate(c8, c8->i, x+1);
//    memcpy(&c8->ram[c8->i], c8->v, x+1);
//    c8->i = c8->i+x+1;
//c8dyn_def_end()

//c8dyn_def_begin(ld_m2r)
//    ptrdiff_t x = a - (uintptr_t)c8->v;
//    memcpy(c8->v, &c8->ram[c8->i], x+1);
//    c8->i = c8->i+x+1;
//c8dyn_def_end()

//c8dyn_def_begin(add_kk)
//    *(uint8_t*)d = *(uint8_t*)a + b;
//c8dyn_def_end()

//c8dyn_def_begin(or)
//    *(uint8_t*)d = *(uint8_t*)a | *(uint8_t*)b;
//c8dyn_def_end()

//c8dyn_def_begin(and)
//    *(uint8_t*)d = *(uint8_t*)a & *(uint8_t*)b;
//c8dyn_def_end()

//c8dyn_def_begin(xor)
//    *(uint8_t*)d = *(uint8_t*)a ^ *(uint8_t*)b;
//c8dyn_def_end()

//c8dyn_def_begin(add)
//    if ((uintptr_t)&c8->i == a)
//        *(uint16_t*)d = *(uint16_t*)a + *(uint8_t*)b;
//    else
//    {
//        c8->v[15] = ((uint32_t)*(uint8_t*)a + (uint32_t)*(uint8_t*)b) > 0xff;
//        *(uint8_t*)d = *(uint8_t*)a + *(uint8_t*)b;
//    }
//c8dyn_def_end()

//c8dyn_def_begin(sub)

//    c8->v[15] = *(uint8_t*)a > *(uint8_t*)a;
//    *(uint8_t*)d = *(uint8_t*)a - *(uint8_t*)b;
//c8dyn_def_end()

//c8dyn_def_begin(shr)

//    c8->v[15] = *(uint8_t*)a & 1;
//    *(uint8_t*)d = *(uint8_t*)a >> 1;
//c8dyn_def_end()

//c8dyn_def_begin(shl)

//    c8->v[15] = *(uint8_t*)a >> 7;
//    *(uint8_t*)d = *(uint8_t*)a << 1;
//c8dyn_def_end()

//c8dyn_def_begin(rnd)

//    *(uint8_t*)d = rand() & a;
//c8dyn_def_end()

//c8dyn_def_begin(drw)

//    c8_draw(c8, *(uint8_t*)d, *(uint8_t*)a, b);
//c8dyn_def_end()

//c8dyn_def_begin(skp)

//    c8->pc += c8->kbd[*(uint8_t*)a] * 2;
//c8dyn_def_end()

//c8dyn_def_begin(sknp)

//    c8->pc += (!c8->kbd[*(uint8_t*)a]) * 2;
//c8dyn_def_end()

//c8dyn_def_begin(ld_key)
//    uint8_t result = 255;

//    for (int i = 0; i < 16; ++i)
//    {
//        if (c8->kbd[i])
//        {
//            result = i;
//            break;
//        }
//    }

//    if (result == 255)
//        c8->pc -= 2;
//    else
//        *(uint8_t*)d = result;
//c8dyn_def_end()

static void dump_code(void *code, sljit_uw len)
{
    if (!code || !len)
    {
        fprintf(stderr, "No code generated\n");
        return;
    }

    FILE *fp = fopen("/tmp/slj_dump", "wb");
    if (!fp)
        return;
    fwrite(code, len, 1, fp);
    fflush(fp);
    fclose(fp);

#if defined(SLJIT_CONFIG_X86_64)

    system("objdump -b binary -m l1om -D /tmp/slj_dump | grep ':\t'");

#elif defined(SLJIT_CONFIG_X86_32)

    system("objdump -b binary -m i386 -D /tmp/slj_dump | grep ':\t'");

#endif
}

static c8dyn_op_t c8dyn_emit_cls(chip8_t *c8, c8dyn_t *dyn)
{
    sljit_emit_enter(dyn->c, 0, 1, 1, 1, 0, 0, 0);

    // R0 = S0; c8_clear(R0);
    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_ijump(dyn->c, SLJIT_CALL1, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8_clear));

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_ret(chip8_t *c8, c8dyn_t *dyn)
{
    sljit_emit_enter(dyn->c, 0, 1, 1, 1, 0, 0, 0);

    // R0 = S0; c8_pop(R0);
    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_ijump(dyn->c, SLJIT_CALL1, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8_pop));

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_jp_nnn(chip8_t *c8, c8dyn_t *dyn, uint16_t nnn)
{
    sljit_emit_enter(dyn->c, 0, 1, 2, 1, 0, 0, 0);

    // R0 = S0; R1 = nnn; c8_jump(R0, R1);
    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_op1(dyn->c, SLJIT_MOV_UH, SLJIT_R1, 0, SLJIT_IMM, nnn & 0xfff);
    sljit_emit_ijump(dyn->c, SLJIT_CALL1, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8_jump));

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_call_nnn(chip8_t *c8, c8dyn_t *dyn, uint16_t nnn)
{
    sljit_emit_enter(dyn->c, 0, 1, 2, 1, 0, 0, 0);

    // R0 = S0; c8_push(R0);
    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_ijump(dyn->c, SLJIT_CALL1, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8_push));

    // R0 = S0; R1 = nnn; c8_jump(R0, R1);
    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_op1(dyn->c, SLJIT_MOV_UH, SLJIT_R1, 0, SLJIT_IMM, nnn & 0xfff);
    sljit_emit_ijump(dyn->c, SLJIT_CALL1, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8_jump));

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_cond_kk(chip8_t *c8, c8dyn_t *dyn, bool equal, uint8_t x, uint8_t kk)
{
    sljit_emit_enter(dyn->c, 0, 1, 0, 1, 0, 0, 0);

    if (x > CHIP8_LAST_V_REG)
    {
        fprintf(stderr, "%s: invalid register %1x\n", (equal ? "se_kk" : "sne_kk"), x);
        exit(1);
    }

    struct sljit_jump *dont_skip;

    // because of early return, equal == SLJIT_NOT_EQUAL, !equal == SLJIT_EQUAL
    sljit_si type = (equal ? SLJIT_NOT_EQUAL : SLJIT_EQUAL);

    dont_skip = sljit_emit_cmp(dyn->c, type, SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(chip8_t, v) + x, SLJIT_IMM, kk);
    sljit_emit_op2(dyn->c, SLJIT_ADD, SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(chip8_t, pc), SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(chip8_t, pc), SLJIT_IMM, 2);

    sljit_set_label(dont_skip, sljit_emit_label(dyn->c));
    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_cond(chip8_t *c8, c8dyn_t *dyn, bool equal, uint8_t x, uint8_t y)
{
    sljit_emit_enter(dyn->c, 0, 1, 0, 1, 0, 0, 0);

    if (x > CHIP8_LAST_V_REG || y > CHIP8_LAST_V_REG)
    {
        fprintf(stderr, "%s: invalid registers %1x %1x\n", (equal ? "se" : "sne"), x, y);
        exit(1);
    }

    struct sljit_jump *dont_skip;

    // because of early return, equal == SLJIT_NOT_EQUAL, !equal == SLJIT_EQUAL
    sljit_si type = (equal ? SLJIT_NOT_EQUAL : SLJIT_EQUAL);

    dont_skip = sljit_emit_cmp(dyn->c, type, SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(chip8_t, v) + x, SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(chip8_t, v) + y);
    sljit_emit_op2(dyn->c, SLJIT_ADD, SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(chip8_t, pc), SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(chip8_t, pc), SLJIT_IMM, 2);

    sljit_set_label(dont_skip, sljit_emit_label(dyn->c));
    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_load_imm(chip8_t *c8, c8dyn_t *dyn, uint8_t x, uint16_t imm)
{
    sljit_emit_enter(dyn->c, 0, 1, 0, 1, 0, 0, 0);

    // &S0->v[n] == S0->v + n * sizeof(S0->v[0])
    if (x < CHIP8_NUM_V_REGS) // S0->v[x] = kk
        sljit_emit_op1(dyn->c, SLJIT_MOV_UB, SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(chip8_t, v) + x, SLJIT_IMM, imm & 0xff);
    else if (x == CHIP8_I)  // S0->i = nnn
        sljit_emit_op1(dyn->c, SLJIT_MOV_UH, SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(chip8_t, i), SLJIT_IMM, imm & 0xfff);
    else
    {
        fprintf(stderr, "ld: invalid register %1x\n", x);
        abort();
    }

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_add_imm(chip8_t *c8, c8dyn_t *dyn, uint8_t x, uint16_t imm)
{
    sljit_emit_enter(dyn->c, 0, 1, 0, 1, 0, 0, 0);

    if (x < CHIP8_NUM_V_REGS) // S0->v[x] += kk
        sljit_emit_op2(dyn->c, SLJIT_ADD, SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(chip8_t, v) + x, SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(chip8_t, v) + x, SLJIT_IMM, imm & 0xff);
    else if (x == CHIP8_I) // S0->i += nnn
        sljit_emit_op2(dyn->c, SLJIT_ADD, SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(chip8_t, i), SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(chip8_t, i), SLJIT_IMM, imm & 0xfff);
    else
    {
        fprintf(stderr, "add: invalid register %1x\n", x);
        abort();
    }

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);
    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_translate(chip8_t *c8)
{
    c8dyn_t   *dyn = (c8dyn_t*)c8->dyn;
    c8dyn_op_t *fn = &dyn->cache[c8->pc];

    const uint16_t instr = c8->ram[c8->pc] << 8 | c8->ram[c8->pc + 1];
    const uint8_t x    = (instr >> 8) & 0x0f;
    const uint8_t y    = (instr >> 4) & 0xf;
    const uint8_t kk   = instr & 0x00ff;
    const uint16_t nnn = instr & 0x0fff;

    *fn = NULL;
    dyn->c = sljit_create_compiler(NULL);

    switch (instr >> 12)
    {
    case 0x0: // these call host functions
        switch (nnn)
        {
        case 0x00e0: // cls
            *fn = c8dyn_emit_cls(c8, dyn);
            break;
        case 0x00ee: // ret
            *fn = c8dyn_emit_ret(c8, dyn);
            break;
        default: // sys addr
            *fn = c8dyn_emit_jp_nnn(c8, dyn, nnn);
            break;
        }
        break;
    case 0x1: // jp nnn
        *fn = c8dyn_emit_jp_nnn(c8, dyn, nnn);
        break;
    case 0x2: // call nnn
        *fn = c8dyn_emit_call_nnn(c8, dyn, nnn);
        break;
    case 0x3: // se vx, kk
        *fn = c8dyn_emit_cond_kk(c8, dyn, true, x, kk);
        break;
    case 0x4: // sne vx, kk
        *fn = c8dyn_emit_cond_kk(c8, dyn, false, x, kk);
        break;
    case 0x5: // se vx, vy
        *fn = c8dyn_emit_cond(c8, dyn, true, x, y);
        break;
    case 0x6: // ld vx, kk
        *fn = c8dyn_emit_load_imm(c8, dyn, x, kk);
        break;
    case 0x7: // add vx, kk
        *fn = c8dyn_emit_add_imm(c8, dyn, x, kk);
        break;
//    case 0x8: // op vx, vy
//    {
//        switch (instr & 0x000f)
//        {
//        case 0x0: // ld
//            tac->op = c8dyn_ld;
//            tac->d  = (uintptr_t)vx;
//            tac->a  = (uintptr_t)vy;
//            break;
//        case 0x1: // or
//            tac->op = c8dyn_or;
//            tac->d  = (uintptr_t)vx;
//            tac->a  = (uintptr_t)vx;
//            tac->b  = (uintptr_t)vy;
//            break;
//        case 0x2: // and
//            tac->op = c8dyn_and;
//            tac->d  = (uintptr_t)vx;
//            tac->a  = (uintptr_t)vx;
//            tac->b  = (uintptr_t)vy;
//            break;
//        case 0x3: // xor
//            tac->op = c8dyn_xor;
//            tac->d  = (uintptr_t)vx;
//            tac->a  = (uintptr_t)vx;
//            tac->b  = (uintptr_t)vy;
//            break;
//        case 0x4: // add
//            tac->op = c8dyn_add;
//            tac->d  = (uintptr_t)vx;
//            tac->a  = (uintptr_t)vx;
//            tac->b  = (uintptr_t)vy;
//            break;
//        case 0x5: // sub
//            tac->op = c8dyn_sub;
//            tac->d  = (uintptr_t)vx;
//            tac->a  = (uintptr_t)vx;
//            tac->b  = (uintptr_t)vy;
//            break;
//        case 0x6: // shr
//            // XXX: doc says vx = vy >> 1, vf = vy &1
//            tac->op = c8dyn_shr;
//            tac->d  = (uintptr_t)vx;
//            tac->a  = (uintptr_t)vx;
//            break;
//        case 0x7: // subn
//            tac->op = c8dyn_sub;
//            tac->d  = (uintptr_t)vx;
//            tac->a  = (uintptr_t)vy;
//            tac->b  = (uintptr_t)vx;
//            break;
//        case 0xe: // shl
//            // XXX: doc says vx = vy << 1, vf = vy >> 7
//            tac->op = c8dyn_shl;
//            tac->d  = (uintptr_t)vx;
//            tac->a  = (uintptr_t)vx;
//            break;
//        }
//        break;
//    }
    case 0x9: // sne vx, vy
        *fn = c8dyn_emit_cond(c8, dyn, true, x, y);
        break;
    case 0xa: // ld i, nnn
        *fn = c8dyn_emit_load_imm(c8, dyn, CHIP8_I, nnn);
        break;
//    case 0xb: // jp v0, addr
//        tac->op = c8dyn_jp_nnn;
//        tac->a  = (uintptr_t)vx;
//        tac->b  = nnn;
//        break;
//    case 0xc: // rnd vx, byte
//        tac->op = c8dyn_rnd;
//        tac->d  = (uintptr_t)vx;
//        tac->a  = kk;
//        break;
//    case 0xd: // drw vx, vy, nibble
//        tac->op = c8dyn_drw;
//        tac->d  = (uintptr_t)vx;
//        tac->a  = (uintptr_t)vy;
//        tac->b  = instr & 0xf;
//        break;
//    case 0xe: // op vx
//        switch (instr & 0xff)
//        {
//        case 0x9e: // skp vx
//            tac->op = c8dyn_skp;
//            tac->a  = (uintptr_t)vx;
//            break;
//        case 0xa1: // sknp vx
//            tac->op = c8dyn_sknp;
//            tac->a  = (uintptr_t)vx;
//            break;
//        }
//        break;
//    case 0xf: // op o1, o2
//        switch (instr & 0xff)
//        {
//        case 0x07: // ld vx, dt
//            tac->op = c8dyn_ld;
//            tac->d  = (uintptr_t)vx;
//            tac->a  = (uintptr_t)&c8->dt;
//            break;
//        case 0x0a: // ld vx, key
//        {
//            tac->op = c8dyn_ld_key;
//            tac->d  = (uintptr_t)vx;
//            break;
//        }
//        case 0x15: // ld dt, vx
//            tac->op = c8dyn_ld;
//            tac->d  = (uintptr_t)&c8->dt;
//            tac->a  = (uintptr_t)vx;
//            break;
//        case 0x18: // ld st, vx
//            tac->op = c8dyn_ld;
//            tac->d  = (uintptr_t)&c8->st;
//            tac->a  = (uintptr_t)vx;
//            break;
//        case 0x1e: // add i, vx
//            tac->op = c8dyn_add;
//            tac->d  = (uintptr_t)&c8->i;
//            tac->a  = (uintptr_t)&c8->i;
//            tac->b  = (uintptr_t)vx;
//            break;
//        case 0x29: // ld f, vx
//            tac->op = c8dyn_ld_f;
//            tac->d  = (uintptr_t)&c8->i;
//            tac->a  = (uintptr_t)vx;
//            break;
//        case 0x33: // ld b, vx
//            tac->op = c8dyn_ld_bcd;
//            tac->a  = (uintptr_t)vx;
//            break;
//        case 0x55: // ld [i], vx
//            tac->op = c8dyn_ld_r2m;
//            tac->a  = (uintptr_t)vx;
//            break;
//        case 0x65: // ld vx, [i]
//            tac->op = c8dyn_ld_m2r;
//            tac->a  = (uintptr_t)vx;
//            break;
//        }
//        break;
    }

    fprintf(stderr, "PC=%04x\n", c8->pc);
    dump_code(*fn, sljit_get_generated_code_size(dyn->c));

    if (!*fn)
        fprintf(stderr, "Compiler error: %i\n", sljit_get_compiler_error(dyn->c));

    sljit_free_compiler(dyn->c);

    return *fn;
}


static void c8_dyn_step(chip8_t *c8)
{
    c8dyn_t *dyn = (c8dyn_t*)c8->dyn;
    while (c8->cycles)
    {
        c8dyn_op_t fn = dyn->cache[c8->pc];

        if (!fn)
            fn = c8dyn_translate(c8);

        c8->pc += 2;

        if (!fn)
        {
            fprintf(stderr, "translation failed.\n");
            exit(1);
        }

        fn(c8);

        fprintf(stderr, "Next PC is %04x\n", c8->pc);

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

static void c8_dyn_new(chip8_t *c8)
{
    c8dyn_t *dyn;
    c8->dyn = dyn = calloc(1, sizeof(*dyn));


    c8dyn_invalidate(c8, 0, 4096);
}

static void c8_dyn_free(chip8_t *c8)
{
    c8dyn_t *dyn = (c8dyn_t*)c8->dyn;

    c8dyn_invalidate(c8, 0, 4096);

    c8->dyn = NULL;
}
