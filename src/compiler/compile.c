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

#include <dst/dst.h>
#include <dst/dstcorelib.h>
#include "compile.h"

#define DST_V_DEF_COPYMEM
#define DST_V_DEF_FLATTENMEM
#include <headerlibs/vector.h>
#undef DST_V_DEF_COPYMEM
#undef DST_V_DEF_FLATTENMEM

static void dstc_ast_push(DstCompiler *c, const Dst *tup) {
    DstSourceMapping mapping;
    if (c->result.status == DST_COMPILE_ERROR) {
        return;
    }
    mapping.line = dst_tuple_sm_line(tup);
    mapping.column = dst_tuple_sm_col(tup);
    if (!mapping.line) {
        /* Reuse previous mapping */
        mapping = c->current_mapping;
    }
    dst_v_push(c->ast_stack, mapping);
    c->current_mapping = mapping;
}

static void dstc_ast_pop(DstCompiler *c) {
    if (c->result.status == DST_COMPILE_ERROR) {
        return;
    }
    dst_v_pop(c->ast_stack);
    if (dst_v_count(c->ast_stack)) {
        c->current_mapping = dst_v_last(c->ast_stack);
    } else {
        c->current_mapping.line = 0;
        c->current_mapping.column = 0;
    }
}

DstFopts dstc_fopts_default(DstCompiler *c) {
    DstFopts ret;
    ret.compiler = c;
    ret.flags = 0;
    ret.hint = dstc_cslot(dst_wrap_nil());
    return ret;
}

/* Throw an error with a dst string. */
void dstc_error(DstCompiler *c, const uint8_t *m) {
    /* Don't override first error */
    if (c->result.status == DST_COMPILE_ERROR) {
        return;
    }
    c->result.status = DST_COMPILE_ERROR;
    c->result.error = m;
}

/* Throw an error with a message in a cstring */
void dstc_cerror(DstCompiler *c, const char *m) {
    dstc_error(c, dst_cstring(m));
}

/* Check error */
int dstc_iserr(DstFopts *opts) {
    return (opts->compiler->result.status == DST_COMPILE_ERROR);
}

/* Get the next key in an associative data structure. Used for iterating through an
 * associative data structure. */
const DstKV *dstc_next(Dst ds, const DstKV *kv) {
    switch(dst_type(ds)) {
        default:
            return NULL;
        case DST_TABLE:
            return (const DstKV *) dst_table_next(dst_unwrap_table(ds), kv);
        case DST_STRUCT:
            return dst_struct_next(dst_unwrap_struct(ds), kv);
    }
}

/* Allocate a slot index */
int32_t dstc_lsloti(DstCompiler *c) {
    DstScope *scope = &dst_v_last(c->scopes);
    /* Get the nth bit in the array */
    int32_t i, biti, len;
    biti = -1;
    len = dst_v_count(scope->slots);
    for (i = 0; i < len; i++) {
        uint32_t block = scope->slots[i];
        if (block == 0xFFFFFFFF) continue;
        biti = i << 5; /* + clz(block) */
        while (block & 1) {
            biti++;
            block >>= 1;
        }
        break;
    }
    if (biti == -1) {
        /* Extend bit vector for slots */
        dst_v_push(scope->slots, len == 7 ? 0xFFFF0000 : 0);
        biti = len << 5;
    }
    /* set the bit at index biti */
    scope->slots[biti >> 5] |= 1 << (biti & 0x1F);
    if (biti > scope->smax)
        scope->smax = biti;
    return biti;
}

/* Allocate a given slot index */
static void slotalloci(DstCompiler *c, int32_t index) {
    int32_t count;
    int32_t block = index >> 5;
    DstScope *scope = &dst_v_last(c->scopes);
    if (index < 0) return;
    while ((count = dst_v_count(scope->slots)) <= block) {
        /* Extend bit vector for slots */
        dst_v_push(scope->slots, count == 7 ? 0xFFFF0000 : 0);
    }
    scope->slots[block] |= 1 << (index & 0x1F);
}

/* Free a slot index */
void dstc_sfreei(DstCompiler *c, int32_t index) {
    DstScope *scope = &dst_v_last(c->scopes);
    /* Don't free the pre allocated slots */
    if (index >= 0 && (index < 0xF0 || index > 0xFF) &&
            index < (dst_v_count(scope->slots) << 5))
        scope->slots[index >> 5] &= ~(1 << (index & 0x1F));
}

/* Allocate a local near (n) slot and return its index. Slot
 * has maximum index max. Common value for max would be 0xFF,
 * the highest slot index representable with one byte. */
int32_t dstc_lslotn(DstCompiler *c, int32_t max, int32_t nth) {
    int32_t ret = dstc_lsloti(c);
    if (ret > max) {
        dstc_sfreei(c, ret);
        ret = 0xF0 + nth;
    }
    return ret;
}

/* Free a slot */
void dstc_freeslot(DstCompiler *c, DstSlot s) {
    if (s.flags & (DST_SLOT_CONSTANT | DST_SLOT_REF | DST_SLOT_NAMED)) return;
    if (s.envindex >= 0) return;
    dstc_sfreei(c, s.index);
}

/* Add a slot to a scope with a symbol associated with it (def or var). */
void dstc_nameslot(DstCompiler *c, const uint8_t *sym, DstSlot s) {
    DstScope *scope = &dst_v_last(c->scopes);
    SymPair sp;
    sp.sym = sym;
    sp.slot = s;
    sp.keep = 0;
    sp.slot.flags |= DST_SLOT_NAMED;
    dst_v_push(scope->syms, sp);
}

/* Enter a new scope */
void dstc_scope(DstCompiler *c, int flags) {
    DstScope scope;
    scope.consts = NULL;
    scope.syms = NULL;
    scope.envs = NULL;
    scope.defs = NULL;
    scope.slots = NULL;
    scope.smax = -1;
    scope.selfconst = -1;
    scope.bytecode_start = dst_v_count(c->buffer);
    scope.flags = flags;

    /* Inherit slots */
    if ((!(flags & DST_SCOPE_FUNCTION)) && dst_v_count(c->scopes)) {
        DstScope *oldscope = &dst_v_last(c->scopes);
        scope.smax = oldscope->smax;
        scope.slots = dst_v_copy(oldscope->slots);
    }

    dst_v_push(c->scopes, scope);
}

/* Leave a scope. */
void dstc_popscope(DstCompiler *c) {
    DstScope scope;
    int32_t oldcount = dst_v_count(c->scopes);
    dst_assert(oldcount, "could not pop scope");
    scope = dst_v_last(c->scopes);
    dst_v_pop(c->scopes);
    /* Move free slots to parent scope if not a new function.
     * We need to know the total number of slots used when compiling the function. */
    if (!(scope.flags & (DST_SCOPE_FUNCTION | DST_SCOPE_UNUSED)) && oldcount > 1) {
        int32_t i;
        DstScope *newscope = &dst_v_last(c->scopes);
        if (newscope->smax < scope.smax)
            newscope->smax = scope.smax;

        /* Keep upvalue slots */
        for (i = 0; i < dst_v_count(scope.syms); i++) {
            SymPair pair = scope.syms[i];
            if (pair.keep) {
                /* The variable should not be lexically accessible */
                pair.sym = NULL;
                dst_v_push(newscope->syms, pair);
                slotalloci(c, pair.slot.index);
            }
        }

    }
    /* Free the scope */
    dst_v_free(scope.consts);
    dst_v_free(scope.syms);
    dst_v_free(scope.envs);
    dst_v_free(scope.defs);
    dst_v_free(scope.slots);
}

/* Leave a scope but keep a slot allocated. */
void dstc_popscope_keepslot(DstCompiler *c, DstSlot retslot) {
    dstc_popscope(c);
    if (retslot.envindex < 0 && retslot.index >= 0) {
        slotalloci(c, retslot.index);
    }
}

/* Create a slot with a constant */
DstSlot dstc_cslot(Dst x) {
    DstSlot ret;
    ret.flags = (1 << dst_type(x)) | DST_SLOT_CONSTANT;
    ret.index = -1;
    ret.constant = x;
    ret.envindex = -1;
    return ret;
}

/* Allow searching for symbols. Return information about the symbol */
DstSlot dstc_resolve(
        DstCompiler *c,
        const uint8_t *sym) {

    DstSlot ret = dstc_cslot(dst_wrap_nil());
    DstScope *top = &dst_v_last(c->scopes);
    DstScope *scope = top;
    SymPair *pair;
    int foundlocal = 1;
    int unused = 0;

    /* Search scopes for symbol, starting from top */
    while (scope >= c->scopes) {
        int32_t i, len;
        if (scope->flags & DST_SCOPE_UNUSED)
            unused = 1;
        len = dst_v_count(scope->syms);
        /* Search in reverse order */
        for (i = len - 1; i >= 0; i--) {
            pair = scope->syms + i;
            if (pair->sym == sym) {
                ret = pair->slot;
                goto found;
            }
        }
        if (scope->flags & DST_SCOPE_FUNCTION)
            foundlocal = 0;
        scope--;
    }

    /* Symbol not found - check for global */
    {
        Dst check;
        DstBindingType btype = dst_env_resolve(c->env, sym, &check);
        switch (btype) {
            default:
            case DST_BINDING_NONE:
                dstc_error(c, dst_formatc("unknown symbol %q", sym));
                return dstc_cslot(dst_wrap_nil());
            case DST_BINDING_DEF:
            case DST_BINDING_MACRO: /* Macro should function like defs when not in calling pos */
                return dstc_cslot(check);
            case DST_BINDING_VAR:
            {
                DstSlot ret = dstc_cslot(check);
                /* TODO save type info */
                ret.flags |= DST_SLOT_REF | DST_SLOT_NAMED | DST_SLOT_MUTABLE | DST_SLOTTYPE_ANY;
                ret.flags &= ~DST_SLOT_CONSTANT;
                return ret;
            }
        }
    }

    /* Symbol was found */
    found:

    /* Constants can be returned immediately (they are stateless) */
    if (ret.flags & (DST_SLOT_CONSTANT | DST_SLOT_REF))
        return ret;

    /* Unused references and locals shouldn't add captured envs. */
    if (unused || foundlocal) {
        ret.envindex = -1;
        return ret;
    }

    /* non-local scope needs to expose its environment */
    pair->keep = 1;
    while (scope >= c->scopes && !(scope->flags & DST_SCOPE_FUNCTION)) scope--;
    dst_assert(scope >= c->scopes, "invalid scopes");
    scope->flags |= DST_SCOPE_ENV;
    scope++;

    /* Propogate env up to current scope */
    int32_t envindex = -1;
    while (scope <= top) {
        if (scope->flags & DST_SCOPE_FUNCTION) {
            int32_t j, len;
            int scopefound = 0;
            /* Check if scope already has env. If so, break */
            len = dst_v_count(scope->envs);
            for (j = 0; j < len; j++) {
                if (scope->envs[j] == envindex) {
                    scopefound = 1;
                    envindex = j;
                    break;
                }
            }
            /* Add the environment if it is not already referenced */
            if (!scopefound) {
                len = dst_v_count(scope->envs);
                dst_v_push(scope->envs, envindex);
                envindex = len;
            }
        }
        scope++;
    }

    ret.envindex = envindex;
    return ret;
}

/* Emit a raw instruction with source mapping. */
void dstc_emit(DstCompiler *c, uint32_t instr) {
    dst_v_push(c->buffer, instr);
    dst_v_push(c->mapbuffer, c->current_mapping);
}

/* Add a constant to the current scope. Return the index of the constant. */
static int32_t dstc_const(DstCompiler *c, Dst x) {
    DstScope *scope = &dst_v_last(c->scopes);
    int32_t i, len;
    /* Get the topmost function scope */
    while (scope > c->scopes) {
        if (scope->flags & DST_SCOPE_FUNCTION)
            break;
        scope--;
    }
    /* Check if already added */
    len = dst_v_count(scope->consts);
    for (i = 0; i < len; i++) {
        if (dst_equals(x, scope->consts[i]))
            return i;
    }
    /* Ensure not too many constsants. */
    if (len >= 0xFFFF) {
        dstc_cerror(c, "too many constants");
        return 0;
    }
    dst_v_push(scope->consts, x);
    return len;
}

/* Load a constant into a local slot */
static void dstc_loadconst(DstCompiler *c, Dst k, int32_t dest) {
    switch (dst_type(k)) {
        case DST_NIL:
            dstc_emit(c, (dest << 8) | DOP_LOAD_NIL);
            break;
        case DST_TRUE:
            dstc_emit(c, (dest << 8) | DOP_LOAD_TRUE);
            break;
        case DST_FALSE:
            dstc_emit(c, (dest << 8) | DOP_LOAD_FALSE);
            break;
        case DST_INTEGER:
            {
                int32_t i = dst_unwrap_integer(k);
                if (i <= INT16_MAX && i >= INT16_MIN) {
                    dstc_emit(c,
                            (i << 16) |
                            (dest << 8) |
                            DOP_LOAD_INTEGER);
                    break;
                }
                goto do_constant;
            }
        default:
        do_constant:
            {
                int32_t cindex = dstc_const(c, k);
                dstc_emit(c,
                        (cindex << 16) |
                        (dest << 8) |
                        DOP_LOAD_CONSTANT);
                break;
            }
    }
}

/* Realize any slot to a local slot. Call this to get a slot index
 * that can be used in an instruction. */
int32_t dstc_preread(
        DstCompiler *c,
        int32_t max,
        int nth,
        DstSlot s) {

    int32_t ret;

    if (s.flags & DST_SLOT_REF)
        max = 0xFF;

    if (s.flags & (DST_SLOT_CONSTANT | DST_SLOT_REF)) {
        ret = dstc_lslotn(c, 0xFF, nth);
        dstc_loadconst(c, s.constant, ret);
        /* If we also are a reference, deref the one element array */
        if (s.flags & DST_SLOT_REF) {
            dstc_emit(c,
                    (ret << 16) |
                    (ret << 8) |
                    DOP_GET_INDEX);
        }
    } else if (s.envindex >= 0 || s.index > max) {
        ret = dstc_lslotn(c, max, nth);
        dstc_emit(c,
                ((uint32_t)(s.index) << 24) |
                ((uint32_t)(s.envindex) << 16) |
                ((uint32_t)(ret) << 8) |
                DOP_LOAD_UPVALUE);
    } else if (s.index > max) {
        ret = dstc_lslotn(c, max, nth);
        dstc_emit(c,
                ((uint32_t)(s.index) << 16) |
                ((uint32_t)(ret) << 8) |
                    DOP_MOVE_NEAR);
    } else {
        /* We have a normal slot that fits in the required bit width */
        ret = s.index;
    }
    return ret;
}

/* Call this to release a read handle after emitting the instruction. */
void dstc_postread(DstCompiler *c, DstSlot s, int32_t index) {
    if (index != s.index || s.envindex >= 0 || s.flags & DST_SLOT_CONSTANT) {
        /* We need to free the temporary slot */
        dstc_sfreei(c, index);
    }
}

/* Check if two slots are equal */
int dstc_sequal(DstSlot lhs, DstSlot rhs) {
    if (lhs.flags == rhs.flags &&
            lhs.index == rhs.index &&
            lhs.envindex == rhs.envindex) {
        if (lhs.flags & (DST_SLOT_REF | DST_SLOT_CONSTANT)) {
            return dst_equals(lhs.constant, rhs.constant);
        } else {
            return 1;
        }
    }
    return 0;
}

/* Move values from one slot to another. The destination must
 * be writeable (not a literal). */
void dstc_copy(
        DstCompiler *c,
        DstSlot dest,
        DstSlot src) {
    int writeback = 0;
    int32_t destlocal = -1;
    int32_t srclocal = -1;
    int32_t reflocal = -1;

    /* Can't write to constants */
    if (dest.flags & DST_SLOT_CONSTANT) {
        dstc_cerror(c, "cannot write to constant");
        return;
    }

    /* Short circuit if dest and source are equal */
    if (dstc_sequal(dest, src)) return;

    /* Types of slots - src */
    /* constants */
    /* upvalues */
    /* refs */
    /* near index */
    /* far index */

    /* Types of slots - dest */
    /* upvalues */
    /* refs */
    /* near index */
    /* far index */

    /* If dest is a near index, do some optimization */
    if (dest.envindex < 0 && dest.index >= 0 && dest.index <= 0xFF) {
        if (src.flags & DST_SLOT_CONSTANT) {
            dstc_loadconst(c, src.constant, dest.index);
        } else if (src.flags & DST_SLOT_REF) {
            dstc_loadconst(c, src.constant, dest.index);
            dstc_emit(c,
                    (dest.index << 16) |
                    (dest.index << 8) |
                    DOP_GET_INDEX);
        } else if (src.envindex >= 0) {
            dstc_emit(c,
                    (src.index << 24) |
                    (src.envindex << 16) |
                    (dest.index << 8) |
                    DOP_LOAD_UPVALUE);
        } else {
            dstc_emit(c,
                    (src.index << 16) |
                    (dest.index << 8) |
                    DOP_MOVE_NEAR);
        }
        return;
    }

    /* Process: src -> srclocal -> destlocal -> dest */

    /* src -> srclocal */
    srclocal = dstc_preread(c, 0xFF, 1, src);

    /* Pull down dest (find destlocal) */
    if (dest.flags & DST_SLOT_REF) {
        writeback = 1;
        destlocal = srclocal;
        reflocal = dstc_lslotn(c, 0xFF, 2);
        dstc_emit(c,
                (dstc_const(c, dest.constant) << 16) |
                (reflocal << 8) |
                DOP_LOAD_CONSTANT);
    } else if (dest.envindex >= 0) {
        writeback = 2;
        destlocal = srclocal;
    } else if (dest.index > 0xFF) {
        writeback = 3;
        destlocal = srclocal;
    } else {
        destlocal = dest.index;
    }

    /* srclocal -> destlocal */
    if (srclocal != destlocal) {
        dstc_emit(c,
                ((uint32_t)(srclocal) << 16) |
                ((uint32_t)(destlocal) << 8) |
                DOP_MOVE_NEAR);
    }

    /* destlocal -> dest */
    if (writeback == 1) {
        dstc_emit(c,
                (destlocal << 16) |
                (reflocal << 8) |
                DOP_PUT_INDEX);
    } else if (writeback == 2) {
        dstc_emit(c,
                ((uint32_t)(dest.index) << 24) |
                ((uint32_t)(dest.envindex) << 16) |
                ((uint32_t)(destlocal) << 8) |
                DOP_SET_UPVALUE);
    } else if (writeback == 3) {
        dstc_emit(c,
                ((uint32_t)(dest.index) << 16) |
                ((uint32_t)(destlocal) << 8) |
                DOP_MOVE_FAR);
    }

    /* Cleanup */
    if (reflocal >= 0) {
        dstc_sfreei(c, reflocal);
    }
    dstc_postread(c, src, srclocal);
}

/* Generate the return instruction for a slot. */
DstSlot dstc_return(DstCompiler *c, DstSlot s) {
    if (!(s.flags & DST_SLOT_RETURNED)) {
        if (s.flags & DST_SLOT_CONSTANT && dst_checktype(s.constant, DST_NIL)) {
            dstc_emit(c, DOP_RETURN_NIL);
        } else {
            int32_t ls = dstc_preread(c, 0xFFFF, 1, s);
            dstc_emit(c, DOP_RETURN | (ls << 8));
            dstc_postread(c, s, ls);
        }
        s.flags |= DST_SLOT_RETURNED;
    }
    return s;
}

/* Get a target slot for emitting an instruction. Will always return
 * a local slot. */
DstSlot dstc_gettarget(DstFopts opts) {
    DstSlot slot;
    if ((opts.flags & DST_FOPTS_HINT) &&
        (opts.hint.envindex < 0) &&
        (opts.hint.index >= 0 && opts.hint.index <= 0xFF)) {
        slot = opts.hint;
    } else {
        slot.envindex = -1;
        slot.constant = dst_wrap_nil();
        slot.flags = 0;
        slot.index = dstc_lslotn(opts.compiler, 0xFF, 4);
    }
    return slot;
}

/* Get a bunch of slots for function arguments */
DstSlot *dstc_toslots(DstCompiler *c, const Dst *vals, int32_t len) {
    int32_t i;
    DstSlot *ret = NULL;
    DstFopts subopts = dstc_fopts_default(c);
    for (i = 0; i < len; i++) {
        dst_v_push(ret, dstc_value(subopts, vals[i]));
    }
    return ret;
}

/* Get a bunch of slots for function arguments */
DstSlot *dstc_toslotskv(DstCompiler *c, Dst ds) {
    DstSlot *ret = NULL;
    const DstKV *kv = NULL;
    DstFopts subopts = dstc_fopts_default(c);
    while ((kv = dstc_next(ds, kv))) {
        dst_v_push(ret, dstc_value(subopts, kv->key));
        dst_v_push(ret, dstc_value(subopts, kv->value));
    }
    return ret;
}

/* Push slots load via dstc_toslots. */
void dstc_pushslots(DstCompiler *c, DstSlot *slots) {
    int32_t i;
    for (i = 0; i < dst_v_count(slots) - 2; i += 3) {
        int32_t ls1 = dstc_preread(c, 0xFF, 1, slots[i]);
        int32_t ls2 = dstc_preread(c, 0xFF, 2, slots[i + 1]);
        int32_t ls3 = dstc_preread(c, 0xFF, 3, slots[i + 2]);
        dstc_emit(c,
                (ls3 << 24) |
                (ls2 << 16) |
                (ls1 << 8) |
                DOP_PUSH_3);
        dstc_postread(c, slots[i], ls1);
        dstc_postread(c, slots[i + 1], ls2);
        dstc_postread(c, slots[i + 2], ls3);
    }
    if (i == dst_v_count(slots) - 2) {
        int32_t ls1 = dstc_preread(c, 0xFF, 1, slots[i]);
        int32_t ls2 = dstc_preread(c, 0xFFFF, 2, slots[i + 1]);
        dstc_emit(c,
                (ls2 << 16) |
                (ls1 << 8) |
                DOP_PUSH_2);
        dstc_postread(c, slots[i], ls1);
        dstc_postread(c, slots[i + 1], ls2);
    } else if (i == dst_v_count(slots) - 1) {
        int32_t ls1 = dstc_preread(c, 0xFFFFFF, 1, slots[i]);
        dstc_emit(c,
                (ls1 << 8) |
                DOP_PUSH);
        dstc_postread(c, slots[i], ls1);
    }
}

/* Free slots loaded via dstc_toslots */
void dstc_freeslots(DstCompiler *c, DstSlot *slots) {
    int32_t i;
    for (i = 0; i < dst_v_count(slots); i++) {
        dstc_freeslot(c, slots[i]);
    }
    dst_v_free(slots);
}

/* Compile some code that will be thrown away. Used to ensure
 * that dead code is well formed without including it in the final
 * bytecode. */
void dstc_throwaway(DstFopts opts, Dst x) {
    DstCompiler *c = opts.compiler;
    int32_t bufstart = dst_v_count(c->buffer);
    int32_t mapbufstart = dst_v_count(c->mapbuffer);
    dstc_scope(c, DST_SCOPE_UNUSED);
    dstc_value(opts, x);
    dstc_popscope(c);
    if (NULL != c->buffer) {
        dst_v__cnt(c->buffer) = bufstart;
        if (NULL != c->mapbuffer)
            dst_v__cnt(c->mapbuffer) = mapbufstart;
    }
}

/* Compile a call or tailcall instruction */
static DstSlot dstc_call(DstFopts opts, DstSlot *slots, DstSlot fun) {
    DstSlot retslot;
    int32_t localindex;
    DstCompiler *c = opts.compiler;
    int specialized = 0;
    if (fun.flags & DST_SLOT_CONSTANT) {
        if (dst_checktype(fun.constant, DST_CFUNCTION)) {
            const DstCFunOptimizer *o = dstc_cfunopt(dst_unwrap_cfunction(fun.constant));
            if (o && o->can_optimize(opts, slots)) {
                specialized = 1;
                retslot = o->optimize(opts, slots);
            }
        }
        /* TODO dst function inlining (no c functions)*/
    }
    if (!specialized) {
        dstc_pushslots(c, slots);
        localindex = dstc_preread(c, 0xFF, 1, fun);
        if (opts.flags & DST_FOPTS_TAIL) {
            dstc_emit(c, (localindex << 8) | DOP_TAILCALL);
            retslot = dstc_cslot(dst_wrap_nil());
            retslot.flags = DST_SLOT_RETURNED;
        } else {
            retslot = dstc_gettarget(opts);
            dstc_emit(c, (localindex << 16) | (retslot.index << 8) | DOP_CALL);
        }
        dstc_postread(c, fun, localindex);
    }
    dstc_freeslots(c, slots);
    return retslot;
}

static DstSlot dstc_array(DstFopts opts, Dst x) {
    DstCompiler *c = opts.compiler;
    DstArray *a = dst_unwrap_array(x);
    return dstc_call(opts,
            dstc_toslots(c, a->data, a->count),
            dstc_cslot(dst_wrap_cfunction(dst_core_array)));
}

static DstSlot dstc_tablector(DstFopts opts, Dst x, DstCFunction cfun) {
    DstCompiler *c = opts.compiler;
    return dstc_call(opts, dstc_toslotskv(c, x), dstc_cslot(dst_wrap_cfunction(cfun)));
}

static DstSlot dstc_bufferctor(DstFopts opts, Dst x) {
    DstCompiler *c = opts.compiler;
    DstBuffer *b = dst_unwrap_buffer(x);
    Dst onearg = dst_stringv(b->data, b->count);
    return dstc_call(opts,
            dstc_toslots(c, &onearg, 1),
            dstc_cslot(dst_wrap_cfunction(dst_core_buffer)));
}

/* Compile a symbol */
DstSlot dstc_symbol(DstFopts opts, const uint8_t *sym) {
    if (dst_string_length(sym) && sym[0] != ':') {
        /* Non keyword */
        return dstc_resolve(opts.compiler, sym);
    } else {
        /* Keyword */
        return dstc_cslot(dst_wrap_symbol(sym));
    }
}

/* Compile a single value */
DstSlot dstc_value(DstFopts opts, Dst x) {
    DstSlot ret;
    DstCompiler *c = opts.compiler;
    int macrorecur = 0;
    opts.compiler->recursion_guard--;
recur:
    if (dstc_iserr(&opts)) {
        return dstc_cslot(dst_wrap_nil());
    }
    if (opts.compiler->recursion_guard <= 0) {
        dstc_cerror(opts.compiler, "recursed too deeply");
        return dstc_cslot(dst_wrap_nil());
    }
    switch (dst_type(x)) {
        default:
            ret = dstc_cslot(x);
            break;
        case DST_SYMBOL:
            {
                const uint8_t *sym = dst_unwrap_symbol(x);
                ret = dstc_symbol(opts, sym);
                break;
            }
        case DST_TUPLE:
            {
                int compiled = 0;
                Dst headval;
                DstSlot head;
                DstFopts subopts = dstc_fopts_default(c);
                const Dst *tup = dst_unwrap_tuple(x);
                if (!macrorecur)
                    dstc_ast_push(c, tup);
                /* Empty tuple is tuple literal */
                if (dst_tuple_length(tup) == 0) {
                    compiled = 1;
                    ret = dstc_cslot(x);
                } else {
                    /* Symbols could be specials */
                    headval = tup[0];
                    if (dst_checktype(headval, DST_SYMBOL)) {
                        const uint8_t *headsym = dst_unwrap_symbol(headval);
                        const DstSpecial *s = dstc_special(headsym);
                        if (NULL != s) {
                            ret = s->compile(opts, dst_tuple_length(tup) - 1, tup + 1);
                            compiled = 1;
                        } else {
                            /* Check macro */
                            Dst macVal;
                            DstBindingType btype = dst_env_resolve(c->env, headsym, &macVal);
                            if (btype == DST_BINDING_MACRO &&
                                    dst_checktype(macVal, DST_FUNCTION)) {
                                if (macrorecur++ > DST_RECURSION_GUARD) {
                                    dstc_cerror(c, "macro expansion recursed too deeply");
                                    return dstc_cslot(dst_wrap_nil());
                                } else {
                                    DstFunction *f = dst_unwrap_function(macVal);
                                    int lock = dst_gclock();
                                    DstSignal status = dst_call(f, dst_tuple_length(tup) - 1, tup + 1, &x);
                                    dst_gcunlock(lock);
                                    if (status != DST_SIGNAL_OK) {
                                        const uint8_t *es = dst_formatc("error in macro expansion: %V", x);
                                        dstc_error(c, es);
                                    }
                                    /* Tail recur on the value */
                                    goto recur;
                                }
                            }
                        }
                    }
                    if (!compiled) {
                        /* Compile the head of the tuple */
                        subopts.flags = DST_FUNCTION | DST_CFUNCTION;
                        head = dstc_value(subopts, tup[0]);
                        /* Add compile function call */
                        ret = dstc_call(opts, dstc_toslots(c, tup + 1, dst_tuple_length(tup) - 1), head);
                    }
                }
                /* Pop source mapping for tuple */
                dstc_ast_pop(c);
            }
            break;
        case DST_ARRAY:
            ret = dstc_array(opts, x);
            break;
        case DST_STRUCT:
            ret = dstc_tablector(opts, x, dst_core_struct);
            break;
        case DST_TABLE:
            ret = dstc_tablector(opts, x, dst_core_table);
            break;
        case DST_BUFFER:
            ret = dstc_bufferctor(opts, x);
            break;
    }
    if (dstc_iserr(&opts)) {
        return dstc_cslot(dst_wrap_nil());
    }
    if (opts.flags & DST_FOPTS_TAIL) {
        ret = dstc_return(opts.compiler, ret);
    }
    if (opts.flags & DST_FOPTS_HINT && !dstc_sequal(opts.hint, ret)) {
        dstc_copy(opts.compiler, opts.hint, ret);
        ret = opts.hint;
    }
    opts.compiler->recursion_guard++;
    return ret;
}

/* Compile a funcdef */
DstFuncDef *dstc_pop_funcdef(DstCompiler *c) {
    DstScope scope = dst_v_last(c->scopes);
    DstFuncDef *def = dst_funcdef_alloc();
    def->slotcount = scope.smax + 1;

    dst_assert(scope.flags & DST_SCOPE_FUNCTION, "expected function scope");

    /* Copy envs */
    def->environments_length = dst_v_count(scope.envs);
    def->environments = dst_v_flatten(scope.envs);

    def->constants_length = dst_v_count(scope.consts);
    def->constants = dst_v_flatten(scope.consts);

    def->defs_length = dst_v_count(scope.defs);
    def->defs = dst_v_flatten(scope.defs);

    /* Copy bytecode (only last chunk) */
    def->bytecode_length = dst_v_count(c->buffer) - scope.bytecode_start;
    if (def->bytecode_length) {
        size_t s = sizeof(int32_t) * def->bytecode_length;
        def->bytecode = malloc(s);
        if (NULL == def->bytecode) {
            DST_OUT_OF_MEMORY;
        }
        memcpy(def->bytecode, c->buffer + scope.bytecode_start, s);
        dst_v__cnt(c->buffer) = scope.bytecode_start;
        if (NULL != c->mapbuffer) {
            int32_t i;
            size_t s = sizeof(DstSourceMapping) * dst_v_count(c->mapbuffer);
            def->sourcemap = malloc(s);
            if (NULL == def->sourcemap) {
                DST_OUT_OF_MEMORY;
            }
            for (i = 0; i < dst_v_count(c->mapbuffer); i++) {
                def->sourcemap[i] = c->mapbuffer[i];
            }
            dst_v__cnt(c->mapbuffer) = scope.bytecode_start;
        }
    }

    /* Get source from parser */
    def->source = c->source;

    def->arity = 0;
    def->flags = 0;
    if (scope.flags & DST_SCOPE_ENV) {
        def->flags |= DST_FUNCDEF_FLAG_NEEDSENV;
    }

    /* Pop the scope */
    dstc_popscope(c);

    return def;
}

/* Initialize a compiler */
static void dstc_init(DstCompiler *c, DstTable *env, const uint8_t *where) {
    c->scopes = NULL;
    c->buffer = NULL;
    c->mapbuffer = NULL;
    c->recursion_guard = DST_RECURSION_GUARD;
    c->env = env;
    c->source = where;
    c->ast_stack = NULL;
    c->current_mapping.line = 0;
    c->current_mapping.column = 0;
    /* Init result */
    c->result.error = NULL;
    c->result.status = DST_COMPILE_OK;
    c->result.funcdef = NULL;
    c->result.error_mapping.line = 0;
    c->result.error_mapping.column = 0;
}

/* Deinitialize a compiler struct */
static void dstc_deinit(DstCompiler *c) {
    while (dst_v_count(c->scopes)) dstc_popscope(c);
    dst_v_free(c->scopes);
    dst_v_free(c->buffer);
    dst_v_free(c->mapbuffer);
    dst_v_free(c->ast_stack);
    c->env = NULL;
}

/* Compile a form. */
DstCompileResult dst_compile(Dst source, DstTable *env, const uint8_t *where) {
    DstCompiler c;
    DstFopts fopts;

    dstc_init(&c, env, where);

    /* Push a function scope */
    dstc_scope(&c, DST_SCOPE_FUNCTION | DST_SCOPE_TOP);

    /* Set initial form options */
    fopts.compiler = &c;
    fopts.flags = DST_FOPTS_TAIL | DST_SLOTTYPE_ANY;
    fopts.hint = dstc_cslot(dst_wrap_nil());

    /* Compile the value */
    dstc_value(fopts, source);

    if (c.result.status == DST_COMPILE_OK) {
        DstFuncDef *def = dstc_pop_funcdef(&c);
        def->name = dst_cstring("_thunk");
        c.result.funcdef = def;
    } else {
        c.result.error_mapping = c.current_mapping;
    }

    dstc_deinit(&c);

    return c.result;
}

/* C Function for compiling */
int dst_compile_cfun(DstArgs args) {
    DstCompileResult res;
    DstTable *t;
    DstTable *env;
    DST_MINARITY(args, 2);
    DST_MAXARITY(args, 3);
    DST_ARG_TABLE(env, args, 1);
    const uint8_t *source = NULL;
    if (args.n == 3) {
        DST_ARG_STRING(source, args, 2);
    }
    res = dst_compile(args.v[0], env, source);
    if (res.status == DST_COMPILE_OK) {
        DST_RETURN_FUNCTION(args, dst_thunk(res.funcdef));
    } else {
        t = dst_table(2);
        dst_table_put(t, dst_csymbolv(":error"), dst_wrap_string(res.error));
        dst_table_put(t, dst_csymbolv(":error-line"), dst_wrap_integer(res.error_mapping.line));
        dst_table_put(t, dst_csymbolv(":error-column"), dst_wrap_integer(res.error_mapping.column));
        DST_RETURN_TABLE(args, t);
    }
}

int dst_lib_compile(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_def(env, "compile", dst_wrap_cfunction(dst_compile_cfun));
    return 0;
}
