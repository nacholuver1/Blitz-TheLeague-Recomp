// blitz - ReXGlue Recompiled Project
//
// Options menu / launcher. See blitz_menu.h (credits darkuni's MIT-licensed menu).

#include "blitz_menu.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>

#include <rex/app/app.h>
#include <rex/logging/log_category.h>
#include <rex/path_settings.h>
#include <rex/settings_registry.h>
#include <rex/ui/ui_system.h>

#include "blitz_settings.h"
#include "blitz_ui.h"


namespace blitz {
namespace {

using namespace blitz::ui;


// ------------------------------------------------------------------ settings helpers
rex::SettingsRegistry& Reg() { return rex::GetSettingsRegistry(); }

std::string GetText(const char* name) {
  if (auto s = Reg().Find(name); s && *s) return (*s)->GetText();
  return {};
}
bool GetBool(const char* name) { return GetText(name) == "true"; }
int GetInt(const char* name) { return std::atoi(GetText(name).c_str()); }
double GetF64(const char* name) { return std::atof(GetText(name).c_str()); }
bool RestartPending(const char* name) {
  if (auto s = Reg().Find(name); s && *s) return (*s)->IsRestartPending();
  return false;
}
void Set(const char* name, const std::string& value) {
  if (auto r = Reg().SetText(name, value); !r) {
    REXLOG_WARN("blitz menu: could not set {} = {}", name, value);
  }
}
void SetBool(const char* name, bool v) { Set(name, v ? "true" : "false"); }
void SetInt(const char* name, int v) { Set(name, std::to_string(v)); }
void SetF64(const char* name, double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f", v);
  Set(name, buf);
}

void Save() {
  const auto path = REX_PATHS().config_file.Get();
  if (auto r = Reg().Save(path); !r) {
    REXLOG_WARN("blitz menu: could not save settings");
  }
}

// ------------------------------------------------------------------ rows
enum Row {
  kStartGame,
  kDisplayMode,
  kRenderRes,
  kAntiAlias,
  kSharpen,
  kFiltering,
  kVsync,
  kAspect,
  kVolume,
  kMute,
  kMuteUnfocused,
  kAudioBuffer,
  kFpsCounter,
  kLauncherAtStart,
  kCloseMenu,
  kQuit,
};

// kSharpen is left out until the sharpening pass exists (blitz.sharpen is saved
// but nothing reads it yet).
const std::vector<Row> kLauncherRows = {
    kStartGame, kDisplayMode, kRenderRes,  kAntiAlias,  kFiltering, kVsync,
    kAspect,    kVolume,      kMute,       kMuteUnfocused, kAudioBuffer, kFpsCounter,
    kLauncherAtStart, kQuit};
const std::vector<Row> kInGameRows = {
    kDisplayMode, kRenderRes,  kAntiAlias,   kFiltering,       kVsync,
    kAspect,      kVolume,     kMute,        kMuteUnfocused, kAudioBuffer,  kFpsCounter,
    kLauncherAtStart, kCloseMenu, kQuit};

constexpr const char* kRestartSettings[] = {"gpu.resolution_scale_x", "gpu.xenos.swap_post_effect",
                                            "apu.max_queued_frames"};

constexpr int kMaxScale = 4;
const char* const kRenderLabels[] = {"1x  1280x720 (original)", "2x  2560x1440", "3x  3840x2160",
                                     "4x  5120x2880"};

const char* const kAAValues[] = {"none", "fxaa", "fxaa_extreme"};
const char* const kAALabels[] = {"Off", "FXAA", "FXAA (extreme)"};
constexpr int kAACount = 3;

// gpu.anisotropic_override: -1 game, 0 off, 1..5 = 1x..16x
const int kAnisoValues[] = {-1, 0, 2, 3, 4, 5};
const char* const kAnisoLabels[] = {"Game default", "Off", "2x", "4x", "8x", "16x"};
constexpr int kAnisoCount = 6;

const int kAudioFrames[] = {8, 16, 32, 64};
const char* const kAudioLabels[] = {"Small (8)", "Medium (16)", "Large (32)", "Max (64)"};
constexpr int kAudioCount = 4;

int AAIndex() {
  const std::string v = GetText("gpu.xenos.swap_post_effect");
  for (int i = 0; i < kAACount; i++)
    if (v == kAAValues[i]) return i;
  return 0;
}
int AnisoIndex() {
  const int v = GetInt("gpu.anisotropic_override");
  for (int i = 0; i < kAnisoCount; i++)
    if (kAnisoValues[i] == v) return i;
  return 0;
}
int AudioIndex() {
  const int v = GetInt("apu.max_queued_frames");
  int best = 0;
  for (int i = 0; i < kAudioCount; i++)
    if (kAudioFrames[i] <= v) best = i;
  return best;
}
int ScaleIndex() { return std::clamp(GetInt("gpu.resolution_scale_x"), 1, kMaxScale) - 1; }

const char* Label(Row row) {
  switch (row) {
    case kStartGame: return "START GAME";
    case kDisplayMode: return "Display mode";
    case kRenderRes: return "Render resolution";
    case kAntiAlias: return "Anti-aliasing";
    case kSharpen: return "Sharpening";
    case kFiltering: return "Texture filtering";
    case kVsync: return "VSync";
    case kAspect: return "Aspect ratio";
    case kVolume: return "Volume";
    case kMute: return "Mute";
    case kMuteUnfocused: return "Mute in background";
    case kAudioBuffer: return "Audio buffer";
    case kFpsCounter: return "FPS counter";
    case kLauncherAtStart: return "Launcher at startup";
    case kCloseMenu: return "Close menu";
    case kQuit: return "Quit";
  }
  return "";
}

const char* Note(Row row) {
  switch (row) {
    case kStartGame: return "Start Blitz: The League with these settings.";
    case kDisplayMode: return "Borderless fullscreen or a window.";
    case kRenderRes: return "Internal resolution; above your screen it supersamples.";
    case kAntiAlias: return "FXAA smooths jagged edges.";
    case kSharpen: return "Sharpens the final image. Pairs well with FXAA.";
    case kFiltering: return "Anisotropic filtering: sharper textures at an angle.";
    case kVsync: return "Wait for the monitor refresh. Off can tear.";
    case kAspect: return "Keep the game's shape with bars, or stretch to the window.";
    case kVolume: return "Master volume.";
    case kMute: return "Silence all game audio.";
    case kMuteUnfocused: return "Silence the game when its window is in the background.";
    case kAudioBuffer: return "Bigger fixes crackles, smaller has less delay.";
    case kFpsCounter: return "Show frame rate in the corner (also F3).";
    case kLauncherAtStart: return "Off starts the game directly. F1 / Esc / Guide still open this menu.";
    case kCloseMenu: return "Back to the game.";
    case kQuit: return nullptr;
  }
  return nullptr;
}

std::string Value(Row row) {
  switch (row) {
    case kDisplayMode: return GetBool("ui.fullscreen") ? "Fullscreen" : "Windowed";
    case kRenderRes: return kRenderLabels[ScaleIndex()];
    case kAntiAlias: return kAALabels[AAIndex()];
    case kSharpen: {
      const int pct = static_cast<int>(std::lround(GetF64("blitz.sharpen") * 100.0));
      return pct == 0 ? std::string("Off") : std::to_string(pct) + "%";
    }
    case kFiltering: return kAnisoLabels[AnisoIndex()];
    case kVsync: return GetBool("gpu.vsync") ? "On" : "Off";
    case kAspect: return GetBool("gpu.xenos.present_letterbox") ? "Keep shape" : "Stretch";
    case kVolume:
      return std::to_string(static_cast<int>(std::lround(GetF64("apu.gain") * 100.0))) + "%";
    case kMute: return GetBool("apu.mute") ? "On" : "Off";
    case kMuteUnfocused: return GetBool("apu.mute_unfocused") ? "On" : "Off";
    case kAudioBuffer: return kAudioLabels[AudioIndex()];
    case kFpsCounter: return GetBool("blitz.show_fps") ? "On" : "Off";
    case kLauncherAtStart: return GetBool("blitz.launcher") ? "On" : "Off";
    default: return {};
  }
}

bool IsOptionRow(Row row) { return row != kStartGame && row != kCloseMenu && row != kQuit; }

// Setting that decides the "applies after restart" marker for a row.
const char* RestartSetting(Row row) {
  switch (row) {
    case kRenderRes: return "gpu.resolution_scale_x";
    case kAntiAlias: return "gpu.xenos.swap_post_effect";
    case kAudioBuffer: return "apu.max_queued_frames";
    default: return nullptr;
  }
}

template <typename T>
int Wrap(int i, int dir, T count) {
  return (i + dir + static_cast<int>(count)) % static_cast<int>(count);
}

constexpr const char* kHint =
    "Up/Down select, Left/Right change, Enter/A choose.  * = after restart";

// ================================================================== menu
class BlitzMenu final : public rex::ui::IDialog {
 public:
  BlitzMenu(rex::ui::IUISystem& ui, std::atomic<LauncherChoice>* launcher)
      : ui_(ui), launcher_(launcher), rows_(launcher ? kLauncherRows : kInGameRows) {}

  bool NeedsContinuousPaint() const override { return true; }
  bool WantsExclusiveInput() const override { return true; }  // the game doesn't see the pad
  bool OnToggle() override { return false; }
  void OnShow() override {
    status_.clear();
    quit_armed_ = false;
    wait_release_ = true;  // swallow the press that opened the menu
  }
  void OnClose() override {}

  bool Draw(ImGuiIO& io) override {
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return true;
    bool keep_open = true;
    const int n = static_cast<int>(rows_.size());

    // ---- input
    int move = 0, activate = 0;
    const double now = ImGui::GetTime();
    const InputState in = ReadInput();
    if (wait_release_) {
      if (!in.up && !in.down && !in.left && !in.right && !in.accept && !in.back) {
        wait_release_ = false;
      }
    } else {
      if (up_.Update(in.up, now)) move = -1;
      if (down_.Update(in.down, now)) move = 1;
      if (left_.Update(in.left, now)) activate = -1;
      if (right_.Update(in.right, now)) activate = 2;  // 2 = change only, never "choose"
      if (accept_.Update(in.accept, now, false)) activate = 1;
      if (back_.Update(in.back, now, false) && !launcher_) keep_open = false;
    }
    if (move) {
      sel_ = Wrap(sel_, move, n);
      quit_armed_ = false;
    }

    // ---- layout
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##blitz_menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoNav);
    const float scale = std::max(0.6f, io.DisplaySize.y / 900.0f);
    Painter p{ImGui::GetWindowDrawList(), ImGui::GetFont(), 24.0f * scale};
    const float lh = p.size * 1.35f;
    const float label_w = p.TextWidth("Launcher at startup") + p.TextWidth("MMM");
    const float value_w = p.TextWidth("< 1x  1280x720 (original) >  *") + p.TextWidth("MM");
    const float hint_w = p.TextWidth(kHint, 0.75f) + 40 * scale;
    const float pw = std::max(label_w + value_w, hint_w) + 40 * scale;
    const float title_h = launcher_ ? lh * 2.6f : lh * 1.4f;
    const float ph = title_h + (static_cast<float>(n) + 3.4f) * lh;
    const float px = (io.DisplaySize.x - pw) / 2, py = (io.DisplaySize.y - ph) / 2;
    const float value_x = px + 20 * scale + label_w;

    if (launcher_) {
      // Nothing is running behind the launcher yet: draw a full backdrop.
      p.Rect(0, 0, io.DisplaySize.x, io.DisplaySize.y, kBackdrop);
    } else {
      p.Rect(0, 0, io.DisplaySize.x, io.DisplaySize.y, kShade);
    }
    p.Rect(px, py, pw, ph, kPanel);
    p.Rect(px, py, pw, 3 * scale, kAccent);
    if (launcher_) {
      p.Text(px + 20 * scale, py + 12 * scale, "BLITZ: THE LEAGUE", kAccent, 1.6f);
      p.Text(px + 20 * scale, py + 12 * scale + lh * 1.45f, "Native PC recompilation  -  launcher",
             kGrey, 0.8f);
    } else {
      p.Text(px + 20 * scale, py + 14 * scale, "BLITZ: THE LEAGUE  -  OPTIONS", kAccent);
    }

    float y = py + 14 * scale + title_h;
    for (int i = 0; i < n; i++) {
      const Row row = rows_[i];
      const float rx = px + 8 * scale, rw = pw - 16 * scale, ry = y - 4 * scale;
      if (ImGui::IsMouseHoveringRect(ImVec2(rx, ry), ImVec2(rx + rw, ry + lh)) &&
          (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
        if (sel_ != i) quit_armed_ = false;
        sel_ = i;
      }
      if (RowClicked(rx, ry, rw, lh, ImGuiMouseButton_Left)) { sel_ = i; activate = 1; }
      if (RowClicked(rx, ry, rw, lh, ImGuiMouseButton_Right)) { sel_ = i; activate = -1; }

      if (i == sel_) p.Rect(rx, ry, rw, lh, kHighlight);
      const ImU32 label_col = row == kStartGame ? kAccent : (i == sel_ ? kWhite : kGrey);
      p.Text(px + 20 * scale, y, Label(row), i == sel_ ? kWhite : label_col);
      if (IsOptionRow(row)) {
        std::string v = "< " + Value(row) + " >";
        if (const char* rs = RestartSetting(row); rs && RestartPending(rs)) v += "  *";
        p.Text(value_x, y, v.c_str(), kWhite);
      } else if (row == kQuit && i == sel_ && quit_armed_) {
        p.Text(value_x, y, "press again to quit", kAccent);
      }
      y += lh;
      if (row == kStartGame) y += lh * 0.35f;  // gap under Start game
    }

    // ---- act
    if (activate != 0) {
      const Row row = rows_[sel_];
      if (IsOptionRow(row)) {
        Change(row, activate > 0 ? 1 : -1);
      } else if (activate == 1) {
        switch (row) {
          case kStartGame:
            Save();
            launcher_->store(LauncherChoice::kStart);
            keep_open = false;
            break;
          case kCloseMenu: keep_open = false; break;
          case kQuit:
            if (quit_armed_ || launcher_) {
              if (launcher_) launcher_->store(LauncherChoice::kQuit);
              rex::app::RequestShutdown(0);
              keep_open = false;
            } else {
              quit_armed_ = true;
            }
            break;
          default: break;
        }
      }
    }

    const char* note = Note(rows_[sel_]);
    std::string bottom = !status_.empty() ? status_ : (note ? std::string(note) : std::string());
    if (bottom.empty() && rows_[sel_] == kQuit) bottom = launcher_ ? "Exit without playing." : "Exit the game.";
    p.Text(px + 20 * scale, y + lh * 0.4f, bottom.c_str(), status_.empty() ? kGrey : kAccent, 0.75f);
    const char* close_hint = launcher_ ? kHint : "F1 / Esc / B / Guide: close.   * = after restart";
    p.Text(px + 20 * scale, y + lh * 1.3f, close_hint, kGrey, 0.75f);
    ImGui::End();
    return keep_open;
  }

 private:
  void Change(Row row, int dir) {
    status_.clear();
    switch (row) {
      case kDisplayMode: SetBool("ui.fullscreen", !GetBool("ui.fullscreen")); break;
      case kRenderRes: {
        const int s = Wrap(ScaleIndex(), dir, kMaxScale) + 1;
        SetInt("gpu.resolution_scale_x", s);
        SetInt("gpu.resolution_scale_y", s);
        break;
      }
      case kAntiAlias:
        Set("gpu.xenos.swap_post_effect", kAAValues[Wrap(AAIndex(), dir, kAACount)]);
        break;
      case kSharpen: {
        int step = static_cast<int>(std::lround(GetF64("blitz.sharpen") * 10.0));
        step = Wrap(step, dir, 11);  // 0..10 -> 0%..100%
        SetF64("blitz.sharpen", step / 10.0);
        break;
      }
      case kFiltering:
        SetInt("gpu.anisotropic_override", kAnisoValues[Wrap(AnisoIndex(), dir, kAnisoCount)]);
        break;
      case kVsync: SetBool("gpu.vsync", !GetBool("gpu.vsync")); break;
      case kAspect:
        SetBool("gpu.xenos.present_letterbox", !GetBool("gpu.xenos.present_letterbox"));
        break;
      case kVolume: {
        int step = static_cast<int>(std::lround(GetF64("apu.gain") * 10.0));
        step = std::clamp(step + dir, 0, 20);  // 0%..200%
        SetF64("apu.gain", step / 10.0);
        break;
      }
      case kMute: SetBool("apu.mute", !GetBool("apu.mute")); break;
      case kMuteUnfocused: SetBool("apu.mute_unfocused", !GetBool("apu.mute_unfocused")); break;
      case kAudioBuffer:
        SetInt("apu.max_queued_frames", kAudioFrames[Wrap(AudioIndex(), dir, kAudioCount)]);
        break;
      case kFpsCounter: {
        const bool on = !GetBool("blitz.show_fps");
        SetBool("blitz.show_fps", on);
        ui_.ShowOverlay("debug", on);
        break;
      }
      case kLauncherAtStart: SetBool("blitz.launcher", !GetBool("blitz.launcher")); break;
      default: return;
    }
    if (const char* rs = RestartSetting(row); rs && RestartPending(rs)) {
      status_ = launcher_ ? "Applies when you start the game (it relaunches once)."
                          : "Applies the next time you start the game.";
    }
    Save();
  }

  rex::ui::IUISystem& ui_;
  std::atomic<LauncherChoice>* launcher_;
  const std::vector<Row>& rows_;
  int sel_ = 0;
  bool quit_armed_ = false;
  bool wait_release_ = true;
  std::string status_;
  Repeater up_, down_, left_, right_, accept_, back_;
};

// ------------------------------------------------------------------ chord watcher
std::atomic<bool> g_watch_running{false};
std::thread g_watch_thread;

}  // namespace

std::unique_ptr<rex::ui::IDialog> MakeMenu(rex::ui::IUISystem& ui) {
  return std::make_unique<BlitzMenu>(ui, nullptr);
}

std::unique_ptr<rex::ui::IDialog> MakeLauncher(rex::ui::IUISystem& ui,
                                               std::atomic<LauncherChoice>& choice) {
  return std::make_unique<BlitzMenu>(ui, &choice);
}

bool AnyRestartPending() {
  for (const char* name : kRestartSettings) {
    if (RestartPending(name)) return true;
  }
  return RestartPending("gpu.resolution_scale_y");
}

void StartMenuChordWatcher(rex::ui::IUISystem& ui) {
  if (g_watch_running.exchange(true)) return;
  g_watch_thread = std::thread([&ui] {
    bool was_down = false;
    while (g_watch_running.load()) {
      const bool down = MenuChordDown();
      if (down && !was_down) {
        ui.PostTask([&ui] { ui.ToggleOverlay(kMenuOverlayName); });
      }
      was_down = down;
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
  });
}

void StopMenuChordWatcher() {
  if (!g_watch_running.exchange(false)) return;
  if (g_watch_thread.joinable()) g_watch_thread.join();
}

}  // namespace blitz
