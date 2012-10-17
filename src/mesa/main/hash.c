/**
 * \file hash.c
 * Generic hash table. 
 *
 * Used for display lists, texture objects, vertex/fragment programs,
 * buffer objects, etc.  The hash functions are thread-safe.
 * 
 * \note key=0 is illegal.
 *
 * \author Brian Paul
 */

/*
 * Mesa 3-D graphics library
 * Version:  6.5.1
 *
 * Copyright (C) 1999-2006  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdbool.h>
#include "glheader.h"
#include "imports.h"
#include "glapi/glthread.h"
#include "hash.h"
#include "open_hash_table.h"

#define TABLE_SIZE 1023  /**< Size of lookup table/array */

#define HASH_FUNC(K)  ((K) % TABLE_SIZE)

/**
 * The hash table data structure.  
 */
struct _mesa_HashTable {
   struct open_hash_table *ht;
   GLuint MaxKey;                        /**< highest key inserted so far */
   _glthread_Mutex Mutex;                /**< mutual exclusion lock */
   _glthread_Mutex WalkMutex;            /**< for _mesa_HashWalk() */
   GLboolean InDeleteAll;                /**< Debug check */
};

/** @{
 * Mapping from our use of GLuint as both the key and the hash value to the
 * open_hash.h API
 *
 * There exist many integer hash functions, designed to avoid collisions when
 * the integers are spread across key space with some patterns.  In GL, the
 * pattern (in the case of glGen*()ed object IDs) is that the keys are unique
 * contiguous integers starting from 1, with very few skipped (would happen
 * due to deletion but us continuing to allocate from MaxKey).  In that case,
 * if we just use the key as the hash value, we will never see a collision in
 * the table, because the table resizes itself when it approaches full, and
 * key % table_size == key.
 */
static bool
uint_key_compare(const void *a, const void *b)
{
   return a == b;
}

static uint32_t
uint_hash(GLuint id)
{
   return id;
}

static void *
uint_key(GLuint id)
{
   return (void *)(uintptr_t) id;
}
/** @} */

/**
 * Create a new hash table.
 * 
 * \return pointer to a new, empty hash table.
 */
struct _mesa_HashTable *
_mesa_NewHashTable(void)
{
   struct _mesa_HashTable *table = CALLOC_STRUCT(_mesa_HashTable);

   table->ht = _mesa_open_hash_table_create(uint_key_compare);

   if (table) {
      _glthread_INIT_MUTEX(table->Mutex);
      _glthread_INIT_MUTEX(table->WalkMutex);
   }
   return table;
}



/**
 * Delete a hash table.
 * Frees each entry on the hash table and then the hash table structure itself.
 * Note that the caller should have already traversed the table and deleted
 * the objects in the table (i.e. We don't free the entries' data pointer).
 *
 * \param table the hash table to delete.
 */
void
_mesa_DeleteHashTable(struct _mesa_HashTable *table)
{
   assert(table);

   if (_mesa_open_hash_table_next_entry(table->ht, NULL) != NULL) {
      _mesa_problem(NULL, "In _mesa_DeleteHashTable, found non-freed data");
   }

   _mesa_open_hash_table_destroy(table->ht, NULL);

   _glthread_DESTROY_MUTEX(table->Mutex);
   _glthread_DESTROY_MUTEX(table->WalkMutex);
   free(table);
}



/**
 * Lookup an entry in the hash table, without locking.
 * \sa _mesa_HashLookup
 */
static inline void *
_mesa_HashLookup_unlocked(struct _mesa_HashTable *table, GLuint key)
{
   const struct open_hash_entry *entry;

   assert(table);
   assert(key);

   entry = _mesa_open_hash_table_search(table->ht,
                                        uint_hash(key), uint_key(key));
   if (!entry)
      return NULL;

   return entry->data;
}


/**
 * Lookup an entry in the hash table.
 * 
 * \param table the hash table.
 * \param key the key.
 * 
 * \return pointer to user's data or NULL if key not in table
 */
void *
_mesa_HashLookup(struct _mesa_HashTable *table, GLuint key)
{
   void *res;
   assert(table);
   _glthread_LOCK_MUTEX(table->Mutex);
   res = _mesa_HashLookup_unlocked(table, key);
   _glthread_UNLOCK_MUTEX(table->Mutex);
   return res;
}


/**
 * Insert a key/pointer pair into the hash table.  
 * If an entry with this key already exists we'll replace the existing entry.
 * 
 * \param table the hash table.
 * \param key the key (not zero).
 * \param data pointer to user data.
 */
void
_mesa_HashInsert(struct _mesa_HashTable *table, GLuint key, void *data)
{
   assert(table);
   assert(key);

   _glthread_LOCK_MUTEX(table->Mutex);

   if (key > table->MaxKey)
      table->MaxKey = key;

   _mesa_open_hash_table_insert(table->ht, uint_hash(key), uint_key(key), data);

   _glthread_UNLOCK_MUTEX(table->Mutex);
}



/**
 * Remove an entry from the hash table.
 * 
 * \param table the hash table.
 * \param key key of entry to remove.
 *
 * While holding the hash table's lock, searches the entry with the matching
 * key and unlinks it.
 */
void
_mesa_HashRemove(struct _mesa_HashTable *table, GLuint key)
{
   struct open_hash_entry *entry;

   assert(table);
   assert(key);

   /* have to check this outside of mutex lock */
   if (table->InDeleteAll) {
      _mesa_problem(NULL, "_mesa_HashRemove illegally called from "
                    "_mesa_HashDeleteAll callback function");
      return;
   }

   _glthread_LOCK_MUTEX(table->Mutex);
   entry = _mesa_open_hash_table_search(table->ht,
                                        uint_hash(key), uint_key(key));
   _mesa_open_hash_table_remove(table->ht, entry);
   _glthread_UNLOCK_MUTEX(table->Mutex);
}



/**
 * Delete all entries in a hash table, but don't delete the table itself.
 * Invoke the given callback function for each table entry.
 *
 * \param table  the hash table to delete
 * \param callback  the callback function
 * \param userData  arbitrary pointer to pass along to the callback
 *                  (this is typically a struct gl_context pointer)
 */
void
_mesa_HashDeleteAll(struct _mesa_HashTable *table,
                    void (*callback)(GLuint key, void *data, void *userData),
                    void *userData)
{
   struct open_hash_entry *entry;

   ASSERT(table);
   ASSERT(callback);
   _glthread_LOCK_MUTEX(table->Mutex);
   table->InDeleteAll = GL_TRUE;
   for (entry = _mesa_open_hash_table_next_entry(table->ht, NULL);
        entry != NULL;
        entry = _mesa_open_hash_table_next_entry(table->ht, entry)) {
      callback((uintptr_t)entry->key, entry->data, userData);
      _mesa_open_hash_table_remove(table->ht, entry);
   }
   table->InDeleteAll = GL_FALSE;
   _glthread_UNLOCK_MUTEX(table->Mutex);
}


/**
 * Walk over all entries in a hash table, calling callback function for each.
 * Note: we use a separate mutex in this function to avoid a recursive
 * locking deadlock (in case the callback calls _mesa_HashRemove()) and to
 * prevent multiple threads/contexts from getting tangled up.
 * A lock-less version of this function could be used when the table will
 * not be modified.
 * \param table  the hash table to walk
 * \param callback  the callback function
 * \param userData  arbitrary pointer to pass along to the callback
 *                  (this is typically a struct gl_context pointer)
 */
void
_mesa_HashWalk(const struct _mesa_HashTable *table,
               void (*callback)(GLuint key, void *data, void *userData),
               void *userData)
{
   /* cast-away const */
   struct _mesa_HashTable *table2 = (struct _mesa_HashTable *) table;
   struct open_hash_entry *entry;

   ASSERT(table);
   ASSERT(callback);
   _glthread_LOCK_MUTEX(table2->WalkMutex);
   for (entry = _mesa_open_hash_table_next_entry(table->ht, NULL);
        entry != NULL;
        entry = _mesa_open_hash_table_next_entry(table->ht, entry)) {
      callback((uintptr_t)entry->key, entry->data, userData);
   }
   _glthread_UNLOCK_MUTEX(table2->WalkMutex);
}

static void
debug_print_entry(GLuint key, void *data, void *userData)
{
   _mesa_debug(NULL, "%u %p\n", key, data);
}

/**
 * Dump contents of hash table for debugging.
 * 
 * \param table the hash table.
 */
void
_mesa_HashPrint(const struct _mesa_HashTable *table)
{
   _mesa_HashWalk(table, debug_print_entry, NULL);
}


/**
 * Find a block of adjacent unused hash keys.
 * 
 * \param table the hash table.
 * \param numKeys number of keys needed.
 * 
 * \return Starting key of free block or 0 if failure.
 *
 * If there are enough free keys between the maximum key existing in the table
 * (_mesa_HashTable::MaxKey) and the maximum key possible, then simply return
 * the adjacent key. Otherwise do a full search for a free key block in the
 * allowable key range.
 */
GLuint
_mesa_HashFindFreeKeyBlock(struct _mesa_HashTable *table, GLuint numKeys)
{
   const GLuint maxKey = ~((GLuint) 0);
   _glthread_LOCK_MUTEX(table->Mutex);
   if (maxKey - numKeys > table->MaxKey) {
      /* the quick solution */
      _glthread_UNLOCK_MUTEX(table->Mutex);
      return table->MaxKey + 1;
   }
   else {
      /* the slow solution */
      GLuint freeCount = 0;
      GLuint freeStart = 1;
      GLuint key;
      for (key = 1; key != maxKey; key++) {
	 if (_mesa_HashLookup_unlocked(table, key)) {
	    /* darn, this key is already in use */
	    freeCount = 0;
	    freeStart = key+1;
	 }
	 else {
	    /* this key not in use, check if we've found enough */
	    freeCount++;
	    if (freeCount == numKeys) {
               _glthread_UNLOCK_MUTEX(table->Mutex);
	       return freeStart;
	    }
	 }
      }
      /* cannot allocate a block of numKeys consecutive keys */
      _glthread_UNLOCK_MUTEX(table->Mutex);
      return 0;
   }
}


/**
 * Return the number of entries in the hash table.
 */
GLuint
_mesa_HashNumEntries(const struct _mesa_HashTable *table)
{
   struct open_hash_entry *entry;
   GLuint count = 0;

   for (entry = _mesa_open_hash_table_next_entry(table->ht, NULL);
        entry != NULL;
        entry = _mesa_open_hash_table_next_entry(table->ht, entry)) {
      count++;
   }

   return count;
}
