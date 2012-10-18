/*
 * Copyright © 2008 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "main/hash_table.h"

#ifdef __cplusplus
extern "C" {
#endif

struct string_to_uint_map;

struct string_to_uint_map *
string_to_uint_map_ctor();

void
string_to_uint_map_dtor(struct string_to_uint_map *);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/**
 * Map from a string (name) to an unsigned integer value
 */
struct string_to_uint_map {
public:
   string_to_uint_map()
   {
      this->ht = _mesa_hash_table_create(NULL,
                                         _mesa_key_string_equal);
   }

   ~string_to_uint_map()
   {
      clear();
      _mesa_hash_table_destroy(this->ht, NULL);
   }

   /**
    * Remove all mappings from this map.
    */
   void clear()
   {
      struct hash_entry *entry;
      hash_table_foreach(this->ht, entry) {
         free((void *)entry->key);
         _mesa_hash_table_remove(this->ht, entry);
      }
   }

   /**
    * Get the value associated with a particular key
    *
    * \return
    * If \c key is found in the map, \c true is returned.  Otherwise \c false
    * is returned.
    *
    * \note
    * If \c key is not found in the table, \c value is not modified.
    */
   bool get(unsigned &value, const char *key)
   {
      struct hash_entry *entry;
      entry = _mesa_hash_table_search(this->ht, _mesa_hash_string(key), key);
      if (!entry)
         return false;

      value = (uintptr_t)entry->data;
      return true;
   }

   void put(unsigned value, const char *key)
   {
      uint32_t hash = _mesa_hash_string(key);
      struct hash_entry *entry;

      entry = _mesa_hash_table_search(this->ht, hash, key);
      if (!entry) {
         key = strdup(key);
         _mesa_hash_table_insert(this->ht, hash, key, (void *)(uintptr_t)value);
      }
   }

private:
   struct hash_table *ht;
};
#endif /* __cplusplus */
