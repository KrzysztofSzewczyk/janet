/*
* Copyright (c) 2019 Calvin Rose
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

/* The symbol cache is an open hashtable with all active symbols in the program
 * stored in it. As the primary use of symbols is table lookups and equality
 * checks, all symbols are interned so that there is a single copy of it in the
 * whole program. Equality is then just a pointer check. */

#ifndef JANET_AMALG
#include <janet.h>
#include "state.h"
#include "gc.h"
#include "util.h"
#include "symcache.h"
#endif

/* Cache state */
JANET_THREAD_LOCAL const uint8_t **janet_vm_cache = NULL;
JANET_THREAD_LOCAL uint32_t janet_vm_cache_capacity = 0;
JANET_THREAD_LOCAL uint32_t janet_vm_cache_count = 0;
JANET_THREAD_LOCAL uint32_t janet_vm_cache_deleted = 0;

/* Initialize the cache (allocate cache memory) */
void janet_symcache_init() {
    janet_vm_cache_capacity = 1024;
    janet_vm_cache = calloc(1, janet_vm_cache_capacity * sizeof(const uint8_t **));
    if (NULL == janet_vm_cache) {
        JANET_OUT_OF_MEMORY;
    }
    janet_vm_cache_count = 0;
    janet_vm_cache_deleted = 0;
}

/* Deinitialize the cache (free the cache memory) */
void janet_symcache_deinit() {
    free((void *)janet_vm_cache);
    janet_vm_cache = NULL;
    janet_vm_cache_capacity = 0;
    janet_vm_cache_count = 0;
    janet_vm_cache_deleted = 0;
}

/* Mark an entry in the table as deleted. */
static const uint8_t JANET_SYMCACHE_DELETED[1] = {0};

/* Find an item in the cache and return its location.
 * If the item is not found, return the location
 * where one would put it. */
static const uint8_t **janet_symcache_findmem(
    const uint8_t *str,
    int32_t len,
    int32_t hash,
    int *success) {
    uint32_t bounds[4];
    uint32_t i, j, index;
    const uint8_t **firstEmpty = NULL;

    /* We will search two ranges - index to the end,
     * and 0 to the index. */
    index = (uint32_t)hash & (janet_vm_cache_capacity - 1);
    bounds[0] = index;
    bounds[1] = janet_vm_cache_capacity;
    bounds[2] = 0;
    bounds[3] = index;
    for (j = 0; j < 4; j += 2)
        for (i = bounds[j]; i < bounds[j + 1]; ++i) {
            const uint8_t *test = janet_vm_cache[i];
            /* Check empty spots */
            if (NULL == test) {
                if (NULL == firstEmpty)
                    firstEmpty = janet_vm_cache + i;
                goto notfound;
            }
            /* Check for marked deleted */
            if (JANET_SYMCACHE_DELETED == test) {
                if (firstEmpty == NULL)
                    firstEmpty = janet_vm_cache + i;
                continue;
            }
            if (janet_string_equalconst(test, str, len, hash)) {
                /* Replace first deleted */
                *success = 1;
                if (firstEmpty != NULL) {
                    *firstEmpty = test;
                    janet_vm_cache[i] = JANET_SYMCACHE_DELETED;
                    return firstEmpty;
                }
                return janet_vm_cache + i;
            }
        }
notfound:
    *success = 0;
    return firstEmpty;
}

#define janet_symcache_find(str, success) \
    janet_symcache_findmem((str), janet_string_length(str), janet_string_hash(str), (success))

/* Resize the cache. */
static void janet_cache_resize(uint32_t newCapacity) {
    uint32_t i, oldCapacity;
    const uint8_t **oldCache = janet_vm_cache;
    const uint8_t **newCache = calloc(1, newCapacity * sizeof(const uint8_t **));
    if (newCache == NULL) {
        JANET_OUT_OF_MEMORY;
    }
    oldCapacity = janet_vm_cache_capacity;
    janet_vm_cache = newCache;
    janet_vm_cache_capacity = newCapacity;
    janet_vm_cache_deleted = 0;
    /* Add all of the old cache entries back */
    for (i = 0; i < oldCapacity; ++i) {
        int status;
        const uint8_t **bucket;
        const uint8_t *x = oldCache[i];
        if (x != NULL && x != JANET_SYMCACHE_DELETED) {
            bucket = janet_symcache_find(x, &status);
            if (status || bucket == NULL) {
                /* there was a problem with the algorithm. */
                break;
            }
            *bucket = x;
        }
    }
    /* Free the old cache */
    free((void *)oldCache);
}

/* Add an item to the cache */
static void janet_symcache_put(const uint8_t *x, const uint8_t **bucket) {
    if ((janet_vm_cache_count + janet_vm_cache_deleted) * 2 > janet_vm_cache_capacity) {
        int status;
        janet_cache_resize(janet_tablen((2 * janet_vm_cache_count + 1)));
        bucket = janet_symcache_find(x, &status);
    }
    /* Add x to the cache */
    janet_vm_cache_count++;
    *bucket = x;
}

/* Remove a symbol from the symcache */
void janet_symbol_deinit(const uint8_t *sym) {
    int status = 0;
    const uint8_t **bucket = janet_symcache_find(sym, &status);
    if (status) {
        janet_vm_cache_count--;
        janet_vm_cache_deleted++;
        *bucket = JANET_SYMCACHE_DELETED;
    }
}

/* Create a symbol from a byte string */
const uint8_t *janet_symbol(const uint8_t *str, int32_t len) {
    int32_t hash = janet_string_calchash(str, len);
    uint8_t *newstr;
    int success = 0;
    const uint8_t **bucket = janet_symcache_findmem(str, len, hash, &success);
    if (success)
        return *bucket;
    newstr = (uint8_t *) janet_gcalloc(JANET_MEMORY_SYMBOL, 2 * sizeof(int32_t) + len + 1)
             + (2 * sizeof(int32_t));
    janet_string_hash(newstr) = hash;
    janet_string_length(newstr) = len;
    memcpy(newstr, str, len);
    newstr[len] = 0;
    janet_symcache_put((const uint8_t *)newstr, bucket);
    return newstr;
}

/* Get a symbol from a cstring */
const uint8_t *janet_csymbol(const char *cstr) {
    int32_t len = 0;
    while (cstr[len]) len++;
    return janet_symbol((const uint8_t *)cstr, len);
}

/* Store counter for genysm to avoid quadratic behavior */
JANET_THREAD_LOCAL uint8_t gensym_counter[8] = {'_', '0', '0', '0', '0', '0', '0', 0};

/* Increment the gensym buffer */
static void inc_gensym(void) {
    for (int i = sizeof(gensym_counter) - 2; i; i--) {
        if (gensym_counter[i] == '9') {
            gensym_counter[i] = 'a';
            break;
        } else if (gensym_counter[i] == 'z') {
            gensym_counter[i] = 'A';
            break;
        } else if (gensym_counter[i] == 'Z') {
            gensym_counter[i] = '0';
        } else {
            gensym_counter[i]++;
            break;
        }
    }
}

/* Generate a unique symbol. This is used in the library function gensym. The
 * symbol will be of the format _XXXXXX, where X is a base64 digit, and
 * prefix is the argument passed. No prefix for speed. */
const uint8_t *janet_symbol_gen(void) {
    const uint8_t **bucket = NULL;
    uint8_t *sym;
    int32_t hash = 0;
    int status;
    /* Leave spaces for 6 base 64 digits and two dashes. That means 64^6 possible suffixes, which
     * is enough for resolving collisions. */
    do {
        hash = janet_string_calchash(
                   gensym_counter,
                   sizeof(gensym_counter) - 1);
        bucket = janet_symcache_findmem(
                     gensym_counter,
                     sizeof(gensym_counter) - 1,
                     hash,
                     &status);
    } while (status && (inc_gensym(), 1));
    sym = (uint8_t *) janet_gcalloc(
              JANET_MEMORY_SYMBOL,
              2 * sizeof(int32_t) + sizeof(gensym_counter)) +
          (2 * sizeof(int32_t));
    memcpy(sym, gensym_counter, sizeof(gensym_counter));
    janet_string_length(sym) = sizeof(gensym_counter) - 1;
    janet_string_hash(sym) = hash;
    janet_symcache_put((const uint8_t *)sym, bucket);
    return (const uint8_t *)sym;
}
