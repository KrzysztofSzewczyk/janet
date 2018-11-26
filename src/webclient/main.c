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
#include <generated/webinit.h>
#include <emscripten.h>

static JanetFiber *repl_fiber = NULL;
static JanetBuffer *line_buffer = NULL;
static const uint8_t *line_prompt = NULL;

/* Yield to JS event loop from janet. Takes a repl prompt
 * and a buffer to fill with input data. */
static int repl_yield(JanetArgs args) {
    JANET_FIXARITY(args, 2);
    JANET_ARG_STRING(line_prompt, args, 0);
    JANET_ARG_BUFFER(line_buffer, args, 1);
    JANET_RETURN_NIL(args);
}

/* Re-enter the loop */
static int enter_loop(void) {
    Janet ret;
    JanetSignal status = janet_continue(repl_fiber, janet_wrap_nil(), &ret);
    if (status == JANET_SIGNAL_ERROR) {
        janet_stacktrace(repl_fiber, "runtime", ret);
        janet_deinit();
        repl_fiber = NULL;
        return 1;
    }
    return 0;
}

/* Allow JS interop from within janet */
static int cfun_js(JanetArgs args) {
    const uint8_t *bytes;
    int32_t len;
    JANET_FIXARITY(args, 1);
    JANET_ARG_BYTES(bytes, len, args, 0);
    (void) len;
    emscripten_run_script((const char *)bytes);
    JANET_RETURN_NIL(args);
}

/* Intialize the repl */
EMSCRIPTEN_KEEPALIVE
void repl_init(void) {
    int status;
    JanetTable *env;

    /* Set up VM */
    janet_init();
    janet_register("repl-yield", repl_yield);
    janet_register("js", cfun_js);
    env = janet_core_env();

    janet_def(env, "repl-yield", janet_wrap_cfunction(repl_yield), NULL);
    janet_def(env, "js", janet_wrap_cfunction(cfun_js), NULL);

    /* Run startup script */
    Janet ret;
    status = janet_dobytes(env, janet_gen_webinit, sizeof(janet_gen_webinit), "webinit.janet", &ret);
    if (status == JANET_SIGNAL_ERROR) {
        printf("start up error.\n");
        janet_deinit();
        repl_fiber = NULL;
        return;
    }
    janet_gcroot(ret);
    repl_fiber = janet_unwrap_fiber(ret);

    /* Start repl */
    if (enter_loop()) return;
}

/* Deinitialize the repl */
EMSCRIPTEN_KEEPALIVE
void repl_deinit(void) {
    if (!repl_fiber) {
        return;
    }
    repl_fiber = NULL;
    line_buffer = NULL;
    janet_deinit();
}

/* Get the prompt to show in the repl */
EMSCRIPTEN_KEEPALIVE
const char *repl_prompt(void) {
    return line_prompt ? ((const char *)line_prompt) : "";
}

/* Restart the repl calling from JS. Pass in the input for the next line. */
EMSCRIPTEN_KEEPALIVE
void repl_input(char *input) {

    /* Create the repl if we haven't yet */
    if (!repl_fiber) {
        printf("initialize the repl first");
    }

    /* Now fill the pending line_buffer and resume the repl loop */
    if (line_buffer) {
        janet_buffer_push_cstring(line_buffer, input);
        line_buffer = NULL;
        enter_loop();
    }
}
