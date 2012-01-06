/*
 * Copyright © 2011 Intel Corporation
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

/** @file brw_packed_floats_gen.c
 *
 * Used to generate the table of packed floats in brw_packed_floats.c, so it
 * doesn't have to be done at runtime.  See vol5c ("Execution Unit ISA") secton
 * 2.2.7 ("Packed 8-bit restricted float vector").
 *
 * Note that the documentation appears to be incorrect in its description of the
 * limits of the packed float format.  It says that the smallest representable
 * number is .125/-.125.  However, the values that would produce .125 and -.125
 * are special cased to be the +0/-0 values.  The actual smallest numbers are
 * 0.1328125 and -0.1328125 -- mantissa values of 1.
 *
 * gcc -Wall -o brw_packed_float_gen brw_packed_float_gen.c -lm
 */

#include <math.h>
#include <stdio.h>
#include <stdint.h>

int
main(int argc, char **argv)
{
   int packed;

   for (packed = 0; packed < 256; packed++) {
      int sbit = packed >> 7;
      int ebits = (packed >> 4) & 0x7;
      int mbits = packed & 0xf;

      int e = ebits - 3;

      float value = 1.0f;

      value += mbits / 16.0f;

      value *= exp2f(e);

      /* But special case 0. */
      if (packed == 0x80 || packed == 0)
	 value = 0.0;

      printf("   [0x%02x] = %s%.10f,\n", packed, sbit ? "-" : "", value);
   }

   return 0;
}
