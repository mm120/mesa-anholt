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

SDValue
gen_target_lowering::LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                                          bool isVarArg,
                                          const SmallVectorImpl<ISD::InputArg> &Ins,
                                          DebugLoc DL, SelectionDAG &DAG,
                                          SmallVectorImpl<SDValue> &InVals) const
{
   /* FINISHME: What exactly are we doing here? */
   for (unsigned i = 0, e = Ins.size(); i < e; ++i) {
      InVals.push_back(SDValue());
   }
   return Chain;
}

SDValue
gen_target_lowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                 bool isVarArg,
                                 const SmallVectorImpl<ISD::OutputArg> &Outs,
                                 const SmallVectorImpl<SDValue> &OutVals,
                                 DebugLoc DL, SelectionDAG &DAG) const
{
   /* FINISHME: No clue. */
   return Chain;
}

SDValue
gen_target_lowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const
{
   return Op;
}

