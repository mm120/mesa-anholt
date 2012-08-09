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

/** @file gen_mc_target_desc.cpp
 *
 * Connects the code-generated MC info through
 * gen_initialize_llvm_target_mc().
 *
 * If we were a backend integrated into LLVM, that function would instead
 * LLVMInitializegenTargetMC and it would get called through
 * InitializeAllTargetMCs(), but since we aren't integrated we do it manually.
 */

#include "llvm/MC/MCCodeGenInfo.h"
#include "llvm/Support/TargetRegistry.h"
#include "gen_subtarget.h"
#include "gen_target_machine.h"
#include "gen_mc_asm_info.h"

#define GET_INSTRINFO_MC_DESC
#include "gen_instr_info.h.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "gen_subtarget_info.h.inc"

#define GET_REGINFO_MC_DESC
#include "gen_register_info.h.inc"

using namespace llvm;

static MCInstrInfo *create_gen_mc_instr_info()
{
   MCInstrInfo *X = new MCInstrInfo();
   InitgenMCInstrInfo(X);
   return X;
}

static MCRegisterInfo *create_gen_mc_register_info(StringRef TT)
{
   MCRegisterInfo *X = new MCRegisterInfo();
   InitgenMCRegisterInfo(X, 0);
   return X;
}

static MCSubtargetInfo *
create_gen_mc_subtarget_info(StringRef TT, StringRef CPU, StringRef FS)
{
   MCSubtargetInfo * X = new MCSubtargetInfo();
   InitgenMCSubtargetInfo(X, TT, CPU, FS);
   return X;
}

static MCCodeGenInfo *
create_gen_mc_codegen_info(StringRef TT, Reloc::Model RM,
                           CodeModel::Model CM, CodeGenOpt::Level OL)
{
   MCCodeGenInfo *X = new MCCodeGenInfo();
   X->InitMCCodeGenInfo(RM, CM, OL);
   return X;
}

void gen_initialize_llvm_target_mc()
{
   RegisterMCAsmInfo<gen_mc_asm_info> Y(the_gen_target);

   TargetRegistry::RegisterMCCodeGenInfo(the_gen_target,
                                         create_gen_mc_codegen_info);

   TargetRegistry::RegisterMCInstrInfo(the_gen_target,
                                       create_gen_mc_instr_info);

   TargetRegistry::RegisterMCRegInfo(the_gen_target,
                                     create_gen_mc_register_info);

   TargetRegistry::RegisterMCSubtargetInfo(the_gen_target,
                                           create_gen_mc_subtarget_info);
}
