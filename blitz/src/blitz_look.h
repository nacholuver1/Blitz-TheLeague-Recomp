// blitz - ReXGlue Recompiled Project
//
// The menus' look, after the games' own menus: Blitz: The League's grimy
// locker room seen through a camcorder, Blitz: The League II's floodlit
// chain-link fence with flying embers and a torn-tape selection. Everything is
// drawn (no game art); the lettering uses fonts the system already has (Impact
// and Bahnschrift on Windows, DejaVu / Liberation condensed on Linux) and falls
// back to the SDK's font. Each game picks its scene and colours in
// blitz_palette.h; this file and blitz_look.cpp are the same in both projects.

#pragma once

#include <cstdint>

#include <imgui.h>

namespace blitz::look {

enum class Scene { kLockerRoom, kFloodlights };

struct Palette {
  Scene scene;
  ImU32 accent;       // the logo's colour: rows to start, title second line
  ImU32 text;         // selected row
  ImU32 dim;          // other rows
  ImU32 hint;         // notes and key hints
  const char* title;  // big first line, e.g. "BLITZ"
  const char* title2; // second line, e.g. "THE LEAGUE II"
};

const Palette& Game();  // blitz_palette.h

ImFont* Heavy();  // titles (Impact)
ImFont* Body();   // rows (Bahnschrift)
void EnsureFonts();  // once, from the first Draw (ImGui 1.92 takes fonts at any time)

// full-screen scene behind the launcher; time animates embers, flicker and REC
void Backdrop(ImDrawList* dl, ImVec2 size, float time, float s);
// the panel the rows sit on
void Panel(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float s);
// BLITZ / THE LEAGUE (II), centred on cx from y; returns its height
float Title(ImDrawList* dl, float cx, float y, float s, float scale = 1.0f);
// the selected row's mark; returns the colour its text is drawn in
ImU32 Selected(ImDrawList* dl, ImVec2 a, ImVec2 b, float s);
// a blood spatter: a blob, satellite drops and a drip
void Spatter(ImDrawList* dl, ImVec2 c, float r, uint32_t seed, ImU32 col);
// text with a black outline
void Outlined(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const char* text, float px);

}  // namespace blitz::look
