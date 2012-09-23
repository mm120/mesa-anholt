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
 * Prototypes the TargetMachine class for gen4+ Intel GPUs.
 */

#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetFrameLowering.h"

#include "gen_frame_lowering.h"
#include "gen_register_info.h"
#include "gen_subtarget.h"
#include "gen_instr_info.h"
#include "gen_intrinsic_info.h"

using namespace llvm;

class gen_target_lowering;

class gen_target_machine : public LLVMTargetMachine {
public:
   gen_target_machine(const Target &T,
                      StringRef TT,
                      StringRef CPU,
                      StringRef FS,
                      TargetOptions TO,
                      Reloc::Model RM,
                      CodeModel::Model CM,
                      CodeGenOpt::Level OL);

   const TargetData DataLayout;

   genSubtarget subtarget;
   gen_instr_info *instr_info;
   gen_target_lowering *target_lowering;
   gen_frame_lowering frame_lowering;
   gen_intrinsic_info intrinsic_info;

   virtual const genSubtarget *getSubtargetImpl() const
   {
      return &subtarget;
   }

   virtual const gen_instr_info *getInstrInfo() const
   {
      return instr_info;
   }

   virtual const TargetData *getTargetData() const
   {
      return &DataLayout;
   }

   virtual const gen_register_info *getRegisterInfo() const
   {
      return &instr_info->getRegisterInfo();
   }

   virtual const TargetLowering *getTargetLowering() const;

   virtual const gen_frame_lowering *getFrameLowering() const
   {
      return &frame_lowering;
   }

   virtual TargetPassConfig *createPassConfig(PassManagerBase &PM);
};

namespace llvm {
   extern Target the_gen_target;
   FunctionPass *create_gen_isel_dag(gen_target_machine &tm);
}

void gen_initialize_llvm_target();
void gen_initialize_llvm_target_mc();
