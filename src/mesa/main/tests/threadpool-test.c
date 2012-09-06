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

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "main/macros.h"
#include "main/mtypes.h"
#include "main/threadpool.h"

const int delay_usec = 10;

static void
set_true_func(void *data)
{
   bool *val = data;
   *val = true;
}

static void
slow_set_true_func(void *data)
{
   struct timeval start, end;
   bool *val = data;

   /* We use a busy wait to avoid sleeping for up to the scheduler interval on
    * some systems.  Better to burn CPU and return from the test faster.
    */
   gettimeofday(&start, NULL);
   do {
      gettimeofday(&end, NULL);
   } while (abs(end.tv_usec - start.tv_usec) < 10);

   *val = true;
}

static bool
test_setup_teardown()
{
   struct threadpool *pool;

   pool = _mesa_threadpool_create();
   _mesa_threadpool_destroy(pool);

   return true;
}

static bool
test_simple_wait()
{
   struct threadpool *pool;
   struct threadpool_task *task;
   bool completed = false;

   pool = _mesa_threadpool_create();
   task = _mesa_threadpool_queue_task(pool, set_true_func, &completed);
   _mesa_threadpool_wait_for_task(pool, &task);
   _mesa_threadpool_destroy(pool);

   return completed;
}

static bool
test_queue_many()
{
   struct threadpool *pool;
   struct threadpool_task *task[10];
   bool completed[10] = { false };
   bool pass = true;
   int i;

   pool = _mesa_threadpool_create();

   for (i = 0; i < 10; i++)
      task[i] = _mesa_threadpool_queue_task(pool, slow_set_true_func,
                                            &completed[i]);

   for (i = 0; i < 10; i++) {
      _mesa_threadpool_wait_for_task(pool, &task[i]);
      pass &= completed[i];
   }

   _mesa_threadpool_destroy(pool);

   return pass;
}


static bool
test_null_pool()
{
   struct threadpool *pool;
   struct threadpool_task *task;
   bool completed = false;

   pool = _mesa_threadpool_create();
   task = _mesa_threadpool_queue_task(NULL, set_true_func, &completed);
   _mesa_threadpool_wait_for_task(pool, &task);
   _mesa_threadpool_destroy(pool);

   _mesa_threadpool_destroy(NULL);

   return completed;
}


static bool
test_null_task()
{
   struct threadpool *pool;
   struct threadpool_task *task = NULL;

   pool = _mesa_threadpool_create();
   _mesa_threadpool_wait_for_task(pool, &task);
   _mesa_threadpool_wait_for_task(NULL, &task);
   _mesa_threadpool_destroy(pool);

   return true;
}

struct {
   bool (*test)(void);
   const char *name;
} tests[] = {
   { test_setup_teardown, "setup and teardown" },
   { test_simple_wait, "waiting on a single task" },
   { test_queue_many, "queueing many slow tasks" },
   { test_null_pool, "NULL pool argument" },
   { test_null_task, "NULL task argument" },
};

int
main(int argc, char **argv)
{
   bool pass = true;
   int i;

   for (i = 0; i < Elements(tests); i++) {
      bool result;
      printf("  Testing %s...", tests[i].name);
      result = tests[i].test();

      if (!result)
         printf(" FAIL");
      pass &= result;
      printf("\n");
   }

   return !pass;
}
