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
  REX_SETTING(bool, spec_warning, true,
              "At startup, warn when the PC is likely too slow even at 30 fps (no real graphics "
              "chip, a card with under 2 GB, under 6 GB of RAM or under 4 CPU threads). Below "
              "the 60 fps bar the first start asks once whether to play at 30 fps");
  REX_SETTING(bool, spec_warning_test, false,
              "Show the first-start 30 fps question even on a PC that meets the bar or "
              "already answered it (for testing)",
              rex::kNoSave);
  REX_SETTING(bool, spec_warning_test_slow, false,
              "Show the 'may be too slow' warning even on a PC above the 30 fps bar (for testing)",
              rex::kNoSave);
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
  // Unused (the sharpening row was removed; Upscaling's AMD CAS sharpens). Still
  // registered so config files that saved it keep loading without a warning.
  REX_SETTING(f64, sharpen, 0.0, "Unused, kept so older config files load", rex::Range{0.0, 1.0});
  REX_SETTING(bool, smart_wait, true,
              "The waits for the GPU sleep after a short spin instead of keeping a core busy "
              "(false: the console's busy waits)");
  REX_SETTING(i32, wait_spin_us, 100, "Smart wait: spin this long before sleeping, in microseconds",
              rex::Range{0, 5000});
  REX_SETTING(i32, wait_sleep_us, 100, "Smart wait: length of each sleep, in microseconds",
              rex::Range{20, 1000});
  REX_SETTING(bool, audio_barrier_fix, true,
              "Fix for sound stopping for good: the game's audio threads meet at a barrier that "
              "can lock up when Windows pauses one of them at the wrong moment (false: the "
              "console's barrier)");
  REX_SETTING(bool, frame_diag, false,
              "Diagnostic: log the frame task once a second (passes, renders skipped, game "
              "steps, presents, game mode)",
              rex::kNoSave);
  REX_SETTING(std::string, frame_mode, "60",
              "Frame rate: 60 (the game's own) or 30 (two game steps per drawn frame, locked to "
              "every other vblank: full game speed on PCs that can't hold 60). Not set: the "
              "first-start hardware check asks on a PC below the bar",
              rex::Allowed("60", "30"));
};

BlitzSettings& Settings();

inline bool FrameMode30() { return Settings().frame_mode.Get() == "30"; }

// Width / height of the 3D view for blitz.aspect_ratio. 21:9 is 64:27 (2560x1080; 3440x1440
// is within 1%).
inline double AspectRatio() {
  const std::string v = Settings().aspect_ratio.Get();
  if (v == "21:9") return 64.0 / 27.0;
  if (v == "32:9") return 32.0 / 9.0;
  return 16.0 / 9.0;
}

}  // namespace blitz
