// gpuseam.c: the seam's bodies, for both legs (gpuseam.h says why it exists).
//
// Every slot's state is one atomic: PENDING, READY, FAILED, or ABANDONED (its owner dropped it while a callback was
// still owed). A callback writes its results and then publishes the state with a release-store, or, finding the slot
// abandoned, cleans it up itself; a poll reads the state with an acquire-load before touching anything else.
#include "gpuseam.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __EMSCRIPTEN__
#include <webgpu/wgpu.h>
#endif

#define GPUSEAM_ABANDONED 3
// A scope of the seam's own that catches every kind: a compile worker's, natively.
#define GPUSEAM_FILTER_ANY (-1)
#define GPUSEAM_MESSAGE_BYTES 512

// A message's bytes into a slot's buffer, cut at its size. WGPU_STRLEN means NUL-terminated.
static int32_t keep_message(WGPUStringView v, char *dst) {
    if (v.data == NULL) return 0;
    size_t n = v.length;
    if (n == WGPU_STRLEN) n = strlen(v.data);
    if (n > GPUSEAM_MESSAGE_BYTES) n = GPUSEAM_MESSAGE_BYTES;
    memcpy(dst, v.data, n);
    return (int32_t)n;
}

static int32_t keep_text(const char *text, char *dst) {
    size_t n = strlen(text);
    if (n > GPUSEAM_MESSAGE_BYTES) n = GPUSEAM_MESSAGE_BYTES;
    memcpy(dst, text, n);
    return (int32_t)n;
}

static int32_t copy_out(const char *src, int32_t length, uint8_t *dst, int32_t capacity) {
    int32_t n = length < capacity ? length : capacity;
    if (n > 0) memcpy(dst, src, (size_t)n);
    return n;
}

// ---- a device's state: its errors and its loss -----------------------------------------------------------------------
//
// One block per device, made with its request and handed to both its callbacks as their userdata. wgpu-native reports a
// child's error to its device's callback after the device's last release (tests/facts, `afterrelease`), so the block is
// held by every handle made from the device, not by the device's copies alone, and freed when the last lets it go. On
// the web the lost callback holds it too, until it fires; natively it never fires (tests/facts, `noloss`).

#define GPUSEAM_RING 64

typedef struct {
    int32_t kind;    // GPUSEAM_KIND_*
    char *message;   // its own allocation, the message whole; NULL when it had none or could not be kept
    int32_t length;
} gpuseam_report;

struct gpuseam_state {
    atomic_int holders;
    atomic_flag lock;
    int32_t watched;                    // 0 for an adopted device, whose errors reach its maker's callback
    gpuseam_report ring[GPUSEAM_RING];  // the oldest at `head`
    int32_t head;
    int32_t count;
    int64_t dropped;                    // what did not fit since the last drain
    atomic_int lost;                    // 0, else GPUSEAM_LOSS_*, published once the message is kept
    char *lost_message;
    int32_t lost_length;
    gpuseam_scope *top;                 // its error scopes, the innermost first, of every thread (each holds the block)
    WGPUQueue queue;                    // natively, a reference on the device's queue, which a map flushes first
    int32_t print;                      // DeviceOptions.printErrors: each report the channel takes is printed to stderr
    int32_t compile_in_channel;         // natively, the jobs sent into its compile pool's channel and not yet taken
    struct gpuseam_pipeline_job *compile_overflow;        // and those waiting beside it, the oldest first
    struct gpuseam_pipeline_job *compile_overflow_tail;
};

struct gpuseam_scope {
    gpuseam_scope *below;    // the next older scope of the same device, on any thread
    gpuseam_state *state;    // a hold on it
    WGPUDevice device;       // a reference, for the web's pop
    const void *thread;      // the thread that pushed it
    int32_t filter;          // GPUSEAM_FILTER_*, the GPUSEAM_KIND_* it catches
    int32_t orphaned;        // its holder is done with it: popped once no scope above it on its thread is left
    int32_t caught;          // it holds an error: natively the first that matched, on the web the browser's answer
    gpuseam_report error;
    atomic_int popped;       // once popped: PENDING while the web's answer is owed, READY, FAILED, or ABANDONED
};

// A drain: the ring's reports, moved out of it.
struct gpuseam_errors {
    int32_t count;
    int64_t dropped;
    gpuseam_report reports[GPUSEAM_RING];
};

static atomic_llong g_states_alive;

static void state_lock(gpuseam_state *s) {
    while (atomic_flag_test_and_set_explicit(&s->lock, memory_order_acquire)) { }
}
static void state_unlock(gpuseam_state *s) { atomic_flag_clear_explicit(&s->lock, memory_order_release); }

static int32_t kind_of(WGPUErrorType type) {
    switch (type) {
        case WGPUErrorType_Validation: return GPUSEAM_KIND_VALIDATION;
        case WGPUErrorType_OutOfMemory: return GPUSEAM_KIND_OUT_OF_MEMORY;
        case WGPUErrorType_Internal: return GPUSEAM_KIND_INTERNAL;
        default: return GPUSEAM_KIND_UNKNOWN;
    }
}

// ---- the package's own refusals (AGENTS.gpu.md item 12, decision 9) ----------------------------------------------------
//
// What a backend would end the program on, the package refuses before the call and reports as a validation error, where
// WebGPU reports one: the innermost validation scope on the calling thread, else the device's channel. A browser keeps
// its scopes and has no way to add an error to them, so on both legs the report rides a call the backend refuses: a
// 4-byte buffer with no usage, labelled `kama-gpu-refusal:<n>`, whose error lands where any would, in call order
// (measured on both legs: the label is in the words). `report_of` puts the refusal's words in place of the backend's.

#define GPUSEAM_CARRIED 64
static const char CARRIER_PREFIX[] = "kama-gpu-refusal:";
static struct { uint32_t id; char *message; int32_t length; } g_carried[GPUSEAM_CARRIED];
static uint32_t g_carried_next = 1;
static atomic_flag g_carried_lock = ATOMIC_FLAG_INIT;

static void carried_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_carried_lock, memory_order_acquire)) { }
}
static void carried_unlock(void) { atomic_flag_clear_explicit(&g_carried_lock, memory_order_release); }

// The refusal a carrier's words name, taken out of the table (its id, after the prefix), or 0 when they name none.
static int32_t carried_words(const char *data, size_t n, char **message) {
    size_t k = sizeof CARRIER_PREFIX - 1;
    for (size_t i = 0; i + k <= n; i++) {
        if (memcmp(data + i, CARRIER_PREFIX, k) != 0) continue;
        uint32_t id = 0;
        size_t j = i + k;
        int digits = 0;
        while (j < n && data[j] >= '0' && data[j] <= '9' && digits < 10) { id = id * 10 + (uint32_t)(data[j] - '0'); j++; digits++; }
        if (digits == 0) return 0;
        int32_t length = 0;
        carried_lock();
        if (g_carried[id % GPUSEAM_CARRIED].id == id && g_carried[id % GPUSEAM_CARRIED].message != NULL) {
            *message = g_carried[id % GPUSEAM_CARRIED].message;
            length = g_carried[id % GPUSEAM_CARRIED].length;
            g_carried[id % GPUSEAM_CARRIED].message = NULL;
            g_carried[id % GPUSEAM_CARRIED].id = 0;
        }
        carried_unlock();
        return length;
    }
    return 0;
}

// A message kept whole, in an allocation of its own; a refusal's carrier's, the refusal's words.
static gpuseam_report report_of(int32_t kind, WGPUStringView v) {
    gpuseam_report r = { kind, NULL, 0 };
    if (v.data == NULL) return r;
    size_t n = v.length == WGPU_STRLEN ? strlen(v.data) : v.length;
    if (n == 0) return r;
    char *carried = NULL;
    int32_t carried_length = carried_words(v.data, n, &carried);
    if (carried_length > 0) {
        r.message = carried;
        r.length = carried_length;
        return r;
    }
    if (n > INT32_MAX) n = INT32_MAX;
    r.message = (char *)malloc(n);
    if (r.message == NULL) return r;
    memcpy(r.message, v.data, n);
    r.length = (int32_t)n;
    return r;
}

// printErrors (AGENTS.gpu.md item 12, decision 3, item 2): the line a report the channel takes is printed as, made
// while the state's lock is held and printed once it is let go, so no thread waits on a write to stderr. NULL when the
// device does not print, or the line cannot be allocated.
static char *printed_line(const gpuseam_state *s, const gpuseam_report *r) {
    if (s == NULL || !s->print) return NULL;
    const char *kind = r->kind == GPUSEAM_KIND_VALIDATION ? "a validation error"
                     : r->kind == GPUSEAM_KIND_OUT_OF_MEMORY ? "an out-of-memory error"
                     : r->kind == GPUSEAM_KIND_INTERNAL ? "an internal error" : "an error";
    size_t n = strlen("@kama/gpu: : \n") + strlen(kind) + (size_t)r->length + 1;
    char *line = (char *)malloc(n);
    if (line == NULL) return NULL;
    snprintf(line, n, "@kama/gpu: %s: %.*s\n", kind, (int)r->length, r->message != NULL ? r->message : "");
    return line;
}

static void print_line(char *line) {
    if (line == NULL) return;
    fputs(line, stderr);
    fflush(stderr);
    free(line);
}

// Into the ring, under the lock: kept while there is room, the oldest first, and counted once it is full, since the first
// error is usually the cause and the later ones its cascade. The report is moved: its message is the ring's, or freed.
static void ring_put(gpuseam_state *s, gpuseam_report *r) {
    if (s->count < GPUSEAM_RING) {
        s->ring[(s->head + s->count) % GPUSEAM_RING] = *r;
        s->count += 1;
    } else {
        s->dropped += 1;
        free(r->message);
    }
    r->message = NULL;
    r->length = 0;
}

static gpuseam_state *state_new(int32_t watched) {
    gpuseam_state *s = (gpuseam_state *)calloc(1, sizeof *s);
    if (s == NULL) return NULL;
    atomic_init(&s->holders, 1);
    atomic_flag_clear(&s->lock);
    atomic_init(&s->lost, 0);
    s->watched = watched;
    atomic_fetch_add_explicit(&g_states_alive, 1, memory_order_relaxed);
    return s;
}

gpuseam_state *gpuseam_state_unwatched(void) { return state_new(0); }

void gpuseam_state_hold(gpuseam_state *s) {
    if (s != NULL) atomic_fetch_add_explicit(&s->holders, 1, memory_order_relaxed);
}

void gpuseam_state_release(gpuseam_state *s) {
    if (s == NULL) return;
    if (atomic_fetch_sub_explicit(&s->holders, 1, memory_order_acq_rel) != 1) return;
    for (int32_t i = 0; i < s->count; i++) free(s->ring[(s->head + i) % GPUSEAM_RING].message);
    free(s->lost_message);
    if (s->queue != NULL) wgpuQueueRelease(s->queue);
    free(s);
    atomic_fetch_sub_explicit(&g_states_alive, 1, memory_order_relaxed);
}

int64_t gpuseam_states_alive(void) { return (int64_t)atomic_load_explicit(&g_states_alive, memory_order_relaxed); }

// The thread a scope was pushed on, by the address of a thread-local of its own.
static _Thread_local char t_thread;
#define THIS_THREAD ((const void *)&t_thread)

// The device state whose encoder this thread is finishing, and whether that finish reported an error: natively an
// encoder's error is reported inside its finish (tests/facts, `atfinish`), so the seam can mark the command buffer.
static _Thread_local gpuseam_state *t_finishing;
static _Thread_local int32_t t_finish_failed;

// Natively, above 0 while this thread makes a substitute call whose error is not the program's (gpuseam_quiet_begin).
static _Thread_local int32_t t_quiet;

// A shader module this thread is creating: natively its first error is copied here, for its compilation info, before it
// goes where every error goes (gpuseam_create_shader_module).
static _Thread_local gpuseam_report *t_observed;

// Natively an error goes to the innermost scope on its thread whose filter matches it: every error measured reaches the
// callback inside the call that caused it, on the calling thread (tests/facts, `inthecall`). A scope keeps the first
// and swallows the rest, as WebGPU's does. Past every scope, an error goes to the channel. On the web the browser's
// scopes have taken what they catch before this callback runs.
static void on_uncaptured_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message, void *u1, void *u2) {
    (void)device; (void)u2;
#ifndef __EMSCRIPTEN__
    if (t_quiet > 0) return;   // a substitute call's own error, under gpuseam_quiet_begin
#endif
    gpuseam_state *s = (gpuseam_state *)u1;
    if (t_finishing == s) t_finish_failed = 1;
    if (t_observed != NULL && t_observed->message == NULL) *t_observed = report_of(kind_of(type), message);
    gpuseam_report r = report_of(kind_of(type), message);
    state_lock(s);
#ifndef __EMSCRIPTEN__
    for (gpuseam_scope *sc = s->top; sc != NULL; sc = sc->below) {
        if (sc->thread != THIS_THREAD || (sc->filter != GPUSEAM_FILTER_ANY && sc->filter != r.kind)) continue;
        if (!sc->caught) {
            sc->error = r;
            sc->caught = 1;
        } else {
            free(r.message);
        }
        state_unlock(s);
        return;
    }
#endif
    char *line = printed_line(s, &r);
    ring_put(s, &r);
    state_unlock(s);
    print_line(line);
}

static void on_device_lost(WGPUDevice const *device, WGPUDeviceLostReason reason, WGPUStringView message, void *u1, void *u2) {
    (void)device; (void)u2;
    gpuseam_state *s = (gpuseam_state *)u1;
    // its instance gone first is not a loss anyone must handle
    if (reason != WGPUDeviceLostReason_CallbackCancelled && atomic_load_explicit(&s->lost, memory_order_acquire) == 0) {
        gpuseam_report r = report_of(GPUSEAM_KIND_UNKNOWN, message);
        s->lost_message = r.message;
        s->lost_length = r.length;
        int32_t why = reason == WGPUDeviceLostReason_Destroyed ? GPUSEAM_LOSS_DESTROYED : GPUSEAM_LOSS_UNKNOWN;
        atomic_store_explicit(&s->lost, why, memory_order_release);
    }
#ifdef __EMSCRIPTEN__
    gpuseam_state_release(s);   // the hold the lost callback kept until it fired
#endif
}

int32_t gpuseam_state_watched(gpuseam_state *s) { return s != NULL && s->watched; }

gpuseam_errors *gpuseam_state_take_errors(gpuseam_state *s) {
    if (s == NULL) return NULL;
    state_lock(s);
    int empty = s->count == 0 && s->dropped == 0;
    state_unlock(s);
    if (empty) return NULL;
    gpuseam_errors *e = (gpuseam_errors *)malloc(sizeof *e);
    if (e == NULL) return NULL;   // the reports stay in the ring for the next drain
    state_lock(s);
    e->count = s->count;
    e->dropped = s->dropped;
    for (int32_t i = 0; i < s->count; i++) e->reports[i] = s->ring[(s->head + i) % GPUSEAM_RING];
    s->head = 0;
    s->count = 0;
    s->dropped = 0;
    state_unlock(s);
    return e;
}

int32_t gpuseam_errors_count(gpuseam_errors *e) { return e == NULL ? 0 : e->count; }
int64_t gpuseam_errors_dropped(gpuseam_errors *e) { return e == NULL ? 0 : e->dropped; }
int32_t gpuseam_errors_kind(gpuseam_errors *e, int32_t at) { return e->reports[at].kind; }
int32_t gpuseam_errors_length(gpuseam_errors *e, int32_t at) { return e->reports[at].length; }
const uint8_t *gpuseam_errors_message(gpuseam_errors *e, int32_t at) { return (const uint8_t *)e->reports[at].message; }

void gpuseam_errors_free(gpuseam_errors *e) {
    if (e == NULL) return;
    for (int32_t i = 0; i < e->count; i++) free(e->reports[i].message);
    free(e);
}

// ---- a device's error scopes ---------------------------------------------------------------------------------------
//
// Natively the seam keeps them, over the uncaptured callback, so neither of wgpu-native's aborting scope paths (an
// Internal filter, a pop with none pushed) is reachable. On the web they are the browser's, and the seam keeps their
// order, so a pop out of order is refused on both legs and the browser's stack is popped only from its top.

static void scope_free(gpuseam_scope *sc) {
    free(sc->error.message);
    wgpuDeviceRelease(sc->device);
    gpuseam_state_release(sc->state);
    free(sc);
}

// The innermost scope `thread` has open on the device, under the lock.
static gpuseam_scope *innermost(gpuseam_state *s, const void *thread) {
    for (gpuseam_scope *sc = s->top; sc != NULL; sc = sc->below) {
        if (sc->thread == thread) return sc;
    }
    return NULL;
}

static void unlink_scope(gpuseam_state *s, gpuseam_scope *sc) {
    gpuseam_scope **at = &s->top;
    while (*at != sc) at = &(*at)->below;
    *at = sc->below;
    sc->below = NULL;
}

// Under the lock: pop the orphans now innermost on `thread`, so the stack never jams. What one had caught goes to the
// channel, natively now and on the web when the browser answers its pop. They are handed back in the order popped,
// linked through `below`, for the caller to finish outside the lock.
static gpuseam_scope *collapse(gpuseam_state *s, const void *thread) {
    gpuseam_scope *done = NULL;
    gpuseam_scope **tail = &done;
    for (;;) {
        gpuseam_scope *sc = innermost(s, thread);
        if (sc == NULL || !sc->orphaned) break;
        unlink_scope(s, sc);
#ifndef __EMSCRIPTEN__
        if (sc->caught) {
            print_line(printed_line(s, &sc->error));   // a scope abandoned out of order that caught an error: rare
            ring_put(s, &sc->error);
            sc->caught = 0;
        }
#endif
        *tail = sc;
        tail = &sc->below;
    }
    return done;
}

#ifdef __EMSCRIPTEN__
static WGPUErrorFilter filter_value(int32_t filter) {
    if (filter == GPUSEAM_FILTER_OUT_OF_MEMORY) return WGPUErrorFilter_OutOfMemory;
    if (filter == GPUSEAM_FILTER_INTERNAL) return WGPUErrorFilter_Internal;
    return WGPUErrorFilter_Validation;
}

static void browser_pop(gpuseam_scope *sc, WGPUPopErrorScopeCallback callback) {
    WGPUPopErrorScopeCallbackInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.mode = WGPUCallbackMode_AllowSpontaneous;
    ci.callback = callback;
    ci.userdata1 = sc;
    wgpuDevicePopErrorScope(sc->device, ci);
}

// An orphan's answer: what it caught goes to the channel.
static void on_orphan_popped(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void *u1, void *u2) {
    (void)u2;
    gpuseam_scope *sc = (gpuseam_scope *)u1;
    if (status == WGPUPopErrorScopeStatus_Success && type != WGPUErrorType_NoError) {
        gpuseam_report r = report_of(kind_of(type), message);
        state_lock(sc->state);
        char *line = printed_line(sc->state, &r);
        ring_put(sc->state, &r);
        state_unlock(sc->state);
        print_line(line);
    }
    scope_free(sc);
}

static void on_scope_popped(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void *u1, void *u2) {
    (void)u2;
    gpuseam_scope *sc = (gpuseam_scope *)u1;
    int outcome = GPUSEAM_READY;
    if (status != WGPUPopErrorScopeStatus_Success) {
        sc->error = report_of(GPUSEAM_KIND_UNKNOWN, message);
        outcome = GPUSEAM_FAILED;
    } else if (type != WGPUErrorType_NoError) {
        sc->error = report_of(kind_of(type), message);
        sc->caught = 1;
    }
    int expected = GPUSEAM_PENDING;
    if (!atomic_compare_exchange_strong_explicit(&sc->popped, &expected, outcome, memory_order_acq_rel, memory_order_acquire)) {
        scope_free(sc);   // ABANDONED
    }
}
#endif

// The orphans `collapse` handed back: freed natively, popped in the browser on the web.
static void finish_orphans(gpuseam_scope *done) {
    while (done != NULL) {
        gpuseam_scope *next = done->below;
#ifdef __EMSCRIPTEN__
        browser_pop(done, on_orphan_popped);
#else
        scope_free(done);
#endif
        done = next;
    }
}

gpuseam_scope *gpuseam_scope_push(gpuseam_state *s, WGPUDevice device, int32_t filter) {
    if (s == NULL || device == NULL) return NULL;
    gpuseam_scope *sc = (gpuseam_scope *)calloc(1, sizeof *sc);
    if (sc == NULL) return NULL;
    gpuseam_state_hold(s);
    sc->state = s;
    wgpuDeviceAddRef(device);
    sc->device = device;
    sc->thread = THIS_THREAD;
    sc->filter = filter;
    atomic_init(&sc->popped, GPUSEAM_PENDING);
#ifdef __EMSCRIPTEN__
    wgpuDevicePushErrorScope(device, filter_value(filter));
#endif
    state_lock(s);
    sc->below = s->top;
    s->top = sc;
    state_unlock(s);
    return sc;
}

int32_t gpuseam_scope_pop(gpuseam_scope *sc) {
    gpuseam_state *s = sc->state;
    state_lock(s);
    if (innermost(s, sc->thread) != sc) {
        sc->orphaned = 1;   // its holder is done with it: popped once the scopes above it are
        state_unlock(s);
        return GPUSEAM_SCOPE_OUT_OF_ORDER;
    }
    unlink_scope(s, sc);
    gpuseam_scope *done = collapse(s, sc->thread);
    state_unlock(s);
#ifdef __EMSCRIPTEN__
    browser_pop(sc, on_scope_popped);
#else
    atomic_store_explicit(&sc->popped, GPUSEAM_READY, memory_order_release);
#endif
    finish_orphans(done);
    return 0;
}

void gpuseam_scope_abandon(gpuseam_scope *sc) {
    if (sc == NULL) return;
    gpuseam_state *s = sc->state;
    state_lock(s);
    sc->orphaned = 1;
    gpuseam_scope *done = collapse(s, sc->thread);
    state_unlock(s);
    finish_orphans(done);
}

int32_t gpuseam_scope_poll(gpuseam_scope *sc) { return (int32_t)atomic_load_explicit(&sc->popped, memory_order_acquire); }
int32_t gpuseam_scope_caught(gpuseam_scope *sc) { return gpuseam_scope_poll(sc) == GPUSEAM_READY && sc->caught; }
int32_t gpuseam_scope_kind(gpuseam_scope *sc) { return gpuseam_scope_poll(sc) == GPUSEAM_PENDING ? 0 : sc->error.kind; }
int32_t gpuseam_scope_length(gpuseam_scope *sc) {
    return gpuseam_scope_poll(sc) == GPUSEAM_PENDING ? 0 : sc->error.length;
}
const uint8_t *gpuseam_scope_message(gpuseam_scope *sc) {
    return gpuseam_scope_poll(sc) == GPUSEAM_PENDING ? NULL : (const uint8_t *)sc->error.message;
}

void gpuseam_scope_drop_popped(gpuseam_scope *sc) {
    if (sc == NULL) return;
    int expected = GPUSEAM_PENDING;
    if (atomic_compare_exchange_strong_explicit(&sc->popped, &expected, GPUSEAM_ABANDONED, memory_order_acq_rel, memory_order_acquire)) {
        return;   // the web's answer is still owed, and frees the scope when it comes
    }
    scope_free(sc);
}

int32_t gpuseam_state_lost(gpuseam_state *s) {
    return s == NULL ? 0 : (int32_t)atomic_load_explicit(&s->lost, memory_order_acquire);
}
int32_t gpuseam_state_lost_length(gpuseam_state *s) { return gpuseam_state_lost(s) != 0 ? s->lost_length : 0; }
const uint8_t *gpuseam_state_lost_message(gpuseam_state *s) {
    return gpuseam_state_lost(s) != 0 ? (const uint8_t *)s->lost_message : NULL;
}

// ---- an adapter on its way ----------------------------------------------------------------------------------------

struct gpuseam_adapter_request {
    atomic_int state;
    WGPUInstance instance;   // a reference held while the slot lives, for its events
    WGPUAdapter adapter;
    char message[GPUSEAM_MESSAGE_BYTES];
    int32_t message_length;
};

static void adapter_request_free(gpuseam_adapter_request *r) {
    if (r->adapter != NULL) wgpuAdapterRelease(r->adapter);
    if (r->instance != NULL) wgpuInstanceRelease(r->instance);
    free(r);
}

static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *u1, void *u2) {
    (void)u2;
    gpuseam_adapter_request *r = (gpuseam_adapter_request *)u1;
    int outcome = GPUSEAM_READY;
    if (status != WGPURequestAdapterStatus_Success || adapter == NULL) {
        r->message_length = keep_message(message, r->message);
        if (r->message_length == 0) r->message_length = keep_text("WebGPU offered no adapter", r->message);
        outcome = status == WGPURequestAdapterStatus_Unavailable ? GPUSEAM_UNAVAILABLE : GPUSEAM_FAILED;
    } else {
        r->adapter = adapter;
    }
    int expected = GPUSEAM_PENDING;
    if (!atomic_compare_exchange_strong_explicit(&r->state, &expected, outcome, memory_order_acq_rel, memory_order_acquire)) {
        adapter_request_free(r);   // ABANDONED
    }
}

gpuseam_adapter_request *gpuseam_request_adapter(WGPUInstance instance, WGPUSurface compatible, int32_t power) {
    gpuseam_adapter_request *r = (gpuseam_adapter_request *)calloc(1, sizeof *r);
    if (r == NULL) return NULL;
    atomic_init(&r->state, GPUSEAM_PENDING);
    wgpuInstanceAddRef(instance);
    r->instance = instance;
    WGPURequestAdapterOptions opts;
    memset(&opts, 0, sizeof opts);
    opts.powerPreference = (WGPUPowerPreference)power;
    opts.compatibleSurface = compatible;
    WGPURequestAdapterCallbackInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.mode = WGPUCallbackMode_AllowSpontaneous;
    ci.callback = on_adapter;
    ci.userdata1 = r;
    wgpuInstanceRequestAdapter(instance, &opts, ci);
    return r;
}

int32_t gpuseam_adapter_request_poll(gpuseam_adapter_request *r) {
    int state = atomic_load_explicit(&r->state, memory_order_acquire);
    if (state == GPUSEAM_PENDING) {
        wgpuInstanceProcessEvents(r->instance);
        state = atomic_load_explicit(&r->state, memory_order_acquire);
    }
    return (int32_t)state;
}

WGPUAdapter gpuseam_adapter_request_take(gpuseam_adapter_request *r) {
    if (atomic_load_explicit(&r->state, memory_order_acquire) != GPUSEAM_READY) return NULL;
    WGPUAdapter a = r->adapter;
    r->adapter = NULL;
    return a;
}

int32_t gpuseam_adapter_request_message(gpuseam_adapter_request *r, uint8_t *dst, int32_t capacity) {
    int state = atomic_load_explicit(&r->state, memory_order_acquire);
    if (state != GPUSEAM_FAILED && state != GPUSEAM_UNAVAILABLE) return 0;
    return copy_out(r->message, r->message_length, dst, capacity);
}

void gpuseam_adapter_request_drop(gpuseam_adapter_request *r) {
    if (r == NULL) return;
    int expected = GPUSEAM_PENDING;
    if (atomic_compare_exchange_strong_explicit(&r->state, &expected, GPUSEAM_ABANDONED, memory_order_acq_rel, memory_order_acquire)) {
        return;   // a callback is still owed, and frees the slot when it fires
    }
    adapter_request_free(r);
}

// ---- what an adapter answers -----------------------------------------------------------------------------------------

int32_t gpuseam_adapter_info_text(WGPUAdapter adapter, int32_t which, uint8_t *dst, int32_t capacity) {
    WGPUAdapterInfo info;
    memset(&info, 0, sizeof info);
    if (wgpuAdapterGetInfo(adapter, &info) != WGPUStatus_Success) return 0;
    WGPUStringView v = which == 0 ? info.vendor : which == 1 ? info.architecture : which == 2 ? info.device : info.description;
    char kept[GPUSEAM_MESSAGE_BYTES];
    int32_t n = keep_message(v, kept);
    wgpuAdapterInfoFreeMembers(info);
    return copy_out(kept, n, dst, capacity);
}

void gpuseam_adapter_info_numbers(WGPUAdapter adapter, int32_t *backend, int32_t *kind, uint32_t *vendor_id,
                                  uint32_t *device_id) {
    WGPUAdapterInfo info;
    memset(&info, 0, sizeof info);
    *backend = 0;
    *kind = 0;
    *vendor_id = 0;
    *device_id = 0;
    if (wgpuAdapterGetInfo(adapter, &info) != WGPUStatus_Success) return;
    *backend = (int32_t)info.backendType;
    *kind = (int32_t)info.adapterType;
    *vendor_id = info.vendorID;
    *device_id = info.deviceID;
    wgpuAdapterInfoFreeMembers(info);
}

static int32_t copy_features(WGPUSupportedFeatures *f, int32_t *dst, int32_t capacity) {
    int32_t n = (int32_t)f->featureCount;
    for (int32_t i = 0; i < n && i < capacity; i++) dst[i] = (int32_t)f->features[i];
    wgpuSupportedFeaturesFreeMembers(*f);
    return n;
}

int32_t gpuseam_adapter_features(WGPUAdapter adapter, int32_t *dst, int32_t capacity) {
    WGPUSupportedFeatures f;
    memset(&f, 0, sizeof f);
    wgpuAdapterGetFeatures(adapter, &f);
    return copy_features(&f, dst, capacity);
}

int32_t gpuseam_device_features(WGPUDevice device, int32_t *dst, int32_t capacity) {
    WGPUSupportedFeatures f;
    memset(&f, 0, sizeof f);
    wgpuDeviceGetFeatures(device, &f);
    return copy_features(&f, dst, capacity);
}

// ---- whether an adapter has given a device ---------------------------------------------------------------------------

struct gpuseam_adapter_mark {
    atomic_int holders;
    atomic_int consumed;
};

gpuseam_adapter_mark *gpuseam_adapter_mark_new(void) {
    gpuseam_adapter_mark *m = (gpuseam_adapter_mark *)calloc(1, sizeof *m);
    if (m == NULL) return NULL;
    atomic_init(&m->holders, 1);
    atomic_init(&m->consumed, 0);
    return m;
}

void gpuseam_adapter_mark_hold(gpuseam_adapter_mark *m) {
    if (m != NULL) atomic_fetch_add_explicit(&m->holders, 1, memory_order_relaxed);
}

void gpuseam_adapter_mark_release(gpuseam_adapter_mark *m) {
    if (m == NULL) return;
    if (atomic_fetch_sub_explicit(&m->holders, 1, memory_order_acq_rel) == 1) free(m);
}

int32_t gpuseam_adapter_mark_consumed(gpuseam_adapter_mark *m) {
    return m != NULL && atomic_load_explicit(&m->consumed, memory_order_acquire) != 0;
}

// ---- a refusal reported, a substitute call quieted ------------------------------------------------------------------

// A carrier for the refusal `message`: a 4-byte buffer made with `usage` and labelled with the refusal's id, its words
// kept for `report_of`. NULL when there is no device, no words, or no memory for them.
static WGPUBuffer carrier_of(WGPUDevice device, const uint8_t *message, int32_t length, WGPUBufferUsage usage) {
    if (device == NULL || message == NULL || length <= 0) return NULL;
    char *copy = (char *)malloc((size_t)length);
    if (copy == NULL) return NULL;
    memcpy(copy, message, (size_t)length);
    carried_lock();
    uint32_t id = g_carried_next++;
    if (g_carried_next == 0) g_carried_next = 1;
    free(g_carried[id % GPUSEAM_CARRIED].message);   // a refusal its device never reported, 64 refusals ago
    g_carried[id % GPUSEAM_CARRIED].id = id;
    g_carried[id % GPUSEAM_CARRIED].message = copy;
    g_carried[id % GPUSEAM_CARRIED].length = length;
    carried_unlock();
    char label[40];
    int n = snprintf(label, sizeof label, "%s%u", CARRIER_PREFIX, id);
    WGPUBufferDescriptor d;
    memset(&d, 0, sizeof d);
    d.label.data = label;
    d.label.length = n > 0 ? (size_t)n : 0;
    d.size = 4;
    d.usage = usage;
    return wgpuDeviceCreateBuffer(device, &d);
}

void gpuseam_refuse(WGPUDevice device, const uint8_t *message, int32_t length) {
    // a buffer with no usage, which the device refuses as it is made
    WGPUBuffer carrier = carrier_of(device, message, length, WGPUBufferUsage_None);
    if (carrier != NULL) wgpuBufferRelease(carrier);
}

// Inside a pass the refused command never reaches the backend, and the carrier takes its place as vertex buffer 0: a
// valid buffer made for copies alone, so its usage error is found in command order on both legs, as every other error
// in the pass is, and lands at `finish` unless an earlier one did (kyoten's .scratch/gpu4-part4/FINDINGS.md: an
// invalid carrier is reported first natively, whatever came before it).
void gpuseam_refuse_in_pass(WGPURenderPassEncoder pass, WGPUDevice device, const uint8_t *message, int32_t length) {
    if (pass == NULL) return;
    WGPUBuffer carrier = carrier_of(device, message, length, WGPUBufferUsage_CopyDst);
    if (carrier == NULL) return;
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, carrier, 0, WGPU_WHOLE_SIZE);
    wgpuBufferRelease(carrier);
}

#ifdef __EMSCRIPTEN__
static void on_quiet_popped(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void *u1, void *u2) {
    (void)status; (void)type; (void)message; (void)u1; (void)u2;
}
#endif

void gpuseam_quiet_begin(WGPUDevice device) {
#ifdef __EMSCRIPTEN__
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
#else
    (void)device;
    t_quiet += 1;
#endif
}

void gpuseam_quiet_end(WGPUDevice device) {
#ifdef __EMSCRIPTEN__
    WGPUPopErrorScopeCallbackInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.mode = WGPUCallbackMode_AllowSpontaneous;
    ci.callback = on_quiet_popped;
    wgpuDevicePopErrorScope(device, ci);
#else
    (void)device;
    t_quiet -= 1;
#endif
}

void gpuseam_state_keep_queue(gpuseam_state *s, WGPUQueue queue) {
#ifndef __EMSCRIPTEN__
    if (s == NULL || queue == NULL || s->queue != NULL) return;
    wgpuQueueAddRef(queue);
    s->queue = queue;
#else
    (void)s; (void)queue;
#endif
}

// ---- a device on its way ---------------------------------------------------------------------------------------------

struct gpuseam_device_request {
    atomic_int state;
    WGPUInstance instance;          // the adapter's instance, for its events, or NULL; a reference held while the slot lives
    WGPUAdapter adapter;            // a reference held while the slot lives
    gpuseam_adapter_mark *mark;     // the adapter's, held while the slot lives
    WGPUDevice device;
    gpuseam_state *errors;          // the device's state, held until the device is taken
    int32_t consumed;               // failed because the adapter had given a device
    char message[GPUSEAM_MESSAGE_BYTES];
    int32_t message_length;
};

static void request_free(gpuseam_device_request *r) {
    if (r->device != NULL) wgpuDeviceRelease(r->device);
    if (r->adapter != NULL) wgpuAdapterRelease(r->adapter);
    if (r->instance != NULL) wgpuInstanceRelease(r->instance);
    gpuseam_adapter_mark_release(r->mark);
    gpuseam_state_release(r->errors);
    free(r);
}

static const char CONSUMED_WORDS[] = "the adapter has already given a device: WebGPU gives an adapter one, so request another adapter";

#if defined(__EMSCRIPTEN__)
// js/gpuseam.js: try WebGPU's three canvas formats on a canvas of the seam's own with `device`, once a page.
extern void gpuseam_web_learn_canvas_formats(WGPUDevice device);
#endif

static void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *u1, void *u2) {
    (void)u2;
    gpuseam_device_request *r = (gpuseam_device_request *)u1;
    int outcome = GPUSEAM_READY;
    if (status != WGPURequestDeviceStatus_Success || device == NULL) {
        // A request that settles after another of the same adapter was granted fails as consumed, whatever the
        // implementation's words: a browser refuses it as such.
        if (gpuseam_adapter_mark_consumed(r->mark)) {
            r->consumed = 1;
            r->message_length = keep_text(CONSUMED_WORDS, r->message);
        } else {
            r->message_length = keep_message(message, r->message);
            if (r->message_length == 0) r->message_length = keep_text("the adapter gave no device", r->message);
        }
        outcome = GPUSEAM_FAILED;
    } else if (r->mark != NULL && atomic_exchange_explicit(&r->mark->consumed, 1, memory_order_acq_rel) != 0) {
        // Granted where another request of the adapter was granted first: WebGPU would have refused it.
        wgpuDeviceRelease(device);
        r->consumed = 1;
        r->message_length = keep_text(CONSUMED_WORDS, r->message);
        outcome = GPUSEAM_FAILED;
    } else {
        r->device = device;
#if defined(__EMSCRIPTEN__)
        gpuseam_web_learn_canvas_formats(device);   // which canvas formats the browser takes, asked once a device exists
#endif
    }
    int expected = GPUSEAM_PENDING;
    if (!atomic_compare_exchange_strong_explicit(&r->state, &expected, outcome, memory_order_acq_rel, memory_order_acquire)) {
        request_free(r);   // ABANDONED
    }
}

gpuseam_device_request *gpuseam_adapter_request_device(WGPUInstance instance, WGPUAdapter adapter,
                                                       gpuseam_adapter_mark *mark, const WGPULimits *limits,
                                                       const int32_t *features, int32_t feature_count,
                                                       int32_t print_errors) {
    gpuseam_device_request *r = (gpuseam_device_request *)calloc(1, sizeof *r);
    if (r == NULL) return NULL;
    gpuseam_adapter_mark_hold(mark);
    r->mark = mark;
    if (gpuseam_adapter_mark_consumed(mark)) {   // refused without asking WebGPU, as a browser refuses it
        r->consumed = 1;
        r->message_length = keep_text(CONSUMED_WORDS, r->message);
        atomic_init(&r->state, GPUSEAM_FAILED);
        return r;
    }
    r->errors = state_new(1);
    if (r->errors == NULL) { gpuseam_adapter_mark_release(mark); free(r); return NULL; }
    r->errors->print = print_errors != 0;
#ifdef __EMSCRIPTEN__
    gpuseam_state_hold(r->errors);   // the lost callback's, released when it fires
#endif
    atomic_init(&r->state, GPUSEAM_PENDING);
    if (instance != NULL) wgpuInstanceAddRef(instance);
    r->instance = instance;
    wgpuAdapterAddRef(adapter);
    r->adapter = adapter;
    WGPUDeviceDescriptor dd;
    memset(&dd, 0, sizeof dd);
    dd.requiredLimits = limits;
    dd.requiredFeatureCount = feature_count > 0 ? (size_t)feature_count : 0;
    dd.requiredFeatures = (const WGPUFeatureName *)features;
    dd.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    dd.deviceLostCallbackInfo.callback = on_device_lost;
    dd.deviceLostCallbackInfo.userdata1 = r->errors;
    dd.uncapturedErrorCallbackInfo.callback = on_uncaptured_error;
    dd.uncapturedErrorCallbackInfo.userdata1 = r->errors;
    WGPURequestDeviceCallbackInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.mode = WGPUCallbackMode_AllowSpontaneous;
    ci.callback = on_device;
    ci.userdata1 = r;
    wgpuAdapterRequestDevice(adapter, &dd, ci);
    return r;
}

int32_t gpuseam_device_request_poll(gpuseam_device_request *r) {
    int state = atomic_load_explicit(&r->state, memory_order_acquire);
    if (state == GPUSEAM_PENDING && r->instance != NULL) {
        wgpuInstanceProcessEvents(r->instance);
        state = atomic_load_explicit(&r->state, memory_order_acquire);
    }
    return (int32_t)state;
}

WGPUDevice gpuseam_device_request_take_device(gpuseam_device_request *r) {
    if (atomic_load_explicit(&r->state, memory_order_acquire) != GPUSEAM_READY) return NULL;
    WGPUDevice d = r->device;
    r->device = NULL;
    return d;
}

gpuseam_state *gpuseam_device_request_take_state(gpuseam_device_request *r) {
    if (atomic_load_explicit(&r->state, memory_order_acquire) != GPUSEAM_READY) return NULL;
    gpuseam_state *s = r->errors;
    r->errors = NULL;
    return s;
}

int32_t gpuseam_device_request_message(gpuseam_device_request *r, uint8_t *dst, int32_t capacity) {
    if (atomic_load_explicit(&r->state, memory_order_acquire) != GPUSEAM_FAILED) return 0;
    return copy_out(r->message, r->message_length, dst, capacity);
}

int32_t gpuseam_device_request_consumed(gpuseam_device_request *r) {
    if (atomic_load_explicit(&r->state, memory_order_acquire) != GPUSEAM_FAILED) return 0;
    return r->consumed;
}

void gpuseam_device_request_drop(gpuseam_device_request *r) {
    if (r == NULL) return;
    int expected = GPUSEAM_PENDING;
    if (atomic_compare_exchange_strong_explicit(&r->state, &expected, GPUSEAM_ABANDONED, memory_order_acq_rel, memory_order_acquire)) {
        return;   // a callback is still owed, and frees the slot when it fires
    }
    request_free(r);
}

// ---- a buffer map ----------------------------------------------------------------------------------------------------

struct gpuseam_map {
    atomic_int state;
    WGPUBuffer buffer;   // a reference held while the slot lives
    size_t offset;
    size_t size;
    char message[GPUSEAM_MESSAGE_BYTES];
    int32_t message_length;
};

static void map_free(gpuseam_map *m) {
    wgpuBufferRelease(m->buffer);
    free(m);
}

static void on_mapped(WGPUMapAsyncStatus status, WGPUStringView message, void *u1, void *u2) {
    (void)u2;
    gpuseam_map *m = (gpuseam_map *)u1;
    int outcome = GPUSEAM_READY;
    if (status != WGPUMapAsyncStatus_Success) {
        m->message_length = keep_message(message, m->message);
        if (m->message_length == 0) m->message_length = keep_text("the buffer could not be mapped", m->message);
        outcome = GPUSEAM_FAILED;
    }
    int expected = GPUSEAM_PENDING;
    if (!atomic_compare_exchange_strong_explicit(&m->state, &expected, outcome, memory_order_acq_rel, memory_order_acquire)) {
        if (outcome == GPUSEAM_READY) wgpuBufferUnmap(m->buffer);
        map_free(m);   // ABANDONED
    }
}

gpuseam_map *gpuseam_map_async(WGPUBuffer buffer, gpuseam_state *state, uint64_t mode, size_t offset, size_t size) {
    gpuseam_map *m = (gpuseam_map *)calloc(1, sizeof *m);
    if (m == NULL) return NULL;
#ifndef __EMSCRIPTEN__
    // wgpu-native keeps a queue's writes until its next submit, where WebGPU has a map see every write made before it
    // (kyoten's .scratch/gpu4-part2: a map with no submit between read the old bytes), so an empty submit sends them.
    if (state != NULL && state->queue != NULL) wgpuQueueSubmit(state->queue, 0, NULL);
#else
    (void)state;
#endif
    atomic_init(&m->state, GPUSEAM_PENDING);
    wgpuBufferAddRef(buffer);
    m->buffer = buffer;
    m->offset = offset;
    m->size = size;
    WGPUBufferMapCallbackInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.mode = WGPUCallbackMode_AllowSpontaneous;
    ci.callback = on_mapped;
    ci.userdata1 = m;
    wgpuBufferMapAsync(buffer, (WGPUMapMode)mode, offset, size, ci);
    return m;
}

int32_t gpuseam_map_poll(gpuseam_map *m) { return (int32_t)atomic_load_explicit(&m->state, memory_order_acquire); }

int32_t gpuseam_map_message(gpuseam_map *m, uint8_t *dst, int32_t capacity) {
    if (atomic_load_explicit(&m->state, memory_order_acquire) != GPUSEAM_FAILED) return 0;
    return copy_out(m->message, m->message_length, dst, capacity);
}

const uint8_t *gpuseam_map_bytes(gpuseam_map *m) {
    if (atomic_load_explicit(&m->state, memory_order_acquire) != GPUSEAM_READY) return NULL;
    return (const uint8_t *)wgpuBufferGetConstMappedRange(m->buffer, m->offset, m->size);
}

void gpuseam_map_drop(gpuseam_map *m) {
    if (m == NULL) return;
    int expected = GPUSEAM_PENDING;
    if (atomic_compare_exchange_strong_explicit(&m->state, &expected, GPUSEAM_ABANDONED, memory_order_acq_rel, memory_order_acquire)) {
        return;   // the map's callback is still owed, and frees the slot when it fires
    }
    if (expected == GPUSEAM_READY) wgpuBufferUnmap(m->buffer);
    map_free(m);
}

// ---- a shader module and its compilation messages -------------------------------------------------------------------

WGPUShaderModule gpuseam_create_shader_module(WGPUDevice device, const uint8_t *code, int32_t code_length,
                                              const uint8_t *label, int32_t label_length, uint8_t **observed,
                                              int32_t *observed_length) {
    WGPUShaderSourceWGSL wgsl;
    memset(&wgsl, 0, sizeof wgsl);
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code.data = (const char *)code;
    wgsl.code.length = code_length > 0 ? (size_t)code_length : 0;
    WGPUShaderModuleDescriptor d;
    memset(&d, 0, sizeof d);
    d.nextInChain = &wgsl.chain;
    d.label.data = (const char *)label;
    d.label.length = label_length > 0 ? (size_t)label_length : 0;
    *observed = NULL;
    *observed_length = 0;
    gpuseam_report seen = { 0, NULL, 0 };
    gpuseam_report *outer = t_observed;
    t_observed = &seen;
    WGPUShaderModule m = wgpuDeviceCreateShaderModule(device, &d);
    t_observed = outer;
    *observed = (uint8_t *)seen.message;
    *observed_length = seen.length;
    return m;
}

void gpuseam_free_bytes(uint8_t *bytes) { free(bytes); }

typedef struct {
    int32_t type;
    uint64_t line, line_pos, offset, length;
    char *text;
    int32_t text_length;
} gpuseam_compilation_entry;

struct gpuseam_compilation {
    atomic_int state;
    WGPUShaderModule module;   // a reference held while the slot lives
    gpuseam_compilation_entry *entries;
    int32_t count;
    char message[GPUSEAM_MESSAGE_BYTES];
    int32_t message_length;
};

static void compilation_free(gpuseam_compilation *c) {
    for (int32_t i = 0; i < c->count; i++) free(c->entries[i].text);
    free(c->entries);
    if (c->module != NULL) wgpuShaderModuleRelease(c->module);
    free(c);
}

#ifdef __EMSCRIPTEN__
static void on_compilation_info(WGPUCompilationInfoRequestStatus status, WGPUCompilationInfo const *info, void *u1,
                                void *u2) {
    (void)u2;
    gpuseam_compilation *c = (gpuseam_compilation *)u1;
    int outcome = GPUSEAM_READY;
    if (status != WGPUCompilationInfoRequestStatus_Success || info == NULL) {
        c->message_length = keep_text("the shader module's compilation messages were not answered", c->message);
        outcome = GPUSEAM_FAILED;
    } else if (info->messageCount > 0) {
        c->entries = (gpuseam_compilation_entry *)calloc(info->messageCount, sizeof *c->entries);
        if (c->entries == NULL) {
            c->message_length = keep_text("out of memory for the compilation messages", c->message);
            outcome = GPUSEAM_FAILED;
        } else {
            for (size_t i = 0; i < info->messageCount; i++) {
                const WGPUCompilationMessage *m = &info->messages[i];
                gpuseam_compilation_entry *e = &c->entries[i];
                e->type = (int32_t)m->type;
                e->line = m->lineNum;
                e->line_pos = m->linePos;
                e->offset = m->offset;
                e->length = m->length;
                gpuseam_report r = report_of(0, m->message);
                e->text = r.message;
                e->text_length = r.length;
            }
            c->count = (int32_t)info->messageCount;
        }
    }
    int expected = GPUSEAM_PENDING;
    if (!atomic_compare_exchange_strong_explicit(&c->state, &expected, outcome, memory_order_acq_rel, memory_order_acquire)) {
        compilation_free(c);   // ABANDONED
    }
}
#endif

gpuseam_compilation *gpuseam_compilation_request(WGPUShaderModule module) {
#ifdef __EMSCRIPTEN__
    gpuseam_compilation *c = (gpuseam_compilation *)calloc(1, sizeof *c);
    if (c == NULL) return NULL;
    atomic_init(&c->state, GPUSEAM_PENDING);
    wgpuShaderModuleAddRef(module);
    c->module = module;
    WGPUCompilationInfoCallbackInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.mode = WGPUCallbackMode_AllowSpontaneous;
    ci.callback = on_compilation_info;
    ci.userdata1 = c;
    wgpuShaderModuleGetCompilationInfo(module, ci);
    return c;
#else
    (void)module;
    return NULL;
#endif
}

int32_t gpuseam_compilation_poll(gpuseam_compilation *c) {
    return (int32_t)atomic_load_explicit(&c->state, memory_order_acquire);
}

int32_t gpuseam_compilation_count(gpuseam_compilation *c) {
    return gpuseam_compilation_poll(c) == GPUSEAM_READY ? c->count : 0;
}

void gpuseam_compilation_place(gpuseam_compilation *c, int32_t at, int32_t *type, uint64_t *line, uint64_t *line_pos,
                               uint64_t *offset, uint64_t *length) {
    const gpuseam_compilation_entry *e = &c->entries[at];
    *type = e->type;
    *line = e->line;
    *line_pos = e->line_pos;
    *offset = e->offset;
    *length = e->length;
}

int32_t gpuseam_compilation_text_length(gpuseam_compilation *c, int32_t at) { return c->entries[at].text_length; }
const uint8_t *gpuseam_compilation_text(gpuseam_compilation *c, int32_t at) { return (const uint8_t *)c->entries[at].text; }

int32_t gpuseam_compilation_message(gpuseam_compilation *c, uint8_t *dst, int32_t capacity) {
    if (gpuseam_compilation_poll(c) != GPUSEAM_FAILED) return 0;
    return copy_out(c->message, c->message_length, dst, capacity);
}

void gpuseam_compilation_drop(gpuseam_compilation *c) {
    if (c == NULL) return;
    int expected = GPUSEAM_PENDING;
    if (atomic_compare_exchange_strong_explicit(&c->state, &expected, GPUSEAM_ABANDONED, memory_order_acq_rel, memory_order_acquire)) {
        return;   // the browser's answer is still owed, and frees the slot when it comes
    }
    compilation_free(c);
}

// ---- finishing an encoder -------------------------------------------------------------------------------------------

WGPUCommandBuffer gpuseam_encoder_finish(WGPUCommandEncoder encoder, gpuseam_state *state, int32_t *failed) {
    gpuseam_state *outer = t_finishing;
    int32_t outer_failed = t_finish_failed;
    t_finishing = state;
    t_finish_failed = 0;
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    *failed = state != NULL && t_finish_failed;
    t_finishing = outer;
    t_finish_failed = outer_failed;
    return commands;
}

// ---- driving the callbacks -------------------------------------------------------------------------------------------

void gpuseam_device_poll(WGPUDevice device, int32_t wait) {
#ifndef __EMSCRIPTEN__
    wgpuDevicePoll(device, wait ? 1u : 0u, NULL);
#else
    (void)device; (void)wait;
#endif
}

// ---- numbers the kama side writes, held to the header ---------------------------------------------------------------

// A 2D color attachment's depthSlice is WGPU_DEPTH_SLICE_UNDEFINED, a #define the generated bindings never see.
_Static_assert(WGPU_DEPTH_SLICE_UNDEFINED == 0xFFFFFFFFu, "gpu.kama's DEPTH_SLICE_UNDEFINED");
// A limit a request leaves to WebGPU's default is WGPU_LIMIT_U32_UNDEFINED or _U64_UNDEFINED, #defines likewise unseen.
_Static_assert(WGPU_LIMIT_U32_UNDEFINED == 0xFFFFFFFFu, "gpu.kama's LIMIT_U32_UNDEFINED");
_Static_assert(WGPU_LIMIT_U64_UNDEFINED == 0xFFFFFFFFFFFFFFFFull, "gpu.kama's LIMIT_U64_UNDEFINED");
// A view's "the rest" of the mip levels and of the array layers, #defines likewise unseen.
_Static_assert(WGPU_MIP_LEVEL_COUNT_UNDEFINED == 0xFFFFFFFFu, "gpu.kama's MIP_LEVEL_COUNT_UNDEFINED");
_Static_assert(WGPU_ARRAY_LAYER_COUNT_UNDEFINED == 0xFFFFFFFFu, "gpu.kama's ARRAY_LAYER_COUNT_UNDEFINED");

// ---- a surface -------------------------------------------------------------------------------------------------------

#if defined(__EMSCRIPTEN__)
// js/gpuseam.js: 1 when the canvas the selector names gives a WebGPU context.
extern int32_t gpuseam_web_canvas_has_webgpu(const char *selector);
// js/gpuseam.js: emdawnwebgpu's configure, its exception caught: -1, or the length of the words it threw.
extern int32_t gpuseam_web_surface_configure(WGPUSurface surface, const WGPUSurfaceConfiguration *config, uint8_t *words,
                                             int32_t capacity);
// js/gpuseam.js: 0 when the browser refused `format` for a canvas of the seam's own, else 1.
extern int32_t gpuseam_web_canvas_takes(int32_t format);
#endif

int32_t gpuseam_surface_configure(WGPUSurface surface, const WGPUSurfaceConfiguration *config, uint8_t *words, int32_t capacity) {
#if defined(__EMSCRIPTEN__)
    return gpuseam_web_surface_configure(surface, config, words, capacity);
#else
    (void)words; (void)capacity;
    wgpuSurfaceConfigure(surface, config);
    return -1;
#endif
}

int32_t gpuseam_surface_format_taken(int32_t format) {
#if defined(__EMSCRIPTEN__)
    return gpuseam_web_canvas_takes(format);
#else
    (void)format;
    return 1;
#endif
}

WGPUSurface gpuseam_create_surface(WGPUInstance instance, int32_t kind, void *display, void *window) {
    WGPUSurfaceDescriptor d;
    memset(&d, 0, sizeof d);
#if defined(__EMSCRIPTEN__)
    (void)display;
    if (kind != 5 || window == NULL) return NULL;
    // A canvas the browser gives no WebGPU context is refused here: emdawnwebgpu asserts one, ending the program.
    if (!gpuseam_web_canvas_has_webgpu((const char *)window)) return NULL;
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas;
    memset(&canvas, 0, sizeof canvas);
    canvas.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvas.selector.data = (const char *)window;
    canvas.selector.length = WGPU_STRLEN;
    d.nextInChain = &canvas.chain;
    return wgpuInstanceCreateSurface(instance, &d);
#elif defined(_WIN32)
    if (kind != 1 || window == NULL) return NULL;
    WGPUSurfaceSourceWindowsHWND hwnd;
    memset(&hwnd, 0, sizeof hwnd);
    hwnd.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    hwnd.hinstance = display;
    hwnd.hwnd = window;
    d.nextInChain = &hwnd.chain;
    return wgpuInstanceCreateSurface(instance, &d);
#else
    (void)instance; (void)kind; (void)display; (void)window;
    return NULL;   // X11, Wayland and Metal come with their legs
#endif
}

#ifndef __EMSCRIPTEN__
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif
static void sleep_ms(int32_t ms) {
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}
#endif

int32_t gpuseam_surface_acquire(WGPUSurface surface, void **texture, int32_t *suboptimal) {
    WGPUSurfaceTexture st;
    memset(&st, 0, sizeof st);
    wgpuSurfaceGetCurrentTexture(surface, &st);
    *texture = NULL;
    *suboptimal = 0;
    switch ((int)st.status) {
        case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
        case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
            *texture = (void *)st.texture;
            *suboptimal = st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
            return GPUSEAM_SURFACE_OK;
        case WGPUSurfaceGetCurrentTextureStatus_Timeout:
            if (st.texture != NULL) wgpuTextureRelease(st.texture);
            return GPUSEAM_SURFACE_TIMEOUT;
        case WGPUSurfaceGetCurrentTextureStatus_Outdated:
            if (st.texture != NULL) wgpuTextureRelease(st.texture);
            return GPUSEAM_SURFACE_OUTDATED;
        case WGPUSurfaceGetCurrentTextureStatus_Lost:
            if (st.texture != NULL) wgpuTextureRelease(st.texture);
            return GPUSEAM_SURFACE_LOST;
#ifndef __EMSCRIPTEN__
        case WGPUSurfaceGetCurrentTextureStatus_Occluded:
            sleep_ms(32);
            return GPUSEAM_SURFACE_OCCLUDED;
#endif
        default:
            if (st.texture != NULL) wgpuTextureRelease(st.texture);
            return GPUSEAM_SURFACE_ERROR;
    }
}

void gpuseam_surface_present(WGPUSurface surface) {
#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(surface);
#else
    (void)surface;
#endif
}

WGPUTextureView gpuseam_texture_view_in(WGPUTexture texture, int32_t format) {
    WGPUTextureViewDescriptor d = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    d.format = (WGPUTextureFormat)format;
    return wgpuTextureCreateView(texture, &d);
}

// ---- render pipelines made asynchronously (GPU-4 part 3, step 4; AGENTS.gpu.md item 12, decisions 2 and 3) ----------
//
// One request slot on both legs. On the web, emdawnwebgpu's createRenderPipelineAsync answers it through on_pipeline.
// Natively, wgpu-native's asynchronous call ends the process (tests/facts), so the package compiles on isolates of its
// own: the request hands back a job, a deep copy of the description, which the kama side sends to its compile pool, and
// a worker makes the pipeline under a scope of its own thread that catches any error, then publishes the slot. Either
// way the failure lands in the slot alone, never in the device's channel, as the browser's does.

struct gpuseam_pipeline {
    atomic_int state;               // PENDING, READY, FAILED or ABANDONED
    WGPURenderPipeline pipeline;    // READY: the slot's reference
    gpuseam_report error;           // FAILED: its kind and words, whole
};

static atomic_llong g_pipelines_alive;   // slots and jobs, for the tests (tests/src/hooks.kama)

static gpuseam_pipeline *pipeline_new(void) {
    gpuseam_pipeline *p = (gpuseam_pipeline *)calloc(1, sizeof *p);
    if (p == NULL) return NULL;
    atomic_init(&p->state, GPUSEAM_PENDING);
    atomic_fetch_add_explicit(&g_pipelines_alive, 1, memory_order_relaxed);
    return p;
}

static void pipeline_free(gpuseam_pipeline *p) {
    if (p->pipeline != NULL) wgpuRenderPipelineRelease(p->pipeline);
    free(p->error.message);
    free(p);
    atomic_fetch_sub_explicit(&g_pipelines_alive, 1, memory_order_relaxed);
}

// READY or FAILED, published once what it carries is written; a slot its request abandoned is freed instead.
static void pipeline_publish(gpuseam_pipeline *p, int outcome) {
    int expected = GPUSEAM_PENDING;
    if (!atomic_compare_exchange_strong_explicit(&p->state, &expected, outcome, memory_order_acq_rel, memory_order_acquire)) {
        pipeline_free(p);
    }
}

static void pipeline_fail(gpuseam_pipeline *p, int32_t kind, const char *words, size_t length) {
    p->error.kind = kind;
    p->error.message = length > 0 ? (char *)malloc(length) : NULL;
    if (p->error.message != NULL) {
        memcpy(p->error.message, words, length);
        p->error.length = (int32_t)length;
    }
}

#ifdef __EMSCRIPTEN__
static void on_pipeline(WGPUCreatePipelineAsyncStatus status, WGPURenderPipeline pipeline, WGPUStringView message,
                        void *u1, void *u2) {
    (void)u2;
    gpuseam_pipeline *p = (gpuseam_pipeline *)u1;
    if (status == WGPUCreatePipelineAsyncStatus_Success && pipeline != NULL) {
        p->pipeline = pipeline;
        pipeline_publish(p, GPUSEAM_READY);
        return;
    }
    if (pipeline != NULL) wgpuRenderPipelineRelease(pipeline);
    p->error = report_of(status == WGPUCreatePipelineAsyncStatus_ValidationError ? GPUSEAM_KIND_VALIDATION
                                                                                : GPUSEAM_KIND_INTERNAL, message);
    pipeline_publish(p, GPUSEAM_FAILED);
}
#else
// A job: the description, deep-copied into one allocation with every array and string it points to, and a reference on
// each handle it names, so the worker's copy outlives the caller's.
struct gpuseam_pipeline_job {
    gpuseam_pipeline *slot;
    gpuseam_state *state;               // a hold: the worker's scope is pushed on it
    WGPUDevice device;                  // a reference
    WGPUShaderModule modules[2];        // references, or NULL
    WGPUPipelineLayout layout;          // a reference, or NULL for "auto"
    WGPURenderPipelineDescriptor d;     // pointing into `bytes`
    gpuseam_pipeline_job *next;         // in the device's overflow, once its channel was full
    _Alignas(16) unsigned char bytes[];
};

// A deep copy past what webgpu.h holds today would copy a new pointer shallowly, and the worker would read freed memory:
// a header that grows a field of the description fails the build here (wgpu-native v29.0.1.1, 64-bit).
_Static_assert(sizeof(WGPURenderPipelineDescriptor) == 168, "WGPURenderPipelineDescriptor changed: update copy_description");
_Static_assert(sizeof(WGPUFragmentState) == 64, "WGPUFragmentState changed: update copy_description");
_Static_assert(sizeof(WGPUColorTargetState) == 32, "WGPUColorTargetState changed: update copy_description");
_Static_assert(sizeof(WGPUVertexBufferLayout) == 40, "WGPUVertexBufferLayout changed: update copy_description");
_Static_assert(sizeof(WGPUVertexState) == 64, "WGPUVertexState changed: update copy_description");
_Static_assert(sizeof(WGPUDepthStencilState) == 72, "WGPUDepthStencilState changed: update copy_description");
_Static_assert(sizeof(WGPUVertexAttribute) == 32, "WGPUVertexAttribute changed: update copy_description");
_Static_assert(sizeof(WGPUConstantEntry) == 32, "WGPUConstantEntry changed: update copy_description");

// A bump allocator over one block; with no block it only measures.
typedef struct { unsigned char *at; size_t used; } bump;

static void *bump_take(bump *b, size_t n) {
    size_t start = (b->used + 15) & ~(size_t)15;
    b->used = start + n;
    return b->at == NULL ? NULL : b->at + start;
}

static WGPUStringView copy_view(bump *b, WGPUStringView v) {
    if (v.data == NULL) return v;   // a null view keeps its meaning
    size_t n = v.length == WGPU_STRLEN ? strlen(v.data) : v.length;
    char *dst = (char *)bump_take(b, n + 1);
    if (dst != NULL) {
        memcpy(dst, v.data, n);
        dst[n] = 0;
    }
    WGPUStringView out = { dst, n };
    return out;
}

static const WGPUConstantEntry *copy_constants(bump *b, const WGPUConstantEntry *src, size_t n) {
    if (n == 0) return NULL;
    WGPUConstantEntry *dst = (WGPUConstantEntry *)bump_take(b, n * sizeof *dst);
    for (size_t i = 0; i < n; i++) {
        WGPUConstantEntry e = src[i];
        e.key = copy_view(b, src[i].key);
        if (dst != NULL) dst[i] = e;
    }
    return dst;
}

// `src` into `dst`, its arrays and strings taken from `b`: the same description, owning nothing it points to.
static void copy_description(bump *b, const WGPURenderPipelineDescriptor *src, WGPURenderPipelineDescriptor *dst) {
    *dst = *src;
    dst->nextInChain = NULL;
    dst->label = copy_view(b, src->label);
    dst->vertex.entryPoint = copy_view(b, src->vertex.entryPoint);
    dst->vertex.constants = copy_constants(b, src->vertex.constants, src->vertex.constantCount);
    WGPUVertexBufferLayout *buffers = NULL;
    if (src->vertex.bufferCount > 0) {
        buffers = (WGPUVertexBufferLayout *)bump_take(b, src->vertex.bufferCount * sizeof *buffers);
    }
    for (size_t i = 0; i < src->vertex.bufferCount; i++) {
        WGPUVertexBufferLayout l = src->vertex.buffers[i];
        WGPUVertexAttribute *attributes = NULL;
        if (l.attributeCount > 0) {
            attributes = (WGPUVertexAttribute *)bump_take(b, l.attributeCount * sizeof *attributes);
            if (attributes != NULL) memcpy(attributes, l.attributes, l.attributeCount * sizeof *attributes);
        }
        l.attributes = attributes;
        if (buffers != NULL) buffers[i] = l;
    }
    dst->vertex.buffers = buffers;
    if (src->depthStencil != NULL) {
        WGPUDepthStencilState *ds = (WGPUDepthStencilState *)bump_take(b, sizeof *ds);
        if (ds != NULL) *ds = *src->depthStencil;
        dst->depthStencil = ds;
    }
    if (src->fragment != NULL) {
        WGPUFragmentState f = *src->fragment;
        WGPUFragmentState *fs = (WGPUFragmentState *)bump_take(b, sizeof *fs);
        f.entryPoint = copy_view(b, src->fragment->entryPoint);
        f.constants = copy_constants(b, src->fragment->constants, src->fragment->constantCount);
        WGPUColorTargetState *targets = NULL;
        if (f.targetCount > 0) targets = (WGPUColorTargetState *)bump_take(b, f.targetCount * sizeof *targets);
        for (size_t i = 0; i < f.targetCount; i++) {
            WGPUColorTargetState t = src->fragment->targets[i];
            if (t.blend != NULL) {
                WGPUBlendState *blend = (WGPUBlendState *)bump_take(b, sizeof *blend);
                if (blend != NULL) *blend = *t.blend;
                t.blend = blend;
            }
            if (targets != NULL) targets[i] = t;
        }
        f.targets = targets;
        if (fs != NULL) *fs = f;
        dst->fragment = fs;
    }
}

static void job_free(gpuseam_pipeline_job *j) {
    for (int i = 0; i < 2; i++) {
        if (j->modules[i] != NULL) wgpuShaderModuleRelease(j->modules[i]);
    }
    if (j->layout != NULL) wgpuPipelineLayoutRelease(j->layout);
    wgpuDeviceRelease(j->device);
    gpuseam_state_release(j->state);
    free(j);
    atomic_fetch_sub_explicit(&g_pipelines_alive, 1, memory_order_relaxed);
}
#endif

gpuseam_pipeline *gpuseam_pipeline_start(WGPUDevice device, gpuseam_state *state, const WGPURenderPipelineDescriptor *d,
                                         const uint8_t *refusal, int32_t refusal_length, void **job) {
    *job = NULL;
    gpuseam_pipeline *p = pipeline_new();
    if (p == NULL) return NULL;
    if (refusal_length > 0) {   // refused before WebGPU is asked: failed at once, in the package's words
        pipeline_fail(p, GPUSEAM_KIND_VALIDATION, (const char *)refusal, (size_t)refusal_length);
        atomic_store_explicit(&p->state, GPUSEAM_FAILED, memory_order_release);
        return p;
    }
#ifdef __EMSCRIPTEN__
    (void)state;
    WGPUCreateRenderPipelineAsyncCallbackInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.mode = WGPUCallbackMode_AllowSpontaneous;
    ci.callback = on_pipeline;
    ci.userdata1 = p;
    wgpuDeviceCreateRenderPipelineAsync(device, d, ci);
#else
    bump measure = { NULL, 0 };
    WGPURenderPipelineDescriptor scratch;
    copy_description(&measure, d, &scratch);
    gpuseam_pipeline_job *j = (gpuseam_pipeline_job *)calloc(1, sizeof *j + measure.used);
    if (j == NULL) {
        static const char words[] = "out of memory for the pipeline's description";
        pipeline_fail(p, GPUSEAM_KIND_OUT_OF_MEMORY, words, sizeof words - 1);
        atomic_store_explicit(&p->state, GPUSEAM_FAILED, memory_order_release);
        return p;
    }
    bump fill = { j->bytes, 0 };
    copy_description(&fill, d, &j->d);
    j->slot = p;
    gpuseam_state_hold(state);
    j->state = state;
    wgpuDeviceAddRef(device);
    j->device = device;
    j->modules[0] = d->vertex.module;
    j->modules[1] = d->fragment != NULL ? d->fragment->module : NULL;
    for (int i = 0; i < 2; i++) {
        if (j->modules[i] != NULL) wgpuShaderModuleAddRef(j->modules[i]);
    }
    j->layout = d->layout;
    if (j->layout != NULL) wgpuPipelineLayoutAddRef(j->layout);
    atomic_fetch_add_explicit(&g_pipelines_alive, 1, memory_order_relaxed);
    *job = j;
#endif
    return p;
}

int32_t gpuseam_pipeline_poll(gpuseam_pipeline *p) {
    return p == NULL ? GPUSEAM_FAILED : (int32_t)atomic_load_explicit(&p->state, memory_order_acquire);
}

WGPURenderPipeline gpuseam_pipeline_handle(gpuseam_pipeline *p) {
    if (gpuseam_pipeline_poll(p) != GPUSEAM_READY || p->pipeline == NULL) return NULL;
    wgpuRenderPipelineAddRef(p->pipeline);
    return p->pipeline;
}

int32_t gpuseam_pipeline_error_kind(gpuseam_pipeline *p) {
    return gpuseam_pipeline_poll(p) == GPUSEAM_FAILED && p != NULL ? p->error.kind : GPUSEAM_KIND_UNKNOWN;
}
int32_t gpuseam_pipeline_error_length(gpuseam_pipeline *p) {
    return gpuseam_pipeline_poll(p) == GPUSEAM_FAILED && p != NULL ? p->error.length : 0;
}
const uint8_t *gpuseam_pipeline_error_message(gpuseam_pipeline *p) {
    return gpuseam_pipeline_poll(p) == GPUSEAM_FAILED && p != NULL ? (const uint8_t *)p->error.message : NULL;
}

void gpuseam_pipeline_drop(gpuseam_pipeline *p) {
    if (p == NULL) return;
    int expected = GPUSEAM_PENDING;
    if (atomic_compare_exchange_strong_explicit(&p->state, &expected, GPUSEAM_ABANDONED, memory_order_acq_rel,
                                                memory_order_acquire)) {
        return;   // a worker or the browser still owes its answer, and frees it then
    }
    pipeline_free(p);
}

int64_t gpuseam_pipelines_alive(void) { return (int64_t)atomic_load_explicit(&g_pipelines_alive, memory_order_relaxed); }

// ---- the native compile pool's seam: the jobs it runs, the overflow beside its channel, and the tests' hooks -------

static atomic_llong g_compile_workers;
static atomic_int g_compile_paused;

int64_t gpuseam_compile_workers_alive(void) { return (int64_t)atomic_load_explicit(&g_compile_workers, memory_order_relaxed); }
void gpuseam_compile_worker_started(void) { atomic_fetch_add_explicit(&g_compile_workers, 1, memory_order_relaxed); }
void gpuseam_compile_worker_ended(void) { atomic_fetch_sub_explicit(&g_compile_workers, 1, memory_order_relaxed); }
void gpuseam_compile_pause(int32_t paused) { atomic_store_explicit(&g_compile_paused, paused != 0, memory_order_release); }
int32_t gpuseam_compile_paused(void) { return atomic_load_explicit(&g_compile_paused, memory_order_acquire); }

#ifndef __EMSCRIPTEN__
// On a worker: the pipeline made under a scope of this thread that catches any error, and the slot published. A slot
// its request abandoned is freed without compiling.
void gpuseam_pipeline_job_run(gpuseam_pipeline_job *j) {
    if (j == NULL) return;
    gpuseam_pipeline *p = j->slot;
    if (atomic_load_explicit(&p->state, memory_order_acquire) == GPUSEAM_ABANDONED) {
        pipeline_free(p);
    } else {
        gpuseam_scope *sc = gpuseam_scope_push(j->state, j->device, GPUSEAM_FILTER_ANY);
        WGPURenderPipeline made = wgpuDeviceCreateRenderPipeline(j->device, &j->d);
        int caught = 0;
        if (sc != NULL) {
            gpuseam_scope_pop(sc);
            if (sc->caught) {
                p->error = sc->error;
                sc->error.message = NULL;
                sc->error.length = 0;
                caught = 1;
            }
            gpuseam_scope_drop_popped(sc);
        }
        if (caught) {
            if (made != NULL) wgpuRenderPipelineRelease(made);
            pipeline_publish(p, GPUSEAM_FAILED);
        } else {
            p->pipeline = made;
            pipeline_publish(p, GPUSEAM_READY);
        }
    }
    job_free(j);
}

// A job never run, its pool gone: failed, in the package's words.
void gpuseam_pipeline_job_cancel(gpuseam_pipeline_job *j) {
    if (j == NULL) return;
    static const char words[] = "the pipeline's compile pool closed before it was compiled";
    pipeline_fail(j->slot, GPUSEAM_KIND_INTERNAL, words, sizeof words - 1);
    pipeline_publish(j->slot, GPUSEAM_FAILED);
    job_free(j);
}

// Whether `job` goes into the pool's channel (1, counted), or waits in the device's overflow, the channel being full (0).
// The device's thread is the only sender, so a count below the channel's capacity means its send never waits.
int32_t gpuseam_compile_enqueue(gpuseam_state *s, gpuseam_pipeline_job *job, int32_t capacity) {
    state_lock(s);
    int32_t into_channel = s->compile_in_channel < capacity && s->compile_overflow == NULL;
    if (into_channel) {
        s->compile_in_channel += 1;
    } else {
        job->next = NULL;
        if (s->compile_overflow == NULL) s->compile_overflow = job; else s->compile_overflow_tail->next = job;
        s->compile_overflow_tail = job;
    }
    state_unlock(s);
    return into_channel;
}

// A worker took a job out of the channel.
void gpuseam_compile_received(gpuseam_state *s) {
    state_lock(s);
    s->compile_in_channel -= 1;
    state_unlock(s);
}

// The oldest job waiting in the overflow, or NULL. A worker takes these before it waits on the channel again.
gpuseam_pipeline_job *gpuseam_compile_overflow_take(gpuseam_state *s) {
    state_lock(s);
    gpuseam_pipeline_job *j = s->compile_overflow;
    if (j != NULL) {
        s->compile_overflow = j->next;
        if (s->compile_overflow == NULL) s->compile_overflow_tail = NULL;
    }
    state_unlock(s);
    return j;
}
#else
void gpuseam_pipeline_job_run(gpuseam_pipeline_job *j) { (void)j; }
void gpuseam_pipeline_job_cancel(gpuseam_pipeline_job *j) { (void)j; }
int32_t gpuseam_compile_enqueue(gpuseam_state *s, gpuseam_pipeline_job *job, int32_t capacity) {
    (void)s; (void)job; (void)capacity;
    return 1;
}
void gpuseam_compile_received(gpuseam_state *s) { (void)s; }
gpuseam_pipeline_job *gpuseam_compile_overflow_take(gpuseam_state *s) { (void)s; return NULL; }
#endif
