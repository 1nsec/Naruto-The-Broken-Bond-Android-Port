// Downloadable content: the Xbox Live Marketplace packages of the game (STFS files whose header
// says LIVE, PIRS or CON, content type 0x00000002) put in dlc/ next to the executable are
// installed at the start, before the game's main thread runs, as the SDK's content manager
// reads them: extracted into hdd/0000000000000000/<title id>/00000002/<package file name>/ with a
// .header file that carries the package's display name and license bits
// (ContentManager::InstallContent). A package installed is deleted from dlc/; one for another
// game, another kind of content or one that does not read stays there and is logged.
//
// On Android the launcher copies the packages the player picks into dlc/ in the data folder
// (LauncherActivity.java, DLC), so this is where they get installed.

#include "dlc_packages.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/logging.h>
#include <rex/string.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xcontent.h>

using rex::X_RESULT;  // X_ERROR_SUCCESS names it unqualified.

void InstallDlcPackages() {
  const std::filesystem::path folder = rex::filesystem::GetExecutableFolder() / "dlc";
  std::error_code ec;
  if (!std::filesystem::is_directory(folder, ec)) return;
  auto* kernel = rex::system::kernel_state();
  auto* content = kernel ? kernel->content_manager() : nullptr;
  if (!content) return;
  const uint32_t title_id = kernel->title_id();

  std::vector<std::filesystem::path> packages;
  for (const auto& entry : std::filesystem::directory_iterator(folder, ec))  // .name: a copy not finished.
    if (entry.is_regular_file(ec) && !entry.path().filename().string().starts_with(".")) packages.push_back(entry.path());

  for (const auto& path : packages) {
    const std::string name = rex::path_to_utf8(path.filename());
    const auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(path);
    if (!header) {
      REXLOG_WARN("[dlc] {}: not an Xbox 360 content package; left in dlc/", name);
      continue;
    }
    const rex::system::XContentType type = header->metadata.content_type;
    const uint32_t package_title = header->metadata.execution_info.title_id;
    if (type != rex::system::XContentType::kMarketplaceContent || package_title != title_id) {
      REXLOG_WARN("[dlc] {}: content type {:08X} of title {:08X}, not downloadable content of this game "
                  "({:08X}); left in dlc/",
                  name, uint32_t(type), package_title, title_id);
      continue;
    }
    const std::string display =
        rex::string::to_utf8(header->metadata.display_name(rex::system::XLanguage::kEnglish));
    const X_RESULT result = content->InstallContent(path);
    if (result != X_ERROR_SUCCESS) {
      REXLOG_ERROR("[dlc] {} ({}): install failed ({:08X}); left in dlc/", name, display, result);
      continue;
    }
    std::filesystem::remove(path, ec);
    REXLOG_INFO("[dlc] installed {} ({})", display.empty() ? name : display, name);
  }
}
