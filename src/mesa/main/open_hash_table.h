/*
 * Copyright © 2009 Intel Corporation
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

#include <inttypes.h>
#include <stdbool.h>

struct open_hash_entry {
	uint32_t hash;
	const void *key;
	void *data;
};

struct open_hash_table {
	struct open_hash_entry *table;
	bool (*key_equals_function)(const void *a, const void *b);
	uint32_t size;
	uint32_t rehash;
	uint32_t max_entries;
	uint32_t size_index;
	uint32_t entries;
	uint32_t deleted_entries;
};

struct open_hash_table *
_mesa_open_hash_table_create(bool (*key_equals_function)(const void *a,
                                                         const void *b));
void _mesa_open_hash_table_destroy(struct open_hash_table *ht,
                                   void (*delete_function)(struct open_hash_entry *entry));

struct open_hash_entry *
_mesa_open_hash_table_insert(struct open_hash_table *ht, uint32_t hash,
                             const void *key, void *data);
struct open_hash_entry *
_mesa_open_hash_table_search(struct open_hash_table *ht, uint32_t hash,
                             const void *key);
void _mesa_open_hash_table_remove(struct open_hash_table *ht,
                                  struct open_hash_entry *entry);

struct open_hash_entry *_mesa_open_hash_table_next_entry(struct open_hash_table *ht,
                                                         struct open_hash_entry *entry);
struct open_hash_entry *
_mesa_open_hash_table_random_entry(struct open_hash_table *ht,
                                   bool (*predicate)(struct open_hash_entry *entry));

uint32_t _mesa_fnv1_hash_string(const void *key);
bool _mesa_string_key_equals(const void *a, const void *b);
