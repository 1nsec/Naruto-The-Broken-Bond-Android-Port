// A sampling profiler for the performance log on Android (sampling_profiler.h).
//
// ITIMER_PROF counts the process's CPU time and raises SIGPROF every
// kIntervalUs of it on a thread that is running, so the samples fall on the
// busy threads in proportion to their CPU time. The handler only stores the
// thread and the interrupted program counter in a ring; a thread of its own
// drains the ring, maps each address to its library and, with the symbol
// files the build packs into the APK (assets/symbols/<library>.syms, nm
// output of the unstripped libraries), to its function, and writes the
// functions that take the most samples on each busy thread to
// REX_NATIVE_FRAME_DIR/profile.txt every 30 s.
#include "android/sampling_profiler.h"

#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <rex/logging.h>

namespace narutobb::profiler {
namespace {

constexpr int kIntervalUs = 2000;                 // Of process CPU time.
constexpr size_t kRingSize = size_t(1) << 16;     // Samples between two drains (1 s).
constexpr auto kReportPeriod = std::chrono::seconds(30);
constexpr size_t kTopFunctions = 40;

struct Sample {
  std::atomic<uint32_t> tid{0};
  std::atomic<uint64_t> pc{0};
};
Sample ring[kRingSize];
std::atomic<uint64_t> head{0};
std::atomic<bool> started{false};

void OnSignal(int, siginfo_t*, void* context) {
  const int saved = errno;
  const auto* uc = static_cast<const ucontext_t*>(context);
#if defined(__aarch64__)
  const uint64_t pc = uc->uc_mcontext.pc;
#elif defined(__x86_64__)
  const uint64_t pc = uint64_t(uc->uc_mcontext.gregs[REG_RIP]);
#else
  const uint64_t pc = 0;
#endif
  const uint64_t at = head.fetch_add(1, std::memory_order_relaxed) & (kRingSize - 1);
  ring[at].tid.store(uint32_t(syscall(SYS_gettid)), std::memory_order_relaxed);
  ring[at].pc.store(pc, std::memory_order_release);
  errno = saved;
}

struct Module {
  uint64_t begin = 0, end = 0, bias = 0;
  std::string name;  // The file name (libmain.so).
};
struct Symbol {
  uint64_t address = 0, size = 0;
  uint32_t name = 0;  // Into Symbols::names.
};
struct Symbols {
  std::vector<Symbol> table;  // By address.
  std::vector<std::string> names;
};

std::vector<Module> LoadedModules() {
  std::vector<Module> modules;
  dl_iterate_phdr(
      [](dl_phdr_info* info, size_t, void* data) {
        auto& out = *static_cast<std::vector<Module>*>(data);
        Module m;
        m.bias = info->dlpi_addr;
        uint64_t lo = UINT64_MAX, hi = 0;
        for (int i = 0; i < info->dlpi_phnum; ++i) {
          const auto& ph = info->dlpi_phdr[i];
          if (ph.p_type != PT_LOAD) continue;
          lo = std::min<uint64_t>(lo, ph.p_vaddr);
          hi = std::max<uint64_t>(hi, ph.p_vaddr + ph.p_memsz);
        }
        if (lo >= hi) return 0;
        m.begin = m.bias + lo;
        m.end = m.bias + hi;
        const char* path = info->dlpi_name && *info->dlpi_name ? info->dlpi_name : "(main)";
        const char* slash = std::strrchr(path, '/');
        m.name = slash ? slash + 1 : path;
        out.push_back(std::move(m));
        return 0;
      },
      &modules);
  std::sort(modules.begin(), modules.end(), [](const Module& a, const Module& b) { return a.begin < b.begin; });
  return modules;
}

// nm -C -n --defined-only --print-size: "address size type name"; the text symbols.
Symbols LoadSymbols(const std::string& path) {
  Symbols out;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    const size_t a = line.find(' ');
    if (a == std::string::npos) continue;
    const size_t b = line.find(' ', a + 1);
    if (b == std::string::npos || b + 3 > line.size()) continue;
    const char type = line[b + 1];
    if (type != 't' && type != 'T' && type != 'w' && type != 'W') continue;
    Symbol s;
    s.address = std::strtoull(line.c_str(), nullptr, 16);
    s.size = std::strtoull(line.c_str() + a + 1, nullptr, 16);
    s.name = uint32_t(out.names.size());
    out.names.push_back(line.substr(b + 3));
    out.table.push_back(s);
  }
  std::sort(out.table.begin(), out.table.end(), [](const Symbol& x, const Symbol& y) { return x.address < y.address; });
  return out;
}

std::string ThreadName(uint32_t tid) {
  std::ifstream in("/proc/self/task/" + std::to_string(tid) + "/comm");
  std::string name;
  std::getline(in, name);
  return name.empty() ? "?" : name;
}

class Profiler {
 public:
  explicit Profiler(std::string out_dir, std::string symbols_dir)
      : out_dir_(std::move(out_dir)), symbols_dir_(std::move(symbols_dir)) {}

  void Run() {
    modules_ = LoadedModules();
    auto last_report = std::chrono::steady_clock::now();
    uint64_t tail = head.load(std::memory_order_acquire);
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      const uint64_t now_head = head.load(std::memory_order_acquire);
      if (now_head - tail > kRingSize) {
        dropped_ += now_head - tail - kRingSize;
        tail = now_head - kRingSize;
      }
      for (; tail < now_head; ++tail) {
        Sample& s = ring[tail & (kRingSize - 1)];
        const uint64_t pc = s.pc.load(std::memory_order_acquire);
        const uint32_t tid = s.tid.load(std::memory_order_relaxed);
        if (pc && tid) Note(tid, pc);
      }
      if (std::chrono::steady_clock::now() - last_report >= kReportPeriod) {
        Report();
        last_report = std::chrono::steady_clock::now();
      }
    }
  }

 private:
  struct Thread {
    std::string name;
    uint64_t samples = 0;
    std::unordered_map<uint32_t, uint64_t> functions;  // By function id (function_names_).
  };

  const Module* ModuleOf(uint64_t pc) {
    auto find = [&]() -> const Module* {
      auto it = std::upper_bound(modules_.begin(), modules_.end(), pc,
                                 [](uint64_t v, const Module& m) { return v < m.begin; });
      if (it == modules_.begin()) return nullptr;
      --it;
      return pc < it->end ? &*it : nullptr;
    };
    if (const Module* m = find()) return m;
    // A library loaded since (the Vulkan driver): the list again, at most every 10 s.
    const auto now = std::chrono::steady_clock::now();
    if (now - modules_loaded_ > std::chrono::seconds(10)) {
      modules_loaded_ = now;
      modules_ = LoadedModules();
      return find();
    }
    return nullptr;
  }

  const Symbols* SymbolsOf(const std::string& library) {
    auto [it, inserted] = symbols_.try_emplace(library);
    if (inserted && !symbols_dir_.empty()) it->second = LoadSymbols(symbols_dir_ + "/" + library + ".syms");
    return it->second.table.empty() ? nullptr : &it->second;
  }

  std::string FunctionOf(uint64_t pc) {
    const Module* m = ModuleOf(pc);
    if (!m) return "(unknown)";
    if (const Symbols* syms = SymbolsOf(m->name)) {
      const uint64_t offset = pc - m->bias;
      auto it = std::upper_bound(syms->table.begin(), syms->table.end(), offset,
                                 [](uint64_t v, const Symbol& s) { return v < s.address; });
      if (it != syms->table.begin()) {
        --it;
        if (!it->size || offset < it->address + it->size) return syms->names[it->name];
      }
    }
    return "(" + m->name + ")";
  }

  void Note(uint32_t tid, uint64_t pc) {
    auto [it, inserted] = threads_.try_emplace(tid);
    Thread& t = it->second;
    if (inserted) t.name = ThreadName(tid);
    ++t.samples;
    ++total_;
    // The symbol lookup once per 64-byte line of code.
    auto [line, fresh] = lines_.try_emplace(pc >> 6, 0u);
    if (fresh) {
      std::string name = FunctionOf(pc);
      auto [id, added] = function_ids_.try_emplace(name, uint32_t(function_names_.size()));
      if (added) function_names_.push_back(std::move(name));
      line->second = id->second;
    }
    ++t.functions[line->second];
  }

  void Report() {
    std::vector<std::pair<uint32_t, const Thread*>> busy;
    for (const auto& [tid, t] : threads_) busy.push_back({tid, &t});
    std::sort(busy.begin(), busy.end(), [](const auto& a, const auto& b) { return a.second->samples > b.second->samples; });
    std::string text = "# Sampling profile: every " + std::to_string(kIntervalUs) +
                       " us of process CPU time, the thread running and its function; cumulative.\n";
    text += "# samples " + std::to_string(total_) + ", dropped " + std::to_string(dropped_) + "\n";
    for (size_t i = 0; i < busy.size() && i < 12; ++i) {
      const auto& [tid, t] = busy[i];
      if (t->samples * 50 < total_) break;  // Under 2% of the samples.
      char header[160];
      std::snprintf(header, sizeof(header), "\n== %s (tid %u): %llu samples, %.1f%%\n", t->name.c_str(), tid,
                    static_cast<unsigned long long>(t->samples), 100.0 * double(t->samples) / double(total_));
      text += header;
      std::vector<std::pair<const std::string*, uint64_t>> top;
      for (const auto& [id, count] : t->functions) top.push_back({&function_names_[id], count});
      std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
      for (size_t f = 0; f < top.size() && f < kTopFunctions; ++f) {
        char row[64];
        std::snprintf(row, sizeof(row), "%6.2f%%  ", 100.0 * double(top[f].second) / double(t->samples));
        text += row + top[f].first->substr(0, 200) + "\n";
      }
    }
    const std::string path = out_dir_ + "/profile.txt";
    if (std::FILE* f = std::fopen(path.c_str(), "w")) {
      std::fwrite(text.data(), 1, text.size(), f);
      std::fclose(f);
    }
  }

  std::string out_dir_, symbols_dir_;
  std::vector<Module> modules_;
  std::chrono::steady_clock::time_point modules_loaded_{};
  std::map<std::string, Symbols> symbols_;
  std::unordered_map<uint64_t, uint32_t> lines_;  // Code line (pc >> 6): function id.
  std::unordered_map<std::string, uint32_t> function_ids_;
  std::vector<std::string> function_names_;
  std::unordered_map<uint32_t, Thread> threads_;
  uint64_t total_ = 0, dropped_ = 0;
};

}  // namespace

void Start() {
  const char* perf = std::getenv("REX_NATIVE_PERF_LOG");
  const char* dir = std::getenv("REX_NATIVE_FRAME_DIR");
  if (!perf || !*perf || !dir || !*dir || started.exchange(true)) return;
  const char* symbols = std::getenv("BROKENBOND_SYMBOLS_DIR");
  struct sigaction action {};
  action.sa_sigaction = OnSignal;
  action.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGPROF, &action, nullptr) != 0) {
    REXLOG_WARN("[narutobb] Sampling profiler: no SIGPROF handler");
    return;
  }
  std::thread([out = std::string(dir), syms = std::string(symbols ? symbols : "")] {
    pthread_setname_np(pthread_self(), "nbb-profiler");
    Profiler(out, syms).Run();
  }).detach();
  itimerval timer{};
  timer.it_interval.tv_usec = kIntervalUs;
  timer.it_value.tv_usec = kIntervalUs;
  if (setitimer(ITIMER_PROF, &timer, nullptr) != 0) {
    REXLOG_WARN("[narutobb] Sampling profiler: setitimer failed");
    return;
  }
  REXLOG_INFO("[narutobb] Sampling profiler on: {}/profile.txt every 30 s{}", dir,
              symbols ? "" : " (no symbol files)");
}

}  // namespace narutobb::profiler
