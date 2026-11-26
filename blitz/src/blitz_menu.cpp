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

#include "blitz_look.h"
#include "blitz_settings.h"
#include "blitz_ui.h"
#include "blitz_updater.h"


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
  kUpdates,
  kDisplayMode,
  kRenderRes,
  kAntiAlias,
  kUpscaling,
  kSharpen,
  kFiltering,
  kVsync,
  kAspect,
  kScreenShape,
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
    kStartGame, kUpdates, kDisplayMode, kRenderRes,  kAntiAlias,  kUpscaling, kFiltering, kVsync,
    kAspect,    kScreenShape, kVolume,  kMute,       kMuteUnfocused, kAudioBuffer, kFpsCounter,
    kLauncherAtStart, kQuit};
const std::vector<Row> kInGameRows = {
    kDisplayMode, kRenderRes,  kAntiAlias,   kUpscaling, kFiltering,       kVsync,
    kAspect,      kScreenShape, kVolume,     kMute,        kMuteUnfocused, kAudioBuffer,  kFpsCounter,
    kLauncherAtStart, kCloseMenu, kQuit};

constexpr const char* kRestartSettings[] = {"gpu.resolution_scale_x", "gpu.xenos.swap_post_effect",
                                            "blitz.aspect_ratio",
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

// gpu.xenos.present_effect: how the picture is scaled to the window.
const char* const kUpscaleValues[] = {"bilinear", "fsr", "cas"};
const char* const kUpscaleLabels[] = {"Off", "AMD FSR 1", "AMD CAS"};
constexpr int kUpscaleCount = 3;

const char* const kShapeValues[] = {"16:9", "21:9", "32:9"};
const char* const kShapeLabels[] = {"16:9 (original)", "21:9 ultrawide", "32:9 super ultrawide"};
constexpr int kShapeCount = 3;

int UpscaleIndex() {
  const std::string v = GetText("gpu.xenos.present_effect");
  for (int i = 0; i < kUpscaleCount; i++)
    if (v == kUpscaleValues[i]) return i;
  return 0;
}
int ShapeIndex() {
  const std::string v = GetText("blitz.aspect_ratio");
  for (int i = 0; i < kShapeCount; i++)
    if (v == kShapeValues[i]) return i;
  return 0;
}
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
    case kUpdates: return "Updates";
    case kDisplayMode: return "Display mode";
    case kRenderRes: return "Render resolution";
    case kAntiAlias: return "Anti-aliasing";
    case kUpscaling: return "Upscaling";
    case kScreenShape: return "Screen shape";
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
    case kUpdates: {
      static std::string note;  // outlives this frame's call
      switch (updater::GetState()) {
        case updater::State::kAvailable:
          note = "Enter: download " + updater::Latest() + " and restart. Settings, saves and the game stay.";
          break;
        case updater::State::kDownloading:
        case updater::State::kInstalling: note = "Updating: the launcher restarts when it is done."; break;
        case updater::State::kFailed: note = updater::Message() + ". Enter to try again."; break;
        default:
          note = GetBool("blitz.check_updates") ? "Checks GitHub for a newer release at startup. Enter checks now."
                                                : "Off: no update checks. Enter checks once now.";
      }
      return note.c_str();
    }
    case kStartGame: return "Start Blitz: The League with these settings.";
    case kDisplayMode: return "Borderless fullscreen or a window.";
    case kRenderRes: return "Internal resolution; above your screen it supersamples.";
    case kAntiAlias: return "FXAA smooths jagged edges.";
    case kUpscaling: return "Sharper scaling to your screen. FSR shines at 1x on a big screen.";
    case kScreenShape: return "Ultrawide widens the 3D view; the HUD is stretched to fit.";
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
    case kUpdates:
      switch (updater::GetState()) {
        case updater::State::kChecking: return "Checking...";
        case updater::State::kUpToDate:
          return std::string(GetBool("blitz.check_updates") ? "Up to date (" : "Off (up to date, ") +
                 updater::Version() + ")";
        case updater::State::kAvailable: return "Version " + updater::Latest() + " - install";
        case updater::State::kDownloading: return "Downloading " + std::to_string(updater::Progress()) + "%";
        case updater::State::kInstalling: return "Installing...";
        case updater::State::kReady: return "Restarting...";
        case updater::State::kFailed: return "Could not update - retry";
        default: return GetBool("blitz.check_updates") ? "Check at startup" : "Off";
      }
    case kDisplayMode: return GetBool("ui.fullscreen") ? "Fullscreen" : "Windowed";
    case kRenderRes: return kRenderLabels[ScaleIndex()];
    case kAntiAlias: return kAALabels[AAIndex()];
    case kUpscaling: return kUpscaleLabels[UpscaleIndex()];
    case kScreenShape: return kShapeLabels[ShapeIndex()];
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
    case kScreenShape: return "blitz.aspect_ratio";
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
    back_pressed_ = false;
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
      back_pressed_ = back_.Update(in.back, now, false);
      if (back_pressed_ && !launcher_ && !popup_) keep_open = false;
    }
    if (move && !popup_) {
      sel_ = Wrap(sel_, move, n);
      quit_armed_ = false;
    }

    // ---- layout
    look::EnsureFonts();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##blitz_menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoNav);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float scale = std::max(0.6f, io.DisplaySize.y / 900.0f);
    Painter p{dl, look::Body() ? look::Body() : ImGui::GetFont(), 24.0f * scale};
    const float lh = p.size * 1.32f;
    const float label_w = p.TextWidth("Launcher at startup") + p.TextWidth("MMMM");
    const float value_w = p.TextWidth("< Off (up to date, 1.10.10) >  *") + p.TextWidth("MM");
    const float hint_w = p.TextWidth(kHint, 0.75f) + 60 * scale;
    const float pw = std::max(label_w + value_w, hint_w) + 60 * scale;
    const float title_h = launcher_ ? 150 * scale : 78 * scale;
    const float ph = title_h + (static_cast<float>(n) + 2.9f) * lh;
    const float px = (io.DisplaySize.x - pw) / 2;
    const float py = std::max(8 * scale, (io.DisplaySize.y - ph) / 2);
    const float value_x = px + 34 * scale + label_w;

    if (launcher_) {
      // Nothing is running behind the launcher yet: draw the whole scene.
      look::Backdrop(dl, io.DisplaySize, static_cast<float>(now), scale);
    } else {
      p.Rect(0, 0, io.DisplaySize.x, io.DisplaySize.y, kShade);
    }
    look::Panel(dl, ImVec2(px, py), ImVec2(px + pw, py + ph), scale);
    look::Title(dl, px + pw / 2, py + (launcher_ ? 14 : 12) * scale, scale, launcher_ ? 0.9f : 0.45f);

    // a newer release: the popup takes the input until answered
    const updater::State ust = updater::GetState();
    if (launcher_ && ust == updater::State::kAvailable && !offered_) {
      offered_ = true;
      popup_ = true;
      popup_sel_ = 0;
    }
    if (popup_) {
      if (activate == -1) popup_sel_ = 0;
      if (activate == 2) popup_sel_ = 1;
    }

    float y = py + title_h;
    for (int i = 0; i < n; i++) {
      const Row row = rows_[i];
      const float rx = px + 22 * scale, rw = pw - 44 * scale, ry = y - 4 * scale;
      if (!popup_) {
        if (ImGui::IsMouseHoveringRect(ImVec2(rx, ry), ImVec2(rx + rw, ry + lh)) &&
            (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
          if (sel_ != i) quit_armed_ = false;
          sel_ = i;
        }
        if (RowClicked(rx, ry, rw, lh, ImGuiMouseButton_Left)) { sel_ = i; activate = 1; }
        if (RowClicked(rx, ry, rw, lh, ImGuiMouseButton_Right)) { sel_ = i; activate = -1; }
      }

      ImU32 text_col = row == kStartGame ? look::Game().accent : look::Game().dim;
      if (i == sel_) text_col = look::Selected(dl, ImVec2(rx, ry), ImVec2(rx + rw, ry + lh), scale);
      p.Text(px + 34 * scale, y, Label(row), text_col);
      if (IsOptionRow(row)) {
        std::string v = "< " + Value(row) + " >";
        if (const char* rs = RestartSetting(row); rs && RestartPending(rs)) v += "  *";
        ImU32 vc = i == sel_ ? text_col : look::Game().text;
        if (row == kUpdates && i != sel_ && (ust == updater::State::kAvailable || ust == updater::State::kFailed))
          vc = look::Game().accent;
        p.Text(value_x, y, v.c_str(), vc);
      } else if (row == kQuit && i == sel_ && quit_armed_) {
        p.Text(value_x, y, "press again to quit", text_col);
      }
      y += lh;
      if (row == kStartGame) y += lh * 0.3f;  // gap under Start game
    }

    // ---- act
    if (popup_) {
      if (activate == 1) {
        if (popup_sel_ == 0) updater::StartInstall();
        popup_ = false;
        wait_release_ = true;
      } else if (back_pressed_) {
        popup_ = false;
        wait_release_ = true;
      }
      activate = 0;
    }
    if (activate != 0) {
      const Row row = rows_[sel_];
      if (row == kUpdates && activate == 1) {
        const updater::State st = updater::GetState();
        if (st == updater::State::kAvailable || (st == updater::State::kFailed && !updater::Latest().empty()))
          updater::StartInstall();
        else if (st != updater::State::kChecking && !updater::Busy())
          updater::StartCheck();
      } else if (IsOptionRow(row)) {
        Change(row, activate > 0 ? 1 : -1);
      } else if (activate == 1) {
        switch (row) {
          case kStartGame:
            if (updater::Busy()) break;  // the update may replace files the game needs
            Save();
            launcher_->store(LauncherChoice::kStart);
            keep_open = false;
            break;
          case kCloseMenu: keep_open = false; break;
          case kQuit:
            if (updater::GetState() == updater::State::kInstalling) break;  // let it finish
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
    // the update is in place: the new version starts, this one goes
    if (launcher_ && keep_open && updater::GetState() == updater::State::kReady) {
      Save();
      launcher_->store(LauncherChoice::kUpdated);
      keep_open = false;
    }

    const char* note = Note(rows_[sel_]);
    std::string bottom = !status_.empty() ? status_ : (note ? std::string(note) : std::string());
    if (bottom.empty() && rows_[sel_] == kQuit) bottom = launcher_ ? "Exit without playing." : "Exit the game.";
    if (rows_[sel_] == kStartGame && updater::Busy()) bottom = "Wait for the update to finish.";
    const ImU32 hint = look::Game().hint;
    dl->AddLine(ImVec2(px + 30 * scale, y + lh * 0.15f), ImVec2(px + pw - 30 * scale, y + lh * 0.15f),
                IM_COL32(200, 210, 215, 40), 1.0f);
    p.Text(px + 34 * scale, y + lh * 0.4f, bottom.c_str(), status_.empty() ? hint : look::Game().accent, 0.78f);
    const char* close_hint = launcher_ ? kHint : "F1 / Esc / B / Guide: close.   * = after restart";
    p.Text(px + 34 * scale, y + lh * 1.3f, close_hint, hint, 0.75f);
    const std::string ver = std::string("v") + updater::Version();
    p.Text(px + pw - 34 * scale - p.TextWidth(ver.c_str(), 0.75f), y + lh * 1.3f, ver.c_str(), hint, 0.75f);
    if (popup_) DrawPopup(dl, io, p, scale);
    ImGui::End();
    return keep_open;
  }

  // "VERSION x IS OUT": Update now / Later
  void DrawPopup(ImDrawList* dl, ImGuiIO& io, const Painter& p, float scale) {
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 160));
    std::string what = updater::Message();
    if (const size_t c = what.find(": "); c != std::string::npos && c < 12) what = what.substr(c + 2);
    const std::string head = "VERSION " + updater::Latest() + " IS OUT";
    const std::string line1 = std::string("You have ") + updater::Version() + (what.empty() ? "." : ".  New: " + what);
    const char* line2 = "Update now downloads it and restarts. Settings, saves and the game stay.";
    const float pw = std::max({p.TextWidth(line1.c_str(), 0.85f), p.TextWidth(line2, 0.85f), 560 * scale}) + 80 * scale;
    const float ph = 250 * scale;
    const ImVec2 p0((io.DisplaySize.x - pw) / 2, (io.DisplaySize.y - ph) / 2), p1(p0.x + pw, p0.y + ph);
    look::Panel(dl, p0, p1, scale);
    ImFont* heavy = look::Heavy() ? look::Heavy() : ImGui::GetFont();
    const float hs = 44 * scale, cx = (p0.x + p1.x) / 2;
    look::Outlined(dl, heavy, hs,
                   ImVec2(cx - heavy->CalcTextSizeA(hs, FLT_MAX, 0, head.c_str()).x / 2, p0.y + 26 * scale),
                   look::Game().accent, head.c_str(), 2.5f * scale);
    p.Text(cx - p.TextWidth(line1.c_str(), 0.85f) / 2, p0.y + 92 * scale, line1.c_str(), look::Game().text, 0.85f);
    p.Text(cx - p.TextWidth(line2, 0.85f) / 2, p0.y + 124 * scale, line2, look::Game().dim, 0.85f);
    const char* labels[2] = {"UPDATE NOW", "LATER"};
    const float bw = 210 * scale, bh = 48 * scale, gap = 40 * scale, by = p1.y - 76 * scale;
    for (int i = 0; i < 2; i++) {
      const ImVec2 a(cx - bw - gap / 2 + i * (bw + gap), by), b(a.x + bw, by + bh);
      if (ImGui::IsMouseHoveringRect(a, b) && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) popup_sel_ = i;
      if (RowClicked(a.x, a.y, bw, bh, ImGuiMouseButton_Left)) {
        if (i == 0) updater::StartInstall();
        popup_ = false;
        wait_release_ = true;
      }
      ImU32 col = look::Game().dim;
      if (i == popup_sel_) col = look::Selected(dl, a, b, scale);
      else dl->AddRect(a, b, IM_COL32(200, 210, 215, 70), 0, 0, std::max(1.0f, scale));
      p.Text(a.x + (bw - p.TextWidth(labels[i])) / 2, by + (bh - p.size) / 2, labels[i], col);
    }
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
      case kUpscaling:
        Set("gpu.xenos.present_effect", kUpscaleValues[Wrap(UpscaleIndex(), dir, kUpscaleCount)]);
        break;
      case kScreenShape:
        Set("blitz.aspect_ratio", kShapeValues[Wrap(ShapeIndex(), dir, kShapeCount)]);
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
      case kUpdates: {
        const bool on = !GetBool("blitz.check_updates");
        SetBool("blitz.check_updates", on);
        if (on && updater::GetState() == updater::State::kIdle) updater::StartCheck();
        break;
      }
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
  bool back_pressed_ = false;
  bool offered_ = false;  // the update popup was shown for the release found
  bool popup_ = false;
  int popup_sel_ = 0;     // 0 Update now, 1 Later
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
