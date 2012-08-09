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

#include "llvm/Target/TargetLowering.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

using namespace llvm;

class gen_target_lowering : public TargetLowering
{
public:
   gen_target_lowering(TargetMachine &tm)
      : TargetLowering(tm, new TargetLoweringObjectFileELF())
   {
   }

   virtual SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                                        bool isVarArg,
                                        const SmallVectorImpl<ISD::InputArg> &Ins,
                                        DebugLoc DL, SelectionDAG &DAG,
                                        SmallVectorImpl<SDValue> &InVals) const;

   virtual SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::OutputArg> &Outs,
                               const SmallVectorImpl<SDValue> &OutVals,
                               DebugLoc DL, SelectionDAG &DAG) const;

   virtual SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const;
};
