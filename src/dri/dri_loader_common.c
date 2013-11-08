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

#include <string.h>
#include "dri_loader_common.h"

#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

void
dri_bind_driver_extensions_to_loader(struct dri_loader *loader,
                                     const __DRIextension **extensions)
{
   int i;

   static const struct {
      const char *name;
      size_t offset;
   } extension_offsets[] = {
#define EXTENSION_LOCATION(name, field) { name, \
                                          offsetof(struct dri_loader, \
                                                   driver_extensions.field) }
      EXTENSION_LOCATION(__DRI_CORE, core),
      EXTENSION_LOCATION(__DRI_DRI2, dri2),
      EXTENSION_LOCATION(__DRI_IMAGE_DRIVER, image_driver),
      EXTENSION_LOCATION(__DRI_SWRAST, swrast),

      EXTENSION_LOCATION(__DRI2_CONFIG_QUERY, config_query),
      EXTENSION_LOCATION(__DRI2_THROTTLE, throttle),
      EXTENSION_LOCATION(__DRI_TEX_BUFFER, tex_buffer),
      EXTENSION_LOCATION(__DRI2_ROBUSTNESS, robustness),
      EXTENSION_LOCATION(__DRI2_FLUSH, flush),
      EXTENSION_LOCATION(__DRI2_RENDERER_QUERY, renderer_query),
      EXTENSION_LOCATION(__DRI_IMAGE, image),
   };

   if (!extensions)
      return;

   for (i = 0; extensions[i]; i++) {
      int j;

      for (j = 0; j < ARRAY_SIZE(extension_offsets); j++) {
         if (strcmp(extensions[i]->name, extension_offsets[j].name) == 0) {
            *(const __DRIextension **)((char *)loader +
                                       extension_offsets[j].offset) =
               extensions[i];
            break;
         }
      }
   }
}
