/*
 * Copyright © 2014 Intel Corporation
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

#include "live_variables.h"
#include "ir_hierarchical_visitor.h"
#include "main/macros.h"
#include "main/hash_table.h"

static bool debug = false;

namespace {
/**
 * Class to walk over a collection of IR, find the set of variables
 * dereferenced, and assign them an offset in the use/def array (with an entry
 * in the array per component in the variable).
 */
class ir_var_ht_initializer : public ir_hierarchical_visitor
{
public:
   ir_var_ht_initializer(struct hash_table *ht);
   ir_visitor_status visit(ir_dereference_variable *ir);

   struct hash_table *ht;
   unsigned next_offset;
   bool failed;
};

} /* anonymous namespace */

static struct var_entry *
get_var_entry(struct glsl_live_variables *live_vars, ir_variable *var)
{
    struct hash_entry *hash_entry =
       _mesa_hash_table_search(live_vars->var_ht,
                               _mesa_hash_pointer(var), var);
    if (!hash_entry)
       return NULL;
    return (struct var_entry *)hash_entry->data;
}

ir_var_ht_initializer::ir_var_ht_initializer(struct hash_table *ht)
   : ht(ht)
{
   next_offset = 0;
   failed = false;
}

ir_visitor_status
ir_var_ht_initializer::visit(ir_dereference_variable *ir)
{
   ir_variable *var = ir->var;
   uint32_t var_hash = _mesa_hash_pointer(var);
   if (_mesa_hash_table_search(ht, var_hash, var))
      return visit_continue;

   struct var_entry *entry = rzalloc(ht, struct var_entry);
   if (!entry) {
      failed = true;
      return visit_stop;
   }

   entry->var = var;
   entry->offset = next_offset;

   if (var->type->is_unsized_array()) {
      entry->components = (MAX2(1, var->data.max_array_access) *
                           var->type->fields.array->component_slots());
   } else {
      entry->components = var->type->component_slots();
   }

   next_offset += entry->components;

   if (debug) {
      fprintf(stderr, "offset %d..%d: var %p:%s\n",
              entry->offset,
              entry->offset + entry->components - 1,
              var, var->name);
   }

   _mesa_hash_table_insert(ht, var_hash, var, entry);

   return visit_continue;
}

static bool
make_var_ht(struct glsl_live_variables *live_vars, exec_list *instructions)
{
   struct hash_table *ht = _mesa_hash_table_create(live_vars,
                                                   _mesa_key_pointer_equal);
   if (!ht)
      return false;

   ir_var_ht_initializer ht_init(ht);
   visit_list_elements(&ht_init, instructions);

   if (ht_init.failed)
      return false;

   live_vars->bitset_words = BITSET_WORDS(ht_init.next_offset);
   live_vars->var_ht = ht;

   return true;
}

void
bb_end(struct glsl_live_variables *live_vars, struct bb_entry *bb)
{
   bb->index = live_vars->num_blocks++;

   /* XXX */
   live_vars->block = reralloc(live_vars,
                               live_vars->block,
                               struct glsl_live_variables_block,
                               live_vars->num_blocks);
   live_vars->block[bb->index].bb_entry = bb;

   _mesa_hash_table_insert(live_vars->bb_ht,
                           _mesa_hash_pointer(bb->first), bb->first, bb);
}

struct ir_list_entry : public exec_node {
   ir_instruction *ir;
};

struct cfg_state {
   struct glsl_live_variables *live_vars;
   /** First basic block inside the current loop (or NULL) */
   struct bb_entry *loop_start;
   /** First basic block after the current loop (or NULL) */
   struct bb_entry *loop_end;
   struct bb_entry *func_exit;

   BITSET_WORD *function_liveout;
};

static void
add_successor(struct bb_entry *parent, struct bb_entry *child)
{
   bb_entry_link *link = new(parent) bb_entry_link;
   link->bb = child;
   parent->successors.push_tail(&link->link);
}

static struct bb_entry *
add_basic_blocks(struct cfg_state *cfg,
                 struct bb_entry *bb,
                 exec_list *instructions)
{
   if (!bb)
      bb = new(cfg->live_vars) bb_entry;

   foreach_list(node, instructions) {
      ir_instruction *ir = (ir_instruction *)node;

      ir_function *f = ir->as_function();
      if (f) {
         /* We ignore the previous "basic block" we were tracking, since it
          * was just a list of ir_variables.
          */

	 foreach_list(signode, &f->signatures) {
	    ir_function_signature *sig = (ir_function_signature *) signode;

            /* Each function signature is treated as an independent control
             * flow graph -- we don't track flow control from the callsites.
             * So, there's a root node at bb_entry (with the first
             * instructions of the function), and an exit node that any return
             * statements jump to.
             *
             * Note that func_exit is the only case where a bb_entry has no
             * first/last instruction.
             */
            struct bb_entry *func_enter = new(cfg->live_vars) bb_entry;
            struct bb_entry *func_exit = new(cfg->live_vars) bb_entry;

            cfg->func_exit = func_exit;
            struct bb_entry *func_last_bb = add_basic_blocks(cfg, func_enter,
                                                             &sig->body);
            add_successor(func_last_bb, func_exit);
            bb_end(cfg->live_vars, func_exit);

            _mesa_hash_table_insert(cfg->live_vars->func_exit_ht,
                                    _mesa_hash_pointer(sig), sig, func_exit);
	 }
         cfg->func_exit = NULL;

         /* Restart bb tracking after the function. */
         bb->first = NULL;
         continue;
      }

      if (!bb->first)
         bb->first = ir;

      bb->last = ir;

      ir_if *ir_if = ir->as_if();
      if (ir_if) {
         bb_end(cfg->live_vars, bb);

         struct bb_entry *then_bb = new(cfg->live_vars) bb_entry;
         struct bb_entry *else_bb = new(cfg->live_vars) bb_entry;
         add_successor(bb, then_bb);
         add_successor(bb, else_bb);

         then_bb = add_basic_blocks(cfg, then_bb, &ir_if->then_instructions);
         else_bb = add_basic_blocks(cfg, else_bb, &ir_if->else_instructions);

         bb = new(cfg->live_vars) bb_entry;

         add_successor(then_bb, bb);
         add_successor(else_bb, bb);
      }

      ir_loop *ir_loop = ir->as_loop();
      if (ir_loop) {
         struct bb_entry *saved_loop_start = cfg->loop_start;
         struct bb_entry *saved_loop_end = cfg->loop_end;
         cfg->loop_start = new(cfg->live_vars) bb_entry;
         cfg->loop_end = new(cfg->live_vars) bb_entry;

         /* The pre-loop code falls into the start of the loop. */
         add_successor(bb, cfg->loop_start);
         bb_end(cfg->live_vars, bb);

         bb = add_basic_blocks(cfg, cfg->loop_start,
                               &ir_loop->body_instructions);

         /* The last thing inside the loop unconditionally jumps back to the
          * start.
          */
         add_successor(bb, cfg->loop_start);

         /* Execution will continue outside of the loop (which was only
          * reachable by break instructions inside the loop).
          */
         bb = cfg->loop_end;

         cfg->loop_start = saved_loop_start;
         cfg->loop_end = saved_loop_end;
      }

      switch (ir->ir_type) {
      case ir_type_return:
         add_successor(bb, cfg->func_exit);
         bb_end(cfg->live_vars, bb);
         bb = new(cfg->live_vars) bb_entry;
         break;

      case ir_type_loop_jump: {
         ir_loop_jump *lj = (ir_loop_jump *)ir;
         if (lj->mode == ir_loop_jump::jump_break) {
            add_successor(bb, cfg->loop_end);
         } else {
            assert(lj->mode == ir_loop_jump::jump_continue);
            add_successor(bb, cfg->loop_start);
         }
         bb_end(cfg->live_vars, bb);
         bb = new(cfg->live_vars) bb_entry;
         break;
      }

      default:
         break;
      }
   }

   bb_end(cfg->live_vars, bb);

   return bb;
}

static bool
make_bb_ht(struct glsl_live_variables *live_vars, exec_list *instructions)
{
   struct hash_table *ht = _mesa_hash_table_create(live_vars,
                                                   _mesa_key_pointer_equal);
   if (!ht)
      return false;

   live_vars->bb_ht = ht;

   struct cfg_state cfg_state;
   memset(&cfg_state, 0, sizeof(cfg_state));

   cfg_state.live_vars = live_vars;

   add_basic_blocks(&cfg_state, NULL, instructions);

   return true;
}

static void
process_assign_def(struct glsl_live_variables *live_vars,
                   BITSET_WORD *def,
                   BITSET_WORD *use,
                   ir_assignment *assign)
{
   /* If the assignment is conditional, then it doesn't screen off later uses
    * of the variable.
    */
   if (assign->condition)
      return;

   ir_dereference_variable *lhs_deref = assign->lhs->as_dereference_variable();
   if (!lhs_deref)
      return;
   ir_variable *lhs_var = lhs_deref->var;

   struct var_entry *var_entry = get_var_entry(live_vars, lhs_var);

   if (lhs_deref->type->is_scalar() || lhs_deref->type->is_vector()) {
      for (unsigned i = 0; i < lhs_deref->type->vector_elements; i++) {
         unsigned var_index = var_entry->offset + i;
         if (assign->write_mask & (1 << i)) {
            BITSET_CLEAR(use, var_index);
            if (def)
               BITSET_SET(def, var_index);
         }
      }
   } else {
      /* No writemasks for things bigger than vectors. */
      for (unsigned i = 0; i < var_entry->components; i++) {
         unsigned var_index = var_entry->offset + i;
         BITSET_CLEAR(use, var_index);
         if (def)
            BITSET_SET(def, var_index);
      }
   }
}

/**
 * Walks the parameter list looking for out values, and if any of those are a
 * bare dereference of a variable, marks them as defs.
 */
static void
process_call_def(struct glsl_live_variables *live_vars,
                 BITSET_WORD *def,
                 BITSET_WORD *use,
                 ir_call *call)
{
   foreach_two_lists(formal_node, &call->callee->parameters,
                     actual_node, &call->actual_parameters) {
      ir_variable *sig_param = (ir_variable *) formal_node;
      ir_rvalue *ir = (ir_rvalue *) actual_node;
      if (sig_param->data.mode != ir_var_function_out
          && sig_param->data.mode != ir_var_function_inout) {
         continue;
      }

      ir_dereference_variable *deref = ir->as_dereference_variable();
      if (!deref)
         continue;

      struct var_entry *var_entry = get_var_entry(live_vars, deref->var);

      for (unsigned i = 0; i < var_entry->components; i++) {
         unsigned var_index = var_entry->offset + i;
         BITSET_CLEAR(use, var_index);
         if (def)
            BITSET_SET(def, var_index);
      }
   }

   if (call->return_deref) {
      struct var_entry *var_entry = get_var_entry(live_vars,
                                                  call->return_deref->var);

      for (unsigned i = 0; i < var_entry->components; i++) {
         unsigned var_index = var_entry->offset + i;
         BITSET_CLEAR(use, var_index);
         if (def)
            BITSET_SET(def, var_index);
      }
   }
}

void
glsl_live_variables_process_defs(struct glsl_live_variables *live_vars,
                                 BITSET_WORD *def,
                                 BITSET_WORD *use,
                                 ir_instruction *ir)
{
   ir_assignment *assign = ir->as_assignment();
   if (assign) {
      process_assign_def(live_vars, def, use, assign);
      return;
   }

   ir_call *call = ir->as_call();
   if (call) {
      process_call_def(live_vars, def, use, call);
      return;
   }
}

namespace {

class use_process_visitor : public ir_hierarchical_visitor
{
public:
   use_process_visitor(struct glsl_live_variables *live_vars,
                       BITSET_WORD *def, BITSET_WORD *use);

   struct glsl_live_variables *live_vars;
   BITSET_WORD *def;
   BITSET_WORD *use;
   virtual ir_visitor_status visit(ir_dereference_variable *ir);
   virtual ir_visitor_status visit_enter(ir_swizzle *ir);
   virtual ir_visitor_status visit(ir_emit_vertex *ir);

   virtual ir_visitor_status visit_enter(ir_if *ir);
   virtual ir_visitor_status visit_enter(ir_loop *ir);
};

} /* anonymous namespace */

use_process_visitor::use_process_visitor(struct glsl_live_variables *live_vars,
                                         BITSET_WORD *def, BITSET_WORD *use)
   : live_vars(live_vars), def(def), use(use)
{
}

static void
mark_use(BITSET_WORD *def, BITSET_WORD *use, unsigned bit)
{
   BITSET_SET(use, bit);
   if (def)
      BITSET_CLEAR(def, bit);
}

ir_visitor_status
use_process_visitor::visit(ir_dereference_variable *ir)
{
   struct var_entry *var_entry = get_var_entry(live_vars, ir->var);

   if (in_assignee)
      return visit_continue;

   for (unsigned i = 0; i < var_entry->components; i++) {
      mark_use(def, use, var_entry->offset + i);
   }

   return visit_continue;
}

ir_visitor_status
use_process_visitor::visit_enter(ir_swizzle *ir)
{
   ir_dereference_variable *deref_var =
      ir->val->as_dereference_variable();
   if (deref_var) {
      struct var_entry *var_entry = get_var_entry(live_vars,
                                                  deref_var->var);
      mark_use(def, use, var_entry->offset + ir->mask.x);
      if (ir->mask.num_components > 1)
         mark_use(def, use, var_entry->offset + ir->mask.y);
      if (ir->mask.num_components > 2)
         mark_use(def, use, var_entry->offset + ir->mask.z);
      if (ir->mask.num_components > 3)
         mark_use(def, use, var_entry->offset + ir->mask.w);
      return visit_continue_with_parent;
   }

   return visit_continue;
}

ir_visitor_status
use_process_visitor::visit(ir_emit_vertex *ir)
{
   for (unsigned i = 0; i < live_vars->bitset_words; i++) {
      use[i] |= live_vars->main_func_liveout[i];
      if (def)
         def[i] &= ~live_vars->main_func_liveout[i];
   }

   return visit_continue;
}

/** @{ control flow handling for use_process_visitor
 *
 * The hierarchical visitor will walk the whole IR tree starting a node, so
 * for example it will by default look into the "then" and "else" instructions
 * of an if statement.  We need to keep it from descending into those, while
 * still looking at the values that got used by the control flow node itself.
 */
ir_visitor_status
use_process_visitor::visit_enter(ir_if *ir)
{
   ir->condition->accept(this);
   return visit_continue_with_parent;
}

ir_visitor_status
use_process_visitor::visit_enter(ir_loop *ir)
{
   return visit_continue_with_parent;
}

/** @} */

void
glsl_live_variables_process_uses(struct glsl_live_variables *live_vars,
                                 BITSET_WORD *def,
                                 BITSET_WORD *use,
                                 ir_instruction *ir)
{
   use_process_visitor upv(live_vars, def, use);
   ir->accept(&upv);
}

/**
 * Computes the set of variables that are assumed to be live at the exit of
 * every function.  This will be used in make_bb_ht to prepopulate liveout.
 *
 * main() is a special case because only shader outputs are live at the end of
 * it.  Other functions, we just assume that all writeable global variables
 * are live (out of laziness).
 */
static bool
setup_function_exit_liveout(struct glsl_live_variables *live_vars,
                            exec_list *instructions)
{
   live_vars->main_func_liveout = rzalloc_array(live_vars, BITSET_WORD,
                                                live_vars->bitset_words);
   live_vars->modifiable_globals = rzalloc_array(live_vars, BITSET_WORD,
                                                 live_vars->bitset_words);
   if (!live_vars->main_func_liveout || !live_vars->modifiable_globals)
      return false;

   /* Walk the top-level instructions looking for modifiable variables. */
   foreach_list(node, instructions) {
      ir_instruction *ir = (ir_instruction *)node;
      ir_variable *var = ir->as_variable();

      if (!var)
         continue;

      if (var->data.mode != ir_var_shader_out &&
          var->data.mode != ir_var_temporary &&
          var->data.mode != ir_var_auto) {
         continue;
      }

      struct var_entry *var_entry = get_var_entry(live_vars, var);
      if (!var_entry)
         continue;

      for (unsigned i = 0; i < var_entry->components; i++) {
         BITSET_SET(live_vars->modifiable_globals, var_entry->offset + i);
         if (var->data.mode == ir_var_shader_out)
            BITSET_SET(live_vars->main_func_liveout, var_entry->offset + i);
      }
   }

   return true;
}

static void
setup_use_def(struct glsl_live_variables *live_vars)
{
   for (unsigned i = 0; i < live_vars->num_blocks; i++) {
      struct glsl_live_variables_block *lvb = &live_vars->block[i];
      struct bb_entry *bb = lvb->bb_entry;

      if (debug) {
         printf("block %d bb %p\n", bb->index, bb);
         foreach_list_typed(bb_entry_link, child, link, &bb->successors) {
            printf("  -> block %d bb %p\n", child->bb->index, child->bb);
         }
      }

      /* If the block has no instructions (function exit nodes), there's
       * nothing to do.
       */
      if (!bb->first)
         continue;

      ir_instruction *ir, *prev;
      for (ir = bb->last, prev = (ir_instruction *)ir->prev;
           ir != bb->first->prev;
           ir = prev, prev = (ir_instruction *)ir->prev) {
         glsl_live_variables_process_defs(live_vars, lvb->def, lvb->use, ir);
         glsl_live_variables_process_uses(live_vars, lvb->def, lvb->use, ir);
      }
   }
}

static void
mark_function_exit_liveout(struct glsl_live_variables *live_vars,
                           exec_list *instructions)
{
   foreach_list(node, instructions) {
      ir_instruction *ir = (ir_instruction *)node;

      ir_function *f = ir->as_function();
      if (!f)
         continue;

      foreach_list(signode, &f->signatures) {
         ir_function_signature *sig = (ir_function_signature *) signode;
         bool is_main = !strcmp(f->name, "main");

         struct hash_entry *he =
            _mesa_hash_table_search(live_vars->func_exit_ht,
                                    _mesa_hash_pointer(sig), sig);
         struct bb_entry *bb = (struct bb_entry *)he->data;
         struct glsl_live_variables_block *lvb = &live_vars->block[bb->index];

         /* Mark any live global variables this function might modify as live.
          */
         if (is_main) {
            memcpy(lvb->liveout, live_vars->main_func_liveout,
                   sizeof(BITSET_WORD) * live_vars->bitset_words);
         } else {
            memcpy(lvb->liveout, live_vars->modifiable_globals,
                   sizeof(BITSET_WORD) * live_vars->bitset_words);
         }

         /* Mark any function outputs as live. */
	 foreach_list(pnode, &sig->parameters) {
	    ir_variable *var = (ir_variable *) pnode;

            if (var->data.mode != ir_var_function_out &&
                var->data.mode != ir_var_function_inout) {
               continue;
            }

            struct var_entry *var_entry = get_var_entry(live_vars, var);
            if (!var_entry)
               continue;

            for (unsigned i = 0; i < var_entry->components; i++)
               BITSET_SET(lvb->liveout, var_entry->offset + i);
         }
      }
   }
}

/**
 * The algorithm incrementally sets bits in liveout and livein,
 * propagating it through control flow.  It will eventually terminate
 * because it only ever adds bits, and stops when no bits are added in
 * a pass.
 */
void
propagate_livein_liveout(struct glsl_live_variables *live_vars)
{
   bool cont = true;
   struct glsl_live_variables_block *bd = live_vars->block;

   while (cont) {
      cont = false;

      for (unsigned b = 0; b < live_vars->num_blocks; b++) {
	 /* Update livein by seeing if a use reaches the top of each block */
	 for (unsigned i = 0; i < live_vars->bitset_words; i++) {
            BITSET_WORD new_livein = (bd[b].use[i] |
                                      (bd[b].liveout[i] & ~bd[b].def[i]));
	    if (new_livein & ~bd[b].livein[i]) {
               bd[b].livein[i] |= new_livein;
               cont = true;
	    }
	 }

         /* Update liveout by seeing if any child needs the def. */
         foreach_list_typed(bb_entry_link, child, link,
                            &bd[b].bb_entry->successors) {
            unsigned c = child->bb->index;
            for (unsigned i = 0; i < live_vars->bitset_words; i++) {
               BITSET_WORD new_liveout = (live_vars->block[c].livein[i] &
                                          ~live_vars->block[b].liveout[i]);
               if (new_liveout) {
                  live_vars->block[b].liveout[i] |= new_liveout;
                  cont = true;
               }
            }
         }
      }
   }
}

struct glsl_live_variables *
glsl_get_live_variables(exec_list *instructions)
{
   struct glsl_live_variables *live_vars = rzalloc(NULL, glsl_live_variables);
   if (!live_vars)
      return NULL;

   live_vars->func_exit_ht = _mesa_hash_table_create(live_vars,
                                                     _mesa_key_pointer_equal);
   if (!live_vars->func_exit_ht) {
      ralloc_free(live_vars);
      return NULL;
   }

   if (debug) {
      fprintf(stderr, "GET LIVE VARS:\n");
      _mesa_print_ir(stderr, instructions, NULL);
      fprintf(stderr, "\n");
   }

   if (!make_var_ht(live_vars, instructions)) {
      ralloc_free(live_vars);
      return NULL;
   }

   if (!setup_function_exit_liveout(live_vars, instructions)) {
      ralloc_free(live_vars);
      return NULL;
   }

   if (!make_bb_ht(live_vars, instructions)) {
      ralloc_free(live_vars);
      return NULL;
   }

   unsigned w = live_vars->bitset_words;
   for (unsigned i = 0; i < live_vars->num_blocks; i++) {
      live_vars->block[i].def = rzalloc_array(live_vars, BITSET_WORD, w);
      live_vars->block[i].use = rzalloc_array(live_vars, BITSET_WORD, w);
      live_vars->block[i].livein = rzalloc_array(live_vars, BITSET_WORD, w);
      live_vars->block[i].liveout = rzalloc_array(live_vars, BITSET_WORD, w);
      if (!live_vars->block[i].def ||
          !live_vars->block[i].use ||
          !live_vars->block[i].livein ||
          !live_vars->block[i].liveout) {
         ralloc_free(live_vars);
         return NULL;
      }
   }

   setup_use_def(live_vars);
   mark_function_exit_liveout(live_vars, instructions);
   propagate_livein_liveout(live_vars);

   return live_vars;
}
