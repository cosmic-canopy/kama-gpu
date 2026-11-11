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
