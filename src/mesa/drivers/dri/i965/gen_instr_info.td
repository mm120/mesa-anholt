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

class inst<dag outs, dag ins, string asmstr, list<dag> pattern> : Instruction {
   field bits<32> Inst;
   field bits<32> bits1;
   field bits<32> bits2;
   field bits<32> bits3;

   bits<7> opcode;

   let Inst{6-0} = opcode;

   let Namespace = "gen";
   dag OutOperandList = outs;
   dag InOperandList = ins;
   let AsmString = asmstr;
   let Pattern = pattern;
}

class arith<bits<7> opcode_val, dag outs, dag ins, string asmstr, list<dag> pattern>
   : inst<outs, ins, asmstr, pattern> {
   let opcode = opcode_val;
}

def STUB_LOAD : arith<0,
    (outs fgrfs:$dst), (ins igrfs:$offset),
    "stub_load $offset, $dst",
    [(set fgrfs:$dst, (load igrfs:$offset))]>;

def STUB_STORE : arith<0,
    (outs), (ins fgrfs:$dst, igrfs:$offset),
    "stub_store $offset, $dst",
    [(store fgrfs:$dst, igrfs:$offset)]>;

def ADD : arith<64,
    (outs grfs:$dst), (ins grfs:$src1, grfs:$src2),
    "add $src1, $src2, $dst",
    [(set grfs:$dst, (fadd grfs:$src1, grfs:$src2))]>;

def MUL : arith<65,
    (outs grfs:$dst), (ins grfs:$src1, grfs:$src2),
    "mul $src1, $src2, $dst",
    [(set grfs:$dst, (fmul grfs:$src1, grfs:$src2))]>;

def MOV : arith<1,
    (outs grfs:$dst), (ins grfs:$src),
    "mov $src, $dst",
    []>;

def MOV_MRF_F : arith<1,
    (outs), (ins i32imm:$src0, grfs:$src1),
    "mov.mrf m$src0, $src1",
    [(int_gen_MOV_MRF_F imm:$src0, grfs:$src1)]>;
