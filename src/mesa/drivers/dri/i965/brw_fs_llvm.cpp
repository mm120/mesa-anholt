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

#include "llvm/DerivedTypes.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Assembly/PrintModulePass.h"
#include "llvm/Transforms/IPO.h"
#include <llvm/Support/FormattedStream.h>
#include "llvm/Support/TargetRegistry.h"
#include "glsl/ir_to_llvm.h"
#include "brw_fs.h"
#include "gen_target_machine.h"

using namespace llvm;

namespace {
class fs_ir_to_llvm : public ir_to_llvm
{
public:
   fs_ir_to_llvm();
   virtual void build_prologue();
   virtual void build_epilogue();
};

} /* namespace */

fs_ir_to_llvm::fs_ir_to_llvm()
   : ir_to_llvm(getGlobalContext())
{
}

void
fs_ir_to_llvm::build_prologue()
{
}

void
fs_ir_to_llvm::build_epilogue()
{
}

bool
fs_visitor::build_llvm()
{
   Module *mod;

   fs_ir_to_llvm build;

   mod = build.build_module(shader->ir);
   if (!mod)
      return false;

   mod->dump();

   gen_initialize_llvm_target();
   gen_initialize_llvm_target_mc();

   std::string FS;
   std::string TT = "FINISHME";
   std::string CPU = "gen7";
   TargetOptions TO;

   gen_target_machine *tm = (gen_target_machine *)
      the_gen_target.createTargetMachine(
         TT,
         CPU,
         FS,
         TO,
         Reloc::Default,
         CodeModel::Default,
         CodeGenOpt::Default);

   PassManager PM;
   PM.add(new TargetData(*tm->getTargetData()));

   /* We want to turn ir_to_llvm's globals for uniforms and ins/outs into
    * temporary storage (thus registers) which get set up using intrinsics.
    * The first step is for them to be internal linkage.
    */
   PM.add(createInternalizePass(true));
   /* Then, we turn all the internal linkage globals into allocas. */
   PM.add(createGlobalOptimizerPass());

   /* Turn all the allocas (variable storage) into stores of unallocated
    * registers.
    */
   PM.add(createPromoteMemoryToRegisterPass());

   PM.add(createPrintModulePass(&outs()));

   std::string CodeString;
   raw_string_ostream oStream(CodeString);
   formatted_raw_ostream out(oStream);

   tm->addPassesToEmitFile(PM, out, TargetMachine::CGFT_AssemblyFile, true);

   PM.run(*mod);

   printf("dump2\n");
   mod->dump();

   return true;
}
