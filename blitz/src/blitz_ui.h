// blitz - ReXGlue Recompiled Project
//
// Shared look and input for the full-screen menus (launcher, options, first-run
// setup): colours, a tiny ImGui draw helper, and keyboard/controller polling
// read straight from Windows and XInput so the menus behave the same whatever
// the SDK forwards to ImGui. Style adapted from darkuni's recomp menus (MIT),
// see blitz_menu.h.

#pragma once

#include <cfloat>

#include <imgui.h>

#ifdef _WIN32
#include <Windows.h>
#include <Xinput.h>
#else
#include <SDL3/SDL.h>
#endif

namespace blitz::ui {

// ------------------------------------------------------------------ colours
inline const ImU32 kWhite = IM_COL32(255, 255, 255, 255);
inline const ImU32 kGrey = IM_COL32(178, 178, 191, 255);
inline const ImU32 kAccent = IM_COL32(140, 230, 60, 255);  // Blitz green
inline const ImU32 kError = IM_COL32(255, 110, 90, 255);
inline const ImU32 kPanel = IM_COL32(12, 16, 12, 236);
inline const ImU32 kHighlight = IM_COL32(60, 110, 30, 235);
inline const ImU32 kShade = IM_COL32(0, 0, 0, 120);
inline const ImU32 kBackdrop = IM_COL32(6, 8, 6, 255);
inline const ImU32 kTrack = IM_COL32(40, 50, 40, 255);

// ------------------------------------------------------------------ input
struct InputState {
  bool up, down, left, right, accept, back;
};

#ifdef _WIN32
// XInputGetStateEx (ordinal 100) also reports the Guide button (0x0400).
using XInputGetStateExFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
inline XInputGetStateExFn LoadXInput() {
  static XInputGetStateExFn fn = [] {
    HMODULE dll = LoadLibraryW(L"xinput1_4.dll");
    if (!dll) dll = LoadLibraryW(L"xinput1_3.dll");
    XInputGetStateExFn f = nullptr;
    if (dll) {
      f = reinterpret_cast<XInputGetStateExFn>(GetProcAddress(dll, reinterpret_cast<LPCSTR>(100)));
      if (!f) f = reinterpret_cast<XInputGetStateExFn>(GetProcAddress(dll, "XInputGetState"));
    }
    return f;
  }();
  return fn;
}
inline constexpr WORD kGuideButton = 0x0400;

inline bool OurWindowInFront() {
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
}
inline bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
#endif

#ifndef _WIN32
// SDL3 (the SDK's window and input backend on Linux). Gamepads the SDK has
// not opened are opened once here; SDL keeps them open.
template <typename Fn>
void ForEachGamepad(Fn&& fn) {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  if (!ids) return;
  for (int i = 0; i < count; i++) {
    SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
    if (!pad) pad = SDL_OpenGamepad(ids[i]);
    if (pad) fn(pad);
  }
  SDL_free(ids);
}
#endif

inline InputState ReadInput() {
  InputState s{};
#ifdef _WIN32
  if (OurWindowInFront()) {
    s.up = KeyDown(VK_UP);
    s.down = KeyDown(VK_DOWN);
    s.left = KeyDown(VK_LEFT);
    s.right = KeyDown(VK_RIGHT);
    s.accept = KeyDown(VK_RETURN) || KeyDown(VK_SPACE);
    s.back = KeyDown(VK_ESCAPE) || KeyDown(VK_BACK);
  }
  if (auto get_state = LoadXInput()) {
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
      XINPUT_STATE st{};
      if (get_state(i, &st) != ERROR_SUCCESS) continue;
      const WORD b = st.Gamepad.wButtons;
      const SHORT ly = st.Gamepad.sThumbLY, lx = st.Gamepad.sThumbLX;
      s.up |= (b & XINPUT_GAMEPAD_DPAD_UP) || ly > 16000;
      s.down |= (b & XINPUT_GAMEPAD_DPAD_DOWN) || ly < -16000;
      s.left |= (b & XINPUT_GAMEPAD_DPAD_LEFT) || lx < -16000;
      s.right |= (b & XINPUT_GAMEPAD_DPAD_RIGHT) || lx > 16000;
      s.accept |= (b & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START)) != 0;
      s.back |= (b & XINPUT_GAMEPAD_B) != 0;
    }
  }
#else
  if (SDL_GetKeyboardFocus()) {
    const bool* k = SDL_GetKeyboardState(nullptr);
    s.up = k[SDL_SCANCODE_UP];
    s.down = k[SDL_SCANCODE_DOWN];
    s.left = k[SDL_SCANCODE_LEFT];
    s.right = k[SDL_SCANCODE_RIGHT];
    s.accept = k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE];
    s.back = k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_BACKSPACE];
  }
  ForEachGamepad([&s](SDL_Gamepad* pad) {
    const auto b = [pad](SDL_GamepadButton btn) { return SDL_GetGamepadButton(pad, btn); };
    const Sint16 lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    const Sint16 ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);  // + is down
    s.up |= b(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -16000;
    s.down |= b(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > 16000;
    s.left |= b(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -16000;
    s.right |= b(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > 16000;
    s.accept |= b(SDL_GAMEPAD_BUTTON_SOUTH) || b(SDL_GAMEPAD_BUTTON_START);
    s.back |= b(SDL_GAMEPAD_BUTTON_EAST);
  });
#endif
  return s;
}

// True while the controller's menu chord is held: Guide, or LB + RB + Back.
inline bool MenuChordDown() {
  bool down = false;
#ifdef _WIN32
  const WORD chord =
      XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_BACK;
  if (auto get_state = LoadXInput()) {
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
      XINPUT_STATE st{};
      if (get_state(i, &st) != ERROR_SUCCESS) continue;
      const WORD b = st.Gamepad.wButtons;
      down |= (b & kGuideButton) != 0 || (b & chord) == chord;
    }
  }
#else
  ForEachGamepad([&down](SDL_Gamepad* pad) {
    const auto b = [pad](SDL_GamepadButton btn) { return SDL_GetGamepadButton(pad, btn); };
    down |= b(SDL_GAMEPAD_BUTTON_GUIDE) ||
            (b(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) && b(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) &&
             b(SDL_GAMEPAD_BUTTON_BACK));
  });
#endif
  return down;
}

inline bool AnyInput(const InputState& s) {
  return s.up || s.down || s.left || s.right || s.accept || s.back;
}

// Edge + auto-repeat for one input.
struct Repeater {
  bool held = false;
  double next = 0;
  bool Update(bool down, double now, bool repeat = true) {
    if (!down) {
      held = false;
      return false;
    }
    if (!held) {
      held = true;
      next = now + 0.40;
      return true;
    }
    if (repeat && now >= next) {
      next = now + 0.09;
      return true;
    }
    return false;
  }
};

// ------------------------------------------------------------------ drawing
struct Painter {
  ImDrawList* dl;
  ImFont* font;
  float size;
  float TextWidth(const char* s, float scale = 1.0f) const {
    return font->CalcTextSizeA(size * scale, FLT_MAX, 0.0f, s).x;
  }
  void Text(float x, float y, const char* s, ImU32 col, float scale = 1.0f) const {
    dl->AddText(font, size * scale, ImVec2(x, y), col, s);
  }
  // Word-wrapped text; returns the height used.
  float Wrapped(float x, float y, float width, const char* s, ImU32 col, float scale = 1.0f) const {
    const float sz = size * scale;
    dl->AddText(font, sz, ImVec2(x, y), col, s, nullptr, width);
    return font->CalcTextSizeA(sz, FLT_MAX, width, s).y;
  }
  void Rect(float x, float y, float w, float h, ImU32 col) const {
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), col);
  }
};

inline bool RowClicked(float x, float y, float w, float h, int button) {
  return ImGui::IsMouseHoveringRect(ImVec2(x, y), ImVec2(x + w, y + h)) &&
         ImGui::IsMouseClicked(button);
}

}  // namespace blitz::ui
