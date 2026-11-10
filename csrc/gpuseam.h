// gpuseam.h: @kama/gpu's C seam — what kama cannot do itself, kept as small as that allows.
//
// WebGPU's asynchronous steps (an adapter, a device from it, a buffer map) finish in a callback, and a callback may fire
// on a thread of the implementation's own (wgpu-native) or from the browser's event loop between frames (the web). So
// the seam receives them here, into a heap slot of its own, and publishes the outcome with a release-store; kama polls
// it with an acquire-load and never hands WebGPU a kama function. A slot dropped while its callback is still owed is
// abandoned, not freed: the callback frees it when it fires. (kama's design, ROADMAP_DETAIL.md § 8, s8-gpu-wrapper.)
//
// Uncaptured errors and a device lost for a reason other than its release are counted process-wide, the last
// message kept; a test reads the count after a frame to prove the frame was valid.
//
// This header reaches every C file of a consumer's build (kama emits each extern header into one shared header), so it
// declares and never defines.
#ifndef GPUSEAM_H
#define GPUSEAM_H

#if defined(__has_include)
#  if !__has_include(<webgpu/webgpu.h>)
#    error "@kama/gpu needs WebGPU's headers: set \"webgpu\": true in your project's kama.json. kama reads that key from the root manifest only, so a dependency cannot set it for you (kama docs/targets.md)."
#  endif
#endif

#include <stddef.h>
#include <stdint.h>
#include <webgpu/webgpu.h>

// The slot states a poll reports.
#define GPUSEAM_PENDING 0
#define GPUSEAM_READY 1
#define GPUSEAM_FAILED 2

// ---- an adapter on its way: WebGPU's first step ----

typedef struct gpuseam_adapter_request gpuseam_adapter_request;

// Ask `instance` for an adapter (`compatible` may be NULL: an offscreen one). `power` is a WGPUPowerPreference. The
// slot holds a reference on the instance while it lives. NULL only when the slot cannot be allocated.
gpuseam_adapter_request *gpuseam_request_adapter(WGPUInstance instance, WGPUSurface compatible, int32_t power);
// GPUSEAM_PENDING, _READY or _FAILED, after processing the instance's events.
int32_t gpuseam_adapter_request_poll(gpuseam_adapter_request *request);
// A READY request's adapter, handed over once (NULL after).
WGPUAdapter gpuseam_adapter_request_take(gpuseam_adapter_request *request);
// A FAILED request's words, copied into `dst` (at most `capacity` bytes, UTF-8, not terminated): their length.
int32_t gpuseam_adapter_request_message(gpuseam_adapter_request *request, uint8_t *dst, int32_t capacity);
// Done with the slot: freed now, or abandoned to its callback if one is still owed. An adapter not taken is released.
void gpuseam_adapter_request_drop(gpuseam_adapter_request *request);

// ---- what an adapter answers ----

// One of the adapter's info strings (`which`: 0 vendor, 1 architecture, 2 device, 3 description), copied into `dst` (at
// most `capacity` bytes, UTF-8, not terminated): its length. A browser may leave each one empty.
int32_t gpuseam_adapter_info_text(WGPUAdapter adapter, int32_t which, uint8_t *dst, int32_t capacity);
// Its WGPUBackendType, WGPUAdapterType, vendor id and device id.
void gpuseam_adapter_info_numbers(WGPUAdapter adapter, int32_t *backend, int32_t *kind, uint32_t *vendor_id,
                                  uint32_t *device_id);
// The WGPUFeatureNames an adapter or a device has, at most `capacity` of them into `dst`: how many it has in all.
int32_t gpuseam_adapter_features(WGPUAdapter adapter, int32_t *dst, int32_t capacity);
int32_t gpuseam_device_features(WGPUDevice device, int32_t *dst, int32_t capacity);

// ---- a device on its way: WebGPU's second step ----

typedef struct gpuseam_device_request gpuseam_device_request;

// Ask `adapter` for a device with `limits` (NULL, or a WGPULimits whose undefined fields take WebGPU's defaults) and
// `feature_count` WGPUFeatureNames, its uncaptured errors and its loss counted process-wide. `instance` is the one the
// adapter came from, whose events a poll processes, or NULL for an adapter adopted from elsewhere. The slot holds a
// reference on each while it lives. NULL only when the slot cannot be allocated.
gpuseam_device_request *gpuseam_adapter_request_device(WGPUInstance instance, WGPUAdapter adapter,
                                                       const WGPULimits *limits, const int32_t *features,
                                                       int32_t feature_count);
// GPUSEAM_PENDING, _READY or _FAILED, after processing the instance's events when it has one.
int32_t gpuseam_device_request_poll(gpuseam_device_request *request);
// A READY request's device, handed over once (NULL after).
WGPUDevice gpuseam_device_request_take_device(gpuseam_device_request *request);
// A FAILED request's words, copied into `dst` (at most `capacity` bytes, UTF-8, not terminated): their length.
int32_t gpuseam_device_request_message(gpuseam_device_request *request, uint8_t *dst, int32_t capacity);
// Done with the slot: freed now, or abandoned to its callback if one is still owed. A device not taken is released.
void gpuseam_device_request_drop(gpuseam_device_request *request);

// ---- a buffer map ----

typedef struct gpuseam_map gpuseam_map;

// Map `size` bytes of `buffer` from `offset` (`mode` a WGPUMapMode). The slot holds a reference on the buffer until it
// is dropped. NULL only when the slot cannot be allocated.
gpuseam_map *gpuseam_map_async(WGPUBuffer buffer, uint64_t mode, size_t offset, size_t size);
int32_t gpuseam_map_poll(gpuseam_map *map);
int32_t gpuseam_map_message(gpuseam_map *map, uint8_t *dst, int32_t capacity);
// Done with the slot: a READY map is unmapped, the buffer's reference released, and the slot freed or abandoned.
void gpuseam_map_drop(gpuseam_map *map);
// A READY map's bytes, read-only (the web's GetMappedRange refuses a read-only map, so this is the const range).
const uint8_t *gpuseam_map_bytes(gpuseam_map *map);

// ---- errors, process-wide ----

int64_t gpuseam_error_count(void);
int32_t gpuseam_last_error(uint8_t *dst, int32_t capacity);
int64_t gpuseam_lost_count(void);

// ---- driving the callbacks ----

// Natively, let `device` finish queued work and fire its callbacks (`wait`: block until the queue is empty). On the web
// the browser's event loop does that between frames, so it does nothing.
void gpuseam_device_poll(WGPUDevice device, int32_t wait);


// ---- a surface ----

// The outcome of an acquire. OK leaves the texture in *texture, and *suboptimal says whether to reconfigure soon.
#define GPUSEAM_SURFACE_OK 0
#define GPUSEAM_SURFACE_OCCLUDED 1
#define GPUSEAM_SURFACE_OUTDATED 2
#define GPUSEAM_SURFACE_LOST 3
#define GPUSEAM_SURFACE_TIMEOUT 4
#define GPUSEAM_SURFACE_ERROR 5

// A surface on a system window, from @kama/window's native handle: `kind` 1 is Win32 (HINSTANCE, HWND), 5 a canvas
// selector (NULL, the selector's C string). NULL for a kind this leg cannot make a surface from.
WGPUSurface gpuseam_create_surface(WGPUInstance instance, int32_t kind, void *display, void *window);
// The next texture to draw into. Natively, an occluded window sleeps 32 ms before answering OCCLUDED, the way a
// browser throttles a hidden page (kama's design, decision 4); the web never reports it.
int32_t gpuseam_surface_acquire(WGPUSurface surface, void **texture, int32_t *suboptimal);
// Show what was drawn. On the web the browser presents when the frame returns (emdawnwebgpu aborts on an explicit
// present), so it does nothing there.
void gpuseam_surface_present(WGPUSurface surface);

#endif
