/*
 * Copyright © 2009 Intel Corporation
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
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include <string.h>
#if defined(USE_SSE_ASM) || defined(__x86_64__)
#pragma GCC push_options
#pragma GCC target ("sse2")
#include <emmintrin.h>
#endif

#include "main/mtypes.h"
#include "x86/sse.h"

/**
 * Streams data into *dst without polluting the CPU cache with the destination
 * cachelines.
 *
 * This can be useful for writing to memory that will immediately be DMAed
 * from next, which cause the data to be pulled out of the CPU's cache anway.
 *
 * This requires SSE2, which introduced the MOVNTI instruction
 * (_mm_stream_si32()).
 */
void _mesa_sse_movnt_write(void *in_dst, const void *in_src, unsigned int len)
{
   int *dst = in_dst;
   const int *src = in_src;

#if defined(USE_SSE_ASM) || defined(__x86_64__)
   _mm_mfence();
   while (len > 3) {
      _mm_stream_si32(dst, *src);
      len -= 4;
      dst++;
      src++;
   }
   _mm_mfence();
#endif /* USE_SSE_ASM */

   /* Finish off any remaining length not aligned to 4 bytes. */
   memcpy(dst, src, len);
}
#pragma GCC pop_options
