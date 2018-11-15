/*
* Copyright (c) 2018 Calvin Rose
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

#include <janet/janet.h>
#include "util.h"
#include "state.h"
#include "gc.h"

/* Base 64 lookup table for digits */
const char janet_base64[65] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "_=";

/* The JANET value types in order. These types can be used as
 * mnemonics instead of a bit pattern for type checking */
const char *const janet_type_names[16] = {
    ":nil",
    ":boolean",
    ":boolean",
    ":fiber",
    ":integer",
    ":real",
    ":string",
    ":symbol",
    ":array",
    ":tuple",
    ":table",
    ":struct",
    ":buffer",
    ":function",
    ":cfunction",
    ":abstract"
};

/* Calculate hash for string */

int32_t janet_string_calchash(const uint8_t *str, int32_t len) {
    const uint8_t *end = str + len;
    uint32_t hash = 5381;
    while (str < end)
        hash = (hash << 5) + hash + *str++;
    return (int32_t) hash;
}

/* Computes hash of an array of values */
int32_t janet_array_calchash(const Janet *array, int32_t len) {
    const Janet *end = array + len;
    uint32_t hash = 5381;
    while (array < end)
        hash = (hash << 5) + hash + janet_hash(*array++);
    return (int32_t) hash;
}

/* Computes hash of an array of values */
int32_t janet_kv_calchash(const JanetKV *kvs, int32_t len) {
    const JanetKV *end = kvs + len;
    uint32_t hash = 5381;
    while (kvs < end) {
        hash = (hash << 5) + hash + janet_hash(kvs->key);
        hash = (hash << 5) + hash + janet_hash(kvs->value);
        kvs++;
    }
    return (int32_t) hash;
}

/* Calculate next power of 2. May overflow. If n is 0,
 * will return 0. */
int32_t janet_tablen(int32_t n) {
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

/* Helper to find a value in a Janet struct or table. Returns the bucket
 * containg the key, or the first empty bucket if there is no such key. */
const JanetKV *janet_dict_find(const JanetKV *buckets, int32_t cap, Janet key) {
    int32_t index = janet_maphash(cap, janet_hash(key));
    int32_t i;
    const JanetKV *first_bucket = NULL;
    /* Higher half */
    for (i = index; i < cap; i++) {
        const JanetKV *kv = buckets + i;
        if (janet_checktype(kv->key, JANET_NIL)) {
            if (janet_checktype(kv->value, JANET_NIL)) {
                return kv;
            } else if (NULL == first_bucket) {
                first_bucket = kv;
            }
        } else if (janet_equals(kv->key, key)) {
            return buckets + i;
        }
    }
    /* Lower half */
    for (i = 0; i < index; i++) {
        const JanetKV *kv = buckets + i;
        if (janet_checktype(kv->key, JANET_NIL)) {
            if (janet_checktype(kv->value, JANET_NIL)) {
                return kv;
            } else if (NULL == first_bucket) {
                first_bucket = kv;
            }
        } else if (janet_equals(kv->key, key)) {
            return buckets + i;
        }
    }
    return first_bucket;
}

/* Get a value from a janet struct or table. */
Janet janet_dictionary_get(const JanetKV *data, int32_t cap, Janet key) {
    const JanetKV *kv = janet_dict_find(data, cap, key);
    if (kv && !janet_checktype(kv->key, JANET_NIL)) {
        return kv->value;
    }
    return janet_wrap_nil();
}

/* Iterate through a struct or dictionary generically */
const JanetKV *janet_dictionary_next(const JanetKV *kvs, int32_t cap, const JanetKV *kv) {
    const JanetKV *end = kvs + cap;
    kv = (kv == NULL) ? kvs : kv + 1;
    while (kv < end) {
        if (!janet_checktype(kv->key, JANET_NIL))
            return kv;
        kv++;
    }
    return NULL;
}

/* Compare a janet string with a cstring. more efficient than loading
 * c string as a janet string. */
int janet_cstrcmp(const uint8_t *str, const char *other) {
    int32_t len = janet_string_length(str);
    int32_t index;
    for (index = 0; index < len; index++) {
        uint8_t c = str[index];
        uint8_t k = ((const uint8_t *)other)[index];
        if (c < k) return -1;
        if (c > k) return 1;
        if (k == '\0') break;
    }
    return (other[index] == '\0') ? 0 : -1;
}

/* Do a binary search on a static array of structs. Each struct must
 * have a string as its first element, and the struct must be sorted
 * lexogrpahically by that element. */
const void *janet_strbinsearch(
        const void *tab,
        size_t tabcount,
        size_t itemsize,
        const uint8_t *key) {
    size_t low = 0;
    size_t hi = tabcount;
    const char *t = (const char *)tab;
    while (low < hi) {
        size_t mid = low + ((hi - low) / 2);
        const char **item = (const char **)(t + mid * itemsize);
        const char *name = *item;
        int comp = janet_cstrcmp(key, name);
        if (comp < 0) {
            hi = mid;
        } else if (comp > 0) {
            low = mid + 1;
        } else {
            return (const void *)item;
        }
    }
    return NULL;
}

/* Register a value in the global registry */
void janet_register(const char *name, JanetCFunction cfun) {
    Janet key = janet_wrap_cfunction(cfun);
    Janet value = janet_csymbolv(name);
    janet_table_put(janet_vm_registry, key, value);
}

/* Add a def to an environment */
void janet_def(JanetTable *env, const char *name, Janet val, const char *doc) {
    JanetTable *subt = janet_table(2);
    janet_table_put(subt, janet_csymbolv(":value"), val);
    if (doc)
        janet_table_put(subt, janet_csymbolv(":doc"), janet_cstringv(doc));
    janet_table_put(env, janet_csymbolv(name), janet_wrap_table(subt));
}

/* Add a var to the environment */
void janet_var(JanetTable *env, const char *name, Janet val, const char *doc) {
    JanetArray *array = janet_array(1);
    JanetTable *subt = janet_table(2);
    janet_array_push(array, val);
    janet_table_put(subt, janet_csymbolv(":ref"), janet_wrap_array(array));
    if (doc)
        janet_table_put(subt, janet_csymbolv(":doc"), janet_cstringv(doc));
    janet_table_put(env, janet_csymbolv(name), janet_wrap_table(subt));
}

/* Load many cfunctions at once */
void janet_cfuns(JanetTable *env, const char *regprefix, const JanetReg *cfuns) {
    while (cfuns->name) {
        Janet name = janet_csymbolv(cfuns->name);
        Janet longname = name;
        if (regprefix) {
            int32_t reglen = 0;
            int32_t nmlen = 0;
            while (regprefix[reglen]) reglen++;
            while (cfuns->name[nmlen]) nmlen++;
            uint8_t *longname_buffer =
                janet_string_begin(reglen + 1 + nmlen);
            memcpy(longname_buffer, regprefix, reglen);
            longname_buffer[reglen] = '.';
            memcpy(longname_buffer + reglen + 1, cfuns->name, nmlen);
            longname = janet_wrap_symbol(janet_string_end(longname_buffer));
        }
        Janet fun = janet_wrap_cfunction(cfuns->cfun);
        janet_def(env, cfuns->name, fun, cfuns->documentation);
        janet_table_put(janet_vm_registry, fun, longname);
        cfuns++;
    }
}

/* Resolve a symbol in the environment */
JanetBindingType janet_resolve(JanetTable *env, const uint8_t *sym, Janet *out) {
    Janet ref;
    JanetTable *entry_table;
    Janet entry = janet_table_get(env, janet_wrap_symbol(sym));
    if (!janet_checktype(entry, JANET_TABLE))
        return JANET_BINDING_NONE;
    entry_table = janet_unwrap_table(entry);
    if (!janet_checktype(
        janet_table_get(entry_table, janet_csymbolv(":macro")),
        JANET_NIL)) {
        *out = janet_table_get(entry_table, janet_csymbolv(":value"));
        return JANET_BINDING_MACRO;
    }
    ref = janet_table_get(entry_table, janet_csymbolv(":ref"));
    if (janet_checktype(ref, JANET_ARRAY)) {
        *out = ref;
        return JANET_BINDING_VAR;
    }
    *out = janet_table_get(entry_table, janet_csymbolv(":value"));
    return JANET_BINDING_DEF;
}

/* Get module from the arguments passed to library */
JanetTable *janet_env(JanetArgs args) {
    JanetTable *module;
    if (args.n >= 1 && janet_checktype(args.v[0], JANET_TABLE)) {
        module = janet_unwrap_table(args.v[0]);
    } else {
        module = janet_table(0);
    }
    *args.ret = janet_wrap_table(module);
    return module;
}

/* Read both tuples and arrays as c pointers + int32_t length. Return 1 if the
 * view can be constructed, 0 if an invalid type. */
int janet_indexed_view(Janet seq, const Janet **data, int32_t *len) {
    if (janet_checktype(seq, JANET_ARRAY)) {
        *data = janet_unwrap_array(seq)->data;
        *len = janet_unwrap_array(seq)->count;
        return 1;
    } else if (janet_checktype(seq, JANET_TUPLE)) {
        *data = janet_unwrap_tuple(seq);
        *len = janet_tuple_length(janet_unwrap_struct(seq));
        return 1;
    }
    return 0;
}

/* Read both strings and buffer as unsigned character array + int32_t len.
 * Returns 1 if the view can be constructed and 0 if the type is invalid. */
int janet_bytes_view(Janet str, const uint8_t **data, int32_t *len) {
    if (janet_checktype(str, JANET_STRING) || janet_checktype(str, JANET_SYMBOL)) {
        *data = janet_unwrap_string(str);
        *len = janet_string_length(janet_unwrap_string(str));
        return 1;
    } else if (janet_checktype(str, JANET_BUFFER)) {
        *data = janet_unwrap_buffer(str)->data;
        *len = janet_unwrap_buffer(str)->count;
        return 1;
    }
    return 0;
}

/* Read both structs and tables as the entries of a hashtable with
 * identical structure. Returns 1 if the view can be constructed and
 * 0 if the type is invalid. */
int janet_dictionary_view(Janet tab, const JanetKV **data, int32_t *len, int32_t *cap) {
    if (janet_checktype(tab, JANET_TABLE)) {
        *data = janet_unwrap_table(tab)->data;
        *cap = janet_unwrap_table(tab)->capacity;
        *len = janet_unwrap_table(tab)->count;
        return 1;
    } else if (janet_checktype(tab, JANET_STRUCT)) {
        *data = janet_unwrap_struct(tab);
        *cap = janet_struct_capacity(janet_unwrap_struct(tab));
        *len = janet_struct_length(janet_unwrap_struct(tab));
        return 1;
    }
    return 0;
}

/* Get actual type name of a value for debugging purposes */
static const char *typestr(JanetArgs args, int32_t n) {
    JanetType actual = n < args.n ? janet_type(args.v[n]) : JANET_NIL;
    return ((actual == JANET_ABSTRACT)
        ? janet_abstract_type(janet_unwrap_abstract(args.v[n]))->name
        : janet_type_names[actual]) + 1;
}

int janet_type_err(JanetArgs args, int32_t n, JanetType expected) {
    const uint8_t *message = janet_formatc(
            "bad slot #%d, expected %t, got %s",
            n,
            expected,
            typestr(args, n));
    JANET_THROWV(args, janet_wrap_string(message));
}

void janet_buffer_push_types(JanetBuffer *buffer, int types) {
    int first = 1;
    int i = 0;
    while (types) {
        if (1 & types) {
            if (first) {
                first = 0;
            } else {
                janet_buffer_push_u8(buffer, '|');
            }
            janet_buffer_push_cstring(buffer, janet_type_names[i] + 1);
        }
        i++;
        types >>= 1;
    }
}

int janet_typemany_err(JanetArgs args, int32_t n, int expected) {
    const uint8_t *message;
    JanetBuffer buf;
    janet_buffer_init(&buf, 20);
    janet_buffer_push_string(&buf, janet_formatc("bad slot #%d, expected ", n));
    janet_buffer_push_types(&buf, expected);
    janet_buffer_push_cstring(&buf, ", got ");
    janet_buffer_push_cstring(&buf, typestr(args, n));
    message = janet_string(buf.data, buf.count);
    janet_buffer_deinit(&buf);
    JANET_THROWV(args, janet_wrap_string(message));
}

int janet_arity_err(JanetArgs args, int32_t n, const char *prefix) {
    JANET_THROWV(args,
            janet_wrap_string(janet_formatc(
                    "expected %s%d argument%s, got %d",
                    prefix, n, n == 1 ? "" : "s", args.n)));
}

int janet_typeabstract_err(JanetArgs args, int32_t n, const JanetAbstractType *at) {
    JANET_THROWV(args,
            janet_wrap_string(janet_formatc(
                    "bad slot #%d, expected %s, got %s",
                    n, at->name, typestr(args, n))));
}
