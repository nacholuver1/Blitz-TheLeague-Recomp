// blitz - ReXGlue Recompiled Project
//
// One block of log lines at game start describing the player's PC and the options in
// effect, so a player's log alone answers "what CPU / render scale / audio format?".
// Nothing personal: no user or device names, only models, sizes and formats.

#include "blitz_sysinfo.h"

#include <rex/logging/log_category.h>
#include <rex/settings_registry.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <Windows.h>
#include <intrin.h>
// defines (not only declares) the property keys below, e.g. PKEY_AudioEndpoint_FormFactor
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <winioctl.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

#ifndef BLITZ_VERSION
#define BLITZ_VERSION "0.0.0"
#endif

namespace blitz {
namespace {

std::string Trim(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\0' || s.back() == '\n')) s.pop_back();
  size_t i = 0;
  while (i < s.size() && s[i] == ' ') ++i;
  return s.substr(i);
}

#ifdef _WIN32
std::string CpuName() {
  std::array<int, 4> r{};
  __cpuid(r.data(), 0x80000000);
  if (static_cast<unsigned>(r[0]) < 0x80000004) return "unknown";
  char name[49] = {};
  for (int i = 0; i < 3; ++i) {
    __cpuid(r.data(), 0x80000002 + i);
    std::memcpy(name + i * 16, r.data(), 16);
  }
  return Trim(name);
}

std::string OsVersion() {
  // RtlGetVersion is not shimmed by compatibility manifests, unlike GetVersionEx
  using Fn = LONG(WINAPI*)(OSVERSIONINFOW*);
  OSVERSIONINFOW v{sizeof(v)};
  if (auto fn = reinterpret_cast<Fn>(
          GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
      fn && fn(&v) == 0) {
    return "Windows " + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) +
           " build " + std::to_string(v.dwBuildNumber);
  }
  return "Windows (version unknown)";
}

std::string Memory() {
  MEMORYSTATUSEX m{sizeof(m)};
  if (!GlobalMemoryStatusEx(&m)) return "unknown";
  return std::to_string(m.ullTotalPhys >> 20) + " MB total, " +
         std::to_string(m.ullAvailPhys >> 20) + " MB free";
}

std::string Display() {
  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) return "unknown";
  return std::to_string(dm.dmPelsWidth) + "x" + std::to_string(dm.dmPelsHeight) + " @ " +
         std::to_string(dm.dmDisplayFrequency) + " Hz (main monitor)";
}

// Default output device's shared-mode mix format and form factor. No device name:
// players often name Bluetooth headsets after themselves.
std::string AudioOutput() {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  std::string out = "unknown";
  IMMDeviceEnumerator* en = nullptr;
  IMMDevice* dev = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                 IID_PPV_ARGS(&en))) &&
      SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) {
    out.clear();
    IAudioClient* client = nullptr;
    if (SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(&client)))) {
      WAVEFORMATEX* fmt = nullptr;
      if (SUCCEEDED(client->GetMixFormat(&fmt)) && fmt) {
        out = std::to_string(fmt->nChannels) + " ch, " + std::to_string(fmt->nSamplesPerSec) +
              " Hz, " + std::to_string(fmt->wBitsPerSample) + "-bit";
        CoTaskMemFree(fmt);
      }
      client->Release();
    }
    IPropertyStore* props = nullptr;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
      PROPVARIANT v;
      PropVariantInit(&v);
      if (SUCCEEDED(props->GetValue(PKEY_AudioEndpoint_FormFactor, &v)) && v.vt == VT_UI4) {
        static const char* const kForms[] = {"remote network device", "speakers", "line level",
                                             "headphones", "microphone", "headset", "handset",
                                             "digital pass-through", "SPDIF", "HDMI/DisplayPort"};
        const char* form = v.ulVal < std::size(kForms) ? kForms[v.ulVal] : "other";
        out += (out.empty() ? "" : ", ") + std::string(form);
      }
      PropVariantClear(&v);
      props->Release();
    }
    if (out.empty()) out = "unknown format";
  } else {
    out = "no default output device";
  }
  if (dev) dev->Release();
  if (en) en->Release();
  if (SUCCEEDED(init)) CoUninitialize();
  return out;
}

// SSD or spinning disk for the drive holding `path` (volumes spanning several disks
// and network drives report "unknown").
std::string DriveKind(const std::filesystem::path& path) {
  std::error_code ec;
  const std::filesystem::path abs = std::filesystem::absolute(path, ec);
  const std::wstring root = abs.root_name().wstring();
  if (root.size() != 2 || root[1] != L':') return "unknown";
  const UINT type = GetDriveTypeW((root + L"\\").c_str());
  if (type == DRIVE_REMOTE) return "network drive";
  if (type == DRIVE_REMOVABLE) return "removable drive";
  HANDLE h = CreateFileW((L"\\\\.\\" + root).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return "unknown";
  STORAGE_PROPERTY_QUERY q{};
  q.PropertyId = StorageDeviceSeekPenaltyProperty;
  q.QueryType = PropertyStandardQuery;
  DEVICE_SEEK_PENALTY_DESCRIPTOR d{};
  DWORD got = 0;
  std::string kind = "unknown";
  if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), &d, sizeof(d), &got,
                      nullptr) &&
      got >= sizeof(d)) {
    kind = d.IncursSeekPenalty ? "hard disk" : "SSD";
  }
  CloseHandle(h);
  return kind;
}
#else
std::string CpuName() {
  std::ifstream f("/proc/cpuinfo");
  for (std::string line; std::getline(f, line);) {
    if (line.rfind("model name", 0) == 0) {
      if (auto c = line.find(':'); c != std::string::npos) return Trim(line.substr(c + 1));
    }
  }
  return "unknown";
}

std::string OsVersion() {
  std::string name = "Linux";
  std::ifstream f("/etc/os-release");
  for (std::string line; std::getline(f, line);) {
    if (line.rfind("PRETTY_NAME=", 0) == 0) {
      name = line.substr(12);
      if (name.size() >= 2 && name.front() == '"') name = name.substr(1, name.size() - 2);
    }
  }
  utsname u{};
  if (uname(&u) == 0) name += std::string(", kernel ") + u.release;
  if (std::getenv("SteamDeck")) name += ", Steam Deck";
  return name;
}

std::string Memory() {
  const long pages = sysconf(_SC_PHYS_PAGES), avail = sysconf(_SC_AVPHYS_PAGES),
             size = sysconf(_SC_PAGE_SIZE);
  if (pages <= 0 || size <= 0) return "unknown";
  return std::to_string((static_cast<uint64_t>(pages) * size) >> 20) + " MB total, " +
         (avail < 0 ? std::string("unknown")
                    : std::to_string((static_cast<uint64_t>(avail) * size) >> 20) + " MB") +
         " free";
}
#endif

}  // namespace

void LogSystemInfo() {
  REXLOG_INFO("blitz: version {}{}", BLITZ_VERSION,
              std::getenv("RECOMPLAUNCHER") ? ", started by RecompLauncher" : "");
  REXLOG_INFO("system: {}", OsVersion());
  REXLOG_INFO("system: CPU {} ({} threads)", CpuName(), std::thread::hardware_concurrency());
  REXLOG_INFO("system: RAM {}", Memory());
#ifdef _WIN32
  REXLOG_INFO("system: display {}", Display());
  REXLOG_INFO("system: audio output {}", AudioOutput());
#endif

  auto& registry = rex::GetSettingsRegistry();
  const auto text = [&](const char* name) -> std::string {
    if (auto s = registry.Find(name); s && *s) return (*s)->GetText();
    return "?";
  };
#ifdef _WIN32
  REXLOG_INFO("system: game folder on {}", DriveKind(text("title.game_root")));
#endif
  // The options the launcher and the F1 menu change, plus the GPU defaults we set.
  static const char* const kOptions[] = {
      "gpu.driver",
      "gpu.resolution_scale_x",
      "ui.fullscreen",
      "gpu.vsync",
      "gpu.xenos.swap_post_effect",
      "gpu.xenos.present_effect",
      "blitz.sharpen",
      "gpu.anisotropic_override",
      "blitz.aspect_ratio",
      "gpu.xenos.present_letterbox",
      "gpu.xenos.readback_resolve",
      "gpu.xenos.async_shader_compilation",
      "apu.max_queued_frames",
      "apu.gain",
      "apu.mute",
      "apu.mute_unfocused",
      "blitz.show_fps",
  };
  std::string line;
  for (const char* name : kOptions) {
    line += (line.empty() ? "" : ", ") + std::string(name) + "=" + text(name);
  }
  REXLOG_INFO("settings: {}", line);
}

}  // namespace blitz
