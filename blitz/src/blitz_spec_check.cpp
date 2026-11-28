// blitz - ReXGlue Recompiled Project
//
// Start-up hardware check and its warning screen. See blitz_spec_check.h.

#include "blitz_spec_check.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#include <imgui.h>

#include <rex/app/app.h>
#include <rex/logging/log_category.h>
#include <rex/path_settings.h>
#include <rex/settings_registry.h>
#include <rex/ui/ui_system.h>

#include "blitz_ui.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace blitz {
namespace {

using namespace blitz::ui;

// ------------------------------------------------------------------ bar
// Below these the game runs well under full speed (player reports: Intel UHD on an
// i3-N305; Ryzen Z1 Extreme handheld). A dedicated card from the GTX 1060 / RX 580
// generation up holds 60 fps at 1x-2x.
// Since 30 fps mode (two game steps per drawn frame) keeps full game speed on a PC
// that draws 30, this is the bar for 60 fps; below it the first start recommends
// 30. Built-in graphics stays on this side: handhelds (Steam Deck, Ryzen Z1) are
// built-in graphics and are what 30 fps mode is for.
constexpr double kMinVramGiB = 3.5;  // "4 GB" cards report a little under 4
constexpr double kMinRamGiB = 7.0;   // "8 GB" PCs report 7.4-7.9
constexpr unsigned kMinThreads = 6;
// Below these even 30 fps is unlikely: the "may be too slow" warning.
constexpr double kMin30VramGiB = 1.5;  // "2 GB" cards
constexpr double kMin30RamGiB = 5.5;   // "6 GB"
constexpr unsigned kMin30Threads = 4;

// ------------------------------------------------------------------ GPU via Vulkan
// The game renders with Vulkan. The few loader calls needed here are declared by
// hand (stable C ABI) so this file doesn't need the Vulkan headers or an import.
using VkInstanceH = void*;
using VkPhysicalDeviceH = void*;
// VKAPI_CALL: __stdcall on 32-bit Windows, the default everywhere else.
#if defined(_WIN32) && !defined(_WIN64)
#define BLITZ_VKAPI __stdcall
#else
#define BLITZ_VKAPI
#endif
using VkFn = void(BLITZ_VKAPI*)();
struct VkInstanceCreateInfoMin {
  uint32_t sType;  // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1
  const void* pNext;
  uint32_t flags;
  const void* pApplicationInfo;
  uint32_t enabledLayerCount;
  const char* const* ppEnabledLayerNames;
  uint32_t enabledExtensionCount;
  const char* const* ppEnabledExtensionNames;
};
using PFN_GetInstanceProcAddr = VkFn(BLITZ_VKAPI*)(VkInstanceH, const char*);
using PFN_CreateInstance = int32_t(BLITZ_VKAPI*)(const VkInstanceCreateInfoMin*, const void*, VkInstanceH*);
using PFN_DestroyInstance = void(BLITZ_VKAPI*)(VkInstanceH, const void*);
using PFN_EnumeratePhysicalDevices = int32_t(BLITZ_VKAPI*)(VkInstanceH, uint32_t*, VkPhysicalDeviceH*);
using PFN_GetProperties = void(BLITZ_VKAPI*)(VkPhysicalDeviceH, void*);
// VkPhysicalDeviceProperties: deviceType at 16, deviceName[256] at 20.
// VkPhysicalDeviceMemoryProperties: memoryHeapCount at 260, heaps {u64 size,
// u32 flags} of 16 bytes from 264; flag 1 = device local.
constexpr int kTypeIntegrated = 1, kTypeDiscrete = 2;

struct GpuInfo {
  std::string name;
  int type = 0;
  double vram_gib = 0;
};

std::vector<GpuInfo> ListGpus() {
  std::vector<GpuInfo> out;
#ifdef _WIN32
  HMODULE lib = LoadLibraryW(L"vulkan-1.dll");
  if (!lib) return out;
  auto gipa = reinterpret_cast<PFN_GetInstanceProcAddr>(GetProcAddress(lib, "vkGetInstanceProcAddr"));
#else
  void* lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!lib) return out;
  auto gipa = reinterpret_cast<PFN_GetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));
#endif
  auto create = gipa ? reinterpret_cast<PFN_CreateInstance>(gipa(nullptr, "vkCreateInstance")) : nullptr;
  VkInstanceH inst = nullptr;
  VkInstanceCreateInfoMin ci{};
  ci.sType = 1;
  if (create && create(&ci, nullptr, &inst) == 0 && inst) {
    auto destroy = reinterpret_cast<PFN_DestroyInstance>(gipa(inst, "vkDestroyInstance"));
    auto enumerate = reinterpret_cast<PFN_EnumeratePhysicalDevices>(gipa(inst, "vkEnumeratePhysicalDevices"));
    auto props = reinterpret_cast<PFN_GetProperties>(gipa(inst, "vkGetPhysicalDeviceProperties"));
    auto mem = reinterpret_cast<PFN_GetProperties>(gipa(inst, "vkGetPhysicalDeviceMemoryProperties"));
    uint32_t count = 0;
    if (enumerate && props && mem && enumerate(inst, &count, nullptr) == 0 && count) {
      std::vector<VkPhysicalDeviceH> devs(count);
      if (enumerate(inst, &count, devs.data()) >= 0) {
        for (uint32_t i = 0; i < count; i++) {
          alignas(8) unsigned char p[4096] = {};
          alignas(8) unsigned char m[2048] = {};
          props(devs[i], p);
          mem(devs[i], m);
          GpuInfo g;
          std::memcpy(&g.type, p + 16, 4);
          char name[257] = {};
          std::memcpy(name, p + 20, 256);
          g.name = name;
          uint32_t heaps = 0;
          std::memcpy(&heaps, m + 260, 4);
          uint64_t local = 0;
          for (uint32_t h = 0; h < std::min<uint32_t>(heaps, 16); h++) {
            uint64_t size = 0;
            uint32_t flags = 0;
            std::memcpy(&size, m + 264 + h * 16, 8);
            std::memcpy(&flags, m + 264 + h * 16 + 8, 4);
            if (flags & 1) local = std::max(local, size);
          }
          g.vram_gib = static_cast<double>(local) / (1ull << 30);
          out.push_back(std::move(g));
        }
      }
    }
    if (destroy) destroy(inst, nullptr);
  }
#ifdef _WIN32
  FreeLibrary(lib);
#else
  dlclose(lib);
#endif
  return out;
}

double RamGiB() {
#ifdef _WIN32
  MEMORYSTATUSEX m{};
  m.dwLength = sizeof(m);
  if (!GlobalMemoryStatusEx(&m)) return 0;
  return static_cast<double>(m.ullTotalPhys) / (1ull << 30);
#else
  const long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGE_SIZE);
  if (pages <= 0 || size <= 0) return 0;
  return static_cast<double>(pages) * static_cast<double>(size) / (1ull << 30);
#endif
}

std::string Fmt(const char* f, double v) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), f, v);
  return buf;
}

// ------------------------------------------------------------------ dialog
enum Row { kPlay, kNeverAgain, kQuit, kRowCount };
const char* RowLabel(int row, bool ask_frame_rate) {
  switch (row) {
    case kPlay: return ask_frame_rate ? "Play at 30 fps (recommended)" : "Play anyway";
    case kNeverAgain: return ask_frame_rate ? "Play at 60 fps" : "Play, and don't show this again";
    default: return "Quit";
  }
}

// Below the 60 fps bar (first start): recommend 30 fps.
constexpr const char* kIntro30 =
    "Blitz recreates the Xbox 360's graphics chip on your PC's graphics card, which takes a "
    "lot of graphics power. This PC may not hold 60 fps, and below 60 the game plays in slow "
    "motion:";
constexpr const char* kAdvice30 =
    "30 fps mode keeps full game speed by drawing every other frame, so the PC only needs to "
    "draw 30. 60 fps needs a dedicated graphics card (GTX 1060 / RX 580 or newer), 8 GB of "
    "RAM and a 6-thread CPU. You can change it later under Frame rate in the launcher or the "
    "F1 menu.";
// Below the 30 fps bar.
constexpr const char* kIntroSlow =
    "Blitz recreates the Xbox 360's graphics chip on your PC's graphics card, which takes a "
    "lot of graphics power. This PC is likely below what the game needs even at 30 fps, so it "
    "may run in slow motion or stutter, especially in matches:";
constexpr const char* kAdviceSlow =
    "If you play anyway: use Frame rate 30 fps, keep Render resolution at 1x and close overlay "
    "and recording apps. 30 fps needs at least a real graphics chip with Vulkan, 6 GB of RAM "
    "and a 4-thread CPU.";

class SpecDialog final : public rex::ui::IDialog {
 public:
  SpecDialog(std::atomic<SpecChoice>& choice, SpecReport report, bool ask_frame_rate)
      : choice_(choice), report_(std::move(report)), ask_frame_rate_(ask_frame_rate) {}

  bool NeedsContinuousPaint() const override { return true; }
  bool WantsExclusiveInput() const override { return true; }
  bool OnToggle() override { return false; }
  void OnShow() override { wait_release_ = true; }
  void OnClose() override {}

  bool Draw(ImGuiIO& io) override {
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return true;
    int move = 0;
    bool accept = false;
    const double now = ImGui::GetTime();
    const InputState in = ReadInput();
    if (wait_release_) {
      if (!AnyInput(in)) wait_release_ = false;
    } else {
      if (up_.Update(in.up, now)) move = -1;
      if (down_.Update(in.down, now)) move = 1;
      accept = accept_.Update(in.accept, now, false);
    }
    if (move) sel_ = (sel_ + move + kRowCount) % kRowCount;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##blitz_spec", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoNav);
    const float scale = std::max(0.6f, io.DisplaySize.y / 900.0f);
    Painter p{ImGui::GetWindowDrawList(), ImGui::GetFont(), 24.0f * scale};
    const float lh = p.size * 1.35f;
    const float pw = std::min(io.DisplaySize.x - 40 * scale, 900 * scale);
    const float tx = 20 * scale, tw = pw - 40 * scale;
    const auto wrapped_h = [&](const char* s) {
      return p.font->CalcTextSizeA(p.size * 0.8f, FLT_MAX, tw, s).y;
    };
    const bool slow = report_.below_30;
    const char* intro = slow ? kIntroSlow : kIntro30;
    const char* advice = slow ? kAdviceSlow : kAdvice30;
    const float ph = lh * 2.6f + wrapped_h(intro) + lh * (report_.reasons.size() + 0.8f) +
                     wrapped_h(advice) + lh * (static_cast<float>(kRowCount) + 2.6f);
    const float px = (io.DisplaySize.x - pw) / 2, py = std::max(8 * scale, (io.DisplaySize.y - ph) / 2);

    p.Rect(0, 0, io.DisplaySize.x, io.DisplaySize.y, kBackdrop);
    p.Rect(px, py, pw, ph, kPanel);
    p.Rect(px, py, pw, 3 * scale, kAccent);
    p.Text(px + tx, py + 12 * scale, slow ? "YOUR PC MAY BE TOO SLOW" : "30 FPS RECOMMENDED", kAccent,
           1.6f);
    p.Text(px + tx, py + 12 * scale + lh * 1.45f, "Blitz: The League hardware check", kGrey, 0.8f);
    float y = py + 14 * scale + lh * 2.6f;
    y += p.Wrapped(px + tx, y, tw, intro, kGrey, 0.8f) + lh * 0.3f;
    for (const std::string& r : report_.reasons) {
      p.Text(px + tx + 16 * scale, y, ("-  " + r).c_str(), kWhite, 0.85f);
      y += lh;
    }
    y += lh * 0.3f;
    y += p.Wrapped(px + tx, y, tw, advice, slow ? kGrey : kWhite, 0.8f) + lh * 0.6f;

    int activate = -1;
    for (int i = 0; i < kRowCount; i++) {
      const float rx = px + 8 * scale, rw = pw - 16 * scale, ry = y - 4 * scale;
      if (ImGui::IsMouseHoveringRect(ImVec2(rx, ry), ImVec2(rx + rw, ry + lh)) &&
          (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
        sel_ = i;
      }
      if (RowClicked(rx, ry, rw, lh, ImGuiMouseButton_Left)) activate = i;
      if (i == sel_) p.Rect(rx, ry, rw, lh, kHighlight);
      p.Text(px + tx, y, RowLabel(i, ask_frame_rate_), i == sel_ ? kWhite : (i == kPlay ? kAccent : kGrey));
      y += lh;
    }
    if (accept) activate = sel_;
    p.Text(px + tx, y + lh * 0.4f, "Up/Down select, Enter/A choose. Mouse works too.", kGrey, 0.75f);
    ImGui::End();

    if (ask_frame_rate_ && (activate == kPlay || activate == kNeverAgain)) {
      // The frame-rate question is asked once: the answer is saved (the launcher's
      // Frame rate row changes it). Below the 30 fps bar the "may be too slow"
      // warning still shows on later starts, until "Don't show this again".
      auto& reg = rex::GetSettingsRegistry();
      const char* mode = activate == kPlay ? "30" : "60";
      if (auto r = reg.SetText("blitz.frame_mode", mode); !r) {
        REXLOG_WARN("blitz: could not set the frame rate");
      }
      if (auto s = reg.Save(REX_PATHS().config_file.Get()); !s) {
        REXLOG_WARN("blitz: could not save settings");
      }
      REXLOG_INFO("blitz: hardware check: player chose {} fps", mode);
      choice_.store(SpecChoice::kPlay);
      return false;
    }
    switch (activate) {
      case kNeverAgain: {
        auto& reg = rex::GetSettingsRegistry();
        if (auto r = reg.SetText("blitz.spec_warning", "false"); !r) {
          REXLOG_WARN("blitz: could not turn off the hardware warning");
        } else if (auto s = reg.Save(REX_PATHS().config_file.Get()); !s) {
          REXLOG_WARN("blitz: could not save settings");
        }
        choice_.store(SpecChoice::kPlay);
        return false;
      }
      case kPlay: choice_.store(SpecChoice::kPlay); return false;
      case kQuit:
        choice_.store(SpecChoice::kQuit);
        rex::app::RequestShutdown(0);
        return false;
      default: return true;
    }
  }

 private:
  std::atomic<SpecChoice>& choice_;
  SpecReport report_;
  bool ask_frame_rate_ = false;
  int sel_ = 0;
  bool wait_release_ = true;
  Repeater up_, down_, accept_;
};

}  // namespace

SpecReport CheckSpecs() {
  SpecReport r;
  const std::vector<GpuInfo> gpus = ListGpus();
  const double ram = RamGiB();
  const unsigned threads = std::thread::hardware_concurrency();

  // The GPU the game will use: gpu.vulkan.device when set, else a dedicated card
  // when there is one (the SDK prefers it), else the first.
  const GpuInfo* gpu = nullptr;
  int forced = -1;
  if (auto s = rex::GetSettingsRegistry().Find("gpu.vulkan.device"); s && *s) {
    forced = std::atoi((*s)->GetText().c_str());
  }
  if (forced >= 0 && forced < static_cast<int>(gpus.size())) gpu = &gpus[forced];
  for (const GpuInfo& g : gpus) {
    if (!gpu && g.type == kTypeDiscrete) gpu = &g;
  }
  if (!gpu && !gpus.empty()) gpu = &gpus[0];

  if (gpu) {
    r.gpu = gpu->name;
    if (gpu->type == kTypeIntegrated) {
      r.reasons.push_back("Built-in graphics: " + gpu->name);
    } else if (gpu->type != kTypeDiscrete) {
      // A software or virtual device: no real graphics chip found.
      r.reasons.push_back("No graphics chip found, only: " + gpu->name);
      r.below_30 = true;
    } else if (gpu->vram_gib > 0 && gpu->vram_gib < kMinVramGiB) {
      r.reasons.push_back(Fmt("Graphics card with %.0f GB of memory (4 GB for 60 fps): ",
                              std::max(1.0, gpu->vram_gib + 0.3)) + gpu->name);
      if (gpu->vram_gib < kMin30VramGiB) r.below_30 = true;
    }
  } else {
    // The check could not see the graphics card (no Vulkan driver, or none listed).
    // The game may still run (Direct3D 12), so this recommends 30 fps rather than
    // warning that the PC is too slow.
    r.reasons.push_back("Graphics card could not be checked (no Vulkan device found)");
  }
  if (ram > 0 && ram < kMinRamGiB) {
    r.reasons.push_back(Fmt("%.0f GB of RAM (8 GB for 60 fps)", std::max(1.0, ram + 0.3)));
    if (ram < kMin30RamGiB) r.below_30 = true;
  }
  if (threads > 0 && threads < kMinThreads) {
    r.reasons.push_back(Fmt("%.0f CPU threads (6 for 60 fps)", threads));
    if (threads < kMin30Threads) r.below_30 = true;
  }

  REXLOG_INFO("blitz: hardware check: GPU '{}' ({}, {:.1f} GB), RAM {:.1f} GB, {} CPU threads: {}",
              gpu ? gpu->name : "none found",
              !gpu ? "-" : gpu->type == kTypeDiscrete ? "dedicated" : gpu->type == kTypeIntegrated ? "integrated" : "other",
              gpu ? gpu->vram_gib : 0.0, ram, threads,
              r.below_30         ? "BELOW the 30 fps bar"
              : r.reasons.empty() ? "meets the 60 fps bar"
                                  : "below the 60 fps bar (30 fps recommended)");
  return r;
}

std::unique_ptr<rex::ui::IDialog> MakeSpecWarning(std::atomic<SpecChoice>& choice, SpecReport report,
                                                  bool ask_frame_rate) {
  return std::make_unique<SpecDialog>(choice, std::move(report), ask_frame_rate);
}

}  // namespace blitz
