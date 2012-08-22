/*
 * Copyright (C) 2005-2007  Brian Paul   All Rights Reserved.
 * Copyright (C) 2008  VMware, Inc.   All Rights Reserved.
 * Copyright © 2010 Intel Corporation
 * Copyright © 2010 Luca Barbieri
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

#ifndef IR_TO_LLVM_H_
#define IR_TO_LLVM_H_

#ifdef _MSC_VER
#include <unordered_map>
#else
#include <tr1/unordered_map>
// use C++0x/Microsoft convention
namespace std
{
   using namespace tr1;
}
#endif

#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Intrinsics.h"
#include "llvm/Support/IRBuilder.h"
#include "ir.h"

class ir_to_llvm : public ir_visitor {
public:
   ir_to_llvm(llvm::LLVMContext &ctx);
   llvm::Type *llvm_base_type(unsigned base_type);
   llvm::Type *llvm_vec_type(const glsl_type *type);
   llvm::Type *llvm_type(const glsl_type *type);
   llvm::Value *llvm_variable(ir_variable *var);
   llvm::Function *llvm_function(ir_function_signature *sig);
   llvm::Value *llvm_value(ir_instruction *ir);
   llvm::Constant *llvm_constant(ir_constant *ir);
   llvm::Constant *llvm_int(unsigned v);
   llvm::Value *llvm_pointer(ir_rvalue *ir);
   llvm::Value *llvm_intrinsic(llvm::Intrinsic::ID id, llvm::Value *a);
   llvm::Value *llvm_intrinsic(llvm::Intrinsic::ID id,
                               llvm::Value *a, llvm::Value *b);
   llvm::Constant *llvm_imm(llvm::Type *type, double v);
   llvm::Value *llvm_expression(ir_expression *ir);
   llvm::Value *llvm_shuffle(llvm::Value *val, int *shuffle_mask,
                             unsigned res_width, const llvm::Twine &name = "");

   virtual void visit(ir_expression *ir);
   virtual void visit(ir_dereference_array *ir);
   virtual void visit(ir_dereference_record *ir);
   virtual void visit(ir_dereference_variable *ir);
   virtual void visit(ir_texture *ir);
   virtual void visit(ir_discard *ir);
   virtual void visit(ir_loop_jump *ir);
   virtual void visit(ir_loop *ir);
   virtual void visit(ir_if *ir);
   virtual void visit(ir_return *ir);
   virtual void visit(ir_call *ir);
   virtual void visit(ir_constant *ir);
   virtual void visit(ir_swizzle *ir);
   virtual void visit(ir_assignment *ir);
   virtual void visit(ir_variable *ir);
   virtual void visit(ir_function *ir);
   virtual void visit(ir_function_signature *ir);

   /**
    * Called at the start of generating code for main(), this can be used for
    * setting up values in the global variables using intrinsics.
    */
   virtual void build_prologue() = 0;

   /**
    * Called at the end of generating code for main(), this can be used for
    * setting up outputs of the global variables to actual hardware state
    * using intrinsics.
    *
    * Note that if you don't implement this function to actually do something
    * with the global outputs, optimization of the module with Internalize and
    * GlobalOptimizer passes will end up dead-code eliminating all of main()!
    */
   virtual void build_epilogue() = 0;

   /** Walks the shader's ir and returns an LLVM module for the code. */
   llvm::Module *build_module(struct exec_list *ir);

   llvm::LLVMContext& ctx;
   llvm::Module *mod;
   llvm::Function *fun;
   /* could easily support more loops, but GLSL doesn't support multiloop
    * break/continue
    */
   std::pair<llvm::BasicBlock *, llvm::BasicBlock *> loop;
   llvm::BasicBlock *bb;
   llvm::Value *result;
   llvm::IRBuilder<> bld;

   typedef std::unordered_map<ir_variable *, llvm::Value *> llvm_variables_t;
   llvm_variables_t llvm_variables;

   typedef std::unordered_map<ir_function_signature *,
      llvm::Function *> llvm_functions_t;
   llvm_functions_t llvm_functions;
};

#endif /* IR_TO_LLVM_H_ */
