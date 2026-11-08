// gpuseam.h: @kama/gpu's C seam — what kama cannot do itself, kept as small as that allows.
//
// WebGPU's asynchronous steps (an adapter, then a device; a buffer map) finish in a callback, and a callback may fire
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

// ---- a device on its way up: the adapter's callback, then the device's, chained into one slot ----

typedef struct gpuseam_device_request gpuseam_device_request;

// Ask `instance` for an adapter (`compatible` may be NULL: an offscreen device) and then a device with WebGPU's
// default limits. `power` is a WGPUPowerPreference. NULL only when the slot cannot be allocated.
gpuseam_device_request *gpuseam_request_device(WGPUInstance instance, WGPUSurface compatible, int32_t power);
// GPUSEAM_PENDING, _READY or _FAILED, after processing the instance's events.
int32_t gpuseam_device_request_poll(gpuseam_device_request *request);
// The adapter and the device of a READY request, each handed over once (NULL after).
WGPUAdapter gpuseam_device_request_take_adapter(gpuseam_device_request *request);
WGPUDevice gpuseam_device_request_take_device(gpuseam_device_request *request);
// A FAILED request's words, copied into `dst` (at most `capacity` bytes, UTF-8, not terminated): their length.
int32_t gpuseam_device_request_message(gpuseam_device_request *request, uint8_t *dst, int32_t capacity);
// Done with the slot: freed now, or abandoned to its callback if one is still owed. Whatever was not taken is released.
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

#endif
