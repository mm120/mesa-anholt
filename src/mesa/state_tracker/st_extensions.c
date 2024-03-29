/**************************************************************************
 * 
 * Copyright 2007 VMware, Inc.
 * Copyright (c) 2008 VMware, Inc.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

#include "main/imports.h"
#include "main/context.h"
#include "main/macros.h"
#include "main/version.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"

#include "st_context.h"
#include "st_extensions.h"
#include "st_format.h"


/*
 * Note: we use these function rather than the MIN2, MAX2, CLAMP macros to
 * avoid evaluating arguments (which are often function calls) more than once.
 */

static unsigned _min(unsigned a, unsigned b)
{
   return (a < b) ? a : b;
}

static float _maxf(float a, float b)
{
   return (a > b) ? a : b;
}

static int _clamp(int a, int min, int max)
{
   if (a < min)
      return min;
   else if (a > max)
      return max;
   else
      return a;
}


/**
 * Query driver to get implementation limits.
 * Note that we have to limit/clamp against Mesa's internal limits too.
 */
void st_init_limits(struct st_context *st)
{
   struct pipe_screen *screen = st->pipe->screen;
   struct gl_constants *c = &st->ctx->Const;
   unsigned sh;
   boolean can_ubo = TRUE;

   c->MaxTextureLevels
      = _min(screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_2D_LEVELS),
            MAX_TEXTURE_LEVELS);

   c->Max3DTextureLevels
      = _min(screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_3D_LEVELS),
            MAX_3D_TEXTURE_LEVELS);

   c->MaxCubeTextureLevels
      = _min(screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS),
            MAX_CUBE_TEXTURE_LEVELS);

   c->MaxTextureRectSize
      = _min(1 << (c->MaxTextureLevels - 1), MAX_TEXTURE_RECT_SIZE);

   c->MaxArrayTextureLayers
      = screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS);

   /* Define max viewport size and max renderbuffer size in terms of
    * max texture size (note: max tex RECT size = max tex 2D size).
    * If this isn't true for some hardware we'll need new PIPE_CAP_ queries.
    */
   c->MaxViewportWidth =
   c->MaxViewportHeight =
   c->MaxRenderbufferSize = c->MaxTextureRectSize;

   c->MaxDrawBuffers = c->MaxColorAttachments =
      _clamp(screen->get_param(screen, PIPE_CAP_MAX_RENDER_TARGETS),
             1, MAX_DRAW_BUFFERS);

   c->MaxDualSourceDrawBuffers
      = _clamp(screen->get_param(screen, PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS),
              0, MAX_DRAW_BUFFERS);

   c->MaxLineWidth
      = _maxf(1.0f, screen->get_paramf(screen,
                                       PIPE_CAPF_MAX_LINE_WIDTH));
   c->MaxLineWidthAA
      = _maxf(1.0f, screen->get_paramf(screen,
                                       PIPE_CAPF_MAX_LINE_WIDTH_AA));

   c->MaxPointSize
      = _maxf(1.0f, screen->get_paramf(screen,
                                       PIPE_CAPF_MAX_POINT_WIDTH));
   c->MaxPointSizeAA
      = _maxf(1.0f, screen->get_paramf(screen,
                                       PIPE_CAPF_MAX_POINT_WIDTH_AA));
   /* called after _mesa_create_context/_mesa_init_point, fix default user
    * settable max point size up
    */
   st->ctx->Point.MaxSize = MAX2(c->MaxPointSize, c->MaxPointSizeAA);
   /* these are not queryable. Note that GL basically mandates a 1.0 minimum
    * for non-aa sizes, but we can go down to 0.0 for aa points.
    */
   c->MinPointSize = 1.0f;
   c->MinPointSizeAA = 0.0f;

   c->MaxTextureMaxAnisotropy
      = _maxf(2.0f, screen->get_paramf(screen,
                                 PIPE_CAPF_MAX_TEXTURE_ANISOTROPY));

   c->MaxTextureLodBias
      = screen->get_paramf(screen, PIPE_CAPF_MAX_TEXTURE_LOD_BIAS);

   c->QuadsFollowProvokingVertexConvention = screen->get_param(
      screen, PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION);

   c->MaxUniformBlockSize =
      screen->get_shader_param(screen, PIPE_SHADER_FRAGMENT,
                               PIPE_SHADER_CAP_MAX_CONSTS) * 16;
   if (c->MaxUniformBlockSize < 16384) {
      can_ubo = FALSE;
   }

   for (sh = 0; sh < PIPE_SHADER_TYPES; ++sh) {
      struct gl_shader_compiler_options *options;
      struct gl_program_constants *pc;

      switch (sh) {
      case PIPE_SHADER_FRAGMENT:
         pc = &c->Program[MESA_SHADER_FRAGMENT];
         options = &st->ctx->ShaderCompilerOptions[MESA_SHADER_FRAGMENT];
         break;
      case PIPE_SHADER_VERTEX:
         pc = &c->Program[MESA_SHADER_VERTEX];
         options = &st->ctx->ShaderCompilerOptions[MESA_SHADER_VERTEX];
         break;
      case PIPE_SHADER_GEOMETRY:
         pc = &c->Program[MESA_SHADER_GEOMETRY];
         options = &st->ctx->ShaderCompilerOptions[MESA_SHADER_GEOMETRY];
         break;
      default:
         /* compute shader, etc. */
         continue;
      }

      pc->MaxTextureImageUnits =
         _min(screen->get_shader_param(screen, sh,
                                       PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS),
              MAX_TEXTURE_IMAGE_UNITS);

      pc->MaxInstructions    = pc->MaxNativeInstructions    =
         screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_INSTRUCTIONS);
      pc->MaxAluInstructions = pc->MaxNativeAluInstructions =
         screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS);
      pc->MaxTexInstructions = pc->MaxNativeTexInstructions =
         screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS);
      pc->MaxTexIndirections = pc->MaxNativeTexIndirections =
         screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS);
      pc->MaxAttribs         = pc->MaxNativeAttribs         =
         screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_INPUTS);
      pc->MaxTemps           = pc->MaxNativeTemps           =
         screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_TEMPS);
      pc->MaxAddressRegs     = pc->MaxNativeAddressRegs     =
         _min(screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_ADDRS),
              MAX_PROGRAM_ADDRESS_REGS);
      pc->MaxParameters      = pc->MaxNativeParameters      =
         screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_CONSTS);

      pc->MaxUniformComponents = 4 * MIN2(pc->MaxNativeParameters, MAX_UNIFORMS);

      pc->MaxUniformBlocks =
         screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_CONST_BUFFERS);
      if (pc->MaxUniformBlocks)
         pc->MaxUniformBlocks -= 1; /* The first one is for ordinary uniforms. */
      pc->MaxUniformBlocks = _min(pc->MaxUniformBlocks, MAX_UNIFORM_BUFFERS);

      pc->MaxCombinedUniformComponents = (pc->MaxUniformComponents +
                                          c->MaxUniformBlockSize / 4 *
                                          pc->MaxUniformBlocks);

      /* Gallium doesn't really care about local vs. env parameters so use the
       * same limits.
       */
      pc->MaxLocalParams = MIN2(pc->MaxParameters, MAX_PROGRAM_LOCAL_PARAMS);
      pc->MaxEnvParams = MIN2(pc->MaxParameters, MAX_PROGRAM_ENV_PARAMS);

      options->EmitNoNoise = TRUE;

      /* TODO: make these more fine-grained if anyone needs it */
      options->MaxIfDepth = screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH);
      options->EmitNoLoops = !screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH);
      options->EmitNoFunctions = !screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_SUBROUTINES);
      options->EmitNoMainReturn = !screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_SUBROUTINES);

      options->EmitNoCont = !screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED);

      options->EmitNoIndirectInput = !screen->get_shader_param(screen, sh,
                                        PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR);
      options->EmitNoIndirectOutput = !screen->get_shader_param(screen, sh,
                                        PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR);
      options->EmitNoIndirectTemp = !screen->get_shader_param(screen, sh,
                                        PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR);
      options->EmitNoIndirectUniform = !screen->get_shader_param(screen, sh,
                                        PIPE_SHADER_CAP_INDIRECT_CONST_ADDR);

      if (pc->MaxNativeInstructions &&
          (options->EmitNoIndirectUniform || pc->MaxUniformBlocks < 12)) {
         can_ubo = FALSE;
      }

      if (options->EmitNoLoops)
         options->MaxUnrollIterations = MIN2(screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_INSTRUCTIONS), 65536);
      else
         options->MaxUnrollIterations = 255; /* SM3 limit */
      options->LowerClipDistance = true;
   }

   c->MaxCombinedTextureImageUnits =
         _min(c->Program[MESA_SHADER_VERTEX].MaxTextureImageUnits +
              c->Program[MESA_SHADER_GEOMETRY].MaxTextureImageUnits +
              c->Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits,
              MAX_COMBINED_TEXTURE_IMAGE_UNITS);

   /* This depends on program constants. */
   c->MaxTextureCoordUnits
      = _min(c->Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits, MAX_TEXTURE_COORD_UNITS);

   c->MaxTextureUnits = _min(c->Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits, c->MaxTextureCoordUnits);

   c->Program[MESA_SHADER_VERTEX].MaxAttribs = MIN2(c->Program[MESA_SHADER_VERTEX].MaxAttribs, 16);

   /* PIPE_SHADER_CAP_MAX_INPUTS for the FS specifies the maximum number
    * of inputs. It's always 2 colors + N generic inputs. */
   c->MaxVarying = screen->get_shader_param(screen, PIPE_SHADER_FRAGMENT,
                                            PIPE_SHADER_CAP_MAX_INPUTS);
   c->MaxVarying = MIN2(c->MaxVarying, MAX_VARYING);
   c->Program[MESA_SHADER_FRAGMENT].MaxInputComponents = c->MaxVarying * 4;
   c->Program[MESA_SHADER_VERTEX].MaxOutputComponents = c->MaxVarying * 4;
   c->Program[MESA_SHADER_GEOMETRY].MaxInputComponents = c->MaxVarying * 4;
   c->Program[MESA_SHADER_GEOMETRY].MaxOutputComponents = c->MaxVarying * 4;
   c->MaxGeometryOutputVertices = screen->get_param(screen, PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES);
   c->MaxGeometryTotalOutputComponents = screen->get_param(screen, PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS);

   c->MinProgramTexelOffset = screen->get_param(screen, PIPE_CAP_MIN_TEXEL_OFFSET);
   c->MaxProgramTexelOffset = screen->get_param(screen, PIPE_CAP_MAX_TEXEL_OFFSET);

   c->MaxProgramTextureGatherComponents = screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS);
   c->MinProgramTextureGatherOffset = screen->get_param(screen, PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET);
   c->MaxProgramTextureGatherOffset = screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET);

   c->UniformBooleanTrue = ~0;

   c->MaxTransformFeedbackBuffers =
      screen->get_param(screen, PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS);
   c->MaxTransformFeedbackBuffers = MIN2(c->MaxTransformFeedbackBuffers, MAX_FEEDBACK_BUFFERS);
   c->MaxTransformFeedbackSeparateComponents =
      screen->get_param(screen, PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS);
   c->MaxTransformFeedbackInterleavedComponents =
      screen->get_param(screen, PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS);

   c->StripTextureBorder = GL_TRUE;

   c->GLSLSkipStrictMaxUniformLimitCheck =
      screen->get_param(screen, PIPE_CAP_TGSI_CAN_COMPACT_CONSTANTS);

   if (can_ubo) {
      st->ctx->Extensions.ARB_uniform_buffer_object = GL_TRUE;
      c->UniformBufferOffsetAlignment =
         screen->get_param(screen, PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT);
      c->MaxCombinedUniformBlocks = c->MaxUniformBufferBindings =
         c->Program[MESA_SHADER_VERTEX].MaxUniformBlocks +
         c->Program[MESA_SHADER_GEOMETRY].MaxUniformBlocks +
         c->Program[MESA_SHADER_FRAGMENT].MaxUniformBlocks;
      assert(c->MaxCombinedUniformBlocks <= MAX_COMBINED_UNIFORM_BUFFERS);
   }
}


/**
 * Given a member \c x of struct gl_extensions, return offset of
 * \c x in bytes.
 */
#define o(x) offsetof(struct gl_extensions, x)


struct st_extension_cap_mapping {
   int extension_offset;
   int cap;
};

struct st_extension_format_mapping {
   int extension_offset[2];
   enum pipe_format format[8];

   /* If TRUE, at least one format must be supported for the extensions to be
    * advertised. If FALSE, all the formats must be supported. */
   GLboolean need_at_least_one;
};

/**
 * Enable extensions if certain pipe formats are supported by the driver.
 * What extensions will be enabled and what formats must be supported is
 * described by the array of st_extension_format_mapping.
 *
 * target and bind_flags are passed to is_format_supported.
 */
static void init_format_extensions(struct st_context *st,
                           const struct st_extension_format_mapping *mapping,
                           unsigned num_mappings,
                           enum pipe_texture_target target,
                           unsigned bind_flags)
{
   struct pipe_screen *screen = st->pipe->screen;
   GLboolean *extensions = (GLboolean *) &st->ctx->Extensions;
   unsigned i;
   int j;
   int num_formats = Elements(mapping->format);
   int num_ext = Elements(mapping->extension_offset);

   for (i = 0; i < num_mappings; i++) {
      int num_supported = 0;

      /* Examine each format in the list. */
      for (j = 0; j < num_formats && mapping[i].format[j]; j++) {
         if (screen->is_format_supported(screen, mapping[i].format[j],
                                         target, 0, bind_flags)) {
            num_supported++;
         }
      }

      if (!num_supported ||
          (!mapping[i].need_at_least_one && num_supported != j)) {
         continue;
      }

      /* Enable all extensions in the list. */
      for (j = 0; j < num_ext && mapping[i].extension_offset[j]; j++)
         extensions[mapping[i].extension_offset[j]] = GL_TRUE;
   }
}

/**
 * Use pipe_screen::get_param() to query PIPE_CAP_ values to determine
 * which GL extensions are supported.
 * Quite a few extensions are always supported because they are standard
 * features or can be built on top of other gallium features.
 * Some fine tuning may still be needed.
 */
void st_init_extensions(struct st_context *st)
{
   struct pipe_screen *screen = st->pipe->screen;
   struct gl_context *ctx = st->ctx;
   int i, glsl_feature_level;
   GLboolean *extensions = (GLboolean *) &ctx->Extensions;

   static const struct st_extension_cap_mapping cap_mapping[] = {
      { o(ARB_base_instance),                PIPE_CAP_START_INSTANCE                   },
      { o(ARB_buffer_storage),               PIPE_CAP_BUFFER_MAP_PERSISTENT_COHERENT },
      { o(ARB_depth_clamp),                  PIPE_CAP_DEPTH_CLIP_DISABLE               },
      { o(ARB_depth_texture),                PIPE_CAP_TEXTURE_SHADOW_MAP               },
      { o(ARB_draw_buffers_blend),           PIPE_CAP_INDEP_BLEND_FUNC                 },
      { o(ARB_draw_instanced),               PIPE_CAP_TGSI_INSTANCEID                  },
      { o(ARB_fragment_program_shadow),      PIPE_CAP_TEXTURE_SHADOW_MAP               },
      { o(ARB_instanced_arrays),             PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR  },
      { o(ARB_occlusion_query),              PIPE_CAP_OCCLUSION_QUERY                  },
      { o(ARB_occlusion_query2),             PIPE_CAP_OCCLUSION_QUERY                  },
      { o(ARB_point_sprite),                 PIPE_CAP_POINT_SPRITE                     },
      { o(ARB_seamless_cube_map),            PIPE_CAP_SEAMLESS_CUBE_MAP                },
      { o(ARB_shader_stencil_export),        PIPE_CAP_SHADER_STENCIL_EXPORT            },
      { o(ARB_shader_texture_lod),           PIPE_CAP_SM3                              },
      { o(ARB_shadow),                       PIPE_CAP_TEXTURE_SHADOW_MAP               },
      { o(ARB_texture_mirror_clamp_to_edge), PIPE_CAP_TEXTURE_MIRROR_CLAMP             },
      { o(ARB_texture_non_power_of_two),     PIPE_CAP_NPOT_TEXTURES                    },
      { o(ARB_timer_query),                  PIPE_CAP_QUERY_TIMESTAMP                  },
      { o(ARB_transform_feedback2),          PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME       },
      { o(ARB_transform_feedback3),          PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME       },

      { o(EXT_blend_equation_separate),      PIPE_CAP_BLEND_EQUATION_SEPARATE          },
      { o(EXT_draw_buffers2),                PIPE_CAP_INDEP_BLEND_ENABLE               },
      { o(EXT_stencil_two_side),             PIPE_CAP_TWO_SIDED_STENCIL                },
      { o(EXT_texture_array),                PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS         },
      { o(EXT_texture_filter_anisotropic),   PIPE_CAP_ANISOTROPIC_FILTER               },
      { o(EXT_texture_mirror_clamp),         PIPE_CAP_TEXTURE_MIRROR_CLAMP             },
      { o(EXT_texture_swizzle),              PIPE_CAP_TEXTURE_SWIZZLE                  },
      { o(EXT_transform_feedback),           PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS        },

      { o(AMD_seamless_cubemap_per_texture), PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE    },
      { o(ATI_separate_stencil),             PIPE_CAP_TWO_SIDED_STENCIL                },
      { o(ATI_texture_mirror_once),          PIPE_CAP_TEXTURE_MIRROR_CLAMP             },
      { o(NV_conditional_render),            PIPE_CAP_CONDITIONAL_RENDER               },
      { o(NV_texture_barrier),               PIPE_CAP_TEXTURE_BARRIER                  },
      /* GL_NV_point_sprite is not supported by gallium because we don't
       * support the GL_POINT_SPRITE_R_MODE_NV option. */

      { o(OES_standard_derivatives),         PIPE_CAP_SM3                              },
      { o(ARB_texture_cube_map_array),       PIPE_CAP_CUBE_MAP_ARRAY                   },
      { o(ARB_texture_multisample),          PIPE_CAP_TEXTURE_MULTISAMPLE              },
      { o(ARB_texture_query_lod),            PIPE_CAP_TEXTURE_QUERY_LOD                }
   };

   /* Required: render target and sampler support */
   static const struct st_extension_format_mapping rendertarget_mapping[] = {
      { { o(ARB_texture_float) },
        { PIPE_FORMAT_R32G32B32A32_FLOAT,
          PIPE_FORMAT_R16G16B16A16_FLOAT } },

      { { o(ARB_texture_rgb10_a2ui) },
        { PIPE_FORMAT_R10G10B10A2_UINT,
          PIPE_FORMAT_B10G10R10A2_UINT },
         GL_TRUE }, /* at least one format must be supported */

      { { o(EXT_framebuffer_sRGB) },
        { PIPE_FORMAT_A8B8G8R8_SRGB,
          PIPE_FORMAT_B8G8R8A8_SRGB },
         GL_TRUE }, /* at least one format must be supported */

      { { o(EXT_packed_float) },
        { PIPE_FORMAT_R11G11B10_FLOAT } },

      { { o(EXT_texture_integer) },
        { PIPE_FORMAT_R32G32B32A32_UINT,
          PIPE_FORMAT_R32G32B32A32_SINT } },

      { { o(ARB_texture_rg) },
        { PIPE_FORMAT_R8_UNORM,
          PIPE_FORMAT_R8G8_UNORM } },
   };

   /* Required: depth stencil and sampler support */
   static const struct st_extension_format_mapping depthstencil_mapping[] = {
      { { o(ARB_depth_buffer_float) },
        { PIPE_FORMAT_Z32_FLOAT,
          PIPE_FORMAT_Z32_FLOAT_S8X24_UINT } },
   };

   /* Required: sampler support */
   static const struct st_extension_format_mapping texture_mapping[] = {
      { { o(ARB_texture_compression_rgtc) },
        { PIPE_FORMAT_RGTC1_UNORM,
          PIPE_FORMAT_RGTC1_SNORM,
          PIPE_FORMAT_RGTC2_UNORM,
          PIPE_FORMAT_RGTC2_SNORM } },

      { { o(EXT_texture_compression_latc) },
        { PIPE_FORMAT_LATC1_UNORM,
          PIPE_FORMAT_LATC1_SNORM,
          PIPE_FORMAT_LATC2_UNORM,
          PIPE_FORMAT_LATC2_SNORM } },

      { { o(EXT_texture_compression_s3tc),
          o(ANGLE_texture_compression_dxt) },
        { PIPE_FORMAT_DXT1_RGB,
          PIPE_FORMAT_DXT1_RGBA,
          PIPE_FORMAT_DXT3_RGBA,
          PIPE_FORMAT_DXT5_RGBA } },

      { { o(EXT_texture_shared_exponent) },
        { PIPE_FORMAT_R9G9B9E5_FLOAT } },

      { { o(EXT_texture_snorm) },
        { PIPE_FORMAT_R8G8B8A8_SNORM } },

      { { o(EXT_texture_sRGB),
          o(EXT_texture_sRGB_decode) },
        { PIPE_FORMAT_A8B8G8R8_SRGB,
          PIPE_FORMAT_B8G8R8A8_SRGB },
        GL_TRUE }, /* at least one format must be supported */

      { { o(ATI_texture_compression_3dc) },
        { PIPE_FORMAT_LATC2_UNORM } },

      { { o(MESA_ycbcr_texture) },
        { PIPE_FORMAT_UYVY,
          PIPE_FORMAT_YUYV },
        GL_TRUE }, /* at least one format must be supported */

      { { o(OES_compressed_ETC1_RGB8_texture) },
        { PIPE_FORMAT_ETC1_RGB8 } },
   };

   /* Required: vertex fetch support. */
   static const struct st_extension_format_mapping vertex_mapping[] = {
      { { o(ARB_vertex_type_2_10_10_10_rev) },
        { PIPE_FORMAT_R10G10B10A2_UNORM,
          PIPE_FORMAT_B10G10R10A2_UNORM,
          PIPE_FORMAT_R10G10B10A2_SNORM,
          PIPE_FORMAT_B10G10R10A2_SNORM,
          PIPE_FORMAT_R10G10B10A2_USCALED,
          PIPE_FORMAT_B10G10R10A2_USCALED,
          PIPE_FORMAT_R10G10B10A2_SSCALED,
          PIPE_FORMAT_B10G10R10A2_SSCALED } },
      { { o(ARB_vertex_type_10f_11f_11f_rev) },
        { PIPE_FORMAT_R11G11B10_FLOAT } },
   };

   static const struct st_extension_format_mapping tbo_rgb32[] = {
      { {o(ARB_texture_buffer_object_rgb32) },
        { PIPE_FORMAT_R32G32B32_FLOAT,
          PIPE_FORMAT_R32G32B32_UINT,
          PIPE_FORMAT_R32G32B32_SINT,
        } },
   };

   /*
    * Extensions that are supported by all Gallium drivers:
    */
   ctx->Extensions.ARB_ES2_compatibility = GL_TRUE;
   ctx->Extensions.ARB_draw_elements_base_vertex = GL_TRUE;
   ctx->Extensions.ARB_explicit_attrib_location = GL_TRUE;
   ctx->Extensions.ARB_fragment_coord_conventions = GL_TRUE;
   ctx->Extensions.ARB_fragment_program = GL_TRUE;
   ctx->Extensions.ARB_fragment_shader = GL_TRUE;
   ctx->Extensions.ARB_half_float_vertex = GL_TRUE;
   ctx->Extensions.ARB_internalformat_query = GL_TRUE;
   ctx->Extensions.ARB_map_buffer_range = GL_TRUE;
   ctx->Extensions.ARB_texture_border_clamp = GL_TRUE; /* XXX temp */
   ctx->Extensions.ARB_texture_cube_map = GL_TRUE;
   ctx->Extensions.ARB_texture_env_combine = GL_TRUE;
   ctx->Extensions.ARB_texture_env_crossbar = GL_TRUE;
   ctx->Extensions.ARB_texture_env_dot3 = GL_TRUE;
   ctx->Extensions.ARB_vertex_program = GL_TRUE;
   ctx->Extensions.ARB_vertex_shader = GL_TRUE;

   ctx->Extensions.EXT_blend_color = GL_TRUE;
   ctx->Extensions.EXT_blend_func_separate = GL_TRUE;
   ctx->Extensions.EXT_blend_minmax = GL_TRUE;
   ctx->Extensions.EXT_gpu_program_parameters = GL_TRUE;
   ctx->Extensions.EXT_pixel_buffer_object = GL_TRUE;
   ctx->Extensions.EXT_point_parameters = GL_TRUE;
   ctx->Extensions.EXT_provoking_vertex = GL_TRUE;

   /* IMPORTANT:
    *    Don't enable EXT_separate_shader_objects. It disallows a certain
    *    optimization in the GLSL compiler and therefore is considered
    *    harmful.
    */
   ctx->Extensions.EXT_separate_shader_objects = GL_FALSE;

   ctx->Extensions.EXT_texture_env_dot3 = GL_TRUE;
   ctx->Extensions.EXT_vertex_array_bgra = GL_TRUE;

   ctx->Extensions.ATI_texture_env_combine3 = GL_TRUE;

   ctx->Extensions.MESA_pack_invert = GL_TRUE;

   ctx->Extensions.NV_fog_distance = GL_TRUE;
   ctx->Extensions.NV_texture_env_combine4 = GL_TRUE;
   ctx->Extensions.NV_texture_rectangle = GL_TRUE;
   ctx->Extensions.NV_vdpau_interop = GL_TRUE;

   ctx->Extensions.OES_EGL_image = GL_TRUE;
   ctx->Extensions.OES_EGL_image_external = GL_TRUE;
   ctx->Extensions.OES_draw_texture = GL_TRUE;

   /* Expose the extensions which directly correspond to gallium caps. */
   for (i = 0; i < Elements(cap_mapping); i++) {
      if (screen->get_param(screen, cap_mapping[i].cap)) {
         extensions[cap_mapping[i].extension_offset] = GL_TRUE;
      }
   }

   /* Expose the extensions which directly correspond to gallium formats. */
   init_format_extensions(st, rendertarget_mapping,
                          Elements(rendertarget_mapping), PIPE_TEXTURE_2D,
                          PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW);
   init_format_extensions(st, depthstencil_mapping,
                          Elements(depthstencil_mapping), PIPE_TEXTURE_2D,
                          PIPE_BIND_DEPTH_STENCIL | PIPE_BIND_SAMPLER_VIEW);
   init_format_extensions(st, texture_mapping, Elements(texture_mapping),
                          PIPE_TEXTURE_2D, PIPE_BIND_SAMPLER_VIEW);
   init_format_extensions(st, vertex_mapping, Elements(vertex_mapping),
                          PIPE_BUFFER, PIPE_BIND_VERTEX_BUFFER);

   /* Figure out GLSL support. */
   glsl_feature_level = screen->get_param(screen, PIPE_CAP_GLSL_FEATURE_LEVEL);

   ctx->Const.GLSLVersion = glsl_feature_level;
   if (glsl_feature_level >= 330)
      ctx->Const.GLSLVersion = 330;

   _mesa_override_glsl_version(st->ctx);

   if (st->options.force_glsl_version > 0 &&
       st->options.force_glsl_version <= ctx->Const.GLSLVersion) {
      ctx->Const.ForceGLSLVersion = st->options.force_glsl_version;
   }

   /* This extension needs full OpenGL 3.2, but we don't know if that's
    * supported at this point. Only check the GLSL version. */
   if (ctx->Const.GLSLVersion >= 150 &&
       screen->get_param(screen, PIPE_CAP_TGSI_VS_LAYER)) {
      ctx->Extensions.AMD_vertex_shader_layer = GL_TRUE;
   }

   if (ctx->Const.GLSLVersion >= 130) {
      ctx->Const.NativeIntegers = GL_TRUE;
      ctx->Const.MaxClipPlanes = 8;

      /* Extensions that either depend on GLSL 1.30 or are a subset thereof. */
      ctx->Extensions.ARB_conservative_depth = GL_TRUE;
      ctx->Extensions.ARB_shading_language_packing = GL_TRUE;
      ctx->Extensions.OES_depth_texture_cube_map = GL_TRUE;
      ctx->Extensions.ARB_shading_language_420pack = GL_TRUE;

      if (!st->options.disable_shader_bit_encoding) {
         ctx->Extensions.ARB_shader_bit_encoding = GL_TRUE;
      }
   } else {
      /* Optional integer support for GLSL 1.2. */
      if (screen->get_shader_param(screen, PIPE_SHADER_VERTEX,
                                   PIPE_SHADER_CAP_INTEGERS) &&
          screen->get_shader_param(screen, PIPE_SHADER_FRAGMENT,
                                   PIPE_SHADER_CAP_INTEGERS)) {
         ctx->Const.NativeIntegers = GL_TRUE;
      }
   }

   /* Below are the cases which cannot be moved into tables easily. */

   if (!ctx->Mesa_DXTn && !st->options.force_s3tc_enable) {
      ctx->Extensions.EXT_texture_compression_s3tc = GL_FALSE;
      ctx->Extensions.ANGLE_texture_compression_dxt = GL_FALSE;
   }

   if (screen->get_shader_param(screen, PIPE_SHADER_GEOMETRY,
                                PIPE_SHADER_CAP_MAX_INSTRUCTIONS) > 0) {
#if 0 /* XXX re-enable when GLSL compiler again supports geometry shaders */
      ctx->Extensions.ARB_geometry_shader4 = GL_TRUE;
#endif
   }

   ctx->Extensions.NV_primitive_restart = GL_TRUE;
   if (!screen->get_param(screen, PIPE_CAP_PRIMITIVE_RESTART)) {
      ctx->Const.PrimitiveRestartInSoftware = GL_TRUE;
   }

   /* ARB_color_buffer_float. */
   if (screen->get_param(screen, PIPE_CAP_VERTEX_COLOR_UNCLAMPED)) {
      ctx->Extensions.ARB_color_buffer_float = GL_TRUE;

      if (!screen->get_param(screen, PIPE_CAP_VERTEX_COLOR_CLAMPED)) {
         st->clamp_vert_color_in_shader = TRUE;
      }

      if (!screen->get_param(screen, PIPE_CAP_FRAGMENT_COLOR_CLAMPED)) {
         st->clamp_frag_color_in_shader = TRUE;
      }

      /* For drivers which cannot do color clamping, it's better to just
       * disable ARB_color_buffer_float in the core profile, because
       * the clamping is deprecated there anyway. */
      if (ctx->API == API_OPENGL_CORE &&
          (st->clamp_frag_color_in_shader || st->clamp_vert_color_in_shader)) {
         st->clamp_vert_color_in_shader = GL_FALSE;
         st->clamp_frag_color_in_shader = GL_FALSE;
         ctx->Extensions.ARB_color_buffer_float = GL_FALSE;
      }
   }

   if (screen->fence_finish) {
      ctx->Extensions.ARB_sync = GL_TRUE;
   }

   /* Maximum sample count. */
   for (i = 16; i > 0; --i) {
      enum pipe_format pformat = st_choose_format(st, GL_RGBA,
                                                  GL_NONE, GL_NONE,
                                                  PIPE_TEXTURE_2D, i,
                                                  PIPE_BIND_RENDER_TARGET, FALSE);
      if (pformat != PIPE_FORMAT_NONE) {
         ctx->Const.MaxSamples = i;
         ctx->Const.MaxColorTextureSamples = i;
         break;
      }
   }
   for (i = ctx->Const.MaxSamples; i > 0; --i) {
      enum pipe_format pformat = st_choose_format(st, GL_DEPTH_STENCIL,
                                                  GL_NONE, GL_NONE,
                                                  PIPE_TEXTURE_2D, i,
                                                  PIPE_BIND_DEPTH_STENCIL, FALSE);
      if (pformat != PIPE_FORMAT_NONE) {
         ctx->Const.MaxDepthTextureSamples = i;
         break;
      }
   }
   for (i = ctx->Const.MaxSamples; i > 0; --i) {
      enum pipe_format pformat = st_choose_format(st, GL_RGBA_INTEGER,
                                                  GL_NONE, GL_NONE,
                                                  PIPE_TEXTURE_2D, i,
                                                  PIPE_BIND_RENDER_TARGET, FALSE);
      if (pformat != PIPE_FORMAT_NONE) {
         ctx->Const.MaxIntegerSamples = i;
         break;
      }
   }
   if (ctx->Const.MaxSamples == 1) {
      /* one sample doesn't really make sense */
      ctx->Const.MaxSamples = 0;
   }
   else if (ctx->Const.MaxSamples >= 2) {
      ctx->Extensions.EXT_framebuffer_multisample = GL_TRUE;
      ctx->Extensions.EXT_framebuffer_multisample_blit_scaled = GL_TRUE;
   }

   if (ctx->Const.MaxSamples == 0 && screen->get_param(screen, PIPE_CAP_FAKE_SW_MSAA)) {
	ctx->Const.FakeSWMSAA = GL_TRUE;
        ctx->Extensions.EXT_framebuffer_multisample = GL_TRUE;
        ctx->Extensions.EXT_framebuffer_multisample_blit_scaled = GL_TRUE;
        ctx->Extensions.ARB_texture_multisample = GL_TRUE;
   }

   if (ctx->Const.MaxDualSourceDrawBuffers > 0 &&
       !st->options.disable_blend_func_extended)
      ctx->Extensions.ARB_blend_func_extended = GL_TRUE;

   st->has_time_elapsed =
      screen->get_param(screen, PIPE_CAP_QUERY_TIME_ELAPSED);

   if (st->has_time_elapsed ||
       ctx->Extensions.ARB_timer_query) {
      ctx->Extensions.EXT_timer_query = GL_TRUE;
   }

   if (ctx->Extensions.ARB_transform_feedback2 &&
       ctx->Extensions.ARB_draw_instanced) {
      ctx->Extensions.ARB_transform_feedback_instanced = GL_TRUE;
   }
   if (st->options.force_glsl_extensions_warn)
      ctx->Const.ForceGLSLExtensionsWarn = 1;

   if (st->options.disable_glsl_line_continuations)
      ctx->Const.DisableGLSLLineContinuations = 1;

   ctx->Const.MinMapBufferAlignment =
      screen->get_param(screen, PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT);

   if (screen->get_param(screen, PIPE_CAP_TEXTURE_BUFFER_OBJECTS)) {
      ctx->Extensions.ARB_texture_buffer_object = GL_TRUE;

      ctx->Const.MaxTextureBufferSize =
         _min(screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE),
              (1u << 31) - 1);
      ctx->Const.TextureBufferOffsetAlignment =
         screen->get_param(screen, PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT);

      if (ctx->Const.TextureBufferOffsetAlignment)
         ctx->Extensions.ARB_texture_buffer_range = GL_TRUE;

      init_format_extensions(st, tbo_rgb32, Elements(tbo_rgb32),
                             PIPE_BUFFER, PIPE_BIND_SAMPLER_VIEW);
   }

   if (screen->get_param(screen, PIPE_CAP_MIXED_FRAMEBUFFER_SIZES)) {
      ctx->Extensions.ARB_framebuffer_object = GL_TRUE;
   }

   /* Unpacking a varying in the fragment shader costs 1 texture indirection.
    * If the number of available texture indirections is very limited, then we
    * prefer to disable varying packing rather than run the risk of varying
    * packing preventing a shader from running.
    */
   if (screen->get_shader_param(screen, PIPE_SHADER_FRAGMENT,
                                PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS) <= 8) {
      /* We can't disable varying packing if transform feedback is available,
       * because transform feedback code assumes a packed varying layout.
       */
      if (!ctx->Extensions.EXT_transform_feedback)
         ctx->Const.DisableVaryingPacking = GL_TRUE;
   }

   if (ctx->API == API_OPENGL_CORE) {
      ctx->Const.MaxViewports = screen->get_param(screen, PIPE_CAP_MAX_VIEWPORTS);
      if (ctx->Const.MaxViewports >= 16) {
         ctx->Const.ViewportBounds.Min = -16384.0;
         ctx->Const.ViewportBounds.Max = 16384.0;
         ctx->Extensions.ARB_viewport_array = GL_TRUE;
      }
   }
   if (ctx->Const.MaxProgramTextureGatherComponents > 0)
      ctx->Extensions.ARB_texture_gather = GL_TRUE;
}
