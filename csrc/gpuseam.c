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

// ---- errors, process-wide ----------------------------------------------------------------------------------------

static atomic_llong g_errors;
static atomic_llong g_lost;
static atomic_flag g_error_lock = ATOMIC_FLAG_INIT;
static char g_last_error[GPUSEAM_MESSAGE_BYTES];
static int32_t g_last_error_length;

static void on_uncaptured_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message, void *u1, void *u2) {
    (void)device; (void)type; (void)u1; (void)u2;
    while (atomic_flag_test_and_set_explicit(&g_error_lock, memory_order_acquire)) { }
    g_last_error_length = keep_message(message, g_last_error);
    atomic_flag_clear_explicit(&g_error_lock, memory_order_release);
    atomic_fetch_add_explicit(&g_errors, 1, memory_order_release);
}

static void on_device_lost(WGPUDevice const *device, WGPUDeviceLostReason reason, WGPUStringView message, void *u1, void *u2) {
    (void)device; (void)u1; (void)u2;
    // released on purpose, or its instance gone first: not a loss anyone must handle
    if (reason == WGPUDeviceLostReason_Destroyed || reason == WGPUDeviceLostReason_CallbackCancelled) return;
    while (atomic_flag_test_and_set_explicit(&g_error_lock, memory_order_acquire)) { }
    g_last_error_length = keep_message(message, g_last_error);
    atomic_flag_clear_explicit(&g_error_lock, memory_order_release);
    atomic_fetch_add_explicit(&g_lost, 1, memory_order_release);
}

int64_t gpuseam_error_count(void) { return (int64_t)atomic_load_explicit(&g_errors, memory_order_acquire); }
int64_t gpuseam_lost_count(void) { return (int64_t)atomic_load_explicit(&g_lost, memory_order_acquire); }

int32_t gpuseam_last_error(uint8_t *dst, int32_t capacity) {
    while (atomic_flag_test_and_set_explicit(&g_error_lock, memory_order_acquire)) { }
    int32_t n = copy_out(g_last_error, g_last_error_length, dst, capacity);
    atomic_flag_clear_explicit(&g_error_lock, memory_order_release);
    return n;
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
    char message[GPUSEAM_MESSAGE_BYTES];
    int32_t message_length;
};

static void request_free(gpuseam_device_request *r) {
    if (r->device != NULL) wgpuDeviceRelease(r->device);
    if (r->adapter != NULL) wgpuAdapterRelease(r->adapter);
    if (r->instance != NULL) wgpuInstanceRelease(r->instance);
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
    dd.uncapturedErrorCallbackInfo.callback = on_uncaptured_error;
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
