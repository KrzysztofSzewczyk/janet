/*
* Copyright (c) 2017 Calvin Rose
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

#include <dst/dst.h>
#include <dst/dstcorelib.h>
#include "state.h"

int dst_core_print(DstArgs args) {
    int32_t i;
    for (i = 0; i < args.n; ++i) {
        int32_t j, len;
        const uint8_t *vstr = dst_to_string(args.v[i]);
        len = dst_string_length(vstr);
        for (j = 0; j < len; ++j) {
            putc(vstr[j], stdout);
        }
    }
    putc('\n', stdout);
    DST_RETURN_NIL();
}

int dst_core_describe(DstArgs args) {
    int32_t i;
    DstBuffer b;
    dst_buffer_init(&b, 0);
    for (i = 0; i < args.n; ++i) {
        int32_t len;
        const uint8_t *str = dst_description(args.v[i]);
        len = dst_string_length(str);
        dst_buffer_push_bytes(&b, str, len);
    }
    *args.ret = dst_stringv(b.data, b.count);
    dst_buffer_deinit(&b);
    return 0;
}

int dst_core_string(DstArgs args) {
    int32_t i;
    DstBuffer b;
    dst_buffer_init(&b, 0);
    for (i = 0; i < args.n; ++i) {
        int32_t len;
        const uint8_t *str = dst_to_string(args.v[i]);
        len = dst_string_length(str);
        dst_buffer_push_bytes(&b, str, len);
    }
    *args.ret = dst_stringv(b.data, b.count);
    dst_buffer_deinit(&b);
    return 0;
}

int dst_core_symbol(DstArgs args) {
    int32_t i;
    DstBuffer b;
    dst_buffer_init(&b, 0);
    for (i = 0; i < args.n; ++i) {
        int32_t len;
        const uint8_t *str = dst_to_string(args.v[i]);
        len = dst_string_length(str);
        dst_buffer_push_bytes(&b, str, len);
    }
    *args.ret = dst_symbolv(b.data, b.count);
    dst_buffer_deinit(&b);
    return 0;
}

int dst_core_buffer(DstArgs args) {
    int32_t i;
    DstBuffer *b = dst_buffer(0);
    for (i = 0; i < args.n; ++i) {
        int32_t len;
        const uint8_t *str = dst_to_string(args.v[i]);
        len = dst_string_length(str);
        dst_buffer_push_bytes(b, str, len);
    }
    DST_RETURN_BUFFER(args, b);
}

int dst_core_format(DstArgs args) {
    const uint8_t *format;
    int32_t i, len, n;
    DstBuffer buf;
    DST_MINARITY(args, 1);
    DST_ARG_BYTES(format, len, args, 0);
    n = 1;
    dst_buffer_init(&buf, len);
    for (i = 0; i < len; i++) {
        uint8_t c = format[i];
        if (c != '%') {
            dst_buffer_push_u8(&buf, c);
        } else {
            if (++i == len) break;
            c = format[i];
            switch (c) {
                default:
                    dst_buffer_push_u8(&buf, c);
                    break;
                case 's':
                {
                    if (n >= args.n) goto noarg;
                    dst_buffer_push_string(&buf, dst_to_string(args.v[n++]));
                    break;
                }
            }
        }
    }
    *args.ret = dst_wrap_string(dst_string(buf.data, buf.count));
    dst_buffer_deinit(&buf);
    return 0;
noarg:
    dst_buffer_deinit(&buf);
    DST_THROW(args, "not enough arguments to format");
}

int dst_core_scannumber(DstArgs args) {
    const uint8_t *data;
    Dst x;
    int32_t len;
    DST_FIXARITY(args, 1);
    DST_ARG_BYTES(data, len, args, 0);
    x = dst_scan_number(data, len);
    if (dst_checktype(x, DST_NIL)) {
        DST_THROW(args, "error parsing number");
    }
    DST_RETURN(args, x);
}

int dst_core_scaninteger(DstArgs args) {
    const uint8_t *data;
    int32_t len, ret;
    int err = 0;
    DST_FIXARITY(args, 1);
    DST_ARG_BYTES(data, len, args, 0);
    ret = dst_scan_integer(data, len, &err);
    if (err) {
        DST_THROW(args, "error parsing integer");
    }
    DST_RETURN_INTEGER(args, ret);
}

int dst_core_scanreal(DstArgs args) {
    const uint8_t *data;
    int32_t len;
    double ret;
    int err = 0;
    DST_FIXARITY(args, 1);
    DST_ARG_BYTES(data, len, args, 0);
    ret = dst_scan_real(data, len, &err);
    if (err) {
        DST_THROW(args, "error parsing real");
    }
    DST_RETURN_REAL(args, ret);
}

int dst_core_tuple(DstArgs args) {
    DST_RETURN_TUPLE(args, dst_tuple_n(args.v, args.n));
}

int dst_core_array(DstArgs args) {
    DstArray *array = dst_array(args.n);
    array->count = args.n;
    memcpy(array->data, args.v, args.n * sizeof(Dst));
    DST_RETURN_ARRAY(args, array);
}

int dst_core_table(DstArgs args) {
    int32_t i;
    DstTable *table = dst_table(args.n >> 1);
    if (args.n & 1) 
        DST_THROW(args, "expected even number of arguments");
    for (i = 0; i < args.n; i += 2) {
        dst_table_put(table, args.v[i], args.v[i + 1]);
    }
    DST_RETURN_TABLE(args, table);
}

int dst_core_struct(DstArgs args) {
    int32_t i;
    DstKV *st = dst_struct_begin(args.n >> 1);
    if (args.n & 1)
        DST_THROW(args, "expected even number of arguments");
    for (i = 0; i < args.n; i += 2) {
        dst_struct_put(st, args.v[i], args.v[i + 1]);
    }
    DST_RETURN_STRUCT(args, dst_struct_end(st));
}

int dst_core_gensym(DstArgs args) {
    DST_MAXARITY(args, 1);
    if (args.n == 0) {
        DST_RETURN_SYMBOL(args, dst_symbol_gen(NULL, 0));
    } else {
        const uint8_t *s;
        int32_t len;
        DST_ARG_BYTES(s, len, args, 0);
        DST_RETURN_SYMBOL(args, dst_symbol_gen(s, len));
    }
}

int dst_core_length(DstArgs args) {
    DST_FIXARITY(args, 1);
    DST_RETURN_INTEGER(args, dst_length(args.v[0]));
}

int dst_core_get(DstArgs args) {
    int32_t i;
    Dst ds;
    DST_MINARITY(args, 1);
    ds = args.v[0];
    for (i = 1; i < args.n; i++) {
        ds = dst_get(ds, args.v[i]);
        if (dst_checktype(ds, DST_NIL))
            break;
    }
    DST_RETURN(args, ds);
}

int dst_core_put(DstArgs args) {
    Dst ds, key, value;
    DstArgs subargs = args;
    DST_MINARITY(args, 3);
    subargs.n -= 2;
    if (dst_core_get(subargs)) return 1;
    ds = *args.ret;
    key = args.v[args.n - 2];
    value = args.v[args.n - 1];
    dst_put(ds, key, value);
    return 0;
}

int dst_core_gccollect(DstArgs args) {
    (void) args;
    dst_collect();
    return 0;
}

int dst_core_gcsetinterval(DstArgs args) {
    int32_t val;
    DST_FIXARITY(args, 1);
    DST_ARG_INTEGER(val, args, 0);
    if (val < 0)
        DST_THROW(args, "expected non-negative integer");
    dst_vm_gc_interval = val;
    DST_RETURN_NIL(args);
}

int dst_core_gcinterval(DstArgs args) {
    DST_FIXARITY(args, 0);
    DST_RETURN_INTEGER(args, dst_vm_gc_interval);
}

int dst_core_type(DstArgs args) {
    DST_FIXARITY(args, 1);
    if (dst_checktype(args.v[0], DST_ABSTRACT)) {
        DST_RETURN(args, dst_csymbolv(dst_abstract_type(dst_unwrap_abstract(args.v[0]))->name));
    } else {
        DST_RETURN(args, dst_csymbolv(dst_type_names[dst_type(args.v[0])]));
    }
}

int dst_core_next(DstArgs args) {
    Dst ds;
    const DstKV *kv;
    DST_FIXARITY(args, 2);
    DST_CHECKMANY(args, 0, DST_TFLAG_DICTIONARY);
    ds = args.v[0];
    if (dst_checktype(ds, DST_TABLE)) {
        DstTable *t = dst_unwrap_table(ds);
        kv = dst_checktype(args.v[1], DST_NIL)
            ? NULL
            : dst_table_find(t, args.v[1]);
    } else {
        const DstKV *st = dst_unwrap_struct(ds);
        kv = dst_checktype(args.v[1], DST_NIL)
            ? NULL
            : dst_struct_find(st, args.v[1]);
    }
    kv = dst_next(ds, kv);
    if (kv) {
        DST_RETURN(args, kv->key);
    }
    DST_RETURN_NIL(args);
}

int dst_core_hash(DstArgs args) {
    DST_FIXARITY(args, 1);
    DST_RETURN_INTEGER(args, dst_hash(args.v[0]));
}

int dst_core_string_slice(DstArgs args) {
    const uint8_t *data;
    int32_t len, start, end;
    const uint8_t *ret;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 3);
    DST_ARG_BYTES(data, len, args, 0);
    /* Get start */
    if (args.n < 2) {
        start = 0;
    } else if (dst_checktype(args.v[1], DST_INTEGER)) {
        start = dst_unwrap_integer(args.v[1]);
    } else {
        DST_THROW(args, "expected integer");
    }
    /* Get end */
    if (args.n < 3) {
        end = -1;
    } else if (dst_checktype(args.v[2], DST_INTEGER)) {
        end = dst_unwrap_integer(args.v[2]);
    } else {
        DST_THROW(args, "expected integer");
    }
    if (start < 0) start = len + start;
    if (end < 0) end = len + end + 1;
    if (end >= start) {
        ret = dst_string(data + start, end - start);
    } else {
        ret = dst_cstring("");
    }
    DST_RETURN_STRING(args, ret);
}
