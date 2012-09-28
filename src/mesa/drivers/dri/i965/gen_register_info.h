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

#include "llvm/Target/TargetRegisterInfo.h"
#define GET_REGINFO_HEADER
#define GET_REGINFO_ENUM
#include "gen_register_info.h.inc"

using namespace llvm;

class gen_register_info : public genGenRegisterInfo
{
public:
   TargetMachine &tm;
   const TargetInstrInfo &tii;
   gen_register_info(TargetMachine &tm, const TargetInstrInfo &tii);

   /** @{ Virtual functions of a RegisterInfo */
   const uint16_t *getCalleeSavedRegs(const MachineFunction *MF = 0) const;
   BitVector getReservedRegs(const MachineFunction &MF) const;
   void eliminateFrameIndex(MachineBasicBlock::iterator II,
                            int SPAdj, RegScavenger *RS = NULL) const;
   unsigned getFrameRegister(const MachineFunction &MF) const;
   /** @} */
};
