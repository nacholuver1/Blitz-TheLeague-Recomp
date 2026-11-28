// blitz - ReXGlue Recompiled Project
//
// Start-up hardware check. Blitz emulates the Xbox 360's GPU (EDRAM, resolves)
// on the PC's graphics card, so integrated graphics, small graphics cards, little
// RAM or few CPU threads run it below full speed. Players who start it on such a
// PC see one warning screen (with "Don't show again") before the launcher or the
// game, on every start path, RecompLauncher included.

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace rex::ui {
class IDialog;
}  // namespace rex::ui

namespace blitz {

struct SpecReport {
  std::string gpu;                   // name of the GPU the game will likely use
  std::vector<std::string> reasons;  // empty: nothing below the bar
};

// Reads the GPU (through the Vulkan loader), RAM and CPU threads, logs one line
// with what it found, and lists each reason the PC is below the bar.
SpecReport CheckSpecs();

enum class SpecChoice : int { kPending = 0, kPlay = 1, kQuit = 2 };

// Warning screen: Play anyway / Don't show again (saves blitz.spec_warning =
// false, then plays) / Quit. With ask_frame_rate (first start, blitz.frame_mode
// never set): Play at 30 fps (recommended) / Play at 60 fps / Quit; either play
// choice saves blitz.frame_mode and turns the warning off.
std::unique_ptr<rex::ui::IDialog> MakeSpecWarning(std::atomic<SpecChoice>& choice,
                                                  SpecReport report, bool ask_frame_rate);

}  // namespace blitz
