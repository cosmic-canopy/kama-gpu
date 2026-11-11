// gpuseam.h: @kama/gpu's C seam — what kama cannot do itself, kept as small as that allows.
//
// WebGPU's asynchronous steps (an adapter, a device from it, a buffer map) finish in a callback, and a callback may fire
// on a thread of the implementation's own (wgpu-native) or from the browser's event loop between frames (the web). So
// the seam receives them here, into a heap slot of its own, and publishes the outcome with a release-store; kama polls
// it with an acquire-load and never hands WebGPU a kama function. A slot dropped while its callback is still owed is
// abandoned, not freed: the callback frees it when it fires. (kama's design, ROADMAP_DETAIL.md § 8, s8-gpu-wrapper.)
//
// Each device has a state block here, made with its request and given to its error and loss callbacks: a ring of its
// uncaptured errors, kept whole, and its loss. Every handle made from the device holds the block, since wgpu-native
// reports a child's error to its device's callback after the device's last release (tests/facts), and the block is
// freed when the last holder lets it go.
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

// A device's state block, and an error scope (below).
typedef struct gpuseam_state gpuseam_state;
typedef struct gpuseam_scope gpuseam_scope;

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

// ---- whether an adapter has given a device ----

// WebGPU gives an adapter one device: once a request is granted the adapter is consumed, and a browser refuses a later
// one. wgpu-native grants it, so the package keeps the rule on every leg (AGENTS.gpu.md item 10, decision 1). A mark is
// shared by an adapter's copies and the requests made of them, counted, and freed with its last holder.
typedef struct gpuseam_adapter_mark gpuseam_adapter_mark;

// A mark not consumed, with one holder. NULL only when it cannot be allocated.
gpuseam_adapter_mark *gpuseam_adapter_mark_new(void);
void gpuseam_adapter_mark_hold(gpuseam_adapter_mark *mark);
void gpuseam_adapter_mark_release(gpuseam_adapter_mark *mark);
// 1 once a device request of the adapter has been granted.
int32_t gpuseam_adapter_mark_consumed(gpuseam_adapter_mark *mark);

// ---- a device on its way: WebGPU's second step ----

typedef struct gpuseam_device_request gpuseam_device_request;

// Ask `adapter` for a device with `limits` (NULL, or a WGPULimits whose undefined fields take WebGPU's defaults) and
// `feature_count` WGPUFeatureNames, its uncaptured errors and its loss kept in a state block of its own. `instance` is the
// one the adapter came from, whose events a poll processes, or NULL for an adapter adopted from elsewhere. `mark` is the
// adapter's: a request of a consumed adapter fails at once without asking WebGPU, a granted one consumes it, and one that
// settles after another consumed it fails, its device released, so every leg keeps WebGPU's rule. The slot holds a
// reference on each while it lives. NULL only when the slot cannot be allocated.
gpuseam_device_request *gpuseam_adapter_request_device(WGPUInstance instance, WGPUAdapter adapter,
                                                       gpuseam_adapter_mark *mark, const WGPULimits *limits,
                                                       const int32_t *features, int32_t feature_count);
// GPUSEAM_PENDING, _READY or _FAILED, after processing the instance's events when it has one.
int32_t gpuseam_device_request_poll(gpuseam_device_request *request);
// A READY request's device, handed over once (NULL after).
WGPUDevice gpuseam_device_request_take_device(gpuseam_device_request *request);
// A READY request's device state, with the hold the request kept, handed over once (NULL after).
gpuseam_state *gpuseam_device_request_take_state(gpuseam_device_request *request);
// A FAILED request's words, copied into `dst` (at most `capacity` bytes, UTF-8, not terminated): their length.
int32_t gpuseam_device_request_message(gpuseam_device_request *request, uint8_t *dst, int32_t capacity);
// 1 when a FAILED request failed because its adapter had given a device.
int32_t gpuseam_device_request_consumed(gpuseam_device_request *request);
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

// ---- a device's state: its errors and its loss ----

// A report's kind, and a loss's reason.
#define GPUSEAM_KIND_VALIDATION 0
#define GPUSEAM_KIND_OUT_OF_MEMORY 1
#define GPUSEAM_KIND_INTERNAL 2
#define GPUSEAM_KIND_UNKNOWN 3
#define GPUSEAM_LOSS_UNKNOWN 1
#define GPUSEAM_LOSS_DESTROYED 2

typedef struct gpuseam_errors gpuseam_errors;

// A block for a device adopted from elsewhere, whose callbacks were its maker's: nothing reaches it, and it says so.
// NULL only when it cannot be allocated.
gpuseam_state *gpuseam_state_unwatched(void);
// Another holder, or one fewer: the block is freed with its last. Both take NULL.
void gpuseam_state_hold(gpuseam_state *state);
void gpuseam_state_release(gpuseam_state *state);
// How many blocks are alive in the process, for a test that one is freed with its last holder.
int64_t gpuseam_states_alive(void);
// Whether the device's callbacks reach the block: 0 for an adopted device, and for NULL.
int32_t gpuseam_state_watched(gpuseam_state *state);

// The device's uncaptured errors since the last drain, the oldest first, moved out of its ring: at most 64, and how many
// did not fit. NULL when there are none (or no memory for the drain, when they stay for the next).
gpuseam_errors *gpuseam_state_take_errors(gpuseam_state *state);
int32_t gpuseam_errors_count(gpuseam_errors *errors);
int64_t gpuseam_errors_dropped(gpuseam_errors *errors);
// Report `at`'s GPUSEAM_KIND_*, and its words, whole (UTF-8, not terminated; NULL with length 0 when it had none).
int32_t gpuseam_errors_kind(gpuseam_errors *errors, int32_t at);
int32_t gpuseam_errors_length(gpuseam_errors *errors, int32_t at);
const uint8_t *gpuseam_errors_message(gpuseam_errors *errors, int32_t at);
void gpuseam_errors_free(gpuseam_errors *errors);

// ---- a device's error scopes ----

// What a scope catches: the GPUSEAM_KIND_* of the same number.
#define GPUSEAM_FILTER_VALIDATION 0
#define GPUSEAM_FILTER_OUT_OF_MEMORY 1
#define GPUSEAM_FILTER_INTERNAL 2
#define GPUSEAM_SCOPE_OUT_OF_ORDER 1

// Open a scope on the calling thread: natively the seam keeps it, catching the first error its filter matches; on the
// web it is the browser's. It holds the block and a reference on `device`. NULL with no memory, or no block.
gpuseam_scope *gpuseam_scope_push(gpuseam_state *state, WGPUDevice device, int32_t filter);
// Pop it: 0, its answer then polled as a slot's; or GPUSEAM_SCOPE_OUT_OF_ORDER while a scope pushed after it on its
// thread is open, when it is orphaned instead, popped once those are, and what it caught goes to the channel. Either way
// the caller is done with the scope as pushed: a popped one is dropped with gpuseam_scope_drop_popped.
int32_t gpuseam_scope_pop(gpuseam_scope *scope);
// A scope dropped unpopped: orphaned, and popped as soon as it is the innermost on its thread.
void gpuseam_scope_abandon(gpuseam_scope *scope);
// A popped scope: GPUSEAM_PENDING (the web's answer owed), _READY or _FAILED; whether it caught an error, and that
// error's GPUSEAM_KIND_* and words (a FAILED pop's words, too).
int32_t gpuseam_scope_poll(gpuseam_scope *scope);
int32_t gpuseam_scope_caught(gpuseam_scope *scope);
int32_t gpuseam_scope_kind(gpuseam_scope *scope);
int32_t gpuseam_scope_length(gpuseam_scope *scope);
const uint8_t *gpuseam_scope_message(gpuseam_scope *scope);
// Done with a popped scope: freed now, or abandoned to the web's answer if it is still owed.
void gpuseam_scope_drop_popped(gpuseam_scope *scope);

// 0 while the device is not lost, else its GPUSEAM_LOSS_*, and the words it was lost with.
int32_t gpuseam_state_lost(gpuseam_state *state);
int32_t gpuseam_state_lost_length(gpuseam_state *state);
const uint8_t *gpuseam_state_lost_message(gpuseam_state *state);

// ---- finishing an encoder ----

// `wgpuCommandEncoderFinish`, and in *failed whether the finish reported an error to `state`'s device. Natively it is
// reported inside the call (tests/facts, `atfinish`), and a command buffer so marked is never submitted, since
// wgpu-native ends the process on its submit; on the web the error arrives a task later, so *failed is 0 there and the
// browser refuses the submit itself. `state` may be NULL (an encoder adopted from elsewhere): never marked.
WGPUCommandBuffer gpuseam_encoder_finish(WGPUCommandEncoder encoder, gpuseam_state *state, int32_t *failed);

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
// A view of every mip level and layer of `texture`, in `format` (a WGPUTextureFormat): the texture's own, or the sRGB or
// linear form it was made to be viewed in. Undefined (0) is the texture's own. Built from the header's own
// WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT, so no "undefined" sentinel has to be mirrored in kama.
WGPUTextureView gpuseam_texture_view_in(WGPUTexture texture, int32_t format);

#endif
