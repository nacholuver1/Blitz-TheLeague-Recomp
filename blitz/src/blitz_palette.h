// blitz - ReXGlue Recompiled Project
//
// Blitz: The League's menu look (see blitz_look.h): its own menu's grimy
// locker room seen through a camcorder, chrome BLITZ over the logo's toxic green.
// Read only by blitz_look.cpp.

#pragma once

#include "blitz_look.h"

namespace blitz::look {

inline const Palette kGamePalette{Scene::kLockerRoom,
                                  IM_COL32(140, 230, 60, 255),   // accent: the logo's green
                                  IM_COL32(248, 252, 246, 255),  // text
                                  IM_COL32(170, 196, 190, 255),  // dim
                                  IM_COL32(120, 146, 140, 255),  // hint
                                  "BLITZ",
                                  "THE LEAGUE"};

}  // namespace blitz::look
