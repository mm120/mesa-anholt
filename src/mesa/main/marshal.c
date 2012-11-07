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

/** \file marshal.c
 *
 * Functions related to marshalling GL calls from a client thread to a server
 * thread.
 */

#include "marshal.h"

#include <stdbool.h>

#include "dispatch.h"
#include "marshal_generated.h"
#include "threadpool.h"


#define ALIGN(value, alignment)  (((value) + alignment - 1) & ~(alignment - 1))


static bool execute_immediately = false;
static bool use_actual_threads = true;


static void
consume_command_queue(void *ctx);


/**
 * A single batch of commands queued up for later execution by a thread pool
 * task.
 */
struct gl_context_marshal_batch
{
   /**
    * Next batch of commands to execute after this batch, or NULL if this is
    * the last set of commands queued.  Protected by ctx->Marshal.Mutex.
    */
   struct gl_context_marshal_batch *Next;

   /**
    * Points to the first command in the batch.
    */
   uint32_t *Buffer;

   /**
    * Amount of data used by batch commands, in multiples of 32 bits.
    */
   size_t DwordsUsed;
};


#define BUFFER_SIZE_DWORDS 65536


static void
submit_batch(struct gl_context *ctx)
{
   if (ctx->Marshal.BatchPrep == NULL)
      return;

   _glthread_LOCK_MUTEX(ctx->Marshal.Mutex);

   /* TODO: wait if the queue is full. */

   *ctx->Marshal.Shared.BatchQueueTail = ctx->Marshal.BatchPrep;
   ctx->Marshal.Shared.BatchQueueTail = &ctx->Marshal.BatchPrep->Next;
   ctx->Marshal.BatchPrep = NULL;

   if (ctx->Marshal.Task != NULL) {
      if (ctx->Marshal.Shared.TaskComplete) {
         /* The task has already started to exit (or has already exited), so
          * it won't pick up the new batch.  We'll need to start a new task,
          * so tell the thread pool we're done with this one.
          */
         _glthread_UNLOCK_MUTEX(ctx->Marshal.Mutex);
         _mesa_threadpool_wait_for_task(ctx->Shared->MarshalThreadPool,
                                        &ctx->Marshal.Task);
      } else {
         /* The task is still running, so it will pick up the new batch.
          * Nothing more we need to do.
          */
         _glthread_UNLOCK_MUTEX(ctx->Marshal.Mutex);
         return;
      }
   } else {
      _glthread_UNLOCK_MUTEX(ctx->Marshal.Mutex);
   }

   /* Now ctx->Marshal.Task is NULL, so it is safe to set
    * ctx->Marshal.Shared.TaskComplete without re-acquiring the mutex, because
    * there is no background task to contend with.
    */
   ctx->Marshal.Shared.TaskComplete = GL_FALSE;

   if (use_actual_threads) {
      ctx->Marshal.Task =
         _mesa_threadpool_queue_task(ctx->Shared->MarshalThreadPool,
                                     consume_command_queue, ctx);
   } else {
      /* If we aren't using actual threads, execute the commands
       * immediately.
       */
      consume_command_queue(ctx);
   }
}


void *
_mesa_allocate_command_in_queue(struct gl_context *ctx,
                                enum marshal_dispatch_cmd_id cmd_id,
                                size_t size_bytes)
{
   struct marshal_cmd_base *cmd_base;
   size_t size_dwords = ALIGN(size_bytes, 4) / 4;

   assert(size_dwords <= 65535);
   assert(size_dwords <= BUFFER_SIZE_DWORDS);

   if (ctx->Marshal.BatchPrep != NULL &&
       ctx->Marshal.BatchPrep->DwordsUsed + size_dwords > BUFFER_SIZE_DWORDS) {
      submit_batch(ctx);
   }

   if (ctx->Marshal.BatchPrep == NULL) {
      /* TODO: how to handle memory allocation failure? */
      ctx->Marshal.BatchPrep =
         calloc(1, sizeof(struct gl_context_marshal_batch));
      ctx->Marshal.BatchPrep->Buffer = malloc(BUFFER_SIZE_DWORDS * 4);
   }

   cmd_base = (struct marshal_cmd_base *)
      &ctx->Marshal.BatchPrep->Buffer[ctx->Marshal.BatchPrep->DwordsUsed];
   ctx->Marshal.BatchPrep->DwordsUsed += size_dwords;
   cmd_base->cmd_id = cmd_id;
   cmd_base->cmd_size = size_dwords;
   return cmd_base;
}


static void
consume_command_queue(void *data)
{
   struct gl_context *ctx = (struct gl_context *) data;
   size_t pos;

   _glapi_set_context(ctx);
   ctx->Driver.SetBackgroundContext(ctx);

   _glthread_LOCK_MUTEX(ctx->Marshal.Mutex);

   while (ctx->Marshal.Shared.BatchQueue != NULL) {
      /* Remove the first batch from the queue. */
      struct gl_context_marshal_batch *batch = ctx->Marshal.Shared.BatchQueue;
      ctx->Marshal.Shared.BatchQueue = batch->Next;
      if (ctx->Marshal.Shared.BatchQueue == NULL)
         ctx->Marshal.Shared.BatchQueueTail = &ctx->Marshal.Shared.BatchQueue;

      /* Drop the mutex, execute it, and free it. */
      _glthread_UNLOCK_MUTEX(ctx->Marshal.Mutex);
      for (pos = 0; pos < batch->DwordsUsed; )
         pos += _mesa_unmarshal_dispatch_cmd(ctx, &batch->Buffer[pos]);
      assert(pos == batch->DwordsUsed);
      free(batch->Buffer);
      free(batch);

      _glthread_LOCK_MUTEX(ctx->Marshal.Mutex);
   }

   ctx->Marshal.Shared.TaskComplete = GL_TRUE;

   _glthread_UNLOCK_MUTEX(ctx->Marshal.Mutex);
}


void
_mesa_post_marshal_hook(struct gl_context *ctx)
{
   if (execute_immediately)
      submit_batch(ctx);
}


void
_mesa_marshal_synchronize(struct gl_context *ctx)
{
   submit_batch(ctx);

   if (ctx->Marshal.Task != NULL) {
      _mesa_threadpool_wait_for_task(ctx->Shared->MarshalThreadPool,
                                     &ctx->Marshal.Task);
   }
}
