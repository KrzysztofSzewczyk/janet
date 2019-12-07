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
#include "gc.h"
#include "util.h"
#include "state.h"
#endif

#ifdef JANET_THREADS

#include <setjmp.h>
#include <time.h>
#include <pthread.h>

/* typedefed in janet.h */
struct JanetMailbox {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    JanetMailbox *parent; /* May not have a parent */
    JanetTable *decode; /* Only allowed access by one thread */
    JanetBuffer buf;
    int refCount;
    int closed;
};

static JANET_THREAD_LOCAL JanetMailbox *janet_vm_mailbox = NULL;

static JanetMailbox *janet_mailbox_create(JanetMailbox *parent, int refCount) {
    JanetMailbox *mailbox = malloc(sizeof(JanetMailbox));
    if (NULL == mailbox) {
        JANET_OUT_OF_MEMORY;
    }
    pthread_mutex_init(&mailbox->lock, NULL);
    pthread_cond_init(&mailbox->cond, NULL);
    janet_buffer_init(&mailbox->buf, 1024);
    mailbox->refCount = refCount;
    mailbox->closed = 0;
    mailbox->parent = parent;
    return mailbox;
}

static void janet_mailbox_destroy(JanetMailbox *mailbox) {
    pthread_mutex_destroy(&mailbox->lock);
    pthread_cond_destroy(&mailbox->cond);
    janet_buffer_deinit(&mailbox->buf);
    free(mailbox);
}

/* Assumes you have the mailbox lock already */
static void janet_mailbox_ref_with_lock(JanetMailbox *mailbox, int delta) {
    mailbox->refCount += delta;
    if (mailbox->refCount <= 0) {
        janet_mailbox_destroy(mailbox);
    } else {
        pthread_mutex_unlock(&mailbox->lock);
    }
}

static void janet_mailbox_ref(JanetMailbox *mailbox, int delta) {
    pthread_mutex_lock(&mailbox->lock);
    janet_mailbox_ref_with_lock(mailbox, delta);
}

void janet_threads_init(void) {
    if (NULL != janet_vm_mailbox) {
        return;
    }
    janet_vm_mailbox = janet_mailbox_create(NULL, 1);
}

void janet_threads_deinit(void) {
    pthread_mutex_lock(&janet_vm_mailbox->lock);
    janet_vm_mailbox->closed = 1;
    janet_mailbox_ref_with_lock(janet_vm_mailbox, -1);
    janet_vm_mailbox = NULL;
}

static void janet_close_thread(JanetThread *thread) {
    if (thread->mailbox) {
        janet_mailbox_ref(thread->mailbox, -1);
        thread->mailbox = NULL;
    }
}

static int thread_gc(void *p, size_t size) {
    (void) size;
    JanetThread *thread = (JanetThread *)p;
    janet_close_thread(thread);
    return 0;
}

static int thread_mark(void *p, size_t size) {
    (void) size;
    JanetThread *thread = (JanetThread *)p;
    if (thread->encode) {
        janet_mark(janet_wrap_table(thread->encode));
    }
    return 0;
}

/* Returns 1 if could not send (encode error), 2 for mailbox closed, and
 * 0 otherwise. Will not panic.  */
int janet_thread_send(JanetThread *thread, Janet msg) {

    /* Ensure mailbox is not closed. */
    JanetMailbox *mailbox = thread->mailbox;
    if (NULL == mailbox) return 2;
    pthread_mutex_lock(&mailbox->lock);
    if (mailbox->closed) {
        janet_mailbox_ref_with_lock(mailbox, -1);
        thread->mailbox = NULL;
        return 2;
    }

    /* Hack to capture all panics from marshalling. This works because
     * we know janet_marshal won't mess with other essential global state. */
    jmp_buf buf;
    jmp_buf *old_buf = janet_vm_jmp_buf;
    janet_vm_jmp_buf = &buf;
    int32_t oldcount = mailbox->buf.count;

    int ret = 0;
    if (setjmp(buf)) {
        ret = 1;
        mailbox->buf.count = oldcount;
    } else {
        janet_marshal(&mailbox->buf, msg, thread->encode, 0);
    }

    /* Cleanup */
    janet_vm_jmp_buf = old_buf;
    pthread_mutex_unlock(&mailbox->lock);

    /* Potentially wake up a blocked thread */
    if (oldcount == 0 && ret == 0) {
        pthread_cond_signal(&mailbox->cond);
    }

    return ret;
}

/* Convert an interval from now in an absolute timespec */
static void janet_sec2ts(double sec, struct timespec *ts) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    time_t tvsec = (time_t) floor(sec);
    long tvnsec = (long) floor(1000000000.0 * (sec - ((double) tvsec)));
    tvsec += now.tv_sec;
    tvnsec += now.tv_nsec;
    if (tvnsec >= 1000000000L) {
        tvnsec -= 1000000000L;
        tvsec += 1;
    }
    ts->tv_sec = tvsec;
    ts->tv_nsec = tvnsec;
}

/* Returns 0 on successful message. Returns 1 if timedout */
int janet_thread_receive(Janet *msg_out, double timeout) {
    pthread_mutex_lock(&janet_vm_mailbox->lock);

    /* For timeouts */
    struct timespec timeout_ts;
    int timedwait = timeout > 0.0;
    int nowait = timeout == 0.0;
    if (timedwait) janet_sec2ts(timeout, &timeout_ts);

    for (;;) {

        /* Check for messages waiting for use */
        if (janet_vm_mailbox->buf.count) {
            /* Hack to capture all panics from marshalling. This works because
             * we know janet_marshal won't mess with other essential global state. */
            jmp_buf buf;
            jmp_buf *old_buf = janet_vm_jmp_buf;
            janet_vm_jmp_buf = &buf;

            /* Handle errors */
            if (setjmp(buf)) {
                /* Bad message, so clear buffer and wait for the next */
                janet_vm_mailbox->buf.count = 0;
                janet_vm_jmp_buf = old_buf;
            } else {
                /* Read from beginning of channel */
                const uint8_t *nextItem = NULL;
                Janet item = janet_unmarshal(
                                 janet_vm_mailbox->buf.data, janet_vm_mailbox->buf.count,
                                 0, janet_vm_mailbox->decode, &nextItem);

                /* Update memory and put result into *msg_out */
                int32_t chunkCount = nextItem - janet_vm_mailbox->buf.data;
                memmove(janet_vm_mailbox->buf.data, nextItem, janet_vm_mailbox->buf.count - chunkCount);
                janet_vm_mailbox->buf.count -= chunkCount;
                *msg_out = item;
                janet_vm_jmp_buf = old_buf;
                pthread_mutex_unlock(&janet_vm_mailbox->lock);
                return 0;
            }
        }

        if (nowait || janet_vm_mailbox->refCount <= 1) {
            /* If there is only one ref, it is us. This means that if we
             * start waiting now, we can never possibly get a message, as
             * our reference will not propogate to other threads while we are blocked. */
            pthread_mutex_unlock(&janet_vm_mailbox->lock);
            return 1;
        }

        /* Wait for next message */
        if (timedwait) {
            if (pthread_cond_timedwait(
                        &janet_vm_mailbox->cond,
                        &janet_vm_mailbox->lock,
                        &timeout_ts)) {
                pthread_mutex_unlock(&janet_vm_mailbox->lock);
                return 1;
            }
        } else {
            pthread_cond_wait(
                &janet_vm_mailbox->cond,
                &janet_vm_mailbox->lock);
        }
    }

}

static Janet janet_thread_getter(void *p, Janet key);

static JanetAbstractType Thread_AT = {
    "core/thread",
    thread_gc,
    thread_mark,
    janet_thread_getter,
    NULL,
    NULL,
    NULL,
    NULL
};

static JanetThread *janet_make_thread(JanetMailbox *mailbox, JanetTable *encode) {
    JanetThread *thread = janet_abstract(&Thread_AT, sizeof(JanetThread));
    thread->mailbox = mailbox;
    thread->encode = encode;
    return thread;
}

JanetThread *janet_getthread(const Janet *argv, int32_t n) {
    return (JanetThread *) janet_getabstract(argv, n, &Thread_AT);
}

static JanetTable *janet_get_core_table(const char *name) {
    JanetTable *env = janet_core_env(NULL);
    Janet out = janet_wrap_nil();
    JanetBindingType bt = janet_resolve(env, janet_csymbol(name), &out);
    if (bt == JANET_BINDING_NONE) return NULL;
    if (!janet_checktype(out, JANET_TABLE)) return NULL;
    return janet_unwrap_table(out);
}

/* Runs in new thread */
static int thread_worker(JanetMailbox *mailbox) {
    JanetFiber *fiber = NULL;
    Janet out;

    /* Use the mailbox we were given */
    janet_vm_mailbox = mailbox;

    /* Init VM */
    janet_init();

    /* Get dictionaries for default encode/decode */
    JanetTable *encode = janet_get_core_table("make-image-dict");
    mailbox->decode = janet_get_core_table("load-image-dict");

    /* Create parent thread */
    JanetThread *parent = janet_make_thread(mailbox->parent, encode);
    janet_mailbox_ref(mailbox->parent, -1);
    mailbox->parent = NULL; /* only used to create the thread */
    Janet parentv = janet_wrap_abstract(parent);

    /* Unmarshal the function */
    Janet funcv;
    int status = janet_thread_receive(&funcv, -1.0);

    if (status) goto error;
    if (!janet_checktype(funcv, JANET_FUNCTION)) goto error;
    JanetFunction *func = janet_unwrap_function(funcv);

    /* Arity check */
    if (func->def->min_arity > 1 || func->def->max_arity < 1) {
        goto error;
    }

    /* Call function */
    Janet argv[1] = { parentv };
    fiber = janet_fiber(func, 64, 1, argv);
    JanetSignal sig = janet_continue(fiber, janet_wrap_nil(), &out);
    if (sig != JANET_SIGNAL_OK) {
        janet_eprintf("in thread %v: ", janet_wrap_abstract(janet_make_thread(mailbox, encode)));
        janet_stacktrace(fiber, out);
    }

    /* Normal exit */
    janet_deinit();
    return 0;

    /* Fail to set something up */
error:
    janet_eprintf("thread failed to start\n");
    janet_deinit();
    return 1;
}

static void *janet_pthread_wrapper(void *param) {
    thread_worker((JanetMailbox *)param);
    return NULL;
}

static int janet_thread_start_child(JanetThread *thread) {
    pthread_t handle;
    int error = pthread_create(&handle, NULL, janet_pthread_wrapper, thread->mailbox);
    if (error) {
        return 1;
    } else {
        pthread_detach(handle);
        return 0;
    }
}

/*
 * Cfuns
 */

static Janet cfun_thread_new(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    JanetTable *encode = janet_get_core_table("make-image-dict");
    JanetMailbox *mailbox = janet_mailbox_create(janet_vm_mailbox, 2);

    /* one for created thread, one for ->parent reference in new mailbox */
    janet_mailbox_ref(janet_vm_mailbox, 2);

    JanetThread *thread = janet_make_thread(mailbox, encode);
    if (janet_thread_start_child(thread)) {
        janet_mailbox_ref(mailbox, -1); /* mailbox reference */
        janet_mailbox_ref(janet_vm_mailbox, -1); /* ->parent reference */
        janet_panic("could not start thread");
    }
    return janet_wrap_abstract(thread);
}

static Janet cfun_thread_send(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetThread *thread = janet_getthread(argv, 0);
    int status = janet_thread_send(thread, argv[1]);
    switch (status) {
        default:
            break;
        case 1:
            janet_panicf("failed to send message %v", argv[1]);
        case 2:
            janet_panic("thread mailbox is closed");
    }
    return argv[0];
}

static Janet cfun_thread_receive(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 1);
    double wait = janet_optnumber(argv, argc, 0, -1.0);
    Janet out;
    int status = janet_thread_receive(&out, wait);
    switch (status) {
        default:
            break;
        case 1:
            janet_panicf("timeout after %f seconds", wait);
    }
    return out;
}

static Janet cfun_thread_close(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetThread *thread = janet_getthread(argv, 0);
    janet_close_thread(thread);
    return janet_wrap_nil();
}

static const JanetMethod janet_thread_methods[] = {
    {"send", cfun_thread_send},
    {"close", cfun_thread_close},
    {NULL, NULL}
};

static Janet janet_thread_getter(void *p, Janet key) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD)) janet_panicf("expected keyword method");
    return janet_getmethod(janet_unwrap_keyword(key), janet_thread_methods);
}

static const JanetReg threadlib_cfuns[] = {
    {
        "thread/new", cfun_thread_new,
        JDOC("(thread/new)\n\n"
             "Start a new thread. The thread will wait for a message containing the function used to start the thread, which should be subsequently "
             "sent over after thread creation.")
    },
    {
        "thread/send", cfun_thread_send,
        JDOC("(thread/send thread msg)\n\n"
             "Send a message to the thread. This will never block and returns thread immediately. "
             "Will throw an error if there is a problem sending the message.")
    },
    {
        "thread/receive", cfun_thread_receive,
        JDOC("(thread/receive &opt timeout)\n\n"
             "Get a message sent to this thread. If timeout is provided, an error will be thrown after the timeout has elapsed but "
             "no messages are received.")
    },
    {
        "thread/close", cfun_thread_close,
        JDOC("(thread/close thread)\n\n"
             "Close a thread, unblocking it and ending communication with it. Note that closing "
             "a thread is idempotent and does not cancel the thread's operation. Returns nil.")
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_thread(JanetTable *env) {
    janet_core_cfuns(env, NULL, threadlib_cfuns);
    janet_register_abstract_type(&Thread_AT);
}

#endif
