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

/*
 * Define a number of functions that can be used internally on ANY Janet.
 */

/* Check if two values are equal. This is strict equality with no conversion. */
int janet_equals(Janet x, Janet y) {
    int result = 0;
    if (janet_type(x) != janet_type(y)) {
        result = 0;
    } else {
        switch (janet_type(x)) {
        case JANET_NIL:
        case JANET_TRUE:
        case JANET_FALSE:
            result = 1;
            break;
        case JANET_NUMBER:
            result = (janet_unwrap_number(x) == janet_unwrap_number(y));
            break;
        case JANET_STRING:
            result = janet_string_equal(janet_unwrap_string(x), janet_unwrap_string(y));
            break;
        case JANET_TUPLE:
            result = janet_tuple_equal(janet_unwrap_tuple(x), janet_unwrap_tuple(y));
            break;
        case JANET_STRUCT:
            result = janet_struct_equal(janet_unwrap_struct(x), janet_unwrap_struct(y));
            break;
        default:
            /* compare pointers */
            result = (janet_unwrap_pointer(x) == janet_unwrap_pointer(y));
            break;
        }
    }
    return result;
}

/* Computes a hash value for a function */
int32_t janet_hash(Janet x) {
    int32_t hash = 0;
    switch (janet_type(x)) {
    case JANET_NIL:
        hash = 0;
        break;
    case JANET_FALSE:
        hash = 1;
        break;
    case JANET_TRUE:
        hash = 2;
        break;
    case JANET_STRING:
    case JANET_SYMBOL:
        hash = janet_string_hash(janet_unwrap_string(x));
        break;
    case JANET_TUPLE:
        hash = janet_tuple_hash(janet_unwrap_tuple(x));
        break;
    case JANET_STRUCT:
        hash = janet_struct_hash(janet_unwrap_struct(x));
        break;
    default:
        /* TODO - test performance with different hash functions */
        if (sizeof(double) == sizeof(void *)) {
            /* Assuming 8 byte pointer */
            uint64_t i = janet_u64(x);
            hash = (int32_t)(i & 0xFFFFFFFF);
            /* Get a bit more entropy by shifting the low bits out */
            hash >>= 3;
            hash ^= (int32_t) (i >> 32);
        } else {
            /* Assuming 4 byte pointer (or smaller) */
            hash = (int32_t) ((char *)janet_unwrap_pointer(x) - (char *)0);
            hash >>= 2;
        }
        break;
    }
    return hash;
}

/* Compares x to y. If they are equal retuns 0. If x is less, returns -1.
 * If y is less, returns 1. All types are comparable
 * and should have strict ordering. */
int janet_compare(Janet x, Janet y) {
    if (janet_type(x) == janet_type(y)) {
        switch (janet_type(x)) {
            case JANET_NIL:
            case JANET_FALSE:
            case JANET_TRUE:
                return 0;
            case JANET_NUMBER:
                /* Check for nans to ensure total order */
                if (janet_unwrap_number(x) != janet_unwrap_number(x))
                    return janet_unwrap_number(y) != janet_unwrap_number(y)
                        ? 0
                        : -1;
                if (janet_unwrap_number(y) != janet_unwrap_number(y))
                    return 1;

                if (janet_unwrap_number(x) == janet_unwrap_number(y)) {
                    return 0;
                } else {
                    return janet_unwrap_number(x) > janet_unwrap_number(y) ? 1 : -1;
                }
            case JANET_STRING:
            case JANET_SYMBOL:
                return janet_string_compare(janet_unwrap_string(x), janet_unwrap_string(y));
            case JANET_TUPLE:
                return janet_tuple_compare(janet_unwrap_tuple(x), janet_unwrap_tuple(y));
            case JANET_STRUCT:
                return janet_struct_compare(janet_unwrap_struct(x), janet_unwrap_struct(y));
            default:
                if (janet_unwrap_string(x) == janet_unwrap_string(y)) {
                    return 0;
                } else {
                    return janet_unwrap_string(x) > janet_unwrap_string(y) ? 1 : -1;
                }
        }
    }
    return (janet_type(x) < janet_type(y)) ? -1 : 1;
}

/* Gets a value and returns.  If successful, return 0. If there is an error,
 * returns -1 for bad ds, -2 for bad key */
int janet_get(Janet ds, Janet key, Janet *out) {
    Janet value;
    switch (janet_type(ds)) {
        default:
            return -1;
        case JANET_STRUCT:
            value = janet_struct_get(janet_unwrap_struct(ds), key);
            break;
        case JANET_TABLE:
            value = janet_table_get(janet_unwrap_table(ds), key);
            break;
        case JANET_ARRAY:
            {
                JanetArray *array = janet_unwrap_array(ds);
                int32_t index;
                if (!janet_checkint(key)) return -2;
                index = janet_unwrap_integer(key);
                if (index < 0 || index >= array->count) {
                    value = janet_wrap_nil();
                } else {
                    value = array->data[index];
                }
                break;
            }
        case JANET_TUPLE:
            {
                const Janet *tuple = janet_unwrap_tuple(ds);
                int32_t index;
                if (!janet_checkint(key)) return -2;
                index = janet_unwrap_integer(key);
                if (index < 0 || index >= janet_tuple_length(tuple)) {
                    /*vm_throw("index out of bounds");*/
                    value = janet_wrap_nil();
                } else {
                    value = tuple[index];
                }
                break;
            }
        case JANET_BUFFER:
            {
                JanetBuffer *buffer = janet_unwrap_buffer(ds);
                int32_t index;
                if (!janet_checkint(key)) return -2;
                index = janet_unwrap_integer(key);
                if (index < 0 || index >= buffer->count) {
                    value = janet_wrap_nil();
                } else {
                    value = janet_wrap_integer(buffer->data[index]);
                }
                break;
            }
        case JANET_STRING:
        case JANET_SYMBOL:
            {
                const uint8_t *str = janet_unwrap_string(ds);
                int32_t index;
                if (!janet_checkint(key)) return -2;
                index = janet_unwrap_integer(key);
                if (index < 0 || index >= janet_string_length(str)) {
                    value = janet_wrap_nil();
                } else {
                    value = janet_wrap_integer(str[index]);
                }
                break;
            }
    }
    *out = value;
    return 0;
}

int janet_getindex(Janet ds, int32_t index, Janet *out) {
    Janet value;
    if (index < 0)
        return -2;
    switch (janet_type(ds)) {
        default:
            return -1;
        case JANET_STRING:
        case JANET_SYMBOL:
            if (index >= janet_string_length(janet_unwrap_string(ds))) {
                value = janet_wrap_nil();
            } else {
                value = janet_wrap_integer(janet_unwrap_string(ds)[index]);
            }
            break;
        case JANET_ARRAY:
            if (index >= janet_unwrap_array(ds)->count) {
                value = janet_wrap_nil();
            } else {
                value = janet_unwrap_array(ds)->data[index];
            }
            break;
        case JANET_BUFFER:
            if (index >= janet_unwrap_buffer(ds)->count) {
                value = janet_wrap_nil();
            } else {
                value = janet_wrap_integer(janet_unwrap_buffer(ds)->data[index]);
            }
            break;
        case JANET_TUPLE:
            if (index >= janet_tuple_length(janet_unwrap_tuple(ds))) {
                value = janet_wrap_nil();
            } else {
                value = janet_unwrap_tuple(ds)[index];
            }
            break;
        case JANET_TABLE:
            value = janet_table_get(janet_unwrap_table(ds), janet_wrap_integer(index));
            break;
        case JANET_STRUCT:
            value = janet_struct_get(janet_unwrap_struct(ds), janet_wrap_integer(index));
            break;
    }
    *out = value;
    return 0;
}

int janet_length(Janet x, int32_t *out) {
    int32_t len;
    switch (janet_type(x)) {
        default:
            return -1;
        case JANET_STRING:
        case JANET_SYMBOL:
            len = janet_string_length(janet_unwrap_string(x));
            break;
        case JANET_ARRAY:
            len = janet_unwrap_array(x)->count;
            break;
        case JANET_BUFFER:
            len = janet_unwrap_buffer(x)->count;
            break;
        case JANET_TUPLE:
            len = janet_tuple_length(janet_unwrap_tuple(x));
            break;
        case JANET_STRUCT:
            len = janet_struct_length(janet_unwrap_struct(x));
            break;
        case JANET_TABLE:
            len = janet_unwrap_table(x)->count;
            break;
    }
    *out = len;
    return 0;
}

int janet_putindex(Janet ds, int32_t index, Janet value) {
    switch (janet_type(ds)) {
        default:
            return -1;
        case JANET_ARRAY:
            {
                JanetArray *array = janet_unwrap_array(ds);
                if (index >= array->count) {
                    janet_array_ensure(array, index + 1, 2);
                    array->count = index + 1;
                }
                array->data[index] = value;
                break;
            }
        case JANET_BUFFER:
            {
                JanetBuffer *buffer = janet_unwrap_buffer(ds);
                if (!janet_checkint(value)) return -3;
                if (index >= buffer->count) {
                    janet_buffer_ensure(buffer, index + 1, 2);
                    buffer->count = index + 1;
                }
                buffer->data[index] = janet_unwrap_integer(value);
                break;
            }
        case JANET_TABLE:
            {
                JanetTable *table = janet_unwrap_table(ds);
                janet_table_put(table, janet_wrap_integer(index), value);
                break;
            }
    }
    return 0;
}

int janet_put(Janet ds, Janet key, Janet value) {
    switch (janet_type(ds)) {
        default:
            return -1;
        case JANET_ARRAY:
            {
                int32_t index;
                JanetArray *array = janet_unwrap_array(ds);
                if (!janet_checkint(key)) return -2;
                index = janet_unwrap_integer(key);
                if (index < 0 || index == INT32_MAX) return -2;
                if (index >= array->count) {
                    janet_array_setcount(array, index + 1);
                }
                array->data[index] = value;
                break;
            }
        case JANET_BUFFER:
            {
                int32_t index;
                JanetBuffer *buffer = janet_unwrap_buffer(ds);
                if (!janet_checkint(key)) return -2;
                index = janet_unwrap_integer(key);
                if (index < 0 || index == INT32_MAX) return -2;
                if (!janet_checkint(value)) return -3;
                if (index >= buffer->count) {
                    janet_buffer_setcount(buffer, index + 1);
                }
                buffer->data[index] = (uint8_t) (janet_unwrap_integer(value) & 0xFF);
                break;
            }
        case JANET_TABLE:
            janet_table_put(janet_unwrap_table(ds), key, value);
            break;
    }
    return 0;
}
