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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file opt_dead_code.cpp
 *
 * Eliminates dead assignments and variable declarations from the code.
 */

#include "ir.h"
#include "ir_visitor.h"
#include "ir_builder.h"
#include "live_variables.h"
#include "glsl_types.h"
#include "main/hash_table.h"
#include "program/prog_instruction.h"

using namespace ir_builder;

static bool debug = false;

static bool
lhs_totally_dead(struct glsl_live_variables *live_vars,
                 BITSET_WORD *live, ir_assignment *ir)
{
   ir_variable *var = ir->lhs->variable_referenced();

   struct hash_entry *hash_entry =
      _mesa_hash_table_search(live_vars->var_ht, _mesa_hash_pointer(var), var);
   struct var_entry *var_entry = (struct var_entry *)hash_entry->data;

   for (unsigned i = 0; i < var_entry->components; i++) {
      if (BITSET_TEST(live, var_entry->offset + i))
         return false;
   }

   return true;
}

static bool
kill_dead_channels(struct glsl_live_variables *live_vars,
                   BITSET_WORD *live,
                   ir_instruction *ir)
{
   ir_assignment *assign = ir->as_assignment();
   if (!assign)
      return false;

   if (lhs_totally_dead(live_vars, live, assign)) {
      if (debug) {
         fprintf(stderr, "Removing dead IR: ");
         ir->fprint(stderr);
         fprintf(stderr, "\n");
      }

      ir->remove();
      return true;
   }

   ir_dereference_variable *lhs_deref = assign->lhs->as_dereference_variable();
   if (!lhs_deref)
      return false;
   ir_variable *lhs_var = lhs_deref->var;

   struct hash_entry *hash_entry =
      _mesa_hash_table_search(live_vars->var_ht,
                              _mesa_hash_pointer(lhs_var), lhs_var);
   struct var_entry *var_entry = (struct var_entry *)hash_entry->data;

   if (lhs_deref->type->is_scalar() || lhs_deref->type->is_vector()) {
      unsigned writemask_used = 0;

      for (unsigned i = 0; i < lhs_deref->type->vector_elements; i++) {
         unsigned var_index = var_entry->offset + i;
         if (assign->write_mask & (1 << i) && BITSET_TEST(live, var_index)) {
            writemask_used |= 1 << i;
         }
      }

      if (writemask_used == assign->write_mask)
         return false;

      /* We've got some unused channels in the LHS, so we want to clear bits
       * from the write mask and pull just the used channels out of the RHS
       * using a swizzle.
       */
      unsigned swiz[4] = {0};
      unsigned next_lhs_channel = 0;
      unsigned next_rhs_channel = 0;
      for (unsigned i = 0; i < 4; i++) {
         if (assign->write_mask & (1 << i)) {
            if (writemask_used & (1 << i))
               swiz[next_lhs_channel++] = next_rhs_channel;
            next_rhs_channel++;
         }
      }

      /* This may not be caught by lhs_totally_dead() above in the case of a
       * limited writemask.
       */
      if (next_lhs_channel == 0) {
         if (debug) {
            fprintf(stderr, "Removing dead IR: ");
            ir->fprint(stderr);
            fprintf(stderr, "\n");
         }

         ir->remove();
         return true;
      }

      if (debug) {
         fprintf(stderr, "Rewriting partially dead IR:\n    ");
         ir->fprint(stderr);
         fprintf(stderr, "\n");
      }
      assign->rhs = swizzle(assign->rhs,
                            MAKE_SWIZZLE4(swiz[0], swiz[1], swiz[2], swiz[3]),
                            next_lhs_channel);
      assign->write_mask = writemask_used;
      if (debug) {
         fprintf(stderr, "to:\n    ");
         ir->fprint(stderr);
         fprintf(stderr, "\n");
      }

      return true;
   }

   return false;
}

namespace {
class kill_variables_visitor : public ir_hierarchical_visitor {
public:
   kill_variables_visitor(struct glsl_live_variables *live_vars,
                          bool linked,
                          bool uniform_locations_assigned);
   virtual ir_visitor_status visit(ir_variable *ir);

   struct glsl_live_variables *live_vars;
   bool linked;
   bool uniform_locations_assigned;
   bool progress;
};

} /* anonymous namespace */

kill_variables_visitor::kill_variables_visitor(struct glsl_live_variables *live_vars,
                                               bool linked,
                                               bool uniform_locations_assigned)
   : live_vars(live_vars), linked(linked),
     uniform_locations_assigned(uniform_locations_assigned)
{
   progress = false;
}

ir_visitor_status
kill_variables_visitor::visit(ir_variable *ir)
{
   switch (ir->data.mode) {
   case ir_var_function_in:
   case ir_var_function_out:
   case ir_var_function_inout:
      /* No deleting variables from function signatures. */
      return visit_continue;
      break;

   case ir_var_uniform:
      /* uniform initializers are precious, and could get used by another
       * stage.  Also, once uniform locations have been assigned, the
       * declaration cannot be deleted.
       */
      if (uniform_locations_assigned || ir->constant_value)
         return visit_continue;
      break;

   default:
      break;
   }

   struct hash_entry *he =
      _mesa_hash_table_search(live_vars->var_ht,
                              _mesa_hash_pointer(ir), ir);

   /* If the variable was never put in the variable hash, then there was no
    * dereference of that variable, so it's dead to us.
    */
   if (!he) {
      if (debug) {
         fprintf(stderr, "Removing dead variable declaration: ");
         ir->fprint(stderr);
         fprintf(stderr, "\n");
      }

      ir->remove();
      progress = true;
   }

   return visit_continue;
}

static bool
kill_dead_variables(struct glsl_live_variables *live_vars,
                    exec_list *instructions, bool linked,
                    bool uniform_locations_assigned)
{
   kill_variables_visitor kv(live_vars, linked, uniform_locations_assigned);
   visit_list_elements(&kv, instructions);
   return kv.progress;
}

static bool
actually_do_dead_code(exec_list *instructions, bool linked,
                      bool uniform_locations_assigned)
{
   bool progress = false;

   struct glsl_live_variables *live_vars;

   live_vars = glsl_get_live_variables(instructions);
   if (!live_vars)
      return false;

   BITSET_WORD *live = ralloc_array(live_vars, BITSET_WORD,
                                    live_vars->bitset_words);
   if (!live) {
      ralloc_free(live_vars);
      return false;
   }

   for (unsigned b = 0; b < live_vars->num_blocks; b++) {
      struct bb_entry *bb = live_vars->block[b].bb_entry;

      if (!bb->first)
         continue;

      memcpy(live, live_vars->block[b].liveout,
             sizeof(BITSET_WORD) * live_vars->bitset_words);

      ir_instruction *ir, *prev;
      ir_instruction *first_prev = (ir_instruction *)bb->first->prev;
      for (ir = bb->last, prev = (ir_instruction *)ir->prev;
           ir != first_prev;
           ir = prev, prev = (ir_instruction *)ir->prev) {

         if (kill_dead_channels(live_vars, live, ir)) {
            progress = true;

            /* If the instruction was removed from the instruction stream, it
             * was entirely dead and we don't need to consider its uses or
             * defs.
             */
            if (ir->next == NULL)
               continue;
         }

         glsl_live_variables_process_defs(live_vars, NULL, live, ir);
         glsl_live_variables_process_uses(live_vars, NULL, live, ir);
      }
   }

   progress = kill_dead_variables(live_vars, instructions, linked,
                                  uniform_locations_assigned) || progress;

   ralloc_free(live_vars);

   return progress;
}

/**
 * Do a dead code pass over instructions and everything that instructions
 * references.
 *
 * Note that this will remove assignments to globals, so it is not suitable
 * for usage on an unlinked instruction stream.
 */
bool
do_dead_code(exec_list *instructions, bool uniform_locations_assigned)
{
   return actually_do_dead_code(instructions, true, uniform_locations_assigned);
}

/**
 * Does a dead code pass on the functions present in the instruction stream.
 *
 * This is suitable for use while the program is not linked, as it will
 * ignore variable declarations (and the assignments to them) for variables
 * with global scope.
 */
bool
do_dead_code_unlinked(exec_list *instructions)
{
   return actually_do_dead_code(instructions, true, true /* XXX */);
}
