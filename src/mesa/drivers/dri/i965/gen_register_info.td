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

class gen_reg<string n> : Register<n> {
   field bits<2> file;
   field bits<8> nr;

   /* This is a C++ namespace that our classes and such will be under. */
   let Namespace = "gen";
}

/* The Rx registers are temporary registers, with no need to dcl them,
 * and are preserved between phases.
 */
class GRF<bits<8> in_nr, string n> : gen_reg<n> {
   let file = 1; /* BRW_GENERAL_REGISTER_FILE */
   let nr = in_nr;
}

/* Needs LLVM 3.2 */
/*
foreach nr = 0-127 in {
   def G#i : GRF<#i, "g#i">;
}
*/

def g0   : GRF<0,   "g0">;
def g1   : GRF<1,   "g1">;
def g2   : GRF<2,   "g2">;
def g3   : GRF<3,   "g3">;
def g4   : GRF<4,   "g4">;
def g5   : GRF<5,   "g5">;
def g6   : GRF<6,   "g6">;
def g7   : GRF<7,   "g7">;
def g8   : GRF<8,   "g8">;

/* This is the general purpose temporary register class. */
def igrfs : RegisterClass<"gen", [i32], 32, (sequence "g%u", 0, 8)>;
def fgrfs : RegisterClass<"gen", [f32], 32, (sequence "g%u", 0, 8)>;
def grfs : RegisterClass<"gen", [f32, i32], 32, (sequence "g%u", 0, 8)>;
