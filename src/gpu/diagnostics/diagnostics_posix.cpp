// The Windows-only diagnostics of the native renderer, on platforms without
// Windows (Android): the minidump writer (crash_dump.cpp), the sampling
// profiler (sampler.cpp) and the guest heap watch (heap_watch.cpp) rely on
// Windows exception filters, thread suspension and PE images. Here they are
// inert: the same interface, never enabled. Crashes are reported by
// src/pc/crash_handler_posix.cpp and by the system (logcat, tombstones).
#include "gpu/diagnostics/crash_dump.h"
#include "gpu/diagnostics/heap_watch.h"
#include "gpu/diagnostics/sampler.h"

namespace gpu::crash_dump {
void Install() {}
void NoteFree(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
}  // namespace gpu::crash_dump

namespace gpu::sampler {
bool Enabled() { return false; }
void RegisterThread(const char*) {}
void NoteSwap(uint64_t) {}
}  // namespace gpu::sampler

namespace gpu::heap_watch {
void Install() {}
bool Enabled() { return false; }
void NoteResolvedLock(bool, uint32_t) {}
void WriteReport(const char*) {}
}  // namespace gpu::heap_watch
