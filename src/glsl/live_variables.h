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

#include "ir.h"
#include "main/bitset.h"

struct var_entry {
   ir_variable *var;
   unsigned offset;
   unsigned components;
};

class bb_entry_link {
public:
   DECLARE_RALLOC_CXX_OPERATORS(bb_entry_link);

   exec_node link;
   struct bb_entry *bb;
};

class bb_entry {
public:
   DECLARE_RALLOC_CXX_OPERATORS(bb_entry);

   int index;

   /** First instruction of the basic block. */
   ir_instruction *first;

   /** Last instruction of the basic block.
    *
    * Note that the last instruction may be a control flow instruction, so be
    * careful when running a visitor over it, which might walk into the "then"
    * and "else" basic blocks of an if statement, for example.
    */
   ir_instruction *last;

   /** List of struct bb_entry that @last may jump to. */
   exec_list successors;
};

struct glsl_live_variables_block {
   /**
    * Which variables are defined before being used in the block.
    *
    * Note that for our purposes, "defined" means unconditionally, completely
    * defined.
    */
   BITSET_WORD *def;

   /**
    * Which variables are used before being defined in the block.
    */
   BITSET_WORD *use;

   /** Which defs reach the entry point of the block. */
   ;   BITSET_WORD *livein;

   /** Which defs reach the exit point of the block. */
   BITSET_WORD *liveout;

   struct bb_entry *bb_entry;
};

struct glsl_live_variables {
   /** Mapping from ir_variable * to struct var_entry */
   struct hash_table *var_ht;

   /** Mapping from the first ir_instruction in each block to struct bb_entry */
   struct hash_table *bb_ht;

   /**
    * Mapping from the ir_function_signature to struct bb_entry for the exit
    * node.
    */
   struct hash_table *func_exit_ht;

   struct glsl_live_variables_block *block;

   /** The channels of variables that are live at the exit of main()
    * (everything that is a ir_var_shader_output).
    */
   BITSET_WORD *main_func_liveout;

   /**
    * The channels of variables that are globals that might be modified (and
    * thus need to have their use/def tracked at function call sites.
    */
   BITSET_WORD *modifiable_globals;

   unsigned bitset_words;
   unsigned num_blocks;
};

void
glsl_live_variables_process_defs(struct glsl_live_variables *live_vars,
                                 BITSET_WORD *def,
                                 BITSET_WORD *use,
                                 ir_instruction *ir);
void
glsl_live_variables_process_uses(struct glsl_live_variables *live_vars,
                                 BITSET_WORD *def,
                                 BITSET_WORD *use,
                                 ir_instruction *ir);

struct glsl_live_variables *
glsl_get_live_variables(exec_list *instructions);
