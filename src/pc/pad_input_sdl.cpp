// pad_input.h on platforms without Windows (Android): controllers through
// SDL3's gamepad API instead of XInput (Bluetooth/USB pads and the launcher's
// on-screen controller, which is an SDL virtual gamepad), embedded resources
// through the resource table, and SDL message boxes.
#include "pad_input.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <vector>

#include <SDL3/SDL.h>

#include "platform/win32.h"

namespace {

// XInput button bits (XINPUT_GAMEPAD_*), the layout PadRaw.buttons uses.
constexpr uint16_t kXDpadUp = 0x0001, kXDpadDown = 0x0002, kXDpadLeft = 0x0004, kXDpadRight = 0x0008;
constexpr uint16_t kXStart = 0x0010, kXBack = 0x0020, kXLeftThumb = 0x0040, kXRightThumb = 0x0080;
constexpr uint16_t kXLeftShoulder = 0x0100, kXRightShoulder = 0x0200, kXGuide = 0x0400;
constexpr uint16_t kXA = 0x1000, kXB = 0x2000, kXX = 0x4000, kXY = 0x8000;

std::mutex pads_mutex;

// The connected gamepads in SDL's order: the "XInput slots" 0-3.
std::vector<SDL_Gamepad*> OpenPads() {
  std::vector<SDL_Gamepad*> pads;
  if (!SDL_WasInit(SDL_INIT_GAMEPAD)) return pads;
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  if (!ids) return pads;
  for (int i = 0; i < count && pads.size() < 4; ++i) {
    // The SDK's input driver normally has it open already.
    SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
    if (!pad) pad = SDL_OpenGamepad(ids[i]);
    if (pad) pads.push_back(pad);
  }
  SDL_free(ids);
  return pads;
}

int16_t Axis(SDL_Gamepad* pad, SDL_GamepadAxis axis, bool invert) {
  const int v = SDL_GetGamepadAxis(pad, axis);
  // SDL's Y axes grow downwards; XInput's grow upwards.
  return int16_t(std::clamp(invert ? -v - 1 : v, -32768, 32767));
}

uint8_t Trigger(SDL_Gamepad* pad, SDL_GamepadAxis axis) {
  return uint8_t(std::clamp(SDL_GetGamepadAxis(pad, axis) * 255 / 32767, 0, 255));
}

PadRaw ReadOne(SDL_Gamepad* pad) {
  PadRaw raw;
  auto button = [pad](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
  if (button(SDL_GAMEPAD_BUTTON_DPAD_UP)) raw.buttons |= kXDpadUp;
  if (button(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) raw.buttons |= kXDpadDown;
  if (button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) raw.buttons |= kXDpadLeft;
  if (button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) raw.buttons |= kXDpadRight;
  if (button(SDL_GAMEPAD_BUTTON_START)) raw.buttons |= kXStart;
  if (button(SDL_GAMEPAD_BUTTON_BACK)) raw.buttons |= kXBack;
  if (button(SDL_GAMEPAD_BUTTON_LEFT_STICK)) raw.buttons |= kXLeftThumb;
  if (button(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) raw.buttons |= kXRightThumb;
  if (button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) raw.buttons |= kXLeftShoulder;
  if (button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) raw.buttons |= kXRightShoulder;
  if (button(SDL_GAMEPAD_BUTTON_GUIDE)) raw.buttons |= kXGuide;
  if (button(SDL_GAMEPAD_BUTTON_SOUTH)) raw.buttons |= kXA;
  if (button(SDL_GAMEPAD_BUTTON_EAST)) raw.buttons |= kXB;
  if (button(SDL_GAMEPAD_BUTTON_WEST)) raw.buttons |= kXX;
  if (button(SDL_GAMEPAD_BUTTON_NORTH)) raw.buttons |= kXY;
  raw.left_trigger = Trigger(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
  raw.right_trigger = Trigger(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
  raw.left_x = Axis(pad, SDL_GAMEPAD_AXIS_LEFTX, false);
  raw.left_y = Axis(pad, SDL_GAMEPAD_AXIS_LEFTY, true);
  raw.right_x = Axis(pad, SDL_GAMEPAD_AXIS_RIGHTX, false);
  raw.right_y = Axis(pad, SDL_GAMEPAD_AXIS_RIGHTY, true);
  return raw;
}

}  // namespace

PadRaw ReadPadRaw() {
  std::lock_guard lock(pads_mutex);
  PadRaw combined;
  for (SDL_Gamepad* pad : OpenPads()) {
    const PadRaw raw = ReadOne(pad);
    combined.buttons |= raw.buttons;
    combined.left_trigger = std::max(combined.left_trigger, raw.left_trigger);
    combined.right_trigger = std::max(combined.right_trigger, raw.right_trigger);
    auto further = [](int16_t& kept, int16_t& kept_other, int16_t x, int16_t y) {
      if (std::abs(x) + std::abs(y) > std::abs(kept) + std::abs(kept_other)) {
        kept = x;
        kept_other = y;
      }
    };
    further(combined.left_x, combined.left_y, raw.left_x, raw.left_y);
    further(combined.right_x, combined.right_y, raw.right_x, raw.right_y);
  }
  return combined;
}

PadRaw ReadPadRawAt(int index, bool* connected) {
  std::lock_guard lock(pads_mutex);
  const auto pads = OpenPads();
  const bool ok = index >= 0 && index < int(pads.size());
  if (connected) *connected = ok;
  PadRaw raw;
  if (ok) {
    const PadRaw full = ReadOne(pads[size_t(index)]);
    raw.buttons = full.buttons;
    raw.left_trigger = full.left_trigger;
    raw.right_trigger = full.right_trigger;
  }
  return raw;
}

uint32_t ReadPads() {
  const PadRaw raw = ReadPadRaw();
  uint32_t bits = 0;
  if ((raw.buttons & kXDpadUp) || raw.left_y > 16000) bits |= kPadUp;
  if ((raw.buttons & kXDpadDown) || raw.left_y < -16000) bits |= kPadDown;
  if ((raw.buttons & kXDpadLeft) || raw.left_x < -16000) bits |= kPadLeft;
  if ((raw.buttons & kXDpadRight) || raw.left_x > 16000) bits |= kPadRight;
  if (raw.buttons & kXA) bits |= kPadA;
  if (raw.buttons & kXB) bits |= kPadB;
  if (raw.buttons & kXX) bits |= kPadX;
  if (raw.buttons & kXY) bits |= kPadY;
  if (raw.buttons & kXStart) bits |= kPadStart;
  if (raw.buttons & kXBack) bits |= kPadBack;
  return bits;
}

bool CtrlHeld() { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }

void ShowGameError(const char* text) {
  MessageBoxA(nullptr, text, "Naruto The Broken Bond", MB_OK | MB_ICONERROR);
}

bool ReadResource(int id, const void** data, size_t* size) {
  HMODULE module = GetModuleHandleW(nullptr);
  HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
  HGLOBAL handle = info ? LoadResource(module, info) : nullptr;
  const void* bytes = handle ? LockResource(handle) : nullptr;
  if (!bytes) return false;
  *data = bytes;
  *size = SizeofResource(module, info);
  return true;
}

// One graphics device (the Vulkan driver's); the choice of card is a Windows
// option.
std::vector<std::pair<int, std::string>> ListGraphicsCards() { return {}; }
