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

#ifndef JANET_H_defined
#define JANET_H_defined

#ifdef __cplusplus
extern "C" {
#endif

/***** START SECTION CONFIG *****/

#ifndef JANET_VERSION
#define JANET_VERSION "0.0.0 alpha"
#endif

/*
 * Detect OS and endianess.
 * From webkit source. There is likely some extreneous
 * detection for unsupported platforms
 */

/* Check Unix */
#if defined(_AIX) \
    || defined(__APPLE__) /* Darwin */ \
    || defined(__FreeBSD__) || defined(__DragonFly__) \
    || defined(__FreeBSD_kernel__) \
    || defined(__GNU__) /* GNU/Hurd */ \
    || defined(__linux__) \
    || defined(__NetBSD__) \
    || defined(__OpenBSD__) \
    || defined(__QNXNTO__) \
    || defined(sun) || defined(__sun) /* Solaris */ \
    || defined(unix) || defined(__unix) || defined(__unix__)
#define JANET_UNIX 1
/* Enable certain posix features */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#elif defined(__EMSCRIPTEN__)
#define JANET_WEB 1
#elif defined(WIN32) || defined(_WIN32)
#define JANET_WINDOWS 1
#endif

/* Check 64-bit vs 32-bit */
#if ((defined(__x86_64__) || defined(_M_X64)) \
     && (defined(JANET_UNIX) || defined(JANET_WINDOWS))) \
	|| (defined(_WIN64)) /* Windows 64 bit */ \
    || (defined(__ia64__) && defined(__LP64__)) /* Itanium in LP64 mode */ \
    || defined(__alpha__) /* DEC Alpha */ \
    || (defined(__sparc__) && defined(__arch64__) || defined (__sparcv9)) /* BE */ \
    || defined(__s390x__) /* S390 64-bit (BE) */ \
    || (defined(__ppc64__) || defined(__PPC64__)) \
    || defined(__aarch64__) /* ARM 64-bit */
#define JANET_64 1
#else
#define JANET_32 1
#endif

/* Check big endian */
#if defined(__MIPSEB__) /* MIPS 32-bit */ \
    || defined(__ppc__) || defined(__PPC__) /* CPU(PPC) - PowerPC 32-bit */ \
    || defined(__powerpc__) || defined(__powerpc) || defined(__POWERPC__) \
    || defined(_M_PPC) || defined(__PPC) \
    || defined(__ppc64__) || defined(__PPC64__) /* PowerPC 64-bit */ \
    || defined(__sparc)   /* Sparc 32bit */  \
    || defined(__sparc__) /* Sparc 64-bit */ \
    || defined(__s390x__) /* S390 64-bit */ \
    || defined(__s390__)  /* S390 32-bit */ \
    || defined(__ARMEB__) /* ARM big endian */ \
    || ((defined(__CC_ARM) || defined(__ARMCC__)) /* ARM RealView compiler */ \
        && defined(__BIG_ENDIAN))
#define JANET_BIG_ENDIAN 1
#else
#define JANET_LITTLE_ENDIAN 1
#endif

/* Check emscripten */
#ifdef __EMSCRIPTEN__
#define JANET_NO_DYNAMIC_MODULES
#define JANET_NO_NANBOX
#endif

/* Define how global janet state is declared */
#ifdef JANET_SINGLE_THREADED
#define JANET_THREAD_LOCAL
#elif defined(__GNUC__)
#define JANET_THREAD_LOCAL __thread
#elif defined(_MSC_BUILD)
#define JANET_THREAD_LOCAL __declspec(thread)
#else
#define JANET_THREAD_LOCAL
#endif

/* Enable or disbale dynamic module loading. Enabled by default. */
#ifndef JANET_NO_DYNAMIC_MODULES
#define JANET_DYNAMIC_MODULES
#endif

/* Enable or disable the assembler. Enabled by default. */
#ifndef JANET_NO_ASSEMBLER
#define JANET_ASSEMBLER
#endif

/* How to export symbols */
#ifndef JANET_API
#ifdef JANET_WINDOWS
#define JANET_API __declspec(dllexport)
#else
#define JANET_API __attribute__((visibility ("default")))
#endif
#endif

/* Handle runtime errors */
#ifndef janet_exit
#include <stdio.h>
#define janet_exit(m) do { \
    printf("C runtime error at line %d in file %s: %s\n",\
        __LINE__,\
        __FILE__,\
        (m));\
    exit(1);\
} while (0)
#endif

#define janet_assert(c, m) do { \
    if (!(c)) janet_exit((m)); \
} while (0)

/* What to do when out of memory */
#ifndef JANET_OUT_OF_MEMORY
#include <stdio.h>
#define JANET_OUT_OF_MEMORY do { printf("janet out of memory\n"); exit(1); } while (0)
#endif

/* Helper for debugging */
#define janet_trace(x) janet_puts(janet_formatc("JANET TRACE %s, %d: %v\n", __FILE__, __LINE__, x))

/* Prevent some recursive functions from recursing too deeply
 * ands crashing (the parser). Instead, error out. */
#define JANET_RECURSION_GUARD 1024

/* Maximum depth to follow table prototypes before giving up and returning nil. */
#define JANET_MAX_PROTO_DEPTH 200

/* Maximum depth to follow table prototypes before giving up and returning nil. */
#define JANET_MAX_MACRO_EXPAND 200

/* Define max stack size for stacks before raising a stack overflow error.
 * If this is not defined, fiber stacks can grow without limit (until memory
 * runs out) */
#define JANET_STACK_MAX 8192

/* Use nanboxed values - uses 8 bytes per value instead of 12 or 16.
 * To turn of nanboxing, for debugging purposes or for certain
 * architectures (Nanboxing only tested on x86 and x64), comment out
 * the JANET_NANBOX define.*/
#ifndef JANET_NO_NANBOX
#define JANET_NANBOX

/* Further refines the type of nanboxing to use. */
#define JANET_NANBOX_47
#endif

/* Alignment for pointers */
#ifndef JANET_WALIGN
#ifdef JANET_32
#define JANET_WALIGN 4
#else
#define JANET_WALIGN 8
#endif
#endif

/***** END SECTION CONFIG *****/

/***** START SECTION TYPES *****/

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* Names of all of the types */
extern const char *const janet_type_names[16];

/* Fiber signals */
typedef enum {
    JANET_SIGNAL_OK,
    JANET_SIGNAL_ERROR,
    JANET_SIGNAL_DEBUG,
    JANET_SIGNAL_YIELD,
    JANET_SIGNAL_USER0,
    JANET_SIGNAL_USER1,
    JANET_SIGNAL_USER2,
    JANET_SIGNAL_USER3,
    JANET_SIGNAL_USER4,
    JANET_SIGNAL_USER5,
    JANET_SIGNAL_USER6,
    JANET_SIGNAL_USER7,
    JANET_SIGNAL_USER8,
    JANET_SIGNAL_USER9
} JanetSignal;

/* Fiber statuses - mostly corresponds to signals. */
typedef enum {
    JANET_STATUS_DEAD,
    JANET_STATUS_ERROR,
    JANET_STATUS_DEBUG,
    JANET_STATUS_PENDING,
    JANET_STATUS_USER0,
    JANET_STATUS_USER1,
    JANET_STATUS_USER2,
    JANET_STATUS_USER3,
    JANET_STATUS_USER4,
    JANET_STATUS_USER5,
    JANET_STATUS_USER6,
    JANET_STATUS_USER7,
    JANET_STATUS_USER8,
    JANET_STATUS_USER9,
    JANET_STATUS_NEW,
    JANET_STATUS_ALIVE
} JanetFiberStatus;

#ifdef JANET_NANBOX
typedef union Janet Janet;
#else
typedef struct Janet Janet;
#endif

/* All of the janet types */
typedef struct JanetFunction JanetFunction;
typedef struct JanetArray JanetArray;
typedef struct JanetBuffer JanetBuffer;
typedef struct JanetTable JanetTable;
typedef struct JanetFiber JanetFiber;

/* Other structs */
typedef struct JanetAbstractHeader JanetAbstractHeader;
typedef struct JanetFuncDef JanetFuncDef;
typedef struct JanetFuncEnv JanetFuncEnv;
typedef struct JanetKV JanetKV;
typedef struct JanetStackFrame JanetStackFrame;
typedef struct JanetAbstractType JanetAbstractType;
typedef struct JanetArgs JanetArgs;
typedef struct JanetReg JanetReg;
typedef struct JanetSourceMapping JanetSourceMapping;
typedef int (*JanetCFunction)(JanetArgs args);

/* Basic types for all Janet Values */
typedef enum JanetType {
    JANET_NIL,
    JANET_FALSE,
    JANET_TRUE,
    JANET_FIBER,
    JANET_INTEGER,
    JANET_REAL,
    JANET_STRING,
    JANET_SYMBOL,
    JANET_ARRAY,
    JANET_TUPLE,
    JANET_TABLE,
    JANET_STRUCT,
    JANET_BUFFER,
    JANET_FUNCTION,
    JANET_CFUNCTION,
    JANET_ABSTRACT
} JanetType;

#define JANET_COUNT_TYPES (JANET_ABSTRACT + 1)

/* Type flags */
#define JANET_TFLAG_NIL (1 << JANET_NIL)
#define JANET_TFLAG_FALSE (1 << JANET_FALSE)
#define JANET_TFLAG_TRUE (1 << JANET_TRUE)
#define JANET_TFLAG_FIBER (1 << JANET_FIBER)
#define JANET_TFLAG_INTEGER (1 << JANET_INTEGER)
#define JANET_TFLAG_REAL (1 << JANET_REAL)
#define JANET_TFLAG_STRING (1 << JANET_STRING)
#define JANET_TFLAG_SYMBOL (1 << JANET_SYMBOL)
#define JANET_TFLAG_ARRAY (1 << JANET_ARRAY)
#define JANET_TFLAG_TUPLE (1 << JANET_TUPLE)
#define JANET_TFLAG_TABLE (1 << JANET_TABLE)
#define JANET_TFLAG_STRUCT (1 << JANET_STRUCT)
#define JANET_TFLAG_BUFFER (1 << JANET_BUFFER)
#define JANET_TFLAG_FUNCTION (1 << JANET_FUNCTION)
#define JANET_TFLAG_CFUNCTION (1 << JANET_CFUNCTION)
#define JANET_TFLAG_ABSTRACT (1 << JANET_ABSTRACT)

/* Some abstractions */
#define JANET_TFLAG_BOOLEAN (JANET_TFLAG_TRUE | JANET_TFLAG_FALSE)
#define JANET_TFLAG_NUMBER (JANET_TFLAG_REAL | JANET_TFLAG_INTEGER)
#define JANET_TFLAG_CALLABLE (JANET_TFLAG_FUNCTION | JANET_TFLAG_CFUNCTION)
#define JANET_TFLAG_BYTES (JANET_TFLAG_STRING | JANET_TFLAG_SYMBOL | JANET_TFLAG_BUFFER)
#define JANET_TFLAG_INDEXED (JANET_TFLAG_ARRAY | JANET_TFLAG_TUPLE)
#define JANET_TFLAG_DICTIONARY (JANET_TFLAG_TABLE | JANET_TFLAG_STRUCT)
#define JANET_TFLAG_LENGTHABLE (JANET_TFLAG_BYTES | JANET_TFLAG_INDEXED | JANET_TFLAG_DICTIONARY)

/* We provide two possible implemenations of Janets. The preferred
 * nanboxing approach, and the standard C version. Code in the rest of the
 * application must interact through exposed interface. */

/* Required interface for Janet */
/* wrap and unwrap for all types */
/* Get type quickly */
/* Check against type quickly */
/* Small footprint */
/* 32 bit integer support */

/* janet_type(x)
 * janet_checktype(x, t)
 * janet_wrap_##TYPE(x)
 * janet_unwrap_##TYPE(x)
 * janet_truthy(x)
 * janet_memclear(p, n) - clear memory for hash tables to nils
 * janet_u64(x) - get 64 bits of payload for hashing
 */

#ifdef JANET_NANBOX

#include <math.h>

union Janet {
    uint64_t u64;
    int64_t i64;
    double real;
};

#define janet_u64(x) ((x).u64)

/* This representation uses 48 bit pointers. The trade off vs. the LuaJIT style
 * 47 bit payload representaion is that the type bits are no long contiguous. Type
 * checking can still be fast, but typewise polymorphism takes a bit longer. However,
 * hopefully we can avoid some annoying problems that occur when trying to use 47 bit pointers
 * in a 48 bit address space (Linux on ARM). If JANET_NANBOX_47 is set, use 47 bit tagged pointers. */

/*                    |.......Tag.......|.......................Payload..................| */
/* Non-double:        t|11111111111|1ttt|xxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx */
/* Types of NIL, TRUE, and FALSE must have payload set to all 1s. */

/* Double (no NaNs):   x xxxxxxxxxxx xxxx xxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx */

#if defined (JANET_NANBOX_47) || defined (JANET_32)

#define JANET_NANBOX_TAGBITS     0xFFFF800000000000llu
#define JANET_NANBOX_PAYLOADBITS 0x00007FFFFFFFFFFFllu


#define janet_nanbox_lowtag(type) \
    ((uint64_t)(type) | 0x1FFF0)

#define janet_nanbox_tag(type) \
    (janet_nanbox_lowtag(type) << 47)

#define janet_type(x) \
    (isnan((x).real) \
        ? (((x).u64 >> 47) & 0xF) \
        : JANET_REAL)

#else /* defined (JANET_NANBOX_47) || defined (JANET_32) */

#define JANET_NANBOX_TAGBITS     0xFFFF000000000000llu
#define JANET_NANBOX_PAYLOADBITS 0x0000FFFFFFFFFFFFllu

#define janet_nanbox_lowtag(type) \
    ((((uint64_t)(type) & 0x1) << 15) | 0x7FF8 | ((type) >> 1))

#define janet_nanbox_tag(type) \
    (janet_nanbox_lowtag(type) << 48)

#define janet_type(x) \
    (isnan((x).real) \
        ? (((x).u64 >> 47) & 0xE) | ((x).u64 >> 63) \
        : JANET_REAL)

#endif /* defined (JANET_NANBOX_47) || defined (JANET_32) */

/* 32 bit mode will not use the full payload for pointers. */
#ifdef JANET_32

#define JANET_NANBOX_POINTERBITS 0xFFFFFFFFllu
#else
#define JANET_NANBOX_POINTERBITS JANET_NANBOX_PAYLOADBITS
#endif

#define janet_nanbox_checkauxtype(x, type) \
    (((x).u64 & JANET_NANBOX_TAGBITS) == janet_nanbox_tag((type)))

#define janet_nanbox_isreal(x) \
    (!isnan((x).real) || janet_nanbox_checkauxtype((x), JANET_REAL))

#define janet_checktype(x, t) \
    (((t) == JANET_REAL) \
        ? janet_nanbox_isreal(x) \
        : janet_nanbox_checkauxtype((x), (t)))

JANET_API void *janet_nanbox_to_pointer(Janet x);
JANET_API void janet_nanbox_memempty(JanetKV *mem, int32_t count);
JANET_API void *janet_nanbox_memalloc_empty(int32_t count);
JANET_API Janet janet_nanbox_from_pointer(void *p, uint64_t tagmask);
JANET_API Janet janet_nanbox_from_cpointer(const void *p, uint64_t tagmask);
JANET_API Janet janet_nanbox_from_double(double d);
JANET_API Janet janet_nanbox_from_bits(uint64_t bits);

#define janet_memempty(mem, len) janet_nanbox_memempty((mem), (len))
#define janet_memalloc_empty(count) janet_nanbox_memalloc_empty(count)

/* Todo - check for single mask operation */
#define janet_truthy(x) \
    (!(janet_checktype((x), JANET_NIL) || janet_checktype((x), JANET_FALSE)))

#define janet_nanbox_from_payload(t, p) \
    janet_nanbox_from_bits(janet_nanbox_tag(t) | (p))

#define janet_nanbox_wrap_(p, t) \
    janet_nanbox_from_pointer((p), janet_nanbox_tag(t))

#define janet_nanbox_wrap_c(p, t) \
    janet_nanbox_from_cpointer((p), janet_nanbox_tag(t))

/* Wrap the simple types */
#define janet_wrap_nil() janet_nanbox_from_payload(JANET_NIL, 1)
#define janet_wrap_true() janet_nanbox_from_payload(JANET_TRUE, 1)
#define janet_wrap_false() janet_nanbox_from_payload(JANET_FALSE, 1)
#define janet_wrap_boolean(b) janet_nanbox_from_payload((b) ? JANET_TRUE : JANET_FALSE, 1)
#define janet_wrap_integer(i) janet_nanbox_from_payload(JANET_INTEGER, (uint32_t)(i))
#define janet_wrap_real(r) janet_nanbox_from_double(r)

/* Unwrap the simple types */
#define janet_unwrap_boolean(x) \
    (janet_checktype(x, JANET_TRUE))
#define janet_unwrap_integer(x) \
    ((int32_t)((x).u64 & 0xFFFFFFFFlu))
#define janet_unwrap_real(x) ((x).real)

/* Wrap the pointer types */
#define janet_wrap_struct(s) janet_nanbox_wrap_c((s), JANET_STRUCT)
#define janet_wrap_tuple(s) janet_nanbox_wrap_c((s), JANET_TUPLE)
#define janet_wrap_fiber(s) janet_nanbox_wrap_((s), JANET_FIBER)
#define janet_wrap_array(s) janet_nanbox_wrap_((s), JANET_ARRAY)
#define janet_wrap_table(s) janet_nanbox_wrap_((s), JANET_TABLE)
#define janet_wrap_buffer(s) janet_nanbox_wrap_((s), JANET_BUFFER)
#define janet_wrap_string(s) janet_nanbox_wrap_c((s), JANET_STRING)
#define janet_wrap_symbol(s) janet_nanbox_wrap_c((s), JANET_SYMBOL)
#define janet_wrap_abstract(s) janet_nanbox_wrap_((s), JANET_ABSTRACT)
#define janet_wrap_function(s) janet_nanbox_wrap_((s), JANET_FUNCTION)
#define janet_wrap_cfunction(s) janet_nanbox_wrap_((s), JANET_CFUNCTION)

/* Unwrap the pointer types */
#define janet_unwrap_struct(x) ((const JanetKV *)janet_nanbox_to_pointer(x))
#define janet_unwrap_tuple(x) ((const Janet *)janet_nanbox_to_pointer(x))
#define janet_unwrap_fiber(x) ((JanetFiber *)janet_nanbox_to_pointer(x))
#define janet_unwrap_array(x) ((JanetArray *)janet_nanbox_to_pointer(x))
#define janet_unwrap_table(x) ((JanetTable *)janet_nanbox_to_pointer(x))
#define janet_unwrap_buffer(x) ((JanetBuffer *)janet_nanbox_to_pointer(x))
#define janet_unwrap_string(x) ((const uint8_t *)janet_nanbox_to_pointer(x))
#define janet_unwrap_symbol(x) ((const uint8_t *)janet_nanbox_to_pointer(x))
#define janet_unwrap_abstract(x) (janet_nanbox_to_pointer(x))
#define janet_unwrap_pointer(x) (janet_nanbox_to_pointer(x))
#define janet_unwrap_function(x) ((JanetFunction *)janet_nanbox_to_pointer(x))
#define janet_unwrap_cfunction(x) ((JanetCFunction)janet_nanbox_to_pointer(x))

/* End of [#ifdef JANET_NANBOX] */
#else

/* A general janet value type */
struct Janet {
    union {
        uint64_t u64;
        double real;
        int32_t integer;
        void *pointer;
        const void *cpointer;
    } as;
    JanetType type;
};

#define janet_u64(x) ((x).as.u64)
#define janet_memempty(mem, count) memset((mem), 0, sizeof(JanetKV) * (count))
#define janet_memalloc_empty(count) calloc((count), sizeof(JanetKV))
#define janet_type(x) ((x).type)
#define janet_checktype(x, t) ((x).type == (t))
#define janet_truthy(x) \
    ((x).type != JANET_NIL && (x).type != JANET_FALSE)

#define janet_unwrap_struct(x) ((const JanetKV *)(x).as.pointer)
#define janet_unwrap_tuple(x) ((const Janet *)(x).as.pointer)
#define janet_unwrap_fiber(x) ((JanetFiber *)(x).as.pointer)
#define janet_unwrap_array(x) ((JanetArray *)(x).as.pointer)
#define janet_unwrap_table(x) ((JanetTable *)(x).as.pointer)
#define janet_unwrap_buffer(x) ((JanetBuffer *)(x).as.pointer)
#define janet_unwrap_string(x) ((const uint8_t *)(x).as.pointer)
#define janet_unwrap_symbol(x) ((const uint8_t *)(x).as.pointer)
#define janet_unwrap_abstract(x) ((x).as.pointer)
#define janet_unwrap_pointer(x) ((x).as.pointer)
#define janet_unwrap_function(x) ((JanetFunction *)(x).as.pointer)
#define janet_unwrap_cfunction(x) ((JanetCFunction)(x).as.pointer)
#define janet_unwrap_boolean(x) ((x).type == JANET_TRUE)
#define janet_unwrap_integer(x) ((x).as.integer)
#define janet_unwrap_real(x) ((x).as.real)

JANET_API Janet janet_wrap_nil(void);
JANET_API Janet janet_wrap_real(double x);
JANET_API Janet janet_wrap_integer(int32_t x);
JANET_API Janet janet_wrap_true(void);
JANET_API Janet janet_wrap_false(void);
JANET_API Janet janet_wrap_boolean(int x);
JANET_API Janet janet_wrap_string(const uint8_t *x);
JANET_API Janet janet_wrap_symbol(const uint8_t *x);
JANET_API Janet janet_wrap_array(JanetArray *x);
JANET_API Janet janet_wrap_tuple(const Janet *x);
JANET_API Janet janet_wrap_struct(const JanetKV *x);
JANET_API Janet janet_wrap_fiber(JanetFiber *x);
JANET_API Janet janet_wrap_buffer(JanetBuffer *x);
JANET_API Janet janet_wrap_function(JanetFunction *x);
JANET_API Janet janet_wrap_cfunction(JanetCFunction x);
JANET_API Janet janet_wrap_table(JanetTable *x);
JANET_API Janet janet_wrap_abstract(void *x);

/* End of tagged union implementation */
#endif

/* Hold components of arguments passed to JanetCFunction. */
struct JanetArgs {
    Janet *v;
    Janet *ret;
    int32_t n;
};

/* Fiber flags */
#define JANET_FIBER_FLAG_SIGNAL_WAITING (1 << 30)

/* Fiber signal masks. */
#define JANET_FIBER_MASK_ERROR 2
#define JANET_FIBER_MASK_DEBUG 4
#define JANET_FIBER_MASK_YIELD 8

#define JANET_FIBER_MASK_USER0 (16 << 0)
#define JANET_FIBER_MASK_USER1 (16 << 1)
#define JANET_FIBER_MASK_USER2 (16 << 2)
#define JANET_FIBER_MASK_USER3 (16 << 3)
#define JANET_FIBER_MASK_USER4 (16 << 4)
#define JANET_FIBER_MASK_USER5 (16 << 5)
#define JANET_FIBER_MASK_USER6 (16 << 6)
#define JANET_FIBER_MASK_USER7 (16 << 7)
#define JANET_FIBER_MASK_USER8 (16 << 8)
#define JANET_FIBER_MASK_USER9 (16 << 9)

#define JANET_FIBER_MASK_USERN(N) (16 << (N))
#define JANET_FIBER_MASK_USER 0x3FF0

#define JANET_FIBER_STATUS_MASK 0xFF0000
#define JANET_FIBER_STATUS_OFFSET 16

/* A lightweight green thread in janet. Does not correspond to
 * operating system threads. */
struct JanetFiber {
    Janet *data;
    JanetFiber *child; /* Keep linked list of fibers for restarting pending fibers */
    JanetFunction *root; /* First value */
    int32_t frame; /* Index of the stack frame */
    int32_t stackstart; /* Beginning of next args */
    int32_t stacktop; /* Top of stack. Where values are pushed and popped from. */
    int32_t capacity;
    int32_t maxstack; /* Arbitrary defined limit for stack overflow */
    int32_t flags; /* Various flags */
};

/* Mark if a stack frame is a tail call for debugging */
#define JANET_STACKFRAME_TAILCALL 1

/* A stack frame on the fiber. Is stored along with the stack values. */
struct JanetStackFrame {
    JanetFunction *func;
    uint32_t *pc;
    JanetFuncEnv *env;
    int32_t prevframe;
    int32_t flags;
};

/* Number of Janets a frame takes up in the stack */
#define JANET_FRAME_SIZE ((sizeof(JanetStackFrame) + sizeof(Janet) - 1) / sizeof(Janet))

/* A dynamic array type. */
struct JanetArray {
    Janet *data;
    int32_t count;
    int32_t capacity;
};

/* A bytebuffer type. Used as a mutable string or string builder. */
struct JanetBuffer {
    uint8_t *data;
    int32_t count;
    int32_t capacity;
};

/* A mutable associative data type. Backed by a hashtable. */
struct JanetTable {
    JanetKV *data;
    JanetTable *proto;
    int32_t count;
    int32_t capacity;
    int32_t deleted;
};

/* A key value pair in a struct or table */
struct JanetKV {
    Janet key;
    Janet value;
};

/* Some function defintion flags */
#define JANET_FUNCDEF_FLAG_VARARG 0x10000
#define JANET_FUNCDEF_FLAG_NEEDSENV 0x20000
#define JANET_FUNCDEF_FLAG_FIXARITY 0x40000
#define JANET_FUNCDEF_FLAG_HASNAME 0x80000
#define JANET_FUNCDEF_FLAG_HASSOURCE 0x100000
#define JANET_FUNCDEF_FLAG_HASDEFS 0x200000
#define JANET_FUNCDEF_FLAG_HASENVS 0x400000
#define JANET_FUNCDEF_FLAG_HASSOURCEMAP 0x800000
#define JANET_FUNCDEF_FLAG_TAG 0xFFFF

/* Source mapping structure for a bytecode instruction */
struct JanetSourceMapping {
    int32_t line;
    int32_t column;
};

/* A function definition. Contains information needed to instantiate closures. */
struct JanetFuncDef {
    int32_t *environments; /* Which environments to capture from parent. */
    Janet *constants;
    JanetFuncDef **defs;
    uint32_t *bytecode;

    /* Various debug information */
    JanetSourceMapping *sourcemap;
    const uint8_t *source;
    const uint8_t *name;

    int32_t flags;
    int32_t slotcount; /* The amount of stack space required for the function */
    int32_t arity; /* Not including varargs */
    int32_t constants_length;
    int32_t bytecode_length;
    int32_t environments_length;
    int32_t defs_length;
};

/* A fuction environment */
struct JanetFuncEnv {
    union {
        JanetFiber *fiber;
        Janet *values;
    } as;
    int32_t length; /* Size of environment */
    int32_t offset; /* Stack offset when values still on stack. If offset is <= 0, then
        environment is no longer on the stack. */
};

/* A function */
struct JanetFunction {
    JanetFuncDef *def;
    JanetFuncEnv *envs[];
};

typedef struct JanetParseState JanetParseState;
typedef struct JanetParser JanetParser;

enum JanetParserStatus {
    JANET_PARSE_ROOT,
    JANET_PARSE_ERROR,
    JANET_PARSE_FULL,
    JANET_PARSE_PENDING
};

/* A janet parser */
struct JanetParser {
    Janet* args;
    const char *error;
    JanetParseState *states;
    uint8_t *buf;
    size_t argcount;
    size_t argcap;
    size_t statecount;
    size_t statecap;
    size_t bufcount;
    size_t bufcap;
    size_t line;
    size_t col;
    int lookback;
};

/* Defines an abstract type */
struct JanetAbstractType {
    const char *name;
    int (*gc)(void *data, size_t len);
    int (*gcmark)(void *data, size_t len);
};

/* Contains information about userdata */
struct JanetAbstractHeader {
    const JanetAbstractType *type;
    size_t size;
};

struct JanetReg {
    const char *name;
    JanetCFunction cfun;
};

/***** END SECTION TYPES *****/

/***** START SECTION OPCODES *****/

/* Bytecode op argument types */
enum JanetOpArgType {
    JANET_OAT_SLOT,
    JANET_OAT_ENVIRONMENT,
    JANET_OAT_CONSTANT,
    JANET_OAT_INTEGER,
    JANET_OAT_TYPE,
    JANET_OAT_SIMPLETYPE,
    JANET_OAT_LABEL,
    JANET_OAT_FUNCDEF
};

/* Various types of instructions */
enum JanetInstructionType {
    JINT_0, /* No args */
    JINT_S, /* Slot(3) */
    JINT_L, /* Label(3) */
    JINT_SS, /* Slot(1), Slot(2) */
    JINT_SL, /* Slot(1), Label(2) */
    JINT_ST, /* Slot(1), Slot(2) */
    JINT_SI, /* Slot(1), Immediate(2) */
    JINT_SD, /* Slot(1), Closure(2) */
    JINT_SU, /* Slot(1), Unsigned Immediate(2) */
    JINT_SSS, /* Slot(1), Slot(1), Slot(1) */
    JINT_SSI, /* Slot(1), Slot(1), Immediate(1) */
    JINT_SSU, /* Slot(1), Slot(1), Unsigned Immediate(1) */
    JINT_SES, /* Slot(1), Environment(1), Far Slot(1) */
    JINT_SC /* Slot(1), Constant(2) */
};

/* All opcodes for the bytecode interpreter. */
enum JanetOpCode {
    JOP_NOOP,
    JOP_ERROR,
    JOP_TYPECHECK,
    JOP_RETURN,
    JOP_RETURN_NIL,
    JOP_ADD_INTEGER,
    JOP_ADD_IMMEDIATE,
    JOP_ADD_REAL,
    JOP_ADD,
    JOP_SUBTRACT_INTEGER,
    JOP_SUBTRACT_REAL,
    JOP_SUBTRACT,
    JOP_MULTIPLY_INTEGER,
    JOP_MULTIPLY_IMMEDIATE,
    JOP_MULTIPLY_REAL,
    JOP_MULTIPLY,
    JOP_DIVIDE_INTEGER,
    JOP_DIVIDE_IMMEDIATE,
    JOP_DIVIDE_REAL,
    JOP_DIVIDE,
    JOP_BAND,
    JOP_BOR,
    JOP_BXOR,
    JOP_BNOT,
    JOP_SHIFT_LEFT,
    JOP_SHIFT_LEFT_IMMEDIATE,
    JOP_SHIFT_RIGHT,
    JOP_SHIFT_RIGHT_IMMEDIATE,
    JOP_SHIFT_RIGHT_UNSIGNED,
    JOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE,
    JOP_MOVE_FAR,
    JOP_MOVE_NEAR,
    JOP_JUMP,
    JOP_JUMP_IF,
    JOP_JUMP_IF_NOT,
    JOP_GREATER_THAN,
    JOP_GREATER_THAN_INTEGER,
    JOP_GREATER_THAN_IMMEDIATE,
    JOP_GREATER_THAN_REAL,
    JOP_GREATER_THAN_EQUAL_REAL,
    JOP_LESS_THAN,
    JOP_LESS_THAN_INTEGER,
    JOP_LESS_THAN_IMMEDIATE,
    JOP_LESS_THAN_REAL,
    JOP_LESS_THAN_EQUAL_REAL,
    JOP_EQUALS,
    JOP_EQUALS_INTEGER,
    JOP_EQUALS_IMMEDIATE,
    JOP_EQUALS_REAL,
    JOP_COMPARE,
    JOP_LOAD_NIL,
    JOP_LOAD_TRUE,
    JOP_LOAD_FALSE,
    JOP_LOAD_INTEGER,
    JOP_LOAD_CONSTANT,
    JOP_LOAD_UPVALUE,
    JOP_LOAD_SELF,
    JOP_SET_UPVALUE,
    JOP_CLOSURE,
    JOP_PUSH,
    JOP_PUSH_2,
    JOP_PUSH_3,
    JOP_PUSH_ARRAY,
    JOP_CALL,
    JOP_TAILCALL,
    JOP_RESUME,
    JOP_SIGNAL,
    JOP_GET,
    JOP_PUT,
    JOP_GET_INDEX,
    JOP_PUT_INDEX,
    JOP_LENGTH,
    JOP_MAKE_ARRAY,
    JOP_MAKE_BUFFER,
    JOP_MAKE_STRING,
    JOP_MAKE_STRUCT,
    JOP_MAKE_TABLE,
    JOP_MAKE_TUPLE,
    JOP_NUMERIC_LESS_THAN,
    JOP_NUMERIC_LESS_THAN_EQUAL,
    JOP_NUMERIC_GREATER_THAN,
    JOP_NUMERIC_GREATER_THAN_EQUAL,
    JOP_NUMERIC_EQUAL,
    JOP_INSTRUCTION_COUNT
};

/* Info about all instructions */
extern enum JanetInstructionType janet_instructions[JOP_INSTRUCTION_COUNT];

/***** END SECTION OPCODES *****/

/***** START SECTION MAIN *****/

/* Parsing */
JANET_API void janet_parser_init(JanetParser *parser);
JANET_API void janet_parser_deinit(JanetParser *parser);
JANET_API int janet_parser_consume(JanetParser *parser, uint8_t c);
JANET_API enum JanetParserStatus janet_parser_status(JanetParser *parser);
JANET_API Janet janet_parser_produce(JanetParser *parser);
JANET_API const char *janet_parser_error(JanetParser *parser);
JANET_API void janet_parser_flush(JanetParser *parser);
JANET_API JanetParser *janet_check_parser(Janet x);

/* Assembly */
#ifdef JANET_ASSEMBLER
typedef struct JanetAssembleResult JanetAssembleResult;
enum JanetAssembleStatus {
    JANET_ASSEMBLE_OK,
    JANET_ASSEMBLE_ERROR
};
struct JanetAssembleResult {
    JanetFuncDef *funcdef;
    const uint8_t *error;
    enum JanetAssembleStatus status;
};
JANET_API JanetAssembleResult janet_asm(Janet source, int flags);
JANET_API Janet janet_disasm(JanetFuncDef *def);
JANET_API Janet janet_asm_decode_instruction(uint32_t instr);
#endif

/* Compilation */
typedef struct JanetCompileResult JanetCompileResult;
enum JanetCompileStatus {
    JANET_COMPILE_OK,
    JANET_COMPILE_ERROR
};
struct JanetCompileResult {
    JanetFuncDef *funcdef;
    const uint8_t *error;
    JanetFiber *macrofiber;
    JanetSourceMapping error_mapping;
    enum JanetCompileStatus status;
};
JANET_API JanetCompileResult janet_compile(Janet source, JanetTable *env, const uint8_t *where);

/* Get the default environment for janet */
JANET_API JanetTable *janet_core_env(void);

JANET_API int janet_dobytes(JanetTable *env, const uint8_t *bytes, int32_t len, const char *sourcePath, Janet *out);
JANET_API int janet_dostring(JanetTable *env, const char *str, const char *sourcePath, Janet *out);

/* Number scanning */
JANET_API Janet janet_scan_number(const uint8_t *src, int32_t len);
JANET_API int32_t janet_scan_integer(const uint8_t *str, int32_t len, int *err);
JANET_API double janet_scan_real(const uint8_t *str, int32_t len, int *err);

/* Array functions */
JANET_API JanetArray *janet_array(int32_t capacity);
JANET_API JanetArray *janet_array_n(const Janet *elements, int32_t n);
JANET_API JanetArray *janet_array_init(JanetArray *array, int32_t capacity);
JANET_API void janet_array_deinit(JanetArray *array);
JANET_API void janet_array_ensure(JanetArray *array, int32_t capacity, int32_t growth);
JANET_API void janet_array_setcount(JanetArray *array, int32_t count);
JANET_API void janet_array_push(JanetArray *array, Janet x);
JANET_API Janet janet_array_pop(JanetArray *array);
JANET_API Janet janet_array_peek(JanetArray *array);

/* Buffer functions */
JANET_API JanetBuffer *janet_buffer(int32_t capacity);
JANET_API JanetBuffer *janet_buffer_init(JanetBuffer *buffer, int32_t capacity);
JANET_API void janet_buffer_deinit(JanetBuffer *buffer);
JANET_API void janet_buffer_ensure(JanetBuffer *buffer, int32_t capacity, int32_t growth);
JANET_API void janet_buffer_setcount(JanetBuffer *buffer, int32_t count);
JANET_API int janet_buffer_extra(JanetBuffer *buffer, int32_t n);
JANET_API int janet_buffer_push_bytes(JanetBuffer *buffer, const uint8_t *string, int32_t len);
JANET_API int janet_buffer_push_string(JanetBuffer *buffer, const uint8_t *string);
JANET_API int janet_buffer_push_cstring(JanetBuffer *buffer, const char *cstring);
JANET_API int janet_buffer_push_u8(JanetBuffer *buffer, uint8_t x);
JANET_API int janet_buffer_push_u16(JanetBuffer *buffer, uint16_t x);
JANET_API int janet_buffer_push_u32(JanetBuffer *buffer, uint32_t x);
JANET_API int janet_buffer_push_u64(JanetBuffer *buffer, uint64_t x);

/* Tuple */
#define janet_tuple_raw(t) ((int32_t *)(t) - 4)
#define janet_tuple_length(t) (janet_tuple_raw(t)[0])
#define janet_tuple_hash(t) ((janet_tuple_raw(t)[1]))
#define janet_tuple_sm_line(t) ((janet_tuple_raw(t)[2]))
#define janet_tuple_sm_col(t) ((janet_tuple_raw(t)[3]))
JANET_API Janet *janet_tuple_begin(int32_t length);
JANET_API const Janet *janet_tuple_end(Janet *tuple);
JANET_API const Janet *janet_tuple_n(const Janet *values, int32_t n);
JANET_API int janet_tuple_equal(const Janet *lhs, const Janet *rhs);
JANET_API int janet_tuple_compare(const Janet *lhs, const Janet *rhs);

/* String/Symbol functions */
#define janet_string_raw(s) ((int32_t *)(s) - 2)
#define janet_string_length(s) (janet_string_raw(s)[0])
#define janet_string_hash(s) ((janet_string_raw(s)[1]))
JANET_API uint8_t *janet_string_begin(int32_t length);
JANET_API const uint8_t *janet_string_end(uint8_t *str);
JANET_API const uint8_t *janet_string(const uint8_t *buf, int32_t len);
JANET_API const uint8_t *janet_cstring(const char *cstring);
JANET_API int janet_string_compare(const uint8_t *lhs, const uint8_t *rhs);
JANET_API int janet_string_equal(const uint8_t *lhs, const uint8_t *rhs);
JANET_API int janet_string_equalconst(const uint8_t *lhs, const uint8_t *rhs, int32_t rlen, int32_t rhash);
JANET_API const uint8_t *janet_string_unique(const uint8_t *buf, int32_t len);
JANET_API const uint8_t *janet_cstring_unique(const char *s);
JANET_API const uint8_t *janet_description(Janet x);
JANET_API const uint8_t *janet_to_string(Janet x);
JANET_API void janet_to_string_b(JanetBuffer *buffer, Janet x);
JANET_API void janet_to_description_b(JanetBuffer *buffer, Janet x);
#define janet_cstringv(cstr) janet_wrap_string(janet_cstring(cstr))
#define janet_stringv(str, len) janet_wrap_string(janet_string((str), (len)))
JANET_API const uint8_t *janet_formatc(const char *format, ...);
JANET_API void janet_puts(const uint8_t *str);

/* Symbol functions */
JANET_API const uint8_t *janet_symbol(const uint8_t *str, int32_t len);
JANET_API const uint8_t *janet_symbol_from_string(const uint8_t *str);
JANET_API const uint8_t *janet_csymbol(const char *str);
JANET_API const uint8_t *janet_symbol_gen(void);
#define janet_symbolv(str, len) janet_wrap_symbol(janet_symbol((str), (len)))
#define janet_csymbolv(cstr) janet_wrap_symbol(janet_csymbol(cstr))

/* Structs */
#define janet_struct_raw(t) ((int32_t *)(t) - 4)
#define janet_struct_length(t) (janet_struct_raw(t)[0])
#define janet_struct_capacity(t) (janet_struct_raw(t)[1])
#define janet_struct_hash(t) (janet_struct_raw(t)[2])
/* Do something with the 4th header slot - flags? */
JANET_API JanetKV *janet_struct_begin(int32_t count);
JANET_API void janet_struct_put(JanetKV *st, Janet key, Janet value);
JANET_API const JanetKV *janet_struct_end(JanetKV *st);
JANET_API Janet janet_struct_get(const JanetKV *st, Janet key);
JANET_API const JanetKV *janet_struct_next(const JanetKV *st, const JanetKV *kv);
JANET_API JanetTable *janet_struct_to_table(const JanetKV *st);
JANET_API int janet_struct_equal(const JanetKV *lhs, const JanetKV *rhs);
JANET_API int janet_struct_compare(const JanetKV *lhs, const JanetKV *rhs);
JANET_API const JanetKV *janet_struct_find(const JanetKV *st, Janet key);

/* Table functions */
JANET_API JanetTable *janet_table(int32_t capacity);
JANET_API JanetTable *janet_table_init(JanetTable *table, int32_t capacity);
JANET_API void janet_table_deinit(JanetTable *table);
JANET_API Janet janet_table_get(JanetTable *t, Janet key);
JANET_API Janet janet_table_rawget(JanetTable *t, Janet key);
JANET_API Janet janet_table_remove(JanetTable *t, Janet key);
JANET_API void janet_table_put(JanetTable *t, Janet key, Janet value);
JANET_API const JanetKV *janet_table_next(JanetTable *t, const JanetKV *kv);
JANET_API const JanetKV *janet_table_to_struct(JanetTable *t);
JANET_API void janet_table_merge_table(JanetTable *table, JanetTable *other);
JANET_API void janet_table_merge_struct(JanetTable *table, const JanetKV *other);
JANET_API JanetKV *janet_table_find(JanetTable *t, Janet key);

/* Fiber */
JANET_API JanetFiber *janet_fiber(JanetFunction *callee, int32_t capacity);
#define janet_fiber_status(f) (((f)->flags & JANET_FIBER_STATUS_MASK) >> JANET_FIBER_STATUS_OFFSET)

/* Treat similar types through uniform interfaces for iteration */
JANET_API int janet_indexed_view(Janet seq, const Janet **data, int32_t *len);
JANET_API int janet_bytes_view(Janet str, const uint8_t **data, int32_t *len);
JANET_API int janet_dictionary_view(Janet tab, const JanetKV **data, int32_t *len, int32_t *cap);
JANET_API Janet janet_dictionary_get(const JanetKV *data, int32_t cap, Janet key);
JANET_API const JanetKV *janet_dictionary_next(const JanetKV *kvs, int32_t cap, const JanetKV *kv);

/* Abstract */
#define janet_abstract_header(u) ((JanetAbstractHeader *)(u) - 1)
#define janet_abstract_type(u) (janet_abstract_header(u)->type)
#define janet_abstract_size(u) (janet_abstract_header(u)->size)
JANET_API void *janet_abstract(const JanetAbstractType *type, size_t size);

/* Native */
JANET_API JanetCFunction janet_native(const char *name, const uint8_t **error);

/* Marshaling */
JANET_API int janet_marshal(
        JanetBuffer *buf,
        Janet x,
        Janet *errval,
        JanetTable *rreg,
        int flags);
JANET_API int janet_unmarshal(
        const uint8_t *bytes,
        size_t len,
        int flags,
        Janet *out,
        JanetTable *reg,
        const uint8_t **next);
JANET_API JanetTable *janet_env_lookup(JanetTable *env);

/* GC */
JANET_API void janet_mark(Janet x);
JANET_API void janet_sweep(void);
JANET_API void janet_collect(void);
JANET_API void janet_clear_memory(void);
JANET_API void janet_gcroot(Janet root);
JANET_API int janet_gcunroot(Janet root);
JANET_API int janet_gcunrootall(Janet root);
JANET_API int janet_gclock(void);
JANET_API void janet_gcunlock(int handle);

/* Functions */
JANET_API JanetFuncDef *janet_funcdef_alloc(void);
JANET_API JanetFunction *janet_thunk(JanetFuncDef *def);
JANET_API int janet_verify(JanetFuncDef *def);

/* Misc */
JANET_API int janet_equals(Janet x, Janet y);
JANET_API int32_t janet_hash(Janet x);
JANET_API int janet_compare(Janet x, Janet y);
JANET_API int janet_cstrcmp(const uint8_t *str, const char *other);

/* VM functions */
JANET_API int janet_init(void);
JANET_API void janet_deinit(void);
JANET_API JanetSignal janet_continue(JanetFiber *fiber, Janet in, Janet *out);
#define janet_run(F,O) janet_continue(F, janet_wrap_nil(), O)
JANET_API JanetSignal janet_call(JanetFunction *fun, int32_t argn, const Janet *argv, Janet *out, JanetFiber **f);
JANET_API void janet_stacktrace(JanetFiber *fiber, const char *errtype, Janet err);

/* C Library helpers */
typedef enum {
    JANET_BINDING_NONE,
    JANET_BINDING_DEF,
    JANET_BINDING_VAR,
    JANET_BINDING_MACRO
} JanetBindingType;
JANET_API void janet_def(JanetTable *env, const char *name, Janet val);
JANET_API void janet_var(JanetTable *env, const char *name, Janet val);
JANET_API void janet_cfuns(JanetTable *env, const char *regprefix, const JanetReg *cfuns);
JANET_API JanetBindingType janet_resolve(JanetTable *env, const uint8_t *sym, Janet *out);
JANET_API JanetTable *janet_env(JanetArgs args);
JANET_API void janet_register(const char *name, JanetCFunction cfun);

/* C Function helpers */
JANET_API int janet_arity_err(JanetArgs args, int32_t n, const char *prefix);
JANET_API int janet_type_err(JanetArgs args, int32_t n, JanetType expected);
JANET_API int janet_typemany_err(JanetArgs args, int32_t n, int expected);
JANET_API int janet_typeabstract_err(JanetArgs args, int32_t n, const JanetAbstractType *at);

/* Helpers for writing modules */
#define JANET_MODULE_ENTRY JANET_API int _janet_init

/***** END SECTION MAIN *****/

/***** START SECTION MACROS *****/

/* Macros */
#define JANET_THROW(a, e) return (*((a).ret) = janet_cstringv(e), JANET_SIGNAL_ERROR)
#define JANET_THROWV(a, v) return (*((a).ret) = (v), JANET_SIGNAL_ERROR)
#define JANET_RETURN(a, v) return (*((a).ret) = (v), JANET_SIGNAL_OK)

/* Early exit macros */
#define JANET_MAXARITY(A, N) do { if ((A).n > (N))\
    return janet_arity_err(A, N, "at most "); } while (0)
#define JANET_MINARITY(A, N) do { if ((A).n < (N))\
    return janet_arity_err(A, N, "at least "); } while (0)
#define JANET_FIXARITY(A, N) do { if ((A).n != (N))\
    return janet_arity_err(A, N, ""); } while (0)
#define JANET_CHECK(A, N, T) do {\
    if ((A).n > (N)) {\
       if (!janet_checktype((A).v[(N)], (T))) return janet_type_err(A, N, T);\
    } else {\
       if ((T) != JANET_NIL) return janet_type_err(A, N, T);\
    }\
} while (0)
#define JANET_CHECKMANY(A, N, TS) do {\
    if ((A).n > (N)) {\
        JanetType t = janet_type((A).v[(N)]);\
        if (!((1 << t) & (TS))) return janet_typemany_err(A, N, TS);\
    } else {\
       if (!((TS) & JANET_NIL)) return janet_typemany_err(A, N, TS);\
    }\
} while (0)

#define JANET_CHECKABSTRACT(A, N, AT) do {\
    if ((A).n > (N)) {\
        Janet x = (A).v[(N)];\
        if (!janet_checktype(x, JANET_ABSTRACT) ||\
                janet_abstract_type(janet_unwrap_abstract(x)) != (AT))\
            return janet_typeabstract_err(A, N, AT);\
    } else {\
        return janet_typeabstract_err(A, N, AT);\
    }\
} while (0)

#define JANET_ARG_NUMBER(DEST, A, N) do { \
    if ((A).n <= (N)) \
        return janet_typemany_err(A, N, JANET_TFLAG_NUMBER);\
    Janet val = (A).v[(N)];\
    if (janet_checktype(val, JANET_REAL)) { \
        DEST = janet_unwrap_real(val); \
    } else if (janet_checktype(val, JANET_INTEGER)) {\
        DEST = (double) janet_unwrap_integer(val);\
    }\
    else return janet_typemany_err(A, N, JANET_TFLAG_NUMBER); \
} while (0)

#define JANET_ARG_BOOLEAN(DEST, A, N) do { \
    JANET_CHECKMANY(A, N, JANET_TFLAG_TRUE | JANET_TFLAG_FALSE);\
    DEST = janet_unwrap_boolean((A).v[(N)]); \
} while (0)

#define JANET_ARG_BYTES(DESTBYTES, DESTLEN, A, N) do {\
    if ((A).n <= (N)) return janet_typemany_err(A, N, JANET_TFLAG_BYTES);\
    if (!janet_bytes_view((A).v[(N)], &(DESTBYTES), &(DESTLEN))) {\
        return janet_typemany_err(A, N, JANET_TFLAG_BYTES);\
    }\
} while (0)

#define JANET_ARG_INDEXED(DESTVALS, DESTLEN, A, N) do {\
    if ((A).n <= (N)) return janet_typemany_err(A, N, JANET_TFLAG_INDEXED);\
    if (!janet_indexed_view((A).v[(N)], &(DESTVALS), &(DESTLEN))) {\
        return janet_typemany_err(A, N, JANET_TFLAG_INDEXED);\
    }\
} while (0)

#define _JANET_ARG(TYPE, NAME, DEST, A, N) do { \
    JANET_CHECK(A, N, TYPE);\
    DEST = janet_unwrap_##NAME((A).v[(N)]); \
} while (0)

#define JANET_ARG_FIBER(DEST, A, N) _JANET_ARG(JANET_FIBER, fiber, DEST, A, N)
#define JANET_ARG_INTEGER(DEST, A, N) _JANET_ARG(JANET_INTEGER, integer, DEST, A, N)
#define JANET_ARG_REAL(DEST, A, N) _JANET_ARG(JANET_REAL, real, DEST, A, N)
#define JANET_ARG_STRING(DEST, A, N) _JANET_ARG(JANET_STRING, string, DEST, A, N)
#define JANET_ARG_SYMBOL(DEST, A, N) _JANET_ARG(JANET_SYMBOL, symbol, DEST, A, N)
#define JANET_ARG_ARRAY(DEST, A, N) _JANET_ARG(JANET_ARRAY, array, DEST, A, N)
#define JANET_ARG_TUPLE(DEST, A, N) _JANET_ARG(JANET_TUPLE, tuple, DEST, A, N)
#define JANET_ARG_TABLE(DEST, A, N) _JANET_ARG(JANET_TABLE, table, DEST, A, N)
#define JANET_ARG_STRUCT(DEST, A, N) _JANET_ARG(JANET_STRUCT, struct, DEST, A, N)
#define JANET_ARG_BUFFER(DEST, A, N) _JANET_ARG(JANET_BUFFER, buffer, DEST, A, N)
#define JANET_ARG_FUNCTION(DEST, A, N) _JANET_ARG(JANET_FUNCTION, function, DEST, A, N)
#define JANET_ARG_CFUNCTION(DEST, A, N) _JANET_ARG(JANET_CFUNCTION, cfunction, DEST, A, N)

#define JANET_ARG_ABSTRACT(DEST, A, N, AT) do { \
    JANET_CHECKABSTRACT(A, N, AT); \
    DEST = janet_unwrap_abstract((A).v[(N)]); \
} while (0)

#define JANET_RETURN_NIL(A) do { return JANET_SIGNAL_OK; } while (0)
#define JANET_RETURN_FALSE(A) JANET_RETURN(A, janet_wrap_false())
#define JANET_RETURN_TRUE(A) JANET_RETURN(A, janet_wrap_true())
#define JANET_RETURN_BOOLEAN(A, X) JANET_RETURN(A, janet_wrap_boolean(X))
#define JANET_RETURN_FIBER(A, X) JANET_RETURN(A, janet_wrap_fiber(X))
#define JANET_RETURN_INTEGER(A, X) JANET_RETURN(A, janet_wrap_integer(X))
#define JANET_RETURN_REAL(A, X) JANET_RETURN(A, janet_wrap_real(X))
#define JANET_RETURN_STRING(A, X) JANET_RETURN(A, janet_wrap_string(X))
#define JANET_RETURN_SYMBOL(A, X) JANET_RETURN(A, janet_wrap_symbol(X))
#define JANET_RETURN_ARRAY(A, X) JANET_RETURN(A, janet_wrap_array(X))
#define JANET_RETURN_TUPLE(A, X) JANET_RETURN(A, janet_wrap_tuple(X))
#define JANET_RETURN_TABLE(A, X) JANET_RETURN(A, janet_wrap_table(X))
#define JANET_RETURN_STRUCT(A, X) JANET_RETURN(A, janet_wrap_struct(X))
#define JANET_RETURN_BUFFER(A, X) JANET_RETURN(A, janet_wrap_buffer(X))
#define JANET_RETURN_FUNCTION(A, X) JANET_RETURN(A, janet_wrap_function(X))
#define JANET_RETURN_CFUNCTION(A, X) JANET_RETURN(A, janet_wrap_cfunction(X))
#define JANET_RETURN_ABSTRACT(A, X) JANET_RETURN(A, janet_wrap_abstract(X))

#define JANET_RETURN_CSTRING(A, X) JANET_RETURN(A, janet_cstringv(X))
#define JANET_RETURN_CSYMBOL(A, X) JANET_RETURN(A, janet_csymbolv(X))

/**** END SECTION MACROS *****/

#ifdef __cplusplus
}
#endif

#endif /* JANET_H_defined */
