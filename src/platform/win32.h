// The Win32 API surface of the port. On Windows it is <windows.h> itself; on
// the other platforms (Android) win32_posix.h implements the subset the port's
// renderer and PC features use, over POSIX and SDL3.
#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include "platform/win32_posix.h"
#endif
