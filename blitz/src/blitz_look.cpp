// blitz - ReXGlue Recompiled Project
//
// The menus' look, see blitz_look.h.

#include "blitz_look.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "blitz_palette.h"

namespace blitz::look {
namespace {

ImFont* g_heavy = nullptr;
ImFont* g_body = nullptr;
bool g_fonts_tried = false;

constexpr float kPi = 3.14159265f;

ImU32 Alpha(ImU32 c, float a) {
  const int base = int((c >> IM_COL32_A_SHIFT) & 0xFF);
  return (c & ~IM_COL32_A_MASK) | (ImU32(std::clamp(int(base * a), 0, 255)) << IM_COL32_A_SHIFT);
}

// a small repeatable random sequence: the same scene every frame
struct Rng {
  uint32_t s;
  float F() {
    s = s * 1664525u + 1013904223u;
    return float(s >> 8) / float(1u << 24);
  }
};

// a soft glow: stacked circles, faint at the edge
void Glow(ImDrawList* dl, ImVec2 c, float r, ImU32 col, int strength = 14) {
  // many faint layers: each adds ~1/64 of the colour, so the falloff has no visible rings
  const int steps = 64;
  const float per = 0.012f * float(strength) / 14.0f;
  for (int i = steps; i >= 1; i--) {
    const float t = float(i) / float(steps);
    dl->AddCircleFilled(c, r * t * t, Alpha(col, per), 64);
  }
}

ImFont* LoadFirst(std::initializer_list<const char*> paths, float px) {
  for (const char* p : paths) {
    std::error_code ec;
    const std::string s(p);
    if (std::filesystem::exists(std::filesystem::path(std::u8string(s.begin(), s.end())), ec)) {
      if (ImFont* f = ImGui::GetIO().Fonts->AddFontFromFileTTF(p, px)) return f;
    }
  }
  return nullptr;
}

void Vignette(ImDrawList* dl, ImVec2 size, float strength) {
  const float e = size.x * 0.22f;
  const ImU32 dark = IM_COL32(0, 0, 0, int(220 * strength)), clear = IM_COL32(0, 0, 0, 0);
  dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(e, size.y), dark, clear, clear, dark);
  dl->AddRectFilledMultiColor(ImVec2(size.x - e, 0), size, clear, dark, dark, clear);
  const float v = size.y * 0.2f;
  dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x, v), dark, dark, clear, clear);
  dl->AddRectFilledMultiColor(ImVec2(0, size.y - v), size, clear, clear, dark, dark);
}

// grime: dust and stains everywhere
void Grime(ImDrawList* dl, ImVec2 size, float s, uint32_t seed) {
  Rng r{seed};
  for (int i = 0; i < 900; i++) {
    const ImVec2 p(r.F() * size.x, r.F() * size.y);
    const float rad = (0.5f + r.F() * 1.6f) * s;
    const bool light = r.F() < 0.35f;
    dl->AddCircleFilled(p, rad, light ? IM_COL32(220, 220, 210, 10 + int(r.F() * 14)) : IM_COL32(0, 0, 0, 40 + int(r.F() * 50)), 6);
  }
}

// ------------------------------------------------------------------ Blitz: the locker room on tape
void LockerRoom(ImDrawList* dl, ImVec2 size, float time, float s) {
  const float w = size.x, h = size.y;
  dl->AddRectFilledMultiColor(ImVec2(0, 0), size, IM_COL32(16, 34, 36, 255), IM_COL32(16, 34, 36, 255),
                              IM_COL32(5, 10, 10, 255), IM_COL32(5, 10, 10, 255));
  // the corridor: back wall, two walls of lockers, floor and ceiling meeting at the far end
  const ImVec2 vp(w * 0.5f, h * 0.44f);
  const float bw = w * 0.16f, bh = h * 0.30f;  // back wall half size
  const ImVec2 b0(vp.x - bw, vp.y - bh * 0.62f), b1(vp.x + bw, vp.y + bh * 0.38f);
  dl->AddRectFilledMultiColor(b0, b1, IM_COL32(62, 88, 84, 255), IM_COL32(62, 88, 84, 255),
                              IM_COL32(40, 58, 56, 255), IM_COL32(40, 58, 56, 255));
  // floor and ceiling
  dl->AddQuadFilled(ImVec2(0, h), ImVec2(w, h), ImVec2(b1.x, b1.y), ImVec2(b0.x, b1.y), IM_COL32(34, 38, 34, 255));
  dl->AddQuadFilled(ImVec2(0, 0), ImVec2(w, 0), ImVec2(b1.x, b0.y), ImVec2(b0.x, b0.y), IM_COL32(8, 14, 14, 255));
  // the runner down the middle of the floor
  const float rn = bw * 0.14f;
  dl->AddQuadFilled(ImVec2(w * 0.5f - w * 0.07f, h), ImVec2(w * 0.5f + w * 0.07f, h), ImVec2(vp.x + rn, b1.y),
                    ImVec2(vp.x - rn, b1.y), IM_COL32(120, 66, 34, 255));
  // walls of lockers: doors get narrower towards the far end
  for (int side = -1; side <= 1; side += 2) {
    const float ex = side < 0 ? 0.0f : w, bx = side < 0 ? b0.x : b1.x;
    dl->AddQuadFilled(ImVec2(ex, 0), ImVec2(bx, b0.y), ImVec2(bx, b1.y), ImVec2(ex, h),
                      side < 0 ? IM_COL32(28, 62, 66, 255) : IM_COL32(24, 56, 60, 255));
    const int doors = 14;
    for (int k = 0; k <= doors; k++) {
      const float t = std::pow(float(k) / doors, 0.55f);
      const float x = ex + (bx - ex) * t, top = 0 + (b0.y - 0) * t, bot = h + (b1.y - h) * t;
      dl->AddLine(ImVec2(x, top + (bot - top) * 0.06f), ImVec2(x, bot - (bot - top) * 0.05f), IM_COL32(6, 16, 18, 230),
                  std::max(1.0f, 3.0f * s * (1.0f - t)));
      if (k < doors) {  // vents near the top of each door
        const float t2 = std::pow(float(k + 1) / doors, 0.55f), x2 = ex + (bx - ex) * t2;
        for (int v = 0; v < 4; v++) {
          const float vy = top + (bot - top) * (0.12f + v * 0.025f);
          dl->AddLine(ImVec2(x + (x2 - x) * 0.2f, vy), ImVec2(x + (x2 - x) * 0.8f, vy), IM_COL32(8, 20, 22, 200),
                      std::max(1.0f, 2.0f * s * (1.0f - t)));
        }
      }
    }
  }
  // the fluorescent tube, flickering now and then
  const float flick = std::fmod(time, 7.3f) < 0.18f && std::fmod(time * 31.0f, 1.0f) < 0.5f ? 0.35f : 1.0f;
  const ImVec2 lc(vp.x, b0.y - (b0.y) * 0.45f);
  Glow(dl, lc, w * 0.32f, IM_COL32(210, 255, 240, int(255 * flick)), 18);
  dl->AddRectFilled(ImVec2(lc.x - w * 0.05f, lc.y - 3 * s), ImVec2(lc.x + w * 0.05f, lc.y + 3 * s),
                    IM_COL32(235, 255, 248, int(240 * flick)), 2 * s);
  // blood on the runner
  // (on the floor below the panel and along the lockers, where the panel leaves it in view)
  Spatter(dl, ImVec2(w * 0.47f, h * 0.95f), 18 * s, 7, IM_COL32(120, 8, 8, 230));
  Spatter(dl, ImVec2(w * 0.24f, h * 0.84f), 12 * s, 21, IM_COL32(110, 6, 6, 220));
  Spatter(dl, ImVec2(w * 0.8f, h * 0.9f), 9 * s, 33, IM_COL32(100, 6, 6, 210));
  Spatter(dl, ImVec2(w * 0.12f, h * 0.35f), 14 * s, 45, IM_COL32(90, 6, 6, 200));
  Grime(dl, size, s, 99);
  Vignette(dl, size, 1.0f);
  // the tape: scanlines, frame corners, REC and a running timecode
  for (float y = 0; y < h; y += 3 * s) dl->AddLine(ImVec2(0, y), ImVec2(w, y), IM_COL32(0, 0, 0, 34), 1.0f);
  const float m = 34 * s, l = 46 * s, t = 3 * s;
  const ImU32 white = IM_COL32(235, 240, 235, 200);
  for (int cx = 0; cx < 2; cx++)
    for (int cy = 0; cy < 2; cy++) {
      const float x = cx ? w - m : m, y = cy ? h - m : m, dx = cx ? -l : l, dy = cy ? -l : l;
      dl->AddLine(ImVec2(x, y), ImVec2(x + dx, y), white, t);
      dl->AddLine(ImVec2(x, y), ImVec2(x, y + dy), white, t);
    }
  ImFont* f = Body() ? Body() : ImGui::GetFont();
  const float fs = 22 * s;
  if (std::fmod(time, 1.4f) < 0.9f) dl->AddCircleFilled(ImVec2(m + 24 * s, m + 26 * s), 8 * s, IM_COL32(230, 30, 30, 240));
  dl->AddText(f, fs, ImVec2(m + 40 * s, m + 14 * s), white, "REC");
  char tc[32];
  const int secs = int(time);
  std::snprintf(tc, sizeof tc, "00:%02d:%02d:%02d", secs / 60 % 60, secs % 60, int(time * 30) % 30);
  dl->AddText(f, fs, ImVec2(w - m - f->CalcTextSizeA(fs, FLT_MAX, 0, tc).x - 12 * s, m + 14 * s), white, tc);
}

// ------------------------------------------------------------------ Blitz II: floodlights over the fence
void Floodlights(ImDrawList* dl, ImVec2 size, float time, float s) {
  const float w = size.x, h = size.y;
  dl->AddRectFilledMultiColor(ImVec2(0, 0), size, IM_COL32(18, 26, 44, 255), IM_COL32(12, 16, 30, 255),
                              IM_COL32(4, 5, 8, 255), IM_COL32(6, 6, 10, 255));
  // fire glow from below right, smoke-blue haze left
  Glow(dl, ImVec2(w * 0.82f, h * 0.95f), w * 0.55f, IM_COL32(255, 110, 30, 255), 20);
  Glow(dl, ImVec2(w * 0.12f, h * 0.7f), w * 0.4f, IM_COL32(90, 120, 170, 200), 16);
  // the floodlight bank, top left, and its beam
  const ImVec2 fl(w * 0.1f, h * 0.14f);
  dl->AddTriangleFilled(fl, ImVec2(w * 0.95f, h * 0.55f), ImVec2(w * 0.55f, h * 1.05f), IM_COL32(200, 220, 255, 12));
  Glow(dl, fl, w * 0.2f, IM_COL32(220, 235, 255, 255), 16);
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 5; c++) {
      const ImVec2 p(fl.x - 50 * s + c * 26 * s + r * 10 * s, fl.y - 34 * s + r * 22 * s - c * 6 * s);
      dl->AddCircleFilled(p, 8 * s, IM_COL32(250, 252, 255, 235), 16);
    }
  // the chain-link fence
  const float gap = 30 * s;
  const ImU32 wire = IM_COL32(150, 175, 210, 26);
  for (float x = -h; x < w + h; x += gap) {
    dl->AddLine(ImVec2(x, 0), ImVec2(x + h, h), wire, 1.6f * s);
    dl->AddLine(ImVec2(x + h, 0), ImVec2(x, h), wire, 1.6f * s);
  }
  // embers rising
  Rng r{4242};
  for (int i = 0; i < 90; i++) {
    const float x0 = r.F() * w, speed = (0.03f + r.F() * 0.08f) * h, phase = r.F() * h, sz = (1.0f + r.F() * 2.4f) * s;
    const float y = h - std::fmod(phase + time * speed, h * 1.1f);
    const float x = x0 + std::sin(time * (0.6f + r.F()) + i) * 18 * s;
    const float life = y / h;
    dl->AddCircleFilled(ImVec2(x, y), sz, IM_COL32(255, 140 + int(r.F() * 80), 40, int(60 + 180 * life)), 8);
  }
  // blood in the corners
  Spatter(dl, ImVec2(w * 0.08f, h * 0.86f), 26 * s, 5, IM_COL32(120, 8, 8, 220));
  Spatter(dl, ImVec2(w * 0.93f, h * 0.12f), 18 * s, 17, IM_COL32(110, 6, 6, 200));
  Grime(dl, size, s, 7);
  Vignette(dl, size, 0.9f);
}

}  // namespace

const Palette& Game() { return kGamePalette; }
ImFont* Heavy() { return g_heavy; }
ImFont* Body() { return g_body; }

void EnsureFonts() {
  if (g_fonts_tried) return;
  g_fonts_tried = true;
#ifdef _WIN32
  wchar_t dir[MAX_PATH] = L"C:\\Windows";
  GetWindowsDirectoryW(dir, MAX_PATH);
  const std::u8string u8 = (std::filesystem::path(dir) / "Fonts").u8string();
  const std::string fonts = std::string(u8.begin(), u8.end()) + "\\";
  const std::string impact = fonts + "impact.ttf", bahn = fonts + "bahnschrift.ttf", arialb = fonts + "arialbd.ttf",
                    segoe = fonts + "segoeui.ttf";
  g_heavy = LoadFirst({impact.c_str(), arialb.c_str()}, 64.0f);
  g_body = LoadFirst({bahn.c_str(), segoe.c_str()}, 24.0f);
#else
  g_heavy = LoadFirst({"/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf",
                       "/usr/share/fonts/TTF/DejaVuSansCondensed-Bold.ttf",
                       "/usr/share/fonts/dejavu-sans-fonts/DejaVuSansCondensed-Bold.ttf",
                       "/usr/share/fonts/truetype/liberation/LiberationSansNarrow-Bold.ttf"},
                      64.0f);
  g_body = LoadFirst({"/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf",
                      "/usr/share/fonts/TTF/DejaVuSansCondensed.ttf",
                      "/usr/share/fonts/dejavu-sans-fonts/DejaVuSansCondensed.ttf",
                      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"},
                     24.0f);
#endif
}

void Backdrop(ImDrawList* dl, ImVec2 size, float time, float s) {
  if (Game().scene == Scene::kLockerRoom) LockerRoom(dl, size, time, s);
  else Floodlights(dl, size, time, s);
}

void Panel(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float s) {
  const bool locker = Game().scene == Scene::kLockerRoom;
  dl->AddRectFilled(ImVec2(p0.x + 10 * s, p0.y + 12 * s), ImVec2(p1.x + 10 * s, p1.y + 12 * s), IM_COL32(0, 0, 0, 120));
  const ImU32 top = locker ? IM_COL32(8, 20, 20, 214) : IM_COL32(10, 12, 20, 214);
  const ImU32 bot = locker ? IM_COL32(2, 6, 6, 228) : IM_COL32(2, 3, 6, 228);
  dl->AddRectFilledMultiColor(p0, p1, top, top, bot, bot);
  // scratches in the steel
  Rng r{locker ? 11u : 23u};
  for (int i = 0; i < 46; i++) {
    const ImVec2 a(p0.x + r.F() * (p1.x - p0.x), p0.y + r.F() * (p1.y - p0.y));
    const float len = (20 + r.F() * 90) * s, ang = -0.5f + r.F() * 0.35f;
    dl->AddLine(a, ImVec2(a.x + std::cos(ang) * len, a.y + std::sin(ang) * len), IM_COL32(200, 215, 220, 10 + int(r.F() * 12)),
                1.0f);
  }
  dl->AddRect(p0, p1, IM_COL32(190, 205, 210, 46), 0, 0, std::max(1.0f, 1.5f * s));
  // a torn strip of the accent colour across the top
  const float bh = 7 * s;
  dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + bh), Game().accent);
  Rng t{77};
  for (float x = p0.x; x < p1.x;) {
    const float step = (8 + t.F() * 18) * s, depth = (2 + t.F() * 7) * s;
    const float x2 = std::min(x + step, p1.x);
    dl->AddTriangleFilled(ImVec2(x, p0.y + bh), ImVec2(x2, p0.y + bh), ImVec2((x + x2) / 2, p0.y + bh + depth), Game().accent);
    x = x2;
  }
  // bolts in the corners
  for (const ImVec2& c : {ImVec2(p0.x + 14 * s, p0.y + 22 * s), ImVec2(p1.x - 14 * s, p0.y + 22 * s),
                          ImVec2(p0.x + 14 * s, p1.y - 14 * s), ImVec2(p1.x - 14 * s, p1.y - 14 * s)}) {
    dl->AddCircleFilled(c, 4.5f * s, IM_COL32(90, 100, 104, 255), 12);
    dl->AddCircleFilled(ImVec2(c.x - 1 * s, c.y - 1 * s), 2.0f * s, IM_COL32(170, 180, 184, 255), 8);
  }
}

void Outlined(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const char* text, float px) {
  const ImU32 ink = IM_COL32(0, 0, 0, (col >> IM_COL32_A_SHIFT) & 0xFF);
  for (int i = 0; i < 8; i++) {
    const float a = i * kPi / 4;
    dl->AddText(font, size, ImVec2(pos.x + std::cos(a) * px, pos.y + std::sin(a) * px), ink, text);
  }
  dl->AddText(font, size, pos, col, text);
}

float Title(ImDrawList* dl, float cx, float y, float s, float scale) {
  ImFont* f = Heavy() ? Heavy() : ImGui::GetFont();
  const Palette& g = Game();
  const float big = 92 * s * scale, small = 40 * s * scale;
  const float bw = f->CalcTextSizeA(big, FLT_MAX, 0, g.title).x;
  const float sw = f->CalcTextSizeA(small, FLT_MAX, 0, g.title2).x;
  // a shadow, the chrome, then a dark band across the lower half for the metal's shine
  Outlined(dl, f, big, ImVec2(cx - bw / 2 + 5 * s, y + 6 * s), IM_COL32(0, 0, 0, 150), g.title, 4 * s);
  Outlined(dl, f, big, ImVec2(cx - bw / 2, y), IM_COL32(238, 240, 244, 255), g.title, 3.5f * s);
  dl->PushClipRect(ImVec2(cx - bw, y + big * 0.56f), ImVec2(cx + bw, y + big), true);
  dl->AddText(f, big, ImVec2(cx - bw / 2, y), IM_COL32(168, 174, 184, 255), g.title);
  dl->PopClipRect();
  // the second line in the logo's colour, slashes either side
  const float y2 = y + big * 0.98f;
  Outlined(dl, f, small, ImVec2(cx - sw / 2, y2), g.accent, g.title2, 3 * s);
  for (int side = -1; side <= 1; side += 2) {
    const float x0 = cx + side * (sw / 2 + 14 * s);
    for (int k = 0; k < 3; k++) {
      const float x = x0 + side * k * 12 * s;
      dl->AddQuadFilled(ImVec2(x, y2 + small * 0.85f), ImVec2(x + side * 6 * s, y2 + small * 0.85f),
                        ImVec2(x + side * 14 * s, y2 + small * 0.2f), ImVec2(x + side * 8 * s, y2 + small * 0.2f),
                        Alpha(g.accent, 1.0f - k * 0.3f));
    }
  }
  return big * 0.98f + small * 1.05f;
}

ImU32 Selected(ImDrawList* dl, ImVec2 a, ImVec2 b, float s) {
  const Palette& g = Game();
  if (g.scene == Scene::kFloodlights) {
    // torn white tape, black text
    Rng r{uint32_t(a.y)};
    const float j = 6 * s;
    dl->AddRectFilled(ImVec2(a.x + j, a.y), ImVec2(b.x - j, b.y), IM_COL32(236, 233, 224, 250));
    for (int side = 0; side < 2; side++) {  // ragged ends
      const float x = side ? b.x - j : a.x + j, dir = side ? 1.0f : -1.0f;
      const int teeth = 5;
      for (int k = 0; k < teeth; k++) {
        const float y0 = a.y + (b.y - a.y) * k / teeth, y1 = a.y + (b.y - a.y) * (k + 1) / teeth;
        dl->AddTriangleFilled(ImVec2(x, y0), ImVec2(x, y1), ImVec2(x + dir * j * (0.4f + r.F()), (y0 + y1) / 2),
                              IM_COL32(236, 233, 224, 250));
      }
    }
    // creases in the tape
    for (int k = 0; k < 3; k++) {
      const float x = a.x + (b.x - a.x) * (0.2f + r.F() * 0.6f);
      dl->AddLine(ImVec2(x, a.y + 2 * s), ImVec2(x + 8 * s, b.y - 2 * s), IM_COL32(150, 145, 135, 90), 1.0f);
    }
    // an orange star in front
    const ImVec2 c(a.x - 10 * s, (a.y + b.y) / 2);
    ImVec2 pts[10];
    for (int k = 0; k < 10; k++) {
      const float rad = (k % 2 ? 3.2f : 8.0f) * s, ang = -kPi / 2 + k * kPi / 5;
      pts[k] = ImVec2(c.x + std::cos(ang) * rad, c.y + std::sin(ang) * rad);
    }
    for (int k = 0; k < 10; k += 2) dl->AddTriangleFilled(c, pts[k], pts[(k + 1) % 10], IM_COL32(255, 140, 30, 255));
    for (int k = 1; k < 10; k += 2) dl->AddTriangleFilled(c, pts[k], pts[(k + 1) % 10], IM_COL32(255, 140, 30, 255));
    return IM_COL32(12, 10, 10, 255);
  }
  // locker room: a smear of the logo green with a bright notch
  dl->AddRectFilledMultiColor(a, b, Alpha(g.accent, 0.55f), Alpha(g.accent, 0.08f), Alpha(g.accent, 0.08f), Alpha(g.accent, 0.55f));
  dl->AddQuadFilled(ImVec2(a.x, a.y), ImVec2(a.x + 8 * s, a.y), ImVec2(a.x + 4 * s, b.y), ImVec2(a.x - 4 * s, b.y), g.accent);
  return g.text;
}

void Spatter(ImDrawList* dl, ImVec2 c, float r, uint32_t seed, ImU32 col) {
  Rng g{seed};
  dl->AddCircleFilled(c, r, col, 24);
  for (int i = 0; i < 5; i++) {  // lumps on the blob
    const float a = g.F() * 2 * kPi, d = r * (0.5f + g.F() * 0.5f);
    dl->AddCircleFilled(ImVec2(c.x + std::cos(a) * d, c.y + std::sin(a) * d), r * (0.35f + g.F() * 0.35f), col, 16);
  }
  for (int i = 0; i < 16; i++) {  // drops flung out
    const float a = g.F() * 2 * kPi, d = r * (1.4f + g.F() * 2.6f);
    dl->AddCircleFilled(ImVec2(c.x + std::cos(a) * d, c.y + std::sin(a) * d), r * (0.06f + g.F() * 0.16f), col, 10);
  }
  // a drip
  const float dx = c.x + (g.F() - 0.5f) * r, len = r * (1.5f + g.F() * 2.0f);
  dl->AddRectFilled(ImVec2(dx - r * 0.07f, c.y), ImVec2(dx + r * 0.07f, c.y + len), col);
  dl->AddCircleFilled(ImVec2(dx, c.y + len), r * 0.13f, col, 10);
}

}  // namespace blitz::look
