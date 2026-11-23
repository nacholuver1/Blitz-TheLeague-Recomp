// blitz - ReXGlue Recompiled Project
//
// Updates from the project's GitHub releases. The launcher asks for the latest
// release; a newer one is offered in a popup, and Update now downloads this
// platform's package (-win64.zip / -linux-x64.tar.gz), checks it against the
// SHA-256 GitHub lists for it, puts the new files over the old ones (old ones
// kept as <name>.old, put back on any failure) and starts the new version.
// Settings (*.toml), saves (user\), the game (game\), cache\ and logs\ are not
// in the package and are never touched. The repository and version come from
// CMakeLists.txt (BLITZ_REPO, project VERSION).

#pragma once

#include <filesystem>
#include <span>
#include <string>

namespace blitz::updater {

enum class State { kIdle, kChecking, kUpToDate, kAvailable, kDownloading, kInstalling, kReady, kFailed };

const char* Version();  // this build, e.g. "1.1.0"

// At startup, before anything renames files: remembers the exe and the command
// line for the relaunch, and deletes what the last update left behind.
void Init(const std::filesystem::path& exe, std::span<const std::string> args);

void StartCheck();    // ask GitHub in the background (BLITZ_UPDATE_URL overrides the address, for tests)
void StartInstall();  // download and install the release found, in the background
State GetState();
bool Busy();          // downloading or installing: the game must not start or quit now
// At exit: a swap in progress finishes (a few seconds at most); a download still running
// will not start one.
void WaitForInstall();
std::string Latest();   // the newer release's version once found
std::string Message();  // a failure's reason, else the release's name
int Progress();         // download percent
bool Relaunch();        // once kReady: start the new version with the same command line

}  // namespace blitz::updater
