// gpuseam.c: the seam's bodies, for both legs (gpuseam.h says why it exists).
//
// Every slot's state is one atomic: PENDING, READY, FAILED, or ABANDONED (its owner dropped it while a callback was
// still owed). A callback writes its results and then publishes the state with a release-store, or, finding the slot
// abandoned, cleans it up itself; a poll reads the state with an acquire-load before touching anything else.
#include "gpuseam.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#ifndef __EMSCRIPTEN__
#include <webgpu/wgpu.h>
#endif

#define GPUSEAM_ABANDONED 3
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

// A message kept whole, in an allocation of its own.
static gpuseam_report report_of(int32_t kind, WGPUStringView v) {
    gpuseam_report r = { kind, NULL, 0 };
    if (v.data == NULL) return r;
    size_t n = v.length == WGPU_STRLEN ? strlen(v.data) : v.length;
    if (n == 0) return r;
    if (n > INT32_MAX) n = INT32_MAX;
    r.message = (char *)malloc(n);
    if (r.message == NULL) return r;
    memcpy(r.message, v.data, n);
    r.length = (int32_t)n;
    return r;
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
    free(s);
    atomic_fetch_sub_explicit(&g_states_alive, 1, memory_order_relaxed);
}

int64_t gpuseam_states_alive(void) { return (int64_t)atomic_load_explicit(&g_states_alive, memory_order_relaxed); }

// The thread a scope was pushed on, by the address of a thread-local of its own.
static _Thread_local char t_thread;
#define THIS_THREAD ((const void *)&t_thread)

// Natively an error goes to the innermost scope on its thread whose filter matches it: every error measured reaches the
// callback inside the call that caused it, on the calling thread (tests/facts, `inthecall`). A scope keeps the first
// and swallows the rest, as WebGPU's does. Past every scope, an error goes to the channel. On the web the browser's
// scopes have taken what they catch before this callback runs.
static void on_uncaptured_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message, void *u1, void *u2) {
    (void)device; (void)u2;
    gpuseam_state *s = (gpuseam_state *)u1;
    gpuseam_report r = report_of(kind_of(type), message);
    state_lock(s);
#ifndef __EMSCRIPTEN__
    for (gpuseam_scope *sc = s->top; sc != NULL; sc = sc->below) {
        if (sc->thread != THIS_THREAD || sc->filter != r.kind) continue;
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
    ring_put(s, &r);
    state_unlock(s);
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
        ring_put(sc->state, &r);
        state_unlock(sc->state);
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
        outcome = GPUSEAM_FAILED;
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
    if (atomic_load_explicit(&r->state, memory_order_acquire) != GPUSEAM_FAILED) return 0;
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

// ---- a device on its way ---------------------------------------------------------------------------------------------

struct gpuseam_device_request {
    atomic_int state;
    WGPUInstance instance;   // the adapter's instance, for its events, or NULL; a reference held while the slot lives
    WGPUAdapter adapter;     // a reference held while the slot lives
    WGPUDevice device;
    gpuseam_state *errors;   // the device's state, held until the device is taken
    char message[GPUSEAM_MESSAGE_BYTES];
    int32_t message_length;
};

static void request_free(gpuseam_device_request *r) {
    if (r->device != NULL) wgpuDeviceRelease(r->device);
    if (r->adapter != NULL) wgpuAdapterRelease(r->adapter);
    if (r->instance != NULL) wgpuInstanceRelease(r->instance);
    gpuseam_state_release(r->errors);
    free(r);
}

static void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *u1, void *u2) {
    (void)u2;
    gpuseam_device_request *r = (gpuseam_device_request *)u1;
    int outcome = GPUSEAM_READY;
    if (status != WGPURequestDeviceStatus_Success || device == NULL) {
        r->message_length = keep_message(message, r->message);
        if (r->message_length == 0) r->message_length = keep_text("the adapter gave no device", r->message);
        outcome = GPUSEAM_FAILED;
    } else {
        r->device = device;
    }
    int expected = GPUSEAM_PENDING;
    if (!atomic_compare_exchange_strong_explicit(&r->state, &expected, outcome, memory_order_acq_rel, memory_order_acquire)) {
        request_free(r);   // ABANDONED
    }
}

gpuseam_device_request *gpuseam_adapter_request_device(WGPUInstance instance, WGPUAdapter adapter,
                                                       const WGPULimits *limits, const int32_t *features,
                                                       int32_t feature_count) {
    gpuseam_device_request *r = (gpuseam_device_request *)calloc(1, sizeof *r);
    if (r == NULL) return NULL;
    r->errors = state_new(1);
    if (r->errors == NULL) { free(r); return NULL; }
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

gpuseam_map *gpuseam_map_async(WGPUBuffer buffer, uint64_t mode, size_t offset, size_t size) {
    gpuseam_map *m = (gpuseam_map *)calloc(1, sizeof *m);
    if (m == NULL) return NULL;
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

// ---- a surface -------------------------------------------------------------------------------------------------------

WGPUSurface gpuseam_create_surface(WGPUInstance instance, int32_t kind, void *display, void *window) {
    WGPUSurfaceDescriptor d;
    memset(&d, 0, sizeof d);
#if defined(__EMSCRIPTEN__)
    (void)display;
    if (kind != 5 || window == NULL) return NULL;
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
