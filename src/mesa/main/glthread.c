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

/** @file glthread.c
 *
 * Support functions for the glthread feature of Mesa.
 *
 * In multicore systems, many applications end up CPU-bound with about half
 * their time spent inside their rendering thread and half inside Mesa.  To
 * alleviate this, we put a shim layer in Mesa at the GL dispatch level that
 * quickly logs the GL commands to a buffer to be processed by a worker
 * thread.
 */

#include "main/mtypes.h"
#include "main/glthread.h"
#include "main/marshal.h"
#include "main/marshal_generated.h"

#ifdef HAVE_PTHREAD

void *
_mesa_glthread_allocate_command(struct gl_context *ctx,
                                enum marshal_dispatch_cmd_id cmd_id,
                                size_t size)
{
   struct glthread_state *glthread = ctx->GLThread;
   struct marshal_cmd_base *cmd_base;

   assert(size <= MARSHAL_MAX_CMD_SIZE);

   if (glthread->batch != NULL &&
       glthread->batch->used + size > MARSHAL_MAX_CMD_SIZE) {
      _mesa_glthread_flush_batch(ctx);
   }

   if (glthread->batch == NULL) {
      /* TODO: handle memory allocation failure. */
      glthread->batch = calloc(1, sizeof(*glthread->batch));
      if (!glthread->batch)
         return NULL;
      glthread->batch->buffer = malloc(MARSHAL_MAX_CMD_SIZE);
   }

   cmd_base = (struct marshal_cmd_base *)
      &glthread->batch->buffer[glthread->batch->used];
   glthread->batch->used += size;
   cmd_base->cmd_id = cmd_id;
   cmd_base->cmd_size = size;
   return cmd_base;
}

static void
glthread_unmarshal_batch(struct gl_context *ctx, struct glthread_batch *batch)
{
   _glapi_set_dispatch(ctx->CurrentServerDispatch);

   free(batch->buffer);
   free(batch);
}

static void *
glthread_worker(void *data)
{
   struct gl_context *ctx = data;
   struct glthread_state *glthread = ctx->GLThread;

   ctx->Driver.SetBackgroundContext(ctx);
   _glapi_set_context(ctx);

   pthread_mutex_lock(&glthread->mutex);

   while (!glthread->shutdown) {
      /* Block (dropping the lock) until new work arrives for us. */
      while (!glthread->batch_queue && !glthread->shutdown) {
         pthread_cond_broadcast(&glthread->work_done);
         pthread_cond_wait(&glthread->new_work, &glthread->mutex);
      }

      if (glthread->shutdown) {
         pthread_mutex_unlock(&glthread->mutex);
         return NULL;
      }

      struct glthread_batch *batch = glthread->batch_queue;
      glthread->batch_queue = batch->next;
      if (glthread->batch_queue_tail == &batch->next)
         glthread->batch_queue_tail = &glthread->batch_queue;

      glthread->busy = true;
      pthread_mutex_unlock(&glthread->mutex);

      glthread_unmarshal_batch(ctx, batch);

      pthread_mutex_lock(&glthread->mutex);
      glthread->busy = false;
   }

   pthread_mutex_unlock(&glthread->mutex);

   return NULL;
}

void
_mesa_glthread_init(struct gl_context *ctx)
{
   struct glthread_state *glthread = calloc(1, sizeof(*glthread));

   if (!glthread)
      return;

   pthread_mutex_init(&glthread->mutex, NULL);
   pthread_cond_init(&glthread->new_work, NULL);
   pthread_cond_init(&glthread->work_done, NULL);

   pthread_create(&glthread->thread, NULL, glthread_worker, ctx);

   glthread->batch_queue_tail = &glthread->batch_queue;
   ctx->GLThread = glthread;
}

void
_mesa_glthread_destroy(struct gl_context *ctx)
{
   struct glthread_state *glthread = ctx->GLThread;

   if (!glthread)
      return;

   pthread_mutex_lock(&glthread->mutex);
   glthread->shutdown = true;
   pthread_cond_broadcast(&glthread->new_work);
   pthread_mutex_unlock(&glthread->mutex);

   pthread_join(glthread->thread, NULL);

   pthread_cond_destroy(&glthread->new_work);
   pthread_cond_destroy(&glthread->work_done);
   pthread_mutex_destroy(&glthread->mutex);
   free(glthread);
   ctx->GLThread = NULL;
}

void
_mesa_glthread_flush_batch(struct gl_context *ctx)
{
   struct glthread_state *glthread = ctx->GLThread;
   struct glthread_batch *batch;

   if (!glthread)
      return;

   batch = glthread->batch;
   if (!batch)
      return;
   glthread->batch = NULL;

   /* Debug: execute the batch immediately from this thread.
    *
    * Note that glthread_unmarshal_batch() changes the dispatch table so we'll
    * need to restore it when it returns.
    */
   if (false) {
      glthread_unmarshal_batch(ctx, batch);
      _glapi_set_dispatch(ctx->CurrentClientDispatch);
      return;
   }

   pthread_mutex_lock(&glthread->mutex);
   *glthread->batch_queue_tail = batch;
   glthread->batch_queue_tail = &batch->next;
   pthread_cond_broadcast(&glthread->new_work);
   pthread_mutex_unlock(&glthread->mutex);
}

/**
 * Waits for all pending batches have been unmarshaled.
 *
 * This can be used by the main thread to synchronize access to the context,
 * since the worker thread will be idle after this.
 */
void
_mesa_glthread_finish(struct gl_context *ctx)
{
   struct glthread_state *glthread = ctx->GLThread;

   if (!glthread)
      return;

   /* If this is called from the worker thread, then we've hit a path that
    * might be called from either the main thread or the worker (such as some
    * dri interface entrypoints), in which case we don't need to actually
    * synchronize against ourself.
    */
   if (pthread_self() == glthread->thread)
      return;

   _mesa_glthread_flush_batch(ctx);

   pthread_mutex_lock(&glthread->mutex);

   while (glthread->batch_queue || glthread->busy)
      pthread_cond_wait(&glthread->work_done, &glthread->mutex);

   pthread_mutex_unlock(&glthread->mutex);
}

#endif
