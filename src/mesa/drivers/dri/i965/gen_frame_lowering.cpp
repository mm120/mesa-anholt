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

/** @file gen_frame_lowering.cpp
 *
 * Implements the TargetFrameLowering class for gen4+ Intel GPUs.
 *
 * Since we don't actually implement function calls, this is just the minimal
 * stub required to make the compiler run.
 */

#include "gen_target_machine.h"


using namespace llvm;

gen_frame_lowering::gen_frame_lowering(StackDirection D,
                                       unsigned StackAl,
                                       int LAO, unsigned TransAl)
  : TargetFrameLowering(D, StackAl, LAO, TransAl)
{
}

gen_frame_lowering::~gen_frame_lowering()
{
}

void
gen_frame_lowering::emitPrologue(MachineFunction &MF) const
{
}

void
gen_frame_lowering::emitEpilogue(MachineFunction &MF,
                                 MachineBasicBlock &MBB) const
{
}

bool
gen_frame_lowering::hasFP(const MachineFunction &MF) const
{
  return false;
}
