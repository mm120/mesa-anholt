/*
 * Copyright © 2013 Intel Corporation
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
#include "program/prog_parameter.h"

#include "brw_context.h"
#include "brw_state.h"

/* Creates a new GS constant buffer reflecting the current GS program's
 * constants, if needed by the GS program.
 *
 * Otherwise, constants go through the CURBEs using the brw_constant_buffer
 * state atom.
 */
static void
brw_upload_gs_pull_constants(struct brw_context *brw)
{
   struct intel_context *intel = &brw->intel;
   /* BRW_NEW_GEOMETRY_PROGRAM */
   struct brw_geometry_program *gp =
      (struct brw_geometry_program *) brw->geometry_program;
   int i;

   if (!gp)
      return;

   /* Updates the ParamaterValues[i] pointers for all parameters of the
    * basic type of PROGRAM_STATE_VAR.
    */

   _mesa_load_state_parameters(&brw->intel.ctx, gp->program.Base.Parameters);

   /* CACHE_NEW_GS_PROG */
   if (!brw->vec4_gs.prog_data->base.nr_pull_params) {
      if (brw->gs.const_bo) {
	 drm_intel_bo_unreference(brw->gs.const_bo);
	 brw->gs.const_bo = NULL;
	 brw->gs.surf_offset[SURF_INDEX_GS_CONST_BUFFER] = 0;
	 brw->state.dirty.brw |= BRW_NEW_GS_CONSTBUF;
      }
      return;
   }

   /* _NEW_PROGRAM_CONSTANTS */
   drm_intel_bo_unreference(brw->gs.const_bo);
   uint32_t size = brw->vec4_gs.prog_data->base.nr_pull_params * 4;
   brw->gs.const_bo = drm_intel_bo_alloc(intel->bufmgr, "gp_const_buffer",
					 size, 64);

   drm_intel_gem_bo_map_gtt(brw->gs.const_bo);
   for (i = 0; i < brw->vec4_gs.prog_data->base.nr_pull_params; i++) {
      memcpy(brw->gs.const_bo->virtual + i * 4,
	     brw->vec4_gs.prog_data->base.pull_param[i],
	     4);
   }

   if (0) {
      for (i = 0; i < ALIGN(brw->vec4_gs.prog_data->base.nr_pull_params, 4) / 4;
           i++) {
	 float *row = (float *)brw->gs.const_bo->virtual + i * 4;
	 printf("gs const surface %3d: %4.3f %4.3f %4.3f %4.3f\n",
		i, row[0], row[1], row[2], row[3]);
      }
   }

   drm_intel_gem_bo_unmap_gtt(brw->gs.const_bo);

   const int surf = SURF_INDEX_GS_CONST_BUFFER;
   intel->vtbl.create_constant_surface(brw, brw->gs.const_bo, 0, size,
				       &brw->gs.surf_offset[surf], false);

   brw->state.dirty.brw |= BRW_NEW_GS_CONSTBUF;
}

const struct brw_tracked_state brw_gs_pull_constants = {
   .dirty = {
      .mesa = (_NEW_PROGRAM_CONSTANTS),
      .brw = (BRW_NEW_BATCH | BRW_NEW_GEOMETRY_PROGRAM),
      .cache = CACHE_NEW_GS_PROG,
   },
   .emit = brw_upload_gs_pull_constants,
};

static void
brw_upload_gs_ubo_surfaces(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->intel.ctx;
   /* _NEW_PROGRAM */
   struct gl_shader_program *prog = ctx->Shader.CurrentGeometryProgram;

   if (!prog)
      return;

   brw_upload_ubo_surfaces(brw, prog->_LinkedShaders[MESA_SHADER_GEOMETRY],
			   &brw->gs.surf_offset[SURF_INDEX_GS_UBO(0)]);
}

const struct brw_tracked_state brw_gs_ubo_surfaces = {
   .dirty = {
      .mesa = (_NEW_PROGRAM |
	       _NEW_BUFFER_OBJECT),
      .brw = BRW_NEW_BATCH,
      .cache = 0,
   },
   .emit = brw_upload_gs_ubo_surfaces,
};

/**
 * Constructs the binding table for the WM surface state, which maps unit
 * numbers to surface state objects.
 */
static void
brw_gs_upload_binding_table(struct brw_context *brw)
{
   uint32_t *bind;
   int i;

   /* If there's no GS, skip changing anything. */
   if (!brw->vec4_gs.prog_data)
      return;

   if (INTEL_DEBUG & DEBUG_SHADER_TIME) {
      gen7_create_shader_time_surface(brw, &brw->gs.surf_offset[SURF_INDEX_GS_SHADER_TIME]);

      assert(brw->vec4_gs.prog_data->base.num_surfaces
             <= SURF_INDEX_GS_SHADER_TIME);
      brw->vec4_gs.prog_data->base.num_surfaces = SURF_INDEX_GS_SHADER_TIME;
   }

   /* CACHE_NEW_GS_PROG: Skip making a binding table if we don't use textures or
    * pull constants.
    */
   if (brw->vec4_gs.prog_data->base.num_surfaces == 0) {
      if (brw->gs.bind_bo_offset != 0) {
	 brw->state.dirty.brw |= BRW_NEW_GS_BINDING_TABLE;
	 brw->gs.bind_bo_offset = 0;
      }
      return;
   }

   /* Might want to calculate nr_surfaces first, to avoid taking up so much
    * space for the binding table.
    */
   bind = brw_state_batch(brw, AUB_TRACE_BINDING_TABLE,
			  sizeof(uint32_t) * BRW_MAX_GS_SURFACES,
			  32, &brw->gs.bind_bo_offset);

   /* BRW_NEW_SURFACES and BRW_NEW_GS_CONSTBUF */
   for (i = 0; i < BRW_MAX_GS_SURFACES; i++) {
      bind[i] = brw->gs.surf_offset[i];
   }

   brw->state.dirty.brw |= BRW_NEW_GS_BINDING_TABLE;
}

const struct brw_tracked_state brw_gs_binding_table = {
   .dirty = {
      .mesa = 0,
      .brw = (BRW_NEW_BATCH |
	      BRW_NEW_GS_CONSTBUF |
	      BRW_NEW_SURFACES),
      .cache = CACHE_NEW_GS_PROG
   },
   .emit = brw_gs_upload_binding_table,
};
