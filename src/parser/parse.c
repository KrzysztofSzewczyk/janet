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
#include <dst/dstparse.h>
#include <headerlibs/vector.h>

/* Quote a value */
static Dst quote(Dst x) {
    Dst *t = dst_tuple_begin(2);
    t[0] = dst_csymbolv("quote");
    t[1] = x;
    return dst_wrap_tuple(dst_tuple_end(t));
}

/* Check if a character is whitespace */
static int is_whitespace(uint8_t c) {
    return c == ' ' 
        || c == '\t'
        || c == '\n'
        || c == '\r'
        || c == '\0'
        || c == '\f'
        || c == ';'
        || c == ',';
}

/* Code gen

printf("static uint32_t symchars[8] = {\n\t");
for (int i = 0; i < 256; i += 32) {
    uint32_t block = 0;
    for (int j = 0; j < 32; j++) {
        block |= is_symbol_char_gen(i + j) << j;
    }
    printf("0x%08x%s", block, (i == (256 - 32)) ? "" : ", ");
}
printf("\n};\n");

static int is_symbol_char_gen(uint8_t c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    return (c == '!' ||
        c == '$' ||
        c == '%' ||
        c == '&' ||
        c == '*' ||
        c == '+' ||
        c == '-' ||
        c == '.' ||
        c == '/' ||
        c == ':' ||
        c == '<' ||
        c == '?' ||
        c == '=' ||
        c == '>' ||
        c == '@' ||
        c == '\\' ||
        c == '^' ||
        c == '_' ||
        c == '~' ||
        c == '|');
}

The table contains 256 bits, where each bit is 1
if the corresponding ascci code is a symbol char, and 0
if not. The upper characters are also considered symbol
chars and are then checked for utf-8 compliance. */
static uint32_t symchars[8] = {
	0x00000000, 0xF7ffec72, 0xd7ffffff, 0x57fffffe,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff
};

/* Check if a character is a valid symbol character
 * symbol chars are A-Z, a-z, 0-9, or one of !$&*+-./:<=>@\^_~| */
static int is_symbol_char(uint8_t c) {
    return symchars[c >> 5] & (1 << (c & 0x1F));
}

/* Validate some utf8. Useful for identifiers. Only validates
 * the encoding, does not check for valid codepoints (they
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

        /* Check for overlong encodings */ 
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

typedef int (*Consumer)(DstParser *p, DstParseState *state, uint8_t c);
struct DstParseState {
    int32_t qcount;
    int32_t argn;
    int flags;
    size_t start;
    Consumer consumer;
};

#define PFLAG_CONTAINER 1
#define PFLAG_BUFFER 2

static void pushstate(DstParser *p, Consumer consumer, int flags) {
    DstParseState s;
    s.qcount = 0;
    s.argn = 0;
    s.flags = flags;
    s.consumer = consumer;
    s.start = p->index;
    dst_v_push(p->states, s);
}

static void popstate(DstParser *p, Dst val) {
    DstParseState top = dst_v_last(p->states);
    DstParseState *newtop;
    dst_v_pop(p->states);
    newtop = &dst_v_last(p->states);
    if (newtop->flags & PFLAG_CONTAINER) {
        int32_t i, len;
        len = newtop->qcount;
        /* Quote the returned value qcount times */
        for (i = 0; i < len; i++) {
            if (p->flags & DST_PARSEFLAG_SOURCEMAP)
                val = dst_ast_wrap(val, (int32_t) top.start, (int32_t) p->index);
            val = quote(val);
        }
        newtop->qcount = 0;

        /* Ast wrap */
        if (p->flags & DST_PARSEFLAG_SOURCEMAP)
            val = dst_ast_wrap(val, (int32_t) top.start, (int32_t) p->index);

        newtop->argn++;
        dst_v_push(p->argstack, val);
    }
}

static uint8_t checkescape(uint8_t c) {
    switch (c) {
        default: return 0;
        case 'h': return 1;
        case 'n': return '\n'; 
        case 't': return '\t'; 
        case 'r': return '\r'; 
        case '0': return '\0'; 
        case 'z': return '\0'; 
        case 'f': return '\f';
        case 'e': return 27;
        case '"': return '"'; 
        case '\'': return '\''; 
        case '\\': return '\\'; 
    }
}

/* Forward declare */
static int stringchar(DstParser *p, DstParseState *state, uint8_t c);

static int escapeh(DstParser *p, DstParseState *state, uint8_t c) {
    int digit = to_hex(c);
    if (digit < 0) {
        p->error = "invalid hex digit in hex escape";
        return 1;
    }
    state->argn = (state->argn << 4) + digit;;
    state->qcount--;
    if (!state->qcount) {
        dst_v_push(p->buf, (state->argn & 0xFF));
        state->argn = 0;
        state->consumer = stringchar;
    }
    return 1;
}

static int escape1(DstParser *p, DstParseState *state, uint8_t c) {
    uint8_t e = checkescape(c);
    if (!e) {
        p->error = "invalid string escape sequence";
        return 1;
    }
    if (c == 'h') {
        state->qcount = 2;
        state->argn = 0;
        state->consumer = escapeh;
    } else {
        dst_v_push(p->buf, e);
        state->consumer = stringchar;
    }
    return 1;
}

static int stringchar(DstParser *p, DstParseState *state, uint8_t c) {
    /* Enter escape */
    if (c == '\\') {
        state->consumer = escape1;
        return 1;
    }
    /* String end */
    if (c == '"') {
        /* String end */
        Dst ret;
        if (state->flags & PFLAG_BUFFER) {
            DstBuffer *b = dst_buffer(dst_v_count(p->buf));
            dst_buffer_push_bytes(b, p->buf, dst_v_count(p->buf));
            ret = dst_wrap_buffer(b);
        } else {
            ret = dst_wrap_string(dst_string(p->buf, dst_v_count(p->buf)));
        }
        dst_v_empty(p->buf);
        popstate(p, ret);
        return 1;
    }
    /* normal char */
    dst_v_push(p->buf, c);
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

static int tokenchar(DstParser *p, DstParseState *state, uint8_t c) {
    Dst numcheck, ret;
    int32_t blen;
    if (is_symbol_char(c)) {
        dst_v_push(p->buf, (uint8_t) c);
        if (c > 127) state->argn = 1; /* Use to indicate non ascii */
        return 1;
    }
    /* Token finished */
    blen = dst_v_count(p->buf);
    numcheck = dst_scan_number(p->buf, blen);
    if (!dst_checktype(numcheck, DST_NIL)) {
        ret = numcheck;
    } else if (!check_str_const("nil", p->buf, blen)) {
        ret = dst_wrap_nil();
    } else if (!check_str_const("false", p->buf, blen)) {
        ret = dst_wrap_false();
    } else if (!check_str_const("true", p->buf, blen)) {
        ret = dst_wrap_true();
    } else if (p->buf) {
        if (p->buf[0] >= '0' && p->buf[0] <= '9') {
            p->error = "symbol literal cannot start with a digit";
            return 0;
        } else {
            /* Don't do full utf8 check unless we have seen non ascii characters. */
            int valid = (!state->argn) || valid_utf8(p->buf, blen);
            if (!valid) {
                p->error = "invalid utf-8 in symbol";
                return 0;
            }
            ret = dst_symbolv(p->buf, blen);
        }
    } else {
        p->error = "empty symbol invalid";
        return 0;
    }
    dst_v_empty(p->buf);
    popstate(p, ret);
    return 0;
}

static int comment(DstParser *p, DstParseState *state, uint8_t c) {
    (void) state;
    if (c == '\n') dst_v_pop(p->states);
    return 1;
}

/* Forward declaration */
static int root(DstParser *p, DstParseState *state, uint8_t c);

static int dotuple(DstParser *p, DstParseState *state, uint8_t c) {
    if (c == ')') {
        int32_t i;
        Dst *ret = dst_tuple_begin(state->argn);
        for (i = state->argn - 1; i >= 0; i--) {
            ret[i] = dst_v_last(p->argstack); dst_v_pop(p->argstack);
        }
        popstate(p, dst_wrap_tuple(dst_tuple_end(ret)));
        return 1;
    }
    return root(p, state, c);
}

static int doarray(DstParser *p, DstParseState *state, uint8_t c) {
    if (c == ']') {
        int32_t i;
        DstArray *array = dst_array(state->argn);
        for (i = state->argn - 1; i >= 0; i--) {
            array->data[i] = dst_v_last(p->argstack); dst_v_pop(p->argstack);
        }
        array->count = state->argn;
        popstate(p, dst_wrap_array(array));
        return 1;
    }
    return root(p, state, c);
}

static int dostruct(DstParser *p, DstParseState *state, uint8_t c) {
    if (c == '}') {
        int32_t i;
        DstKV *st;
        if (state->argn & 1) {
            p->error = "struct literal expects even number of arguments";
            return 1;
        }
        st = dst_struct_begin(state->argn >> 1);
        for (i = state->argn; i > 0; i -= 2) {
            Dst value = dst_v_last(p->argstack); dst_v_pop(p->argstack);
            Dst key = dst_v_last(p->argstack); dst_v_pop(p->argstack);
            dst_struct_put(st, key, value);
        }
        popstate(p, dst_wrap_struct(dst_struct_end(st)));
        return 1;
    }
    return root(p, state, c);
}

static int dotable(DstParser *p, DstParseState *state, uint8_t c) {
    if (c == '}') {
        int32_t i;
        DstTable *table;
        if (state->argn & 1) {
            p->error = "table literal expects even number of arguments";
            return 1;
        }
        table = dst_table(state->argn >> 1);
        for (i = state->argn; i > 0; i -= 2) {
            Dst value = dst_v_last(p->argstack); dst_v_pop(p->argstack);
            Dst key = dst_v_last(p->argstack); dst_v_pop(p->argstack);
            dst_table_put(table, key, value);
        }
        popstate(p, dst_wrap_table(table));
        return 1;
    }
    return root(p, state, c);
}

static int ampersand(DstParser *p, DstParseState *state, uint8_t c) {
    (void) state;
    dst_v_pop(p->states);
    if (c == '{') {
        pushstate(p, dotable, PFLAG_CONTAINER);
        return 1;
    } else if (c == '"') {
        pushstate(p, stringchar, PFLAG_BUFFER);
        return 1;
    }
    pushstate(p, tokenchar, 0);
    dst_v_push(p->buf, '@'); /* Push the leading ampersand that was dropped */
    return 0;
}

static int root(DstParser *p, DstParseState *state, uint8_t c) {
    switch (c) {
        default:
            if (is_whitespace(c)) return 1;
            if (!is_symbol_char(c)) {
                p->error = "unexpected character";
                return 1;
            }
            pushstate(p, tokenchar, 0);
            return 0;
        case '\'':
            state->qcount++;
            return 1;
        case '"':
            pushstate(p, stringchar, 0);
            return 1;
        case '#':
            pushstate(p, comment, 0);
            return 1;
        case '@':
            pushstate(p, ampersand, 0);
            return 1;
        case ')':
        case ']':
        case '}':
            p->error = "mismatched delimiter";
            return 1;
        case '(':
            pushstate(p, dotuple, PFLAG_CONTAINER);
            return 1;
        case '[':
            pushstate(p, doarray, PFLAG_CONTAINER);
            return 1;
        case '{':
            pushstate(p, dostruct, PFLAG_CONTAINER);
            return 1;
    }
}

int dst_parser_consume(DstParser *parser, uint8_t c) {
    int consumed = 0;
    if (parser->error) return 0;
    parser->index++;
    while (!consumed && !parser->error) {
        DstParseState *state = &dst_v_last(parser->states);
        consumed = state->consumer(parser, state, c);
    }
    parser->lookback = c;
    return 1;
}

enum DstParserStatus dst_parser_status(DstParser *parser) {
    if (parser->error) return DST_PARSE_ERROR;
    if (dst_v_count(parser->states) > 1) return DST_PARSE_PENDING;
    if (dst_v_count(parser->argstack)) return DST_PARSE_FULL;
    return DST_PARSE_ROOT;
}

const char *dst_parser_error(DstParser *parser) {
    enum DstParserStatus status = dst_parser_status(parser);
    if (status == DST_PARSE_ERROR) {
        const char *e = parser->error;
        dst_v_empty(parser->argstack);
        dst_v__cnt(parser->states) = 1;
        parser->error = NULL;
        dst_v_empty(parser->buf);
        return e;
    }
    return NULL;
}

Dst dst_parser_produce(DstParser *parser) {
    Dst ret;
    int32_t i;
    enum DstParserStatus status = dst_parser_status(parser);
    if (status != DST_PARSE_FULL) return dst_wrap_nil();
    ret = parser->argstack[0];
    for (i = 1; i < dst_v_count(parser->argstack); i++) {
        parser->argstack[i - 1] = parser->argstack[i];
    }
    dst_v__cnt(parser->argstack)--;
    return ret;
}

void dst_parser_init(DstParser *parser, int flags) {
    parser->argstack = NULL;
    parser->states = NULL;
    parser->buf = NULL;
    parser->error = NULL;
    parser->index = 0;
    parser->lookback = -1;
    parser->flags = flags;
    pushstate(parser, root, PFLAG_CONTAINER);
}

void dst_parser_deinit(DstParser *parser) {
    dst_v_free(parser->argstack);
    dst_v_free(parser->buf);
    dst_v_free(parser->states);
}

/* C functions */

static int parsermark(void *p, size_t size) {
    int32_t i;
    DstParser *parser = (DstParser *)p;
    (void) size;
    for (i = 0; i < dst_v_count(parser->argstack); i++) {
        dst_mark(parser->argstack[i]);
    }
    return 0;
}

static int parsergc(void *p, size_t size) {
    DstParser *parser = (DstParser *)p;
    (void) size;
    dst_parser_deinit(parser);
    return 0;
}

DstAbstractType dst_parse_parsertype = {
    ":parse.parser",
    parsergc,
    parsermark
};

/* C Function parser */
static int cfun_parser(DstArgs args) {
    int flags;
    if (args.n > 1) return dst_throw(args, "expected 1 argument");
    if (args.n) {
        if (!dst_checktype(args.v[0], DST_INTEGER)) return dst_throw(args, "expected integer");
        flags = dst_unwrap_integer(args.v[0]);
    } else {
        flags = 0;
    }
    DstParser *p = dst_abstract(&dst_parse_parsertype, sizeof(DstParser));
    dst_parser_init(p, flags);
    return dst_return(args, dst_wrap_abstract(p));
}

/* Check file argument */
static DstParser *checkparser(DstArgs args) {
    DstParser *p;
    if (args.n == 0) {
    	*args.ret = dst_cstringv("expected parse.parser");
        return NULL;
    }
    if (!dst_checktype(args.v[0], DST_ABSTRACT)) {
    	*args.ret = dst_cstringv("expected parse.parser");
        return NULL;
    }
    p = (DstParser *) dst_unwrap_abstract(args.v[0]);
    if (dst_abstract_type(p) != &dst_parse_parsertype) {
    	*args.ret = dst_cstringv("expected parse.parser");
        return NULL;
    }
    return p;
}

static int cfun_consume(DstArgs args) {
    const uint8_t *bytes;
    int32_t len;
    DstParser *p;
    int32_t i;
    if (args.n != 2) return dst_throw(args, "expected 2 arguments");
    p = checkparser(args);
    if (!p) return 1;
    if (!dst_chararray_view(args.v[1], &bytes, &len)) return dst_throw(args, "expected string/buffer");
    for (i = 0; i < len; i++) {
        dst_parser_consume(p, bytes[i]);
        switch (dst_parser_status(p)) {
            case DST_PARSE_ROOT:
            case DST_PARSE_PENDING:
                break;
            default:
                {
                    DstBuffer *b = dst_buffer(len - i);
                    dst_buffer_push_bytes(b, bytes + i + 1, len - i - 1);
                    return dst_return(args, dst_wrap_buffer(b));
                }
        }
    }
    return dst_return(args, dst_wrap_nil());
}

static int cfun_byte(DstArgs args) {
    DstParser *p;
    if (args.n != 2) return dst_throw(args, "expected 2 arguments");
    p = checkparser(args);
    if (!p) return 1;
    if (!dst_checktype(args.v[1], DST_INTEGER)) return dst_throw(args, "expected integer");
    dst_parser_consume(p, 0xFF & dst_unwrap_integer(args.v[1]));
    return dst_return(args, args.v[0]);
}

static int cfun_status(DstArgs args) {
    const char *stat = NULL;
    DstParser *p = checkparser(args);
    if (!p) return 1;
    switch (dst_parser_status(p)) {
        case DST_PARSE_FULL:
            stat = ":full";
            break;
        case DST_PARSE_PENDING:
            stat = ":pending";
            break;
        case DST_PARSE_ERROR:
            stat = ":error";
            break;
        case DST_PARSE_ROOT:
            stat = ":root";
            break;
    }
    return dst_return(args, dst_csymbolv(stat));
}

static int cfun_error(DstArgs args) {
    const char *err;
    DstParser *p = checkparser(args);
    if (!p) return 1;
    err = dst_parser_error(p);
    if (err) {
        return dst_return(args, dst_cstringv(err));
    } else {
        return dst_return(args, dst_wrap_nil());
    }
}

static int cfun_produce(DstArgs args) {
    Dst val;
    DstParser *p = checkparser(args);
    if (!p) return 1;
    val = dst_parser_produce(p);
    return dst_return(args, val);
}

/* AST */
static int cfun_unwrap1(DstArgs args) {
    if (args.n != 1) return dst_throw(args, "expected 1 argument");
    return dst_return(args, dst_ast_unwrap1(args.v[0]));
}

static int cfun_unwrap(DstArgs args) {
    if (args.n != 1) return dst_throw(args, "expected 1 argument");
    return dst_return(args, dst_ast_unwrap(args.v[0]));
}

static int cfun_wrap(DstArgs args) {
    if (args.n != 1) return dst_throw(args, "expected 1 argument");
    return dst_return(args, dst_ast_wrap(args.v[0], -1, -1));
}

static int cfun_node(DstArgs args) {
    DstAst *ast;
    Dst *tup;
    int32_t start, end;
    if (args.n != 1) return dst_throw(args, "expected 1 argument");
    ast = dst_ast_node(args.v[0]);
    if (ast) {
        start = ast->source_start;
        end = ast->source_end;
    } else {
        start = -1;
        end = -1;
    }
    tup = dst_tuple_begin(2);
    tup[0] = dst_wrap_integer(start);
    tup[1] = dst_wrap_integer(end);
    return dst_return(args, dst_wrap_tuple(dst_tuple_end(tup)));
}

static int cfun_parsenumber(DstArgs args) {
    const uint8_t *data;
    Dst x;
    int32_t len;
    if (args.n != 1) return dst_throw(args, "expected string or buffer");
    if (!dst_chararray_view(args.v[0], &data, &len))
        return dst_throw(args, "expected string or buffer");
    x = dst_scan_number(data, len);
    if (!dst_checktype(x, DST_INTEGER) && !dst_checktype(x, DST_REAL)) {
        return dst_throw(args, "error parsing number");
    }
    return dst_return(args, x);
}

static int cfun_parseint(DstArgs args) {
    const uint8_t *data;
    int32_t len, ret;
    int err = 0;
    if (args.n != 1) return dst_throw(args, "expected string or buffer");
    if (!dst_chararray_view(args.v[0], &data, &len))
        return dst_throw(args, "expected string or buffer");
    ret = dst_scan_integer(data, len, &err);
    if (err) {
        return dst_throw(args, "error parsing integer");
    }
    return dst_return(args, dst_wrap_integer(ret));
}

static int cfun_parsereal(DstArgs args) {
    const uint8_t *data;
    int32_t len;
    double ret;
    int err = 0;
    if (args.n != 1) return dst_throw(args, "expected string or buffer");
    if (!dst_chararray_view(args.v[0], &data, &len))
        return dst_throw(args, "expected string or buffer");
    ret = dst_scan_real(data, len, &err);
    if (err) {
        return dst_throw(args, "error parsing real");
    }
    return dst_return(args, dst_wrap_real(ret));
}

static const DstReg cfuns[] = {
    {"parser", cfun_parser},
    {"parser-produce", cfun_produce},
    {"parser-consume", cfun_consume},
    {"parser-byte", cfun_byte},
    {"parser-error", cfun_error},
    {"parser-status", cfun_status},
    {"ast-unwrap", cfun_unwrap},
    {"ast-unwrap1", cfun_unwrap1},
    {"ast-wrap", cfun_wrap},
    {"ast-node", cfun_node},
    {"parse-number", cfun_parsenumber},
    {"parse-int", cfun_parseint},
    {"parse-real", cfun_parsereal},
    {NULL, NULL}
};

/* Load the library */
int dst_lib_parse(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);
    return 0;
}
