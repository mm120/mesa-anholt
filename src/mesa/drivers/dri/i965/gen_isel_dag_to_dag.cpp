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

#include "gen_target_machine.h"
#include "llvm/CodeGen/SelectionDAGISel.h"

class gen_dag_to_dag_isel : public SelectionDAGISel {
public:
   gen_dag_to_dag_isel(gen_target_machine &tm)
   : SelectionDAGISel(tm),
     TM(tm)
   {
   }

   SDNode *Select(SDNode *N);

   gen_target_machine &TM;

   /* The real meat of this file: All the generated code for pattern matching
    * LLVM IR and choosing gen instructions for it.
    */
#include "gen_dag_isel.h.inc"
};

SDNode *gen_dag_to_dag_isel::Select(SDNode *N)
{
   return SelectCode(N);
}

FunctionPass *llvm::create_gen_isel_dag(gen_target_machine &tm)
{
   return new gen_dag_to_dag_isel(tm);
}

