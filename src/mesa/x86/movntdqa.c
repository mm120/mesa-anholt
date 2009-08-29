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

#include <smmintrin.h>

#include "main/mtypes.h"
#include "x86/sse.h"

/* Copies memory from src to dst, using SSE 4.1's MOVNTDQA to get streaming
 * read performance from uncached memory.  Requires src and dst to be 16-byte
 * aligned, and len to be 64-byte aligned.
 */
void _mesa_sse_movntdqa(void *dst, void *src, unsigned int len)
{
   unsigned int i;

   _mm_mfence();

   for (i = 0; i < len; i += 64) {
      __m128i *dst_cacheline = (__m128i *)((char *)dst + i);
      __m128i *src_cacheline = (__m128i *)((char *)src + i);
      __m128i temp1, temp2, temp3, temp4;

      temp1 = _mm_stream_load_si128(src_cacheline + 0);
      temp2 = _mm_stream_load_si128(src_cacheline + 1);
      temp3 = _mm_stream_load_si128(src_cacheline + 2);
      temp4 = _mm_stream_load_si128(src_cacheline + 3);
      _mm_store_si128(dst_cacheline + 0, temp1);
      _mm_store_si128(dst_cacheline + 1, temp2);
      _mm_store_si128(dst_cacheline + 2, temp3);
      _mm_store_si128(dst_cacheline + 3, temp4);
   }

   _mm_mfence();
}
