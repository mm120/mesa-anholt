/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright 2007 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 * Copyright © 2010 Intel Corporation
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

#include "main/mtypes.h"
#include "main/texobj.h"
#include "program/prog_instruction.h"
#include "program/prog_parameter.h"
#include "program/prog_cache.h"
#include "tnl/tnl.h"
#include "tnl/t_context.h"
#include "tnl/t_pipeline.h"

/** @file t_loopback_sw.c
 *
 * This file provides a draw_prims implementation that runs through
 * software TNL, calling back to draw_prims with a simpler VP.
 *
 * This may be used as a debugging aid to isolate hardware driver
 * failures, or possibly for performance reasons.
 */

#define MAX_INST (VERT_ATTRIB_MAX + 1)

static GLboolean
run_loopback_sw(GLcontext *ctx, struct tnl_pipeline_stage *stage)
{
   TNLcontext *tnl = TNL_CONTEXT(ctx);
   struct vertex_buffer *vb = &tnl->vb;
   GLuint p, a;
   struct _mesa_index_buffer ib, *ib_ptr;
   struct gl_vertex_program *vp = ctx->VertexProgram._Current;
   struct gl_client_array arrays[VERT_ATTRIB_MAX];
   const struct gl_client_array *inputs[VERT_ATTRIB_MAX];

   memset(&inputs, 0, sizeof(inputs));
   memset(&arrays, 0, sizeof(arrays));
   memset(&ib, 0, sizeof(ib));

   if (vb->Elts) {
      ib.ptr = vb->Elts;
      ib.type = GL_UNSIGNED_INT;
      ib.count = vb->Count;
      ib.obj = ctx->Shared->NullBufferObj;
      ib_ptr = &ib;
   } else {
      ib_ptr = NULL;
   }

   for (a = 0; a < VERT_ATTRIB_MAX; a++) {
      inputs[a] = &arrays[a];
      if (vp->Base.InputsRead & BITFIELD64_BIT(a)) {
	 switch (a) {
	 default:
	    arrays[a].Type = GL_FLOAT;
	    arrays[a].Size = vb->AttribPtr[a]->size;
	    arrays[a].StrideB = vb->AttribPtr[a]->stride;
	    arrays[a].Stride = vb->AttribPtr[a]->stride;
	    arrays[a]._MaxElement = vb->AttribPtr[a]->count;
	    arrays[a].Format = GL_RGBA;
	    arrays[a].Ptr = (void *)vb->AttribPtr[a]->data;
	    arrays[a].BufferObj = ctx->Shared->NullBufferObj;
	    arrays[a].Enabled = GL_TRUE;
	 }
      }
   }

   for (p = 0 ; p < vb->PrimitiveCount ; p++) {
      struct _mesa_prim prim = vb->Primitive[p];

      prim.basevertex = 0;
      tnl->loopback_draw_prims(ctx, inputs, &prim, 1,
			       ib_ptr, GL_TRUE, 0, 0);
   }

   return GL_FALSE;		/* finished the pipe */
}

const struct tnl_pipeline_stage _tnl_loopback_sw_stage =
{
   "loopback_sw",		/* name */
   NULL,			/* private data */
   NULL,			/* creator */
   NULL,			/* destructor */
   NULL,			/* validate */
   run_loopback_sw		/* run */
};

const struct tnl_pipeline_stage *_tnl_loopback_sw_pipeline[] = {
   &_tnl_vertex_program_stage,
   &_tnl_loopback_sw_stage,
   NULL
};

static struct prog_instruction *
get_next_inst(struct gl_vertex_program *vp)
{
   struct prog_instruction *inst;

   assert(vp->Base.NumInstructions < MAX_INST);
   inst = &vp->Base.Instructions[vp->Base.NumInstructions++];
   memset(inst, 0, sizeof(*inst));

   return inst;
}

static void
emit_mov_attr(struct gl_vertex_program *vp,
	      int input_index,
	      int output_index)
{
   struct prog_instruction *inst = get_next_inst(vp);

   vp->Base.InputsRead |= (1 << input_index);
   vp->Base.OutputsWritten |= (1 << output_index);

   inst->Opcode = OPCODE_MOV;
   inst->DstReg.CondMask = COND_TR;
   inst->DstReg.File = PROGRAM_OUTPUT;
   inst->DstReg.Index = output_index;
   inst->DstReg.WriteMask = WRITEMASK_XYZW;
   inst->SrcReg[0].File = PROGRAM_INPUT;
   inst->SrcReg[0].Index = input_index;
   inst->SrcReg[0].Swizzle = SWIZZLE_NOOP;
   inst->SrcReg[1].File = PROGRAM_UNDEFINED;
   inst->SrcReg[2].File = PROGRAM_UNDEFINED;
}

struct loopback_vp_key {
   GLbitfield64 vp_outputs_written;
};

static struct
gl_vertex_program *
tnl_loopback_sw_get_vp_for_key(GLcontext *ctx, struct loopback_vp_key *key)
{
   struct gl_vertex_program *vp;
   struct prog_instruction *inst;

   vp = (struct gl_vertex_program *)ctx->Driver.NewProgram(ctx,
							   GL_VERTEX_PROGRAM_ARB,
							   0);
   vp->Base.Parameters = _mesa_new_parameter_list();

   vp->Base.Instructions = _mesa_alloc_instructions(MAX_INST);
   if (!vp->Base.Instructions) {
      _mesa_error(NULL, GL_OUT_OF_MEMORY, "vertex program build");
      return NULL;
   }

   emit_mov_attr(vp, VERT_ATTRIB_POS, VERT_RESULT_HPOS);

   inst = get_next_inst(vp);
   inst->Opcode = OPCODE_END;
   inst->DstReg.CondMask = COND_TR;
   inst->DstReg.File = PROGRAM_UNDEFINED;
   inst->SrcReg[0].File = PROGRAM_UNDEFINED;
   inst->SrcReg[1].File = PROGRAM_UNDEFINED;
   inst->SrcReg[2].File = PROGRAM_UNDEFINED;

   return vp;
}

static struct gl_vertex_program *
tnl_loopback_sw_get_vp(GLcontext *ctx)
{
   TNLcontext *tnl = TNL_CONTEXT(ctx);
   struct gl_vertex_program *vp;
   struct loopback_vp_key key;

   memset(&key, 0, sizeof(key));
   key.vp_outputs_written = ctx->VertexProgram._Current->Base.OutputsWritten;

   if (!tnl->loopback_vp_cache)
      tnl->loopback_vp_cache = _mesa_new_program_cache();

   vp = (struct gl_vertex_program *)
      _mesa_search_program_cache(tnl->loopback_vp_cache, &key, sizeof(key));
   if (!vp) {
      vp = tnl_loopback_sw_get_vp_for_key(ctx, &key);
      _mesa_program_cache_insert(ctx, tnl->loopback_vp_cache, &key, sizeof(key),
				 &vp->Base);
   }
   return vp;
}

void
_tnl_loopback_sw_draw_prims(GLcontext *ctx,
			    const struct gl_client_array *arrays[],
			    const struct _mesa_prim *prim,
			    GLuint nr_prims,
			    const struct _mesa_index_buffer *ib,
			    GLboolean index_bounds_valid,
			    GLuint min_index,
			    GLuint max_index,
			    vbo_draw_func draw_prims)
{
   TNLcontext *tnl = TNL_CONTEXT(ctx);
   struct gl_vertex_program *vp, *old_vp;

   assert(ctx->VertexProgram._MaintainTnlProgram);
   tnl->loopback_draw_prims = draw_prims;

   vp = tnl_loopback_sw_get_vp(ctx);

   old_vp = ctx->VertexProgram._Current;
   ctx->VertexProgram._Current = vp;

   /* XXX: unset */
   _tnl_install_pipeline(ctx, _tnl_loopback_sw_pipeline);

   _mesa_lock_context_textures(ctx);
   _tnl_draw_prims(ctx, arrays, prim, nr_prims, ib, min_index, max_index);
   _mesa_unlock_context_textures(ctx);

   ctx->VertexProgram._Current = old_vp;
}
