// blitz - ReXGlue Recompiled Project
//
// Blitz-specific settings (scope "blitz"), stored in blitz.toml with the SDK ones.

#pragma once

#include <filesystem>
#include <string>

#include <rex/setting.h>

namespace blitz {

class BlitzSettings : public rex::SettingOwner {
 public:
  BlitzSettings() : SettingOwner("blitz") {}

  REX_SETTING(bool, show_fps, false, "Show the FPS counter (debug overlay) at startup");
  REX_SETTING(bool, launcher, true, "Show the launcher menu before the game starts");
  REX_SETTING(bool, check_updates, true,
              "The launcher asks GitHub for a newer release and offers to install it");
  REX_SETTING(std::filesystem::path, iso, "",
              "Disc image to set up from on first run (--blitz.iso=path); unpacked into a "
              "'game' folder next to the exe",
              rex::kNoSave);
  REX_SETTING(std::filesystem::path, game, "",
              "Your copy of the game, a disc image or an extracted folder (--blitz.game=path), "
              "started without the launcher menu; for launchers and shortcuts",
              rex::kNoSave);
  REX_SETTING(std::string, aspect_ratio, "16:9",
              "Screen shape the 3D view is drawn for: 16:9 (the game's own), 21:9 or 32:9 for "
              "ultrawide screens (the picture is stretched to the window)",
              rex::Allowed("16:9", "21:9", "32:9"), rex::kRestart);
  REX_SETTING(f64, sharpen, 0.0, "Output sharpening strength, 0 is off", rex::Range{0.0, 1.0});
  REX_SETTING(bool, smart_wait, true,
              "The waits for the GPU sleep after a short spin instead of keeping a core busy "
              "(false: the console's busy waits)");
  REX_SETTING(i32, wait_spin_us, 100, "Smart wait: spin this long before sleeping, in microseconds",
              rex::Range{0, 5000});
  REX_SETTING(i32, wait_sleep_us, 100, "Smart wait: length of each sleep, in microseconds",
              rex::Range{20, 1000});
};

BlitzSettings& Settings();

// Width / height of the 3D view for blitz.aspect_ratio. 21:9 is 64:27 (2560x1080; 3440x1440
// is within 1%).
inline double AspectRatio() {
  const std::string v = Settings().aspect_ratio.Get();
  if (v == "21:9") return 64.0 / 27.0;
  if (v == "32:9") return 32.0 / 9.0;
  return 16.0 / 9.0;
}

}  // namespace blitz
