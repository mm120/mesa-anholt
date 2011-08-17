/*
 * Copyright © 2010 Intel Corporation
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

#pragma once

extern "C" {
#include <stdint.h>
#include "main/macros.h"
#include "main/mtypes.h"
#include "brw_defines.h"
#include "brw_eu.h"
#include "brw_context.h"
}

#include "../glsl/glsl_types.h"
#include "../glsl/list.h"
#include "../glsl/ir.h"
#include "../glsl/ir_visitor.h"

namespace brw {

enum register_file {
   ARF = BRW_ARCHITECTURE_REGISTER_FILE,
   GRF = BRW_GENERAL_REGISTER_FILE,
   MRF = BRW_MESSAGE_REGISTER_FILE,
   IMM = BRW_IMMEDIATE_VALUE,
   BRW_REG,
   ATTR,
   UNIFORM, /* prog_data->params[reg] */
   BAD_FILE
};

struct reg
{
public:
   enum register_file file;

   /**
    * Register number.
    *
    * For GRF, this is an index into virtual_grf_*[] until register
    * allocation.  After register allocation, it becomes the hardware
    * GRF number.  For MRF, this is the MRF number.  For IMM and
    * BRW_REG, this is unused.
    */
   int reg;

   /**
    * For virtual GRF registers, this is a hardware register offset
    * from the start of the register block (for example, a constant
    * index in an array access).
    */
   int reg_offset;

   /** Register type.  BRW_REGISTER_TYPE_* */
   int type;

   bool sechalf;
   struct brw_reg fixed_hw_reg;
   int smear; /* -1, or a channel of the reg to smear to all channels. */

   /** Value for file == BRW_IMMMEDIATE_FILE */
   union {
      int32_t i;
      uint32_t u;
      float f;
   } imm;
};

struct instruction : public exec_node
{
public:
   enum opcode opcode;

   bool saturate;
   int conditional_mod; /**< BRW_CONDITIONAL_* */
   int mlen; /**< SEND message length */
   int base_mrf; /**< First MRF in the SEND message, if mlen is nonzero. */
   int sampler;
   bool shadow_compare;
   bool eot;
   bool header_present;
   bool predicate_inverse;

   /** @{
    * Annotation for the generated IR.  One of the two can be set.
    */
   ir_instruction *ir;
   const char *annotation;
   /** @} */
};

class compiler : public ir_visitor
{
public:
   ~compiler();
   struct intel_context *intel;
   struct brw_context *brw;
   struct brw_compile *p;
   const struct gl_fragment_program *fp;
   struct gl_context *ctx;
   struct brw_shader *shader;
   struct gl_shader_program *prog;
   exec_list instructions;
   void *mem_ctx;

   void init(struct brw_compile *p, struct gl_shader_program *prog,
	     struct brw_shader *shader);
};

int brw_type_for_base_type(const struct glsl_type *type);
uint32_t brw_conditional_for_comparison(unsigned int op);
uint32_t brw_math_function(enum opcode op);

} /* namespace brw */

using namespace brw;
