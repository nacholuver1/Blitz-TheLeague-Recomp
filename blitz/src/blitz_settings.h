// blitz - ReXGlue Recompiled Project
//
// Blitz-specific settings (scope "blitz"), stored in blitz.toml with the SDK ones.

#pragma once

#include <filesystem>

#include <rex/setting.h>

namespace blitz {

class BlitzSettings : public rex::SettingOwner {
 public:
  BlitzSettings() : SettingOwner("blitz") {}

  REX_SETTING(bool, show_fps, false, "Show the FPS counter (debug overlay) at startup");
  REX_SETTING(bool, launcher, true, "Show the launcher menu before the game starts");
  REX_SETTING(std::filesystem::path, iso, "",
              "Disc image to set up from on first run (--blitz.iso=path); unpacked into a "
              "'game' folder next to the exe",
              rex::kNoSave);
  REX_SETTING(f64, sharpen, 0.0, "Output sharpening strength, 0 is off", rex::Range{0.0, 1.0});
};

BlitzSettings& Settings();

}  // namespace blitz
