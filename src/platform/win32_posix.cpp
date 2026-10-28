// POSIX/SDL implementation of the Win32 subset in win32_posix.h (Android).
#include "platform/win32.h"

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <rex/filesystem.h>
#include <rex/platform/exceptions.h>

namespace win32_posix {
namespace {

thread_local DWORD last_error = ERROR_SUCCESS;

// Kernel objects behind HANDLEs.
enum class Kind { kEvent, kTimer, kFile, kFind, kThread };

struct Object {
  explicit Object(Kind k) : kind(k) {}
  virtual ~Object() = default;
  const Kind kind;
};

struct Event final : Object {
  Event(bool manual_reset, bool initial) : Object(Kind::kEvent), manual(manual_reset), signaled(initial) {}
  std::mutex mutex;
  std::condition_variable cv;
  const bool manual;
  bool signaled;
};

struct Timer final : Object {
  explicit Timer(bool manual_reset) : Object(Kind::kTimer), manual(manual_reset) {}
  std::mutex mutex;
  std::condition_variable cv;
  const bool manual;
  bool armed = false;
  bool signaled = false;
  std::chrono::steady_clock::time_point due{};
  std::chrono::nanoseconds period{0};
};

struct File final : Object {
  explicit File(int descriptor) : Object(Kind::kFile), fd(descriptor) {}
  ~File() override {
    if (fd >= 0) ::close(fd);
  }
  int fd;
};

struct Find final : Object {
  Find() : Object(Kind::kFind) {}
  ~Find() override {
    if (dir) closedir(dir);
  }
  DIR* dir = nullptr;
  std::string folder;
  std::string pattern;
};

struct Thread final : Object {
  explicit Thread(pid_t thread_id) : Object(Kind::kThread), tid(thread_id) {}
  const pid_t tid;
};

constexpr intptr_t kCurrentProcess = -1;
constexpr intptr_t kCurrentThread = -2;

bool IsPseudo(HANDLE handle) {
  const auto value = reinterpret_cast<intptr_t>(handle);
  return value == kCurrentProcess || value == kCurrentThread || handle == nullptr;
}

Object* ToObject(HANDLE handle) {
  if (IsPseudo(handle) || handle == INVALID_HANDLE_VALUE) return nullptr;
  return static_cast<Object*>(handle);
}

pid_t ThreadIdOf(HANDLE handle) {
  if (reinterpret_cast<intptr_t>(handle) == kCurrentThread) return pid_t(syscall(SYS_gettid));
  if (auto* object = ToObject(handle); object && object->kind == Kind::kThread) {
    return static_cast<Thread*>(object)->tid;
  }
  return 0;
}

// The marker of the port's own module (GetModuleHandleW(nullptr)).
char own_module_marker;

std::string PathUtf8(LPCWSTR path) { return path ? Utf8(path) : std::string(); }

void CopyWide(const std::string& text, LPWSTR out, size_t size) {
  if (!out || !size) return;
  const std::wstring wide = Wide(text.c_str());
  const size_t n = std::min(wide.size(), size - 1);
  std::wmemcpy(out, wide.data(), n);
  out[n] = L'\0';
}

FILETIME ToFileTime(const timeval& tv) {
  const uint64_t ticks = uint64_t(tv.tv_sec) * 10'000'000ull + uint64_t(tv.tv_usec) * 10ull;
  return FILETIME{DWORD(ticks), DWORD(ticks >> 32)};
}

SDL_Window* ToWindow(HWND window) { return static_cast<SDL_Window*>(window); }

// A memory copy that fails instead of crashing on an unmapped or protected
// page (the SDK's SEH emulation turns the fault into an exception).
__attribute__((noinline)) void PlainCopy(void* out, const void* in, size_t size) { std::memcpy(out, in, size); }
bool GuardedCopy(void* out, const void* in, size_t size) {
  SEH_TRY {
    PlainCopy(out, in, size);
    return true;
  }
  SEH_CATCH { return false; }
  SEH_END
}

int ToProt(DWORD protect) {
  switch (protect & 0xFF) {
    case PAGE_NOACCESS:
      return PROT_NONE;
    case PAGE_READONLY:
      return PROT_READ;
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
      return PROT_READ | PROT_WRITE;
    case PAGE_EXECUTE:
      return PROT_EXEC;
    case PAGE_EXECUTE_READ:
      return PROT_READ | PROT_EXEC;
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
      return PROT_READ | PROT_WRITE | PROT_EXEC;
    default:
      return PROT_READ | PROT_WRITE;
  }
}

// Win32 virtual keys -> SDL scancodes, for GetAsyncKeyState.
SDL_Scancode ScancodeOf(int vk) {
  if (vk >= 'A' && vk <= 'Z') return SDL_Scancode(SDL_SCANCODE_A + (vk - 'A'));
  if (vk >= '1' && vk <= '9') return SDL_Scancode(SDL_SCANCODE_1 + (vk - '1'));
  if (vk == '0') return SDL_SCANCODE_0;
  if (vk >= VK_F1 && vk <= VK_F12) return SDL_Scancode(SDL_SCANCODE_F1 + (vk - VK_F1));
  if (vk >= VK_NUMPAD0 + 1 && vk <= VK_NUMPAD9) return SDL_Scancode(SDL_SCANCODE_KP_1 + (vk - VK_NUMPAD0 - 1));
  switch (vk) {
    case VK_NUMPAD0: return SDL_SCANCODE_KP_0;
    case VK_BACK: return SDL_SCANCODE_BACKSPACE;
    case VK_TAB: return SDL_SCANCODE_TAB;
    case VK_RETURN: return SDL_SCANCODE_RETURN;
    case VK_PAUSE: return SDL_SCANCODE_PAUSE;
    case VK_CAPITAL: return SDL_SCANCODE_CAPSLOCK;
    case VK_ESCAPE: return SDL_SCANCODE_ESCAPE;
    case VK_SPACE: return SDL_SCANCODE_SPACE;
    case VK_PRIOR: return SDL_SCANCODE_PAGEUP;
    case VK_NEXT: return SDL_SCANCODE_PAGEDOWN;
    case VK_END: return SDL_SCANCODE_END;
    case VK_HOME: return SDL_SCANCODE_HOME;
    case VK_LEFT: return SDL_SCANCODE_LEFT;
    case VK_UP: return SDL_SCANCODE_UP;
    case VK_RIGHT: return SDL_SCANCODE_RIGHT;
    case VK_DOWN: return SDL_SCANCODE_DOWN;
    case VK_INSERT: return SDL_SCANCODE_INSERT;
    case VK_DELETE: return SDL_SCANCODE_DELETE;
    case VK_MULTIPLY: return SDL_SCANCODE_KP_MULTIPLY;
    case VK_ADD: return SDL_SCANCODE_KP_PLUS;
    case VK_SUBTRACT: return SDL_SCANCODE_KP_MINUS;
    case VK_DECIMAL: return SDL_SCANCODE_KP_PERIOD;
    case VK_DIVIDE: return SDL_SCANCODE_KP_DIVIDE;
    case VK_LSHIFT: return SDL_SCANCODE_LSHIFT;
    case VK_RSHIFT: return SDL_SCANCODE_RSHIFT;
    case VK_LCONTROL: return SDL_SCANCODE_LCTRL;
    case VK_RCONTROL: return SDL_SCANCODE_RCTRL;
    case VK_LMENU: return SDL_SCANCODE_LALT;
    case VK_RMENU: return SDL_SCANCODE_RALT;
    default: return SDL_SCANCODE_UNKNOWN;
  }
}

}  // namespace

std::string Utf8(const wchar_t* text) {
  std::string out;
  if (!text) return out;
  for (const wchar_t* p = text; *p; ++p) {
    uint32_t c = uint32_t(*p);
    if (c >= 0xD800 && c <= 0xDBFF && p[1] >= 0xDC00 && p[1] <= 0xDFFF) {  // UTF-16 pair in a wchar_t string
      c = 0x10000 + ((c - 0xD800) << 10) + (uint32_t(p[1]) - 0xDC00);
      ++p;
    }
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xC0 | (c >> 6));
      out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += char(0xE0 | (c >> 12));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    } else {
      out += char(0xF0 | (c >> 18));
      out += char(0x80 | ((c >> 12) & 0x3F));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    }
  }
  return out;
}

std::wstring Wide(const char* text) {
  std::wstring out;
  if (!text) return out;
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  while (*p) {
    uint32_t c = *p++;
    int extra = 0;
    if (c >= 0xF0) {
      c &= 0x07;
      extra = 3;
    } else if (c >= 0xE0) {
      c &= 0x0F;
      extra = 2;
    } else if (c >= 0xC0) {
      c &= 0x1F;
      extra = 1;
    }
    while (extra-- > 0 && (*p & 0xC0) == 0x80) c = (c << 6) | (*p++ & 0x3F);
    out += wchar_t(c);
  }
  return out;
}

}  // namespace win32_posix

using namespace win32_posix;

// ---------------------------------------------------------------------------
// Text.
// ---------------------------------------------------------------------------
int WideCharToMultiByte(UINT, DWORD, LPCWSTR wide, int wide_count, LPSTR out, int out_size, LPCSTR, BOOL* used_default) {
  if (used_default) *used_default = FALSE;
  if (!wide) return 0;
  std::wstring source = wide_count < 0 ? std::wstring(wide) : std::wstring(wide, size_t(wide_count));
  std::string text = Utf8(source.c_str());
  const int needed = int(text.size()) + (wide_count < 0 ? 1 : 0);  // -1 counts the terminator
  if (!out || out_size <= 0) return needed;
  if (out_size < needed) {
    SetLastError(122);  // ERROR_INSUFFICIENT_BUFFER
    return 0;
  }
  std::memcpy(out, text.data(), text.size());
  if (wide_count < 0) out[text.size()] = '\0';
  return needed;
}

int MultiByteToWideChar(UINT, DWORD, LPCSTR text, int count, LPWSTR out, int out_size) {
  if (!text) return 0;
  std::string source = count < 0 ? std::string(text) : std::string(text, size_t(count));
  std::wstring wide = Wide(source.c_str());
  const int needed = int(wide.size()) + (count < 0 ? 1 : 0);
  if (!out || out_size <= 0) return needed;
  if (out_size < needed) return 0;
  std::wmemcpy(out, wide.data(), wide.size());
  if (count < 0) out[wide.size()] = L'\0';
  return needed;
}

// ---------------------------------------------------------------------------
// Errors.
// ---------------------------------------------------------------------------
DWORD GetLastError() { return last_error; }
void SetLastError(DWORD error) { last_error = error; }

// ---------------------------------------------------------------------------
// Processes, threads and handles.
// ---------------------------------------------------------------------------
HANDLE GetCurrentProcess() { return reinterpret_cast<HANDLE>(kCurrentProcess); }
HANDLE GetCurrentThread() { return reinterpret_cast<HANDLE>(kCurrentThread); }
DWORD GetCurrentProcessId() { return DWORD(getpid()); }
DWORD GetCurrentThreadId() { return DWORD(syscall(SYS_gettid)); }

BOOL SetThreadPriority(HANDLE thread, int priority) {
  // Windows priorities as Linux nice values (an Android app may raise a
  // thread to about -10, like the system's display threads).
  int nice = 0;
  if (priority >= THREAD_PRIORITY_TIME_CRITICAL) nice = -10;
  else if (priority >= THREAD_PRIORITY_HIGHEST) nice = -8;
  else if (priority >= THREAD_PRIORITY_ABOVE_NORMAL) nice = -4;
  else if (priority >= THREAD_PRIORITY_NORMAL) nice = 0;
  else if (priority >= THREAD_PRIORITY_BELOW_NORMAL) nice = 4;
  else if (priority >= THREAD_PRIORITY_LOWEST) nice = 8;
  else nice = 19;
  const pid_t tid = ThreadIdOf(thread);
  if (!tid) return FALSE;
  // A refused raise is not an error worth failing for: the thread keeps running.
  setpriority(PRIO_PROCESS, id_t(tid), nice);
  return TRUE;
}

int GetThreadPriority(HANDLE thread) {
  const pid_t tid = ThreadIdOf(thread);
  errno = 0;
  const int nice = tid ? getpriority(PRIO_PROCESS, id_t(tid)) : 0;
  if (nice <= -10) return THREAD_PRIORITY_TIME_CRITICAL;
  if (nice <= -8) return THREAD_PRIORITY_HIGHEST;
  if (nice < 0) return THREAD_PRIORITY_ABOVE_NORMAL;
  if (nice == 0) return THREAD_PRIORITY_NORMAL;
  if (nice <= 4) return THREAD_PRIORITY_BELOW_NORMAL;
  if (nice <= 8) return THREAD_PRIORITY_LOWEST;
  return THREAD_PRIORITY_IDLE;
}

HRESULT SetThreadDescription(HANDLE thread, PCWSTR name) {
  if (reinterpret_cast<intptr_t>(thread) != kCurrentThread || !name) return E_FAIL;
  std::string text = Utf8(name);
  if (text.size() > 15) text.resize(15);  // The kernel's limit (16 with the terminator).
  pthread_setname_np(pthread_self(), text.c_str());
  return S_OK;
}

BOOL GetThreadTimes(HANDLE thread, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user) {
  if (reinterpret_cast<intptr_t>(thread) != kCurrentThread &&
      ThreadIdOf(thread) != pid_t(syscall(SYS_gettid))) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
  }
  rusage usage{};
  if (getrusage(RUSAGE_THREAD, &usage) != 0) return FALSE;
  if (creation) *creation = {};
  if (exit) *exit = {};
  if (kernel) *kernel = ToFileTime(usage.ru_stime);
  if (user) *user = ToFileTime(usage.ru_utime);
  return TRUE;
}

BOOL GetProcessTimes(HANDLE, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user) {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return FALSE;
  if (creation) *creation = {};
  if (exit) *exit = {};
  if (kernel) *kernel = ToFileTime(usage.ru_stime);
  if (user) *user = ToFileTime(usage.ru_utime);
  return TRUE;
}

DWORD GetActiveProcessorCount(WORD) {
  const long count = sysconf(_SC_NPROCESSORS_ONLN);
  return count > 0 ? DWORD(count) : 1;
}

BOOL TerminateProcess(HANDLE process, UINT exit_code) {
  if (reinterpret_cast<intptr_t>(process) != kCurrentProcess) return FALSE;
  _exit(int(exit_code));
}

void ExitProcess(UINT exit_code) { std::exit(int(exit_code)); }

void Sleep(DWORD milliseconds) {
  if (milliseconds == 0) {
    sched_yield();
    return;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

BOOL SwitchToThread() { return sched_yield() == 0; }

BOOL CloseHandle(HANDLE handle) {
  if (IsPseudo(handle)) return TRUE;
  if (handle == INVALID_HANDLE_VALUE) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  delete static_cast<Object*>(handle);
  return TRUE;
}

BOOL DuplicateHandle(HANDLE, HANDLE source, HANDLE, LPHANDLE target, DWORD, BOOL, DWORD) {
  // Only the current thread's pseudo handle is duplicated by the port.
  if (!target) return FALSE;
  if (reinterpret_cast<intptr_t>(source) == kCurrentThread) {
    *target = new Thread(pid_t(syscall(SYS_gettid)));
    return TRUE;
  }
  SetLastError(ERROR_NOT_SUPPORTED);
  return FALSE;
}

HANDLE OpenThread(DWORD, BOOL, DWORD thread_id) { return thread_id ? new Thread(pid_t(thread_id)) : nullptr; }

// Suspending another thread and reading its registers is the Windows-only
// sampling profiler's business; it is unavailable here.
DWORD SuspendThread(HANDLE) {
  SetLastError(ERROR_NOT_SUPPORTED);
  return DWORD(-1);
}
DWORD ResumeThread(HANDLE) {
  SetLastError(ERROR_NOT_SUPPORTED);
  return DWORD(-1);
}
BOOL GetThreadContext(HANDLE, CONTEXT*) {
  SetLastError(ERROR_NOT_SUPPORTED);
  return FALSE;
}

void GetCurrentThreadStackLimits(PULONG_PTR low, PULONG_PTR high) {
  pthread_attr_t attr;
  void* address = nullptr;
  size_t size = 0;
  if (pthread_getattr_np(pthread_self(), &attr) == 0) {
    pthread_attr_getstack(&attr, &address, &size);
    pthread_attr_destroy(&attr);
  }
  if (low) *low = ULONG_PTR(address);
  if (high) *high = ULONG_PTR(address) + size;
}

// ---------------------------------------------------------------------------
// Events and waitable timers.
// ---------------------------------------------------------------------------
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES, BOOL manual_reset, BOOL initial_state, LPCWSTR) {
  return new Event(manual_reset != FALSE, initial_state != FALSE);
}
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES, BOOL manual_reset, BOOL initial_state, LPCSTR) {
  return new Event(manual_reset != FALSE, initial_state != FALSE);
}
HANDLE CreateEventExW(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD flags, DWORD) {
  return new Event((flags & CREATE_EVENT_MANUAL_RESET) != 0, (flags & CREATE_EVENT_INITIAL_SET) != 0);
}

BOOL SetEvent(HANDLE handle) {
  auto* object = ToObject(handle);
  if (!object || object->kind != Kind::kEvent) return FALSE;
  auto* event = static_cast<Event*>(object);
  {
    std::lock_guard lock(event->mutex);
    event->signaled = true;
  }
  if (event->manual) event->cv.notify_all();
  else event->cv.notify_one();
  return TRUE;
}

BOOL ResetEvent(HANDLE handle) {
  auto* object = ToObject(handle);
  if (!object || object->kind != Kind::kEvent) return FALSE;
  auto* event = static_cast<Event*>(object);
  std::lock_guard lock(event->mutex);
  event->signaled = false;
  return TRUE;
}

namespace {
template <typename Predicate>
bool WaitUntil(std::condition_variable& cv, std::unique_lock<std::mutex>& lock, DWORD milliseconds,
               Predicate predicate) {
  if (milliseconds == INFINITE) {
    cv.wait(lock, predicate);
    return true;
  }
  return cv.wait_for(lock, std::chrono::milliseconds(milliseconds), predicate);
}

DWORD WaitTimer(Timer* timer, DWORD milliseconds) {
  std::unique_lock lock(timer->mutex);
  const auto limit = milliseconds == INFINITE ? std::chrono::steady_clock::time_point::max()
                                              : std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  for (;;) {
    if (timer->signaled) {
      if (!timer->manual) timer->signaled = false;
      return WAIT_OBJECT_0;
    }
    if (timer->armed) {
      const auto due = timer->due;
      if (due <= std::chrono::steady_clock::now()) {
        if (timer->period.count() > 0) timer->due += timer->period;
        else timer->armed = false;
        if (timer->manual) timer->signaled = true;
        return WAIT_OBJECT_0;
      }
      if (due > limit) {
        lock.unlock();
        std::this_thread::sleep_until(limit);
        return WAIT_TIMEOUT;
      }
      // Precise sleep to the due time (frame pacing): the absolute monotonic
      // clock, not a condition variable's coarser wake-up.
      lock.unlock();
      const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(due.time_since_epoch()).count();
      timespec ts{time_t(ns / 1'000'000'000), long(ns % 1'000'000'000)};
      while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
      }
      lock.lock();
      continue;
    }
    if (std::chrono::steady_clock::now() >= limit) return WAIT_TIMEOUT;
    if (milliseconds == INFINITE) timer->cv.wait(lock);
    else timer->cv.wait_until(lock, limit);
  }
}
}  // namespace

DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds) {
  auto* object = ToObject(handle);
  if (!object) {
    SetLastError(ERROR_INVALID_HANDLE);
    return WAIT_FAILED;
  }
  if (object->kind == Kind::kEvent) {
    auto* event = static_cast<Event*>(object);
    std::unique_lock lock(event->mutex);
    if (!WaitUntil(event->cv, lock, milliseconds, [event] { return event->signaled; })) return WAIT_TIMEOUT;
    if (!event->manual) event->signaled = false;
    return WAIT_OBJECT_0;
  }
  if (object->kind == Kind::kTimer) return WaitTimer(static_cast<Timer*>(object), milliseconds);
  SetLastError(ERROR_NOT_SUPPORTED);
  return WAIT_FAILED;
}

DWORD WaitForSingleObjectEx(HANDLE handle, DWORD milliseconds, BOOL) { return WaitForSingleObject(handle, milliseconds); }

DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL wait_all, DWORD milliseconds) {
  // Polled: the port waits on few handles, rarely.
  const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  for (;;) {
    if (wait_all) {
      bool all = true;
      for (DWORD i = 0; i < count; ++i) {
        auto* object = ToObject(handles[i]);
        if (object && object->kind == Kind::kEvent) {
          auto* event = static_cast<Event*>(object);
          std::lock_guard lock(event->mutex);
          all = all && event->signaled;
        } else {
          all = false;
        }
      }
      if (all) {
        for (DWORD i = 0; i < count; ++i) WaitForSingleObject(handles[i], 0);
        return WAIT_OBJECT_0;
      }
    } else {
      for (DWORD i = 0; i < count; ++i) {
        if (WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0) return WAIT_OBJECT_0 + i;
      }
    }
    if (milliseconds != INFINITE && std::chrono::steady_clock::now() >= limit) return WAIT_TIMEOUT;
    std::this_thread::sleep_for(std::chrono::microseconds(500));
  }
}

HANDLE CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD flags, DWORD) {
  return new Timer((flags & CREATE_WAITABLE_TIMER_MANUAL_RESET) != 0);
}
HANDLE CreateWaitableTimerW(LPSECURITY_ATTRIBUTES, BOOL manual_reset, LPCWSTR) { return new Timer(manual_reset != FALSE); }

BOOL SetWaitableTimer(HANDLE handle, const LARGE_INTEGER* due_time, LONG period, PTIMERAPCROUTINE, LPVOID, BOOL) {
  auto* object = ToObject(handle);
  if (!object || object->kind != Kind::kTimer || !due_time) return FALSE;
  auto* timer = static_cast<Timer*>(object);
  std::chrono::steady_clock::time_point due;
  if (due_time->QuadPart < 0) {
    // Relative, in 100 ns units.
    due = std::chrono::steady_clock::now() + std::chrono::nanoseconds(-due_time->QuadPart * 100);
  } else {
    // Absolute UTC FILETIME: converted through the system clock.
    const int64_t unix_100ns = due_time->QuadPart - 116444736000000000LL;
    const auto system_due = std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(unix_100ns * 100)));
    due = std::chrono::steady_clock::now() + (system_due - std::chrono::system_clock::now());
  }
  {
    std::lock_guard lock(timer->mutex);
    timer->due = due;
    timer->armed = true;
    timer->signaled = false;
    timer->period = std::chrono::milliseconds(period);
  }
  timer->cv.notify_all();
  return TRUE;
}

BOOL SetWaitableTimerEx(HANDLE timer, const LARGE_INTEGER* due_time, LONG period, PTIMERAPCROUTINE completion,
                        LPVOID argument, PREASON_CONTEXT, ULONG) {
  return SetWaitableTimer(timer, due_time, period, completion, argument, FALSE);
}

BOOL CancelWaitableTimer(HANDLE handle) {
  auto* object = ToObject(handle);
  if (!object || object->kind != Kind::kTimer) return FALSE;
  auto* timer = static_cast<Timer*>(object);
  std::lock_guard lock(timer->mutex);
  timer->armed = false;
  return TRUE;
}

// ---------------------------------------------------------------------------
// Time.
// ---------------------------------------------------------------------------
BOOL QueryPerformanceCounter(LARGE_INTEGER* count) {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  count->QuadPart = LONGLONG(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
  return TRUE;
}
BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency) {
  frequency->QuadPart = 1'000'000'000LL;
  return TRUE;
}
ULONGLONG GetTickCount64() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ULONGLONG(ts.tv_sec) * 1000ull + ULONGLONG(ts.tv_nsec) / 1'000'000ull;
}
DWORD GetTickCount() { return DWORD(GetTickCount64()); }
DWORD timeGetTime() { return DWORD(GetTickCount64()); }
UINT timeBeginPeriod(UINT) { return TIMERR_NOERROR; }
UINT timeEndPeriod(UINT) { return TIMERR_NOERROR; }
void GetSystemTimeAsFileTime(LPFILETIME time) {
  timeval tv;
  gettimeofday(&tv, nullptr);
  const uint64_t ticks = uint64_t(tv.tv_sec) * 10'000'000ull + uint64_t(tv.tv_usec) * 10ull + 116444736000000000ull;
  *time = FILETIME{DWORD(ticks), DWORD(ticks >> 32)};
}

// ---------------------------------------------------------------------------
// Memory.
// ---------------------------------------------------------------------------
LPVOID VirtualAlloc(LPVOID address, SIZE_T size, DWORD allocation_type, DWORD protect) {
  const int prot = (allocation_type & MEM_COMMIT) ? ToProt(protect) : PROT_NONE;
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
  if (address) flags |= MAP_FIXED;
  void* result = mmap(address, size, prot, flags, -1, 0);
  return result == MAP_FAILED ? nullptr : result;
}

BOOL VirtualFree(LPVOID address, SIZE_T size, DWORD free_type) {
  if (free_type & MEM_DECOMMIT) return mprotect(address, size, PROT_NONE) == 0;
  // MEM_RELEASE passes size 0 on Windows; the mapping's own size is unknown
  // here, so the caller's allocation is left mapped (diagnostics only).
  return size ? munmap(address, size) == 0 : TRUE;
}

BOOL VirtualProtect(LPVOID address, SIZE_T size, DWORD new_protect, PDWORD old_protect) {
  const uintptr_t page = uintptr_t(sysconf(_SC_PAGESIZE));
  const uintptr_t begin = uintptr_t(address) & ~(page - 1);
  const uintptr_t end = (uintptr_t(address) + size + page - 1) & ~(page - 1);
  if (old_protect) *old_protect = PAGE_READWRITE;  // Not tracked by the kernel's API.
  return mprotect(reinterpret_cast<void*>(begin), end - begin, ToProt(new_protect)) == 0;
}

SIZE_T VirtualQuery(LPCVOID address, MEMORY_BASIC_INFORMATION* info, SIZE_T length) {
  // Readability by a guarded one-byte probe: cheap when the page is readable,
  // which is the case the port asks about.
  if (!info || length < sizeof(MEMORY_BASIC_INFORMATION)) return 0;
  const uintptr_t page = uintptr_t(sysconf(_SC_PAGESIZE));
  uint8_t byte;
  const bool readable = GuardedCopy(&byte, address, 1);
  *info = {};
  info->BaseAddress = reinterpret_cast<PVOID>(uintptr_t(address) & ~(page - 1));
  info->AllocationBase = info->BaseAddress;
  info->RegionSize = page;
  info->State = readable ? MEM_COMMIT : MEM_RESERVE;
  info->Protect = readable ? PAGE_READWRITE : PAGE_NOACCESS;
  info->AllocationProtect = info->Protect;
  info->Type = MEM_PRIVATE;
  return sizeof(MEMORY_BASIC_INFORMATION);
}

BOOL ReadProcessMemory(HANDLE process, LPCVOID base, LPVOID buffer, SIZE_T size, SIZE_T* read) {
  if (read) *read = 0;
  if (reinterpret_cast<intptr_t>(process) != kCurrentProcess) return FALSE;
  if (!GuardedCopy(buffer, base, size)) {
    SetLastError(299);  // ERROR_PARTIAL_COPY
    return FALSE;
  }
  if (read) *read = size;
  return TRUE;
}

BOOL WriteProcessMemory(HANDLE process, LPVOID base, LPCVOID buffer, SIZE_T size, SIZE_T* written) {
  if (written) *written = 0;
  if (reinterpret_cast<intptr_t>(process) != kCurrentProcess) return FALSE;
  if (!GuardedCopy(base, buffer, size)) {
    SetLastError(299);
    return FALSE;
  }
  if (written) *written = size;
  return TRUE;
}

BOOL FlushInstructionCache(HANDLE, LPCVOID base, SIZE_T size) {
  __builtin___clear_cache(static_cast<char*>(const_cast<void*>(base)),
                          static_cast<char*>(const_cast<void*>(base)) + size);
  return TRUE;
}

// ---------------------------------------------------------------------------
// Files.
// ---------------------------------------------------------------------------
HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD, LPSECURITY_ATTRIBUTES, DWORD disposition, DWORD flags, HANDLE) {
  if (!path) return INVALID_HANDLE_VALUE;
  int open_flags = O_CLOEXEC;
  if ((access & GENERIC_READ) && (access & GENERIC_WRITE)) open_flags |= O_RDWR;
  else if (access & GENERIC_WRITE) open_flags |= O_WRONLY;
  else open_flags |= O_RDONLY;
  switch (disposition) {
    case CREATE_NEW: open_flags |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: open_flags |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: open_flags |= O_CREAT; break;
    case TRUNCATE_EXISTING: open_flags |= O_TRUNC; break;
    default: break;
  }
  if (flags & FILE_FLAG_WRITE_THROUGH) open_flags |= O_SYNC;
  const int fd = ::open(path, open_flags, 0644);
  if (fd < 0) {
    SetLastError(errno == ENOENT ? ERROR_FILE_NOT_FOUND : errno == EEXIST ? ERROR_ALREADY_EXISTS : ERROR_ACCESS_DENIED);
    return INVALID_HANDLE_VALUE;
  }
  return new File(fd);
}

HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attributes, DWORD disposition,
                   DWORD flags, HANDLE template_file) {
  const std::string text = PathUtf8(path);
  return CreateFileA(text.c_str(), access, share, attributes, disposition, flags, template_file);
}

BOOL ReadFile(HANDLE handle, LPVOID buffer, DWORD size, LPDWORD read, LPOVERLAPPED) {
  if (read) *read = 0;
  auto* object = ToObject(handle);
  if (!object || object->kind != Kind::kFile) return FALSE;
  const int fd = static_cast<File*>(object)->fd;
  size_t done = 0;
  while (done < size) {
    const ssize_t n = ::read(fd, static_cast<uint8_t*>(buffer) + done, size - done);
    if (n < 0) {
      if (errno == EINTR) continue;
      return FALSE;
    }
    if (n == 0) break;
    done += size_t(n);
  }
  if (read) *read = DWORD(done);
  return TRUE;
}

BOOL WriteFile(HANDLE handle, LPCVOID buffer, DWORD size, LPDWORD written, LPOVERLAPPED) {
  if (written) *written = 0;
  auto* object = ToObject(handle);
  if (!object || object->kind != Kind::kFile) return FALSE;
  const int fd = static_cast<File*>(object)->fd;
  size_t done = 0;
  while (done < size) {
    const ssize_t n = ::write(fd, static_cast<const uint8_t*>(buffer) + done, size - done);
    if (n < 0) {
      if (errno == EINTR) continue;
      return FALSE;
    }
    done += size_t(n);
  }
  if (written) *written = DWORD(done);
  return TRUE;
}

BOOL FlushFileBuffers(HANDLE handle) {
  auto* object = ToObject(handle);
  if (!object || object->kind != Kind::kFile) return FALSE;
  return fsync(static_cast<File*>(object)->fd) == 0;
}

BOOL GetFileSizeEx(HANDLE handle, LARGE_INTEGER* size) {
  auto* object = ToObject(handle);
  if (!object || object->kind != Kind::kFile || !size) return FALSE;
  struct stat st {};
  if (fstat(static_cast<File*>(object)->fd, &st) != 0) return FALSE;
  size->QuadPart = st.st_size;
  return TRUE;
}

BOOL DeleteFileW(LPCWSTR path) { return ::unlink(PathUtf8(path).c_str()) == 0; }

BOOL CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES) {
  if (::mkdir(PathUtf8(path).c_str(), 0755) == 0) return TRUE;
  SetLastError(errno == EEXIST ? ERROR_ALREADY_EXISTS : ERROR_ACCESS_DENIED);
  return FALSE;
}

DWORD GetFileAttributesW(LPCWSTR path) {
  struct stat st {};
  if (::stat(PathUtf8(path).c_str(), &st) != 0) return INVALID_FILE_ATTRIBUTES;
  return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

DWORD GetTempPathW(DWORD size, LPWSTR buffer) {
  // The app's cache folder (TMPDIR, set by the launcher), with a trailing slash.
  std::string folder;
  if (const char* tmp = std::getenv("TMPDIR"); tmp && *tmp) folder = tmp;
  else folder = (rex::filesystem::GetExecutableFolder() / "cache").string();
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  if (folder.empty() || folder.back() != '/') folder += '/';
  const std::wstring wide = Wide(folder.c_str());
  if (!buffer || size <= wide.size()) return DWORD(wide.size() + 1);
  CopyWide(folder, buffer, size);
  return DWORD(wide.size());
}

UINT GetTempFileNameW(LPCWSTR path, LPCWSTR prefix, UINT unique, LPWSTR out) {
  std::string base = PathUtf8(path);
  if (!base.empty() && base.back() != '/') base += '/';
  std::string name = base + PathUtf8(prefix).substr(0, 3);
  if (unique) {
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "%X.tmp", unique & 0xFFFF);
    CopyWide(name + suffix, out, MAX_PATH);
    return unique;
  }
  std::string templ = name + "XXXXXX";
  std::vector<char> buffer(templ.begin(), templ.end());
  buffer.push_back('\0');
  const int fd = mkstemp(buffer.data());
  if (fd < 0) return 0;
  ::close(fd);
  CopyWide(buffer.data(), out, MAX_PATH);
  return 1;
}

namespace {
bool NextMatch(Find* find, WIN32_FIND_DATAW* data) {
  while (dirent* entry = readdir(find->dir)) {
    if (fnmatch(find->pattern.c_str(), entry->d_name, 0) != 0) continue;
    *data = {};
    const std::string full = find->folder + "/" + entry->d_name;
    struct stat st {};
    if (::stat(full.c_str(), &st) == 0) {
      data->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
      data->nFileSizeHigh = DWORD(uint64_t(st.st_size) >> 32);
      data->nFileSizeLow = DWORD(st.st_size);
    }
    CopyWide(entry->d_name, data->cFileName, 260);
    return true;
  }
  return false;
}
}  // namespace

HANDLE FindFirstFileW(LPCWSTR pattern, WIN32_FIND_DATAW* data) {
  const std::filesystem::path path(PathUtf8(pattern));
  auto* find = new Find();
  find->folder = path.parent_path().empty() ? std::string(".") : path.parent_path().string();
  find->pattern = path.filename().string();
  find->dir = opendir(find->folder.c_str());
  if (!find->dir || !NextMatch(find, data)) {
    delete find;
    SetLastError(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
  }
  return find;
}

BOOL FindNextFileW(HANDLE handle, WIN32_FIND_DATAW* data) {
  auto* object = ToObject(handle);
  if (!object || object->kind != Kind::kFind) return FALSE;
  if (NextMatch(static_cast<Find*>(object), data)) return TRUE;
  SetLastError(ERROR_NO_MORE_FILES);
  return FALSE;
}

BOOL FindClose(HANDLE handle) { return CloseHandle(handle); }

FILE* _wfopen(const wchar_t* path, const wchar_t* mode) {
  return std::fopen(PathUtf8(path).c_str(), Utf8(mode).c_str());
}

int _wputenv_s(const wchar_t* name, const wchar_t* value) {
  return setenv(Utf8(name).c_str(), Utf8(value).c_str(), 1);
}
int _putenv_s(const char* name, const char* value) { return setenv(name, value, 1); }

DWORD GetEnvironmentVariableW(LPCWSTR name, LPWSTR buffer, DWORD size) {
  const char* value = std::getenv(Utf8(name).c_str());
  if (!value) {
    SetLastError(203);  // ERROR_ENVVAR_NOT_FOUND
    return 0;
  }
  const std::wstring wide = Wide(value);
  if (!buffer || size <= wide.size()) return DWORD(wide.size() + 1);
  CopyWide(value, buffer, size);
  return DWORD(wide.size());
}

// ---------------------------------------------------------------------------
// Modules.
// ---------------------------------------------------------------------------
HMODULE GetModuleHandleW(LPCWSTR name) { return name ? nullptr : static_cast<HMODULE>(&own_module_marker); }
HMODULE GetModuleHandleA(LPCSTR name) { return name ? nullptr : static_cast<HMODULE>(&own_module_marker); }

BOOL GetModuleHandleExA(DWORD, LPCSTR, HMODULE* module) {
  if (module) *module = static_cast<HMODULE>(&own_module_marker);
  return TRUE;
}

DWORD GetModuleFileNameA(HMODULE, LPSTR buffer, DWORD size) {
  // The executable's path: on Android, a name in the app's data folder (the
  // SDK's stand-in for the executable's folder), so files "next to the
  // executable" land there.
  const std::string path = rex::filesystem::GetExecutablePath().string();
  if (!buffer || !size) return 0;
  const size_t n = std::min<size_t>(path.size(), size - 1);
  std::memcpy(buffer, path.data(), n);
  buffer[n] = '\0';
  return DWORD(n);
}

DWORD GetModuleFileNameW(HMODULE module, LPWSTR buffer, DWORD size) {
  char path[4096];
  GetModuleFileNameA(module, path, sizeof(path));
  CopyWide(path, buffer, size);
  return buffer ? DWORD(std::wcslen(buffer)) : 0;
}

HMODULE LoadLibraryA(LPCSTR name) { return name ? dlopen(name, RTLD_NOW | RTLD_LOCAL) : nullptr; }
HMODULE LoadLibraryW(LPCWSTR name) {
  const std::string text = PathUtf8(name);
  return LoadLibraryA(text.c_str());
}
BOOL FreeLibrary(HMODULE module) {
  if (!module || module == &own_module_marker) return TRUE;
  return dlclose(module) == 0;
}
FARPROC GetProcAddress(HMODULE module, LPCSTR name) {
  if (!name) return nullptr;
  void* handle = (!module || module == &own_module_marker) ? RTLD_DEFAULT : module;
  return reinterpret_cast<FARPROC>(dlsym(handle, name));
}

// ---------------------------------------------------------------------------
// Resources: the table of embedded files from resources_posix.cpp.
// ---------------------------------------------------------------------------
namespace win32_posix {
struct ResourceEntry {
  int id;
  const unsigned char* data;
  size_t size;
};
// Defined in the generated resources_posix.cpp; ends with an id of 0.
extern const ResourceEntry kResources[];
}  // namespace win32_posix

HRSRC FindResourceW(HMODULE, LPCWSTR name, LPCWSTR) {
  if (!IS_INTRESOURCE(name)) return nullptr;
  const int id = int(reinterpret_cast<uintptr_t>(name));
  for (const ResourceEntry* entry = kResources; entry->id; ++entry) {
    if (entry->id == id) return const_cast<ResourceEntry*>(entry);
  }
  SetLastError(1814);  // ERROR_RESOURCE_NAME_NOT_FOUND
  return nullptr;
}
HGLOBAL LoadResource(HMODULE, HRSRC resource) { return resource; }
LPVOID LockResource(HGLOBAL data) {
  return data ? const_cast<unsigned char*>(static_cast<ResourceEntry*>(data)->data) : nullptr;
}
DWORD SizeofResource(HMODULE, HRSRC resource) {
  return resource ? DWORD(static_cast<ResourceEntry*>(resource)->size) : 0;
}

// ---------------------------------------------------------------------------
// Windows and the screen (HWND = SDL_Window*).
// ---------------------------------------------------------------------------
BOOL IsWindow(HWND window) { return window != nullptr; }

BOOL IsIconic(HWND window) {
  if (!window) return FALSE;
  return (SDL_GetWindowFlags(ToWindow(window)) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0;
}

BOOL IsWindowVisible(HWND window) {
  if (!window) return FALSE;
  return (SDL_GetWindowFlags(ToWindow(window)) & SDL_WINDOW_HIDDEN) == 0;
}

BOOL GetClientRect(HWND window, LPRECT rect) {
  if (!window || !rect) return FALSE;
  int w = 0, h = 0;
  if (!SDL_GetWindowSizeInPixels(ToWindow(window), &w, &h)) return FALSE;
  *rect = RECT{0, 0, LONG(w), LONG(h)};
  return TRUE;
}

BOOL GetWindowRect(HWND window, LPRECT rect) {
  if (!window || !rect) return FALSE;
  int x = 0, y = 0, w = 0, h = 0;
  SDL_GetWindowPosition(ToWindow(window), &x, &y);
  if (!SDL_GetWindowSizeInPixels(ToWindow(window), &w, &h)) return FALSE;
  *rect = RECT{LONG(x), LONG(y), LONG(x + w), LONG(y + h)};
  return TRUE;
}

BOOL SetWindowPos(HWND window, HWND, int x, int y, int cx, int cy, UINT flags) {
  if (!window) return FALSE;
  if (!(flags & SWP_NOSIZE)) SDL_SetWindowSize(ToWindow(window), cx, cy);
  if (!(flags & SWP_NOMOVE)) SDL_SetWindowPosition(ToWindow(window), x, y);
  return TRUE;
}

BOOL ShowWindow(HWND window, int command) {
  if (!window) return FALSE;
  switch (command) {
    case SW_HIDE: SDL_HideWindow(ToWindow(window)); break;
    case SW_MINIMIZE: SDL_MinimizeWindow(ToWindow(window)); break;
    case SW_RESTORE: SDL_RestoreWindow(ToWindow(window)); break;
    default: SDL_ShowWindow(ToWindow(window)); break;
  }
  return TRUE;
}

BOOL SetForegroundWindow(HWND window) { return window && SDL_RaiseWindow(ToWindow(window)); }
HWND GetForegroundWindow() { return SDL_GetKeyboardFocus(); }

DWORD GetWindowThreadProcessId(HWND window, LPDWORD process_id) {
  if (process_id) *process_id = window ? DWORD(getpid()) : 0;
  return 0;
}

BOOL SetWindowTextW(HWND window, LPCWSTR text) {
  if (!window) return FALSE;
  return SDL_SetWindowTitle(ToWindow(window), PathUtf8(text).c_str());
}

BOOL PostMessageW(HWND window, UINT message, WPARAM, LPARAM) {
  if (message != WM_CLOSE && message != WM_QUIT) return FALSE;
  SDL_Event event{};
  if (window && message == WM_CLOSE) {
    event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    event.window.windowID = SDL_GetWindowID(ToWindow(window));
  } else {
    event.type = SDL_EVENT_QUIT;
  }
  return SDL_PushEvent(&event);
}

UINT GetDpiForWindow(HWND window) {
  const float scale = window ? SDL_GetWindowDisplayScale(ToWindow(window)) : 1.0f;
  return UINT(96.0f * (scale > 0.0f ? scale : 1.0f));
}

HMONITOR MonitorFromWindow(HWND window, DWORD) {
  const SDL_DisplayID display = window ? SDL_GetDisplayForWindow(ToWindow(window)) : SDL_GetPrimaryDisplay();
  return display ? reinterpret_cast<HMONITOR>(uintptr_t(display)) : nullptr;
}

BOOL GetMonitorInfoW(HMONITOR monitor, MONITORINFO* info) {
  if (!monitor || !info) return FALSE;
  const auto display = SDL_DisplayID(reinterpret_cast<uintptr_t>(monitor));
  SDL_Rect bounds{}, usable{};
  if (!SDL_GetDisplayBounds(display, &bounds)) return FALSE;
  if (!SDL_GetDisplayUsableBounds(display, &usable)) usable = bounds;
  info->rcMonitor = RECT{bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h};
  info->rcWork = RECT{usable.x, usable.y, usable.x + usable.w, usable.y + usable.h};
  info->dwFlags = display == SDL_GetPrimaryDisplay() ? 1u : 0u;  // MONITORINFOF_PRIMARY
  if (info->cbSize >= sizeof(MONITORINFOEXW)) {
    const char* name = SDL_GetDisplayName(display);
    CopyWide(name ? name : "display", static_cast<MONITORINFOEXW*>(info)->szDevice, 32);
  }
  return TRUE;
}

BOOL EnumDisplaySettingsW(LPCWSTR, DWORD, DEVMODEW* devmode) {
  if (!devmode) return FALSE;
  const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
  if (!mode) return FALSE;
  devmode->dmPelsWidth = DWORD(mode->w * (mode->pixel_density > 0 ? mode->pixel_density : 1.0f));
  devmode->dmPelsHeight = DWORD(mode->h * (mode->pixel_density > 0 ? mode->pixel_density : 1.0f));
  devmode->dmDisplayFrequency = DWORD(mode->refresh_rate + 0.5f);
  return TRUE;
}

int GetSystemMetrics(int index) {
  const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
  if (!mode) return 0;
  const float density = mode->pixel_density > 0 ? mode->pixel_density : 1.0f;
  if (index == SM_CXSCREEN) return int(mode->w * density);
  if (index == SM_CYSCREEN) return int(mode->h * density);
  return 0;
}

DPI_AWARENESS_CONTEXT GetWindowDpiAwarenessContext(HWND) { return reinterpret_cast<DPI_AWARENESS_CONTEXT>(-4); }
DPI_AWARENESS GetAwarenessFromDpiAwarenessContext(DPI_AWARENESS_CONTEXT) { return DPI_AWARENESS_PER_MONITOR_AWARE; }

int MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type) {
  const SDL_MessageBoxFlags flags = (type & 0xF0) == MB_ICONERROR     ? SDL_MESSAGEBOX_ERROR
                                    : (type & 0xF0) == MB_ICONWARNING ? SDL_MESSAGEBOX_WARNING
                                                                       : SDL_MESSAGEBOX_INFORMATION;
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_ERROR, "narutobb", "%s: %s", caption ? caption : "", text ? text : "");
#endif
  SDL_ShowSimpleMessageBox(flags, caption ? caption : "", text ? text : "", ToWindow(owner));
  return IDOK;
}

int MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type) {
  return MessageBoxA(owner, PathUtf8(text).c_str(), PathUtf8(caption).c_str(), type);
}

void OutputDebugStringA(LPCSTR text) {
#if defined(__ANDROID__)
  __android_log_write(ANDROID_LOG_DEBUG, "narutobb", text ? text : "");
#else
  std::fputs(text ? text : "", stderr);
#endif
}
void OutputDebugStringW(LPCWSTR text) { OutputDebugStringA(PathUtf8(text).c_str()); }

// ---------------------------------------------------------------------------
// Keyboard and mouse state.
// ---------------------------------------------------------------------------
SHORT GetAsyncKeyState(int virtual_key) {
  if (virtual_key == VK_LBUTTON || virtual_key == VK_RBUTTON || virtual_key == VK_MBUTTON) {
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(nullptr, nullptr);
    const SDL_MouseButtonFlags mask = virtual_key == VK_LBUTTON   ? SDL_BUTTON_LMASK
                                      : virtual_key == VK_RBUTTON ? SDL_BUTTON_RMASK
                                                                  : SDL_BUTTON_MMASK;
    return (buttons & mask) ? SHORT(0x8000) : SHORT(0);
  }
  int count = 0;
  const bool* state = SDL_GetKeyboardState(&count);
  if (!state) return 0;
  auto down = [&](SDL_Scancode code) { return code > SDL_SCANCODE_UNKNOWN && int(code) < count && state[code]; };
  bool pressed = false;
  switch (virtual_key) {
    case VK_SHIFT: pressed = down(SDL_SCANCODE_LSHIFT) || down(SDL_SCANCODE_RSHIFT); break;
    case VK_CONTROL: pressed = down(SDL_SCANCODE_LCTRL) || down(SDL_SCANCODE_RCTRL); break;
    case VK_MENU: pressed = down(SDL_SCANCODE_LALT) || down(SDL_SCANCODE_RALT); break;
    default: pressed = down(ScancodeOf(virtual_key)); break;
  }
  return pressed ? SHORT(0x8000) : SHORT(0);
}

SHORT GetKeyState(int virtual_key) { return GetAsyncKeyState(virtual_key); }

// ---------------------------------------------------------------------------
// Sound and Windows exception handling: unavailable here.
// ---------------------------------------------------------------------------
BOOL PlaySoundW(LPCWSTR, HMODULE, DWORD) { return FALSE; }

PVOID AddVectoredExceptionHandler(ULONG, PVECTORED_EXCEPTION_HANDLER) {
  SetLastError(ERROR_NOT_SUPPORTED);
  return nullptr;
}
ULONG RemoveVectoredExceptionHandler(PVOID) { return 0; }
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER) { return nullptr; }
USHORT RtlCaptureStackBackTrace(ULONG, ULONG, PVOID*, PULONG hash) {
  if (hash) *hash = 0;
  return 0;
}
