"""Apply the native renderer's device feature additions to a separate build copy of Plume."""
from pathlib import Path
import sys

p = Path(sys.argv[1])
output = Path(sys.argv[2])
s = p.read_text(encoding="utf-8")
assert "// Native renderer device features." not in s
s = s.replace("static const std::unordered_set<std::string> OptionalInstanceExtensions = {",
              """static const std::unordered_set<std::string> OptionalInstanceExtensions = {
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
        VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
        VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,""")
s = s.replace("static const std::unordered_set<std::string> OptionalDeviceExtensions = {",
              """static const std::unordered_set<std::string> OptionalDeviceExtensions = {
        VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
        VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME,
        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,""")
s = s.replace("        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,",
              "        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,\n        VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,")
features = [
    ("dynamic", "VkPhysicalDeviceExtendedDynamicStateFeaturesEXT", "EXTENDED_DYNAMIC_STATE", "extendedDynamicState"),
    ("border", "VkPhysicalDeviceCustomBorderColorFeaturesEXT", "CUSTOM_BORDER_COLOR", "customBorderColors"),
    ("maintenance", "VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT", "SWAPCHAIN_MAINTENANCE_1", "swapchainMaintenance1"),
]
query = "// Native renderer device features.\n"
enable = ""
for name, typ, enum, field in features:
    query += f"""
        {typ} {name}Features = {{}};
        if (supportedOptionalExtensions.count(VK_EXT_{enum}_EXTENSION_NAME)) {{
            {name}Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_{enum}_FEATURES_EXT;
            {name}Features.pNext = featuresChain;
            featuresChain = &{name}Features;
        }}
"""
    enable += f"""
        if ({name}Features.{field}) {{
            {name}Features.pNext = createDeviceChain;
            createDeviceChain = &{name}Features;
        }}
"""
# The extension name has _1, whereas the structure enum has 1.
query = query.replace("supportedOptionalExtensions.count(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)",
                     "(supportedOptionalExtensions.count(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) || "
                     "supportedOptionalExtensions.count(VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME))")
s = s.replace("        VkPhysicalDeviceFeatures2 deviceFeatures", query + "\n        VkPhysicalDeviceFeatures2 deviceFeatures")
s = s.replace("        std::vector<const char *> enabledExtensions;", enable + "\n        std::vector<const char *> enabledExtensions;", 1)
# Features that are core in Vulkan 1.2: queried (and enabled) on a 1.2 device
# even when its extension list leaves out the promoted extension's name.
for found, extension in (("descriptorIndexingFound", "VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME"),
                         ("scalarBlockLayoutFound", "VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME"),
                         ("bufferDeviceAddressFound", "VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME")):
    line = (f"const bool {found} = supportedOptionalExtensions.find({extension}) != "
            "supportedOptionalExtensions.end();")
    assert s.count(line) == 1, found
    s = s.replace(line, line[:-1] + " || physicalDeviceProperties.apiVersion >= VK_API_VERSION_1_2;")

# Android: the game can bring its own Vulkan driver (Turnip, loaded with
# libadrenotools; src/android/gpu_driver.cpp). volk then starts from that
# driver's vkGetInstanceProcAddr instead of the system's libvulkan.
android_loader = """#if defined(__ANDROID__)
extern "C" PFN_vkGetInstanceProcAddr brokenbond_android_vulkan_loader(void);
#endif

namespace plume {"""
assert s.count("namespace plume {") >= 1
s = s.replace("namespace plume {", android_loader, 1)
volk_init = "        VkResult res = volkInitialize();\n"
assert s.count(volk_init) == 1
s = s.replace(volk_init, """#if defined(__ANDROID__)
        VkResult res = VK_SUCCESS;
        if (PFN_vkGetInstanceProcAddr bundledLoader = brokenbond_android_vulkan_loader()) {
            volkInitializeCustom(bundledLoader);
        } else {
            res = volkInitialize();
        }
#else
        VkResult res = volkInitialize();
#endif
""")
if not output.exists() or output.read_text(encoding="utf-8") != s:
    output.write_text(s, encoding="utf-8", newline="\n")
