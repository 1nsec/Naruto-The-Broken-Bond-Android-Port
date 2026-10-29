// Precompiled pipelines. The list, shaders/pipeline_cache.inc (optional), is
// generated from the pipelines.json of earlier boots and git-ignored like the
// shader cache: the shader names, the layout and the
// state words of each key. At the first draw (shader cache loaded) the engine
// prepares its root signature and a pool of the dispatcher compiles the list with
// the DXIL of the cache and the spec constant variants the draw takes. A draw takes
// a ready pipeline, or waits for one being created; one missing from the list is
// created by the draw. REX_NATIVE_PSO_PRECOMPILE=0 turns the pool off.
//
// The list also knows the world each pipeline was first created in
// (common/title_loading.h): when the title starts loading a world, its entries
// not compiled yet are posted to the asynchronous threads ahead of the pool,
// which compile them at the highest priority while the loading screen is up
// (PrefetchWorldPipelines). REX_NATIVE_PSO_WORLD=0 turns that off.
//
// A build without a list (the Android APK is built without the boots of a PC)
// takes the list the Vulkan backend learns on the device instead
// (BROKENBOND_PIPELINE_LIST, vulkan/pipeline_cache.inc): the pipelines drawn
// in the earlier sessions, in the order of their first draw, with the world of
// each (LoadLearnedPipelines).
#include "gpu/shaders/pipelines.h"
#include "gpu/common/title_loading.h"
#include "gpu/d3d/draw.h"
#include "gpu/shaders/shaders.h"
#include "gpu/d3d/frame.h"
#include "gpu/d3d/d3d.h"
#include "gpu/backend/dispatcher.h"
#include "gpu/backend/pipeline_keys.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <dxgiformat.h>
#endif
#include <array>
#include <charconv>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "platform/win32.h"
#ifdef _WIN32
#include <wrl/client.h>
#endif
#ifdef _WIN32
#include <dxcapi.h>
#endif
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>
#include <rex/system/xmemory.h>

using namespace gpu;

namespace gpu {
struct PipelineCacheElement {
  const char* semantic;  // Static string (XenosRecomp semantics), as VertexElement.
  uint32_t semantic_index, format, slot, offset;
  uint32_t instance_step = 0;  // As VertexElement; written only when not 0.
};
struct PipelineCacheEntry {
  const char* vs;
  const char* ps;  // With "+specN", or "depth-only".
  uint64_t key, layout;
  uint32_t first_element, element_count;
  std::array<uint32_t, 21> state;
};
// The entries first created while a world was the one loaded last, as a
// range of kPipelineCacheWorldEntries (entry indices); a sentinel of key 0
// ends the table.
struct PipelineCacheWorld {
  uint32_t key, first, count;
};
#if __has_include("gpu/shaders/pipeline_cache.inc")
#include "gpu/shaders/pipeline_cache.inc"
constexpr std::span<const PipelineCacheEntry> kCompiledPipelines{kPipelineCacheEntries};
constexpr std::span<const PipelineCacheElement> kCompiledElements{kPipelineCacheElements};
constexpr std::span<const PipelineCacheWorld> kCompiledWorlds{kPipelineCacheWorlds};
constexpr std::span<const uint16_t> kCompiledWorldEntries{kPipelineCacheWorldEntries};
#else
constexpr std::span<const PipelineCacheEntry> kCompiledPipelines{};
constexpr std::span<const PipelineCacheElement> kCompiledElements{};
constexpr std::span<const PipelineCacheWorld> kCompiledWorlds{};
constexpr std::span<const uint16_t> kCompiledWorldEntries{};
constexpr const char* kPipelineCacheIdentity = "";
#endif

// The list the pool compiles: the build's, or the one learned on the device.
// Set before the pool starts (StartPipelinePrecompile), read-only afterwards.
struct PipelineList {
  std::span<const PipelineCacheEntry> entries{kCompiledPipelines};
  std::span<const PipelineCacheElement> elements{kCompiledElements};
  std::span<const PipelineCacheWorld> worlds{kCompiledWorlds};
  std::span<const uint16_t> world_entries{kCompiledWorldEntries};
  std::string identity{kPipelineCacheIdentity};
} pipeline_list;

// The storage of a learned list: the strings its entries point to stay for the
// life of the process.
struct LearnedPipelines {
  std::deque<std::string> strings;
  std::vector<PipelineCacheEntry> entries;
  std::vector<PipelineCacheElement> elements;
  std::vector<PipelineCacheWorld> worlds;
  std::vector<uint16_t> world_entries;
} learned_pipelines;

// The list of BROKENBOND_PIPELINE_LIST into pipeline_list, when the build has
// none. A line: key, VS name, PS name, layout id, world (hex), the 21 state
// words, the element count and per element its semantic, index, format, slot,
// offset and instance step. A line that does not parse, or whose layout id is
// not its elements', is left out; DescribeEntry checks the key.
size_t LoadLearnedPipelines() {
  if (!kCompiledPipelines.empty()) return 0;
  const char* path = std::getenv("BROKENBOND_PIPELINE_LIST");
  if (!path || !*path) return 0;
  std::FILE* in = std::fopen(path, "rb");
  if (!in) return 0;
  std::string text;
  char chunk[1 << 16];
  for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), in)) > 0;) text.append(chunk, n);
  std::fclose(in);
  auto& l = learned_pipelines;
  std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> layouts;  // Layout id: first element, count.
  std::map<uint32_t, std::vector<uint16_t>> worlds;
  std::unordered_set<uint64_t> keys;
  const auto intern = [&](std::string_view s) { return l.strings.emplace_back(s).c_str(); };
  for (size_t at = 0; at < text.size() && l.entries.size() < 65535;) {
    const size_t end = text.find('\n', at);
    if (end == std::string::npos) break;  // A cut last line.
    const std::string_view line(text.data() + at, end - at);
    at = end + 1;
    std::vector<std::string_view> f;
    for (size_t i = 0; i < line.size();) {
      const size_t space = line.find(' ', i);
      f.push_back(line.substr(i, space == std::string_view::npos ? std::string_view::npos : space - i));
      if (space == std::string_view::npos) break;
      i = space + 1;
    }
    const auto number = [](std::string_view s, auto& value, int base = 10) {
      const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value, base);
      return !s.empty() && error == std::errc() && end == s.data() + s.size();
    };
    uint64_t key = 0, layout = 0;
    uint32_t world = 0, count = 0;
    if (f.size() < 27 || !number(f[0], key, 16) || !number(f[3], layout, 16) || !number(f[4], world, 16) ||
        !number(f[26], count) || !count || f.size() != 27 + size_t(count) * 6 || !keys.insert(key).second)
      continue;
    PipelineCacheEntry entry{};
    bool ok = true;
    for (size_t w = 0; w < 21 && ok; ++w) ok = number(f[5 + w], entry.state[w]);
    std::vector<VertexElement> elements;
    std::vector<PipelineCacheElement> cached;
    for (uint32_t e = 0; e < count && ok; ++e) {
      const auto* g = f.data() + 27 + size_t(e) * 6;
      PipelineCacheElement c{};
      ok = !g[0].empty() && number(g[1], c.semantic_index) && number(g[2], c.format) && number(g[3], c.slot) &&
           number(g[4], c.offset) && number(g[5], c.instance_step);
      if (!ok) break;
      c.semantic = intern(g[0]);
      cached.push_back(c);
      elements.push_back({c.semantic, c.semantic_index, VertexFormat(c.format), c.slot, c.offset, c.instance_step});
    }
    if (!ok || ElementsId(elements) != layout) continue;
    auto [it, inserted] = layouts.try_emplace(layout, uint32_t(l.elements.size()), count);
    if (inserted) l.elements.insert(l.elements.end(), cached.begin(), cached.end());
    entry.vs = intern(f[1]);
    entry.ps = intern(f[2]);
    entry.key = key;
    entry.layout = layout;
    entry.first_element = it->second.first;
    entry.element_count = it->second.second;
    if (world) worlds[world].push_back(uint16_t(l.entries.size()));
    l.entries.push_back(entry);
  }
  for (const auto& [world, indices] : worlds) {
    l.worlds.push_back({world, uint32_t(l.world_entries.size()), uint32_t(indices.size())});
    l.world_entries.insert(l.world_entries.end(), indices.begin(), indices.end());
  }
  l.worlds.push_back({0, uint32_t(l.world_entries.size()), 0});  // The sentinel.
  pipeline_list.entries = l.entries;
  pipeline_list.elements = l.elements;
  pipeline_list.worlds = l.worlds;
  pipeline_list.world_entries = l.world_entries;
  pipeline_list.identity = fmt::format("learned:{}", l.entries.size());
  return l.entries.size();
}

struct PipelinePrecompile {
  std::once_flag once;
  std::chrono::steady_clock::time_point started;
  unsigned threads = 0;
  std::vector<VertexElements> layouts;  // Per entry, shared by layout id.
  std::atomic<bool> pool_started{false};  // The layouts are built and the pool runs or ran.
  std::atomic<uint64_t> created{0}, present{0}, failed{0}, unresolved{0}, mismatched{0};
  // The world prefetches (PrefetchWorldPipelines) since the start.
  std::atomic<uint64_t> world_posted{0}, world_present{0};
  // Entries taken as the alpha to coverage variant (transparency antialiasing
  // on), and alpha to coverage entries taken back as the alpha test (off).
  std::atomic<uint64_t> alpha_to_coverage{0}, alpha_test{0};
  std::mutex mutex;
  std::string first_failure;  // JSON value.
} pipeline_precompile;

void PrecompileNote(std::atomic<uint64_t>& counter, std::string failure) {
  ++counter;
  std::lock_guard lock(pipeline_precompile.mutex);
  if (pipeline_precompile.first_failure.empty()) pipeline_precompile.first_failure = std::move(failure);
}

// The description of one entry of the list, on any thread: its shaders by
// name, the key checked against the engine's. False for an entry the cache
// cannot resolve or whose key does not match; note: the pool's counters and
// first failure take it (the pool only: the world prefetch counts its own).
// linked: the spec constants of the pixel shader taken; variant: the draw asks
// for another variant than the list holds (its key replaced the entry's).
bool DescribeEntry(size_t i, PipelineDesc& desc, uint32_t& linked, bool& variant, bool note) {
  const PipelineCacheEntry& entry = pipeline_list.entries[i];
  auto unresolved = [&](const char* why) {
    if (note)
      PrecompileNote(pipeline_precompile.unresolved, fmt::format("{{\"reason\":\"{}\",\"vs\":\"{}\",\"ps\":\"{}\"}}",
                                                                 why, entry.vs, entry.ps));
    return false;
  };
  auto vs = shader_cache.by_name.find(entry.vs);
  if (vs == shader_cache.by_name.end()) return unresolved("vs-not-in-cache");
  desc.vs.key = entry.vs;
  desc.vs.format = desc.ps.format = shader_cache.format;
  desc.vs.spec_mask = vs->second->spec_mask;
  desc.ps.key = entry.ps;
  desc.vs.bytes = LoadModule(*vs->second);
  if (!desc.vs.bytes) return unresolved("vs-module-unreadable");
  std::string variant_key;  // The pixel shader key the draw asks for now, when the list holds the other.
  linked = 0;
  variant = false;
  if (desc.ps.key != "depth-only") {
    std::string name = desc.ps.key;
    uint32_t spec = 0;
    if (const size_t plus = name.find("+spec"); plus != std::string::npos) {
      spec = uint32_t(std::stoul(name.substr(plus + 5)));
      name.resize(plus);
    }
    auto ps = shader_cache.by_name.find(name);
    if (ps == shader_cache.by_name.end()) return unresolved("ps-not-in-cache");
    // The list holds the variants of the boots it came from, with the
    // transparency antialiasing those boots had. The draw asks for the alpha
    // test (only TransparencySpec turns it into alpha to coverage), so an
    // entry goes back to the alpha test and through TransparencySpec as the
    // draw does: alpha to coverage in a multisampled target when the option
    // is on, the alpha test otherwise.
    const uint32_t requested = spec == kSpecAlphaToCoverage ? kSpecAlphaTest : spec;
    linked = TransparencySpec(requested, ps->second->spec_mask, entry.state[8], entry.state[0]);
    if (linked != spec) variant_key = fmt::format("{}+spec{}", name, linked);
    std::string error;
    desc.ps.bytes = LoadShader(*ps->second, linked, error);
    if (!desc.ps.bytes) return unresolved("ps-module-unavailable");
    desc.ps.spec_mask = ps->second->spec_mask;
    desc.ps.spec_value = linked;
  }
  desc.elements = pipeline_precompile.layouts[i];
  desc.layout_id = entry.layout;
  desc.state = entry.state;
  if (PipelineKey(desc.vs.key, desc.ps.key, desc.layout_id, desc.state) != entry.key) {
    if (note)
      PrecompileNote(pipeline_precompile.mismatched, fmt::format("{{\"reason\":\"key\",\"key\":\"{:016x}\"}}",
                                                                 entry.key));
    return false;
  }
  // As the draw: the variant's key; alpha to coverage turns state word 11 on,
  // and the alpha test keeps the word of the entry (the title's alpha to mask).
  if (!variant_key.empty()) {
    variant = true;
    desc.ps.key = variant_key;
    if (linked == kSpecAlphaToCoverage) desc.state[11] = 1;
  }
  return true;
}

// One entry of the list on a pool thread: its description, then the engine's
// PrecompilePipeline.
void PrecompileEntry(size_t i, Engine& engine) {
  PipelineDesc desc;
  uint32_t linked = 0;
  bool variant = false;
  if (!DescribeEntry(i, desc, linked, variant, true)) return;
  bool present = false;
  std::string evidence;
  if (!engine.PrecompilePipeline(desc, present, evidence))
    return PrecompileNote(pipeline_precompile.failed, evidence);
  ++(present ? pipeline_precompile.present : pipeline_precompile.created);
  if (variant) ++(linked == kSpecAlphaToCoverage ? pipeline_precompile.alpha_to_coverage
                                                 : pipeline_precompile.alpha_test);
}

// REX_NATIVE_PSO_WORLD=0 leaves the pipelines of a world to the pool and the draws.
bool worldPrefetchOption() {
  static const bool value = [] {
    const char* text = std::getenv("REX_NATIVE_PSO_WORLD");
    return !(text && std::string_view(text) == "0");
  }();
  return value;
}

void StartPipelinePrecompile() {
  auto& p = pipeline_precompile;
  const char* off = std::getenv("REX_NATIVE_PSO_PRECOMPILE");
  if (!(off && std::string_view(off) == "0")) {
    if (const size_t learned = LoadLearnedPipelines())
      REXLOG_INFO("[broken_bond] Precompiling the {} pipelines drawn on this device before", learned);
  }
  const char* skip = off && std::string_view(off) == "0" ? "disabled" : pipeline_list.entries.empty() ? "list-absent"
                     : !shader_cache.loaded ? "shader-cache-unavailable" : nullptr;
  auto* dispatcher = host::ActiveDispatcher();
  std::string prepared;
  if (!skip && (!dispatcher || !dispatcher->Run([](Engine& engine, std::string& out) {
        return engine.PreparePipelines(out);
      }, prepared))) skip = "prepare-failed";
  if (skip) {
    host::Record("native-d3d-pipeline-precompile-start", fmt::format(
        "\"started\":false,\"reason\":\"{}\",\"list\":{},\"identity\":\"{}\",\"prepare\":{}", skip,
        pipeline_list.entries.size(), pipeline_list.identity, prepared.empty() ? "null" : prepared));
    return;
  }
  std::unordered_map<uint64_t, VertexElements> shared;
  for (const auto& entry : pipeline_list.entries) {
    auto& layout = shared[entry.layout];
    if (!layout) {
      std::vector<VertexElement> elements;
      for (uint32_t e = entry.first_element; e < entry.first_element + entry.element_count; ++e) {
        const auto& c = pipeline_list.elements[e];
        elements.push_back({c.semantic, c.semantic_index, VertexFormat(c.format), c.slot, c.offset, c.instance_step});
      }
      layout = std::make_shared<const std::vector<VertexElement>>(std::move(elements));
    }
    p.layouts.push_back(layout);
  }
  // Two thirds of the hardware threads, at least two; one under Wine / Proton,
  // so the pipeline guard of the backend (RADV crashes) names one culprit.
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const bool wine = ntdll && GetProcAddress(ntdll, "wine_get_version");
  p.threads = wine ? 1u : std::max(2u, std::thread::hardware_concurrency() * 2 / 3);
  p.started = std::chrono::steady_clock::now();
  p.pool_started.store(true, std::memory_order_release);
  const bool started = dispatcher->StartPool(pipeline_list.entries.size(), p.threads, PrecompileEntry, [](bool stopped) {
    auto& p = pipeline_precompile;
    std::string failure;
    {
      std::lock_guard lock(p.mutex);
      failure = p.first_failure.empty() ? "null" : p.first_failure;
    }
    host::Record("native-d3d-pipeline-precompile", fmt::format(
        "\"list\":{},\"threads\":{},\"created\":{},\"present\":{},\"failed\":{},\"unresolved\":{},\"mismatched\":{},"
        "\"alpha_to_coverage\":{},\"alpha_test\":{},\"world_posted\":{},\"world_present\":{},\"stopped\":{},\"ms\":{},"
        "\"swap\":{},\"first_failure\":{}",
        pipeline_list.entries.size(),
        p.threads, p.created.load(), p.present.load(), p.failed.load(), p.unresolved.load(), p.mismatched.load(),
        p.alpha_to_coverage.load(), p.alpha_test.load(), p.world_posted.load(), p.world_present.load(), stopped,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - p.started).count(),
        swaps.load(), failure));
  });
  host::Record("native-d3d-pipeline-precompile-start", fmt::format(
      "\"started\":{},\"list\":{},\"layouts\":{},\"worlds\":{},\"threads\":{},\"identity\":\"{}\",\"swap\":{}",
      started, pipeline_list.entries.size(), shared.size(), pipeline_list.worlds.empty() ? 0 : pipeline_list.worlds.size() - 1,
      p.threads, pipeline_list.identity, swaps.load()));
}

void StartPipelinePrecompileOnce() { std::call_once(pipeline_precompile.once, StartPipelinePrecompile); }

// The entries of a world (the table ends with a sentinel of key 0), described
// on the calling thread and posted to the asynchronous threads of the pipeline
// set, which skip the ones the set has (compiled by the pool, or taken by a
// draw already). Nothing before the pool started: its layouts are the
// entries', and the first world of a boot loads before the first draw.
WorldPrefetch PrefetchWorldPipelines(uint32_t world_key) {
  WorldPrefetch out;
  auto& p = pipeline_precompile;
  if (!worldPrefetchOption() || !p.pool_started.load(std::memory_order_acquire)) return out;
  const PipelineCacheWorld* world = nullptr;
  if (world_key)  // The sentinel of key 0 is not a world.
    for (const auto& w : pipeline_list.worlds)
      if (w.key == world_key) { world = &w; break; }
  if (!world) return out;
  out.listed = world->count;
  std::vector<PipelineDesc> descs;
  descs.reserve(world->count);
  for (uint32_t e = world->first; e < world->first + world->count; ++e) {
    PipelineDesc desc;
    uint32_t linked = 0;
    bool variant = false;
    if (DescribeEntry(pipeline_list.world_entries[e], desc, linked, variant, false)) descs.push_back(std::move(desc));
    else ++out.unresolved;
  }
  auto* dispatcher = host::ActiveDispatcher();
  if (!dispatcher || !dispatcher->WithEngine([&](Engine& engine) {
        for (const auto& desc : descs) ++(engine.PostPipeline(desc) ? out.posted : out.present);
      })) return out;
  p.world_posted.fetch_add(out.posted, std::memory_order_relaxed);
  p.world_present.fetch_add(out.present, std::memory_order_relaxed);
  return out;
}
}  // namespace gpu

namespace gpu {
// The pipelines created with their stable identities, also as
// pipelines.json in REX_NATIVE_FRAME_DIR (the input of the precompiled list),
// on the stop and on the close.
void RecordPipelines() {
  auto* dispatcher = host::ActiveDispatcher();
  std::string pipelines, takes;
  if (!dispatcher || !dispatcher->Run([&takes](Engine& engine, std::string& o) {
        o = engine.PipelineList();
        takes = engine.PipelineSummary();
        return true;
      }, pipelines) || pipelines.empty()) return;
  // How the draws took their pipelines (pipeline_set.h): the record and, in
  // the product (no journal), the SDK log.
  host::Record("native-d3d-pipeline-takes", takes);
  if (!host::Journaled()) REXLOG_INFO("[broken_bond] Native renderer pipelines: {{{}}}", takes);
  size_t count = 0;
  for (size_t at = pipelines.find("{\"key\""); at != std::string::npos; at = pipelines.find("{\"key\"", at + 1)) ++count;
  std::string file = "null";
  if (const char* dir = std::getenv("REX_NATIVE_FRAME_DIR"); dir && *dir) {
    std::string path = std::string(dir) + "/pipelines.json";
    std::replace(path.begin(), path.end(), '\\', '/');  // A JSON-safe path in the record.
    if (std::FILE* out = std::fopen(path.c_str(), "wb")) {
      std::fwrite(pipelines.data(), 1, pipelines.size(), out);
      std::fclose(out);
      file = "\"" + path + "\"";
    }
  }
  host::Record("native-d3d-pipelines", fmt::format("\"count\":{},\"file\":{},\"pipelines\":{}", count, file, pipelines));
}
}  // namespace gpu
