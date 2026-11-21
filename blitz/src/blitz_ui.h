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
#endif
  return s;
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
