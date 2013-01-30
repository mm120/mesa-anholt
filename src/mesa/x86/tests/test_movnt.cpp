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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <gtest/gtest.h>
#include <GL/gl.h>

extern "C" {
#include "x86/sse.h"
}

class test_movnt_write : public ::testing::Test {
};

TEST_F(test_movnt_write, movnt_write)
{
   uint8_t expected[64];
   uint8_t src[64];
   uint8_t dst[64];

   for (unsigned srco = 0; srco < 7; srco++) {
      for (unsigned dsto = 0; dsto < 7; dsto++) {
         for (unsigned len = 0; len < 32; len++) {
            /* Set up the data each time, just in case something bad happens
             * during the loop.
             */
            memset(dst, 0xd0, sizeof(dst));
            for (unsigned i = 0; i < sizeof(src); i++) {
               src[i] = i;
            }

            /* Set up the expected results. */
            memset(expected, 0xd0, sizeof(dst));
            for (unsigned i = 0; i < len; i++) {
               expected[dsto + i] = src[srco + i];
            }

            /* Run the test function */
            _mesa_sse_movnt_write(dst + dsto, src + srco, len);

            EXPECT_EQ(memcmp(expected, dst, sizeof(expected)), 0);
         }
      }
   }
}
