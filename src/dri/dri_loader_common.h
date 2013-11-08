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

#include <GL/gl.h>
#include <GL/internal/dri_interface.h>

#ifndef DRI_LOADER_H
#define DRI_LOADER_H

struct dri_loader {
   struct {
      /**
       * The DRI1 extension.  Some functions from this struct are used even in
       * a DRI2 loader.
       */
      const __DRIcoreExtension *core;
      const __DRIdri2Extension *dri2;
      /**
       * The driver extension for supporting DRI3 and similar
       * client-allocated-buffer loaders.
       *
       * It contains some of the same functions as core and dri2.
       */
      const __DRIimageDriverExtension *image_driver;
      /**
       * The swrast driver extension, for supporting DRI-like rendering
       * without an actual DRI hardware device (DRM fd) involved.
       */
      const __DRIswrastExtension *swrast;
      const __DRIimageExtension *image;
      const __DRI2flushExtension *flush;
      const __DRI2configQueryExtension *config_query;
      const __DRItexBufferExtension *tex_buffer;
      const __DRI2rendererQueryExtension *renderer_query;
      const __DRIrobustnessExtension *robustness;
      const __DRI2throttleExtension *throttle;
   } driver_extensions;
};

void
dri_bind_driver_extensions_to_loader(struct dri_loader *loader,
                                     const __DRIextension **extensions);

#endif /* DRI_LOADER_H */
