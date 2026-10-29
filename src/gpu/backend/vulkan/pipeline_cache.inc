// Included after Backend::State, inside gpu::vulkan.
// The Vulkan pipeline cache, kept on disk between sessions when the app names
// a file (BROKENBOND_PIPELINE_CACHE; the Android app sets it): every graphics
// pipeline is created through it, so a pipeline compiled in an earlier
// session is made from the driver's cached binaries in a fraction of the time
// (on a phone the compilation of the first session is most of its stutter).
// The file holds a small header of its own (magic, version, size, FNV-1a 64
// of the data) and the driver's data, which is used only when its Vulkan
// header names this device and driver (pipelineCacheUUID): a file of another
// driver (Settings > GPU driver) or a cut file starts an empty cache. It is
// saved from a thread of its own, at most every kPipelineCacheSaveInterval
// while new pipelines are created, and at the shutdown.
//
// With it, when the app names a second file (BROKENBOND_PIPELINE_LIST), the
// list of the pipelines drawn on this device, in the order of their first draw
// over the sessions: one line per pipeline with the stable identities of its
// key (as the pipelines.json of record_pipelines), which the dispatcher's pool
// precompiles at the next boot when the build has no list of its own
// (shaders/pipelines.cpp, LoadLearnedPipelines). Lines are only added.
namespace {
constexpr char kPipelineCacheMagic[4] = {'N', 'B', 'P', 'C'};
constexpr uint32_t kPipelineCacheFormat = 1;
constexpr size_t kPipelineCacheHeader = 24;  // Magic, format, data bytes, hash.
constexpr auto kPipelineCacheSaveInterval = std::chrono::seconds(20);
constexpr size_t kPipelineListMax = 65535;  // The pool's world index is 16 bits.

uint64_t Fnv1a64(const uint8_t* data, size_t size) {
  uint64_t hash = 0xCBF29CE484222325ull;
  for (size_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 0x100000001B3ull; }
  return hash;
}

void PipelineCacheLog(bool error, const std::string& text) {
#ifdef __ANDROID__
  __android_log_print(error ? ANDROID_LOG_WARN : ANDROID_LOG_INFO, "narutobb", "Vulkan pipeline cache: %s",
                      text.c_str());
#else
  (void)error; (void)text;
#endif
}

// The driver's data of the file at path, empty with the reason when there is
// none this device can use.
std::vector<uint8_t> ReadPipelineCache(const std::string& path, const VkPhysicalDeviceProperties& device,
                                       std::string& status) {
  std::vector<uint8_t> file;
  if (std::FILE* in = std::fopen(path.c_str(), "rb")) {
    uint8_t chunk[1 << 16];
    for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), in)) > 0;) file.insert(file.end(), chunk, chunk + n);
    std::fclose(in);
  } else {
    status = "empty (no file yet)";
    return {};
  }
  uint32_t format = 0;
  uint64_t bytes = 0, hash = 0;
  if (file.size() >= kPipelineCacheHeader) {
    std::memcpy(&format, file.data() + 4, 4);
    std::memcpy(&bytes, file.data() + 8, 8);
    std::memcpy(&hash, file.data() + 16, 8);
  }
  if (file.size() < kPipelineCacheHeader || std::memcmp(file.data(), kPipelineCacheMagic, 4) ||
      format != kPipelineCacheFormat || bytes != file.size() - kPipelineCacheHeader ||
      hash != Fnv1a64(file.data() + kPipelineCacheHeader, size_t(bytes))) {
    status = "empty (the file was incomplete)";
    return {};
  }
  std::vector<uint8_t> data(file.begin() + kPipelineCacheHeader, file.end());
  // VkPipelineCacheHeaderVersionOne: header size, version, vendor, device, UUID.
  uint32_t header_size = 0, version = 0, vendor = 0, id = 0;
  if (data.size() >= 16 + VK_UUID_SIZE) {
    std::memcpy(&header_size, data.data(), 4);
    std::memcpy(&version, data.data() + 4, 4);
    std::memcpy(&vendor, data.data() + 8, 4);
    std::memcpy(&id, data.data() + 12, 4);
  }
  if (data.size() < 16 + VK_UUID_SIZE || header_size < 16 + VK_UUID_SIZE || header_size > data.size() ||
      version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE || vendor != device.vendorID || id != device.deviceID ||
      std::memcmp(data.data() + 16, device.pipelineCacheUUID, VK_UUID_SIZE)) {
    status = "empty (made by another driver)";
    return {};
  }
  return data;
}

// bytes into path, through a temporary file renamed over it.
bool ReplaceFile(const std::string& path, const uint8_t* bytes, size_t size) {
  const std::string temporary = path + ".tmp";
  std::FILE* out = std::fopen(temporary.c_str(), "wb");
  if (!out) return false;
  const bool complete = std::fwrite(bytes, 1, size, out) == size;
  if (std::fclose(out) != 0 || !complete) return false;
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  return !error;
}

// The cache's data into path.
bool WritePipelineCache(VkDevice device, VkPipelineCache cache, const std::string& path, size_t& written) {
  // The asynchronous threads may add pipelines between the size and the data
  // (VK_INCOMPLETE): room for some more, and another try.
  std::vector<uint8_t> file;
  size_t size = 0;
  VkResult result = VK_INCOMPLETE;
  for (int attempt = 0; attempt < 4 && result == VK_INCOMPLETE; ++attempt) {
    if (vkGetPipelineCacheData(device, cache, &size, nullptr) != VK_SUCCESS || !size) return false;
    size += size / 8 + (size_t(1) << 20);
    file.resize(kPipelineCacheHeader + size);
    result = vkGetPipelineCacheData(device, cache, &size, file.data() + kPipelineCacheHeader);
  }
  if (result != VK_SUCCESS) return false;
  file.resize(kPipelineCacheHeader + size);
  const uint64_t bytes = size, hash = Fnv1a64(file.data() + kPipelineCacheHeader, size);
  std::memcpy(file.data(), kPipelineCacheMagic, 4);
  std::memcpy(file.data() + 4, &kPipelineCacheFormat, 4);
  std::memcpy(file.data() + 8, &bytes, 8);
  std::memcpy(file.data() + 16, &hash, 8);
  if (!ReplaceFile(path, file.data(), file.size())) return false;
  written = file.size();
  return true;
}

// The logical key (the first field) of a line of the pipeline list; 0 if none.
uint64_t PipelineListKey(const std::string& line) {
  uint64_t key = 0;
  const auto end = line.find(' ');
  if (end != 16 || std::from_chars(line.data(), line.data() + 16, key, 16).ec != std::errc()) return 0;
  return key;
}
}  // namespace

void Backend::State::EnsurePipelineCache() {
  if (pipeline_cache || pipeline_cache_tried || !vulkan) return;
  pipeline_cache_tried = true;
  const char* path = std::getenv("BROKENBOND_PIPELINE_CACHE");
  std::vector<uint8_t> data;
  if (path && *path) {
    pipeline_cache_path = path;
    data = ReadPipelineCache(pipeline_cache_path, vulkan->physicalDeviceProperties, pipeline_cache_status);
  } else {
    pipeline_cache_status = "in memory (no file)";
  }
  VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
  info.initialDataSize = data.size(); info.pInitialData = data.empty() ? nullptr : data.data();
  VkResult result = vkCreatePipelineCache(vulkan->vk, &info, nullptr, &pipeline_cache);
  if (result != VK_SUCCESS && !data.empty()) {
    // Data the driver refused: an empty cache.
    pipeline_cache_status = fmt::format("empty (the driver refused the file: {})", int32_t(result));
    data.clear(); info.initialDataSize = 0; info.pInitialData = nullptr;
    result = vkCreatePipelineCache(vulkan->vk, &info, nullptr, &pipeline_cache);
  }
  if (result != VK_SUCCESS) {
    pipeline_cache = VK_NULL_HANDLE;
    pipeline_cache_status = fmt::format("none ({})", int32_t(result));
    PipelineCacheLog(true, pipeline_cache_status);
    return;
  }
  pipeline_cache_loaded = data.size();
  if (!data.empty()) pipeline_cache_status = "loaded";
  pipeline_cache_saved_at = std::chrono::steady_clock::now();
  PipelineCacheLog(false, fmt::format("{}, {} KB{}", pipeline_cache_status, data.size() / 1024,
                                      pipeline_cache_path.empty() ? "" : " from " + pipeline_cache_path));
  if (const char* list = std::getenv("BROKENBOND_PIPELINE_LIST"); list && *list) {
    pipeline_list_path = list;
    if (std::FILE* in = std::fopen(list, "rb")) {
      std::string text;
      char chunk[1 << 16];
      for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), in)) > 0;) text.append(chunk, n);
      std::fclose(in);
      for (size_t at = 0; at < text.size() && pipeline_list_lines.size() < kPipelineListMax;) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) break;  // A cut last line.
        std::string line = text.substr(at, end - at);
        at = end + 1;
        const uint64_t key = PipelineListKey(line);
        if (key && pipeline_list_keys.insert(key).second) pipeline_list_lines.push_back(std::move(line));
      }
    }
    pipeline_list_loaded = pipeline_list_lines.size();
  }
}

bool Backend::State::LearnPipelines() {
  if (pipeline_list_path.empty()) {
    pipeline_order_listed = pipeline_order.size();
    return false;
  }
  bool learned = false;
  for (; pipeline_order_listed < pipeline_order.size() && pipeline_list_lines.size() < kPipelineListMax;
       ++pipeline_order_listed) {
    const uint64_t backend_key = pipeline_order[pipeline_order_listed];
    const auto it = pipeline_descs.find(backend_key);
    if (it == pipeline_descs.end() || !it->second.elements) continue;
    const PipelineDesc& d = it->second;
    const auto plain = [](const std::string& name) {
      return !name.empty() && name.find_first_of(": \t\r\n") == std::string::npos;
    };
    if (!plain(d.vs.key) || !plain(d.ps.key)) continue;  // The START policy's own shaders are not in the cache.
    const uint64_t key = PipelineKey(d.vs.key, d.ps.key, d.layout_id, d.state);
    if (!pipeline_list_keys.insert(key).second) continue;
    const auto world = pipeline_worlds.find(backend_key);
    std::string line = fmt::format("{:016x} {} {} {:016x} {:08x} {} {}", key, d.vs.key, d.ps.key, d.layout_id,
                                   world != pipeline_worlds.end() ? world->second : 0u, fmt::join(d.state, " "),
                                   d.elements->size());
    for (const auto& e : *d.elements)
      line += fmt::format(" {} {} {} {} {} {}", e.semantic, e.semantic_index, uint32_t(e.format), e.slot, e.offset,
                          e.instance_step);
    pipeline_list_lines.push_back(std::move(line));
    learned = true;
  }
  return learned;
}

void Backend::State::SavePipelineCache(bool wait) {
  if (!pipeline_cache || pipeline_cache_path.empty()) return;
  if (pipeline_cache_writer.joinable()) {
    if (!wait && pipeline_cache_writing.load(std::memory_order_acquire)) return;  // The last save is still writing.
    pipeline_cache_writer.join();
  }
  const uint64_t created = pipelines_created.load(std::memory_order_relaxed);
  const bool learned = LearnPipelines();
  if (created == pipelines_saved && !learned) return;
  const bool cache_changed = created != pipelines_saved;
  pipelines_saved = created;
  pipeline_cache_saved_at = std::chrono::steady_clock::now();
  const VkDevice device = vulkan->vk;
  const VkPipelineCache cache = pipeline_cache;
  const std::string path = pipeline_cache_path, list_path = pipeline_list_path;
  std::string list;
  if (learned) {
    list.reserve(pipeline_list_lines.size() * 160);
    for (const auto& line : pipeline_list_lines) (list += line) += '\n';
  }
  const size_t listed = pipeline_list_lines.size();
  const auto write = [this, device, cache, path, list_path, list = std::move(list), cache_changed, created, listed] {
    size_t written = 0;
    const bool saved = !cache_changed || WritePipelineCache(device, cache, path, written);
    if (saved && cache_changed) {
      pipeline_cache_saves.fetch_add(1, std::memory_order_relaxed);
      pipeline_cache_saved_bytes.store(written, std::memory_order_relaxed);
    }
    const bool list_saved = list.empty() ||
        ReplaceFile(list_path, reinterpret_cast<const uint8_t*>(list.data()), list.size());
    PipelineCacheLog(!saved || !list_saved,
                     fmt::format("{}{}", !cache_changed ? std::string()
                                         : saved ? fmt::format("saved {} KB ({} pipelines created this session)",
                                                               written / 1024, created)
                                                 : std::string("could not be saved"),
                                 list.empty() ? std::string()
                                 : list_saved ? fmt::format("{}list of {} pipelines saved", cache_changed ? "; " : "", listed)
                                              : std::string("; the pipeline list could not be saved")));
    pipeline_cache_writing.store(false, std::memory_order_release);
  };
  pipeline_cache_writing.store(true, std::memory_order_release);
  if (wait) write();
  else pipeline_cache_writer = std::thread(write);
}

void Backend::State::MaybeSavePipelineCache() {
  if (!pipeline_cache || pipeline_cache_path.empty() ||
      (pipelines_created.load(std::memory_order_relaxed) == pipelines_saved &&
       pipeline_order_listed == pipeline_order.size()) ||
      std::chrono::steady_clock::now() - pipeline_cache_saved_at < kPipelineCacheSaveInterval)
    return;
  SavePipelineCache(false);
}

void Backend::State::ReleasePipelineCache() {
  if (pipeline_cache_writer.joinable()) pipeline_cache_writer.join();
  if (!pipeline_cache) return;
  SavePipelineCache(true);
  vkDestroyPipelineCache(vulkan->vk, pipeline_cache, nullptr);
  pipeline_cache = VK_NULL_HANDLE;
}

std::string Backend::State::PipelineCacheSummary() const {
  return fmt::format("{{\"status\":\"{}\",\"file\":{},\"loaded_bytes\":{},\"created\":{},\"saves\":{},\"saved_bytes\":{},"
                     "\"list_loaded\":{},\"list\":{}}}",
                     JsonText(pipeline_cache_status), !pipeline_cache_path.empty(), pipeline_cache_loaded,
                     pipelines_created.load(std::memory_order_relaxed),
                     pipeline_cache_saves.load(std::memory_order_relaxed),
                     pipeline_cache_saved_bytes.load(std::memory_order_relaxed), pipeline_list_loaded,
                     pipeline_list_lines.size());
}

// The draw census (CountDraw), every 30 s with the profile: the draws per
// frame of each pair of targets, the largest first.
void Backend::State::LogDrawCensus() {
  if (!profile::Profiling()) return;
  const auto now = std::chrono::steady_clock::now();
  if (draw_census_since == std::chrono::steady_clock::time_point{}) {
    draw_census_since = now;
    draw_census_presents = presents;
    draw_census.clear();
    return;
  }
  if (now - draw_census_since < std::chrono::seconds(30)) return;
  const uint64_t frames = std::max<uint64_t>(1, presents - draw_census_presents);
  std::vector<std::pair<uint64_t, uint64_t>> sorted(draw_census.begin(), draw_census.end());
  std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  uint64_t total = 0;
  for (const auto& [key, count] : sorted) total += count;
  std::string text = fmt::format("{} frames, {:.0f} draws per frame:", frames, double(total) / frames);
  const auto size = [](uint64_t s) {
    return s ? fmt::format("{}x{}", (s >> 15) & 0x7FFF, s & 0x7FFF) : std::string("-");
  };
  for (size_t i = 0; i < sorted.size() && i < 16; ++i) {
    const uint64_t key = sorted[i].first;
    text += fmt::format(" [{}{} color {} depth {}: {:.1f}]", (key >> 60) & 1 ? "depth-only" : "draw", "",
                        size((key >> 30) & 0x3FFFFFFF), size(key & 0x3FFFFFFF), double(sorted[i].second) / frames);
  }
#ifdef __ANDROID__
  __android_log_print(ANDROID_LOG_INFO, "narutobb", "Draw census: %s", text.c_str());
#else
  (void)text;
#endif
  if (!surfaces_snapshotted && double(total) / frames > 300) SnapshotSurfaces();
  draw_census.clear();
  draw_census_since = now;
  draw_census_presents = presents;
}

void Backend::State::SnapshotSurfaces() {
  surfaces_snapshotted = true;
  const char* dir = std::getenv("REX_NATIVE_FRAME_DIR");
  if (!dir || !*dir) return;
  const std::filesystem::path folder = std::filesystem::path(dir) / "surfaces";
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  if (error) return;
  const char* reason = nullptr;
  VkResult result = VK_SUCCESS;
  if (!CloseBatch(reason, result)) return;
  size_t written = 0;
  for (const auto& [size, key] : census_color_keys) {
    auto it = surfaces.find(key);
    if (it == surfaces.end() || !it->second.texture || it->second.desc.format != SurfaceFormat::Color8888 ||
        it->second.desc.samples != 1)
      continue;
    auto& surface = it->second;
    if (surface.last_use && !completion->WaitFor(surface.last_use, kWaitBudget)) continue;
    const uint32_t w = surface.desc.width, h = surface.desc.height;
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    uint64_t sequence = 0;
    std::shared_ptr<VmaBuffer> buffer;
    if (!ReadImage(surface.Image(), surface.state, VkDeviceSize(w) * h * 4, region, sequence, buffer, reason, result))
      continue;
    surface.last_use = sequence;
    // Half size above 480 pixels (2 x 2 averages): enough to tell the passes apart.
    const uint32_t step = w > 480 ? 2 : 1, ow = w / step, oh = h / step;
    const uint32_t row = (ow * 3 + 3) & ~3u;
    std::vector<uint8_t> bmp(54 + size_t(row) * oh);
    const auto put32 = [&](size_t at, uint32_t v) { std::memcpy(bmp.data() + at, &v, 4); };
    bmp[0] = 'B'; bmp[1] = 'M';
    put32(2, uint32_t(bmp.size())); put32(10, 54); put32(14, 40); put32(18, ow); put32(22, oh);
    bmp[26] = 1; bmp[28] = 24; put32(34, uint32_t(size_t(row) * oh));
    const uint8_t* rgba = buffer->mapped;
    for (uint32_t y = 0; y < oh; ++y) {
      uint8_t* out = bmp.data() + 54 + size_t(oh - 1 - y) * row;  // Bottom-up rows.
      for (uint32_t x = 0; x < ow; ++x) {
        uint32_t sum[3]{};
        for (uint32_t dy = 0; dy < step; ++dy)
          for (uint32_t dx = 0; dx < step; ++dx) {
            const uint8_t* p = rgba + (size_t(y * step + dy) * w + x * step + dx) * 4;
            for (int c = 0; c < 3; ++c) sum[c] += p[c];
          }
        for (int c = 0; c < 3; ++c) out[x * 3 + 2 - c] = uint8_t(sum[c] / (step * step));  // BGR.
      }
    }
    const auto name = folder / fmt::format("{}x{}.bmp", w, h);
    if (std::FILE* f = std::fopen(name.string().c_str(), "wb")) {
      written += std::fwrite(bmp.data(), 1, bmp.size(), f) == bmp.size();
      std::fclose(f);
    }
  }
  census_color_keys.clear();
#ifdef __ANDROID__
  __android_log_print(ANDROID_LOG_INFO, "narutobb", "Surface images: %zu in %s", written, folder.string().c_str());
#endif
}
