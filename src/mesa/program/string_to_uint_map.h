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

#include "hash_table.h"

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
/**
 * Map from a string (name) to an unsigned integer value
 *
 * \note
 * Because of the way this class interacts with the \c hash_table
 * implementation, values of \c UINT_MAX cannot be stored in the map.
 */
struct string_to_uint_map {
public:
   string_to_uint_map()
   {
      this->ht = hash_table_ctor(0, hash_table_string_hash,
				 hash_table_string_compare);
   }

   ~string_to_uint_map()
   {
      hash_table_call_foreach(this->ht, delete_key, NULL);
      hash_table_dtor(this->ht);
   }

   /**
    * Remove all mappings from this map.
    */
   void clear()
   {
      hash_table_call_foreach(this->ht, delete_key, NULL);
      hash_table_clear(this->ht);
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
      const intptr_t v =
	 (intptr_t) hash_table_find(this->ht, (const void *) key);

      if (v == 0)
	 return false;

      value = (unsigned)(v - 1);
      return true;
   }

   void put(unsigned value, const char *key)
   {
      /* The low-level hash table structure returns NULL if key is not in the
       * hash table.  However, users of this map might want to store zero as a
       * valid value in the table.  Bias the value by +1 so that a
       * user-specified zero is stored as 1.  This enables ::get to tell the
       * difference between a user-specified zero (returned as 1 by
       * hash_table_find) and the key not in the table (returned as 0 by
       * hash_table_find).
       *
       * The net effect is that we can't store UINT_MAX in the table.  This is
       * because UINT_MAX+1 = 0.
       */
      assert(value != UINT_MAX);
      char *dup_key = strdup(key);
      bool result = hash_table_replace(this->ht,
				       (void *) (intptr_t) (value + 1),
				       dup_key);
      if (result)
	 free(dup_key);
   }

private:
   static void delete_key(const void *key, void *data, void *closure)
   {
      (void) data;
      (void) closure;

      free((char *)key);
   }

   struct hash_table *ht;
};
#endif /* __cplusplus */
