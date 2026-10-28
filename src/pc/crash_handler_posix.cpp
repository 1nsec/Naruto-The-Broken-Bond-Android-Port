// Writes a crash report to logs/ (in the app's data folder) when the game
// crashes, on platforms without Windows (Android): the signal, the faulting
// address and a backtrace with module offsets. Installed before main runs; the
// signal then goes on to the handler that was there before (the system's
// debuggerd on Android, which also writes a tombstone to logcat).
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <unwind.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>

#include <rex/filesystem.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace {

constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
struct sigaction previous[NSIG];
std::atomic<bool> entered{false};
char report_folder[1024];

struct Trace {
  void* frames[64];
  int count = 0;
};

_Unwind_Reason_Code Collect(_Unwind_Context* context, void* arg) {
  auto* trace = static_cast<Trace*>(arg);
  const uintptr_t pc = _Unwind_GetIP(context);
  if (pc && trace->count < 64) trace->frames[trace->count++] = reinterpret_cast<void*>(pc);
  return trace->count < 64 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

void Line(int fd, const char* text) {
  if (fd >= 0) {
    ssize_t ignored = write(fd, text, std::strlen(text));
    (void)ignored;
  }
#if defined(__ANDROID__)
  __android_log_write(ANDROID_LOG_FATAL, "narutobb", text);
#endif
}

void OnSignal(int sig, siginfo_t* info, void* context) {
  if (!entered.exchange(true)) {
    char path[1200];
    std::snprintf(path, sizeof(path), "%s/crash_%ld.txt", report_folder, static_cast<long>(std::time(nullptr)));
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    char line[512];
    Line(fd, "Naruto: The Broken Bond crashed.\n");
    std::snprintf(line, sizeof(line), "Signal: %d (%s), code %d, address %p\n", sig, strsignal(sig),
                  info ? info->si_code : 0, info ? info->si_addr : nullptr);
    Line(fd, line);
    std::snprintf(line, sizeof(line), "Thread: %ld\n", static_cast<long>(gettid()));
    Line(fd, line);
    Trace trace;
    _Unwind_Backtrace(Collect, &trace);
    for (int i = 0; i < trace.count; ++i) {
      Dl_info dl{};
      if (dladdr(trace.frames[i], &dl) && dl.dli_fname) {
        const char* name = std::strrchr(dl.dli_fname, '/');
        std::snprintf(line, sizeof(line), "  #%02d %s+0x%zx %s\n", i, name ? name + 1 : dl.dli_fname,
                      size_t(uintptr_t(trace.frames[i]) - uintptr_t(dl.dli_fbase)),
                      dl.dli_sname ? dl.dli_sname : "");
      } else {
        std::snprintf(line, sizeof(line), "  #%02d %p\n", i, trace.frames[i]);
      }
      Line(fd, line);
    }
    if (fd >= 0) close(fd);
  }
  // On to the handler that was installed before (or the default action).
  const struct sigaction& old = previous[sig];
  if ((old.sa_flags & SA_SIGINFO) && old.sa_sigaction) {
    old.sa_sigaction(sig, info, context);
    return;
  }
  if (old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN && old.sa_handler) {
    old.sa_handler(sig);
    return;
  }
  signal(sig, SIG_DFL);
  raise(sig);
}

struct CrashHandlerInstaller {
  CrashHandlerInstaller() {
    std::error_code ec;
    const auto folder = rex::filesystem::GetExecutableFolder() / "logs";
    std::filesystem::create_directories(folder, ec);
    std::snprintf(report_folder, sizeof(report_folder), "%s", folder.string().c_str());
    struct sigaction action {};
    action.sa_sigaction = OnSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int sig : kSignals) sigaction(sig, &action, &previous[sig]);
  }
} g_crash_handler_installer;

}  // namespace
