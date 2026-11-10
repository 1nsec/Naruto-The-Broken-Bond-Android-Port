// The sampling profiler of the performance log on Android: with
// REX_NATIVE_PERF_LOG and REX_NATIVE_FRAME_DIR (Settings > Performance log),
// the functions that take the most CPU time on each busy thread, in
// REX_NATIVE_FRAME_DIR/profile.txt every 30 s. Symbol files of the libraries
// come from BROKENBOND_SYMBOLS_DIR (the APK's assets/symbols, extracted by the
// game activity); without them a sample counts for its library.
#pragma once

namespace narutobb::profiler {
// Starts the profiler once, when the performance log is on.
void Start();
}  // namespace narutobb::profiler
