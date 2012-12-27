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

/** \file marshal.h
 *
 * Declarations of functions related to marshalling GL calls from a client
 * thread to a server thread.
 */

#ifndef MARSHAL_H
#define MARSHAL_H

#include "main/context.h"


enum marshal_dispatch_cmd_id;


#define MARSHAL_MAX_CMD_SIZE 65535


struct marshal_cmd_base
{
   /**
    * Type of command.  See enum marshal_dispatch_cmd_id.
    */
   uint16_t cmd_id;

   /**
    * Size of command, in multiples of 4 bytes, including cmd_base.
    */
   uint16_t cmd_size;
};

struct marshal_cmd_ShaderSource;
struct marshal_cmd_Flush;
struct marshal_cmd_DrawElements;

struct _glapi_table *
_mesa_create_marshal_table(const struct gl_context *ctx);

size_t
_mesa_unmarshal_dispatch_cmd(struct gl_context *ctx, const void *cmd);

void GLAPIENTRY
_mesa_marshal_ShaderSource(GLuint shader, GLsizei count,
                           const GLchar * const *string, const GLint *length);

void GLAPIENTRY
_mesa_marshal_Flush(void);

void
_mesa_marshal_synchronize(struct gl_context *ctx);

void
_mesa_post_marshal_hook(struct gl_context *ctx);

void *
_mesa_allocate_command_in_queue(struct gl_context *ctx,
                                enum marshal_dispatch_cmd_id cmd_id,
                                size_t size_bytes);

void
_mesa_unmarshal_ShaderSource(struct gl_context *ctx,
                             const struct marshal_cmd_ShaderSource *cmd);

void
_mesa_unmarshal_Flush(struct gl_context *ctx,
                      const struct marshal_cmd_Flush *cmd);

void GLAPIENTRY
_mesa_marshal_DrawElements(GLenum mode, GLsizei count, GLenum type,
                           const GLvoid * indices);

void
_mesa_unmarshal_DrawElements(struct gl_context *ctx,
                             const struct marshal_cmd_DrawElements *cmd);

#endif /* MARSHAL_H */
