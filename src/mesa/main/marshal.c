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
      /* If we aren't using actual threads, execute the commands immediately.
       * Note that consume_command_queue() changes the dispatch table so we'll
       * need to restore it when it returns.
       */
      consume_command_queue(ctx);
      _glapi_set_dispatch(ctx->CurrentClientDispatch);
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
   _glapi_set_dispatch(ctx->CurrentServerDispatch);
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


struct marshal_cmd_Flush
{
   struct marshal_cmd_base cmd_base;
};


void
_mesa_unmarshal_Flush(struct gl_context *ctx,
                      const struct marshal_cmd_Flush *cmd)
{
   CALL_Flush(ctx->CurrentServerDispatch, ());
}


void GLAPIENTRY
_mesa_marshal_Flush(void)
{
   GET_CURRENT_CONTEXT(ctx);
   struct marshal_cmd_Flush *cmd =
      _mesa_allocate_command_in_queue(ctx, DISPATCH_CMD_Flush,
                                      sizeof(struct marshal_cmd_Flush));
   (void) cmd;
   _mesa_post_marshal_hook(ctx);

   /* Flush() needs to be handled specially.  In addition to telling the
    * background thread to flush, we need to ensure that our own buffer is
    * submitted to the background thread so that it will complete in a finite
    * amount of time.
    */
   submit_batch(ctx);
}


struct marshal_cmd_ShaderSource
{
   struct marshal_cmd_base cmd_base;
   GLuint shader;
   GLsizei count;
   /* Followed by GLint length[count], then the contents of all strings,
    * concatenated.
    */
};


void
_mesa_unmarshal_ShaderSource(struct gl_context *ctx,
                             const struct marshal_cmd_ShaderSource *cmd)
{
   const GLint *cmd_length = (const GLint *) (cmd + 1);
   const GLchar *cmd_strings = (const GLchar *) (cmd_length + cmd->count);
   /* TODO: how to deal with malloc failure? */
   const GLchar * *string = malloc(cmd->count * sizeof(const GLchar *));
   int i;

   for (i = 0; i < cmd->count; ++i) {
      string[i] = cmd_strings;
      cmd_strings += cmd_length[i];
   }
   CALL_ShaderSource(ctx->CurrentServerDispatch,
                     (cmd->shader, cmd->count, string, cmd_length));
   free(string);
}


static size_t
measure_ShaderSource_strings(GLsizei count, const GLchar * const *string,
                             const GLint *length_in, GLint *length_out)
{
   int i;
   size_t total_string_length = 0;

   for (i = 0; i < count; ++i) {
      if (length_in == NULL || length_in[i] < 0)
         length_out[i] = strlen(string[i]);
      else
         length_out[i] = length_in[i];
      total_string_length += length_out[i];
   }
   return total_string_length;
}


void GLAPIENTRY
_mesa_marshal_ShaderSource(GLuint shader, GLsizei count,
                           const GLchar * const *string, const GLint *length)
{
   /* TODO: how to report an error if count < 0? */

   GET_CURRENT_CONTEXT(ctx);
   /* TODO: how to deal with malloc failure? */
   const size_t fixed_cmd_size = sizeof(struct marshal_cmd_ShaderSource);
   STATIC_ASSERT(fixed_cmd_size % sizeof(GLint) == 0);
   size_t length_size = count * sizeof(GLint);
   GLint *length_tmp = malloc(length_size);
   size_t total_string_length =
      measure_ShaderSource_strings(count, string, length, length_tmp);
   size_t total_cmd_size = fixed_cmd_size + length_size + total_string_length;

   if (total_cmd_size <= MARSHAL_MAX_CMD_SIZE) {
      struct marshal_cmd_ShaderSource *cmd =
         _mesa_allocate_command_in_queue(ctx, DISPATCH_CMD_ShaderSource,
                                         total_cmd_size);
      GLint *cmd_length = (GLint *) (cmd + 1);
      GLchar *cmd_strings = (GLchar *) (cmd_length + count);
      int i;

      cmd->shader = shader;
      cmd->count = count;
      memcpy(cmd_length, length_tmp, length_size);
      for (i = 0; i < count; ++i) {
         memcpy(cmd_strings, string[i], cmd_length[i]);
         cmd_strings += cmd_length[i];
      }
      _mesa_post_marshal_hook(ctx);
   } else {
      _mesa_marshal_synchronize(ctx);
      CALL_ShaderSource(ctx->CurrentServerDispatch,
                        (shader, count, string, length_tmp));
   }
   free(length_tmp);
}


/* DrawElements when marshalled synchronously */
static void GLAPIENTRY
_mesa_marshal_DrawElements_sync(GLenum mode, GLsizei count, GLenum type,
                                const GLvoid * indices)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_marshal_synchronize(ctx);
   /*SYNC_EXECUTE_HOOK(DrawElements);*/
   CALL_DrawElements(ctx->CurrentServerDispatch, (mode, count, type, indices));
}

/* DrawElements when marshalled asynchronously */
struct marshal_cmd_DrawElements
{
   struct marshal_cmd_base cmd_base;
   GLenum mode;
   GLsizei count;
   GLenum type;
   const GLvoid * indices;
};

void
_mesa_unmarshal_DrawElements(struct gl_context *ctx,
                             const struct marshal_cmd_DrawElements *cmd)
{
   const GLenum mode = cmd->mode;
   const GLsizei count = cmd->count;
   const GLenum type = cmd->type;
   const const GLvoid * indices = cmd->indices;
   CALL_DrawElements(ctx->CurrentServerDispatch, (mode, count, type, indices));
}

void GLAPIENTRY
_mesa_marshal_DrawElements(GLenum mode, GLsizei count, GLenum type,
                           const GLvoid * indices)
{
   GET_CURRENT_CONTEXT(ctx);

   /* In legacy GL, attribute pointers (glVertexPointer, etc.) could point at
    * bare user memory, in which case we would have to actually copy their
    * memory at the moment of this draw call.  Just do it synchronously for
    * those types of contexts to avoid all that logic.
    */
   if (ctx->API != API_OPENGL_CORE &&
       ctx->API != API_OPENGLES2) {
      _mesa_marshal_DrawElements_sync(mode, count, type, indices);
      return;
   }

   /*ASYNC_MARSHAL_HOOK(DrawElements);*/
   size_t cmd_size = sizeof(struct marshal_cmd_DrawElements);
   struct marshal_cmd_DrawElements *cmd =
      _mesa_allocate_command_in_queue(ctx, DISPATCH_CMD_DrawElements, cmd_size);
   cmd->mode = mode;
   cmd->count = count;
   cmd->type = type;
   cmd->indices = indices;
   _mesa_post_marshal_hook(ctx);
}

