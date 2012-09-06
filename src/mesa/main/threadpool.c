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

#include "main/mtypes.h"
#include "main/threadpool.h"

static void *
threadpool_worker(void *data)
{
   struct threadpool *pool = data;

   pthread_mutex_lock(&pool->m);

   while (!pool->shutdown) {
      struct threadpool_task *task;

      /* Block (dropping the lock) until new work arrives for us. */
      while (!pool->workqueue && !pool->shutdown)
         pthread_cond_wait(&pool->new_work, &pool->m);

      if (pool->shutdown) {
         pthread_mutex_unlock(&pool->m);
         return NULL;
      }

      /* Pull the first task from the list.  We don't free it -- it now lacks
       * a reference other than the worker creator's, whose responsibility it
       * is to call threadpool_wait_for_work() to free it.
       */
      task = pool->workqueue;
      pool->workqueue = task->next;

      /* Call the task's work func. */
      pthread_mutex_unlock(&pool->m);
      task->work(task->data);
      task->finished = GL_TRUE;
      pthread_cond_broadcast(&task->finish);
      pthread_mutex_lock(&pool->m);
   }

   pthread_mutex_unlock(&pool->m);

   return NULL;
}

struct threadpool *
_mesa_threadpool_create(void)
{
   struct threadpool *pool = calloc(1, sizeof(*pool));
   int i;

   if (!pool)
      return NULL;

   pthread_mutex_init(&pool->m, NULL);
   pthread_cond_init(&pool->new_work, NULL);

   /* FINISHME: MAXTHREADS should be dynamically detected based on number of
    * CPUs.
    */
   for (i = 0; i < MAXTHREADS; i++) {
      /* FINISHME: we should delay thread creation until we have unconsumed
       * work and not all threads generated.
       */
      pthread_create(&pool->t[i], NULL, threadpool_worker, pool);
   }

   return pool;
}

void
_mesa_threadpool_destroy(struct threadpool *pool)
{
   int i;

   if (!pool)
      return;

   pthread_mutex_lock(&pool->m);
   pool->shutdown = GL_TRUE;
   pthread_cond_broadcast(&pool->new_work);
   pthread_mutex_unlock(&pool->m);

   for (i = 0; i < MAXTHREADS; i++) {
      pthread_join(pool->t[i], NULL);
   }

   pthread_cond_destroy(&pool->new_work);
   pthread_mutex_destroy(&pool->m);
   free(pool);
}

/**
 * Queues a request for the work function to be asynchronously executed by the
 * thread pool.
 *
 * The work func will get the "data" argument as its parameter -- any
 * communication between the caller and the work function will occur through
 * that.
 *
 * If there is an error, the work function is called immediately and NULL is
 * returned.
 */
struct threadpool_task *
_mesa_threadpool_queue_task(struct threadpool *pool,
                            threadpool_task_func work, void *data)
{
   struct threadpool_task *task;

   if (!pool) {
      work(data);
      return NULL;
   }

   task = calloc(1, sizeof(*task));
   if (!task) {
      work(data);
      return NULL;
   }

   task->work = work;
   task->data = data;
   pthread_cond_init(&task->finish, NULL);

   pthread_mutex_lock(&pool->m);
   /* FINISHME: This code means we LIFO.  That's pretty silly. */
   task->next = pool->workqueue;
   pool->workqueue = task;
   pthread_cond_signal(&pool->new_work);
   pthread_mutex_unlock(&pool->m);

   return task;
}

/**
 * Blocks on the completion of the given task and frees the task.
 */
void
_mesa_threadpool_wait_for_task(struct threadpool *pool,
                               struct threadpool_task **task_handle)
{
   struct threadpool_task *task = *task_handle;

   if (!pool || !task)
      return;

   pthread_mutex_lock(&pool->m);
   while (!task->finished)
      pthread_cond_wait(&task->finish, &pool->m);
   pthread_mutex_unlock(&pool->m);

   pthread_cond_destroy(&task->finish);
   free(task);
   *task_handle = NULL;
}
