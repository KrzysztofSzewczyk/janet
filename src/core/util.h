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

#ifndef JANET_UTIL_H_defined
#define JANET_UTIL_H_defined

#include <janet/janet.h>

/* Utils */
#define janet_maphash(cap, hash) ((uint32_t)(hash) & (cap - 1))
extern const char janet_base64[65];
int32_t janet_array_calchash(const Janet *array, int32_t len);
int32_t janet_kv_calchash(const JanetKV *kvs, int32_t len);
int32_t janet_string_calchash(const uint8_t *str, int32_t len);
int32_t janet_tablen(int32_t n);
void janet_buffer_push_types(JanetBuffer *buffer, int types);
const JanetKV *janet_dict_find(const JanetKV *buckets, int32_t cap, Janet key);
Janet janet_dict_get(const JanetKV *buckets, int32_t cap, Janet key);
const void *janet_strbinsearch(
        const void *tab,
        size_t tabcount,
        size_t itemsize,
        const uint8_t *key);

/* Initialize builtin libraries */
int janet_lib_io(JanetArgs args);
int janet_lib_math(JanetArgs args);
int janet_lib_array(JanetArgs args);
int janet_lib_tuple(JanetArgs args);
int janet_lib_buffer(JanetArgs args);
int janet_lib_table(JanetArgs args);
int janet_lib_fiber(JanetArgs args);
int janet_lib_os(JanetArgs args);
int janet_lib_string(JanetArgs args);
int janet_lib_marsh(JanetArgs args);
int janet_lib_parse(JanetArgs args);
#ifdef JANET_ASSEMBLER
int janet_lib_asm(JanetArgs args);
#endif
int janet_lib_compile(JanetArgs args);
int janet_lib_debug(JanetArgs args);

#endif
