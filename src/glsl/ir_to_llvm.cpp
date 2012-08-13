/*
 * Copyright (C) 2005-2007  Brian Paul   All Rights Reserved.
 * Copyright (C) 2008  VMware, Inc.   All Rights Reserved.
 * Copyright © 2010 Intel Corporation
 * Copyright © 2010 Luca Barbieri
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file ir_to_llvm.cpp
 *
 * Translates the IR to LLVM
 */

/* this tends to get set as part of LLVM_CFLAGS, but we definitely want asserts */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "llvm/DerivedTypes.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Intrinsics.h"

#include <vector>
#include <stdio.h>
#ifdef _MSC_VER
#include <unordered_map>
#else
#include <tr1/unordered_map>
#endif
// use C++0x/Microsoft convention
namespace std
{
   using namespace tr1;
}

using namespace llvm;

#include "ir.h"
#include "ir_visitor.h"
#include "glsl_types.h"

class ir_to_llvm_visitor : public ir_visitor {
public:
   ir_to_llvm_visitor();

   LLVMContext& ctx;
   Module *mod;
   Function *fun;
   /* could easily support more loops, but GLSL doesn't support multiloop
    * break/continue
    */
   std::pair<BasicBlock *, BasicBlock *> loop;
   BasicBlock *bb;
   Value *result;
   IRBuilder<> bld;

   ir_to_llvm_visitor(LLVMContext &p_ctx, Module *p_mod)
      : ctx(p_ctx), mod(p_mod), fun(0), bb(0), bld(ctx)
   {
      loop = std::make_pair((BasicBlock *)0, (BasicBlock *)0);
   }

   Type *llvm_base_type(unsigned base_type)
   {
      switch (base_type) {
      case GLSL_TYPE_VOID:
         return Type::getVoidTy(ctx);
      case GLSL_TYPE_UINT:
      case GLSL_TYPE_INT:
         return Type::getInt32Ty(ctx);
      case GLSL_TYPE_FLOAT:
         return Type::getFloatTy(ctx);
      case GLSL_TYPE_BOOL:
         return Type::getInt1Ty(ctx);
      case GLSL_TYPE_SAMPLER:
         return PointerType::getUnqual(Type::getVoidTy(ctx));
      default:
         assert(0);
         return 0;
      }
   }

   Type *llvm_vec_type(const glsl_type *type)
   {
      if (type->is_array())
         return ArrayType::get(llvm_type(type->fields.array),
                               type->array_size());

      if (type->is_record()) {
         std::vector<Type *> fields;
         for (unsigned i = 0; i < type->length; i++)
            fields.push_back(llvm_type(type->fields.structure[i].type));
         return StructType::get(ctx, fields);
      }

      Type *base_type = llvm_base_type(type->base_type);
      if (type->vector_elements <= 1)
         return base_type;
      else
         return VectorType::get(base_type, type->vector_elements);
   }

   Type *llvm_type(const glsl_type *type)
   {
      Type *vec_type = llvm_vec_type(type);
      if (type->matrix_columns <= 1)
         return vec_type;
      else
         return ArrayType::get(vec_type, type->matrix_columns);
   }

   typedef std::unordered_map<ir_variable *, Value *> llvm_variables_t;
   llvm_variables_t llvm_variables;

   Value *llvm_variable(class ir_variable *var)
   {
      llvm_variables_t::iterator vari = llvm_variables.find(var);
      if (vari != llvm_variables.end())
         return vari->second;
      else {
         Type *type = llvm_type(var->type);

         Value *v;
         if (fun) {
            if (bb == &fun->getEntryBlock())
               v = bld.CreateAlloca(type, 0, var->name);
            else
               v = new AllocaInst(type, 0, var->name,
                                  fun->getEntryBlock().getTerminator());
         }
         else {
            /* TODO: can anything global be non-constant in GLSL?; fix
             * linkage
             */
            Function::LinkageTypes linkage;
            if (var->mode == ir_var_auto || var->mode == ir_var_temporary)
               linkage = GlobalValue::InternalLinkage;
            else
               linkage = GlobalValue::ExternalLinkage;
            Constant *init = NULL;
            if (var->constant_value)
               init = llvm_constant(var->constant_value);
            else if (linkage == GlobalValue::InternalLinkage)
               init = UndefValue::get(llvm_type(var->type));
            v = new GlobalVariable(*mod, type, var->read_only, linkage,
                                   init, var->name);
         }
         llvm_variables[var] = v;
         return v;
      }
   }

   typedef std::unordered_map<ir_function_signature *,
                              Function *> llvm_functions_t;
   llvm_functions_t llvm_functions;

   Function *llvm_function(class ir_function_signature *sig)
   {
      llvm_functions_t::iterator funi = llvm_functions.find(sig);
      if (funi != llvm_functions.end())
         return funi->second;
      else {
         const char *name = sig->function_name();
         Function::LinkageTypes linkage;
         if (!strcmp(name, "main") || !sig->is_defined)
            linkage = Function::ExternalLinkage;
         else
            linkage = Function::InternalLinkage;
         std::vector<Type *> params;
         foreach_iter(exec_list_iterator, iter, sig->parameters) {
            ir_variable *arg = (ir_variable *)iter.get();
            params.push_back(llvm_type(arg->type));
         }

         FunctionType *ft = FunctionType::get(llvm_type(sig->return_type),
                                              params, false);

         Function *f = Function::Create(ft, linkage, name, mod);
         llvm_functions[sig] = f;
         return f;
      }

   }

   Value *llvm_value(class ir_instruction *ir)
   {
      result = NULL;
      ir->accept(this);
      return result;
   }

   Constant *llvm_constant(class ir_constant *ir)
   {
      return (Constant *)llvm_value(ir);
   }

   Constant *llvm_int(unsigned v)
   {
      return ConstantInt::get(Type::getInt32Ty(ctx), v);
   }

   Value *llvm_pointer(class ir_rvalue *ir)
   {
      if (ir_dereference_variable *deref = ir->as_dereference_variable())
         return llvm_variable(deref->variable_referenced());
      else if (ir_dereference_array *deref = ir->as_dereference_array()) {
         Value *ref[] = {
            llvm_int(0),
            llvm_value(deref->array_index)
         };
         return bld.CreateInBoundsGEP(llvm_pointer(deref->array), ref);
      }
      else if (ir->as_dereference()) {
         ir_dereference_record *deref = (ir_dereference_record *)ir;
         int idx = deref->record->type->field_index(deref->field);
         assert(idx >= 0);
         return bld.CreateConstInBoundsGEP2_32(llvm_pointer(deref->record),
                                               0, idx);
      }
      else {
         assert(0);
         return 0;
      }
   }

   Value *llvm_intrinsic(Intrinsic::ID id, Value *a)
   {
      Type *types[1] = {a->getType()};
      return bld.CreateCall(Intrinsic::getDeclaration(mod, id, types), a);
   }

   Value *llvm_intrinsic(Intrinsic::ID id, Value *a, Value *b)
   {
      Type *types[2] = {a->getType(), b->getType()};
      /* only one type suffix is usually needed, so pass 1 here */
      return bld.CreateCall2(Intrinsic::getDeclaration(mod, id, types), a, b);
   }

   Constant *llvm_imm(Type *type, double v)
   {
      if (type->isVectorTy()) {
         std::vector<Constant *> values;
         values.push_back(llvm_imm(((VectorType *)type)->getElementType(), v));
         for (unsigned i = 1; i < ((VectorType *)type)->getNumElements(); ++i)
            values.push_back(values[0]);
         return ConstantVector::get(values);
      }
      else if (type->isIntegerTy())
         return ConstantInt::get(type, v);
      else if (type->isFloatingPointTy())
         return ConstantFP::get(type, v);
      else {
         assert(0);
         return 0;
      }
   }

   static Value *create_shuffle3(IRBuilder<>& bld,
                                 Value *v,
                                 unsigned a, unsigned b, unsigned c,
                                 const Twine& name = "")
   {
      Type *int_ty = Type::getInt32Ty(v->getContext());
      Constant *vals[3] = {
         ConstantInt::get(int_ty, a),
         ConstantInt::get(int_ty, b),
         ConstantInt::get(int_ty, c)
      };
      return bld.CreateShuffleVector(v, UndefValue::get(v->getType()),
                                     ConstantVector::get(vals),
                                     name);
   }

   Value *llvm_expression(ir_expression *ir)
   {
      Value *ops[2];
      for (unsigned i = 0; i < ir->get_num_operands(); ++i)
         ops[i] = llvm_value(ir->operands[i]);

      if (ir->get_num_operands() == 2) {
         int vecidx = -1;
         int scaidx = -1;
         if (ir->operands[0]->type->vector_elements <= 1 &&
             ir->operands[1]->type->vector_elements > 1) {
            scaidx = 0;
            vecidx = 1;
         }
         else if (ir->operands[0]->type->vector_elements > 1 &&
                  ir->operands[1]->type->vector_elements <= 1) {
            scaidx = 1;
            vecidx = 0;
         }
         else
            assert(ir->operands[0]->type->vector_elements ==
                   ir->operands[1]->type->vector_elements);

         if (scaidx >= 0) {
            Value *vec;
            vec = UndefValue::get(ops[vecidx]->getType());
            for (unsigned i = 0; i < ir->operands[vecidx]->type->vector_elements; ++i)
               vec = bld.CreateInsertElement(vec,  ops[scaidx],
                                             llvm_int(i), "sca2vec");
            ops[scaidx] = vec;
         }
      }

      switch (ir->operation) {
      case ir_unop_logic_not:
         return bld.CreateNot(ops[0]);
      case ir_unop_neg:
         return bld.CreateNeg(ops[0]);
      case ir_unop_abs:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_UINT:
         case GLSL_TYPE_BOOL:
            return ops[0];
         case GLSL_TYPE_INT:
            return bld.CreateSelect(bld.CreateICmpSGE(ops[0],
                                                      llvm_imm(ops[0]->getType(), 0),
                                                      "sabs.ge"),
                                    ops[0], bld.CreateNeg(ops[0], "sabs.neg"),
                                    "sabs.select");
         case GLSL_TYPE_FLOAT:
            return bld.CreateSelect(bld.CreateFCmpUGE(ops[0],
                                                      llvm_imm(ops[0]->getType(), 0),
                                                      "fabs.ge"),
                                    ops[0], bld.CreateFNeg(ops[0], "fabs.neg"),
                                    "fabs.select");
         default:
            assert(0);
         }

      case ir_unop_sign:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
            return ops[0];
         case GLSL_TYPE_UINT:
            return bld.CreateZExt(bld.CreateICmpNE(ops[0],
                                                   llvm_imm(ops[0]->getType(),
                                                            0),
                                                   "usign.ne"),
                                  ops[0]->getType(), "usign.zext");
         case GLSL_TYPE_INT:
            return bld.CreateSelect(bld.CreateICmpNE(ops[0],
                                                     llvm_imm(ops[0]->getType(),
                                                              0),
                                                     "ssign.ne"),
                                    bld.CreateSelect(bld.CreateICmpSGE(ops[0],
                                                                       llvm_imm(ops[0]->getType(), 0),
                                                                       "ssign.ge"),
                                                     llvm_imm(ops[0]->getType(),
                                                              1),
                                                     llvm_imm(ops[0]->getType(),
                                                              -1),
                                                     "sabs.selects"),
                                    llvm_imm(ops[0]->getType(), 0),
                                    "sabs.select0");
         case GLSL_TYPE_FLOAT:
            return bld.CreateSelect(bld.CreateFCmpONE(ops[0],
                                                      llvm_imm(ops[0]->getType(),
                                                               0),
                                                      "fsign.ne"),
                                    bld.CreateSelect(bld.CreateFCmpUGE(ops[0],
                                                                       llvm_imm(ops[0]->getType(),
                                                                                0),
                                                                       "fsign.ge"),
                                                     llvm_imm(ops[0]->getType(), 1),
                                                     llvm_imm(ops[0]->getType(), -1),
                                                     "fabs.selects"),
                                    llvm_imm(ops[0]->getType(), 0),
                                    "fabs.select0");
         default:
            assert(0);
         }

      case ir_unop_rcp:
         assert(ir->operands[0]->type->base_type == GLSL_TYPE_FLOAT);
         return bld.CreateFDiv(llvm_imm(ops[0]->getType(), 1), ops[0]);
      case ir_unop_exp:
         assert(ir->operands[0]->type->base_type == GLSL_TYPE_FLOAT);
         return llvm_intrinsic(Intrinsic::exp, ops[0]);
      case ir_unop_exp2:
         assert(ir->operands[0]->type->base_type == GLSL_TYPE_FLOAT);
         return llvm_intrinsic(Intrinsic::exp2, ops[0]);
      case ir_unop_log:
         assert(ir->operands[0]->type->base_type == GLSL_TYPE_FLOAT);
         return llvm_intrinsic(Intrinsic::log, ops[0]);
      case ir_unop_log2:
         assert(ir->operands[0]->type->base_type == GLSL_TYPE_FLOAT);
         return llvm_intrinsic(Intrinsic::log2, ops[0]);
      case ir_unop_sin:
         assert(ir->operands[0]->type->base_type == GLSL_TYPE_FLOAT);
         return llvm_intrinsic(Intrinsic::sin, ops[0]);
      case ir_unop_cos:
         assert(ir->operands[0]->type->base_type == GLSL_TYPE_FLOAT);
         return llvm_intrinsic(Intrinsic::cos, ops[0]);

         /* TODO: implement these somehow */
      case ir_unop_dFdx:
         assert(0);
         /* return llvm_intrinsic(Intrinsic::ddx, ops[0]); */
      case ir_unop_dFdy:
         assert(0);
         /* return llvm_intrinsic(Intrinsic::ddy, ops[0]); */

      case ir_binop_add:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_UINT:
         case GLSL_TYPE_INT:
            return bld.CreateAdd(ops[0], ops[1]);
         case GLSL_TYPE_FLOAT:
            return bld.CreateFAdd(ops[0], ops[1]);
         default:
            assert(0);
         }

      case ir_binop_sub:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_UINT:
         case GLSL_TYPE_INT:
            return bld.CreateSub(ops[0], ops[1]);
         case GLSL_TYPE_FLOAT:
            return bld.CreateFSub(ops[0], ops[1]);
         default:
            assert(0);
         }

      case ir_binop_mul:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
            return bld.CreateAnd(ops[0], ops[1]);
         case GLSL_TYPE_UINT:
         case GLSL_TYPE_INT:
            return bld.CreateMul(ops[0], ops[1]);
         case GLSL_TYPE_FLOAT:
            return bld.CreateFMul(ops[0], ops[1]);
         default:
            assert(0);
         }

      case ir_binop_div:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_UINT:
            return bld.CreateUDiv(ops[0], ops[1]);
         case GLSL_TYPE_INT:
            return bld.CreateSDiv(ops[0], ops[1]);
         case GLSL_TYPE_FLOAT:
            return bld.CreateFDiv(ops[0], ops[1]);
         default:
            assert(0);
         }

      case ir_binop_mod:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_UINT:
            return bld.CreateURem(ops[0], ops[1]);
         case GLSL_TYPE_INT:
            return bld.CreateSRem(ops[0], ops[1]);
         case GLSL_TYPE_FLOAT:
            return bld.CreateFRem(ops[0], ops[1]);
         default:
            assert(0);
         }

      case ir_binop_less:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_UINT:
            return bld.CreateICmpULT(ops[0], ops[1]);
         case GLSL_TYPE_INT:
            return bld.CreateICmpSLT(ops[0], ops[1]);
         case GLSL_TYPE_FLOAT:
            return bld.CreateFCmpOLT(ops[0], ops[1]);
         default:
            assert(0);
         }

      case ir_binop_greater:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_UINT:
            return bld.CreateICmpUGT(ops[0], ops[1]);
         case GLSL_TYPE_INT:
            return bld.CreateICmpSGT(ops[0], ops[1]);
         case GLSL_TYPE_FLOAT:
            return bld.CreateFCmpOGT(ops[0], ops[1]);
         default:
            assert(0);
         }

      case ir_binop_lequal:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_UINT:
            return bld.CreateICmpULE(ops[0], ops[1]);
         case GLSL_TYPE_INT:
            return bld.CreateICmpSLE(ops[0], ops[1]);
         case GLSL_TYPE_FLOAT:
            return bld.CreateFCmpOLE(ops[0], ops[1]);
         default:
            assert(0);
         }

      case ir_binop_gequal:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_UINT:
            return bld.CreateICmpUGE(ops[0], ops[1]);
         case GLSL_TYPE_INT:
            return bld.CreateICmpSGE(ops[0], ops[1]);
         case GLSL_TYPE_FLOAT:
            return bld.CreateFCmpOGE(ops[0], ops[1]);
         default:
            assert(0);
         }

      case ir_binop_equal:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_UINT:
         case GLSL_TYPE_INT:
            return bld.CreateICmpEQ(ops[0], ops[1]);
         case GLSL_TYPE_FLOAT:
            return bld.CreateFCmpOEQ(ops[0], ops[1]);
         default:
            assert(0);
         }
      case ir_binop_nequal:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_UINT:
         case GLSL_TYPE_INT:
            return bld.CreateICmpNE(ops[0], ops[1]);
         case GLSL_TYPE_FLOAT:
            return bld.CreateFCmpONE(ops[0], ops[1]);
         default:
            assert(0);
         }

      case ir_binop_logic_xor:
         assert(ir->operands[0]->type->base_type == GLSL_TYPE_BOOL);
         return bld.CreateICmpNE(ops[0], ops[1]);
      case ir_binop_logic_or:
         assert(ir->operands[0]->type->base_type == GLSL_TYPE_BOOL);
         return bld.CreateOr(ops[0], ops[1]);
      case ir_binop_logic_and:
         assert(ir->operands[0]->type->base_type == GLSL_TYPE_BOOL);
         return bld.CreateAnd(ops[0], ops[1]);

      case ir_binop_dot: {
         Value *prod = NULL;
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_UINT:
         case GLSL_TYPE_INT:
            prod = bld.CreateMul(ops[0], ops[1], "dot.mul");
            break;
         case GLSL_TYPE_FLOAT:
            prod = bld.CreateFMul(ops[0], ops[1], "dot.mul");
            break;
         default:
            assert(0);
         }

         if (ir->operands[0]->type->vector_elements <= 1)
            return prod;

         Value *sum = 0;
         for (unsigned i = 0; i < ir->operands[0]->type->vector_elements; ++i) {
            Value *elem = bld.CreateExtractElement(prod,
                                                   llvm_int(i),
                                                   "dot.elem");
            if (sum) {
               if (ir->operands[0]->type->base_type == GLSL_TYPE_FLOAT)
                  sum = bld.CreateFAdd(sum, elem, "dot.add");
               else
                  sum = bld.CreateAdd(sum, elem, "dot.add");
            }
            else
               sum = elem;
         }
         return sum;
      }

      case ir_unop_sqrt:
         assert(ir->operands[0]->type->base_type == GLSL_TYPE_FLOAT);
         return llvm_intrinsic(Intrinsic::sqrt, ops[0]);
      case ir_unop_rsq:
         assert(ir->operands[0]->type->base_type == GLSL_TYPE_FLOAT);
         return bld.CreateFDiv(llvm_imm(ops[0]->getType(), 1),
                               llvm_intrinsic(Intrinsic::sqrt, ops[0]),
                               "rsqrt.rcp");

      case ir_unop_i2f:
         return bld.CreateSIToFP(ops[0], llvm_type(ir->type));
      case ir_unop_u2f:
      case ir_unop_b2f:
         return bld.CreateUIToFP(ops[0], llvm_type(ir->type));
      case ir_unop_b2i:
         return bld.CreateZExt(ops[0], llvm_type(ir->type));
      case ir_unop_f2i:
         return bld.CreateFPToSI(ops[0], llvm_type(ir->type));
      case ir_unop_f2b:
         return bld.CreateFCmpONE(ops[0], llvm_imm(ops[0]->getType(), 0));
      case ir_unop_i2b:
         return bld.CreateICmpNE(ops[0], llvm_imm(ops[0]->getType(), 0));

      case ir_unop_trunc: {
         if (ir->operands[0]->type->base_type != GLSL_TYPE_FLOAT)
            return ops[0];
         glsl_type int_type = *ir->operands[0]->type;
         int_type.base_type = GLSL_TYPE_INT;
         return bld.CreateSIToFP(bld.CreateFPToSI(ops[0],
                                                  llvm_type(&int_type),
                                                  "trunc.fptosi"),
                                 ops[0]->getType(), "trunc.sitofp");
      }

      case ir_unop_floor: {
         if (ir->operands[0]->type->base_type != GLSL_TYPE_FLOAT)
            return ops[0];
         Value *one = llvm_imm(ops[0]->getType(), 1);
         return bld.CreateFSub(ops[0], bld.CreateFRem(ops[0], one));
      }

      case ir_unop_ceil: {
         if (ir->operands[0]->type->base_type != GLSL_TYPE_FLOAT)
            return ops[0];
         Value *one = llvm_imm(ops[0]->getType(), 1);
         return bld.CreateFAdd(bld.CreateFSub(ops[0],
                                              bld.CreateFRem(ops[0], one)),
                               one);
      }

      case ir_unop_fract: {
         if (ir->operands[0]->type->base_type != GLSL_TYPE_FLOAT)
            return llvm_imm(ops[0]->getType(), 0);
         Value *one = llvm_imm(ops[0]->getType(), 1);
         return bld.CreateFRem(ops[0], one);
      }

      /* TODO: NaNs might be wrong in min/max, not sure how to fix it */
      case ir_binop_min:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
            return bld.CreateAnd(ops[0], ops[1], "bmin");
         case GLSL_TYPE_UINT:
            return bld.CreateSelect(bld.CreateICmpULE(ops[0], ops[1],
                                                      "umin.le"),
                                    ops[0], ops[1], "umin.select");
         case GLSL_TYPE_INT:
            return bld.CreateSelect(bld.CreateICmpSLE(ops[0], ops[1],
                                                      "smin.le"),
                                    ops[0], ops[1], "smin.select");
         case GLSL_TYPE_FLOAT:
            return bld.CreateSelect(bld.CreateFCmpULE(ops[0], ops[1],
                                                      "fmin.le"),
                                    ops[0], ops[1], "fmin.select");
         default:
            assert(0);
         }

      case ir_binop_max:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
            return bld.CreateOr(ops[0], ops[1], "bmax");
         case GLSL_TYPE_UINT:
            return bld.CreateSelect(bld.CreateICmpUGE(ops[0], ops[1],
                                                      "umax.ge"),
                                    ops[0], ops[1], "umax.select");
         case GLSL_TYPE_INT:
            return bld.CreateSelect(bld.CreateICmpSGE(ops[0], ops[1],
                                                      "smax.ge"),
                                    ops[0], ops[1], "smax.select");
         case GLSL_TYPE_FLOAT:
            return bld.CreateSelect(bld.CreateFCmpUGE(ops[0], ops[1],
                                                      "fmax.ge"),
                                    ops[0], ops[1], "fmax.select");
         default:
            assert(0);
         }

      case ir_binop_pow:
         return llvm_intrinsic(Intrinsic::pow, ops[0], ops[1]);
         break;

      case ir_unop_bit_not:
         return bld.CreateNot(ops[0]);
      case ir_binop_bit_and:
         return bld.CreateAnd(ops[0], ops[1]);
      case ir_binop_bit_xor:
         return bld.CreateXor(ops[0], ops[1]);
      case ir_binop_bit_or:
         return bld.CreateOr(ops[0], ops[1]);

      case ir_binop_lshift:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_UINT:
         case GLSL_TYPE_INT:
            return bld.CreateLShr(ops[0], ops[1]);
         default:
            assert(0);
         }

      case ir_binop_rshift:
         switch (ir->operands[0]->type->base_type) {
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_UINT:
            return bld.CreateLShr(ops[0], ops[1]);
         case GLSL_TYPE_INT:
            return bld.CreateAShr(ops[0], ops[1]);
         default:
            assert(0);
            return 0;
         }

      default:
         assert(0);
         return 0;
      }
   }

   virtual void visit(class ir_expression *ir)
   {
      result = llvm_expression(ir);
   }

   virtual void visit(class ir_dereference_array *ir)
   {
      result = bld.CreateLoad(llvm_pointer(ir));
   }

   virtual void visit(class ir_dereference_record *ir)
   {
      result = bld.CreateLoad(llvm_pointer(ir));
   }

   virtual void visit(class ir_dereference_variable *ir)
   {
      result = bld.CreateLoad(llvm_pointer(ir));
   }

   virtual void visit(class ir_texture *ir)
   {
      // TODO
   }

   virtual void visit(class ir_discard *ir)
   {
      BasicBlock *discard = BasicBlock::Create(ctx, "discard", fun);
      BasicBlock *after;
      if (ir->condition) {
         after = BasicBlock::Create(ctx, "discard.survived", fun);
         bld.CreateCondBr(llvm_value(ir->condition), discard, after);
      }
      else {
         after = BasicBlock::Create(ctx, "dead_code.discard", fun);
         bld.CreateBr(discard);
      }

      bld.SetInsertPoint(discard);

      bb = after;
      bld.SetInsertPoint(bb);
   }

   virtual void visit(class ir_loop_jump *ir)
   {
      BasicBlock *target;

      switch (ir->mode) {
      case ir_loop_jump::jump_continue:
         target = loop.first;
         break;
      case ir_loop_jump::jump_break:
         target = loop.second;
         break;
      default:
         assert(!"not reached");
         target = NULL;
         break;
      }

      bld.CreateBr(target);

      bb = BasicBlock::Create(ctx, "dead_code.jump", fun);
      bld.SetInsertPoint(bb);
   }

   virtual void visit(class ir_loop *ir)
   {
      BasicBlock *body = BasicBlock::Create(ctx, "loop", fun);
      BasicBlock *header = body;
      BasicBlock *after = BasicBlock::Create(ctx, "loop.after", fun);
      Value *ctr = NULL;

      if (ir->counter) {
         ctr = llvm_variable(ir->counter);
         if (ir->from)
            bld.CreateStore(llvm_value(ir->from), ctr);
         if (ir->to)
            header = BasicBlock::Create(ctx, "loop.header", fun);
      }

      bld.CreateBr(header);

      if (ir->counter && ir->to) {
         bld.SetInsertPoint(header);
         Value *cond = NULL;
         Value *load = bld.CreateLoad(ctr);
         Value *to = llvm_value(ir->to);
         switch (ir->counter->type->base_type) {
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_UINT:
            cond = bld.CreateICmpULT(load, to);
            break;
         case GLSL_TYPE_INT:
            cond = bld.CreateICmpSLT(load, to);
            break;
         case GLSL_TYPE_FLOAT:
            cond = bld.CreateFCmpOLT(load, to);
            break;
         default:
            assert(0);
         }
         bld.CreateCondBr(cond, body, after);
      }

      bld.SetInsertPoint(body);

      std::pair<BasicBlock *, BasicBlock *> saved_loop = loop;
      loop = std::make_pair(header, after);
      visit_exec_list(&ir->body_instructions, this);
      loop = saved_loop;

      if (ir->counter && ir->increment) {
         switch (ir->counter->type->base_type) {
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_UINT:
         case GLSL_TYPE_INT:
            bld.CreateStore(bld.CreateAdd(bld.CreateLoad(ctr),
                                          llvm_value(ir->increment)), ctr);
            break;
         case GLSL_TYPE_FLOAT:
            bld.CreateStore(bld.CreateFAdd(bld.CreateLoad(ctr),
                                           llvm_value(ir->increment)), ctr);
            break;
         default:
            assert(0);
         }
      }
      bld.CreateBr(header);

      bb = after;
      bld.SetInsertPoint(bb);
   }

   virtual void visit(class ir_if *ir)
   {
      BasicBlock *bbt = BasicBlock::Create(ctx, "if", fun);
      BasicBlock *bbf = BasicBlock::Create(ctx, "else", fun);
      BasicBlock *bbe = BasicBlock::Create(ctx, "endif", fun);
      bld.CreateCondBr(llvm_value(ir->condition), bbt, bbf);

      bld.SetInsertPoint(bbt);
      visit_exec_list(&ir->then_instructions, this);
      bld.CreateBr(bbe);

      bld.SetInsertPoint(bbf);
      visit_exec_list(&ir->else_instructions, this);
      bld.CreateBr(bbe);

      bb = bbe;
      bld.SetInsertPoint(bb);
   }

   virtual void visit(class ir_return *ir)
   {
      if (!ir->value)
         bld.CreateRetVoid();
      else
         bld.CreateRet(llvm_value(ir->value));

      bb = BasicBlock::Create(ctx, "dead_code.return", fun);
      bld.SetInsertPoint(bb);
   }

   virtual void visit(class ir_call *ir)
   {
      std::vector<Value *> args;

      foreach_iter(exec_list_iterator, iter, *ir) {
         ir_rvalue *arg = (ir_constant *)iter.get();
         args.push_back(llvm_value(arg));
      }

      result = bld.CreateCall(llvm_function(ir->callee), args);

      AttrListPtr attr;
      ((CallInst *)result)->setAttributes(attr);
   }

   virtual void visit(class ir_constant *ir)
   {
      if (ir->type->base_type == GLSL_TYPE_STRUCT) {
         std::vector<Constant *> fields;
         foreach_iter(exec_list_iterator, iter, ir->components) {
            ir_constant *field = (ir_constant *)iter.get();
            fields.push_back(llvm_constant(field));
         }
         result = ConstantStruct::get((StructType *)llvm_type(ir->type), fields);
      }
      else if (ir->type->base_type == GLSL_TYPE_ARRAY) {
         std::vector<Constant *> elems;
         for (unsigned i = 0; i < ir->type->length; i++)
            elems.push_back(llvm_constant(ir->array_elements[i]));
         result = ConstantArray::get((ArrayType *)llvm_type(ir->type), elems);
      }
      else {
         Type *base_type = llvm_base_type(ir->type->base_type);
         Type *type = llvm_type(ir->type);

         std::vector<Constant *> vecs;
         unsigned idx = 0;
         for (unsigned i = 0; i < ir->type->matrix_columns; ++i) {
            std::vector<Constant *> elems;
            for (unsigned j = 0; j < ir->type->vector_elements; ++j) {
               Constant *elem;
               switch (ir->type->base_type) {
               case GLSL_TYPE_FLOAT:
                  elem = ConstantFP::get(base_type, ir->value.f[idx]);
                  break;
               case GLSL_TYPE_UINT:
                  elem = ConstantInt::get(base_type, ir->value.u[idx]);
                  break;
               case GLSL_TYPE_INT:
                  elem = ConstantInt::get(base_type, ir->value.i[idx]);
                  break;
               case GLSL_TYPE_BOOL:
                  elem = ConstantInt::get(base_type, ir->value.b[idx]);
                  break;
               default:
                  assert(0);
               }
               elems.push_back(elem);
               ++idx;
            }

            Constant *vec;
            if (ir->type->vector_elements > 1)
               vec = ConstantVector::get(elems);
            else
               vec = elems[0];
            vecs.push_back(vec);
         }

         if (ir->type->matrix_columns > 1)
            result = ConstantArray::get((ArrayType *)type, vecs);
         else
            result = vecs[0];
      }
   }

   Value *llvm_shuffle(Value *val, int *shuffle_mask,
                       unsigned res_width, const Twine &name = "")
   {
      Type *elem_type = val->getType();
      Type *res_type = elem_type;;
      unsigned val_width = 1;
      if (val->getType()->isVectorTy()) {
         val_width = ((VectorType *)val->getType())->getNumElements();
         elem_type = ((VectorType *)val->getType())->getElementType();
      }
      if (res_width > 1)
         res_type = VectorType::get(elem_type, res_width);

      std::vector<Constant *> shuffle_mask_values;
      assert(res_width <= 4);
      bool any_def = false;
      for (unsigned i = 0; i < res_width; ++i) {
         if (shuffle_mask[i] < 0)
            shuffle_mask_values.push_back(UndefValue::get(Type::getInt32Ty(ctx)));
         else {
            any_def = true;
            shuffle_mask_values.push_back(llvm_int(shuffle_mask[i]));
         }
      }

      Value *undef = UndefValue::get(res_type);
      if (!any_def)
         return undef;

      if (val_width > 1) {
         if (res_width > 1) {
            if (val_width == res_width) {
               bool nontrivial = false;
               for (unsigned i = 0; i < val_width; ++i) {
                  if (shuffle_mask[i] != (int)i)
                     nontrivial = true;
               }
               if (!nontrivial)
                  return val;
            }

            return bld.CreateShuffleVector(val,
                                           UndefValue::get(val->getType()),
                                           ConstantVector::get(shuffle_mask_values),
                                           name);
         }
         else
            return bld.CreateExtractElement(val, llvm_int(shuffle_mask[0]),
                                            name);
      }
      else {
         if (res_width > 1) {
            Value *tmp = undef;
            for (unsigned i = 0; i < res_width; ++i) {
               if (shuffle_mask[i] >= 0)
                  tmp = bld.CreateInsertElement(tmp, val, llvm_int(i), name);
            }
            return tmp;
         }
         else if (shuffle_mask[0] >= 0)
            return val;
         else
            return undef;
      }
   }

   virtual void visit(class ir_swizzle *swz)
   {
      Value *val = llvm_value(swz->val);
      int mask[4] = {
         (int)swz->mask.x,
         (int)swz->mask.y,
         (int)swz->mask.z,
         (int)swz->mask.w
      };
      result = llvm_shuffle(val, mask, swz->mask.num_components, "swizzle");
   }

   virtual void visit(class ir_assignment *ir)
   {
      Value *lhs = llvm_pointer(ir->lhs);
      Value *rhs = llvm_value(ir->rhs);
      unsigned width = ir->lhs->type->vector_elements;
      unsigned mask = (1 << width) - 1;
      assert(rhs);

      if (!(ir->write_mask & mask))
         return;

      if (ir->rhs->type->vector_elements < width) {
         int expand_mask[4] = {-1, -1, -1, -1};
         for (unsigned i = 0; i < ir->rhs->type->vector_elements; ++i)
            expand_mask[i] = i;
         rhs = llvm_shuffle(rhs, expand_mask, width, "assign.expand");
      }

      if (width > 1 && (ir->write_mask & mask) != mask) {
         Constant *blend_mask[4];
         for (unsigned i = 0; i < width; ++i) {
            if (ir->write_mask & (1 << i))
               blend_mask[i] = llvm_int(width + i);
            else
               blend_mask[i] = llvm_int(i);
         }
         rhs = bld.CreateShuffleVector(bld.CreateLoad(lhs), rhs,
                                       ConstantVector::get(blend_mask),
                                       "assign.writemask");
      }

      if (ir->condition)
         rhs = bld.CreateSelect(llvm_value(ir->condition), rhs,
                                bld.CreateLoad(lhs), "assign.conditional");

      bld.CreateStore(rhs, lhs);
   }

   virtual void visit(class ir_variable *var)
   {
      llvm_variable(var);
   }

   virtual void visit(ir_function_signature *sig)
   {
      if (!sig->is_defined)
         return;

      assert(!fun);
      fun = llvm_function(sig);

      bb = BasicBlock::Create(ctx, "entry", fun);
      bld.SetInsertPoint(bb);

      Function::arg_iterator ai = fun->arg_begin();
      foreach_iter(exec_list_iterator, iter, sig->parameters) {
         ir_variable *arg = (ir_variable *)iter.get();
         ai->setName(arg->name);
         bld.CreateStore(ai, llvm_variable(arg));
         ++ai;
      }

      foreach_iter(exec_list_iterator, iter, sig->body) {
         ir_instruction *ir = (ir_instruction *)iter.get();

         ir->accept(this);
      }

      if (fun->getReturnType()->isVoidTy())
         bld.CreateRetVoid();
      else
         bld.CreateRet(UndefValue::get(fun->getReturnType()));

      bb = NULL;
      fun = NULL;
   }

   virtual void visit(class ir_function *funs)
   {
      foreach_iter(exec_list_iterator, iter, *funs) {
         ir_function_signature *sig = (ir_function_signature *)iter.get();
         sig->accept(this);
      }
   }
};

struct Module *
glsl_ir_to_llvm_module(struct exec_list *ir)
{
   LLVMContext& ctx = getGlobalContext();
   Module *mod = new Module("glsl", ctx);
   ir_to_llvm_visitor v(ctx, mod);

   visit_exec_list(ir, &v);

   /* mod->dump(); */
   if (verifyModule(*mod, PrintMessageAction, 0)) {
      delete mod;
      return 0;
   }

   return mod;
}
