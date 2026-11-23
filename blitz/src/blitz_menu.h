// blitz - ReXGlue Recompiled Project
//
// Options menu, used two ways:
//  - Launcher: shown at startup before the game loads, with a "Start game" row.
//    Settings that only apply at startup take effect by relaunching the process.
//  - In game: F1, Esc, or the controller's Guide button (or LB + RB + Back).
//
// Layout and behaviour adapted from darkuni's recomp menus
// (https://github.com/darkuni/outrun-arcade-recomp, MIT License, Copyright (c)
// 2026 darkuni): a centred panel of "Label  < value >" rows, Up/Down to select,
// Left/Right/Enter to change, a note line for the selected row, saved to the
// config file on every change. Ported to the ReXGlue 0.11 overlay API and
// extended with a launcher mode, controller navigation and Blitz's options.

#pragma once

#include <atomic>
#include <memory>

namespace rex::ui {
class IDialog;
class IUISystem;
}  // namespace rex::ui

namespace blitz {

inline constexpr const char* kMenuOverlayName = "blitz_menu";

// kUpdated: an update is in place; the new version starts and this one quits.
enum class LauncherChoice : int { kPending = 0, kStart = 1, kQuit = 2, kUpdated = 3 };

// In-game overlay factory for IUISystem::AddOverlays.
std::unique_ptr<rex::ui::IDialog> MakeMenu(rex::ui::IUISystem& ui);

// Startup launcher dialog. Writes the player's choice to `choice`.
std::unique_ptr<rex::ui::IDialog> MakeLauncher(rex::ui::IUISystem& ui,
                                               std::atomic<LauncherChoice>& choice);

// True when a setting changed this session only applies after a restart.
bool AnyRestartPending();

// Polls the controller for Guide (or LB + RB + Back) on a background thread
// and toggles the in-game menu through the UI thread.
void StartMenuChordWatcher(rex::ui::IUISystem& ui);
void StopMenuChordWatcher();

}  // namespace blitz
