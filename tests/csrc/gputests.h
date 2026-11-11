// gputests.h: what the tests ask the system about a window, from outside the package. Declares and never defines (kama
// emits each extern header into one shared header).
#ifndef GPUTESTS_H
#define GPUTESTS_H

#include <stdint.h>

// Minimises `hwnd` (minimised 1) or restores it (0) without activating it: 1 when done, -1 off Windows and on the web.
int32_t gputests_minimise(void *hwnd, int32_t minimised);

#endif
