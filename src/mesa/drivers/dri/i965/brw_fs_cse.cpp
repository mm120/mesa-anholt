/*
 * Copyright © 2012 Intel Corporation
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

#include "brw_fs.h"
#include "brw_cfg.h"

/** @file brw_fs_cse.cpp
 *
 * Support for local common subexpression elimination.
 *
 * See Muchnik's Advanced Compiler Design and Implementation, section
 * 13.1 (p378).
 */

namespace {

#define AEB_GRF_HASH_SIZE 256

struct aeb {
   /* All available expressions (struct aeb_entry). */
   exec_list aeb;

   /* List of struct aeb_external_entry for entries that use a virtual GRF,
    * improving access times.
    */
   exec_list aeb_grf_hash[AEB_GRF_HASH_SIZE];
};

struct aeb_entry : public exec_node {
   /** The instruction that generates the expression value. */
   fs_inst *generator;

   /** The temporary where the value is stored. */
   fs_reg tmp;
};

struct aeb_external_entry : public exec_node {
   struct aeb_entry *entry;
};

} /* anonymous namespace */

static bool
is_expression(const fs_inst *const inst)
{
   switch (inst->opcode) {
   case BRW_OPCODE_SEL:
   case BRW_OPCODE_NOT:
   case BRW_OPCODE_AND:
   case BRW_OPCODE_OR:
   case BRW_OPCODE_XOR:
   case BRW_OPCODE_SHR:
   case BRW_OPCODE_SHL:
   case BRW_OPCODE_RSR:
   case BRW_OPCODE_RSL:
   case BRW_OPCODE_ASR:
   case BRW_OPCODE_ADD:
   case BRW_OPCODE_MUL:
   case BRW_OPCODE_FRC:
   case BRW_OPCODE_RNDU:
   case BRW_OPCODE_RNDD:
   case BRW_OPCODE_RNDE:
   case BRW_OPCODE_RNDZ:
   case BRW_OPCODE_LINE:
   case BRW_OPCODE_PLN:
   case BRW_OPCODE_MAD:
   case FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD:
   case FS_OPCODE_CINTERP:
   case FS_OPCODE_LINTERP:
      return true;
   default:
      return false;
   }
}

static bool
operands_match(fs_reg *xs, fs_reg *ys)
{
   return xs[0].equals(ys[0]) && xs[1].equals(ys[1]) && xs[2].equals(ys[2]);
}

static void
aeb_add_entry(struct aeb *aeb, struct aeb_entry *entry)
{
   aeb->aeb.push_tail(entry);

   for (int i = 0; i < 3; i++) {
      struct fs_reg *src_reg = &entry->generator->src[i];
      if (src_reg->file == GRF) {
         struct aeb_external_entry *external = ralloc(aeb, aeb_external_entry);
         external->entry = entry;
         aeb->aeb_grf_hash[src_reg->reg %
                           AEB_GRF_HASH_SIZE].push_tail(external);
      }
   }
}

static void
aeb_remove_entry(struct aeb *aeb, struct aeb_entry *entry)
{
   for (int i = 0; i < 3; i++) {
      struct fs_reg *src_reg = &entry->generator->src[i];
      if (src_reg->file == GRF) {
         foreach_list_safe(node, &aeb->aeb_grf_hash[src_reg->reg %
                                                    AEB_GRF_HASH_SIZE]) {
            struct aeb_external_entry *external =
               (struct aeb_external_entry *)node;
            if (external->entry == entry) {
               external->remove();
               ralloc_free(external);
               break;
            }
         }
      }
   }

   entry->remove();
   ralloc_free(entry);
}

bool
fs_visitor::opt_cse_local(bblock_t *block)
{
   bool progress = false;

   struct aeb *aeb = ralloc(NULL, struct aeb);
   aeb->aeb.make_empty();
   for (unsigned i = 0; i < ARRAY_SIZE(aeb->aeb_grf_hash); i++)
      aeb->aeb_grf_hash[i].make_empty();

   for (fs_inst *inst = (fs_inst *)block->start;
	inst != block->end->next;
	inst = (fs_inst *) inst->next) {

      /* Skip some cases. */
      if (is_expression(inst) && !inst->predicate && inst->mlen == 0 &&
          !inst->force_uncompressed && !inst->force_sechalf &&
          !inst->conditional_mod)
      {
	 bool found = false;

	 aeb_entry *entry;
	 foreach_list(entry_node, &aeb->aeb) {
	    entry = (aeb_entry *) entry_node;

	    /* Match current instruction's expression against those in AEB. */
	    if (inst->opcode == entry->generator->opcode &&
		inst->saturate == entry->generator->saturate &&
                inst->dst.type == entry->generator->dst.type &&
                operands_match(entry->generator->src, inst->src)) {

	       found = true;
	       progress = true;
	       break;
	    }
	 }

	 if (!found) {
	    /* Our first sighting of this expression.  Create an entry. */
	    aeb_entry *entry = ralloc(aeb, aeb_entry);
	    entry->tmp = reg_undef;
	    entry->generator = inst;
            aeb_add_entry(aeb, entry);
	 } else {
	    /* This is at least our second sighting of this expression.
	     * If we don't have a temporary already, make one.
	     */
	    bool no_existing_temp = entry->tmp.file == BAD_FILE;
	    if (no_existing_temp) {
	       entry->tmp = fs_reg(this, glsl_type::float_type);
	       entry->tmp.type = inst->dst.type;

	       fs_inst *copy = new(ralloc_parent(inst))
		  fs_inst(BRW_OPCODE_MOV, entry->generator->dst, entry->tmp);
	       entry->generator->insert_after(copy);
	       entry->generator->dst = entry->tmp;
	    }

	    /* dest <- temp */
            assert(inst->dst.type == entry->tmp.type);
	    fs_inst *copy = new(ralloc_parent(inst))
	       fs_inst(BRW_OPCODE_MOV, inst->dst, entry->tmp);
            copy->force_writemask_all = inst->force_writemask_all;
	    inst->replace_with(copy);

	    /* Appending an instruction may have changed our bblock end. */
	    if (inst == block->end) {
	       block->end = copy;
	    }

	    /* Continue iteration with copy->next */
	    inst = copy;
	 }
      }

      /* Kill all AEB entries that use the destination. */
      if (inst->dst.file == GRF) {
         foreach_list_safe(node, &aeb->aeb_grf_hash[inst->dst.reg %
                                                    AEB_GRF_HASH_SIZE]) {
            struct aeb_external_entry *external =
               (struct aeb_external_entry *)node;
            struct aeb_entry *entry = external->entry;
            for (int i = 0; i < 3; i++) {
               if (inst->overwrites_reg(entry->generator->src[i])) {
                  aeb_remove_entry(aeb, entry);
                  break;
               }
            }
         }
      } else {
         foreach_list_safe(entry_node, &aeb->aeb) {
            aeb_entry *entry = (aeb_entry *)entry_node;

            for (int i = 0; i < 3; i++) {
               if (inst->overwrites_reg(entry->generator->src[i])) {
                  aeb_remove_entry(aeb, entry);
                  break;
               }
            }
         }
      }
   }

   ralloc_free(aeb);

   if (progress)
      this->live_intervals_valid = false;

   return progress;
}

bool
fs_visitor::opt_cse()
{
   bool progress = false;

   cfg_t cfg(this);

   for (int b = 0; b < cfg.num_blocks; b++) {
      bblock_t *block = cfg.blocks[b];

      progress = opt_cse_local(block) || progress;
   }

   return progress;
}
