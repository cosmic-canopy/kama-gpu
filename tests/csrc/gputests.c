// gputests.c: gputests.h's bodies. Win32 answers natively on Windows; elsewhere the question answers -1.
#include "gputests.h"

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <windows.h>

int32_t gputests_minimise(void *hwnd, int32_t minimised) {
    // SW_SHOWMINNOACTIVE and SW_SHOWNOACTIVATE leave the active window where it is.
    ShowWindow((HWND)hwnd, minimised ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE);
    return 1;
}
#else
int32_t gputests_minimise(void *hwnd, int32_t minimised) { (void)hwnd; (void)minimised; return -1; }
#endif

#if defined(__EMSCRIPTEN__)
#include <malloc.h>
int64_t gputests_heap_in_use(void) { return (int64_t)mallinfo().uordblks; }
#else
int64_t gputests_heap_in_use(void) { return -1; }
#endif

#if defined(__EMSCRIPTEN__)
int64_t gputests_command_buffers_kept(void *instance) { (void)instance; return -1; }
int64_t gputests_private_bytes(void) { return -1; }
#else
#include <string.h>
#include <webgpu/wgpu.h>

int64_t gputests_command_buffers_kept(void *instance) {
    if (instance == NULL) return -1;
    WGPUGlobalReport r;
    memset(&r, 0, sizeof r);
    wgpuGenerateReport((WGPUInstance)instance, &r);
    return (int64_t)r.hub.commandBuffers.numKeptFromUser;
}

#if defined(_WIN32)
#include <psapi.h>
int64_t gputests_private_bytes(void) {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    memset(&pmc, 0, sizeof pmc);
    pmc.cb = sizeof pmc;
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof pmc)) return -1;
    return (int64_t)pmc.PrivateUsage;
}
#elif defined(__APPLE__)
#include <mach/mach.h>
int64_t gputests_private_bytes(void) {
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return -1;
    return (int64_t)info.phys_footprint;
}
#else
#include <stdio.h>
#include <unistd.h>
int64_t gputests_private_bytes(void) {
    FILE *f = fopen("/proc/self/statm", "r");
    if (f == NULL) return -1;
    long size = 0, resident = 0;
    int got = fscanf(f, "%ld %ld", &size, &resident);
    fclose(f);
    return got == 2 ? (int64_t)resident * (int64_t)sysconf(_SC_PAGESIZE) : -1;
}
#endif
#endif
