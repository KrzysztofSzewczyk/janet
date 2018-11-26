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
#include "state.h"
#include "symcache.h"
#include "gc.h"

/* GC State */
JANET_THREAD_LOCAL void *janet_vm_blocks;
JANET_THREAD_LOCAL uint32_t janet_vm_gc_interval;
JANET_THREAD_LOCAL uint32_t janet_vm_next_collection;
JANET_THREAD_LOCAL int janet_vm_gc_suspend = 0;

/* Roots */
JANET_THREAD_LOCAL Janet *janet_vm_roots;
JANET_THREAD_LOCAL uint32_t janet_vm_root_count;
JANET_THREAD_LOCAL uint32_t janet_vm_root_capacity;

/* Helpers for marking the various gc types */
static void janet_mark_funcenv(JanetFuncEnv *env);
static void janet_mark_funcdef(JanetFuncDef *def);
static void janet_mark_function(JanetFunction *func);
static void janet_mark_array(JanetArray *array);
static void janet_mark_table(JanetTable *table);
static void janet_mark_struct(const JanetKV *st);
static void janet_mark_tuple(const Janet *tuple);
static void janet_mark_buffer(JanetBuffer *buffer);
static void janet_mark_string(const uint8_t *str);
static void janet_mark_fiber(JanetFiber *fiber);
static void janet_mark_abstract(void *adata);

/* Local state that is only temporary */
static JANET_THREAD_LOCAL uint32_t depth = JANET_RECURSION_GUARD;
static JANET_THREAD_LOCAL uint32_t orig_rootcount;

/* Mark a value */
void janet_mark(Janet x) {
    if (depth) {
        depth--;
        switch (janet_type(x)) {
            default: break;
            case JANET_STRING:
            case JANET_SYMBOL: janet_mark_string(janet_unwrap_string(x)); break;
            case JANET_FUNCTION: janet_mark_function(janet_unwrap_function(x)); break;
            case JANET_ARRAY: janet_mark_array(janet_unwrap_array(x)); break;
            case JANET_TABLE: janet_mark_table(janet_unwrap_table(x)); break;
            case JANET_STRUCT: janet_mark_struct(janet_unwrap_struct(x)); break;
            case JANET_TUPLE: janet_mark_tuple(janet_unwrap_tuple(x)); break;
            case JANET_BUFFER: janet_mark_buffer(janet_unwrap_buffer(x)); break;
            case JANET_FIBER: janet_mark_fiber(janet_unwrap_fiber(x)); break;
            case JANET_ABSTRACT: janet_mark_abstract(janet_unwrap_abstract(x)); break;
        }
        depth++;
    } else {
        janet_gcroot(x);
    }
}

static void janet_mark_string(const uint8_t *str) {
    janet_gc_mark(janet_string_raw(str));
}

static void janet_mark_buffer(JanetBuffer *buffer) {
    janet_gc_mark(buffer);
}

static void janet_mark_abstract(void *adata) {
    if (janet_gc_reachable(janet_abstract_header(adata)))
        return;
    janet_gc_mark(janet_abstract_header(adata));
    if (janet_abstract_header(adata)->type->gcmark) {
        janet_abstract_header(adata)->type->gcmark(adata, janet_abstract_size(adata));
    }
}

/* Mark a bunch of items in memory */
static void janet_mark_many(const Janet *values, int32_t n) {
    const Janet *end = values + n;
    while (values < end) {
        janet_mark(*values);
        values += 1;
    }
}

/* Mark a bunch of key values items in memory */
static void janet_mark_kvs(const JanetKV *kvs, int32_t n) {
    const JanetKV *end = kvs + n;
    while (kvs < end) {
        janet_mark(kvs->key);
        janet_mark(kvs->value);
        kvs++;
    }
}

static void janet_mark_array(JanetArray *array) {
    if (janet_gc_reachable(array))
        return;
    janet_gc_mark(array);
    janet_mark_many(array->data, array->count);
}

static void janet_mark_table(JanetTable *table) {
    recur: /* Manual tail recursion */
    if (janet_gc_reachable(table))
        return;
    janet_gc_mark(table);
    janet_mark_kvs(table->data, table->capacity);
    if (table->proto) {
        table = table->proto;
        goto recur;
    }
}

static void janet_mark_struct(const JanetKV *st) {
    if (janet_gc_reachable(janet_struct_raw(st)))
        return;
    janet_gc_mark(janet_struct_raw(st));
    janet_mark_kvs(st, janet_struct_capacity(st));
}

static void janet_mark_tuple(const Janet *tuple) {
    if (janet_gc_reachable(janet_tuple_raw(tuple)))
        return;
    janet_gc_mark(janet_tuple_raw(tuple));
    janet_mark_many(tuple, janet_tuple_length(tuple));
}

/* Helper to mark function environments */
static void janet_mark_funcenv(JanetFuncEnv *env) {
    if (janet_gc_reachable(env))
        return;
    janet_gc_mark(env);
    if (env->offset) {
        /* On stack */
        janet_mark_fiber(env->as.fiber);
    } else {
        /* Not on stack */
        janet_mark_many(env->as.values, env->length);
    }
}

/* GC helper to mark a FuncDef */
static void janet_mark_funcdef(JanetFuncDef *def) {
    int32_t i;
    if (janet_gc_reachable(def))
        return;
    janet_gc_mark(def);
    janet_mark_many(def->constants, def->constants_length);
    for (i = 0; i < def->defs_length; ++i) {
        janet_mark_funcdef(def->defs[i]);
    }
    if (def->source)
        janet_mark_string(def->source);
    if (def->name)
        janet_mark_string(def->name);
}

static void janet_mark_function(JanetFunction *func) {
    int32_t i;
    int32_t numenvs;
    if (janet_gc_reachable(func))
        return;
    janet_gc_mark(func);
    numenvs = func->def->environments_length;
    for (i = 0; i < numenvs; ++i) {
        janet_mark_funcenv(func->envs[i]);
    }
    janet_mark_funcdef(func->def);
}

static void janet_mark_fiber(JanetFiber *fiber) {
    int32_t i, j;
    JanetStackFrame *frame;
recur:
    if (janet_gc_reachable(fiber))
        return;
    janet_gc_mark(fiber);
    i = fiber->frame;
    j = fiber->stackstart - JANET_FRAME_SIZE;
    while (i > 0) {
        frame = (JanetStackFrame *)(fiber->data + i - JANET_FRAME_SIZE);
        if (NULL != frame->func)
            janet_mark_function(frame->func);
        if (NULL != frame->env)
            janet_mark_funcenv(frame->env);
        /* Mark all values in the stack frame */
        janet_mark_many(fiber->data + i, j - i);
        j = i - JANET_FRAME_SIZE;
        i = frame->prevframe;
    }

    /* Explicit tail recursion */
    if (fiber->child) {
        fiber = fiber->child;
        goto recur;
    }
}

/* Deinitialize a block of memory */
static void janet_deinit_block(JanetGCMemoryHeader *block) {
    void *mem = ((char *)(block + 1));
    JanetAbstractHeader *h = (JanetAbstractHeader *)mem;
    switch (block->flags & JANET_MEM_TYPEBITS) {
        default:
        case JANET_MEMORY_FUNCTION:
            break; /* Do nothing for non gc types */
        case JANET_MEMORY_SYMBOL:
            janet_symbol_deinit((const uint8_t *)mem + 2 * sizeof(int32_t));
            break;
        case JANET_MEMORY_ARRAY:
            janet_array_deinit((JanetArray*) mem);
            break;
        case JANET_MEMORY_TABLE:
            janet_table_deinit((JanetTable*) mem);
            break;
        case JANET_MEMORY_FIBER:
            free(((JanetFiber *)mem)->data);
            break;
        case JANET_MEMORY_BUFFER:
            janet_buffer_deinit((JanetBuffer *) mem);
            break;
        case JANET_MEMORY_ABSTRACT:
            if (h->type->gc) {
                janet_assert(!h->type->gc((void *)(h + 1), h->size), "finalizer failed");
            }
            break;
        case JANET_MEMORY_FUNCENV:
            {
                JanetFuncEnv *env = (JanetFuncEnv *)mem;
                if (0 == env->offset)
                    free(env->as.values);
            }
            break;
        case JANET_MEMORY_FUNCDEF:
            {
                JanetFuncDef *def = (JanetFuncDef *)mem;
                /* TODO - get this all with one alloc and one free */
                free(def->defs);
                free(def->environments);
                free(def->constants);
                free(def->bytecode);
                free(def->sourcemap);
            }
            break;
    }
}

/* Iterate over all allocated memory, and free memory that is not
 * marked as reachable. Flip the gc color flag for next sweep. */
void janet_sweep() {
    JanetGCMemoryHeader *previous = NULL;
    JanetGCMemoryHeader *current = janet_vm_blocks;
    JanetGCMemoryHeader *next;
    while (NULL != current) {
        next = current->next;
        if (current->flags & (JANET_MEM_REACHABLE | JANET_MEM_DISABLED)) {
            previous = current;
            current->flags &= ~JANET_MEM_REACHABLE;
        } else {
            janet_deinit_block(current);
            if (NULL != previous) {
                previous->next = next;
            } else {
                janet_vm_blocks = next;
            }
            free(current);
        }
        current = next;
    }
}

/* Allocate some memory that is tracked for garbage collection */
void *janet_gcalloc(enum JanetMemoryType type, size_t size) {
    JanetGCMemoryHeader *mdata;
    size_t total = size + sizeof(JanetGCMemoryHeader);

    /* Make sure everything is inited */
    janet_assert(NULL != janet_vm_cache, "please initialize janet before use");
    void *mem = malloc(total);

    /* Check for bad malloc */
    if (NULL == mem) {
        JANET_OUT_OF_MEMORY;
    }

    mdata = (JanetGCMemoryHeader *)mem;

    /* Configure block */
    mdata->flags = type;

    /* Prepend block to heap list */
    janet_vm_next_collection += (int32_t) size;
    mdata->next = janet_vm_blocks;
    janet_vm_blocks = mdata;

    return (char *) mem + sizeof(JanetGCMemoryHeader);
}

/* Run garbage collection */
void janet_collect(void) {
    uint32_t i;
    if (janet_vm_gc_suspend) return;
    depth = JANET_RECURSION_GUARD;
    orig_rootcount = janet_vm_root_count;
    for (i = 0; i < orig_rootcount; i++)
        janet_mark(janet_vm_roots[i]);
    while (orig_rootcount < janet_vm_root_count) {
        Janet x = janet_vm_roots[--janet_vm_root_count];
        janet_mark(x);
    }
    janet_sweep();
    janet_vm_next_collection = 0;
}

/* Add a root value to the GC. This prevents the GC from removing a value
 * and all of its children. If gcroot is called on a value n times, unroot
 * must also be called n times to remove it as a gc root. */
void janet_gcroot(Janet root) {
    uint32_t newcount = janet_vm_root_count + 1;
    if (newcount > janet_vm_root_capacity) {
        uint32_t newcap = 2 * newcount;
        janet_vm_roots = realloc(janet_vm_roots, sizeof(Janet) * newcap);
        if (NULL == janet_vm_roots) {
            JANET_OUT_OF_MEMORY;
        }
        janet_vm_root_capacity = newcap;
    }
    janet_vm_roots[janet_vm_root_count] = root;
    janet_vm_root_count = newcount;
}

/* Identity equality for GC purposes */
static int janet_gc_idequals(Janet lhs, Janet rhs) {
    if (janet_type(lhs) != janet_type(rhs))
        return 0;
    switch (janet_type(lhs)) {
        case JANET_TRUE:
        case JANET_FALSE:
        case JANET_NIL:
            return 1;
        case JANET_INTEGER:
            return janet_unwrap_integer(lhs) == janet_unwrap_integer(rhs);
        case JANET_REAL:
            return janet_unwrap_real(lhs) == janet_unwrap_real(rhs);
        default:
            return janet_unwrap_pointer(lhs) == janet_unwrap_pointer(rhs);
    }
}

/* Remove a root value from the GC. This allows the gc to potentially reclaim
 * a value and all its children. */
int janet_gcunroot(Janet root) {
    Janet *vtop = janet_vm_roots + janet_vm_root_count;
    Janet *v = janet_vm_roots;
    /* Search from top to bottom as access is most likely LIFO */
    for (v = janet_vm_roots; v < vtop; v++) {
        if (janet_gc_idequals(root, *v)) {
            *v = janet_vm_roots[--janet_vm_root_count];
            return 1;
        }
    }
    return 0;
}

/* Remove a root value from the GC. This sets the effective reference count to 0. */
int janet_gcunrootall(Janet root) {
    Janet *vtop = janet_vm_roots + janet_vm_root_count;
    Janet *v = janet_vm_roots;
    int ret = 0;
    /* Search from top to bottom as access is most likely LIFO */
    for (v = janet_vm_roots; v < vtop; v++) {
        if (janet_gc_idequals(root, *v)) {
            *v = janet_vm_roots[--janet_vm_root_count];
            vtop--;
            ret = 1;
        }
    }
    return ret;
}

/* Free all allocated memory */
void janet_clear_memory(void) {
    JanetGCMemoryHeader *current = janet_vm_blocks;
    while (NULL != current) {
        janet_deinit_block(current);
        JanetGCMemoryHeader *next = current->next;
        free(current);
        current = next;
    }
    janet_vm_blocks = NULL;
}

/* Primitives for suspending GC. */
int janet_gclock(void) { return janet_vm_gc_suspend++; }
void janet_gcunlock(int handle) { janet_vm_gc_suspend = handle; }
