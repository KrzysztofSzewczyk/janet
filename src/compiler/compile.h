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

#ifndef DST_COMPILE_H
#define DST_COMPILE_H

#include <dst/dst.h>
#include <dst/dstcompile.h>
#include <dst/dstopcodes.h>
#include "regalloc.h"

/* Compiler typedefs */
typedef struct DstCompiler DstCompiler;
typedef struct FormOptions FormOptions;
typedef struct SlotTracker SlotTracker;
typedef struct DstScope DstScope;
typedef struct DstSlot DstSlot;
typedef struct DstFopts DstFopts;
typedef struct DstCFunOptimizer DstCFunOptimizer;
typedef struct DstSpecial DstSpecial;

#define DST_SLOT_CONSTANT 0x10000
#define DST_SLOT_NAMED 0x20000
#define DST_SLOT_MUTABLE 0x40000
#define DST_SLOT_REF 0x80000
#define DST_SLOT_RETURNED 0x100000
/* Needed for handling single element arrays as global vars. */

#define DST_SLOTTYPE_ANY 0xFFFF

/* A stack slot */
struct DstSlot {
    int32_t index;
    int32_t envindex; /* 0 is local, positive number is an upvalue */
    uint32_t flags;
    Dst constant; /* If the slot has a constant value */
};

#define DST_SCOPE_FUNCTION 1
#define DST_SCOPE_ENV 2
#define DST_SCOPE_TOP 4
#define DST_SCOPE_UNUSED 8

/* A symbol and slot pair */
typedef struct SymPair {
    const uint8_t *sym;
    int keep;
    DstSlot slot;
} SymPair;

/* A lexical scope during compilation */
struct DstScope {

    /* For debugging */
    const char *name;

    /* Scopes are doubly linked list */
    DstScope *parent;
    DstScope *child;

    /* Constants for this funcdef */
    Dst *consts;

    /* Map of symbols to slots. Use a simple linear scan for symbols. */
    SymPair *syms;

    /* Regsiter allocator */
    DstcRegisterAllocator ra;

    /* FuncDefs */
    DstFuncDef **defs;

    /* Referenced closure environents. The values at each index correspond
     * to which index to get the environment from in the parent. The environment
     * that corresponds to the direct parent's stack will always have value 0. */
    int32_t *envs;

    /* Where to add reference to self in constants */
    int32_t selfconst;

    int32_t bytecode_start;
    int flags;
};

/* Compilation state */
struct DstCompiler {
    int recursion_guard;
    
    /* Pointer to current scope */
    DstScope *scope;

    uint32_t *buffer;
    DstSourceMapping *mapbuffer;

    /* Keep track of where we are in the source */
    DstSourceMapping current_mapping;

    /* Hold the environment */
    DstTable *env;

    /* Name of source to attach to generated functions */
    const uint8_t *source;

    DstCompileResult result;
};

#define DST_FOPTS_TAIL 0x10000
#define DST_FOPTS_HINT 0x20000
#define DST_FOPTS_DROP 0x40000

/* Options for compiling a single form */
struct DstFopts {
    DstCompiler *compiler;
    uint32_t flags; /* bit set of accepted primitive types */
    DstSlot hint;
};

/* Get the default form options */
DstFopts dstc_fopts_default(DstCompiler *c);

/* A grouping of optimizations on a cfunction given certain conditions
 * on the arguments (such as all constants, or some known types). The appropriate
 * optimizations should be tried before compiling a normal function call. */
struct DstCFunOptimizer {
    DstCFunction cfun;
    int (*can_optimize)(DstFopts opts, DstSlot *args);
    DstSlot (*optimize)(DstFopts opts, DstSlot *args);
};

/* A grouping of a named special and the corresponding compiler fragment */
struct DstSpecial {
    const char *name;
    DstSlot (*compile)(DstFopts opts, int32_t argn, const Dst *argv);
};

/****************************************************/

/* Get a cfunction optimizer. Return NULL if none exists.  */
const DstCFunOptimizer *dstc_cfunopt(DstCFunction cfun);

/* Get a special. Return NULL if none exists */
const DstSpecial *dstc_special(const uint8_t *name);

/* Check error */
int dstc_iserr(DstFopts *opts);

/* Helper for iterating tables and structs */
const DstKV *dstc_next(Dst ds, const DstKV *kv);

void dstc_freeslot(DstCompiler *c, DstSlot s);
void dstc_nameslot(DstCompiler *c, const uint8_t *sym, DstSlot s);
DstSlot dstc_nearslot(DstCompiler *c, DstcRegisterTemp tag);
DstSlot dstc_farslot(DstCompiler *c);

/* Throw away some code after checking that it is well formed. */
void dstc_throwaway(DstFopts opts, Dst x);

/* Get a target slot for emitting an instruction. Will always return
 * a local slot. */
DstSlot dstc_gettarget(DstFopts opts);

/* Get a bunch of slots for function arguments */
DstSlot *dstc_toslots(DstCompiler *c, const Dst *vals, int32_t len);

/* Get a bunch of slots for function arguments */
DstSlot *dstc_toslotskv(DstCompiler *c, Dst ds);

/* Push slots load via dstc_toslots. */
void dstc_pushslots(DstCompiler *c, DstSlot *slots);

/* Free slots loaded via dstc_toslots */
void dstc_freeslots(DstCompiler *c, DstSlot *slots);

/* Generate the return instruction for a slot. */
DstSlot dstc_return(DstCompiler *c, DstSlot s);

/* Store an error */
void dstc_error(DstCompiler *c, const uint8_t *m);
void dstc_cerror(DstCompiler *c, const char *m);

/* Dispatch to correct form compiler */
DstSlot dstc_value(DstFopts opts, Dst x);

/* Push and pop from the scope stack */
void dstc_scope(DstScope *s, DstCompiler *c, int flags, const char *name);
void dstc_popscope(DstCompiler *c);
void dstc_popscope_keepslot(DstCompiler *c, DstSlot retslot);
DstFuncDef *dstc_pop_funcdef(DstCompiler *c);

/* Create a destory slots */
DstSlot dstc_cslot(Dst x);

/* Search for a symbol */
DstSlot dstc_resolve(DstCompiler *c, const uint8_t *sym);

#endif
