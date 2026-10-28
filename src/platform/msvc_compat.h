// MSVC keywords, attributes and compiler intrinsics that the port uses
// directly, for Clang on platforms without Windows (Android). The build
// force-includes this header there (-include), because MSVC declares these
// names without any include (for example _byteswap_ulong).
#pragma once

#ifndef _WIN32

#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <cwchar>
#include <strings.h>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

// ---------------------------------------------------------------------------
// Calling conventions, attributes and MSVC keywords.
// ---------------------------------------------------------------------------
#define WINAPI
#define WINAPIV
#define APIENTRY
#define CALLBACK
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif
#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif
// __declspec(x) -> PLATFORM_DECLSPEC_x, so each MSVC attribute maps on its own.
#define __declspec(x) PLATFORM_DECLSPEC_##x
#define PLATFORM_DECLSPEC_noinline __attribute__((noinline))
#define PLATFORM_DECLSPEC_noreturn __attribute__((noreturn))
#define PLATFORM_DECLSPEC_dllexport __attribute__((visibility("default")))
#define PLATFORM_DECLSPEC_dllimport
#define PLATFORM_DECLSPEC_thread thread_local
#define PLATFORM_DECLSPEC_selectany __attribute__((weak))
#define PLATFORM_DECLSPEC_novtable
#define PLATFORM_DECLSPEC_align(n) alignas(n)

// ---------------------------------------------------------------------------
// Compiler intrinsics of MSVC.
// ---------------------------------------------------------------------------
// The widths of MSVC's types (unsigned long is 32-bit on Windows, 64-bit here).
inline uint16_t _byteswap_ushort(uint16_t v) { return __builtin_bswap16(v); }
inline uint32_t _byteswap_ulong(uint32_t v) { return __builtin_bswap32(v); }
inline uint64_t _byteswap_uint64(uint64_t v) { return __builtin_bswap64(v); }
inline unsigned char _BitScanForward(unsigned long* index, unsigned long mask) {
  if (!mask) return 0;
  *index = unsigned(__builtin_ctzl(mask));
  return 1;
}
inline unsigned char _BitScanReverse(unsigned long* index, unsigned long mask) {
  if (!mask) return 0;
  *index = unsigned(63 - __builtin_clzl(mask));
  return 1;
}
inline unsigned char _BitScanForward64(unsigned long* index, unsigned long long mask) {
  if (!mask) return 0;
  *index = unsigned(__builtin_ctzll(mask));
  return 1;
}
inline unsigned char _BitScanReverse64(unsigned long* index, unsigned long long mask) {
  if (!mask) return 0;
  *index = unsigned(63 - __builtin_clzll(mask));
  return 1;
}
inline void YieldProcessor() {
#if defined(__aarch64__)
  __asm__ __volatile__("yield");
#elif defined(__x86_64__) || defined(__i386__)
  _mm_pause();
#endif
}
#define _ReadWriteBarrier() __atomic_signal_fence(__ATOMIC_SEQ_CST)
#define MemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
inline int32_t InterlockedExchange(volatile int32_t* target, int32_t value) {
  return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}
inline int32_t InterlockedIncrement(volatile int32_t* target) {
  return __atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST);
}
inline int32_t InterlockedDecrement(volatile int32_t* target) {
  return __atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST);
}
inline int32_t InterlockedCompareExchange(volatile int32_t* target, int32_t exchange, int32_t comparand) {
  __atomic_compare_exchange_n(target, &comparand, exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return comparand;
}

// MSVC CRT secure variants used by the port.
inline int localtime_s(std::tm* out, const std::time_t* time) { return localtime_r(time, out) ? 0 : errno; }
inline int gmtime_s(std::tm* out, const std::time_t* time) { return gmtime_r(time, out) ? 0 : errno; }
template <size_t N, typename... Args>
inline int sprintf_s(char (&buffer)[N], const char* format, Args... args) {
  return std::snprintf(buffer, N, format, args...);
}
inline int sprintf_s(char* buffer, size_t size, const char* format, ...) {
  va_list list;
  va_start(list, format);
  const int n = std::vsnprintf(buffer, size, format, list);
  va_end(list);
  return n;
}
template <size_t N, typename... Args>
inline int swprintf_s(wchar_t (&buffer)[N], const wchar_t* format, Args... args) {
  return std::swprintf(buffer, N, format, args...);
}
inline int swprintf_s(wchar_t* buffer, size_t size, const wchar_t* format, ...) {
  va_list list;
  va_start(list, format);
  const int n = std::vswprintf(buffer, size, format, list);
  va_end(list);
  return n;
}
template <size_t N>
inline int wcscat_s(wchar_t (&buffer)[N], const wchar_t* source) {
  const size_t used = std::wcslen(buffer);
  const size_t add = std::wcslen(source);
  if (used + add >= N) return ERANGE;
  std::wmemcpy(buffer + used, source, add + 1);
  return 0;
}
inline int wcscat_s(wchar_t* buffer, size_t size, const wchar_t* source) {
  const size_t used = std::wcslen(buffer);
  const size_t add = std::wcslen(source);
  if (used + add >= size) return ERANGE;
  std::wmemcpy(buffer + used, source, add + 1);
  return 0;
}
template <size_t N>
inline int wcscpy_s(wchar_t (&buffer)[N], const wchar_t* source) {
  const size_t n = std::wcslen(source);
  if (n >= N) return ERANGE;
  std::wmemcpy(buffer, source, n + 1);
  return 0;
}
template <size_t N>
inline int strcpy_s(char (&buffer)[N], const char* source) {
  const size_t n = std::strlen(source);
  if (n >= N) return ERANGE;
  std::memcpy(buffer, source, n + 1);
  return 0;
}
inline int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
inline int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }


#endif  // !_WIN32
