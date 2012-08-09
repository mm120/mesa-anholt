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

#include "llvm/ADT/BitVector.h"
#include "gen_subtarget.h"
#include "gen_target_machine.h"

#define GET_REGINFO_TARGET_DESC
#include "gen_register_info.h.inc"

gen_register_info::gen_register_info(TargetMachine &tm,
                                     const TargetInstrInfo &tii)
   : genGenRegisterInfo(0), tm(tm), tii(tii)
{
}

static uint16_t callee_saved_reg = gen::NoRegister;;

/* Unused function handling support */
const uint16_t *
gen_register_info::getCalleeSavedRegs(const MachineFunction *MF) const
{
   return &callee_saved_reg;
}

void
gen_register_info::eliminateFrameIndex(MachineBasicBlock::iterator MI,
                                       int SPAdj,
                                       RegScavenger *RS) const
{
   assert(!"Subroutines not supported yet");
}

unsigned
gen_register_info::getFrameRegister(const MachineFunction &MF) const
{
   assert(!"Subroutines not supported yet");
   return 0;
}

BitVector gen_register_info::getReservedRegs(const MachineFunction &MF) const
{
   BitVector reserved(getNumRegs());

   return reserved;
}
