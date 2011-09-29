
#include "main/glheader.h"
#include "main/macros.h"
#include "main/mfeatures.h"
#include "main/mtypes.h"
#include "main/enums.h"
#include "main/bufferobj.h"
#include "main/context.h"
#include "main/formats.h"
#include "main/pbo.h"
#include "main/renderbuffer.h"
#include "main/texcompress.h"
#include "main/texstore.h"
#include "main/texgetimage.h"
#include "main/texobj.h"
#include "main/teximage.h"

#include "intel_context.h"
#include "intel_mipmap_tree.h"
#include "intel_buffer_objects.h"
#include "intel_batchbuffer.h"
#include "intel_tex.h"
#include "intel_blit.h"
#include "intel_fbo.h"
#include "intel_span.h"

#define FILE_DEBUG_FLAG DEBUG_TEXTURE

/* Functions to store texture images.  Where possible, mipmap_tree's
 * will be created or further instantiated with image data, otherwise
 * images will be stored in malloc'd memory.  A validation step is
 * required to pull those images into a mipmap tree, or otherwise
 * decide a fallback is required.
 */



/* Otherwise, store it in memory if (Border != 0) or (any dimension ==
 * 1).
 *    
 * Otherwise, if max_level >= level >= min_level, create tree with
 * space for textures from min_level down to max_level.
 *
 * Otherwise, create tree with space for textures from (level
 * 0)..(1x1).  Consider pruning this tree at a validation if the
 * saving is worth it.
 */
struct intel_mipmap_tree *
intel_miptree_create_for_teximage(struct intel_context *intel,
				  struct intel_texture_object *intelObj,
				  struct intel_texture_image *intelImage,
				  GLboolean expect_accelerated_upload)
{
   GLuint firstLevel;
   GLuint lastLevel;
   int width, height, depth;
   GLuint i;

   intel_miptree_get_dimensions_for_image(&intelImage->base.Base,
                                          &width, &height, &depth);

   DBG("%s\n", __FUNCTION__);

   if (intelImage->base.Base.Border)
      return NULL;

   if (intelImage->base.Base.Level > intelObj->base.BaseLevel &&
       (width == 1 ||
        (intelObj->base.Target != GL_TEXTURE_1D && height == 1) ||
        (intelObj->base.Target == GL_TEXTURE_3D && depth == 1))) {
      /* For this combination, we're at some lower mipmap level and
       * some important dimension is 1.  We can't extrapolate up to a
       * likely base level width/height/depth for a full mipmap stack
       * from this info, so just allocate this one level.
       */
      firstLevel = intelImage->base.Base.Level;
      lastLevel = intelImage->base.Base.Level;
   } else {
      /* If this image disrespects BaseLevel, allocate from level zero.
       * Usually BaseLevel == 0, so it's unlikely to happen.
       */
      if (intelImage->base.Base.Level < intelObj->base.BaseLevel)
	 firstLevel = 0;
      else
	 firstLevel = intelObj->base.BaseLevel;

      /* Figure out image dimensions at start level. */
      for (i = intelImage->base.Base.Level; i > firstLevel; i--) {
	 width <<= 1;
	 if (height != 1)
	    height <<= 1;
	 if (depth != 1)
	    depth <<= 1;
      }

      /* Guess a reasonable value for lastLevel.  This is probably going
       * to be wrong fairly often and might mean that we have to look at
       * resizable buffers, or require that buffers implement lazy
       * pagetable arrangements.
       */
      if ((intelObj->base.Sampler.MinFilter == GL_NEAREST ||
	   intelObj->base.Sampler.MinFilter == GL_LINEAR) &&
	  intelImage->base.Base.Level == firstLevel &&
	  (intel->gen < 4 || firstLevel == 0)) {
	 lastLevel = firstLevel;
      } else {
	 lastLevel = firstLevel + _mesa_logbase2(MAX2(MAX2(width, height), depth));
      }
   }

   return intel_miptree_create(intel,
			       intelObj->base.Target,
			       intelImage->base.Base.TexFormat,
			       firstLevel,
			       lastLevel,
			       width,
			       height,
			       depth,
			       expect_accelerated_upload);
}

/* There are actually quite a few combinations this will work for,
 * more than what I've listed here.
 */
static bool
check_pbo_format(GLenum format, GLenum type,
                 gl_format mesa_format)
{
   switch (mesa_format) {
   case MESA_FORMAT_ARGB8888:
      return (format == GL_BGRA && (type == GL_UNSIGNED_BYTE ||
				    type == GL_UNSIGNED_INT_8_8_8_8_REV));
   case MESA_FORMAT_RGB565:
      return (format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5);
   case MESA_FORMAT_L8:
      return (format == GL_LUMINANCE && type == GL_UNSIGNED_BYTE);
   case MESA_FORMAT_YCBCR:
      return (type == GL_UNSIGNED_SHORT_8_8_MESA || type == GL_UNSIGNED_BYTE);
   default:
      return false;
   }
}


/* XXX: Do this for TexSubImage also:
 */
static bool
try_pbo_upload(struct gl_context *ctx,
               struct gl_texture_image *image,
               const struct gl_pixelstore_attrib *unpack,
	       GLenum format, GLenum type,
               GLint width, GLint height, const void *pixels)
{
   struct intel_texture_image *intelImage = intel_texture_image(image);
   struct intel_context *intel = intel_context(ctx);
   struct intel_buffer_object *pbo = intel_buffer_object(unpack->BufferObj);
   GLuint src_offset, src_stride;
   GLuint dst_x, dst_y, dst_stride;
   drm_intel_bo *dst_buffer, *src_buffer;

   if (!_mesa_is_bufferobj(unpack->BufferObj))
      return false;

   DBG("trying pbo upload\n");

   if (intel->ctx._ImageTransferState ||
       unpack->SkipPixels || unpack->SkipRows) {
      DBG("%s: image transfer\n", __FUNCTION__);
      return false;
   }

   if (!check_pbo_format(format, type, intelImage->base.Base.TexFormat)) {
      DBG("%s: format mismatch (upload to %s with format 0x%x, type 0x%x)\n",
	  __FUNCTION__, _mesa_get_format_name(intelImage->base.Base.TexFormat),
	  format, type);
      return false;
   }

   ctx->Driver.AllocTextureImageBuffer(ctx, image, image->TexFormat,
                                       width, height, 1);

   if (!intelImage->mt) {
      DBG("%s: no miptree\n", __FUNCTION__);
      return false;
   }

   dst_buffer = intelImage->mt->region->bo;
   src_buffer = intel_bufferobj_source(intel, pbo, 64, &src_offset);
   /* note: potential 64-bit ptr to 32-bit int cast */
   src_offset += (GLuint) (unsigned long) pixels;

   if (unpack->RowLength > 0)
      src_stride = unpack->RowLength;
   else
      src_stride = width;

   intel_miptree_get_image_offset(intelImage->mt, intelImage->base.Base.Level,
				  intelImage->base.Base.Face, 0,
				  &dst_x, &dst_y);

   dst_stride = intelImage->mt->region->pitch;

   if (!intelEmitCopyBlit(intel,
			  intelImage->mt->cpp,
			  src_stride, src_buffer,
			  src_offset, GL_FALSE,
			  dst_stride, dst_buffer, 0,
			  intelImage->mt->region->tiling,
			  0, 0, dst_x, dst_y, width, height,
			  GL_COPY)) {
      DBG("%s: blit failed\n", __FUNCTION__);
      return false;
   }

   DBG("%s: success\n", __FUNCTION__);
   return true;
}

/**
 * \param scatter Scatter if true. Gather if false.
 *
 * \see intel_tex_image_x8z24_scatter
 * \see intel_tex_image_x8z24_gather
 */
static void
intel_tex_image_s8z24_scattergather(struct intel_context *intel,
				    struct intel_texture_image *intel_image,
				    bool scatter)
{
   struct gl_context *ctx = &intel->ctx;
   struct gl_renderbuffer *depth_rb = intel_image->depth_rb;
   struct gl_renderbuffer *stencil_rb = intel_image->stencil_rb;
   int w, h, d;

   intel_miptree_get_dimensions_for_image(&intel_image->base.Base, &w, &h, &d);
   assert(d == 1); /* FINISHME */

   uint32_t depth_row[w];
   uint8_t stencil_row[w];

   intel_renderbuffer_map(intel, depth_rb);
   intel_renderbuffer_map(intel, stencil_rb);

   if (scatter) {
      for (int y = 0; y < h; ++y) {
	 depth_rb->GetRow(ctx, depth_rb, w, 0, y, depth_row);
	 for (int x = 0; x < w; ++x) {
	    stencil_row[x] = depth_row[x] >> 24;
	 }
	 stencil_rb->PutRow(ctx, stencil_rb, w, 0, y, stencil_row, NULL);
      }
   } else { /* gather */
      for (int y = 0; y < h; ++y) {
	 depth_rb->GetRow(ctx, depth_rb, w, 0, y, depth_row);
	 stencil_rb->GetRow(ctx, stencil_rb, w, 0, y, stencil_row);
	 for (int x = 0; x < w; ++x) {
	    uint32_t s8_x24 = stencil_row[x] << 24;
	    uint32_t x8_z24 = depth_row[x] & 0x00ffffff;
	    depth_row[x] = s8_x24 | x8_z24;
	 }
	 depth_rb->PutRow(ctx, depth_rb, w, 0, y, depth_row, NULL);
      }
   }

   intel_renderbuffer_unmap(intel, depth_rb);
   intel_renderbuffer_unmap(intel, stencil_rb);
}

/**
 * Copy the x8 bits from intel_image->depth_rb to intel_image->stencil_rb.
 */
void
intel_tex_image_s8z24_scatter(struct intel_context *intel,
			      struct intel_texture_image *intel_image)
{
   intel_tex_image_s8z24_scattergather(intel, intel_image, true);
}

/**
 * Copy the data in intel_image->stencil_rb to the x8 bits in
 * intel_image->depth_rb.
 */
void
intel_tex_image_s8z24_gather(struct intel_context *intel,
			     struct intel_texture_image *intel_image)
{
   intel_tex_image_s8z24_scattergather(intel, intel_image, false);
}

bool
intel_tex_image_s8z24_create_renderbuffers(struct intel_context *intel,
					   struct intel_texture_image *image)
{
   struct gl_context *ctx = &intel->ctx;
   bool ok = true;
   int width, height, depth;
   struct gl_renderbuffer *drb;
   struct gl_renderbuffer *srb;
   struct intel_renderbuffer *idrb;
   struct intel_renderbuffer *isrb;

   intel_miptree_get_dimensions_for_image(&image->base.Base,
                                          &width, &height, &depth);
   assert(depth == 1); /* FINISHME */

   assert(intel->has_separate_stencil);
   assert(image->base.Base.TexFormat == MESA_FORMAT_S8_Z24);
   assert(image->mt != NULL);

   drb = intel_create_wrapped_renderbuffer(ctx, width, height,
					   MESA_FORMAT_X8_Z24);
   srb = intel_create_wrapped_renderbuffer(ctx, width, height,
					   MESA_FORMAT_S8);

   if (!drb || !srb) {
      if (drb) {
	 drb->Delete(drb);
      }
      if (srb) {
	 srb->Delete(srb);
      }
      return false;
   }

   idrb = intel_renderbuffer(drb);
   isrb = intel_renderbuffer(srb);

   intel_region_reference(&idrb->region, image->mt->region);
   ok = intel_alloc_renderbuffer_storage(ctx, srb, GL_STENCIL_INDEX8,
					 width, height);

   if (!ok) {
      drb->Delete(drb);
      srb->Delete(srb);
      return false;
   }

   intel_renderbuffer_set_draw_offset(idrb, image, 0);
   intel_renderbuffer_set_draw_offset(isrb, image, 0);

   _mesa_reference_renderbuffer(&image->depth_rb, drb);
   _mesa_reference_renderbuffer(&image->stencil_rb, srb);

   return true;
}

static void
intelTexImage(struct gl_context * ctx,
              GLint dims,
              GLenum target, GLint level,
              GLint internalFormat,
              GLint width, GLint height, GLint depth,
              GLint border,
              GLenum format, GLenum type, const void *pixels,
              const struct gl_pixelstore_attrib *unpack,
              struct gl_texture_object *texObj,
              struct gl_texture_image *texImage, GLsizei imageSize)
{
   DBG("%s target %s level %d %dx%dx%d border %d\n", __FUNCTION__,
       _mesa_lookup_enum_by_nr(target), level, width, height, depth, border);

   /* Attempt to use the blitter for PBO image uploads.
    */
   if (dims <= 2 &&
       try_pbo_upload(ctx, texImage, unpack, format, type,
		      width, height, pixels)) {
      return;
   }

   DBG("Upload image %dx%dx%d pixels %p\n", width, height, depth, pixels);

   if (pixels || _mesa_is_bufferobj(unpack->BufferObj)) {
      _mesa_store_teximage3d(ctx, target, level, internalFormat,
                             width, height, depth, border,
                             format, type, pixels,
                             unpack, texObj, texImage);
   } else {
      ctx->Driver.AllocTextureImageBuffer(ctx, texImage, texImage->TexFormat,
                                          width, height, depth);
   }
}


static void
intelTexImage3D(struct gl_context * ctx,
                GLenum target, GLint level,
                GLint internalFormat,
                GLint width, GLint height, GLint depth,
                GLint border,
                GLenum format, GLenum type, const void *pixels,
                const struct gl_pixelstore_attrib *unpack,
                struct gl_texture_object *texObj,
                struct gl_texture_image *texImage)
{
   intelTexImage(ctx, 3, target, level,
                 internalFormat, width, height, depth, border,
                 format, type, pixels, unpack, texObj, texImage, 0);
}


static void
intelTexImage2D(struct gl_context * ctx,
                GLenum target, GLint level,
                GLint internalFormat,
                GLint width, GLint height, GLint border,
                GLenum format, GLenum type, const void *pixels,
                const struct gl_pixelstore_attrib *unpack,
                struct gl_texture_object *texObj,
                struct gl_texture_image *texImage)
{
   intelTexImage(ctx, 2, target, level,
                 internalFormat, width, height, 1, border,
                 format, type, pixels, unpack, texObj, texImage, 0);
}


static void
intelTexImage1D(struct gl_context * ctx,
                GLenum target, GLint level,
                GLint internalFormat,
                GLint width, GLint border,
                GLenum format, GLenum type, const void *pixels,
                const struct gl_pixelstore_attrib *unpack,
                struct gl_texture_object *texObj,
                struct gl_texture_image *texImage)
{
   intelTexImage(ctx, 1, target, level,
                 internalFormat, width, 1, 1, border,
                 format, type, pixels, unpack, texObj, texImage, 0);
}


/**
 * Binds a region to a texture image, like it was uploaded by glTexImage2D().
 *
 * Used for GLX_EXT_texture_from_pixmap and EGL image extensions,
 */
static void
intel_set_texture_image_region(struct gl_context *ctx,
			       struct gl_texture_image *image,
			       struct intel_region *region,
			       GLenum target,
			       GLenum internalFormat,
			       gl_format format)
{
   struct intel_context *intel = intel_context(ctx);
   struct intel_texture_image *intel_image = intel_texture_image(image);

   _mesa_init_teximage_fields(&intel->ctx, target, image,
			      region->width, region->height, 1,
			      0, internalFormat, format);

   ctx->Driver.FreeTextureImageBuffer(ctx, image);

   intel_image->mt = intel_miptree_create_for_region(intel, target,
						     image->TexFormat,
						     region);
   if (intel_image->mt == NULL)
       return;

   image->RowStride = region->pitch;
}

void
intelSetTexBuffer2(__DRIcontext *pDRICtx, GLint target,
		   GLint texture_format,
		   __DRIdrawable *dPriv)
{
   struct gl_framebuffer *fb = dPriv->driverPrivate;
   struct intel_context *intel = pDRICtx->driverPrivate;
   struct gl_context *ctx = &intel->ctx;
   struct intel_texture_object *intelObj;
   struct intel_renderbuffer *rb;
   struct gl_texture_object *texObj;
   struct gl_texture_image *texImage;
   int level = 0, internalFormat;
   gl_format texFormat;

   texObj = _mesa_get_current_tex_object(ctx, target);
   intelObj = intel_texture_object(texObj);

   if (!intelObj)
      return;

   if (dPriv->lastStamp != dPriv->dri2.stamp ||
       !pDRICtx->driScreenPriv->dri2.useInvalidate)
      intel_update_renderbuffers(pDRICtx, dPriv);

   rb = intel_get_renderbuffer(fb, BUFFER_FRONT_LEFT);
   /* If the region isn't set, then intel_update_renderbuffers was unable
    * to get the buffers for the drawable.
    */
   if (rb->region == NULL)
      return;

   if (texture_format == __DRI_TEXTURE_FORMAT_RGB) {
      internalFormat = GL_RGB;
      texFormat = MESA_FORMAT_XRGB8888;
   }
   else {
      internalFormat = GL_RGBA;
      texFormat = MESA_FORMAT_ARGB8888;
   }

   _mesa_lock_texture(&intel->ctx, texObj);
   texImage = _mesa_get_tex_image(ctx, texObj, target, level);
   intel_set_texture_image_region(ctx, texImage, rb->region, target,
				  internalFormat, texFormat);
   _mesa_unlock_texture(&intel->ctx, texObj);
}

void
intelSetTexBuffer(__DRIcontext *pDRICtx, GLint target, __DRIdrawable *dPriv)
{
   /* The old interface didn't have the format argument, so copy our
    * implementation's behavior at the time.
    */
   intelSetTexBuffer2(pDRICtx, target, __DRI_TEXTURE_FORMAT_RGBA, dPriv);
}

#if FEATURE_OES_EGL_image
static void
intel_image_target_texture_2d(struct gl_context *ctx, GLenum target,
			      struct gl_texture_object *texObj,
			      struct gl_texture_image *texImage,
			      GLeglImageOES image_handle)
{
   struct intel_context *intel = intel_context(ctx);
   __DRIscreen *screen;
   __DRIimage *image;

   screen = intel->intelScreen->driScrnPriv;
   image = screen->dri2.image->lookupEGLImage(screen, image_handle,
					      screen->loaderPrivate);
   if (image == NULL)
      return;

   intel_set_texture_image_region(ctx, texImage, image->region,
				  target, image->internal_format, image->format);
}
#endif

void
intelInitTextureImageFuncs(struct dd_function_table *functions)
{
   functions->TexImage1D = intelTexImage1D;
   functions->TexImage2D = intelTexImage2D;
   functions->TexImage3D = intelTexImage3D;

#if FEATURE_OES_EGL_image
   functions->EGLImageTargetTexture2D = intel_image_target_texture_2d;
#endif
}
