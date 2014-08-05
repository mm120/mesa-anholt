/*
 * Copyright (c) 2014 Scott Mansell
 * Copyright © 2014 Broadcom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <inttypes.h>
#include "pipe/p_state.h"
#include "util/u_format.h"
#include "util/u_hash_table.h"
#include "util/u_hash.h"
#include "util/u_memory.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_dump.h"

#include "vc4_context.h"
#include "vc4_qpu.h"
#include "vc4_qir.h"
#include "simpenrose/simpenrose.h"

struct tgsi_to_qir {
        struct tgsi_parse_context parser;
        struct qcompile *c;
        struct qreg *temps;
        struct qreg *inputs;
        struct qreg *outputs;
        struct qreg *uniforms;
        struct qreg *consts;
        uint32_t num_consts;

        struct vc4_shader_state *shader_state;
        struct vc4_fs_key *fs_key;
        struct vc4_vs_key *vs_key;

        uint32_t *uniform_data;
        enum quniform_contents *uniform_contents;
        uint32_t num_uniforms;
        uint32_t num_outputs;
};

struct vc4_key {
        struct vc4_shader_state *shader_state;
};

struct vc4_fs_key {
        struct vc4_key base;
        enum pipe_format color_format;
};

struct vc4_vs_key {
        struct vc4_key base;
        enum pipe_format attr_formats[8];
};

static struct qreg
add_uniform(struct tgsi_to_qir *trans,
            enum quniform_contents contents,
            uint32_t data)
{
        uint32_t uniform = trans->num_uniforms++;
        struct qreg u = { QFILE_UNIF, uniform };

        trans->uniform_contents[uniform] = contents;
        trans->uniform_data[uniform] = data;

        return u;
}

static struct qreg
get_temp_for_uniform(struct tgsi_to_qir *trans, enum quniform_contents contents,
                     uint32_t data)
{
        struct qcompile *c = trans->c;

        for (int i = 0; i < trans->num_uniforms; i++) {
                if (trans->uniform_contents[i] == contents &&
                    trans->uniform_data[i] == data)
                        return trans->uniforms[i];
        }

        struct qreg t = qir_get_temp(c);
        struct qreg u = add_uniform(trans, contents, data);

        qir_emit(c, qir_inst(QOP_MOV, t, u, c->undef));

        trans->uniforms[u.index] = t;
        return t;
}

static struct qreg
qir_uniform_ui(struct tgsi_to_qir *trans, uint32_t ui)
{
        return get_temp_for_uniform(trans, QUNIFORM_CONSTANT, ui);
}

static struct qreg
get_src(struct tgsi_to_qir *trans, struct tgsi_src_register *src, int i)
{
        struct qcompile *c = trans->c;
        struct qreg r = c->undef;

        uint32_t s = i;
        switch (i) {
        case TGSI_SWIZZLE_X:
                s = src->SwizzleX;
                break;
        case TGSI_SWIZZLE_Y:
                s = src->SwizzleY;
                break;
        case TGSI_SWIZZLE_Z:
                s = src->SwizzleZ;
                break;
        case TGSI_SWIZZLE_W:
                s = src->SwizzleW;
                break;
        default:
                abort();
        }

        assert(!src->Indirect);

        switch (src->File) {
        case TGSI_FILE_NULL:
                return r;
        case TGSI_FILE_TEMPORARY:
                r = trans->temps[src->Index * 4 + s];
                break;
        case TGSI_FILE_IMMEDIATE:
                r = trans->consts[src->Index * 4 + s];
                break;
        case TGSI_FILE_CONSTANT:
                r = get_temp_for_uniform(trans, QUNIFORM_UNIFORM,
                                         src->Index * 4 + s);
                break;
        case TGSI_FILE_INPUT:
                r = trans->inputs[src->Index * 4 + s];
                break;
        case TGSI_FILE_SAMPLER:
        case TGSI_FILE_SAMPLER_VIEW:
                r = c->undef;
                break;
        default:
                fprintf(stderr, "unknown src file %d\n", src->File);
                abort();
        }

        if (src->Absolute) {
                struct qreg abs = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_FMAXABS, abs, r, r));
                r = abs;
        }

        if (src->Negate) {
                struct qreg zero = qir_uniform_ui(trans, 0);
                struct qreg neg = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_FSUB, neg, zero, r));
                r = neg;
        }

        return r;
};


static struct qreg
get_dst(struct tgsi_to_qir *trans, struct tgsi_full_instruction *tgsi_inst,
        int i)
{
        struct qcompile *c = trans->c;
        struct tgsi_dst_register *tgsi_dst = &tgsi_inst->Dst[0].Register;

        assert(!tgsi_dst->Indirect);

        switch (tgsi_dst->File) {
        case TGSI_FILE_TEMPORARY:
                trans->temps[tgsi_dst->Index * 4 + i] = qir_get_temp(c);
                return trans->temps[tgsi_dst->Index * 4 + i];
        case TGSI_FILE_OUTPUT:
                trans->outputs[tgsi_dst->Index * 4 + i] = qir_get_temp(c);
                trans->num_outputs = MAX2(trans->num_outputs,
                                          tgsi_dst->Index * 4 + i + 1);
                return trans->outputs[tgsi_dst->Index * 4 + i];
        default:
                fprintf(stderr, "unknown dst file %d\n", tgsi_dst->File);
                abort();
        }
};

static void
tgsi_to_qir_alu(struct tgsi_to_qir *trans,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg dst, struct qreg *src, int i)
{
        struct qcompile *c = trans->c;
        struct qreg srcs[4] = {
                src[0 * 4 + i],
                src[1 * 4 + i],
                src[2 * 4 + i]
        };

        qir_emit(c, qir_inst4(op, dst, srcs));
}

static void
tgsi_to_qir_mad(struct tgsi_to_qir *trans,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg dst, struct qreg *src, int i)
{
        struct qcompile *c = trans->c;
        struct qreg mul = qir_get_temp(c);
        qir_emit(c, qir_inst(QOP_FMUL, mul, src[0 * 4 + i], src[1 * 4 + i]));
        qir_emit(c, qir_inst(QOP_FADD, dst, mul, src[2 * 4 + i]));
}

static void
tgsi_to_qir_lit(struct tgsi_to_qir *trans,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg dst, struct qreg *src, int i)
{
        struct qcompile *c = trans->c;

        switch (i) {
        case 0:
        case 3:
                qir_emit(c, qir_inst(QOP_MOV, dst,
                                     qir_uniform_ui(trans, fui(1.0)), c->undef));
                break;
        case 1:
                qir_emit(c, qir_inst(QOP_FMAX, dst,
                                     src[0 * 4 + 0],
                                     qir_uniform_ui(trans, 0)));
                break;
        case 2: {
                struct qreg srcy_clamp = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_FMAX, srcy_clamp,
                                     src[0 * 4 + 0], qir_uniform_ui(trans, 0)));

                struct qreg log = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_LOG2, log, srcy_clamp, c->undef));
                /* XXX: Clamp src.w to -128..128 */
                struct qreg mul = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_FMUL, mul, src[0 * 4 + 3], log));
                struct qreg exp = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_EXP2, exp, mul, c->undef));

                qir_emit(c, qir_inst4(QOP_CMP, dst,
                                      (struct qreg[4]){src[0 * 4 + 0],
                                                      qir_uniform_ui(trans, 0),
                                                      exp}));
                break;
        }
        }
}

static void
tgsi_to_qir_lrp(struct tgsi_to_qir *trans,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg dst, struct qreg *src, int i)
{
        struct qcompile *c = trans->c;
        struct qreg src1_minus_src2 = qir_get_temp(c);
        struct qreg src0_times_src1_minus_src2 = qir_get_temp(c);

        /* LRP is:
         *    src0 * src1 + (1 - src0) * src2.
         * -> src0 * src1 + src2 - src0 * src2
         * -> src2 + src0 * (src1 - src2)
         */
        qir_emit(c, qir_inst(QOP_FSUB, src1_minus_src2,
                             src[1 * 4 + i], src[2 * 4 + i]));

        qir_emit(c, qir_inst(QOP_FMUL, src0_times_src1_minus_src2,
                             src[0 * 4 + i], src1_minus_src2));

        qir_emit(c, qir_inst(QOP_FADD, dst,
                             src[2 * 4 + i], src0_times_src1_minus_src2));
}

static void
tgsi_to_qir_pow(struct tgsi_to_qir *trans,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg dst, struct qreg *src, int i)
{
        struct qcompile *c = trans->c;
        struct qreg log = qir_get_temp(c);
        struct qreg mul = qir_get_temp(c);

        /* Note that this instruction replicates its result from the x channel
         */
        qir_emit(c, qir_inst(QOP_LOG2, log, src[0 * 4 + 0], c->undef));
        qir_emit(c, qir_inst(QOP_FMUL, mul, src[1 * 4 + 0], log));
        qir_emit(c, qir_inst(QOP_EXP2, dst, mul, c->undef));
}

static void
tgsi_to_qir_trunc(struct tgsi_to_qir *trans,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg dst, struct qreg *src, int i)
{
        struct qcompile *c = trans->c;
        struct qreg trunc = qir_get_temp(c);
        qir_emit(c, qir_inst(QOP_FTOI, trunc, src[0 * 4 + i], c->undef));
        qir_emit(c, qir_inst(QOP_ITOF, dst, trunc, c->undef));
}

static void
tgsi_to_qir_tex(struct tgsi_to_qir *trans,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src)
{
        struct qcompile *c = trans->c;

        assert(!tgsi_inst->Instruction.Saturate);

        struct qreg s = src[0 * 4 + 0];
        struct qreg t = src[0 * 4 + 1];

        if (tgsi_inst->Instruction.Opcode == TGSI_OPCODE_TXP) {
                struct qreg proj = qir_get_temp(c);
                struct qreg temp;

                qir_emit(c, qir_inst(QOP_RCP, proj, src[0 * 4 + 3], c->undef));

                temp = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_FMUL, temp, s, proj));
                s = temp;

                temp = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_FMUL, temp, t, proj));
                t = temp;
        }

        /* There is no native support for GL texture rectangle coordinates, so
         * we have to rescale from ([0, width], [0, height]) to ([0, 1], [0,
         * 1]).
         */
        if (tgsi_inst->Texture.Texture == TGSI_TEXTURE_RECT) {
                struct qreg temp, scale;

                temp = qir_get_temp(c);
                scale = get_temp_for_uniform(trans, QUNIFORM_TEXRECT_SCALE_X,
                                             0 /* XXX */);
                qir_emit(c, qir_inst(QOP_FMUL, temp, s, scale));
                s = temp;

                temp = qir_get_temp(c);
                scale = get_temp_for_uniform(trans, QUNIFORM_TEXRECT_SCALE_Y,
                                             0 /* XXX */);
                qir_emit(c, qir_inst(QOP_FMUL, temp, t, scale));
                t = temp;
        }

        uint32_t tex_and_sampler = 0; /* XXX */
        struct qreg sampler_p0 = add_uniform(trans, QUNIFORM_TEXTURE_CONFIG_P0,
                                             tex_and_sampler);
        struct qreg sampler_p1 = add_uniform(trans, QUNIFORM_TEXTURE_CONFIG_P1,
                                             tex_and_sampler);

        qir_emit(c, qir_inst(QOP_TEX_T, c->undef, t, sampler_p0));
        if (tgsi_inst->Instruction.Opcode == TGSI_OPCODE_TXB) {
                qir_emit(c, qir_inst(QOP_TEX_B, c->undef, src[0 * 4 + 3],
                                     sampler_p1));
                qir_emit(c, qir_inst(QOP_TEX_S, c->undef, s,
                                     add_uniform(trans, QUNIFORM_CONSTANT, 0)));
        } else {
                qir_emit(c, qir_inst(QOP_TEX_S, c->undef, s, sampler_p1));
        }

        qir_emit(c, qir_inst(QOP_TEX_RESULT, c->undef, c->undef, c->undef));

        for (int i = 0; i < 4; i++) {
                if (tgsi_inst->Dst[0].Register.WriteMask & (1 << i));
                qir_emit(c, qir_inst(QOP_R4_UNPACK_A + i,
                                     get_dst(trans, tgsi_inst, i),
                                     c->undef, c->undef));
        }
}

static void
tgsi_to_qir_dp(struct tgsi_to_qir *trans,
               struct tgsi_full_instruction *tgsi_inst,
               int num, struct qreg dst, struct qreg *src, int i)
{
        struct qcompile *c = trans->c;
        struct qreg sum = qir_get_temp(c);

        qir_emit(c, qir_inst(QOP_FMUL, sum, src[0 * 4 + 0], src[1 * 4 + 0]));
        for (int j = 1; j < num; j++) {
                struct qreg mul = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_FMUL, mul,
                                     src[0 * 4 + j], src[1 * 4 + j]));

                struct qreg add = (j != num - 1) ? qir_get_temp(c) : dst;
                qir_emit(c, qir_inst(QOP_FADD, add, sum, mul));
                sum = add;
        }
}

static void
tgsi_to_qir_dp2(struct tgsi_to_qir *trans,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg dst, struct qreg *src, int i)
{
        tgsi_to_qir_dp(trans, tgsi_inst, 2, dst, src, i);
}

static void
tgsi_to_qir_dp3(struct tgsi_to_qir *trans,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg dst, struct qreg *src, int i)
{
        tgsi_to_qir_dp(trans, tgsi_inst, 3, dst, src, i);
}

static void
tgsi_to_qir_dp4(struct tgsi_to_qir *trans,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg dst, struct qreg *src, int i)
{
        tgsi_to_qir_dp(trans, tgsi_inst, 4, dst, src, i);
}

static void
tgsi_to_qir_abs(struct tgsi_to_qir *trans,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg dst, struct qreg *src, int i)
{
        struct qcompile *c = trans->c;
        struct qreg arg = src[0 * 4 + i];
        qir_emit(c, qir_inst(QOP_FMAXABS, dst, arg, arg));
}

static void
emit_tgsi_declaration(struct tgsi_to_qir *trans,
                      struct tgsi_full_declaration *decl)
{
        struct qcompile *c = trans->c;

        switch (decl->Declaration.File) {
        case TGSI_FILE_INPUT:
                for (int i = decl->Range.First * 4;
                     i < (decl->Range.Last + 1) * 4;
                     i++) {
                        struct qreg dst = qir_get_temp(c);
                        trans->inputs[i] = dst;

                        if (c->stage == QSTAGE_FRAG) {
                                struct qreg t = qir_get_temp(c);
                                struct qreg vary = {
                                        QFILE_VARY,
                                        i
                                };

                                qir_emit(c, qir_inst(QOP_MOV, t,
                                                     vary, c->undef));
                                /* XXX: multiply by W */
                                qir_emit(c, qir_inst(QOP_VARY_ADD_C,
                                                     dst, t, c->undef));
                        } else {
                                /* XXX: attribute type/size/count */
                                qir_emit(c, qir_inst(QOP_VPM_READ,
                                                     dst,
                                                     c->undef,
                                                     c->undef));
                        }

                        c->num_inputs++;
                }
                break;
        }
}

static void
emit_tgsi_instruction(struct tgsi_to_qir *trans,
                      struct tgsi_full_instruction *tgsi_inst)
{
        struct qcompile *c = trans->c;
        struct {
                enum qop op;
                void (*func)(struct tgsi_to_qir *trans,
                             struct tgsi_full_instruction *tgsi_inst,
                             enum qop op,
                             struct qreg dst, struct qreg *src, int i);
        } op_trans[] = {
                [TGSI_OPCODE_MOV] = { QOP_MOV, tgsi_to_qir_alu },
                [TGSI_OPCODE_ABS] = { 0, tgsi_to_qir_abs },
                [TGSI_OPCODE_MUL] = { QOP_FMUL, tgsi_to_qir_alu },
                [TGSI_OPCODE_ADD] = { QOP_FADD, tgsi_to_qir_alu },
                [TGSI_OPCODE_SUB] = { QOP_FSUB, tgsi_to_qir_alu },
                [TGSI_OPCODE_MIN] = { QOP_FMIN, tgsi_to_qir_alu },
                [TGSI_OPCODE_MAX] = { QOP_FMAX, tgsi_to_qir_alu },
                [TGSI_OPCODE_RSQ] = { QOP_RSQ, tgsi_to_qir_alu },
                [TGSI_OPCODE_SEQ] = { QOP_SEQ, tgsi_to_qir_alu },
                [TGSI_OPCODE_SNE] = { QOP_SNE, tgsi_to_qir_alu },
                [TGSI_OPCODE_SGE] = { QOP_SGE, tgsi_to_qir_alu },
                [TGSI_OPCODE_SLT] = { QOP_SLT, tgsi_to_qir_alu },
                [TGSI_OPCODE_CMP] = { QOP_CMP, tgsi_to_qir_alu },
                [TGSI_OPCODE_MAD] = { 0, tgsi_to_qir_mad },
                [TGSI_OPCODE_DP2] = { 0, tgsi_to_qir_dp2 },
                [TGSI_OPCODE_DP3] = { 0, tgsi_to_qir_dp3 },
                [TGSI_OPCODE_DP4] = { 0, tgsi_to_qir_dp4 },
                [TGSI_OPCODE_RCP] = { QOP_RCP, tgsi_to_qir_alu },
                [TGSI_OPCODE_RSQ] = { QOP_RSQ, tgsi_to_qir_alu },
                [TGSI_OPCODE_EX2] = { QOP_EXP2, tgsi_to_qir_alu },
                [TGSI_OPCODE_LG2] = { QOP_LOG2, tgsi_to_qir_alu },
                [TGSI_OPCODE_LIT] = { 0, tgsi_to_qir_lit },
                [TGSI_OPCODE_LRP] = { 0, tgsi_to_qir_lrp },
                [TGSI_OPCODE_POW] = { 0, tgsi_to_qir_pow },
                [TGSI_OPCODE_TRUNC] = { 0, tgsi_to_qir_trunc },
        };
        static int asdf = 0;
        uint32_t tgsi_op = tgsi_inst->Instruction.Opcode;

        if (tgsi_op == TGSI_OPCODE_END)
                return;

        struct qreg src_regs[12];
        for (int s = 0; s < 3; s++) {
                for (int i = 0; i < 4; i++) {
                        src_regs[4 * s + i] =
                                get_src(trans, &tgsi_inst->Src[s].Register, i);
                }
        }

        switch (tgsi_op) {
        case TGSI_OPCODE_TEX:
        case TGSI_OPCODE_TXP:
        case TGSI_OPCODE_TXB:
                tgsi_to_qir_tex(trans, tgsi_inst,
                                op_trans[tgsi_op].op, src_regs);
                return;
        default:
                break;
        }

        if (tgsi_op > ARRAY_SIZE(op_trans) || !(op_trans[tgsi_op].func)) {
                fprintf(stderr, "unknown tgsi inst: ");
                tgsi_dump_instruction(tgsi_inst, asdf++);
                fprintf(stderr, "\n");
                abort();
        }

        for (int i = 0; i < 4; i++) {
                if (!(tgsi_inst->Dst[0].Register.WriteMask & (1 << i)))
                        continue;

                struct qreg dst = get_dst(trans, tgsi_inst, i);
                op_trans[tgsi_op].func(trans, tgsi_inst,
                                       op_trans[tgsi_op].op, dst, src_regs, i);

                if (tgsi_inst->Instruction.Saturate) {
                        struct qreg low, high;
                        low = qir_uniform_ui(trans,
                                             fui(tgsi_inst->Instruction.Saturate == TGSI_SAT_MINUS_PLUS_ONE ? -1.0 : 0.0));
                        high = qir_uniform_ui(trans, fui(1.0));
                        struct qreg maxresult = qir_get_temp(c);
                        qir_emit(c, qir_inst(QOP_FMIN, maxresult, dst, high));
                        dst = get_dst(trans, tgsi_inst, i);
                        qir_emit(c, qir_inst(QOP_FMAX, dst, maxresult, low));
                }
        }
}

static void
parse_tgsi_immediate(struct tgsi_to_qir *trans, struct tgsi_full_immediate *imm)
{
        for (int i = 0; i < 4; i++) {
                unsigned n = trans->num_consts++;
                trans->consts[n] = qir_uniform_ui(trans, imm->u[i].Uint);
        }
}

static void
emit_frag_end(struct tgsi_to_qir *trans)
{
        struct qcompile *c = trans->c;

        struct qreg t = qir_get_temp(c);

        const struct util_format_description *format_desc =
                util_format_description(trans->fs_key->color_format);

        struct qreg swizzled_outputs[4] = {
                trans->outputs[format_desc->swizzle[0]],
                trans->outputs[format_desc->swizzle[1]],
                trans->outputs[format_desc->swizzle[2]],
                trans->outputs[format_desc->swizzle[3]],
        };

        qir_emit(c, qir_inst4(QOP_PACK_COLORS, t, swizzled_outputs));
        qir_emit(c, qir_inst(QOP_TLB_COLOR_WRITE, c->undef,
                             t, c->undef));
}

static void
emit_scaled_viewport_write(struct tgsi_to_qir *trans)
{
        struct qcompile *c = trans->c;
        struct qreg xy[2], xyi[2];

        for (int i = 0; i < 2; i++) {
                trans->uniform_contents[trans->num_uniforms] =
                        QUNIFORM_VIEWPORT_X_SCALE + i;
                struct qreg scale = { QFILE_UNIF, trans->num_uniforms++ };

                xy[i] = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_FMUL, xy[i],
                                     trans->outputs[i], scale));
                xyi[i] = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_FTOI, xyi[i], xy[i], c->undef));
        }

        struct qreg packed_xy = qir_get_temp(c);
        qir_emit(c, qir_inst(QOP_PACK_SCALED, packed_xy, xyi[0], xyi[1]));

        qir_emit(c, qir_inst(QOP_VPM_WRITE, c->undef, packed_xy,
                             c->undef));
}

static void
emit_zs_write(struct tgsi_to_qir *trans)
{
        struct qcompile *c = trans->c;

        /* XXX: rescale */
        qir_emit(c, qir_inst(QOP_VPM_WRITE, c->undef,
                             trans->outputs[2], c->undef));
}

static void
emit_1_wc_write(struct tgsi_to_qir *trans)
{
        struct qcompile *c = trans->c;

        /* XXX: RCP */
        qir_emit(c, qir_inst(QOP_VPM_WRITE, c->undef,
                             trans->outputs[3], c->undef));
}

static void
emit_vert_end(struct tgsi_to_qir *trans)
{
        struct qcompile *c = trans->c;

        emit_scaled_viewport_write(trans);
        emit_zs_write(trans);
        emit_1_wc_write(trans);

        for (int i = 4; i < trans->num_outputs; i++) {
                qir_emit(c, qir_inst(QOP_VPM_WRITE, c->undef,
                                     trans->outputs[i], c->undef));
        }
}

static void
emit_coord_end(struct tgsi_to_qir *trans)
{
        struct qcompile *c = trans->c;

        for (int i = 0; i < 4; i++) {
                trans->inputs[i] = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_VPM_WRITE, c->undef,
                                     trans->outputs[i], c->undef));
        }

        emit_scaled_viewport_write(trans);
        emit_zs_write(trans);
        emit_1_wc_write(trans);
}

static struct tgsi_to_qir *
vc4_shader_tgsi_to_qir(struct vc4_compiled_shader *shader, enum qstage stage,
                       struct vc4_key *key)
{
        struct tgsi_to_qir *trans = CALLOC_STRUCT(tgsi_to_qir);
        struct qcompile *c;
        int ret;

        c = qir_compile_init();
        c->stage = stage;

        memset(trans, 0, sizeof(*trans));
        /* XXX sizing */
        trans->temps = calloc(sizeof(struct qreg), 1024);
        trans->inputs = calloc(sizeof(struct qreg), 8 * 4);
        trans->outputs = calloc(sizeof(struct qreg), 1024);
        trans->uniforms = calloc(sizeof(struct qreg), 1024);
        trans->consts = calloc(sizeof(struct qreg), 1024);

        trans->uniform_data = calloc(sizeof(uint32_t), 1024);
        trans->uniform_contents = calloc(sizeof(enum quniform_contents), 1024);

        trans->shader_state = key->shader_state;
        trans->c = c;
        ret = tgsi_parse_init(&trans->parser, trans->shader_state->base.tokens);
        assert(ret == TGSI_PARSE_OK);

        if (vc4_debug & VC4_DEBUG_TGSI) {
                fprintf(stderr, "TGSI:\n");
                tgsi_dump(trans->shader_state->base.tokens, 0);
        }

        switch (stage) {
        case QSTAGE_FRAG:
                trans->fs_key = (struct vc4_fs_key *)key;
                break;
        case QSTAGE_VERT:
                trans->vs_key = (struct vc4_vs_key *)key;
                break;
        case QSTAGE_COORD:
                trans->vs_key = (struct vc4_vs_key *)key;
                break;
        }

        while (!tgsi_parse_end_of_tokens(&trans->parser)) {
                tgsi_parse_token(&trans->parser);

                switch (trans->parser.FullToken.Token.Type) {
                case TGSI_TOKEN_TYPE_DECLARATION:
                        emit_tgsi_declaration(trans,
                                              &trans->parser.FullToken.FullDeclaration);
                        break;

                case TGSI_TOKEN_TYPE_INSTRUCTION:
                        emit_tgsi_instruction(trans,
                                              &trans->parser.FullToken.FullInstruction);
                        break;

                case TGSI_TOKEN_TYPE_IMMEDIATE:
                        parse_tgsi_immediate(trans,
                                             &trans->parser.FullToken.FullImmediate);
                        break;
                }
        }

        switch (stage) {
        case QSTAGE_FRAG:
                emit_frag_end(trans);
                break;
        case QSTAGE_VERT:
                emit_vert_end(trans);
                break;
        case QSTAGE_COORD:
                emit_coord_end(trans);
                break;
        }

        tgsi_parse_free(&trans->parser);
        free(trans->temps);

        qir_optimize(c);

        if (vc4_debug & VC4_DEBUG_QIR) {
                fprintf(stderr, "QIR:\n");
                qir_dump(c);
        }
        vc4_generate_code(c);

        if (vc4_debug & VC4_DEBUG_SHADERDB) {
                fprintf(stderr, "SHADER-DB: %s: %d instructions\n",
                        qir_get_stage_name(c->stage), c->qpu_inst_count);
                fprintf(stderr, "SHADER-DB: %s: %d uniforms\n",
                        qir_get_stage_name(c->stage), trans->num_uniforms);
        }

        return trans;
}

static void *
vc4_shader_state_create(struct pipe_context *pctx,
                        const struct pipe_shader_state *cso)
{
        struct vc4_shader_state *so = CALLOC_STRUCT(vc4_shader_state);
        if (!so)
                return NULL;

        so->base.tokens = tgsi_dup_tokens(cso->tokens);

        return so;
}

static void
copy_uniform_state_to_shader(struct vc4_compiled_shader *shader,
                             int shader_index,
                             struct tgsi_to_qir *trans)
{
        int count = trans->num_uniforms;
        struct vc4_shader_uniform_info *uinfo = &shader->uniforms[shader_index];

        uinfo->count = count;
        uinfo->data = malloc(count * sizeof(*uinfo->data));
        memcpy(uinfo->data, trans->uniform_data,
               count * sizeof(*uinfo->data));
        uinfo->contents = malloc(count * sizeof(*uinfo->contents));
        memcpy(uinfo->contents, trans->uniform_contents,
               count * sizeof(*uinfo->contents));
}

static void
vc4_fs_compile(struct vc4_context *vc4, struct vc4_compiled_shader *shader,
               struct vc4_fs_key *key)
{
        struct tgsi_to_qir *trans = vc4_shader_tgsi_to_qir(shader, QSTAGE_FRAG,
                                                           &key->base);
        shader->num_inputs = trans->c->num_inputs;
        copy_uniform_state_to_shader(shader, 0, trans);
        shader->bo = vc4_bo_alloc_mem(vc4->screen, trans->c->qpu_insts,
                                      trans->c->qpu_inst_count * sizeof(uint64_t),
                                      "fs_code");

        qir_compile_destroy(trans->c);
        free(trans);
}

static void
vc4_vs_compile(struct vc4_context *vc4, struct vc4_compiled_shader *shader,
               struct vc4_vs_key *key)
{
        struct tgsi_to_qir *vs_trans = vc4_shader_tgsi_to_qir(shader,
                                                              QSTAGE_VERT,
                                                              &key->base);
        copy_uniform_state_to_shader(shader, 0, vs_trans);

        struct tgsi_to_qir *cs_trans = vc4_shader_tgsi_to_qir(shader,
                                                              QSTAGE_COORD,
                                                              &key->base);
        copy_uniform_state_to_shader(shader, 1, cs_trans);

        uint32_t vs_size = vs_trans->c->qpu_inst_count * sizeof(uint64_t);
        uint32_t cs_size = cs_trans->c->qpu_inst_count * sizeof(uint64_t);
        shader->coord_shader_offset = vs_size; /* XXX: alignment? */
        shader->bo = vc4_bo_alloc(vc4->screen,
                                  shader->coord_shader_offset + cs_size,
                                  "vs_code");

        void *map = vc4_bo_map(shader->bo);
        memcpy(map, vs_trans->c->qpu_insts, vs_size);
        memcpy(map + shader->coord_shader_offset,
               cs_trans->c->qpu_insts, cs_size);

        qir_compile_destroy(vs_trans->c);
        qir_compile_destroy(cs_trans->c);
}

static void
vc4_update_compiled_fs(struct vc4_context *vc4)
{
        struct vc4_fs_key local_key;
        struct vc4_fs_key *key = &local_key;

        memset(key, 0, sizeof(*key));
        key->base.shader_state = vc4->prog.bind_fs;

        if (vc4->framebuffer.cbufs[0])
                key->color_format = vc4->framebuffer.cbufs[0]->format;

        vc4->prog.fs = util_hash_table_get(vc4->fs_cache, key);
        if (vc4->prog.fs)
                return;

        key = malloc(sizeof(*key));
        memcpy(key, &local_key, sizeof(*key));

        struct vc4_compiled_shader *shader = CALLOC_STRUCT(vc4_compiled_shader);
        vc4_fs_compile(vc4, shader, key);
        util_hash_table_set(vc4->fs_cache, key, shader);

        vc4->prog.fs = shader;
}

static void
vc4_update_compiled_vs(struct vc4_context *vc4)
{
        struct vc4_vs_key local_key;
        struct vc4_vs_key *key = &local_key;

        memset(key, 0, sizeof(*key));
        key->base.shader_state = vc4->prog.bind_vs;

        vc4->prog.vs = util_hash_table_get(vc4->vs_cache, key);
        if (vc4->prog.vs)
                return;

        key = malloc(sizeof(*key));
        memcpy(key, &local_key, sizeof(*key));

        struct vc4_compiled_shader *shader = CALLOC_STRUCT(vc4_compiled_shader);
        vc4_vs_compile(vc4, shader, key);
        util_hash_table_set(vc4->vs_cache, key, shader);

        vc4->prog.vs = shader;
}

void
vc4_update_compiled_shaders(struct vc4_context *vc4)
{
        vc4_update_compiled_fs(vc4);
        vc4_update_compiled_vs(vc4);
}

static unsigned
fs_cache_hash(void *key)
{
        return util_hash_crc32(key, sizeof(struct vc4_fs_key));
}

static unsigned
vs_cache_hash(void *key)
{
        return util_hash_crc32(key, sizeof(struct vc4_vs_key));
}

static int
fs_cache_compare(void *key1, void *key2)
{
        return memcmp(key1, key2, sizeof(struct vc4_fs_key));
}

static int
vs_cache_compare(void *key1, void *key2)
{
        return memcmp(key1, key2, sizeof(struct vc4_vs_key));
}

struct delete_state {
        struct vc4_context *vc4;
        struct vc4_shader_state *shader_state;
};

static enum pipe_error
fs_delete_from_cache(void *in_key, void *in_value, void *data)
{
        struct delete_state *del = data;
        struct vc4_fs_key *key = in_key;
        struct vc4_compiled_shader *shader = in_value;

        if (key->base.shader_state == data) {
                util_hash_table_remove(del->vc4->fs_cache, key);
                vc4_bo_unreference(&shader->bo);
                free(shader);
        }

        return 0;
}

static enum pipe_error
vs_delete_from_cache(void *in_key, void *in_value, void *data)
{
        struct delete_state *del = data;
        struct vc4_vs_key *key = in_key;
        struct vc4_compiled_shader *shader = in_value;

        if (key->base.shader_state == data) {
                util_hash_table_remove(del->vc4->vs_cache, key);
                vc4_bo_unreference(&shader->bo);
                free(shader);
        }

        return 0;
}

static void
vc4_shader_state_delete(struct pipe_context *pctx, void *hwcso)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        struct vc4_shader_state *so = hwcso;
        struct delete_state del;

        del.vc4 = vc4;
        del.shader_state = so;
        util_hash_table_foreach(vc4->fs_cache, fs_delete_from_cache, &del);
        util_hash_table_foreach(vc4->vs_cache, vs_delete_from_cache, &del);

        free((void *)so->base.tokens);
        free(so);
}

static uint32_t translate_wrap(uint32_t p_wrap)
{
        switch (p_wrap) {
        case PIPE_TEX_WRAP_REPEAT:
                return 0;
        case PIPE_TEX_WRAP_CLAMP:
        case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
                return 1;
        case PIPE_TEX_WRAP_MIRROR_REPEAT:
                return 2;
        case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
                return 3;
        default:
                fprintf(stderr, "Unknown wrap mode %d\n", p_wrap);
                assert(!"not reached");
                return 0;
        }
}

static uint32_t
get_texture_p0(struct vc4_texture_stateobj *texstate,
               uint32_t tex_and_sampler)
{
        uint32_t texi = (tex_and_sampler >> 0) & 0xff;
        struct pipe_sampler_view *texture = texstate->textures[texi];
        struct vc4_resource *rsc = vc4_resource(texture->texture);

        return (texture->u.tex.last_level |
                simpenrose_hw_addr(rsc->bo->map) /* XXX */
                /* XXX: data type */);
}

static uint32_t
get_texture_p1(struct vc4_texture_stateobj *texstate,
               uint32_t tex_and_sampler)
{
        uint32_t texi = (tex_and_sampler >> 0) & 0xff;
        uint32_t sampi = (tex_and_sampler >> 8) & 0xff;
        struct pipe_sampler_view *texture = texstate->textures[texi];
        struct pipe_sampler_state *sampler = texstate->samplers[sampi];
        static const uint32_t mipfilter_map[] = {
                [PIPE_TEX_MIPFILTER_NEAREST] = 2,
                [PIPE_TEX_MIPFILTER_LINEAR] = 4,
                [PIPE_TEX_MIPFILTER_NONE] = 0
        };
        static const uint32_t imgfilter_map[] = {
                [PIPE_TEX_FILTER_NEAREST] = 1,
                [PIPE_TEX_FILTER_LINEAR] = 0,
        };

        return ((1 << 31) /* XXX: data type */|
                (texture->texture->height0 << 20) |
                (texture->texture->width0 << 8) |
                (imgfilter_map[sampler->mag_img_filter] << 7) |
                ((imgfilter_map[sampler->min_img_filter] +
                  mipfilter_map[sampler->min_mip_filter]) << 4) |
                (translate_wrap(sampler->wrap_t) << 2) |
                (translate_wrap(sampler->wrap_s) << 0));
}

static uint32_t
get_texrect_scale(struct vc4_texture_stateobj *texstate,
                  enum quniform_contents contents,
                  uint32_t data)
{
        struct pipe_sampler_view *texture = texstate->textures[data];
        uint32_t dim;

        if (contents == QUNIFORM_TEXRECT_SCALE_X)
                dim = texture->texture->width0;
        else
                dim = texture->texture->height0;

        return fui(1.0f / dim);
}

void
vc4_get_uniform_bo(struct vc4_context *vc4, struct vc4_compiled_shader *shader,
                   struct vc4_constbuf_stateobj *cb,
                   struct vc4_texture_stateobj *texstate,
                   int shader_index, struct vc4_bo **out_bo,
                   uint32_t *out_offset)
{
        struct vc4_shader_uniform_info *uinfo = &shader->uniforms[shader_index];
        struct vc4_bo *ubo = vc4_bo_alloc(vc4->screen, uinfo->count * 4, "ubo");
        uint32_t *map = vc4_bo_map(ubo);

        for (int i = 0; i < uinfo->count; i++) {

                switch (uinfo->contents[i]) {
                case QUNIFORM_CONSTANT:
                        map[i] = uinfo->data[i];
                        break;
                case QUNIFORM_UNIFORM:
                        map[i] = ((uint32_t *)cb->cb[0].user_buffer)[uinfo->data[i]];
                        break;
                case QUNIFORM_VIEWPORT_X_SCALE:
                        map[i] = fui(vc4->framebuffer.width * 16.0f / 2.0f);
                        break;
                case QUNIFORM_VIEWPORT_Y_SCALE:
                        map[i] = fui(vc4->framebuffer.height * -16.0f / 2.0f);
                        break;

                case QUNIFORM_TEXTURE_CONFIG_P0:
                        map[i] = get_texture_p0(texstate, uinfo->data[i]);
                        break;

                case QUNIFORM_TEXTURE_CONFIG_P1:
                        map[i] = get_texture_p1(texstate, uinfo->data[i]);
                        break;

                case QUNIFORM_TEXRECT_SCALE_X:
                case QUNIFORM_TEXRECT_SCALE_Y:
                        map[i] = get_texrect_scale(texstate,
                                                   uinfo->contents[i],
                                                   uinfo->data[i]);
                        break;
                }
#if 0
                fprintf(stderr, "%p/%d: %d: 0x%08x (%f)\n",
                        shader, shader_index, i, map[i], uif(map[i]));
#endif
        }

        *out_bo = ubo;
        *out_offset = 0;
}

static void
vc4_fp_state_bind(struct pipe_context *pctx, void *hwcso)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        vc4->prog.bind_fs = hwcso;
        vc4->prog.dirty |= VC4_SHADER_DIRTY_FP;
        vc4->dirty |= VC4_DIRTY_PROG;
}

static void
vc4_vp_state_bind(struct pipe_context *pctx, void *hwcso)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        vc4->prog.bind_vs = hwcso;
        vc4->prog.dirty |= VC4_SHADER_DIRTY_VP;
        vc4->dirty |= VC4_DIRTY_PROG;
}

void
vc4_program_init(struct pipe_context *pctx)
{
        struct vc4_context *vc4 = vc4_context(pctx);

        pctx->create_vs_state = vc4_shader_state_create;
        pctx->delete_vs_state = vc4_shader_state_delete;

        pctx->create_fs_state = vc4_shader_state_create;
        pctx->delete_fs_state = vc4_shader_state_delete;

        pctx->bind_fs_state = vc4_fp_state_bind;
        pctx->bind_vs_state = vc4_vp_state_bind;

        vc4->fs_cache = util_hash_table_create(fs_cache_hash, fs_cache_compare);
        vc4->vs_cache = util_hash_table_create(vs_cache_hash, vs_cache_compare);
}
