#include "player_assignment.h"

#include <algorithm>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(keyboard_player, "auto", "Input",
                      "Player the keyboard controls: auto (shares player 1 with the first "
                      "controller), 1, or 2 (needs a controller for player 1)")
    .allowed({"auto", "1", "2"});

using rex::input::DeviceId;
using rex::input::DeviceInfo;
using rex::input::kMaxGuestUsers;

namespace {

// The keyboard/mouse device the SDK's MnK driver reports.
bool IsKeyboard(const DeviceInfo& device) {
  return device.synthetic && device.name == "Keyboard and Mouse";
}

#ifdef __ANDROID__
// The Android app's on-screen controller (src/android/touch_controller.cpp).
bool IsOnScreen(const DeviceInfo& device) {
  return !device.synthetic && device.name == "On-screen controller";
}
#endif

}  // namespace

void PlayerAssignment::OnDevicesChanged(const std::vector<DeviceInfo>& devices) {
  std::lock_guard lock(mutex_);
  devices_ = devices;
  for (const auto& device : devices) {
    REXLOG_INFO("[input] device '{}' ordinal {}{}", device.name,
                device.synthetic ? std::string("-") : std::to_string(device.ordinal),
                device.synthetic ? " (synthetic)" : "");
  }
}

namespace {
const PlayerAssignment* g_current = nullptr;
}  // namespace

const PlayerAssignment* PlayerAssignment::Current() {
  return g_current;
}

uint32_t PlayerAssignment::Rank(const DeviceInfo& device) const {
#ifndef __ANDROID__
  // The SDK's ordinal: unplugging pad one does not move pad two to player 1.
  return device.ordinal;
#else
  // The on-screen controller attaches with the game and would take ordinal 0
  // from a controller built into the device or connected later.
  if (device.synthetic) return 0;
  uint32_t rank = 0;
  for (const auto& other : devices_) {
    if (other.synthetic || other.id == device.id) continue;
    // Physical controllers first, each group in the SDK's order.
    const bool before = IsOnScreen(other) == IsOnScreen(device) ? other.ordinal < device.ordinal
                                                                 : IsOnScreen(device);
    if (before) ++rank;
  }
  return rank;
#endif
}

uint32_t PlayerAssignment::UserFor(const DeviceInfo& device) const {
  const std::string mode = REXCVAR_GET(keyboard_player);
  const bool any_pad = std::any_of(devices_.begin(), devices_.end(),
                                   [](const DeviceInfo& d) { return !d.synthetic; });
  // Player the keyboard controls; controllers move up to make room for it.
  const bool keyboard_first = any_pad && mode == "1";
  const bool keyboard_second = any_pad && mode == "2";
  if (IsKeyboard(device)) {
    return keyboard_second ? 1 : 0;
  }
  if (device.synthetic) {
    return 0;  // the NOP stand-in keeps player 1 connected
  }
  const uint32_t rank = Rank(device);
  if (keyboard_first) return rank + 1;
  if (keyboard_second) return rank == 0 ? 0 : rank + 1;
  return rank;
}

void PlayerAssignment::DevicesForUser(uint32_t user_index, std::vector<DeviceId>& out) const {
  g_current = this;
  out.clear();
  if (user_index >= kMaxGuestUsers) {
    return;
  }
  std::lock_guard lock(mutex_);
  const std::string mode = REXCVAR_GET(keyboard_player);
  if (mode != logged_mode_) {
    logged_mode_ = mode;
    REXLOG_INFO("[input] keyboard player: {}", mode);
  }
  for (const auto& device : devices_) {
    if (UserFor(device) == user_index) {
      out.push_back(device.id);
    }
  }
}

int PlayerAssignment::UserForDevice(DeviceId id) const {
  std::lock_guard lock(mutex_);
  for (const auto& device : devices_) {
    if (device.id == id) return static_cast<int>(UserFor(device));
  }
  return -1;
}

int PlayerAssignment::ControllerNumber(DeviceId id) const {
  std::lock_guard lock(mutex_);
  const DeviceInfo* self = nullptr;
  for (const auto& device : devices_) {
    if (device.id == id && !device.synthetic) self = &device;
  }
  if (!self) return -1;
#ifdef __ANDROID__
  return static_cast<int>(Rank(*self));
#else
  int number = 0;
  for (const auto& device : devices_) {
    if (!device.synthetic && device.ordinal < self->ordinal) ++number;
  }
  return number;
#endif
}
