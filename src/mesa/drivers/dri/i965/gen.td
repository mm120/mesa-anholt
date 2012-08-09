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

include "llvm/Target/Target.td"

def gen_instr_info : InstrInfo {}

class Proc<string Name, list<SubtargetFeature> Features>
   : Processor<Name, NoItineraries, Features>;

def feature_gen6 : SubtargetFeature<"gen6", "gen", "6",
    "Enable gen6 features">;
def feature_gen7 : SubtargetFeature<"gen7", "gen", "7",
    "Enable gen7 features">;

def : Proc<"gen6", [feature_gen6]>;
def : Proc<"gen7", [feature_gen7]>;

def gen : Target {
   let InstructionSet = gen_instr_info;
}

include "gen_register_info.td"
include "gen_instr_info.td"
