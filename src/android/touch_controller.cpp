// The on-screen controller of the Android app (TouchControllerView.java) as an
// SDL virtual gamepad: the SDK's SDL input driver, and the PC Options menu
// (pad_input_sdl.cpp), see it as one more controller. Called from the Java UI
// thread; SDL's joystick calls lock internally.
#include <jni.h>

#include <mutex>

#include <SDL3/SDL.h>

#include <rex/logging.h>

namespace {

std::mutex mutex;
SDL_JoystickID joystick_id = 0;
SDL_Joystick* joystick = nullptr;

// The buttons SOUTH (A) to DPAD_RIGHT and the six axes, in SDL's gamepad
// order, so the Java side uses SDL_GamepadButton / SDL_GamepadAxis numbers.
constexpr int kButtons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
constexpr int kAxes = SDL_GAMEPAD_AXIS_COUNT;

bool Attach() {
  if (joystick) return true;
  // SDL starts with the game; touches before that are dropped.
  if (!SDL_WasInit(SDL_INIT_JOYSTICK)) return false;
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = kAxes;
  desc.nbuttons = kButtons;
  desc.button_mask = (1u << kButtons) - 1;
  desc.axis_mask = (1u << kAxes) - 1;
  desc.name = "On-screen controller";
  joystick_id = SDL_AttachVirtualJoystick(&desc);
  if (!joystick_id) {
    REXLOG_WARN("[narutobb] On-screen controller: SDL_AttachVirtualJoystick failed: {}", SDL_GetError());
    return false;
  }
  joystick = SDL_OpenJoystick(joystick_id);
  if (!joystick) {
    REXLOG_WARN("[narutobb] On-screen controller: SDL_OpenJoystick failed: {}", SDL_GetError());
    SDL_DetachVirtualJoystick(joystick_id);
    joystick_id = 0;
    return false;
  }
  REXLOG_INFO("[narutobb] On-screen controller attached as an SDL virtual gamepad");
  return true;
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL Java_org_brokenbond_port_TouchControllerView_nativeAttach(JNIEnv*, jclass) {
  std::lock_guard lock(mutex);
  return Attach() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_org_brokenbond_port_TouchControllerView_nativeButton(JNIEnv*, jclass,
                                                                                         jint button,
                                                                                         jboolean pressed) {
  std::lock_guard lock(mutex);
  if (!Attach() || button < 0 || button >= kButtons) return;
  SDL_SetJoystickVirtualButton(joystick, button, pressed == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL Java_org_brokenbond_port_TouchControllerView_nativeAxis(JNIEnv*, jclass, jint axis,
                                                                                       jint value) {
  std::lock_guard lock(mutex);
  if (!Attach() || axis < 0 || axis >= kAxes) return;
  if (value < -32768) value = -32768;
  if (value > 32767) value = 32767;
  SDL_SetJoystickVirtualAxis(joystick, axis, static_cast<Sint16>(value));
}
