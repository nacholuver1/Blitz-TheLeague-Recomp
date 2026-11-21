// blitz - ReXGlue Recompiled Project
//
// First-run setup. The exe ships without any game files; the player points it
// at their own dumped disc image (.iso), which is unpacked into a "game" folder
// next to the exe, or at a folder they already extracted. Either way the
// folder's default.xex must be the exact one this build was recompiled from.

#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

namespace rex::ui {
class IDialog;
class IUISystem;
}  // namespace rex::ui

namespace blitz {

// Empty when `dir` holds the default.xex this build was made from, otherwise a
// sentence saying what is wrong.
std::string CheckGameFolder(const std::filesystem::path& dir);

// Where an unpacked disc image goes: <exe folder>/game.
std::filesystem::path BundledGameFolder();

enum class SetupChoice : int { kPending = 0, kDone = 1, kQuit = 2 };

// Setup dialog. On kDone, `game_root` holds the folder to play from.
// `iso` (from --blitz.iso) starts unpacking right away; `problem` explains why
// setup is showing (for example a folder that holds a different version).
std::unique_ptr<rex::ui::IDialog> MakeSetup(std::atomic<SetupChoice>& choice,
                                            std::filesystem::path& game_root,
                                            std::filesystem::path iso, std::string problem);

}  // namespace blitz
