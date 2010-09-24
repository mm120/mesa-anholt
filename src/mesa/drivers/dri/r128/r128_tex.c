/**************************************************************************

Copyright 1999, 2000 ATI Technologies Inc. and Precision Insight, Inc.,
                                               Cedar Park, Texas.
All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
on the rights to use, copy, modify, merge, publish, distribute, sub
license, and/or sell copies of the Software, and to permit persons to whom
the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
ATI, PRECISION INSIGHT AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

/*
 * Authors:
 *   Gareth Hughes <gareth@valinux.com>
 *   Kevin E. Martin <martin@valinux.com>
 *   Brian Paul <brianp@valinux.com>
 */

#include "r128_context.h"
#include "r128_ioctl.h"
#include "r128_tex.h"
#include "r128_texobj.h"

#include "main/simple_list.h"
#include "main/enums.h"
#include "main/texstore.h"
#include "main/teximage.h"
#include "main/texobj.h"
#include "main/imports.h"
#include "main/texobj.h"
#include "main/macros.h"

#include "xmlpool.h"

#define TEX_0	1
#define TEX_1	2


/**
 * Set the texture wrap modes.  Currently \c GL_REPEAT, \c GL_CLAMP,
 * \c GL_CLAMP_TO_EDGE, and \c GL_MIRRORED_REPEAT are supported.
 * 
 * \param t Texture object whose wrap modes are to be set
 * \param swrap Wrap mode for the \a s texture coordinate
 * \param twrap Wrap mode for the \a t texture coordinate
 */
static void r128SetTexWrap( r128TexObjPtr t, GLenum swrap, GLenum twrap )
{
   t->setup.tex_cntl &= ~(R128_TEX_CLAMP_S_MASK | R128_TEX_CLAMP_T_MASK);

   switch ( swrap ) {
   case GL_CLAMP:
      t->setup.tex_cntl |= R128_TEX_CLAMP_S_BORDER_COLOR;
      break;
   case GL_CLAMP_TO_EDGE:
      t->setup.tex_cntl |= R128_TEX_CLAMP_S_CLAMP;
      break;
   case GL_REPEAT:
      t->setup.tex_cntl |= R128_TEX_CLAMP_S_WRAP;
      break;
   case GL_MIRRORED_REPEAT:
      t->setup.tex_cntl |= R128_TEX_CLAMP_S_MIRROR;
      break;
   }

   switch ( twrap ) {
   case GL_CLAMP:
      t->setup.tex_cntl |= R128_TEX_CLAMP_T_BORDER_COLOR;
      break;
   case GL_CLAMP_TO_EDGE:
      t->setup.tex_cntl |= R128_TEX_CLAMP_T_CLAMP;
      break;
   case GL_REPEAT:
      t->setup.tex_cntl |= R128_TEX_CLAMP_T_WRAP;
      break;
   case GL_MIRRORED_REPEAT:
      t->setup.tex_cntl |= R128_TEX_CLAMP_T_MIRROR;
      break;
   }
}

static void r128SetTexFilter( r128TexObjPtr t, GLenum minf, GLenum magf )
{
   t->setup.tex_cntl &= ~(R128_MIN_BLEND_MASK | R128_MAG_BLEND_MASK);

   switch ( minf ) {
   case GL_NEAREST:
      t->setup.tex_cntl |= R128_MIN_BLEND_NEAREST;
      break;
   case GL_LINEAR:
      t->setup.tex_cntl |= R128_MIN_BLEND_LINEAR;
      break;
   case GL_NEAREST_MIPMAP_NEAREST:
      t->setup.tex_cntl |= R128_MIN_BLEND_MIPNEAREST;
      break;
   case GL_LINEAR_MIPMAP_NEAREST:
      t->setup.tex_cntl |= R128_MIN_BLEND_MIPLINEAR;
      break;
   case GL_NEAREST_MIPMAP_LINEAR:
      t->setup.tex_cntl |= R128_MIN_BLEND_LINEARMIPNEAREST;
      break;
   case GL_LINEAR_MIPMAP_LINEAR:
      t->setup.tex_cntl |= R128_MIN_BLEND_LINEARMIPLINEAR;
      break;
   }

   switch ( magf ) {
   case GL_NEAREST:
      t->setup.tex_cntl |= R128_MAG_BLEND_NEAREST;
      break;
   case GL_LINEAR:
      t->setup.tex_cntl |= R128_MAG_BLEND_LINEAR;
      break;
   }
}

static void r128SetTexBorderColor( r128TexObjPtr t, const GLfloat color[4] )
{
   GLubyte c[4];
   CLAMPED_FLOAT_TO_UBYTE(c[0], color[0]);
   CLAMPED_FLOAT_TO_UBYTE(c[1], color[1]);
   CLAMPED_FLOAT_TO_UBYTE(c[2], color[2]);
   CLAMPED_FLOAT_TO_UBYTE(c[3], color[3]);
   t->setup.tex_border_color = r128PackColor( 4, c[0], c[1], c[2], c[3] );
}


static r128TexObjPtr r128AllocTexObj( struct gl_texture_object *texObj )
{
   r128TexObjPtr t;

   if ( R128_DEBUG & DEBUG_VERBOSE_API ) {
      fprintf( stderr, "%s( %p )\n", __FUNCTION__, (void *) texObj );
   }

   t = (r128TexObjPtr) CALLOC_STRUCT( r128_tex_obj );
   texObj->DriverData = t;
   if ( t != NULL ) {

      /* Initialize non-image-dependent parts of the state:
       */
      t->base.tObj = texObj;

      /* FIXME Something here to set initial values for other parts of
       * FIXME t->setup?
       */
  
      make_empty_list( (driTextureObject *) t );

      r128SetTexWrap( t, texObj->WrapS, texObj->WrapT );
      r128SetTexFilter( t, texObj->MinFilter, texObj->MagFilter );
      r128SetTexBorderColor( t, texObj->BorderColor.f );
   }

   return t;
}

static void r128TexImage1D( GLcontext *ctx, GLenum target, GLint level,
			    GLint internalFormat,
			    GLint width, GLint border,
			    GLenum format, GLenum type, const GLvoid *pixels,
			    const struct gl_pixelstore_attrib *packing,
			    struct gl_texture_object *texObj,
			    struct gl_texture_image *texImage )
{
   driTextureObject * t = (driTextureObject *) texObj->DriverData;

   if ( t ) {
      driSwapOutTextureObject( t );
   }
   else {
      t = (driTextureObject *) r128AllocTexObj(texObj);
      if (!t) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glTexImage1D");
         return;
      }
   }

   /* Note, this will call r128ChooseTextureFormat */
   _mesa_store_teximage1d( ctx, target, level, internalFormat,
			   width, border, format, type,
			   pixels, packing, texObj, texImage );

   t->dirty_images[0] |= (1 << level);
}


static void r128TexSubImage1D( GLcontext *ctx,
			       GLenum target,
			       GLint level,
			       GLint xoffset,
			       GLsizei width,
			       GLenum format, GLenum type,
			       const GLvoid *pixels,
			       const struct gl_pixelstore_attrib *packing,
			       struct gl_texture_object *texObj,
			       struct gl_texture_image *texImage )
{
   driTextureObject * t = (driTextureObject *) texObj->DriverData;

   assert( t ); /* this _should_ be true */
   if ( t ) {
      driSwapOutTextureObject( t );
   }
   else {
      t = (driTextureObject *) r128AllocTexObj(texObj);
      if (!t) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glTexSubImage1D");
         return;
      }
   }

   _mesa_store_texsubimage1d(ctx, target, level, xoffset, width,
			     format, type, pixels, packing, texObj,
			     texImage);

   t->dirty_images[0] |= (1 << level);
}


static void r128TexImage2D( GLcontext *ctx, GLenum target, GLint level,
			    GLint internalFormat,
			    GLint width, GLint height, GLint border,
			    GLenum format, GLenum type, const GLvoid *pixels,
			    const struct gl_pixelstore_attrib *packing,
			    struct gl_texture_object *texObj,
			    struct gl_texture_image *texImage )
{
   driTextureObject * t = (driTextureObject *) texObj->DriverData;

   if ( t ) {
      driSwapOutTextureObject( (driTextureObject *) t );
   }
   else {
      t = (driTextureObject *) r128AllocTexObj(texObj);
      if (!t) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glTexImage2D");
         return;
      }
   }

   /* Note, this will call r128ChooseTextureFormat */
   _mesa_store_teximage2d(ctx, target, level, internalFormat,
                          width, height, border, format, type, pixels,
                          &ctx->Unpack, texObj, texImage);

   t->dirty_images[0] |= (1 << level);
}


static void r128TexSubImage2D( GLcontext *ctx,
			       GLenum target,
			       GLint level,
			       GLint xoffset, GLint yoffset,
			       GLsizei width, GLsizei height,
			       GLenum format, GLenum type,
			       const GLvoid *pixels,
			       const struct gl_pixelstore_attrib *packing,
			       struct gl_texture_object *texObj,
			       struct gl_texture_image *texImage )
{
   driTextureObject * t = (driTextureObject *) texObj->DriverData;

   assert( t ); /* this _should_ be true */
   if ( t ) {
      driSwapOutTextureObject( t );
   }
   else {
      t = (driTextureObject *) r128AllocTexObj(texObj);
      if (!t) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glTexImage2D");
         return;
      }
   }

   _mesa_store_texsubimage2d(ctx, target, level, xoffset, yoffset, width,
			     height, format, type, pixels, packing, texObj,
			     texImage);
   t->dirty_images[0] |= (1 << level);
}


static void r128TexEnv( GLcontext *ctx, GLenum target,
			  GLenum pname, const GLfloat *param )
{
   r128ContextPtr rmesa = R128_CONTEXT(ctx);
   struct gl_texture_unit *texUnit;
   GLubyte c[4];

   if ( R128_DEBUG & DEBUG_VERBOSE_API ) {
      fprintf( stderr, "%s( %s )\n",
	       __FUNCTION__, _mesa_lookup_enum_by_nr( pname ) );
   }

   switch ( pname ) {
   case GL_TEXTURE_ENV_MODE:
      FLUSH_BATCH( rmesa );
      rmesa->new_state |= R128_NEW_ALPHA;
      break;

   case GL_TEXTURE_ENV_COLOR:
      texUnit = &ctx->Texture.Unit[ctx->Texture.CurrentUnit];
      CLAMPED_FLOAT_TO_UBYTE( c[0], texUnit->EnvColor[0] );
      CLAMPED_FLOAT_TO_UBYTE( c[1], texUnit->EnvColor[1] );
      CLAMPED_FLOAT_TO_UBYTE( c[2], texUnit->EnvColor[2] );
      CLAMPED_FLOAT_TO_UBYTE( c[3], texUnit->EnvColor[3] );
      rmesa->env_color = r128PackColor( 4, c[0], c[1], c[2], c[3] );
      if ( rmesa->setup.constant_color_c != rmesa->env_color ) {
	 FLUSH_BATCH( rmesa );
	 rmesa->setup.constant_color_c = rmesa->env_color;

	 /* More complex multitexture/multipass fallbacks for GL_BLEND
	  * can be done later, but this allows a single pass GL_BLEND
	  * in some cases (ie. Performer town demo).  This is only
	  * applicable to the regular Rage 128, as the Pro and M3 can
	  * handle true single-pass GL_BLEND texturing.
	  */
	 rmesa->blend_flags &= ~R128_BLEND_ENV_COLOR;
	 if ( R128_IS_PLAIN( rmesa ) &&
	      rmesa->env_color != 0x00000000 &&
	      rmesa->env_color != 0xff000000 &&
	      rmesa->env_color != 0x00ffffff &&
	      rmesa->env_color != 0xffffffff ) {
	    rmesa->blend_flags |= R128_BLEND_ENV_COLOR;
	 }
      }
      break;

   case GL_TEXTURE_LOD_BIAS:
      {
	 uint32_t t = rmesa->setup.tex_cntl_c;
	 GLint bias;
	 uint32_t b;

	 /* GTH: This isn't exactly correct, but gives good results up to a
	  * certain point.  It is better than completely ignoring the LOD
	  * bias.  Unfortunately there isn't much range in the bias, the
	  * spec mentions strides that vary between 0.5 and 2.0 but these
	  * numbers don't seem to relate to the GL LOD bias value at all.
	  */
	 if ( param[0] >= 1.0 ) {
	    bias = -128;
	 } else if ( param[0] >= 0.5 ) {
	    bias = -64;
	 } else if ( param[0] >= 0.25 ) {
	    bias = 0;
	 } else if ( param[0] >= 0.0 ) {
	    bias = 63;
	 } else {
	    bias = 127;
	 }

	 b = (uint32_t)bias & 0xff;
	 t &= ~R128_LOD_BIAS_MASK;
	 t |= (b << R128_LOD_BIAS_SHIFT);

	 if ( rmesa->setup.tex_cntl_c != t ) {
	    FLUSH_BATCH( rmesa );
	    rmesa->setup.tex_cntl_c = t;
	    rmesa->dirty |= R128_UPLOAD_CONTEXT;
	 }
      }
      break;

   default:
      return;
   }
}


static void r128TexParameter( GLcontext *ctx, GLenum target,
                              struct gl_texture_object *tObj,
                              GLenum pname, const GLfloat *params )
{
   r128ContextPtr rmesa = R128_CONTEXT(ctx);
   r128TexObjPtr t = (r128TexObjPtr)tObj->DriverData;

   if ( R128_DEBUG & DEBUG_VERBOSE_API ) {
      fprintf( stderr, "%s( %s )\n",
	       __FUNCTION__, _mesa_lookup_enum_by_nr( pname ) );
   }

   if ( ( target != GL_TEXTURE_2D ) && ( target != GL_TEXTURE_1D ) )
      return;

   switch ( pname ) {
   case GL_TEXTURE_MIN_FILTER:
   case GL_TEXTURE_MAG_FILTER:
      if ( t->base.bound ) FLUSH_BATCH( rmesa );
      r128SetTexFilter( t, tObj->MinFilter, tObj->MagFilter );
      break;

   case GL_TEXTURE_WRAP_S:
   case GL_TEXTURE_WRAP_T:
      if ( t->base.bound ) FLUSH_BATCH( rmesa );
      r128SetTexWrap( t, tObj->WrapS, tObj->WrapT );
      break;

   case GL_TEXTURE_BORDER_COLOR:
      if ( t->base.bound ) FLUSH_BATCH( rmesa );
      r128SetTexBorderColor( t, tObj->BorderColor.f );
      break;

   case GL_TEXTURE_BASE_LEVEL:
   case GL_TEXTURE_MAX_LEVEL:
   case GL_TEXTURE_MIN_LOD:
   case GL_TEXTURE_MAX_LOD:
      /* This isn't the most efficient solution but there doesn't appear to
       * be a nice alternative for R128.  Since there's no LOD clamping,
       * we just have to rely on loading the right subset of mipmap levels
       * to simulate a clamped LOD.
       */
      if ( t->base.bound ) FLUSH_BATCH( rmesa );
      driSwapOutTextureObject( (driTextureObject *) t );
      break;

   default:
      return;
   }
}

static void r128BindTexture( GLcontext *ctx, GLenum target,
			       struct gl_texture_object *tObj )
{
   if ( R128_DEBUG & DEBUG_VERBOSE_API ) {
      fprintf( stderr, "%s( %p ) unit=%d\n", __FUNCTION__, (void *) tObj,
	       ctx->Texture.CurrentUnit );
   }

   assert( (target != GL_TEXTURE_2D && target != GL_TEXTURE_1D) ||
           (tObj->DriverData != NULL) );
}


static void r128DeleteTexture( GLcontext *ctx,
				 struct gl_texture_object *tObj )
{
   r128ContextPtr rmesa = R128_CONTEXT(ctx);
   driTextureObject * t = (driTextureObject *) tObj->DriverData;

   if ( t ) {
      if ( t->bound && rmesa ) {
	 FLUSH_BATCH( rmesa );
      }

      driDestroyTextureObject( t );
   }
   /* Free mipmap images and the texture object itself */
   _mesa_delete_texture_object(ctx, tObj);
}

/**
 * Allocate a new texture object.
 * Called via ctx->Driver.NewTextureObject.
 * Note: we could use containment here to 'derive' the driver-specific
 * texture object from the core mesa gl_texture_object.  Not done at this time.
 */
static struct gl_texture_object *
r128NewTextureObject( GLcontext *ctx, GLuint name, GLenum target )
{
   struct gl_texture_object *obj;
   obj = _mesa_new_texture_object(ctx, name, target);
   r128AllocTexObj( obj );
   return obj;
}

void r128InitTextureFuncs( struct dd_function_table *functions )
{
   functions->TexEnv			= r128TexEnv;
   functions->TexImage1D		= r128TexImage1D;
   functions->TexSubImage1D		= r128TexSubImage1D;
   functions->TexImage2D		= r128TexImage2D;
   functions->TexSubImage2D		= r128TexSubImage2D;
   functions->TexParameter		= r128TexParameter;
   functions->BindTexture		= r128BindTexture;
   functions->NewTextureObject		= r128NewTextureObject;
   functions->DeleteTexture		= r128DeleteTexture;
   functions->IsTextureResident		= driIsTextureResident;

   driInitTextureFormats();
}

