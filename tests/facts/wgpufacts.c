// wgpufacts.c: what wgpu-native does that @kama/gpu's seam is built on, held so that a wgpu-native that changes any of it
// fails the gate by name, rather than turning into a use-after-free or a crash inside the package (GPU-1;
// AGENTS.gpu.md item 2, question 5). Natively only: the web's facts are measured when its leg runs (GPU-3).
//
//   wgpufacts <case>      one case a process, since five of them end the process; tools/check-facts.sh runs each
//
// A case that holds prints "ok   <fact>" and exits 0, one that does not prints "FAIL <fact>: <why>" and exits 1, and
// with no adapter a case prints "skip" and exits 77. An abort case that gets past its call prints "FAIL …: did not
// abort" and exits 1; check-facts.sh holds the rest of it: an exit other than 0, 1 and 77, and the panic's own words.
//
// Made from kyoten's probe of 2026-11-10 (.scratch/item2-errors), wgpu-native v29.0.1.1 over Vulkan.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

// ---- what the callbacks saw ----------------------------------------------------------------------------------------

static _Thread_local char t_marker;   // its address names the thread a callback runs on
static const char *g_main_thread;
static int g_in_call;                 // 1 while a case is inside the wgpu call it is watching

typedef struct {
    int count;            // uncaptured errors seen
    int in_call;          // how many arrived inside the watched call
    int on_caller;        // how many arrived on the calling thread
    WGPUErrorType type;   // the last one's type
    int lost;             // lost callbacks seen
} seen;

static seen g_a, g_b;
static char g_words[4096];   // the last error's words, cut at the buffer, NUL-terminated

static void on_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message, void *u1, void *u2) {
    (void)device; (void)u2;
    seen *s = (seen *)u1;
    size_t n = message.data == NULL ? 0 : (message.length == WGPU_STRLEN ? strlen(message.data) : message.length);
    if (n >= sizeof g_words) n = sizeof g_words - 1;
    if (n > 0) memcpy(g_words, message.data, n);
    g_words[n] = '\0';
    s->count += 1;
    s->in_call += g_in_call;
    s->on_caller += (&t_marker == g_main_thread);
    s->type = type;
}

static void on_lost(WGPUDevice const *device, WGPUDeviceLostReason reason, WGPUStringView message, void *u1, void *u2) {
    (void)device; (void)reason; (void)message; (void)u2;
    ((seen *)u1)->lost += 1;
}

static void on_pop(WGPUPopErrorScopeStatus s, WGPUErrorType t, WGPUStringView m, void *u1, void *u2) {
    (void)s; (void)t; (void)m; (void)u1; (void)u2;
}

// ---- a device ------------------------------------------------------------------------------------------------------

static WGPUInstance g_instance;
static WGPUAdapter g_adapter;

static void on_adapter(WGPURequestAdapterStatus s, WGPUAdapter a, WGPUStringView m, void *u1, void *u2) {
    (void)m; (void)u1; (void)u2;
    if (s == WGPURequestAdapterStatus_Success) g_adapter = a;
}

static void on_device(WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView m, void *u1, void *u2) {
    (void)m; (void)u2;
    if (s == WGPURequestDeviceStatus_Success) *(WGPUDevice *)u1 = d;
}

static void events(void) { wgpuInstanceProcessEvents(g_instance); }

// A device whose callbacks write into `s`; with `handler` 0 it has no uncaptured callback at all.
static WGPUDevice device_for(seen *s, int handler) {
    WGPUDeviceDescriptor dd;
    memset(&dd, 0, sizeof dd);
    dd.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    dd.deviceLostCallbackInfo.callback = on_lost;
    dd.deviceLostCallbackInfo.userdata1 = s;
    if (handler) {
        dd.uncapturedErrorCallbackInfo.callback = on_error;
        dd.uncapturedErrorCallbackInfo.userdata1 = s;
    }
    WGPUDevice d = NULL;
    WGPURequestDeviceCallbackInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.mode = WGPUCallbackMode_AllowSpontaneous;
    ci.callback = on_device;
    ci.userdata1 = &d;
    wgpuAdapterRequestDevice(g_adapter, &dd, ci);
    for (int i = 0; i < 100 && d == NULL; i++) events();
    if (d == NULL) { printf("FAIL the adapter gave no device\n"); exit(1); }
    return d;
}

static WGPUBuffer buffer(WGPUDevice d, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor bd;
    memset(&bd, 0, sizeof bd);
    bd.size = size;
    bd.usage = usage;
    return wgpuDeviceCreateBuffer(d, &bd);
}

// MapRead may be combined only with CopyDst, so this is a validation error, made inside a watched call.
static WGPUBuffer bad_buffer(WGPUDevice d) {
    g_in_call = 1;
    WGPUBuffer b = buffer(d, 16, WGPUBufferUsage_MapRead | WGPUBufferUsage_Storage);
    g_in_call = 0;
    return b;
}

// An encoder holding a copy of 1024 bytes into a 4-byte buffer: an error its finish reports.
static WGPUCommandEncoder bad_encoder(WGPUDevice d) {
    WGPUBuffer src = buffer(d, 1024, WGPUBufferUsage_CopySrc);
    WGPUBuffer dst = buffer(d, 4, WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder e = wgpuDeviceCreateCommandEncoder(d, NULL);
    g_in_call = 1;
    wgpuCommandEncoderCopyBufferToBuffer(e, src, 0, dst, 0, 1024);
    g_in_call = 0;
    wgpuBufferRelease(dst);
    wgpuBufferRelease(src);
    return e;
}

static int g_failed;

static void fact(int holds, const char *what, const char *why) {
    if (holds) printf("ok   %s\n", what);
    else { printf("FAIL %s: %s\n", what, why); g_failed = 1; }
}

static void did_not_abort(const char *what) {
    printf("FAIL %s: did not abort\n", what);
    exit(1);
}

// ---- the cases -----------------------------------------------------------------------------------------------------

// An uncaptured error reaches its device's callback inside the call that caused it, on the calling thread, with that
// device's userdata, and nothing more arrives later: so an error is in the package's ring when the call returns, and
// the seam can keep each thread's scope stack itself.
static void in_the_call(void) {
    WGPUDevice d = device_for(&g_a, 1);
    WGPUBuffer b = bad_buffer(d);
    fact(g_a.count == 1, "an uncaptured error reaches its device's callback", "not reported");
    fact(g_a.in_call == 1, "inside the call that caused it", "reported outside the call");
    fact(g_a.on_caller == 1, "on the calling thread", "reported on another thread");
    fact(g_a.type == WGPUErrorType_Validation, "as a validation error", "another type");
    events();
    wgpuDevicePoll(d, 1, NULL);
    events();
    fact(g_a.count == 1, "and nothing more arrives later", "reported again by a poll");
    wgpuBufferRelease(b);
    wgpuDeviceRelease(d);
}

// Two devices' errors reach only their own callbacks.
static void two_devices(void) {
    WGPUDevice a = device_for(&g_a, 1);
    WGPUDevice b = device_for(&g_b, 1);
    WGPUBuffer x = bad_buffer(b);
    fact(g_a.count == 0 && g_b.count == 1, "an error on one device reaches only its own callback", "crossed over");
    WGPUBuffer y = bad_buffer(a);
    fact(g_a.count == 1 && g_b.count == 1, "and the other's only its", "crossed over");
    wgpuBufferRelease(y);
    wgpuBufferRelease(x);
    wgpuDeviceRelease(b);
    wgpuDeviceRelease(a);
}

// An encoder's error is reported inside its finish, not at the bad command: the seam marks a command buffer whose
// finish saw an error, and never submits it (question 3).
static void at_finish(void) {
    WGPUDevice d = device_for(&g_a, 1);
    WGPUCommandEncoder e = bad_encoder(d);
    fact(g_a.count == 0, "an encoder's bad command is not reported when recorded", "reported at the command");
    g_in_call = 1;
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(e, NULL);
    g_in_call = 0;
    fact(g_a.count == 1 && g_a.in_call == 1, "it is reported inside the encoder's finish", "not reported there");
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(e);
    wgpuDeviceRelease(d);
}

// A child reports to its device's callback after the device's last release, with the device's userdata: so the state
// block the userdata names lives as long as the handles made from the device (question 6).
static void after_release(void) {
    WGPUDevice d = device_for(&g_a, 1);
    WGPUCommandEncoder e = bad_encoder(d);
    WGPUQueue q = wgpuDeviceGetQueue(d);
    WGPUBuffer dst = buffer(d, 4, WGPUBufferUsage_CopyDst);
    WGPUTextureDescriptor td;
    memset(&td, 0, sizeof td);
    td.usage = WGPUTextureUsage_TextureBinding;
    td.dimension = WGPUTextureDimension_2D;
    td.size.width = 4;
    td.size.height = 4;
    td.size.depthOrArrayLayers = 1;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    WGPUTexture t = wgpuDeviceCreateTexture(d, &td);
    wgpuDeviceRelease(d);
    events();

    g_in_call = 1;
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(e, NULL);
    g_in_call = 0;
    fact(g_a.count == 1 && g_a.in_call == 1, "after the device's last release, an encoder still reports to its callback",
         "no report");
    static uint8_t bytes[64];
    g_in_call = 1;
    wgpuQueueWriteBuffer(q, dst, 0, bytes, sizeof bytes);
    g_in_call = 0;
    fact(g_a.count == 2 && g_a.in_call == 2, "and a queue", "no report");
    WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    vd.format = WGPUTextureFormat_Depth32Float;
    g_in_call = 1;
    WGPUTextureView v = wgpuTextureCreateView(t, &vd);
    g_in_call = 0;
    fact(g_a.count == 3 && g_a.in_call == 3, "and a texture", "no report");

    wgpuTextureViewRelease(v);
    wgpuTextureRelease(t);
    wgpuBufferRelease(dst);
    wgpuQueueRelease(q);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(e);
    events();
}

// No lost callback, ever: not at a destroy, a poll, the device's last release or its last child's. So natively the
// seam frees the state block when its last holder drops it, and a wgpu-native that starts calling back must fail
// here first (question 4).
static void no_loss(void) {
    WGPUDevice d = device_for(&g_a, 1);
    wgpuDeviceAddRef(d);
    wgpuDeviceRelease(d);
    events();
    fact(g_a.lost == 0, "no lost callback when one of two references is released", "called back");
    WGPUBuffer kept = buffer(d, 16, WGPUBufferUsage_CopyDst);
    wgpuDeviceRelease(d);
    events();
    fact(g_a.lost == 0, "nor at the device's last release", "called back");
    wgpuBufferRelease(kept);
    events();
    fact(g_a.lost == 0, "nor at its last child's", "called back");

    WGPUDevice e = device_for(&g_b, 1);
    wgpuDeviceDestroy(e);
    events();
    wgpuDevicePoll(e, 1, NULL);
    events();
    fact(g_b.lost == 0, "nor at a destroy, a poll or the events after it", "called back");
    wgpuDeviceRelease(e);
    events();
    fact(g_b.lost == 0, "nor at a destroyed device's release", "called back");
}

// A shader module from WGSL (`label` its label), made inside a watched call.
static WGPUShaderModule wgsl_module(WGPUDevice d, const char *code, const char *label) {
    WGPUShaderSourceWGSL w;
    memset(&w, 0, sizeof w);
    w.chain.sType = WGPUSType_ShaderSourceWGSL;
    w.code.data = code;
    w.code.length = strlen(code);
    WGPUShaderModuleDescriptor md;
    memset(&md, 0, sizeof md);
    md.nextInChain = &w.chain;
    md.label.data = label;
    md.label.length = strlen(label);
    g_in_call = 1;
    WGPUShaderModule m = wgpuDeviceCreateShaderModule(d, &md);
    g_in_call = 0;
    return m;
}

// A WGSL error is reported inside wgpuDeviceCreateShaderModule, and its words place it as naga does: `wgsl:<line>:
// <column>`, the column in characters, and a caret a character under the echoed line. The package keeps those words as
// the module is made, and places them in UTF-8 bytes (GPU-4; AGENTS.gpu.md item 12, decision 3).
static void wgsl_error(void) {
    WGPUDevice d = device_for(&g_a, 1);
    // `nosuch` begins at the 27th character of line 2, the 28th byte: é is two bytes.
    WGPUShaderModule m = wgsl_module(d, "@fragment fn fs() -> @location(0) vec4f {\n"
                                        "  let caf\xC3\xA9 = 1.0; let b = nosuch + caf\xC3\xA9;\n"
                                        "  return vec4f(b); }\n", "facts");
    fact(g_a.count == 1 && g_a.in_call == 1, "a WGSL error is reported inside wgpuDeviceCreateShaderModule", "not in the call");
    fact(g_a.type == WGPUErrorType_Validation, "as a validation error", "another type");
    fact(strstr(g_words, "wgsl:2:27") != NULL, "its words place it as wgsl:<line>:<column>, the column in characters",
         g_words);
    fact(strstr(g_words, "^^^^^^ ") != NULL, "and mark its span with a caret a character", g_words);
    fact(strstr(g_words, "Shader 'facts'") != NULL, "and name the module's label in a `Shader` line", g_words);
    wgpuShaderModuleRelease(m);
    wgpuDeviceRelease(d);
}

// Taken in v29.0.1.1, where WebGPU refuses it (Dawn: "Color target has no corresponding fragment stage output but
// writeMask … is not zero"): a colour target the fragment stage never writes, with a write mask that is not zero. The
// package cannot tell it without the stage's outputs, so the SPEC documents it and what a program means instead, a
// write mask of None (AGENTS.gpu.md item 12, decision 10). Firefox 160's WebGPU, on a later wgpu, refuses it, so the
// day this fails, wgpu-native refuses it too, and the SPEC's note on wgpu-native goes.
static void unwritten_target(void) {
    WGPUDevice d = device_for(&g_a, 1);
    WGPUShaderModule m = wgsl_module(d,
        "@vertex fn vs() -> @builtin(position) vec4f { return vec4f(0.0, 0.0, 0.0, 1.0); }\n"
        "@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0); }\n", "facts");
    WGPUColorTargetState targets[2];
    memset(targets, 0, sizeof targets);
    targets[0].format = WGPUTextureFormat_Undefined;   // an empty slot at location 0, which the stage writes
    targets[1].format = WGPUTextureFormat_RGBA8Unorm;  // location 1, which it does not
    targets[1].writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs;
    memset(&fs, 0, sizeof fs);
    fs.module = m;
    fs.entryPoint.data = "fs";
    fs.entryPoint.length = 2;
    fs.targetCount = 2;
    fs.targets = targets;
    WGPURenderPipelineDescriptor pd;
    memset(&pd, 0, sizeof pd);
    pd.vertex.module = m;
    pd.vertex.entryPoint.data = "vs";
    pd.vertex.entryPoint.length = 2;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.multisample.count = 1;
    pd.multisample.mask = 0xFFFFFFFF;
    pd.fragment = &fs;
    WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline(d, &pd);
    events();
    fact(g_a.count == 0, "a colour target the fragment stage never writes, with a write mask, is taken (WebGPU refuses it)",
         g_words);
    wgpuRenderPipelineRelease(p);
    wgpuShaderModuleRelease(m);
    wgpuDeviceRelease(d);
}

// ---- the aborts: the seam's guards are why none is reachable from the safe API -------------------------------------

static void abort_no_handler(void) {
    WGPUDevice d = device_for(&g_a, 0);
    bad_buffer(d);
    did_not_abort("a device with no uncaptured callback aborts on its first error");
}

static void abort_internal_filter(void) {
    WGPUDevice d = device_for(&g_a, 1);
    wgpuDevicePushErrorScope(d, WGPUErrorFilter_Internal);
    did_not_abort("pushing an Internal error scope aborts");
}

static WGPUFuture pop(WGPUDevice d, WGPUCallbackMode mode) {
    WGPUPopErrorScopeCallbackInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.mode = mode;
    ci.callback = on_pop;
    return wgpuDevicePopErrorScope(d, ci);
}

static void abort_empty_pop(void) {
    WGPUDevice d = device_for(&g_a, 1);
    pop(d, WGPUCallbackMode_AllowSpontaneous);
    did_not_abort("popping an error scope with none pushed aborts");
}

static void abort_wait_any(void) {
    WGPUDevice d = device_for(&g_a, 1);
    wgpuDevicePushErrorScope(d, WGPUErrorFilter_Validation);
    WGPUFutureWaitInfo wi;
    memset(&wi, 0, sizeof wi);
    wi.future = pop(d, WGPUCallbackMode_WaitAnyOnly);
    wgpuInstanceWaitAny(g_instance, 1, &wi, 0);
    did_not_abort("waiting through wgpuInstanceWaitAny aborts");
}

static void abort_submit_failed(void) {
    WGPUDevice d = device_for(&g_a, 1);
    WGPUCommandEncoder e = bad_encoder(d);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(e, NULL);
    WGPUQueue q = wgpuDeviceGetQueue(d);
    wgpuQueueSubmit(q, 1, &cb);
    did_not_abort("submitting a command buffer whose finish failed aborts");
}

static void on_compilation_info(WGPUCompilationInfoRequestStatus s, WGPUCompilationInfo const *info, void *u1, void *u2) {
    (void)s; (void)info; (void)u1; (void)u2;
}

// Not implemented in v29.0.1.1, for a good module or a bad one: the package answers natively from what creation kept
// (GPU-4). The day this holds no more, the package can ask wgpu-native as it asks the browser.
static void abort_compilation_info(void) {
    WGPUDevice d = device_for(&g_a, 1);
    WGPUShaderModule m = wgsl_module(d, "@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0); }\n", "facts");
    WGPUCompilationInfoCallbackInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.mode = WGPUCallbackMode_AllowSpontaneous;
    ci.callback = on_compilation_info;
    wgpuShaderModuleGetCompilationInfo(m, ci);
    did_not_abort("asking a shader module for its compilation info aborts");
}

static void on_pipeline_async(WGPUCreatePipelineAsyncStatus s, WGPURenderPipeline p, WGPUStringView m, void *u1, void *u2) {
    (void)s; (void)p; (void)m; (void)u1; (void)u2;
}

// Not implemented in v29.0.1.1: the package compiles on isolates of its own (GPU-4 part 3). The day this holds no more,
// the package can hand wgpu-native the request, as it hands the browser it.
static void abort_render_pipeline_async(void) {
    WGPUDevice d = device_for(&g_a, 1);
    WGPUShaderModule m = wgsl_module(d,
        "@vertex fn vs() -> @builtin(position) vec4f { return vec4f(0.0, 0.0, 0.0, 1.0); }\n"
        "@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0); }\n", "facts");
    WGPUColorTargetState target;
    memset(&target, 0, sizeof target);
    target.format = WGPUTextureFormat_RGBA8Unorm;
    target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs;
    memset(&fs, 0, sizeof fs);
    fs.module = m;
    fs.entryPoint.data = "fs";
    fs.entryPoint.length = 2;
    fs.targetCount = 1;
    fs.targets = &target;
    WGPURenderPipelineDescriptor pd;
    memset(&pd, 0, sizeof pd);
    pd.vertex.module = m;
    pd.vertex.entryPoint.data = "vs";
    pd.vertex.entryPoint.length = 2;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.multisample.count = 1;
    pd.multisample.mask = 0xFFFFFFFF;
    pd.fragment = &fs;
    WGPUCreateRenderPipelineAsyncCallbackInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.mode = WGPUCallbackMode_AllowSpontaneous;
    ci.callback = on_pipeline_async;
    wgpuDeviceCreateRenderPipelineAsync(d, &pd, ci);
    did_not_abort("creating a render pipeline asynchronously aborts");
}

// ---- the program ---------------------------------------------------------------------------------------------------

typedef struct { const char *name; void (*run)(void); } entry;

static const entry CASES[] = {
    { "inthecall", in_the_call },
    { "twodevices", two_devices },
    { "atfinish", at_finish },
    { "afterrelease", after_release },
    { "noloss", no_loss },
    { "wgsl", wgsl_error },
    { "unwritten", unwritten_target },
    { "abort-nohandler", abort_no_handler },
    { "abort-internal", abort_internal_filter },
    { "abort-emptypop", abort_empty_pop },
    { "abort-waitany", abort_wait_any },
    { "abort-submit", abort_submit_failed },
    { "abort-compilationinfo", abort_compilation_info },
    { "abort-pipelineasync", abort_render_pipeline_async },
};

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_main_thread = &t_marker;
    if (argc != 2) {
        printf("usage: wgpufacts <case>; the cases:");
        for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) printf(" %s", CASES[i].name);
        printf("\n");
        return 2;
    }
    const entry *chosen = NULL;
    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        if (strcmp(argv[1], CASES[i].name) == 0) chosen = &CASES[i];
    }
    if (chosen == NULL) { printf("FAIL no case %s\n", argv[1]); return 2; }

    g_instance = wgpuCreateInstance(NULL);
    if (g_instance == NULL) { printf("skip no WebGPU instance\n"); return 77; }
    WGPURequestAdapterOptions ao;
    memset(&ao, 0, sizeof ao);
    WGPURequestAdapterCallbackInfo aci;
    memset(&aci, 0, sizeof aci);
    aci.mode = WGPUCallbackMode_AllowSpontaneous;
    aci.callback = on_adapter;
    wgpuInstanceRequestAdapter(g_instance, &ao, aci);
    for (int i = 0; i < 100 && g_adapter == NULL; i++) events();
    if (g_adapter == NULL) { printf("skip no adapter\n"); return 77; }

    chosen->run();
    return g_failed;
}
