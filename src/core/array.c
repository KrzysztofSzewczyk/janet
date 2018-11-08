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
#include "gc.h"
#include <string.h>

/* Initializes an array */
JanetArray *janet_array_init(JanetArray *array, int32_t capacity) {
    Janet *data = NULL;
    if (capacity > 0) {
        data = (Janet *) malloc(sizeof(Janet) * capacity);
        if (NULL == data) {
            JANET_OUT_OF_MEMORY;
        }
    }
    array->count = 0;
    array->capacity = capacity;
    array->data = data;
    return array;
}

void janet_array_deinit(JanetArray *array) {
    free(array->data);
}

/* Creates a new array */
JanetArray *janet_array(int32_t capacity) {
    JanetArray *array = janet_gcalloc(JANET_MEMORY_ARRAY, sizeof(JanetArray));
    return janet_array_init(array, capacity);
}

/* Creates a new array from n elements. */
JanetArray *janet_array_n(const Janet *elements, int32_t n) {
    JanetArray *array = janet_gcalloc(JANET_MEMORY_ARRAY, sizeof(JanetArray));
    array->capacity = n;
    array->count = n;
    array->data = malloc(sizeof(Janet) * n);
    if (!array->data) {
        JANET_OUT_OF_MEMORY;
    }
    memcpy(array->data, elements, sizeof(Janet) * n);
    return array;
}

/* Ensure the array has enough capacity for elements */
void janet_array_ensure(JanetArray *array, int32_t capacity, int32_t growth) {
    Janet *newData;
    Janet *old = array->data;
    if (capacity <= array->capacity) return;
    capacity *= growth;
    newData = realloc(old, capacity * sizeof(Janet));
    if (NULL == newData) {
        JANET_OUT_OF_MEMORY;
    }
    array->data = newData;
    array->capacity = capacity;
}

/* Set the count of an array. Extend with nil if needed. */
void janet_array_setcount(JanetArray *array, int32_t count) {
    if (count < 0)
        return;
    if (count > array->count) {
        int32_t i;
        janet_array_ensure(array, count, 1);
        for (i = array->count; i < count; i++) {
            array->data[i] = janet_wrap_nil();
        }
    }
    array->count = count;
}

/* Push a value to the top of the array */
void janet_array_push(JanetArray *array, Janet x) {
    int32_t newcount = array->count + 1;
    janet_array_ensure(array, newcount, 2);
    array->data[array->count] = x;
    array->count = newcount;
}

/* Pop a value from the top of the array */
Janet janet_array_pop(JanetArray *array) {
    if (array->count) {
        return array->data[--array->count];
    } else {
        return janet_wrap_nil();
    }
}

/* Look at the last value in the array */
Janet janet_array_peek(JanetArray *array) {
    if (array->count) {
        return array->data[array->count - 1];
    } else {
        return janet_wrap_nil();
    }
}

/* C Functions */

static int cfun_new(JanetArgs args) {
    int32_t cap;
    JanetArray *array;
    JANET_FIXARITY(args, 1);
    JANET_ARG_INTEGER(cap, args, 0);
    array = janet_array(cap);
    JANET_RETURN_ARRAY(args, array);
}

static int cfun_pop(JanetArgs args) {
    JanetArray *array;
    JANET_FIXARITY(args, 1);
    JANET_ARG_ARRAY(array, args, 0);
    JANET_RETURN(args, janet_array_pop(array));
}

static int cfun_peek(JanetArgs args) {
    JanetArray *array;
    JANET_FIXARITY(args, 1);
    JANET_ARG_ARRAY(array, args, 0);
    JANET_RETURN(args, janet_array_peek(array));
}

static int cfun_push(JanetArgs args) {
    JanetArray *array;
    int32_t newcount;
    JANET_MINARITY(args, 1);
    JANET_ARG_ARRAY(array, args, 0);
    newcount = array->count - 1 + args.n;
    janet_array_ensure(array, newcount, 2);
    if (args.n > 1) memcpy(array->data + array->count, args.v + 1, (args.n - 1) * sizeof(Janet));
    array->count = newcount;
    JANET_RETURN(args, args.v[0]);
}

static int cfun_setcount(JanetArgs args) {
    JanetArray *array;
    int32_t newcount;
    JANET_FIXARITY(args, 2);
    JANET_ARG_ARRAY(array, args, 0);
    JANET_ARG_INTEGER(newcount, args, 1);
    if (newcount < 0) JANET_THROW(args, "expected positive integer");
    janet_array_setcount(array, newcount);
    JANET_RETURN(args, args.v[0]);
}

static int cfun_ensure(JanetArgs args) {
    JanetArray *array;
    int32_t newcount;
    int32_t growth;
    JANET_FIXARITY(args, 3);
    JANET_ARG_ARRAY(array, args, 0);
    JANET_ARG_INTEGER(newcount, args, 1);
    JANET_ARG_INTEGER(growth, args, 2);
    if (newcount < 0) JANET_THROW(args, "expected positive integer");
    janet_array_ensure(array, newcount, growth);
    JANET_RETURN(args, args.v[0]);
}

static int cfun_slice(JanetArgs args) {
    const Janet *vals;
    int32_t len;
    JanetArray *ret;
    int32_t start, end;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 3);
    if (!janet_indexed_view(args.v[0], &vals, &len))
        JANET_THROW(args, "expected array|tuple");
    /* Get start */
    if (args.n < 2) {
        start = 0;
    } else if (janet_checktype(args.v[1], JANET_INTEGER)) {
        start = janet_unwrap_integer(args.v[1]);
    } else {
        JANET_THROW(args, "expected integer");
    }
    /* Get end */
    if (args.n < 3) {
        end = -1;
    } else if (janet_checktype(args.v[2], JANET_INTEGER)) {
        end = janet_unwrap_integer(args.v[2]);
    } else {
        JANET_THROW(args, "expected integer");
    }
    if (start < 0) start = len + start;
    if (end < 0) end = len + end + 1;
    if (end >= start) {
        ret = janet_array(end - start);
        memcpy(ret->data, vals + start, sizeof(Janet) * (end - start));
        ret->count = end - start;
    } else {
        ret = janet_array(0);
    }
    JANET_RETURN_ARRAY(args, ret);
}

static int cfun_concat(JanetArgs args) {
    int32_t i;
    JanetArray *array;
    JANET_MINARITY(args, 1);
    JANET_ARG_ARRAY(array, args, 0);
    for (i = 1; i < args.n; i++) {
        switch (janet_type(args.v[i])) {
            default:
                janet_array_push(array, args.v[i]);
                break;
            case JANET_ARRAY:
            case JANET_TUPLE:
                {
                    int32_t j, len;
                    const Janet *vals;
                    janet_indexed_view(args.v[i], &vals, &len);
                    for (j = 0; j < len; j++)
                        janet_array_push(array, vals[j]);
                }
                break;
        }
    }
    JANET_RETURN_ARRAY(args, array);
}

static const JanetReg cfuns[] = {
    {"array.new", cfun_new},
    {"array.pop", cfun_pop},
    {"array.peek", cfun_peek},
    {"array.push", cfun_push},
    {"array.setcount", cfun_setcount},
    {"array.ensure", cfun_ensure},
    {"array.slice", cfun_slice},
    {"array.concat", cfun_concat},
    {NULL, NULL}
};

/* Load the array module */
int janet_lib_array(JanetArgs args) {
    JanetTable *env = janet_env(args);
    janet_cfuns(env, NULL, cfuns);
    return 0;
}
