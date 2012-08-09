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

/** @file gen_target_machine.h
 * Implements the TargetMachine class for gen4+ Intel GPUs.
 */

#include "gen_target_machine.h"
#include "gen_target_lowering.h"
#include "llvm/PassManager.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/TargetRegistry.h"

Target llvm::the_gen_target;

gen_target_machine::gen_target_machine(const Target &T,
                                       StringRef TT,
                                       StringRef CPU,
                                       StringRef FS,
                                       TargetOptions TO,
                                       Reloc::Model RM,
                                       CodeModel::Model CM,
                                       CodeGenOpt::Level OL)
   : LLVMTargetMachine(T, TT, CPU, FS, TO, RM, CM, OL),
     DataLayout("e-p:32:32")
{
   instr_info = new gen_instr_info(*this);
   target_lowering = new gen_target_lowering(*this);
}

const TargetLowering *
gen_target_machine::getTargetLowering() const
{
   return target_lowering;
}

class gen_pass_config : public TargetPassConfig {
public:
   gen_pass_config(gen_target_machine *TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM)
   {
   }

   gen_target_machine &get_gen_target_machine() const
   {
     return getTM<gen_target_machine>();
   }

   virtual bool addInstSelector();
};

bool
gen_pass_config::addInstSelector()
{
   PM->add(create_gen_isel_dag(get_gen_target_machine()));
   return false;
}

TargetPassConfig *gen_target_machine::createPassConfig(PassManagerBase &PM)
{
   return new gen_pass_config(this, PM);
}

void
gen_initialize_llvm_target()
{
   RegisterTargetMachine<gen_target_machine> X(the_gen_target);
}
