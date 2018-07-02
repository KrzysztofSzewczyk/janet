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

#ifndef DST_CORELIB_H_defined
#define DST_CORELIB_H_defined

#ifdef __cplusplus
extern "C" {
#endif

#include "dsttypes.h"

/* Native */
int dst_core_native(DstArgs args);

/* Arithmetic */
int dst_int(DstArgs args);
int dst_real(DstArgs args);
int dst_rand(DstArgs args);
int dst_srand(DstArgs args);
int dst_remainder(DstArgs args);

/* Misc core functions */
int dst_core_print(DstArgs args);
int dst_core_describe(DstArgs args);
int dst_core_string(DstArgs args);
int dst_core_symbol(DstArgs args);
int dst_core_buffer(DstArgs args);
int dst_core_scannumber(DstArgs args);
int dst_core_scaninteger(DstArgs args);
int dst_core_scanreal(DstArgs args);
int dst_core_tuple(DstArgs args);
int dst_core_array(DstArgs args);
int dst_core_table(DstArgs args);
int dst_core_struct(DstArgs args);
int dst_core_buffer(DstArgs args);
int dst_core_gensym(DstArgs args);
int dst_core_type(DstArgs args);
int dst_core_next(DstArgs args);
int dst_core_hash(DstArgs args);

/* GC */
int dst_core_gccollect(DstArgs args);
int dst_core_gcsetinterval(DstArgs args);
int dst_core_gcinterval(DstArgs args);

/* Initialize builtin libraries */
int dst_lib_io(DstArgs args);
int dst_lib_math(DstArgs args);
int dst_lib_array(DstArgs args);
int dst_lib_tuple(DstArgs args);
int dst_lib_buffer(DstArgs args);
int dst_lib_table(DstArgs args);
int dst_lib_fiber(DstArgs args);
int dst_lib_os(DstArgs args);
int dst_lib_string(DstArgs args);
int dst_lib_marsh(DstArgs args);
int dst_lib_parse(DstArgs args);
int dst_lib_asm(DstArgs args);

/* Useful for compiler */
Dst dst_op_add(Dst lhs, Dst rhs);
Dst dst_op_subtract(Dst lhs, Dst rhs);

#ifdef __cplusplus
}
#endif

#endif /* DST_CORELIB_H_defined */
