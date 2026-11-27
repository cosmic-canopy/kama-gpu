// gputests.h: what the tests ask the system about a window, from outside the package. Declares and never defines (kama
// emits each extern header into one shared header).
#ifndef GPUTESTS_H
#define GPUTESTS_H

#include <stdint.h>

// Minimises `hwnd` (minimised 1) or restores it (0) without activating it: 1 when done, -1 off Windows and on the web.
int32_t gputests_minimise(void *hwnd, int32_t minimised);

// The bytes the C heap holds in use: on the web, where a program's memory is fixed, what a leak would show first; -1
// natively, where the question is not asked.
int64_t gputests_heap_in_use(void);

// Natively, the command buffers wgpu-native's registry keeps for `instance`'s programs (`wgpuGenerateReport`'s
// `hub.commandBuffers.numKeptFromUser`): one finished and released unsubmitted stays there for good (wgpu-native #638,
// GPU-19). -1 on the web, which has no such registry.
int64_t gputests_command_buffers_kept(void *instance);

// The process's private bytes as the system counts them (Windows' PrivateUsage, Linux's resident set, macOS's physical
// footprint): what a leak grows. -1 on the web.
int64_t gputests_private_bytes(void);

#endif
