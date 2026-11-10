// A fixture for tools/wgpugen's coverage report: the seam's C, read as text. Its layout is part of the test:
// - gpuseam_request_adapter, which the safe API calls, names wgpuDeviceDestroy and wgpuTextureDestroy in comments, and
//   neither is called;
// - the one-line helper it calls sits just above gpuseam_map_async, which nothing calls, so a one-line definition read
//   as the start of a longer one would take that function's body, and its call, with it.
#include <stddef.h>

typedef void (*done_fn)(int status);
void *wgpuInstanceRequestAdapter(void *instance, void *options, done_fn done);
void wgpuInstanceProcessEvents(void *instance);
void wgpuAdapterRelease(void *adapter);
void *wgpuBufferMapAsync(void *buffer, int mode, size_t offset, size_t size, done_fn done);
static void helper(void *instance);

static void on_done(int status) {
    (void)status;
    wgpuAdapterRelease(NULL);
}

void *gpuseam_request_adapter(void *instance) {
    // wgpuDeviceDestroy is named here, in a comment, and never called
    /* nor is wgpuTextureDestroy */
    helper(instance);
    return wgpuInstanceRequestAdapter(instance, NULL, on_done);
}

static void helper(void *instance) { wgpuInstanceProcessEvents(instance); }

void *gpuseam_map_async(void *buffer) {
    return wgpuBufferMapAsync(buffer, 0, 0, 0, NULL);
}
