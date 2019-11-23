/*
* Copyright (c) 2019 Calvin Rose
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

#ifndef JANET_AMALG
#include <janet.h>
#include "util.h"
#endif

/* Check if a character is whitespace */
static int is_whitespace(uint8_t c) {
    return c == ' '
           || c == '\t'
           || c == '\n'
           || c == '\r'
           || c == '\0'
           || c == '\v'
           || c == '\f';
}

/* Code generated by tools/symcharsgen.c.
 * The table contains 256 bits, where each bit is 1
 * if the corresponding ascii code is a symbol char, and 0
 * if not. The upper characters are also considered symbol
 * chars and are then checked for utf-8 compliance. */
static const uint32_t symchars[8] = {
    0x00000000, 0xf7ffec72, 0xc7ffffff, 0x07fffffe,
    0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff
};

/* Check if a character is a valid symbol character
 * symbol chars are A-Z, a-z, 0-9, or one of !$&*+-./:<=>@\^_~| */
static int is_symbol_char(uint8_t c) {
    return symchars[c >> 5] & ((uint32_t)1 << (c & 0x1F));
}

/* Validate some utf8. Useful for identifiers. Only validates
 * the encoding, does not check for valid code points (they
 * are less well defined than the encoding). */
static int valid_utf8(const uint8_t *str, int32_t len) {
    int32_t i = 0;
    int32_t j;
    while (i < len) {
        int32_t nexti;
        uint8_t c = str[i];

        /* Check the number of bytes in code point */
        if (c < 0x80) nexti = i + 1;
        else if ((c >> 5) == 0x06) nexti = i + 2;
        else if ((c >> 4) == 0x0E) nexti = i + 3;
        else if ((c >> 3) == 0x1E) nexti = i + 4;
        /* Don't allow 5 or 6 byte code points */
        else return 0;

        /* No overflow */
        if (nexti > len) return 0;

        /* Ensure trailing bytes are well formed (10XX XXXX) */
        for (j = i + 1; j < nexti; j++) {
            if ((str[j] >> 6) != 2) return 0;
        }

        /* Check for overlong encoding */
        if ((nexti == i + 2) && str[i] < 0xC2) return 0;
        if ((str[i] == 0xE0) && str[i + 1] < 0xA0) return 0;
        if ((str[i] == 0xF0) && str[i + 1] < 0x90) return 0;

        i = nexti;
    }
    return 1;
}

/* Get hex digit from a letter */
static int to_hex(uint8_t c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'A' && c <= 'F') {
        return 10 + c - 'A';
    } else if (c >= 'a' && c <= 'f') {
        return 10 + c - 'a';
    } else {
        return -1;
    }
}

typedef int (*Consumer)(JanetParser *p, JanetParseState *state, uint8_t c);
struct JanetParseState {
    int32_t counter;
    int32_t argn;
    int flags;
    size_t line;
    size_t column;
    Consumer consumer;
};

/* Define a stack on the main parser struct */
#define DEF_PARSER_STACK(NAME, T, STACK, STACKCOUNT, STACKCAP) \
static void NAME(JanetParser *p, T x) { \
    size_t oldcount = p->STACKCOUNT; \
    size_t newcount = oldcount + 1; \
    if (newcount > p->STACKCAP) { \
        T *next; \
        size_t newcap = 2 * newcount; \
        next = realloc(p->STACK, sizeof(T) * newcap); \
        if (NULL == next) { \
            JANET_OUT_OF_MEMORY; \
        } \
        p->STACK = next; \
        p->STACKCAP = newcap; \
    } \
    p->STACK[oldcount] = x; \
    p->STACKCOUNT = newcount; \
}

DEF_PARSER_STACK(push_buf, uint8_t, buf, bufcount, bufcap)
DEF_PARSER_STACK(push_arg, Janet, args, argcount, argcap)
DEF_PARSER_STACK(_pushstate, JanetParseState, states, statecount, statecap)

#undef DEF_PARSER_STACK

#define PFLAG_CONTAINER 0x100
#define PFLAG_BUFFER 0x200
#define PFLAG_PARENS 0x400
#define PFLAG_SQRBRACKETS 0x800
#define PFLAG_CURLYBRACKETS 0x1000
#define PFLAG_STRING 0x2000
#define PFLAG_LONGSTRING 0x4000
#define PFLAG_READERMAC 0x8000
#define PFLAG_ATSYM 0x10000
#define PFLAG_COMMENT 0x20000
#define PFLAG_TOKEN 0x40000

static void pushstate(JanetParser *p, Consumer consumer, int flags) {
    JanetParseState s;
    s.counter = 0;
    s.argn = 0;
    s.flags = flags;
    s.consumer = consumer;
    s.line = p->line;
    s.column = p->column;
    _pushstate(p, s);
}

static void popstate(JanetParser *p, Janet val) {
    for (;;) {
        JanetParseState top = p->states[--p->statecount];
        JanetParseState *newtop = p->states + p->statecount - 1;
        if (newtop->flags & PFLAG_CONTAINER) {
            /* Source mapping info */
            if (janet_checktype(val, JANET_TUPLE)) {
                janet_tuple_sm_line(janet_unwrap_tuple(val)) = (int32_t) top.line;
                janet_tuple_sm_column(janet_unwrap_tuple(val)) = (int32_t) top.column;
            }
            newtop->argn++;
            /* Keep track of number of values in the root state */
            if (p->statecount == 1) p->pending++;
            push_arg(p, val);
            return;
        } else if (newtop->flags & PFLAG_READERMAC) {
            Janet *t = janet_tuple_begin(2);
            int c = newtop->flags & 0xFF;
            const char *which =
                (c == '\'') ? "quote" :
                (c == ',') ? "unquote" :
                (c == ';') ? "splice" :
                (c == '|') ? "short-fn" :
                (c == '~') ? "quasiquote" : "<unknown>";
            t[0] = janet_csymbolv(which);
            t[1] = val;
            /* Quote source mapping info */
            janet_tuple_sm_line(t) = (int32_t) newtop->line;
            janet_tuple_sm_column(t) = (int32_t) newtop->column;
            val = janet_wrap_tuple(janet_tuple_end(t));
        } else {
            return;
        }
    }
}

static int checkescape(uint8_t c) {
    switch (c) {
        default:
            return -1;
        case 'x':
            return 1;
        case 'n':
            return '\n';
        case 't':
            return '\t';
        case 'r':
            return '\r';
        case '0':
            return '\0';
        case 'z':
            return '\0';
        case 'f':
            return '\f';
        case 'v':
            return '\v';
        case 'e':
            return 27;
        case '"':
            return '"';
        case '\\':
            return '\\';
    }
}

/* Forward declare */
static int stringchar(JanetParser *p, JanetParseState *state, uint8_t c);

static int escapeh(JanetParser *p, JanetParseState *state, uint8_t c) {
    int digit = to_hex(c);
    if (digit < 0) {
        p->error = "invalid hex digit in hex escape";
        return 1;
    }
    state->argn = (state->argn << 4) + digit;
    state->counter--;
    if (!state->counter) {
        push_buf(p, (state->argn & 0xFF));
        state->argn = 0;
        state->consumer = stringchar;
    }
    return 1;
}

static int escape1(JanetParser *p, JanetParseState *state, uint8_t c) {
    int e = checkescape(c);
    if (e < 0) {
        p->error = "invalid string escape sequence";
        return 1;
    }
    if (c == 'x') {
        state->counter = 2;
        state->argn = 0;
        state->consumer = escapeh;
    } else {
        push_buf(p, (uint8_t) e);
        state->consumer = stringchar;
    }
    return 1;
}

static int stringend(JanetParser *p, JanetParseState *state) {
    Janet ret;
    uint8_t *bufstart = p->buf;
    int32_t buflen = (int32_t) p->bufcount;
    if (state->flags & PFLAG_LONGSTRING) {
        /* Check for leading newline character so we can remove it */
        if (bufstart[0] == '\n') {
            bufstart++;
            buflen--;
        }
        if (buflen > 0 && bufstart[buflen - 1] == '\n') {
            buflen--;
        }
    }
    if (state->flags & PFLAG_BUFFER) {
        JanetBuffer *b = janet_buffer(buflen);
        janet_buffer_push_bytes(b, bufstart, buflen);
        ret = janet_wrap_buffer(b);
    } else {
        ret = janet_wrap_string(janet_string(bufstart, buflen));
    }
    p->bufcount = 0;
    popstate(p, ret);
    return 1;
}

static int stringchar(JanetParser *p, JanetParseState *state, uint8_t c) {
    /* Enter escape */
    if (c == '\\') {
        state->consumer = escape1;
        return 1;
    }
    /* String end */
    if (c == '"') {
        return stringend(p, state);
    }
    /* normal char */
    if (c != '\n' && c != '\r')
        push_buf(p, c);
    return 1;
}

/* Check for string equality in the buffer */
static int check_str_const(const char *cstr, const uint8_t *str, int32_t len) {
    int32_t index;
    for (index = 0; index < len; index++) {
        uint8_t c = str[index];
        uint8_t k = ((const uint8_t *)cstr)[index];
        if (c < k) return -1;
        if (c > k) return 1;
        if (k == '\0') break;
    }
    return (cstr[index] == '\0') ? 0 : -1;
}

static int tokenchar(JanetParser *p, JanetParseState *state, uint8_t c) {
    Janet ret;
    double numval;
    int32_t blen;
    if (is_symbol_char(c)) {
        push_buf(p, (uint8_t) c);
        if (c > 127) state->argn = 1; /* Use to indicate non ascii */
        return 1;
    }
    /* Token finished */
    blen = (int32_t) p->bufcount;
    int start_dig = p->buf[0] >= '0' && p->buf[0] <= '9';
    int start_num = start_dig || p->buf[0] == '-' || p->buf[0] == '+' || p->buf[0] == '.';
    if (p->buf[0] == ':') {
        /* Don't do full utf-8 check unless we have seen non ascii characters. */
        int valid = (!state->argn) || valid_utf8(p->buf + 1, blen - 1);
        if (!valid) {
            p->error = "invalid utf-8 in keyword";
            return 0;
        }
        ret = janet_keywordv(p->buf + 1, blen - 1);
    } else if (start_num && !janet_scan_number(p->buf, blen, &numval)) {
        ret = janet_wrap_number(numval);
    } else if (!check_str_const("nil", p->buf, blen)) {
        ret = janet_wrap_nil();
    } else if (!check_str_const("false", p->buf, blen)) {
        ret = janet_wrap_false();
    } else if (!check_str_const("true", p->buf, blen)) {
        ret = janet_wrap_true();
    } else {
        if (start_dig) {
            p->error = "symbol literal cannot start with a digit";
            return 0;
        } else {
            /* Don't do full utf-8 check unless we have seen non ascii characters. */
            int valid = (!state->argn) || valid_utf8(p->buf, blen);
            if (!valid) {
                p->error = "invalid utf-8 in symbol";
                return 0;
            }
            ret = janet_symbolv(p->buf, blen);
        }
    }
    p->bufcount = 0;
    popstate(p, ret);
    return 0;
}

static int comment(JanetParser *p, JanetParseState *state, uint8_t c) {
    (void) state;
    if (c == '\n') {
        p->statecount--;
        p->bufcount = 0;
    } else {
        push_buf(p, c);
    }
    return 1;
}

static Janet close_tuple(JanetParser *p, JanetParseState *state, int32_t flag) {
    Janet *ret = janet_tuple_begin(state->argn);
    janet_tuple_flag(ret) |= flag;
    for (int32_t i = state->argn - 1; i >= 0; i--)
        ret[i] = p->args[--p->argcount];
    return janet_wrap_tuple(janet_tuple_end(ret));
}

static Janet close_array(JanetParser *p, JanetParseState *state) {
    JanetArray *array = janet_array(state->argn);
    for (int32_t i = state->argn - 1; i >= 0; i--)
        array->data[i] = p->args[--p->argcount];
    array->count = state->argn;
    return janet_wrap_array(array);
}

static Janet close_struct(JanetParser *p, JanetParseState *state) {
    JanetKV *st = janet_struct_begin(state->argn >> 1);
    for (int32_t i = state->argn; i > 0; i -= 2) {
        Janet value = p->args[--p->argcount];
        Janet key = p->args[--p->argcount];
        janet_struct_put(st, key, value);
    }
    return janet_wrap_struct(janet_struct_end(st));
}

static Janet close_table(JanetParser *p, JanetParseState *state) {
    JanetTable *table = janet_table(state->argn >> 1);
    for (int32_t i = state->argn; i > 0; i -= 2) {
        Janet value = p->args[--p->argcount];
        Janet key = p->args[--p->argcount];
        janet_table_put(table, key, value);
    }
    return janet_wrap_table(table);
}

#define PFLAG_INSTRING 0x100000
#define PFLAG_END_CANDIDATE 0x200000
static int longstring(JanetParser *p, JanetParseState *state, uint8_t c) {
    if (state->flags & PFLAG_INSTRING) {
        /* We are inside the long string */
        if (c == '`') {
            state->flags |= PFLAG_END_CANDIDATE;
            state->flags &= ~PFLAG_INSTRING;
            state->counter = 1; /* Use counter to keep track of number of '=' seen */
            return 1;
        }
        push_buf(p, c);
        return 1;
    } else if (state->flags & PFLAG_END_CANDIDATE) {
        int i;
        /* We are checking a potential end of the string */
        if (state->counter == state->argn) {
            stringend(p, state);
            return 0;
        }
        if (c == '`' && state->counter < state->argn) {
            state->counter++;
            return 1;
        }
        /* Failed end candidate */
        for (i = 0; i < state->counter; i++) {
            push_buf(p, '`');
        }
        push_buf(p, c);
        state->counter = 0;
        state->flags &= ~PFLAG_END_CANDIDATE;
        state->flags |= PFLAG_INSTRING;
        return 1;
    } else {
        /* We are at beginning of string */
        state->argn++;
        if (c != '`') {
            state->flags |= PFLAG_INSTRING;
            push_buf(p, c);
        }
        return 1;
    }
}

static int root(JanetParser *p, JanetParseState *state, uint8_t c);

static int atsign(JanetParser *p, JanetParseState *state, uint8_t c) {
    (void) state;
    p->statecount--;
    switch (c) {
        case '{':
            pushstate(p, root, PFLAG_CONTAINER | PFLAG_CURLYBRACKETS | PFLAG_ATSYM);
            return 1;
        case '"':
            pushstate(p, stringchar, PFLAG_BUFFER | PFLAG_STRING);
            return 1;
        case '`':
            pushstate(p, longstring, PFLAG_BUFFER | PFLAG_LONGSTRING);
            return 1;
        case '[':
            pushstate(p, root, PFLAG_CONTAINER | PFLAG_SQRBRACKETS | PFLAG_ATSYM);
            return 1;
        case '(':
            pushstate(p, root, PFLAG_CONTAINER | PFLAG_PARENS | PFLAG_ATSYM);
            return 1;
        default:
            break;
    }
    pushstate(p, tokenchar, PFLAG_TOKEN);
    push_buf(p, '@'); /* Push the leading at-sign that was dropped */
    return 0;
}

/* The root state of the parser */
static int root(JanetParser *p, JanetParseState *state, uint8_t c) {
    switch (c) {
        default:
            if (is_whitespace(c)) return 1;
            if (!is_symbol_char(c)) {
                p->error = "unexpected character";
                return 1;
            }
            pushstate(p, tokenchar, PFLAG_TOKEN);
            return 0;
        case '\'':
        case ',':
        case ';':
        case '~':
        case '|':
            pushstate(p, root, PFLAG_READERMAC | c);
            return 1;
        case '"':
            pushstate(p, stringchar, PFLAG_STRING);
            return 1;
        case '#':
            pushstate(p, comment, PFLAG_COMMENT);
            return 1;
        case '@':
            pushstate(p, atsign, PFLAG_ATSYM);
            return 1;
        case '`':
            pushstate(p, longstring, PFLAG_LONGSTRING);
            return 1;
        case ')':
        case ']':
        case '}': {
            Janet ds;
            if (p->statecount == 1) {
                p->error = "unexpected delimiter";
                return 1;
            }
            if ((c == ')' && (state->flags & PFLAG_PARENS)) ||
                    (c == ']' && (state->flags & PFLAG_SQRBRACKETS))) {
                if (state->flags & PFLAG_ATSYM) {
                    ds = close_array(p, state);
                } else {
                    ds = close_tuple(p, state, c == ']' ? JANET_TUPLE_FLAG_BRACKETCTOR : 0);
                }
            } else if (c == '}' && (state->flags & PFLAG_CURLYBRACKETS)) {
                if (state->argn & 1) {
                    p->error = "struct and table literals expect even number of arguments";
                    return 1;
                }
                if (state->flags & PFLAG_ATSYM) {
                    ds = close_table(p, state);
                } else {
                    ds = close_struct(p, state);
                }
            } else {
                p->error = "mismatched delimiter";
                return 1;
            }
            popstate(p, ds);
        }
        return 1;
        case '(':
            pushstate(p, root, PFLAG_CONTAINER | PFLAG_PARENS);
            return 1;
        case '[':
            pushstate(p, root, PFLAG_CONTAINER | PFLAG_SQRBRACKETS);
            return 1;
        case '{':
            pushstate(p, root, PFLAG_CONTAINER | PFLAG_CURLYBRACKETS);
            return 1;
    }
}

static void janet_parser_checkdead(JanetParser *parser) {
    if (parser->flag) janet_panic("parser is dead, cannot consume");
    if (parser->error) janet_panic("parser has unchecked error, cannot consume");
}

/* Public API */

void janet_parser_consume(JanetParser *parser, uint8_t c) {
    int consumed = 0;
    janet_parser_checkdead(parser);
    if (c == '\r') {
        parser->line++;
        parser->column = 0;
    } else if (c == '\n') {
        parser->column = 0;
        if (parser->lookback != '\r')
            parser->line++;
    } else {
        parser->column++;
    }
    while (!consumed && !parser->error) {
        JanetParseState *state = parser->states + parser->statecount - 1;
        consumed = state->consumer(parser, state, c);
    }
    parser->lookback = c;
}

void janet_parser_eof(JanetParser *parser) {
    janet_parser_checkdead(parser);
    size_t oldcolumn = parser->column;
    size_t oldline = parser->line;
    janet_parser_consume(parser, '\n');
    if (parser->statecount > 1) {
        parser->error = "unexpected end of source";
    }
    parser->line = oldline;
    parser->column = oldcolumn;
    parser->flag = 1;
}

enum JanetParserStatus janet_parser_status(JanetParser *parser) {
    if (parser->error) return JANET_PARSE_ERROR;
    if (parser->flag) return JANET_PARSE_DEAD;
    if (parser->statecount > 1) return JANET_PARSE_PENDING;
    return JANET_PARSE_ROOT;
}

void janet_parser_flush(JanetParser *parser) {
    parser->argcount = 0;
    parser->statecount = 1;
    parser->bufcount = 0;
    parser->pending = 0;
}

const char *janet_parser_error(JanetParser *parser) {
    enum JanetParserStatus status = janet_parser_status(parser);
    if (status == JANET_PARSE_ERROR) {
        const char *e = parser->error;
        parser->error = NULL;
        janet_parser_flush(parser);
        return e;
    }
    return NULL;
}

Janet janet_parser_produce(JanetParser *parser) {
    Janet ret;
    size_t i;
    if (parser->pending == 0) return janet_wrap_nil();
    ret = parser->args[0];
    for (i = 1; i < parser->argcount; i++) {
        parser->args[i - 1] = parser->args[i];
    }
    parser->pending--;
    parser->argcount--;
    return ret;
}

void janet_parser_init(JanetParser *parser) {
    parser->args = NULL;
    parser->states = NULL;
    parser->buf = NULL;
    parser->argcount = 0;
    parser->argcap = 0;
    parser->bufcount = 0;
    parser->bufcap = 0;
    parser->statecount = 0;
    parser->statecap = 0;
    parser->error = NULL;
    parser->lookback = -1;
    parser->line = 1;
    parser->column = 0;
    parser->pending = 0;
    parser->flag = 0;

    pushstate(parser, root, PFLAG_CONTAINER);
}

void janet_parser_deinit(JanetParser *parser) {
    free(parser->args);
    free(parser->buf);
    free(parser->states);
}

void janet_parser_clone(const JanetParser *src, JanetParser *dest) {
    /* Misc fields */
    dest->flag = src->flag;
    dest->pending = src->pending;
    dest->lookback = src->lookback;
    dest->line = src->line;
    dest->column = src->column;
    dest->error = src->error;

    /* Keep counts */
    dest->argcount = src->argcount;
    dest->bufcount = src->bufcount;
    dest->statecount = src->statecount;

    /* Capacities are equal to counts */
    dest->bufcap = dest->bufcount;
    dest->statecap = dest->statecount;
    dest->argcap = dest->argcount;

    /* Deep cloned fields */
    dest->args = NULL;
    dest->states = NULL;
    dest->buf = NULL;
    if (dest->bufcap) {
        dest->buf = malloc(dest->bufcap);
        if (!dest->buf) goto nomem;
    }
    if (dest->argcap) {
        dest->args = malloc(sizeof(Janet) * dest->argcap);
        if (!dest->args) goto nomem;
    }
    if (dest->statecap) {
        dest->states = malloc(sizeof(JanetParseState) * dest->statecap);
        if (!dest->states) goto nomem;
    }

    memcpy(dest->buf, src->buf, dest->bufcap);
    memcpy(dest->args, src->args, dest->argcap * sizeof(Janet));
    memcpy(dest->states, src->states, dest->statecap * sizeof(JanetParseState));

    return;

nomem:
    JANET_OUT_OF_MEMORY;
}

int janet_parser_has_more(JanetParser *parser) {
    return !!parser->pending;
}

/* C functions */

static int parsermark(void *p, size_t size) {
    size_t i;
    JanetParser *parser = (JanetParser *)p;
    (void) size;
    for (i = 0; i < parser->argcount; i++) {
        janet_mark(parser->args[i]);
    }
    return 0;
}

static int parsergc(void *p, size_t size) {
    JanetParser *parser = (JanetParser *)p;
    (void) size;
    janet_parser_deinit(parser);
    return 0;
}

static Janet parserget(void *p, Janet key);

static JanetAbstractType janet_parse_parsertype = {
    "core/parser",
    parsergc,
    parsermark,
    parserget,
    NULL,
    NULL,
    NULL,
    NULL
};

/* C Function parser */
static Janet cfun_parse_parser(int32_t argc, Janet *argv) {
    (void) argv;
    janet_fixarity(argc, 0);
    JanetParser *p = janet_abstract(&janet_parse_parsertype, sizeof(JanetParser));
    janet_parser_init(p);
    return janet_wrap_abstract(p);
}

static Janet cfun_parse_consume(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
    JanetParser *p = janet_getabstract(argv, 0, &janet_parse_parsertype);
    JanetByteView view = janet_getbytes(argv, 1);
    if (argc == 3) {
        int32_t offset = janet_getinteger(argv, 2);
        if (offset < 0 || offset > view.len)
            janet_panicf("invalid offset %d out of range [0,%d]", offset, view.len);
        view.len -= offset;
        view.bytes += offset;
    }
    int32_t i;
    for (i = 0; i < view.len; i++) {
        janet_parser_consume(p, view.bytes[i]);
        switch (janet_parser_status(p)) {
            case JANET_PARSE_ROOT:
            case JANET_PARSE_PENDING:
                break;
            default:
                return janet_wrap_integer(i + 1);
        }
    }
    return janet_wrap_integer(i);
}

static Janet cfun_parse_eof(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetParser *p = janet_getabstract(argv, 0, &janet_parse_parsertype);
    janet_parser_eof(p);
    return argv[0];
}

static Janet cfun_parse_insert(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetParser *p = janet_getabstract(argv, 0, &janet_parse_parsertype);
    JanetParseState *s = p->states + p->statecount - 1;
    if (s->consumer == tokenchar) {
        janet_parser_consume(p, ' ');
        p->column--;
        s = p->states + p->statecount - 1;
    }
    if (s->flags & PFLAG_CONTAINER) {
        s->argn++;
        if (p->statecount == 1) p->pending++;
        push_arg(p, argv[1]);
    } else if (s->flags & (PFLAG_STRING | PFLAG_LONGSTRING)) {
        const uint8_t *str = janet_to_string(argv[1]);
        int32_t slen = janet_string_length(str);
        size_t newcount = p->bufcount + slen;
        if (p->bufcap < newcount) {
            size_t newcap = 2 * newcount;
            p->buf = realloc(p->buf, newcap);
            if (p->buf == NULL) {
                JANET_OUT_OF_MEMORY;
            }
            p->bufcap = newcap;
        }
        memcpy(p->buf + p->bufcount, str, slen);
        p->bufcount = newcount;
    } else {
        janet_panic("cannot insert value into parser");
    }
    return argv[0];
}

static Janet cfun_parse_has_more(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetParser *p = janet_getabstract(argv, 0, &janet_parse_parsertype);
    return janet_wrap_boolean(janet_parser_has_more(p));
}

static Janet cfun_parse_byte(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetParser *p = janet_getabstract(argv, 0, &janet_parse_parsertype);
    int32_t i = janet_getinteger(argv, 1);
    janet_parser_consume(p, 0xFF & i);
    return argv[0];
}

static Janet cfun_parse_status(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetParser *p = janet_getabstract(argv, 0, &janet_parse_parsertype);
    const char *stat = NULL;
    switch (janet_parser_status(p)) {
        case JANET_PARSE_PENDING:
            stat = "pending";
            break;
        case JANET_PARSE_ERROR:
            stat = "error";
            break;
        case JANET_PARSE_ROOT:
            stat = "root";
            break;
        case JANET_PARSE_DEAD:
            stat = "dead";
            break;
    }
    return janet_ckeywordv(stat);
}

static Janet cfun_parse_error(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetParser *p = janet_getabstract(argv, 0, &janet_parse_parsertype);
    const char *err = janet_parser_error(p);
    if (err) return janet_cstringv(err);
    return janet_wrap_nil();
}

static Janet cfun_parse_produce(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetParser *p = janet_getabstract(argv, 0, &janet_parse_parsertype);
    return janet_parser_produce(p);
}

static Janet cfun_parse_flush(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetParser *p = janet_getabstract(argv, 0, &janet_parse_parsertype);
    janet_parser_flush(p);
    return argv[0];
}

static Janet cfun_parse_where(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetParser *p = janet_getabstract(argv, 0, &janet_parse_parsertype);
    Janet *tup = janet_tuple_begin(2);
    tup[0] = janet_wrap_integer(p->line);
    tup[1] = janet_wrap_integer(p->column);
    return janet_wrap_tuple(janet_tuple_end(tup));
}

static Janet janet_wrap_parse_state(JanetParseState *s, Janet *args,
                                    uint8_t *buff, uint32_t bufcount) {
    JanetTable *state = janet_table(0);
    const uint8_t *buffer;
    int add_buffer = 0;
    const char *type = NULL;

    if (s->flags & PFLAG_CONTAINER) {
        JanetArray *container_args = janet_array(s->argn);
        container_args->count = s->argn;
        memcpy(container_args->data, args, sizeof(args[0])*s->argn);
        janet_table_put(state, janet_ckeywordv("args"),
                        janet_wrap_array(container_args));
    }

    if (s->flags & PFLAG_PARENS || s->flags & PFLAG_SQRBRACKETS) {
        if (s->flags & PFLAG_ATSYM) {
            type = "array";
        } else {
            type = "tuple";
        }
    } else if (s->flags & PFLAG_CURLYBRACKETS) {
        if (s->flags & PFLAG_ATSYM) {
            type = "table";
        } else {
            type = "struct";
        }
    } else if (s->flags & PFLAG_STRING || s->flags & PFLAG_LONGSTRING) {
        if (s->flags & PFLAG_BUFFER) {
            type = "buffer";
        } else {
            type = "string";
        }
        add_buffer = 1;
    } else if (s->flags & PFLAG_COMMENT) {
        type = "comment";
        add_buffer = 1;
    } else if (s->flags & PFLAG_TOKEN) {
        type = "token";
        add_buffer = 1;
    } else if (s->flags & PFLAG_ATSYM) {
        type = "at";
    } else if (s->flags & PFLAG_READERMAC) {
        int c = s->flags & 0xFF;
        type = (c == '\'') ? "quote" :
               (c == ',') ? "unquote" :
               (c == ';') ? "splice" :
               (c == '~') ? "quasiquote" : "<reader>";
    } else {
        type = "root";
    }

    if (type) {
        janet_table_put(state, janet_ckeywordv("type"),
                        janet_ckeywordv(type));
    }

    if (add_buffer) {
        buffer = janet_string(buff, bufcount);
        janet_table_put(state, janet_ckeywordv("buffer"), janet_wrap_string(buffer));
    }

    janet_table_put(state, janet_ckeywordv("line"), janet_wrap_integer(s->line));
    janet_table_put(state, janet_ckeywordv("column"), janet_wrap_integer(s->column));
    return janet_wrap_table(state);
}

struct ParserStateGetter {
    const char *name;
    Janet(*fn)(const JanetParser *p);
};

static Janet parser_state_delimiters(const JanetParser *_p) {
    JanetParser *clone = janet_abstract(&janet_parse_parsertype, sizeof(JanetParser));
    janet_parser_clone(_p, clone);
    size_t i;
    const uint8_t *str;
    size_t oldcount;
    oldcount = clone->bufcount;
    for (i = 0; i < clone->statecount; i++) {
        JanetParseState *s = clone->states + i;
        if (s->flags & PFLAG_PARENS) {
            push_buf(clone, '(');
        } else if (s->flags & PFLAG_SQRBRACKETS) {
            push_buf(clone, '[');
        } else if (s->flags & PFLAG_CURLYBRACKETS) {
            push_buf(clone, '{');
        } else if (s->flags & PFLAG_STRING) {
            push_buf(clone, '"');
        } else if (s->flags & PFLAG_LONGSTRING) {
            int32_t i;
            for (i = 0; i < s->argn; i++) {
                push_buf(clone, '`');
            }
        }
    }
    str = janet_string(clone->buf + oldcount, (int32_t)(clone->bufcount - oldcount));
    clone->bufcount = oldcount;
    return janet_wrap_string(str);
}

static Janet parser_state_frames(const JanetParser *p) {
    int32_t count = (int32_t) p->statecount;
    JanetArray *states = janet_array(count);
    states->count = count;
    uint8_t *buf = p->buf;
    Janet *args = p->args;
    for (int32_t i = count - 1; i >= 0; --i) {
        JanetParseState *s = p->states + i;
        states->data[i] = janet_wrap_parse_state(s, args, buf, (uint32_t) p->bufcount);
        args -= s->argn;
    }
    return janet_wrap_array(states);
}

static const struct ParserStateGetter parser_state_getters[] = {
    {"frames", parser_state_frames},
    {"delimiters", parser_state_delimiters},
    {NULL, NULL}
};

static Janet cfun_parse_state(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    const uint8_t *key = NULL;
    JanetParser *p = janet_getabstract(argv, 0, &janet_parse_parsertype);
    if (argc == 2) {
        key = janet_getkeyword(argv, 1);
    }

    if (key) {
        /* Get one result */
        for (const struct ParserStateGetter *sg = parser_state_getters;
                sg->name != NULL; sg++) {
            if (janet_cstrcmp(key, sg->name)) continue;
            return sg->fn(p);
        }
        janet_panicf("unexpected keyword %v", janet_wrap_keyword(key));
        return janet_wrap_nil();
    } else {
        /* Put results in table */
        JanetTable *tab = janet_table(0);
        for (const struct ParserStateGetter *sg = parser_state_getters;
                sg->name != NULL; sg++) {
            janet_table_put(tab, janet_ckeywordv(sg->name), sg->fn(p));
        }
        return janet_wrap_table(tab);
    }
}

static Janet cfun_parse_clone(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetParser *src = janet_getabstract(argv, 0, &janet_parse_parsertype);
    JanetParser *dest = janet_abstract(&janet_parse_parsertype, sizeof(JanetParser));
    janet_parser_clone(src, dest);
    return janet_wrap_abstract(dest);
}

static const JanetMethod parser_methods[] = {
    {"byte", cfun_parse_byte},
    {"clone", cfun_parse_clone},
    {"consume", cfun_parse_consume},
    {"eof", cfun_parse_eof},
    {"error", cfun_parse_error},
    {"flush", cfun_parse_flush},
    {"has-more", cfun_parse_has_more},
    {"insert", cfun_parse_insert},
    {"produce", cfun_parse_produce},
    {"state", cfun_parse_state},
    {"status", cfun_parse_status},
    {"where", cfun_parse_where},
    {NULL, NULL}
};

static Janet parserget(void *p, Janet key) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD)) janet_panicf("expected keyword method");
    return janet_getmethod(janet_unwrap_keyword(key), parser_methods);
}

static const JanetReg parse_cfuns[] = {
    {
        "parser/new", cfun_parse_parser,
        JDOC("(parser/new)\n\n"
             "Creates and returns a new parser object. Parsers are state machines "
             "that can receive bytes, and generate a stream of janet values.")
    },
    {
        "parser/clone", cfun_parse_clone,
        JDOC("(parser/clone p)\n\n"
             "Creates a deep clone of a parser that is identical to the input parser. "
             "This cloned parser can be used to continue parsing from a good checkpoint "
             "if parsing later fails. Returns a new parser.")
    },
    {
        "parser/has-more", cfun_parse_has_more,
        JDOC("(parser/has-more parser)\n\n"
             "Check if the parser has more values in the value queue.")
    },
    {
        "parser/produce", cfun_parse_produce,
        JDOC("(parser/produce parser)\n\n"
             "Dequeue the next value in the parse queue. Will return nil if "
             "no parsed values are in the queue, otherwise will dequeue the "
             "next value.")
    },
    {
        "parser/consume", cfun_parse_consume,
        JDOC("(parser/consume parser bytes &opt index)\n\n"
             "Input bytes into the parser and parse them. Will not throw errors "
             "if there is a parse error. Starts at the byte index given by index. Returns "
             "the number of bytes read.")
    },
    {
        "parser/byte", cfun_parse_byte,
        JDOC("(parser/byte parser b)\n\n"
             "Input a single byte into the parser byte stream. Returns the parser.")
    },
    {
        "parser/error", cfun_parse_error,
        JDOC("(parser/error parser)\n\n"
             "If the parser is in the error state, returns the message associated with "
             "that error. Otherwise, returns nil. Also flushes the parser state and parser "
             "queue, so be sure to handle everything in the queue before calling "
             "parser/error.")
    },
    {
        "parser/status", cfun_parse_status,
        JDOC("(parser/status parser)\n\n"
             "Gets the current status of the parser state machine. The status will "
             "be one of:\n\n"
             "\t:pending - a value is being parsed.\n"
             "\t:error - a parsing error was encountered.\n"
             "\t:root - the parser can either read more values or safely terminate.")
    },
    {
        "parser/flush", cfun_parse_flush,
        JDOC("(parser/flush parser)\n\n"
             "Clears the parser state and parse queue. Can be used to reset the parser "
             "if an error was encountered. Does not reset the line and column counter, so "
             "to begin parsing in a new context, create a new parser.")
    },
    {
        "parser/state", cfun_parse_state,
        JDOC("(parser/state parser &opt key)\n\n"
             "Returns a representation of the internal state of the parser. If a key is passed, "
             "only that information about the state is returned. Allowed keys are:\n\n"
             "\t:delimiters - Each byte in the string represents a nested data structure. For example, "
             "if the parser state is '([\"', then the parser is in the middle of parsing a "
             "string inside of square brackets inside parentheses. Can be used to augment a REPL prompt."
             "\t:frames - Each table in the array represents a 'frame' in the parser state. Frames "
             "contain information about the start of the expression being parsed as well as the "
             "type of that expression and some type-specific information.")
    },
    {
        "parser/where", cfun_parse_where,
        JDOC("(parser/where parser)\n\n"
             "Returns the current line number and column of the parser's internal state.")
    },
    {
        "parser/eof", cfun_parse_eof,
        JDOC("(parser/eof parser)\n\n"
             "Indicate that the end of file was reached to the parser. This puts the parser in the :dead state.")
    },
    {
        "parser/insert", cfun_parse_insert,
        JDOC("(parser/insert parser value)\n\n"
             "Insert a value into the parser. This means that the parser state can be manipulated "
             "in between chunks of bytes. This would allow a user to add extra elements to arrays "
             "and tuples, for example. Returns the parser.")
    },
    {NULL, NULL, NULL}
};

/* Load the library */
void janet_lib_parse(JanetTable *env) {
    janet_core_cfuns(env, NULL, parse_cfuns);
}
