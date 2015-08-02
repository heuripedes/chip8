#include "chip8_private.h"
#include <stddef.h>
#include <sys/mman.h>
#include <assert.h>
#include "sljit/sljitLir.h"

/*
 * This is a really ineffective dynarec. Every Chip-8 instruction gets compiled
 * to a native function.
 */

typedef void SLJIT_CALL (*c8dyn_op_t)(chip8_t *c8);

typedef struct {
    struct sljit_compiler *c;
    c8dyn_op_t cache[4096];
} c8dyn_t;

#define c8dyn_def_begin(name) static void c8dyn_##name(chip8_t *c8, C8DYN_OP_ARGS) { c8->pc += 2;
#define c8dyn_def_end(name) }


static inline void c8dyn_write_reg(struct sljit_compiler *c, uint8_t reg, sljit_si src, sljit_si srcw)
{
    sljit_si op  = SLJIT_MOV_UB;
    sljit_si dstw = SLJIT_OFFSETOF(chip8_t, v) + reg;

    if (reg > CHIP8_LAST_REG)
    {
        fprintf(stderr, "regw: Invalid reg %u\n", reg);
        exit(1);
    }

    if (reg == CHIP8_I)
    {
        op   = SLJIT_MOV_UH;
        dstw = SLJIT_OFFSETOF(chip8_t, i);
    }
    else if (reg == CHIP8_PC)
    {
        op   = SLJIT_MOV_UH;
        dstw = SLJIT_OFFSETOF(chip8_t, pc);
    }
    else if (reg == CHIP8_DT)
        dstw = SLJIT_OFFSETOF(chip8_t, dt);
    else if (reg == CHIP8_ST)
        dstw = SLJIT_OFFSETOF(chip8_t, st);

    sljit_emit_op1(c, op, SLJIT_MEM1(SLJIT_S0), dstw, src, srcw);
}

static inline void c8dyn_read_reg(struct sljit_compiler *c, bool expand, uint8_t reg, sljit_si dst, sljit_si dstw)
{
    sljit_si op  = SLJIT_MOV_UB;
    sljit_si srcw = SLJIT_OFFSETOF(chip8_t, v) + reg;

    if (reg > CHIP8_LAST_REG)
    {
        fprintf(stderr, "regw: Invalid reg %u\n", reg);
        exit(1);
    }

    if (reg == CHIP8_I)
    {
        op   = SLJIT_MOV_UH;
        srcw = SLJIT_OFFSETOF(chip8_t, i);
    }
    else if (reg == CHIP8_PC)
    {
        op   = SLJIT_MOV_UH;
        srcw = SLJIT_OFFSETOF(chip8_t, pc);
    }
    else if (reg == CHIP8_DT)
        srcw = SLJIT_OFFSETOF(chip8_t, dt);
    else if (reg == CHIP8_ST)
        srcw = SLJIT_OFFSETOF(chip8_t, st);

    sljit_emit_op1(c, op | (expand ? SLJIT_INT_OP : 0), dst, dstw, SLJIT_MEM1(SLJIT_S0), srcw);
}

static void SLJIT_CALL c8dyn_invalidate(chip8_t *c8, uint16_t begin, uint16_t end)
{
    c8dyn_t *dyn = (c8dyn_t*)c8->dyn;

    end &= 0xfff;

    for (uint16_t i = begin; i <= end; ++i)
    {
        if (dyn->cache[i])
        {
            sljit_free_code((void*)dyn->cache[i]);
            dyn->cache[i] = NULL;
        }
    }
}

static void SLJIT_CALL c8dyn_jump(chip8_t *c8, uint16_t addr)
{
    c8_jump(c8, addr);
}

static void SLJIT_CALL c8dyn_ret(chip8_t *c8)
{
    c8_pop(c8);
}

static void SLJIT_CALL c8dyn_call(chip8_t *c8, uint16_t addr)
{
    c8_push(c8);
    c8_jump(c8, addr);
}

static void SLJIT_CALL c8dyn_draw(chip8_t *c8, uint16_t xy, uint8_t nrows)
{
    c8_draw(c8, xy >> 8, xy & 0xFF, nrows);
}

static uint8_t SLJIT_CALL c8dyn_wait_key(chip8_t *c8)
{
    return c8_wait_key(c8);
}

static void SLJIT_CALL c8dyn_load_bcd(chip8_t *c8, uint8_t val)
{
    c8_load_bcd(c8, val);
}

static void SLJIT_CALL c8dyn_load_ram(chip8_t *c8, bool from_ram, uint8_t x)
{
    c8_load_ram(c8, from_ram, x);
}

static uint8_t SLJIT_CALL c8dyn_rnd(chip8_t *c8, uint8_t mask)
{
    return rand() & mask;
}

static void SLJIT_CALL c8dyn_cls(chip8_t *c8)
{
    c8_clear(c8);
}

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

    system("objdump -b binary -m l1om -M intel -D /tmp/slj_dump | grep ':\t' > /dev/stderr");

#elif defined(SLJIT_CONFIG_X86_32)

    system("objdump -b binary -m i386 -D /tmp/slj_dump | grep ':\t' > /dev/stderr");

#endif
}

static c8dyn_op_t c8dyn_emit_cls(chip8_t *c8, c8dyn_t *dyn)
{
    sljit_emit_enter(dyn->c, 0, 1, 1, 1, 0, 0, 0);

    // R0 = S0; c8_clear(R0);
    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_ijump(dyn->c, SLJIT_CALL1, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8dyn_cls));

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_ret(chip8_t *c8, c8dyn_t *dyn)
{
    sljit_emit_enter(dyn->c, 0, 1, 1, 1, 0, 0, 0);

    // R0 = S0; c8_pop(R0);
    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_ijump(dyn->c, SLJIT_CALL1, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8dyn_ret));

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_jp_nnn(chip8_t *c8, c8dyn_t *dyn, uint16_t nnn)
{
    sljit_emit_enter(dyn->c, 0, 1, 2, 1, 0, 0, 0);

    // R0 = S0; R1 = nnn; c8_jump(R0, R1);
    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_op1(dyn->c, SLJIT_MOV_UH, SLJIT_R1, 0, SLJIT_IMM, nnn & 0xfff);
    sljit_emit_ijump(dyn->c, SLJIT_CALL2, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8dyn_jump));

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_jp_disp(chip8_t *c8, c8dyn_t *dyn, uint16_t nnn)
{
    sljit_emit_enter(dyn->c, 0, 1, 2, 1, 0, 0, 0);

    // R0 = S0; R1 = S0->v[x] + nnn; c8_jump(R0, R1);
    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    c8dyn_read_reg(dyn->c, true, CHIP8_V0, SLJIT_R1, 0);
    sljit_emit_op2(dyn->c, SLJIT_ADD, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, nnn & 0xfff);
    sljit_emit_ijump(dyn->c, SLJIT_CALL2, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8dyn_jump));

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_call_nnn(chip8_t *c8, c8dyn_t *dyn, uint16_t nnn)
{
    sljit_emit_enter(dyn->c, 0, 1, 2, 1, 0, 0, 0);

    // R0 = S0; R1 = nnn; c8dyn_call(R0, R1);
    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_op1(dyn->c, SLJIT_MOV_UH, SLJIT_R1, 0, SLJIT_IMM, nnn & 0xfff);
    sljit_emit_ijump(dyn->c, SLJIT_CALL2, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8dyn_call));

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_cond_kk(chip8_t *c8, c8dyn_t *dyn, bool equal, uint8_t x, uint8_t kk)
{

    if (x > CHIP8_LAST_V_REG)
    {
        fprintf(stderr, "%s: invalid register %1x\n", (equal ? "se_kk" : "sne_kk"), x);
        exit(1);
    }

    struct sljit_jump *dont_skip;

    // because of early return, equal == SLJIT_NOT_EQUAL, !equal == SLJIT_EQUAL
    sljit_si type = (equal ? SLJIT_NOT_EQUAL : SLJIT_EQUAL);

    sljit_emit_enter(dyn->c, 0, 1, 1, 1, 0, 0, 0);

    // R0 = S0->v[x]; if (type) return; R0 = S0->pc; R0 += 2; S0->pc = R0;
    c8dyn_read_reg(dyn->c, false, x, SLJIT_R0, 0);
    dont_skip = sljit_emit_cmp(dyn->c, type, SLJIT_R0, 0, SLJIT_IMM, kk);
    c8dyn_read_reg(dyn->c, true, CHIP8_PC, SLJIT_R0, 0);
    sljit_emit_op2(dyn->c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 2);
    c8dyn_write_reg(dyn->c, CHIP8_PC, SLJIT_R0, 0);

    sljit_set_label(dont_skip, sljit_emit_label(dyn->c));
    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_cond(chip8_t *c8, c8dyn_t *dyn, bool equal, uint8_t x, uint8_t y)
{

    if (x > CHIP8_LAST_V_REG || y > CHIP8_LAST_V_REG)
    {
        fprintf(stderr, "%s: invalid registers %1x %1x\n", (equal ? "se" : "sne"), x, y);
        exit(1);
    }

    struct sljit_jump *dont_skip;

    // because of early return, equal == SLJIT_NOT_EQUAL, !equal == SLJIT_EQUAL
    sljit_si type = (equal ? SLJIT_NOT_EQUAL : SLJIT_EQUAL);

    sljit_emit_enter(dyn->c, 0, 1, 2, 1, 0, 0, 0);

    // R0 = S0->v[x]; R1 = S->v[y]; if (type) return; R0 = S0->pc; R0 += 2; S0->pc = R0;
    c8dyn_read_reg(dyn->c, false, x, SLJIT_R0, 0);
    c8dyn_read_reg(dyn->c, false, y, SLJIT_R1, 0);
    dont_skip = sljit_emit_cmp(dyn->c, type, SLJIT_R0, 0, SLJIT_R1, 0);
    c8dyn_read_reg(dyn->c, true, CHIP8_PC, SLJIT_R0, 0);
    sljit_emit_op2(dyn->c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 2);
    c8dyn_write_reg(dyn->c, CHIP8_PC, SLJIT_R0, 0);

    sljit_set_label(dont_skip, sljit_emit_label(dyn->c));
    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_load_imm(chip8_t *c8, c8dyn_t *dyn, uint8_t x, uint16_t imm)
{
    sljit_emit_enter(dyn->c, 0, 1, 0, 1, 0, 0, 0);

    c8dyn_write_reg(dyn->c, x, SLJIT_IMM, imm);

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_add_imm(chip8_t *c8, c8dyn_t *dyn, uint8_t x, uint8_t imm)
{
    if (x > CHIP8_LAST_V_REG)
    {
        fprintf(stderr, "add: invalid register %1x\n", x);
        exit(1);
    }

    sljit_emit_enter(dyn->c, 0, 1, 1, 1, 0, 0, 0);

    c8dyn_read_reg(dyn->c, false, x, SLJIT_R0, 0);
    sljit_emit_op2(dyn->c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, imm);
    c8dyn_write_reg(dyn->c, x, SLJIT_R0, 0);

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);
    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_load(chip8_t *c8, c8dyn_t *dyn, uint8_t x, uint8_t y)
{
    sljit_si  src, srcw, dst, dstw;

    src = dst = SLJIT_MEM1(SLJIT_S0);

    if (x < CHIP8_NUM_V_REGS)
        dstw = SLJIT_OFFSETOF(chip8_t, v) + x;
    else if (x == CHIP8_DT)
        dstw = SLJIT_OFFSETOF(chip8_t, dt);
    else if (x == CHIP8_ST)
        dstw = SLJIT_OFFSETOF(chip8_t, st);
    else
        goto invalid;

    if (y < CHIP8_NUM_V_REGS)
        srcw = SLJIT_OFFSETOF(chip8_t, v) + y;
    else if (y == CHIP8_DT)
        srcw = SLJIT_OFFSETOF(chip8_t, dt);
    else
        goto invalid;

    sljit_emit_enter(dyn->c, 0, 1, 0, 1, 0, 0, 0);

    sljit_emit_op1(dyn->c, SLJIT_MOV_UB, dst, dstw, src, srcw);

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);
    return (c8dyn_op_t)sljit_generate_code(dyn->c);

invalid:
    fprintf(stderr, "load: invalid register combination %1x %1x\n", x, y);
    exit(1);
}

static c8dyn_op_t c8dyn_emit_add(chip8_t *c8, c8dyn_t *dyn, uint8_t x, uint8_t y)
{
    sljit_si  src, srcw, dst, dstw, dstop;

    src = dst = SLJIT_MEM1(SLJIT_S0);

    if (x < CHIP8_NUM_V_REGS)
    {
        dstw  = SLJIT_OFFSETOF(chip8_t, v) + x;
        dstop = SLJIT_MOV_UB;
    }
    else if (x == CHIP8_I)
    {
        dstw  = SLJIT_OFFSETOF(chip8_t, i);
        dstop = SLJIT_MOV_UH;
    }
    else
        goto invalid;

    if (y < CHIP8_NUM_V_REGS)
        srcw = SLJIT_OFFSETOF(chip8_t, v) + y;
    else
        goto invalid;

    sljit_emit_enter(dyn->c, 0, 1, 2, 1, 0, 0, 0);

    if (x != CHIP8_I)
    {
        struct sljit_jump *set_zero;
        struct sljit_jump *keep_going;

        // word R0 = (ubyte)S0->v[x]; word R1 = (ubyte)S0->v[y]
        sljit_emit_op1(dyn->c, SLJIT_IMOV_UB, SLJIT_R0, 0, dst, dstw);
        sljit_emit_op1(dyn->c, SLJIT_IMOV_UB, SLJIT_R1, 0, src, srcw);

        // R0 = R0 + R1
        sljit_emit_op2(dyn->c, SLJIT_IADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);

        // if (r0 <= 0xff) S0->v[15] = 0; else S0->v[15] = 1;
        set_zero = sljit_emit_cmp(dyn->c, SLJIT_LESS_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0xff);
        c8dyn_write_reg(dyn->c, CHIP8_VF, SLJIT_IMM, 1);
        keep_going = sljit_emit_jump(dyn->c, SLJIT_JUMP);
        sljit_set_label(set_zero, sljit_emit_label(dyn->c));
        c8dyn_write_reg(dyn->c, CHIP8_VF, SLJIT_IMM, 0);

        sljit_set_label(keep_going, sljit_emit_label(dyn->c));
    }

    // R0 = x's val; R1 = S0->v[y]; R0 = R0 + R1
    sljit_emit_op1(dyn->c, dstop, SLJIT_R0, 0, dst, dstw);
    sljit_emit_op1(dyn->c, SLJIT_MOV_UB, SLJIT_R1, 0, src, srcw);
    sljit_emit_op2(dyn->c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);

    // *x = R0
    sljit_emit_op1(dyn->c, dstop, dst, dstw, SLJIT_R0, 0);

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);

invalid:
    fprintf(stderr, "add: invalid register combination %1x %1x\n", x, y);
    exit(1);
}

static c8dyn_op_t c8dyn_emit_bwop(chip8_t *c8, c8dyn_t *dyn, char type, uint8_t x, uint8_t y)
{
    sljit_si  src, srcw, dst, dstw, op;

    src = dst = SLJIT_MEM1(SLJIT_S0);

    if (x < CHIP8_NUM_V_REGS)
        dstw = SLJIT_OFFSETOF(chip8_t, v) + x;
    else
        goto invalid;

    if (y < CHIP8_NUM_V_REGS)
        srcw = SLJIT_OFFSETOF(chip8_t, v) + y;
    else
        goto invalid;

    switch (type)
    {
    case '&': op = SLJIT_AND; break;
    case '|': op = SLJIT_OR; break;
    case '^': op = SLJIT_XOR; break;
    case '<': op = SLJIT_SHL; break;
    case '>': op = SLJIT_LSHR; break;
    default:
        fprintf(stderr, "bwop: invalid operation %c\n", type);
        exit(1);
    }

    sljit_emit_enter(dyn->c, 0, 1, 2, 1, 0, 0, 0);

    if (type == '>' || type == '<')
    {
        sljit_emit_op1(dyn->c, SLJIT_IMOV_UB, SLJIT_R0, 0, dst, dstw);

        if (type == '>') // R1 = R0 & 1
            sljit_emit_op2(dyn->c, SLJIT_AND, SLJIT_R1, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
        else // R1 = R0 >> 7
            sljit_emit_op2(dyn->c, SLJIT_LSHR, SLJIT_R1, 0, SLJIT_R0, 0, SLJIT_IMM, 7);

        // vf = R1
        c8dyn_write_reg(dyn->c, CHIP8_VF, SLJIT_R1, 0);

        // R0 = R0 op 1
        sljit_emit_op2(dyn->c, op, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
    }
    else
    {
        sljit_emit_op1(dyn->c, SLJIT_IMOV_UB, SLJIT_R0, 0, dst, dstw);
        sljit_emit_op1(dyn->c, SLJIT_IMOV_UB, SLJIT_R1, 0, src, srcw);
        sljit_emit_op2(dyn->c, op, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
    }

    sljit_emit_op1(dyn->c, SLJIT_MOV_UB, dst, dstw, SLJIT_R0, 0);

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);
    return (c8dyn_op_t)sljit_generate_code(dyn->c);

invalid:
    fprintf(stderr, "bwop (%c): invalid register combination %1x %1x\n", type, x, y);
    exit(1);
}

static c8dyn_op_t c8dyn_emit_sub(chip8_t *c8, c8dyn_t *dyn, bool swap_x_y, uint8_t x, uint8_t y)
{
    sljit_si  src, srcw, dst, dstw;

    src = dst = SLJIT_MEM1(SLJIT_S0);

    if (x < CHIP8_NUM_V_REGS)
        dstw  = SLJIT_OFFSETOF(chip8_t, v) + x;
    else
        goto invalid;

    if (y < CHIP8_NUM_V_REGS)
        srcw = SLJIT_OFFSETOF(chip8_t, v) + y;
    else
        goto invalid;

    sljit_emit_enter(dyn->c, 0, 1, 2, 1, 0, 0, 0);

    struct sljit_jump *set_zero;
    struct sljit_jump *keep_going;

    // R0 = S0->v[x]; R1 = S0->v[y]
    if (swap_x_y)
    {
        sljit_emit_op1(dyn->c, SLJIT_MOV_UB, SLJIT_R0, 0, src, srcw);
        sljit_emit_op1(dyn->c, SLJIT_MOV_UB, SLJIT_R1, 0, dst, dstw);
    }
    else
    {
        sljit_emit_op1(dyn->c, SLJIT_MOV_UB, SLJIT_R0, 0, dst, dstw);
        sljit_emit_op1(dyn->c, SLJIT_MOV_UB, SLJIT_R1, 0, src, srcw);
    }

    // if (r0 < r1) S0->v[15] = 0; else S0->v[15] = 1;
    set_zero = sljit_emit_cmp(dyn->c, SLJIT_LESS, SLJIT_R0, 0, SLJIT_R1, 0);
    c8dyn_write_reg(dyn->c, CHIP8_VF, SLJIT_IMM, 1);
    keep_going = sljit_emit_jump(dyn->c, SLJIT_JUMP);
    sljit_set_label(set_zero, sljit_emit_label(dyn->c));
    c8dyn_write_reg(dyn->c, CHIP8_VF, SLJIT_IMM, 0);
    sljit_set_label(keep_going, sljit_emit_label(dyn->c));

    // R0 = R0 - R1
    sljit_emit_op2(dyn->c, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);

    // *x = R0
    sljit_emit_op1(dyn->c, SLJIT_MOV_UB, dst, dstw, SLJIT_R0, 0);

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);

invalid:
    fprintf(stderr, "add: invalid register combination %1x %1x\n", x, y);
    exit(1);
}

static c8dyn_op_t c8dyn_emit_draw(chip8_t *c8, c8dyn_t *dyn, uint8_t x, uint8_t y, uint8_t height)
{
    sljit_emit_enter(dyn->c, 0, 1, 3, 1, 0, 0, 0);

    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    // R1 = S0->v[x]; R2 = S0->v[x]; R1 = R1 << 8; R1 = R1 | R2;
    c8dyn_read_reg(dyn->c, true, x, SLJIT_R1, 0);
    c8dyn_read_reg(dyn->c, true, y, SLJIT_R2, 0);
    sljit_emit_op2(dyn->c, SLJIT_SHL, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, 8);
    sljit_emit_op2(dyn->c , SLJIT_OR, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_R2, 0);
    // R2 = height
    sljit_emit_op1(dyn->c, SLJIT_MOV_UB, SLJIT_R2, 0, SLJIT_IMM, height);

    // c8dyn_draw(r0, r1, r2);
    sljit_emit_ijump(dyn->c, SLJIT_CALL3, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8dyn_draw));

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_load_f(chip8_t *c8, c8dyn_t *dyn, uint8_t x)
{
    sljit_emit_enter(dyn->c, 0, 1, 2, 1, 0, 0, 0);

    // r0 = f
    sljit_emit_op1(dyn->c, SLJIT_IMOV_UH, SLJIT_R0, 0, SLJIT_IMM, CHIP8_FONT_ADDR);

    // r1 = s0->v[x] * 5
    sljit_emit_op1(dyn->c, SLJIT_IMOV_UB, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), SLJIT_OFFSETOF(chip8_t, v) + x);
    sljit_emit_op2(dyn->c, SLJIT_IMUL, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, 5);

    // r0 = (r0 + r1) & 0xfff
    sljit_emit_op2(dyn->c, SLJIT_IADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
    sljit_emit_op2(dyn->c, SLJIT_IAND, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 0xfff);

    c8dyn_write_reg(dyn->c, CHIP8_I, SLJIT_R0, 0);

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);
    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_cond_key(chip8_t *c8, c8dyn_t *dyn, bool equal, uint8_t x)
{
    if (x > CHIP8_LAST_V_REG)
    {
        fprintf(stderr, "%s: invalid register %1x\n", (equal ? "skp" : "sknp"), x);
        exit(1);
    }

    struct sljit_jump *dont_skip;

    // because of early return, equal == SLJIT_NOT_EQUAL, !equal == SLJIT_EQUAL
    sljit_si type = (equal ? SLJIT_NOT_EQUAL : SLJIT_EQUAL);

    sljit_emit_enter(dyn->c, 0, 1, 1, 1, 0, 0, 0);

    // R0 = S0->v[x]; R0 += offsetof(chip8_t, kbd); R0 = ((uint8_t*)S0)[R0];
    c8dyn_read_reg(dyn->c, true, x, SLJIT_R0, 0);
    sljit_emit_op2(dyn->c, SLJIT_IADD, SLJIT_R0, 0, SLJIT_IMM, SLJIT_OFFSETOF(chip8_t, kbd), SLJIT_R0, 0);
    sljit_emit_op1(dyn->c, SLJIT_MOV_UB, SLJIT_R0, 0, SLJIT_MEM2(SLJIT_S0, SLJIT_R0), 0);

    dont_skip = sljit_emit_cmp(dyn->c, type, SLJIT_R0, 0, SLJIT_IMM, 1);
    c8dyn_read_reg(dyn->c, true, CHIP8_PC, SLJIT_R0, 0);
    sljit_emit_op2(dyn->c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 2);
    c8dyn_write_reg(dyn->c, CHIP8_PC, SLJIT_R0, 0);

    sljit_set_label(dont_skip, sljit_emit_label(dyn->c));
    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_wait_key(chip8_t *c8, c8dyn_t *dyn, uint8_t x)
{
    if (x > CHIP8_LAST_V_REG)
    {
        fprintf(stderr, "wait_key: invalid register %1x\n", x);
        exit(1);
    }

    sljit_emit_enter(dyn->c, 0, 1, 1, 1, 0, 0, 0);

    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_ijump(dyn->c, SLJIT_CALL1, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8dyn_wait_key));
    c8dyn_write_reg(dyn->c, x, SLJIT_R0, 0);

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_load_bcd(chip8_t *c8, c8dyn_t *dyn, uint8_t x)
{
    if (x > CHIP8_LAST_V_REG)
    {
        fprintf(stderr, "load_bcd: invalid register %1x\n", x);
        exit(1);
    }

    sljit_emit_enter(dyn->c, 0, 1, 3, 1, 0, 0, 0);

    // R0 = S0;
    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    // R1 = S0->i; R2 = R1 + 2; c8dyn_invalidate(R0, R1, R2)
    c8dyn_read_reg(dyn->c, true, CHIP8_I, SLJIT_R1, 0);
    sljit_emit_op2(dyn->c, SLJIT_IADD, SLJIT_R2, 0, SLJIT_R1, 0, SLJIT_IMM, 2);
    sljit_emit_ijump(dyn->c, SLJIT_CALL3, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8dyn_invalidate));

    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    c8dyn_read_reg(dyn->c, false, x, SLJIT_R1, 0);
    sljit_emit_ijump(dyn->c, SLJIT_CALL2, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8dyn_load_bcd));

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_load_ram(chip8_t *c8, c8dyn_t *dyn, bool from_ram, uint8_t x)
{
    if (x > CHIP8_LAST_V_REG)
    {
        fprintf(stderr, "load_ram: invalid register %1x\n", x);
        exit(1);
    }

    sljit_emit_enter(dyn->c, 0, 1, 3, 1, 0, 0, 0);

    if (!from_ram)
    {
        // R0 = S0;
        sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
        // R1 = S0->i; R2 = R1 + 2
        c8dyn_read_reg(dyn->c, true, CHIP8_I, SLJIT_R1, 0);
        sljit_emit_op2(dyn->c, SLJIT_ADD, SLJIT_R2, 0, SLJIT_R1, 0, SLJIT_IMM, x);

        sljit_emit_ijump(dyn->c, SLJIT_CALL3, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8dyn_invalidate));
    }

    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_op1(dyn->c, SLJIT_MOV_UB, SLJIT_R1, 0, SLJIT_IMM, from_ram);
    sljit_emit_op1(dyn->c, SLJIT_MOV_UB, SLJIT_R2, 0, SLJIT_IMM, x);
    sljit_emit_ijump(dyn->c, SLJIT_CALL3, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8dyn_load_ram));

    sljit_emit_return(dyn->c, SLJIT_UNUSED, 0, 0);

    return (c8dyn_op_t)sljit_generate_code(dyn->c);
}

static c8dyn_op_t c8dyn_emit_rnd(chip8_t *c8, c8dyn_t *dyn, uint8_t x, uint8_t kk)
{
    if (x > CHIP8_LAST_V_REG)
    {
        fprintf(stderr, "load_ram: invalid register %1x\n", x);
        exit(1);
    }

    sljit_emit_enter(dyn->c, 0, 1, 2, 1, 0, 0, 0);

    // S0->v[x] = c8dyn_rnd(c8, kk);
    sljit_emit_op1(dyn->c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_op1(dyn->c, SLJIT_MOV_UB, SLJIT_R1, 0, SLJIT_IMM, kk);
    sljit_emit_ijump(dyn->c, SLJIT_CALL2, SLJIT_IMM, SLJIT_FUNC_OFFSET(c8dyn_rnd));
    c8dyn_write_reg(dyn->c, x, SLJIT_R0, 0);

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
    case 0x8: // op vx, vy
    {
        switch (instr & 0x000f)
        {
        case 0x0: // ld
            *fn = c8dyn_emit_load(c8, dyn, x, y);
            break;
        case 0x1: // or
            *fn = c8dyn_emit_bwop(c8, dyn, '|', x, y);
            break;
        case 0x2: // and
            *fn = c8dyn_emit_bwop(c8, dyn, '&', x, y);
            break;
        case 0x3: // xor
            *fn = c8dyn_emit_bwop(c8, dyn, '^', x, y);
            break;
        case 0x4: // add
            *fn = c8dyn_emit_add(c8, dyn, x, y);
            break;
        case 0x5: // sub
            *fn = c8dyn_emit_sub(c8, dyn, false, x, y);
            break;
        case 0x6: // shr
            *fn = c8dyn_emit_bwop(c8, dyn, '>', x, x);
            break;
        case 0x7: // subn
            *fn = c8dyn_emit_sub(c8, dyn, true, x, y);
            break;
        case 0xe: // shl
            *fn = c8dyn_emit_bwop(c8, dyn, '<', x, x);
            break;
        }
        break;
    }
    case 0x9: // sne vx, vy
        *fn = c8dyn_emit_cond(c8, dyn, false, x, y);
        break;
    case 0xa: // ld i, nnn
        *fn = c8dyn_emit_load_imm(c8, dyn, CHIP8_I, nnn);
        break;
    case 0xb: // jp v0, addr
        *fn = c8dyn_emit_jp_disp(c8, dyn, nnn);
        break;
    case 0xc: // rnd vx, byte
        *fn = c8dyn_emit_rnd(c8, dyn, x, kk);
        break;
    case 0xd: // drw vx, vy, nibble
        *fn = c8dyn_emit_draw(c8, dyn, x, y, instr & 0xf);
        break;
    case 0xe: // op vx
        switch (instr & 0xff)
        {
        case 0x9e: // skp vx
            *fn = c8dyn_emit_cond_key(c8, dyn, true, x);
            break;
        case 0xa1: // sknp vx
            *fn = c8dyn_emit_cond_key(c8, dyn, false, x);
            break;
        }
        break;
    case 0xf: // op o1, o2
        switch (instr & 0xff)
        {
        case 0x07: // ld vx, dt
            *fn = c8dyn_emit_load(c8, dyn, x, CHIP8_DT);
            break;
        case 0x0a: // ld vx, key
            *fn = c8dyn_emit_wait_key(c8, dyn, x);
            break;
        case 0x15: // ld dt, vx
            *fn = c8dyn_emit_load(c8, dyn, CHIP8_DT, x);
            break;
        case 0x18: // ld st, vx
            *fn = c8dyn_emit_load(c8, dyn, CHIP8_ST, x);
            break;
        case 0x1e: // add i, vx
            *fn = c8dyn_emit_add(c8, dyn, CHIP8_I, x);
            break;
        case 0x29: // ld f, vx
            *fn = c8dyn_emit_load_f(c8, dyn, x);
            break;
        case 0x33: // ld b, vx
            *fn = c8dyn_emit_load_bcd(c8, dyn, x);
            break;
        case 0x55: // ld [i], vx
            *fn = c8dyn_emit_load_ram(c8, dyn, true, x);
            break;
        case 0x65: // ld vx, [i]
            *fn = c8dyn_emit_load_ram(c8, dyn, false, x);
            break;
        }
        break;
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
