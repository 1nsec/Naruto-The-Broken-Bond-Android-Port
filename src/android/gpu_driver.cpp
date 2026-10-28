// The Vulkan driver of the game on Android: the system's, or Turnip (Mesa's
// open-source driver for Adreno GPUs) bundled with the app and loaded with
// libadrenotools.
//
// The renderer needs features that the Qualcomm drivers of older Adreno GPUs
// (the Adreno 650 of the Snapdragon 865, for example) do not offer, such as
// 64-bit integers in shaders. Turnip has them. GameActivity passes the choice
// of the launcher's settings and where the bundled driver is:
//   BROKENBOND_GPU_DRIVER   auto (default), system or turnip
//   BROKENBOND_DRIVER_DIR   folder of the bundled driver (app-private storage)
//   BROKENBOND_DRIVER_NAME  its file name
//   BROKENBOND_HOOK_DIR     the app's native library folder (adrenotools' hooks)
// "auto" asks the system driver first and takes Turnip on a Qualcomm GPU that
// lacks one of the features. Plume's Vulkan loader (volk) calls
// brokenbond_android_vulkan_loader() before it loads the system's libvulkan
// (tools/native-renderer/extend-plume-vulkan.py); a null result keeps the
// system driver.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

#include <dlfcn.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <rex/logging.h>

#if BROKENBOND_ADRENOTOOLS
#include <adrenotools/driver.h>
#endif

namespace {

constexpr uint32_t kQualcommVendorId = 0x5143;

struct DeviceReport {
  bool found = false;
  uint32_t vendor = 0, api = 0;
  bool int64 = false, bc = false;
  std::string name;
};

// What a driver offers, from its first device: the renderer needs Vulkan 1.2,
// 64-bit shader integers and the BC texture formats.
DeviceReport Probe(PFN_vkGetInstanceProcAddr gipa) {
  DeviceReport report;
  auto create = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
  if (!create) return report;
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "brokenbond-probe";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  info.pApplicationInfo = &app;
  VkInstance instance = VK_NULL_HANDLE;
  if (create(&info, nullptr, &instance) != VK_SUCCESS) return report;
  auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance, "vkDestroyInstance"));
  auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance, "vkEnumeratePhysicalDevices"));
  auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(instance, "vkGetPhysicalDeviceProperties"));
  auto features = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(gipa(instance, "vkGetPhysicalDeviceFeatures"));
  uint32_t count = 1;
  VkPhysicalDevice device = VK_NULL_HANDLE;
  if (enumerate && properties && features) {
    VkResult result = enumerate(instance, &count, &device);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && count > 0 && device) {
      VkPhysicalDeviceProperties p{};
      properties(device, &p);
      VkPhysicalDeviceFeatures f{};
      features(device, &f);
      report.found = true;
      report.vendor = p.vendorID;
      report.api = p.apiVersion;
      report.int64 = f.shaderInt64;
      report.bc = f.textureCompressionBC;
      report.name = p.deviceName;
    }
  }
  if (destroy) destroy(instance, nullptr);
  return report;
}

bool Sufficient(const DeviceReport& r) {
  return r.found && r.api >= VK_API_VERSION_1_2 && r.int64 && r.bc;
}

void Log(const char* driver, const DeviceReport& r) {
  REXLOG_INFO("[narutobb] GPU driver {}: {} (vendor 0x{:04x}, Vulkan {}.{}.{}, shaderInt64 {}, BC textures {})",
              driver, r.found ? r.name : std::string("no device"), r.vendor, VK_API_VERSION_MAJOR(r.api),
              VK_API_VERSION_MINOR(r.api), VK_API_VERSION_PATCH(r.api), r.int64, r.bc);
}

const char* Env(const char* name) {
  const char* value = std::getenv(name);
  return value && *value ? value : nullptr;
}

#if BROKENBOND_ADRENOTOOLS
PFN_vkGetInstanceProcAddr OpenBundled() {
  const char* dir = Env("BROKENBOND_DRIVER_DIR");
  const char* name = Env("BROKENBOND_DRIVER_NAME");
  const char* hooks = Env("BROKENBOND_HOOK_DIR");
  if (!dir || !name || !hooks) {
    REXLOG_INFO("[narutobb] GPU driver: no bundled driver in this build");
    return nullptr;
  }
  std::string folder = dir;
  if (folder.back() != '/') folder += '/';
  std::string hook_folder = hooks;
  if (hook_folder.back() != '/') hook_folder += '/';
  void* handle = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, nullptr, hook_folder.c_str(),
                                            folder.c_str(), name, nullptr, nullptr);
  if (!handle) {
    REXLOG_WARN("[narutobb] GPU driver: adrenotools could not load {}{}: {}", folder, name, dlerror());
    return nullptr;
  }
  return reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(handle, "vkGetInstanceProcAddr"));
}
#endif

}  // namespace

extern "C" PFN_vkGetInstanceProcAddr brokenbond_android_vulkan_loader(void) {
  const std::string_view mode = Env("BROKENBOND_GPU_DRIVER") ? Env("BROKENBOND_GPU_DRIVER") : "auto";
  if (mode == "system") {
    REXLOG_INFO("[narutobb] GPU driver: system (chosen in Settings)");
    return nullptr;
  }
  DeviceReport system;
  if (mode != "turnip") {
    void* lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    auto gipa = lib ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr")) : nullptr;
    if (gipa) system = Probe(gipa);
    Log("system", system);
    if (Sufficient(system)) return nullptr;
    if (system.found && system.vendor != kQualcommVendorId) {
      REXLOG_WARN("[narutobb] GPU driver: the system driver lacks features the renderer needs, and Turnip only "
                  "runs on Adreno GPUs");
      return nullptr;
    }
  }
#if BROKENBOND_ADRENOTOOLS
  PFN_vkGetInstanceProcAddr turnip = OpenBundled();
  if (turnip) {
    DeviceReport bundled = Probe(turnip);
    Log("Turnip (bundled)", bundled);
    if (bundled.found) return turnip;
    REXLOG_WARN("[narutobb] GPU driver: Turnip found no GPU it supports; using the system driver");
  }
#else
  REXLOG_INFO("[narutobb] GPU driver: this build has no adrenotools; using the system driver");
#endif
  return nullptr;
}
