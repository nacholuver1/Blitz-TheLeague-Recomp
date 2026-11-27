// blitz - ReXGlue Recompiled Project

#include <rex/app/title_app.h>
#include <rex/logging/log_category.h>
#include <rex/settings_registry.h>

#include <rex/input_keys.h>
#include <rex/ui/ui_system.h>

#include "blitz_crash_dump.h"
#include "blitz_fp_guard.h"
#include "blitz_menu.h"
#include "blitz_settings.h"
#include "blitz_setup.h"
#include "blitz_sysinfo.h"
#include "blitz_updater.h"
#include "generated/default/blitz_init.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include <rex/path_settings.h>
#include <rex/platform/file.h>
#include <rex/platform/process.h>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace blitz {
BlitzSettings& Settings() {
  static BlitzSettings settings;
  return settings;
}
}  // namespace blitz

namespace {

const rex::ui::OverlayConfig kOverlays[] = {
    {blitz::kMenuOverlayName, {rex::VirtualKey::kF1}, blitz::MakeMenu},
};

// Sets a setting at the title layer, so a config file or --name=value still wins,
// and logs the value that ends up in effect.
// Puts a setting back to its default when the config file holds `value` (a
// choice the menus no longer offer). Values from the command line are kept.
void ResetSavedChoice(std::string_view name, std::string_view value) {
  auto& registry = rex::GetSettingsRegistry();
  auto s = registry.Find(name);
  if (!s || !*s) {
    REXLOG_WARN("blitz: {} not found, could not reset it", name);
    return;
  }
  if ((*s)->GetLayer() != rex::SettingLayer::kFile || (*s)->GetText() != value) return;
  if (auto r = registry.Reset(name); !r) {
    REXLOG_WARN("blitz: could not reset {}", name);
    return;
  }
  REXLOG_INFO("blitz: {} was {} in the config file; reset to {}", name, value, (*s)->GetText());
}

void DefaultSetting(std::string_view name, std::string_view value) {
  auto& registry = rex::GetSettingsRegistry();
  if (auto r = registry.SetText(rex::SettingLayer::kTitle, name, value); !r) {
    REXLOG_WARN("blitz: could not default {} to {}", name, value);
  }
  if (auto s = registry.Find(name); s && *s) {
    REXLOG_INFO("blitz: {} = {}", name, (*s)->GetText());
  }
}

}  // namespace

class BlitzApp : public rex::app::TitleApp {
 public:
  BlitzApp() : TitleApp("blitz", kPPCImageConfig) {}

  // Registers the blitz.* settings before the config file and command line load.
  [[nodiscard]] rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    // Keep compiled shaders between runs (the SDK keeps none by default). With
    // synchronous compilation this removes the first-time hitches on later runs.
    DefaultSetting("app.cache_root", "{app.root}/cache");
    // Hide the mouse cursor after 2 s without movement (the SDK default never
    // hides it, so it sat over the game). Moving the mouse brings it back.
    DefaultSetting("ui.cursor_timeout", "2");
    // Only user 0 has the signed-in profile. With a second (idle) pad in slot 0 the
    // player's pad sat in slot 1 and the game said the profile couldn't be saved
    // (player report). The first pad to press a button becomes user 0 instead.
    // Startup-only, so it must be set here, before the config file loads.
    DefaultSetting("hid.first_press_is_user0", "true");
    // The SDK writes app.root into blitz.toml. Lock it to the exe's folder so
    // a moved or renamed game folder keeps its saves/cache/logs with it.
    if (auto r = settings.SetText(rex::SettingLayer::kLock, "app.root",
                                  rex::platform::GetExecutableFolder().generic_string());
        !r) {
      REXLOG_WARN("blitz: could not lock app.root to the exe folder");
    }
    return settings.Register(blitz::Settings());
  }

  // Runs before LaunchTitle. The vectored handler is process-wide, so it covers
  // every guest thread the title starts later.
  [[nodiscard]] rex::Result<void> OnInit() override {
    fp_guard_ = blitz::InstallFpGuard();
    // The SDK crash reporter is installed before OnInit; chain in front of it.
    blitz::InstallCrashDump();

    // Menu rows removed: Mute (Volume 0 and "Mute in background" cover it),
    // VSync (Off only tears, and with the SDK's guest pacing it also runs the
    // game's vblank timer at 1000 Hz) and texture filtering "Off". A player who
    // picked one earlier has no row left to undo it, so a value saved in the
    // config file goes back to the default. The command line still sets them.
    ResetSavedChoice("apu.mute", "true");
    ResetSavedChoice("gpu.vsync", "false");
    ResetSavedChoice("gpu.anisotropic_override", "0");

    // GPU defaults. Set here rather than OnConfigure because the GPU driver
    // module registers these settings.
    //
    // Blitz renders some textures on the GPU and then processes them on the CPU
    // (create-a-team helmets, sideline players/cheerleaders). Without resolve
    // readback the CPU sees stale memory: scrambled helmets, invisible sideline.
    DefaultSetting("gpu.xenos.readback_resolve", "full");
    // With async compilation, draws whose pipeline isn't ready are skipped. The
    // helmet logo is baked once when a campaign loads, so a skipped bake leaves
    // the custom team's logo missing for the whole session.
    DefaultSetting("gpu.xenos.async_shader_compilation", "false");
    // Ultrawide: the 3D view is widened in blitz_hooks.cpp; present the picture at the same
    // shape (letterboxed to the window as usual). Not saved, so it always follows the setting.
    if (const std::string aspect = blitz::Settings().aspect_ratio.Get(); aspect == "21:9") {
      DefaultSetting("gpu.xenos.present_aspect_ratio", "64:27");
    } else if (aspect == "32:9") {
      DefaultSetting("gpu.xenos.present_aspect_ratio", "32:9");
    }

    // Find the game files without a command line: a "game" folder next to the
    // exe or in a parent folder. --title.game_root still wins. RunSetup asks the
    // player when nothing usable is found.
    DefaultGameRoot();

    // In-game options menu: F1 (the controller's Guide button is wired up once
    // the game is running, see StartInGameMenu).
    if (auto r = GetUI().AddOverlays(kOverlays); !r) {
      REXLOG_WARN("blitz: could not add the options menu");
    }
    if (blitz::Settings().show_fps.Get()) {
      GetUI().ShowOverlay("debug", true);
    }
    return TitleApp::OnInit();
  }

  // First run (or a missing / wrong game folder): asks for the player's own disc
  // image or extracted folder. Returns false when the player quits instead.
  bool RunSetup() {
    namespace fs = std::filesystem;
    auto& registry = rex::GetSettingsRegistry();
    std::string current;
    if (auto s = registry.Find("title.game_root"); s && *s) current = (*s)->GetText();
    std::string problem;
    std::error_code ec;
    // --blitz.game (what launchers pass): an extracted folder is played as it is;
    // a disc image is unpacked like --blitz.iso unless a game folder is set up
    // (launchers pass it on every start, and every folder that passes the check
    // holds the same verified build, so it is not unpacked again).
    fs::path iso = blitz::Settings().iso.Get();
    const fs::path game = blitz::Settings().game.Get();
    const bool game_folder = !game.empty() && fs::is_directory(game, ec);
    if (game_folder) {
      problem = blitz::CheckGameFolder(game);
      if (problem.empty()) return UseGameRoot(game, current);
      REXLOG_WARN("blitz: --blitz.game folder '{}': {}", game.generic_string(), problem);
    } else if (!game.empty()) {
      iso = game;
    }
    if (!game_folder && !current.empty()) {
      problem = blitz::CheckGameFolder(current);
      if (problem.empty()) return true;
      REXLOG_WARN("blitz: game folder '{}': {}", current, problem);
      // A folder that simply isn't there needs no explanation on the setup screen.
      if (!fs::is_regular_file(fs::path(current) / "default.xex", ec)) problem.clear();
    }

    std::atomic<blitz::SetupChoice> choice{blitz::SetupChoice::kPending};
    fs::path root;
    auto setup = blitz::MakeSetup(choice, root, iso, problem);
    {
      auto guard = GetUI().AddDialog(*setup);
      setup->OnShow();
      while (choice.load() == blitz::SetupChoice::kPending && RunFrame()) {
      }
    }
    if (choice.load() != blitz::SetupChoice::kDone) return false;
    return UseGameRoot(root, current);
  }

  // Plays from `root` and saves it as title.game_root (unless it already is).
  bool UseGameRoot(const std::filesystem::path& root, const std::string& current) {
    auto& registry = rex::GetSettingsRegistry();
    if (root.generic_string() == current) return true;
    REXLOG_INFO("blitz: game folder set to '{}'", root.generic_string());
    if (auto r = registry.SetText(rex::SettingLayer::kRuntime, "title.game_root",
                                  root.generic_string());
        !r) {
      REXLOG_WARN("blitz: could not set title.game_root");
      return false;
    }
    if (auto r = registry.Save(REX_PATHS().config_file.Get()); !r) {
      REXLOG_WARN("blitz: could not save the game folder to the config file");
    }
    return true;
  }

  // Shows the launcher until the player starts the game or quits.
  // Returns false when the game should not be launched in this process.
  bool RunLauncher() {
    // --blitz.game comes from a launcher or shortcut that already chose the settings
    if (!blitz::Settings().launcher.Get() || !blitz::Settings().game.Get().empty()) return true;
    // RecompLauncher installs updates itself: never check from inside it
    const char* rl = std::getenv("RECOMPLAUNCHER");
    const bool from_recomplauncher = rl && std::string_view(rl) == "1";
    if (blitz::Settings().check_updates.Get() && !from_recomplauncher) blitz::updater::StartCheck();
    std::atomic<blitz::LauncherChoice> choice{blitz::LauncherChoice::kPending};
    auto launcher = blitz::MakeLauncher(GetUI(), choice);
    {
      auto guard = GetUI().AddDialog(*launcher);
      launcher->OnShow();
      while (choice.load() == blitz::LauncherChoice::kPending && RunFrame()) {
      }
    }
    if (choice.load() == blitz::LauncherChoice::kUpdated) {
      // the new version is in place: start it and close this one
      if (!blitz::updater::Relaunch()) {
        rex::ShowMessageBox(rex::MessageType::kInfo,
                            "The update is installed. Start the game again to play the new version.");
      }
      return false;
    }
    if (choice.load() != blitz::LauncherChoice::kStart) return false;
    if (blitz::AnyRestartPending()) {
      // Resolution / AA / audio buffer are read when the GPU and audio start,
      // which already happened. Relaunch once, skipping the launcher.
      REXLOG_INFO("blitz: relaunching to apply startup-only settings");
#ifdef _WIN32
      SetEnvironmentVariableW(L"REX_BLITZ_LAUNCHER", L"false");
#else
      setenv("REX_BLITZ_LAUNCHER", "false", 1);
#endif
      RequestRestart();
      return false;
    }
    return true;
  }

  void StartInGameMenu() { blitz::StartMenuChordWatcher(GetUI()); }

 private:
  // Finds the game files. A configured title.game_root is kept only if it holds
  // default.xex; otherwise (unset, or stale after the folder was renamed or
  // moved) look for a "game" folder next to the exe and then in each parent
  // folder, so both <root>\BlitzPC\blitz.exe and the build folders under
  // <root>\blitz\out\... find <root>\game.
  static void DefaultGameRoot() {
    namespace fs = std::filesystem;
    auto& registry = rex::GetSettingsRegistry();
    auto setting = registry.Find("title.game_root");
    if (!setting || !*setting) return;

    const auto has_xex = [](const fs::path& dir) {
      std::error_code ec;
      return !dir.empty() && fs::is_regular_file(dir / "default.xex", ec);
    };
    const std::string configured = (*setting)->GetText();
    if (has_xex(fs::path(configured))) return;

    fs::path found;
    fs::path dir = rex::platform::GetExecutableFolder();
    for (int depth = 0; depth < 7 && !dir.empty(); ++depth) {
      if (has_xex(dir / "game")) {
        found = dir / "game";
        break;
      }
      const fs::path parent = dir.parent_path();
      if (parent == dir) break;
      dir = parent;
    }
    if (found.empty()) {
      REXLOG_WARN("blitz: no game folder found (configured: '{}'); put the game in a 'game' "
                  "folder next to BlitzPC or set title.game_root in blitz.toml",
                  configured);
      return;
    }
    if (configured.empty()) {
      DefaultSetting("title.game_root", found.generic_string());
      return;
    }
    // A stale path from blitz.toml: override it above the config-file layer.
    REXLOG_WARN("blitz: game folder '{}' not found, using '{}'", configured,
                found.generic_string());
    if (auto r = registry.SetText(rex::SettingLayer::kRuntime, "title.game_root",
                                  found.generic_string());
        !r) {
      REXLOG_WARN("blitz: could not override title.game_root");
    }
  }

 public:

  void OnShutdown() override {
    blitz::StopMenuChordWatcher();
    blitz::RemoveCrashDump();
    blitz::RemoveFpGuard(fp_guard_);
    fp_guard_ = nullptr;
  }

 private:
  void* fp_guard_ = nullptr;
};

namespace {

int BlitzMain(BlitzApp& app, const rex::CLIArgList& args) {
  // before anything else: what the last update left is deleted, and the exe's
  // path is kept for an update's relaunch (on Linux the running file is renamed)
  blitz::updater::Init(rex::platform::GetExecutablePath(), args.GetAll());
  if (app.Init(args)) {
    // system info after the launcher, so the log shows the options the game starts with
    if (app.RunSetup() && app.RunLauncher() && (blitz::LogSystemInfo(), app.LaunchTitle())) {
      app.StartInGameMenu();
      app.Run();
    }
  }
  // the launcher's window was closed while an update replaced files: let it finish (a download is
  // simply dropped, an interrupted swap would leave a mix of versions)
  blitz::updater::WaitForInstall();
  return app.Shutdown();
}

}  // namespace

REX_TITLE_APP_MAIN(BlitzApp, BlitzMain)
