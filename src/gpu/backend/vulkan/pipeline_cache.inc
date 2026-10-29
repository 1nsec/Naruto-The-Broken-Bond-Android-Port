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
namespace {
constexpr char kPipelineCacheMagic[4] = {'N', 'B', 'P', 'C'};
constexpr uint32_t kPipelineCacheFormat = 1;
constexpr size_t kPipelineCacheHeader = 24;  // Magic, format, data bytes, hash.
constexpr auto kPipelineCacheSaveInterval = std::chrono::seconds(20);

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

// The cache's data into path, through a temporary file renamed over it.
bool WritePipelineCache(VkDevice device, VkPipelineCache cache, const std::string& path, size_t& written) {
  size_t size = 0;
  if (vkGetPipelineCacheData(device, cache, &size, nullptr) != VK_SUCCESS || !size) return false;
  std::vector<uint8_t> file(kPipelineCacheHeader + size);
  if (vkGetPipelineCacheData(device, cache, &size, file.data() + kPipelineCacheHeader) != VK_SUCCESS) return false;
  file.resize(kPipelineCacheHeader + size);
  const uint64_t bytes = size, hash = Fnv1a64(file.data() + kPipelineCacheHeader, size);
  std::memcpy(file.data(), kPipelineCacheMagic, 4);
  std::memcpy(file.data() + 4, &kPipelineCacheFormat, 4);
  std::memcpy(file.data() + 8, &bytes, 8);
  std::memcpy(file.data() + 16, &hash, 8);
  const std::string temporary = path + ".tmp";
  std::FILE* out = std::fopen(temporary.c_str(), "wb");
  if (!out) return false;
  const bool complete = std::fwrite(file.data(), 1, file.size(), out) == file.size();
  if (std::fclose(out) != 0 || !complete) return false;
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) return false;
  written = file.size();
  return true;
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
}

void Backend::State::SavePipelineCache(bool wait) {
  if (!pipeline_cache || pipeline_cache_path.empty()) return;
  if (pipeline_cache_writer.joinable()) {
    if (!wait && pipeline_cache_writing.load(std::memory_order_acquire)) return;  // The last save is still writing.
    pipeline_cache_writer.join();
  }
  const uint64_t created = pipelines_created.load(std::memory_order_relaxed);
  if (created == pipelines_saved) return;
  pipelines_saved = created;
  pipeline_cache_saved_at = std::chrono::steady_clock::now();
  const VkDevice device = vulkan->vk;
  const VkPipelineCache cache = pipeline_cache;
  const std::string path = pipeline_cache_path;
  const auto write = [this, device, cache, path, created] {
    size_t written = 0;
    const bool saved = WritePipelineCache(device, cache, path, written);
    if (saved) {
      pipeline_cache_saves.fetch_add(1, std::memory_order_relaxed);
      pipeline_cache_saved_bytes.store(written, std::memory_order_relaxed);
    }
    PipelineCacheLog(!saved, saved ? fmt::format("saved {} KB ({} pipelines created this session)", written / 1024, created)
                                   : std::string("could not be saved"));
    pipeline_cache_writing.store(false, std::memory_order_release);
  };
  pipeline_cache_writing.store(true, std::memory_order_release);
  if (wait) write();
  else pipeline_cache_writer = std::thread(write);
}

void Backend::State::MaybeSavePipelineCache() {
  if (!pipeline_cache || pipeline_cache_path.empty() ||
      pipelines_created.load(std::memory_order_relaxed) == pipelines_saved ||
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
  return fmt::format("{{\"status\":\"{}\",\"file\":{},\"loaded_bytes\":{},\"created\":{},\"saves\":{},\"saved_bytes\":{}}}",
                     JsonText(pipeline_cache_status), !pipeline_cache_path.empty(), pipeline_cache_loaded,
                     pipelines_created.load(std::memory_order_relaxed),
                     pipeline_cache_saves.load(std::memory_order_relaxed),
                     pipeline_cache_saved_bytes.load(std::memory_order_relaxed));
}
