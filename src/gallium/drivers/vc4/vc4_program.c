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
#include "util/u_memory.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_dump.h"

#include "vc4_context.h"
#include "vc4_qpu.h"
#include "vc4_qir.h"

struct tgsi_to_qir {
        struct tgsi_parse_context parser;
        struct qcompile *c;
        struct qreg undef;
        struct qreg *temps;
        struct qreg *inputs;
        struct qreg *outputs;
        struct qreg *uniforms;
        struct qreg *consts;
        uint32_t num_consts;

        uint32_t *uniform_data;
        enum quniform_contents *uniform_contents;
        uint32_t num_uniforms;
};

static struct qreg
get_temp_for_uniform(struct tgsi_to_qir *trans, uint32_t uniform)
{
        struct qcompile *c = trans->c;
        struct qreg t = qir_get_temp(c);
        struct qreg u = { QFILE_UNIF, uniform };

        qir_emit(c, qir_inst(QOP_MOV, t, u, trans->undef));

        trans->uniforms[uniform] = t;
        return t;
}

static struct qreg
qir_uniform_ui(struct tgsi_to_qir *trans, uint32_t ui)
{
        for (int i = 0; i < trans->num_uniforms; i++) {
                if (trans->uniform_contents[i] == QUNIFORM_CONSTANT &&
                    trans->uniform_data[i] == ui)
                        return trans->uniforms[i];
        }

        trans->uniform_contents[trans->num_uniforms] = QUNIFORM_CONSTANT;
        trans->uniform_data[trans->num_uniforms] = ui;
        return get_temp_for_uniform(trans, trans->num_uniforms++);
}

static struct qreg
qir_uniform(struct tgsi_to_qir *trans, uint32_t index)
{
        for (int i = 0; i < trans->num_uniforms; i++) {
                if (trans->uniform_contents[i] == QUNIFORM_UNIFORM &&
                    trans->uniform_data[i] == index)
                        return trans->uniforms[i];
        }

        trans->uniform_contents[trans->num_uniforms] = QUNIFORM_UNIFORM;
        trans->uniform_data[trans->num_uniforms] = index;
        return get_temp_for_uniform(trans, trans->num_uniforms++);
}

static struct qreg
get_src(struct tgsi_to_qir *trans, struct tgsi_src_register *src, int i)
{
        struct qcompile *c = trans->c;
        struct qreg r = trans->undef;

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
                r = qir_uniform(trans, src->Index * 4 + s);
                break;
        case TGSI_FILE_INPUT:
                r = trans->inputs[src->Index * 4 + s];
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
        qir_emit(c, qir_inst(op, dst, src[0 * 4 + i], src[1 * 4 + i]));
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
                [TGSI_OPCODE_ABS] = { QOP_FMAXABS, tgsi_to_qir_alu },
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
                [TGSI_OPCODE_MAD] = { 0, tgsi_to_qir_mad },
                [TGSI_OPCODE_DP2] = { 0, tgsi_to_qir_dp2 },
                [TGSI_OPCODE_DP3] = { 0, tgsi_to_qir_dp3 },
                [TGSI_OPCODE_DP4] = { 0, tgsi_to_qir_dp4 },
                [TGSI_OPCODE_RCP] = { QOP_RCP, tgsi_to_qir_alu },
                [TGSI_OPCODE_RSQ] = { QOP_RSQ, tgsi_to_qir_alu },
                [TGSI_OPCODE_EX2] = { QOP_EXP2, tgsi_to_qir_alu },
                [TGSI_OPCODE_LG2] = { QOP_LOG2, tgsi_to_qir_alu },
                [TGSI_OPCODE_LIT] = { QOP_MOV, tgsi_to_qir_alu }, /* XXX */
        };
        static int asdf = 0;
        uint32_t tgsi_op = tgsi_inst->Instruction.Opcode;

        if (tgsi_op == TGSI_OPCODE_END)
                return;

        tgsi_dump_instruction(tgsi_inst, asdf++);

        if (tgsi_op > ARRAY_SIZE(op_trans) || !op_trans[tgsi_op].func) {
                fprintf(stderr, "unknown tgsi inst: ");
                tgsi_dump_instruction(tgsi_inst, asdf++);
                fprintf(stderr, "\n");
                abort();
        }

        struct qreg src_regs[12];
        for (int s = 0; s < 3; s++) {
                for (int i = 0; i < 4; i++) {
                        src_regs[4 * s + i] =
                                get_src(trans, &tgsi_inst->Src[s].Register, i);
                }
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
emit_frag_init(struct tgsi_to_qir *trans, struct vc4_shader_state *so)
{
        /* XXX: lols */
        for (int i = 0; i < 4; i++) {
                trans->inputs[i] = qir_uniform_ui(trans, fui(i / 4.0));
        }

}

static void
emit_vert_init(struct tgsi_to_qir *trans, struct vc4_shader_state *so)
{
        struct qcompile *c = trans->c;

        /* XXX: attribute type/size/count */
        for (int i = 0; i < 4; i++) {
                trans->inputs[i] = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_VPM_READ, trans->inputs[i],
                                     trans->undef, trans->undef));
        }
}

static void
emit_coord_init(struct tgsi_to_qir *trans, struct vc4_shader_state *so)
{
        struct qcompile *c = trans->c;

        /* XXX: attribute type/size/count */
        for (int i = 0; i < 4; i++) {
                trans->inputs[i] = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_VPM_READ, trans->inputs[i],
                                     trans->undef, trans->undef));
        }
}

static void
emit_frag_end(struct tgsi_to_qir *trans, struct vc4_shader_state *so)
{
        struct qcompile *c = trans->c;

        struct qreg t = qir_get_temp(c);
        qir_emit(c, qir_inst4(QOP_PACK_COLORS, t, trans->outputs));
        qir_emit(c, qir_inst(QOP_TLB_COLOR_WRITE, trans->undef,
                             t, trans->undef));
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
                qir_emit(c, qir_inst(QOP_FTOI, xyi[i], xy[i], trans->undef));
        }

        struct qreg packed_xy = qir_get_temp(c);
        qir_emit(c, qir_inst(QOP_PACK_SCALED, packed_xy, xyi[0], xyi[1]));

        qir_emit(c, qir_inst(QOP_VPM_WRITE, trans->undef, packed_xy,
                             trans->undef));
}

static void
emit_zs_write(struct tgsi_to_qir *trans)
{
        struct qcompile *c = trans->c;

        /* XXX: rescale */
        qir_emit(c, qir_inst(QOP_VPM_WRITE, trans->undef,
                             trans->outputs[2], trans->undef));
}

static void
emit_1_wc_write(struct tgsi_to_qir *trans)
{
        struct qcompile *c = trans->c;

        /* XXX: RCP */
        qir_emit(c, qir_inst(QOP_VPM_WRITE, trans->undef,
                             trans->outputs[3], trans->undef));
}

static void
emit_vert_end(struct tgsi_to_qir *trans, struct vc4_shader_state *so)
{
        emit_scaled_viewport_write(trans);
        emit_zs_write(trans);
        emit_1_wc_write(trans);
        /* XXX: write varyings */
}

static void
emit_coord_end(struct tgsi_to_qir *trans, struct vc4_shader_state *so)
{
        struct qcompile *c = trans->c;

        for (int i = 0; i < 4; i++) {
                trans->inputs[i] = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_VPM_WRITE, trans->undef,
                                     trans->outputs[i], trans->undef));
        }

        emit_scaled_viewport_write(trans);
        emit_zs_write(trans);
        emit_1_wc_write(trans);
}

static struct tgsi_to_qir *
vc4_shader_tgsi_to_qir(struct vc4_shader_state *so, enum qstage stage)
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

        trans->c = c;
        ret = tgsi_parse_init(&trans->parser, so->base.tokens);
        assert(ret == TGSI_PARSE_OK);

        fprintf(stderr, "TGSI:\n");
        tgsi_dump(so->base.tokens, 0);

        switch (stage) {
        case QSTAGE_FRAG:
                emit_frag_init(trans, so);
                break;
        case QSTAGE_VERT:
                emit_vert_init(trans, so);
                break;
        case QSTAGE_COORD:
                emit_coord_init(trans, so);
                break;
        }

        while (!tgsi_parse_end_of_tokens(&trans->parser)) {
                tgsi_parse_token(&trans->parser);

                switch (trans->parser.FullToken.Token.Type) {
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
                emit_frag_end(trans, so);
                break;
        case QSTAGE_VERT:
                emit_vert_end(trans, so);
                break;
        case QSTAGE_COORD:
                emit_coord_end(trans, so);
                break;
        }

        qir_dump(c);

        tgsi_parse_free(&trans->parser);
        free(trans->temps);

        vc4_generate_code(c);

        return trans;
}

static struct vc4_shader_state *
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
copy_uniform_state_to_shader(struct vc4_shader_state *so,
                             int shader_index,
                             struct tgsi_to_qir *trans)
{
        int count = trans->num_uniforms;
        struct vc4_shader_uniform_info *uinfo = &so->uniforms[shader_index];

        uinfo->count = count;
        uinfo->data = malloc(count * sizeof(*uinfo->data));
        memcpy(uinfo->data, trans->uniform_data,
               count * sizeof(*uinfo->data));
        uinfo->contents = malloc(count * sizeof(*uinfo->contents));
        memcpy(uinfo->contents, trans->uniform_contents,
               count * sizeof(*uinfo->contents));
}

static void *
vc4_fs_state_create(struct pipe_context *pctx,
                    const struct pipe_shader_state *cso)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        struct vc4_shader_state *so = vc4_shader_state_create(pctx, cso);
        if (!so)
                return NULL;

        struct tgsi_to_qir *trans = vc4_shader_tgsi_to_qir(so, QSTAGE_FRAG);
        copy_uniform_state_to_shader(so, 0, trans);

        so->bo = vc4_bo_alloc_mem(vc4->screen, trans->c->qpu_insts,
                                  trans->c->num_qpu_insts * sizeof(uint64_t),
                                  "fs_code");

        qir_compile_destroy(trans->c);
        free(trans);

        return so;
}

static void *
vc4_vs_state_create(struct pipe_context *pctx,
                    const struct pipe_shader_state *cso)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        struct vc4_shader_state *so = vc4_shader_state_create(pctx, cso);
        if (!so)
                return NULL;

        struct tgsi_to_qir *vs_trans = vc4_shader_tgsi_to_qir(so, QSTAGE_VERT);
        copy_uniform_state_to_shader(so, 0, vs_trans);

        struct tgsi_to_qir *cs_trans = vc4_shader_tgsi_to_qir(so, QSTAGE_COORD);
        copy_uniform_state_to_shader(so, 1, cs_trans);

        uint32_t vs_size = vs_trans->c->num_qpu_insts * sizeof(uint64_t);
        uint32_t cs_size = cs_trans->c->num_qpu_insts * sizeof(uint64_t);
        so->coord_shader_offset = vs_size; /* XXX: alignment? */
        so->bo = vc4_bo_alloc(vc4->screen,
                              so->coord_shader_offset + cs_size,
                              "vs_code");

        void *map = vc4_bo_map(so->bo);
        memcpy(map, vs_trans->c->qpu_insts, vs_size);
        memcpy(map + so->coord_shader_offset, cs_trans->c->qpu_insts, cs_size);

        qir_compile_destroy(vs_trans->c);
        qir_compile_destroy(cs_trans->c);

        return so;
}

static void
vc4_shader_state_delete(struct pipe_context *pctx, void *hwcso)
{
        struct pipe_shader_state *so = hwcso;

        free((void *)so->tokens);
        free(so);
}

void
vc4_get_uniform_bo(struct vc4_context *vc4, struct vc4_shader_state *shader,
                   struct vc4_constbuf_stateobj *cb,
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
                }
#if 1
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
        vc4->prog.fs = hwcso;
        vc4->prog.dirty |= VC4_SHADER_DIRTY_FP;
        vc4->dirty |= VC4_DIRTY_PROG;
}

static void
vc4_vp_state_bind(struct pipe_context *pctx, void *hwcso)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        vc4->prog.vs = hwcso;
        vc4->prog.dirty |= VC4_SHADER_DIRTY_VP;
        vc4->dirty |= VC4_DIRTY_PROG;
}

void
vc4_program_init(struct pipe_context *pctx)
{
        pctx->create_vs_state = vc4_vs_state_create;
        pctx->delete_vs_state = vc4_shader_state_delete;

        pctx->create_fs_state = vc4_fs_state_create;
        pctx->delete_fs_state = vc4_shader_state_delete;

        pctx->bind_fs_state = vc4_fp_state_bind;
        pctx->bind_vs_state = vc4_vp_state_bind;
}
