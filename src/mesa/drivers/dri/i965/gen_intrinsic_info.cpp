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

/** @file gen_intrinsic_info.cpp
 *
 * Implements some functions for getting at the gen intrinsic operations.
 *
 * Most of the code is generated from the .td files, and we're just wrapping
 * it with a bit of glue code.
 */

#include "llvm/Module.h"
#include "llvm/Function.h"
#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#include "gen_intrinsic_info.h"
#include "gen_target_machine.h"

static FunctionType *
getType(LLVMContext &Context, unsigned id) {
   Type *ResultTy = NULL;
   SmallVector<Type*, 8> ArgTys;
   bool IsVarArg = false;

#define GET_INTRINSIC_GENERATOR
#include "gen_intrinsics.h.inc"
#undef GET_INTRINSIC_GENERATOR

  return FunctionType::get(ResultTy, ArgTys, IsVarArg);
}

std::string gen_intrinsic_info::getName(unsigned IntrID, Type **Tys,
                                        unsigned numTys) const {
   static const char *const names[] = {
#define GET_INTRINSIC_NAME_TABLE
#include "gen_intrinsics.h.inc"
#undef GET_INTRINSIC_NAME_TABLE
   };

   assert(!isOverloaded(IntrID) && "gen intrinsics are not overloaded");
   if (IntrID < Intrinsic::num_intrinsics)
      return 0;
   assert(IntrID < genIntrinsic::num_gen_intrinsics &&
          "Invalid intrinsic ID");

   std::string Result(names[IntrID - Intrinsic::num_intrinsics]);
   return Result;
}

unsigned gen_intrinsic_info::
lookupName(const char *Name, unsigned Len) const {
   if (strncmp(Name, "llvm.", 5) != 0)
      return 0; /* All intrinsics start with 'llvm.' */

#define GET_FUNCTION_RECOGNIZER
#include "gen_intrinsics.h.inc"
#undef GET_FUNCTION_RECOGNIZER
   return 0;
}

bool
gen_intrinsic_info::isOverloaded(unsigned IntrID) const {
   if (IntrID == 0)
      return false;

   unsigned id = IntrID - Intrinsic::num_intrinsics + 1;
#define GET_INTRINSIC_OVERLOAD_TABLE
#include "gen_intrinsics.h.inc"
#undef GET_INTRINSIC_OVERLOAD_TABLE
}

/// This defines the "getAttributes(genIntrinsic::ID id)" function.
#define GET_INTRINSIC_ATTRIBUTES
#include "gen_intrinsics.h.inc"
#undef GET_INTRINSIC_ATTRIBUTES

Function *
gen_intrinsic_info::getDeclaration(Module *M,
                                   unsigned intrinsic,
                                   Type **Tys,
                                   unsigned numTys) const
{
   assert(!isOverloaded(intrinsic) && "gen intrinsics are not overloaded");
   AttrListPtr AList = getAttributes((genIntrinsic::ID) intrinsic);
   return cast<Function>(M->getOrInsertFunction(getName(intrinsic),
                                                getType(M->getContext(),
                                                        intrinsic),
                                                AList));
}
