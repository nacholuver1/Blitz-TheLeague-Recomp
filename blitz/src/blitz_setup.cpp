// blitz - ReXGlue Recompiled Project
//
// First-run setup: disc image unpacking (XDVDFS), game folder check, and the
// setup screen. See blitz_setup.h.

#include "blitz_setup.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>

#include <rex/app/app.h>
#include <rex/logging/log_category.h>
#include <rex/platform/file.h>
#include <rex/ui/ui_system.h>

#include "blitz_ui.h"

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#include <shobjidl.h>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace fs = std::filesystem;

namespace blitz {
namespace {

using namespace blitz::ui;

// The disc this build was recompiled from: Blitz: The League (USA, Europe).
// The recompiled code only matches this exact default.xex.
constexpr uint32_t kTitleId = 0x4D5707D4;
constexpr std::array<uint8_t, 32> kXexSha256 = {
    0x3e, 0xb7, 0x63, 0x82, 0xb8, 0xd3, 0x98, 0x77, 0x9b, 0xc9, 0xbf, 0x86, 0x5e, 0xcc, 0xf8, 0x29,
    0xf1, 0xde, 0xc3, 0x87, 0x06, 0xf2, 0xed, 0xb3, 0x30, 0x85, 0x01, 0xcb, 0x93, 0xda, 0x4c, 0x4e};

// ------------------------------------------------------------------ xex check
uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// Title ID from the XEX2 execution-info header (readable even when the image is
// encrypted). 0 when not found.
uint32_t XexTitleId(const std::vector<uint8_t>& d) {
  if (d.size() < 0x18 || std::memcmp(d.data(), "XEX2", 4) != 0) return 0;
  const uint32_t count = Be32(&d[0x14]);
  for (uint32_t i = 0; i < count && 0x18 + i * 8 + 8 <= d.size(); i++) {
    const uint32_t key = Be32(&d[0x18 + i * 8]);
    const uint32_t val = Be32(&d[0x18 + i * 8 + 4]);
    if (key == 0x00040006 && val + 16 <= d.size()) return Be32(&d[val + 12]);
  }
  return 0;
}

bool Sha256(const std::vector<uint8_t>& data, std::array<uint8_t, 32>& out) {
#ifdef _WIN32
  return BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<uint8_t*>(data.data()),
                    static_cast<ULONG>(data.size()), out.data(), 32) == 0;
#else
  (void)data;
  (void)out;
  return false;
#endif
}

std::string CheckXex(const std::vector<uint8_t>& xex) {
  const uint32_t tid = XexTitleId(xex);
  if (tid == 0) return "That default.xex is not a valid Xbox 360 game executable.";
  if (tid != kTitleId) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "That isn't Blitz: The League (title ID %08X).", tid);
    return buf;
  }
  std::array<uint8_t, 32> hash{};
  if (!Sha256(xex, hash)) return "Could not check default.xex.";
  if (hash != kXexSha256) {
    return "That copy of Blitz: The League is a different version from the one this build "
           "was made from (the USA / Europe disc).";
  }
  return {};
}

std::optional<std::vector<uint8_t>> ReadFile(const fs::path& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return std::nullopt;
  const auto size = static_cast<size_t>(f.tellg());
  std::vector<uint8_t> data(size);
  f.seekg(0);
  if (!f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size))) {
    return std::nullopt;
  }
  return data;
}

// ------------------------------------------------------------------ xdvdfs
// Xbox 360 game discs hold an XDVDFS volume at one of a few known offsets.
// Each directory is a binary tree of entries:
//   u16 left, u16 right (in 4-byte units), u32 sector, u32 size, u8 attr,
//   u8 name length, name.
constexpr uint64_t kSector = 0x800;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr uint64_t kPartitions[] = {0x0, 0xFD90000, 0x89D80000, 0x2080000};

struct DiscEntry {
  fs::path rel;
  uint64_t offset = 0;
  uint64_t size = 0;
  bool dir = false;
};

class Disc {
 public:
  bool Open(const fs::path& path, std::string& error) {
    f_.open(path, std::ios::binary);
    if (!f_) {
      error = "Could not open the disc image.";
      return false;
    }
    char magic[sizeof(kMagic) - 1];
    for (uint64_t off : kPartitions) {
      f_.clear();
      f_.seekg(static_cast<std::streamoff>(off + 0x10000));
      if (f_.read(magic, sizeof(magic)) && std::memcmp(magic, kMagic, sizeof(magic)) == 0) {
        base_ = off;
        uint8_t root[8];
        f_.seekg(static_cast<std::streamoff>(off + 0x10000 + 0x14));
        if (!f_.read(reinterpret_cast<char*>(root), 8)) break;
        const uint32_t sector = root[0] | (root[1] << 8) | (root[2] << 16) | (uint32_t(root[3]) << 24);
        const uint32_t size = root[4] | (root[5] << 8) | (root[6] << 16) | (uint32_t(root[7]) << 24);
        if (!Walk(sector, size, {}, 0)) {
          error = "The disc image's file table is damaged.";
          return false;
        }
        return true;
      }
    }
    error = "That file isn't an Xbox 360 disc image (no XDVDFS volume found).";
    return false;
  }

  const std::vector<DiscEntry>& entries() const { return entries_; }

  bool Read(uint64_t offset, char* out, size_t n) {
    f_.clear();
    f_.seekg(static_cast<std::streamoff>(offset));
    return static_cast<bool>(f_.read(out, static_cast<std::streamsize>(n)));
  }

 private:
  bool Walk(uint32_t dir_sector, uint32_t dir_size, const fs::path& path, int depth) {
    if (depth > 32 || dir_size > (64u << 20)) return false;
    std::vector<uint8_t> table(dir_size);
    if (!Read(base_ + dir_sector * kSector, reinterpret_cast<char*>(table.data()), dir_size)) {
      return false;
    }
    std::vector<uint32_t> stack = {0};
    std::vector<bool> seen(dir_size / 4 + 1);
    while (!stack.empty()) {
      const uint32_t off = stack.back();
      stack.pop_back();
      if (off + 14 > dir_size || seen[off / 4]) continue;
      seen[off / 4] = true;
      const uint8_t* e = &table[off];
      const uint16_t left = e[0] | (e[1] << 8);
      const uint16_t right = e[2] | (e[3] << 8);
      if (left == 0xFFFF) continue;  // padding
      const uint32_t sector = e[4] | (e[5] << 8) | (e[6] << 16) | (uint32_t(e[7]) << 24);
      const uint32_t size = e[8] | (e[9] << 8) | (e[10] << 16) | (uint32_t(e[11]) << 24);
      const uint8_t attr = e[12], nlen = e[13];
      if (off + 14 + nlen > dir_size) continue;
      if (left) stack.push_back(left * 4u);
      if (right) stack.push_back(right * 4u);
      std::string name(reinterpret_cast<const char*>(e + 14), nlen);
      // Never let a name step outside the output folder.
      if (name.empty() || name == "." || name == ".." ||
          name.find_first_of("/\\:") != std::string::npos) {
        continue;
      }
      const fs::path rel = path / name;
      if (attr & 0x10) {
        // The disc's console system update is not part of the game.
        if (depth == 0 && name == "$SystemUpdate") continue;
        entries_.push_back({rel, 0, 0, true});
        if (size && !Walk(sector, size, rel, depth + 1)) return false;
      } else {
        entries_.push_back({rel, base_ + sector * kSector, size, false});
      }
    }
    return true;
  }

  std::ifstream f_;
  uint64_t base_ = 0;
  std::vector<DiscEntry> entries_;
};

// ------------------------------------------------------------------ unpacking
struct Progress {
  std::atomic<uint64_t> done{0};
  std::atomic<uint64_t> total{0};
  std::atomic<bool> cancel{false};
  std::atomic<bool> finished{false};
  std::mutex mutex;
  std::string file;   // guarded by mutex
  std::string error;  // guarded by mutex; empty on success
};

void SetError(Progress& p, std::string msg) {
  std::lock_guard lock(p.mutex);
  p.error = std::move(msg);
}

// Unpacks `iso` into `dest`, via a "<dest>.partial" folder renamed at the end so
// an interrupted unpack is never mistaken for a finished one.
void Unpack(fs::path iso, fs::path dest, Progress& p) {
  struct Finish {
    Progress& p;
    ~Finish() { p.finished.store(true); }
  } finish{p};

  std::string error;
  Disc disc;
  if (!disc.Open(iso, error)) return SetError(p, error);

  // Check the game before writing gigabytes.
  const DiscEntry* xex = nullptr;
  uint64_t total = 0;
  for (const auto& e : disc.entries()) {
    total += e.size;
    if (!e.dir && e.rel == fs::path("default.xex")) xex = &e;
  }
  if (!xex) return SetError(p, "That disc image has no default.xex - is it an Xbox 360 game?");
  std::vector<uint8_t> xex_data(xex->size);
  if (!disc.Read(xex->offset, reinterpret_cast<char*>(xex_data.data()), xex_data.size())) {
    return SetError(p, "Could not read default.xex from the disc image.");
  }
  if (auto bad = CheckXex(xex_data); !bad.empty()) return SetError(p, bad);

  std::error_code ec;
  if (fs::exists(dest, ec)) {
    return SetError(p, "A 'game' folder that doesn't hold this game already exists next to "
                       "blitz.exe. Move or delete it, then try again.");
  }
  const fs::path partial = fs::path(dest).concat(".partial");
  fs::remove_all(partial, ec);  // a previous interrupted unpack
  const auto space = fs::space(dest.parent_path(), ec);
  if (!ec && space.available < total + (256ull << 20)) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Not enough disk space: unpacking needs %.1f GB next to blitz.exe.",
                  static_cast<double>(total) / (1ull << 30));
    return SetError(p, buf);
  }

  p.total.store(total);
  REXLOG_INFO("blitz setup: unpacking {} ({} files, {} bytes) into {}", iso.string(),
              disc.entries().size(), total, dest.string());
  std::vector<char> buffer(16u << 20);
  for (const auto& e : disc.entries()) {
    const fs::path out = partial / e.rel;
    if (e.dir) {
      fs::create_directories(out, ec);
      continue;
    }
    {
      std::lock_guard lock(p.mutex);
      p.file = e.rel.generic_string();
    }
    fs::create_directories(out.parent_path(), ec);
    std::ofstream o(out, std::ios::binary | std::ios::trunc);
    if (!o) {
      fs::remove_all(partial, ec);
      return SetError(p, "Could not write " + out.string());
    }
    uint64_t left = e.size, offset = e.offset;
    while (left) {
      if (p.cancel.load()) {
        o.close();
        fs::remove_all(partial, ec);
        return SetError(p, "Cancelled.");
      }
      const size_t n = static_cast<size_t>(std::min<uint64_t>(left, buffer.size()));
      if (!disc.Read(offset, buffer.data(), n)) {
        o.close();
        fs::remove_all(partial, ec);
        return SetError(p, "The disc image ended early - the dump may be incomplete.");
      }
      if (!o.write(buffer.data(), static_cast<std::streamsize>(n))) {
        o.close();
        fs::remove_all(partial, ec);
        return SetError(p, "Could not write " + out.string() + " (disk full?)");
      }
      left -= n;
      offset += n;
      p.done.fetch_add(n);
    }
  }
  fs::rename(partial, dest, ec);
  if (ec) {
    fs::remove_all(partial, ec);
    return SetError(p, "Could not finish unpacking (rename to 'game' failed).");
  }
  REXLOG_INFO("blitz setup: unpacked into {}", dest.string());
}

// ------------------------------------------------------------------ file picker
#ifdef _WIN32
// Runs the Windows file dialog on its own thread so the setup screen keeps
// drawing; owned by our window so it stays on top of it.
std::future<std::optional<fs::path>> PickAsync(bool folder) {
  HWND owner = nullptr;
  if (OurWindowInFront()) owner = GetForegroundWindow();
  return std::async(std::launch::async, [folder, owner]() -> std::optional<fs::path> {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::optional<fs::path> result;
    IFileOpenDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dlg)))) {
      DWORD opts = 0;
      dlg->GetOptions(&opts);
      opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
      if (folder) {
        opts |= FOS_PICKFOLDERS;
        dlg->SetTitle(L"Choose your extracted Blitz: The League folder (with default.xex)");
      } else {
        opts |= FOS_FILEMUSTEXIST;
        dlg->SetTitle(L"Choose your Blitz: The League disc image");
        const COMDLG_FILTERSPEC filters[] = {{L"Xbox 360 disc image (*.iso)", L"*.iso"},
                                             {L"All files", L"*.*"}};
        dlg->SetFileTypes(2, filters);
      }
      dlg->SetOptions(opts);
      if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
          PWSTR path = nullptr;
          if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            result = fs::path(path);
            CoTaskMemFree(path);
          }
          item->Release();
        }
      }
      dlg->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return result;
  });
}
#else
std::future<std::optional<fs::path>> PickAsync(bool) {
  std::promise<std::optional<fs::path>> p;
  p.set_value(std::nullopt);
  return p.get_future();
}
#endif

// ------------------------------------------------------------------ dialog
enum Row { kUseIso, kUseFolder, kQuit, kRowCount };

const char* RowLabel(int row) {
  switch (row) {
    case kUseIso: return "Use my disc image (.iso)";
    case kUseFolder: return "Use an extracted game folder";
    case kQuit: return "Quit";
  }
  return "";
}

constexpr const char* kIntro =
    "This build doesn't include the game. You need your own copy of Blitz: The League for "
    "Xbox 360 (USA / Europe disc), dumped to a disc image (.iso). The image is unpacked into a "
    "'game' folder next to blitz.exe (about 6.3 GB, one time only). If you already extracted "
    "the disc, choose that folder instead (it must contain default.xex).";

class SetupDialog final : public rex::ui::IDialog {
 public:
  SetupDialog(std::atomic<SetupChoice>& choice, fs::path& game_root, fs::path iso,
              std::string problem)
      : choice_(choice), game_root_(game_root), status_(std::move(problem)) {
    if (!iso.empty()) StartUnpack(iso);
  }
  ~SetupDialog() override {
    progress_.cancel.store(true);
    if (worker_.joinable()) worker_.join();
  }

  bool NeedsContinuousPaint() const override { return true; }
  bool WantsExclusiveInput() const override { return true; }
  bool OnToggle() override { return false; }
  void OnShow() override { wait_release_ = true; }
  void OnClose() override {}

  bool Draw(ImGuiIO& io) override {
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return true;
    Poll();

    // ---- input
    int move = 0;
    bool accept = false, back = false;
    const double now = ImGui::GetTime();
    const InputState in = ReadInput();
    if (wait_release_) {
      if (!AnyInput(in)) wait_release_ = false;
    } else {
      if (up_.Update(in.up, now)) move = -1;
      if (down_.Update(in.down, now)) move = 1;
      accept = accept_.Update(in.accept, now, false);
      back = back_.Update(in.back, now, false);
    }
    const bool busy = worker_.joinable() || picker_.valid();
    if (!busy && move) sel_ = (sel_ + move + kRowCount) % kRowCount;

    // ---- layout
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##blitz_setup", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoNav);
    const float scale = std::max(0.6f, io.DisplaySize.y / 900.0f);
    Painter p{ImGui::GetWindowDrawList(), ImGui::GetFont(), 24.0f * scale};
    const float lh = p.size * 1.35f;
    const float pw = std::min(io.DisplaySize.x - 40 * scale, 900 * scale);
    const float tx = 20 * scale, tw = pw - 40 * scale;
    const float intro_h = p.font->CalcTextSizeA(p.size * 0.8f, FLT_MAX, tw, kIntro).y;
    const float ph = lh * 2.6f + intro_h + lh * (kRowCount + 4.6f);
    const float px = (io.DisplaySize.x - pw) / 2, py = (io.DisplaySize.y - ph) / 2;

    p.Rect(0, 0, io.DisplaySize.x, io.DisplaySize.y, kBackdrop);
    p.Rect(px, py, pw, ph, kPanel);
    p.Rect(px, py, pw, 3 * scale, kAccent);
    p.Text(px + tx, py + 12 * scale, "BLITZ: THE LEAGUE", kAccent, 1.6f);
    p.Text(px + tx, py + 12 * scale + lh * 1.45f, "First-time setup", kGrey, 0.8f);
    float y = py + 14 * scale + lh * 2.6f;
    y += p.Wrapped(px + tx, y, tw, kIntro, kGrey, 0.8f) + lh * 0.6f;

    int activate = -1;
    if (worker_.joinable()) {
      DrawProgress(p, px + tx, y, tw, lh, scale);
      y += lh * kRowCount;
      if (back) progress_.cancel.store(true);
    } else {
      for (int i = 0; i < kRowCount; i++) {
        const float rx = px + 8 * scale, rw = pw - 16 * scale, ry = y - 4 * scale;
        if (!busy) {
          if (ImGui::IsMouseHoveringRect(ImVec2(rx, ry), ImVec2(rx + rw, ry + lh)) &&
              (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
            sel_ = i;
          }
          if (RowClicked(rx, ry, rw, lh, ImGuiMouseButton_Left)) activate = i;
        }
        if (i == sel_) p.Rect(rx, ry, rw, lh, busy ? kTrack : kHighlight);
        p.Text(px + tx, y, RowLabel(i), i == sel_ ? kWhite : (i == kUseIso ? kAccent : kGrey));
        y += lh;
      }
      if (!busy && accept) activate = sel_;
    }

    bool keep_open = true;
    switch (activate) {
      case kUseIso:
      case kUseFolder:
        status_.clear();
        picking_folder_ = activate == kUseFolder;
        picker_ = PickAsync(picking_folder_);
        break;
      case kQuit:
        choice_.store(SetupChoice::kQuit);
        rex::app::RequestShutdown(0);
        keep_open = false;
        break;
      default: break;
    }
    if (done_) {
      choice_.store(SetupChoice::kDone);
      keep_open = false;
    }

    std::string bottom = status_;
    ImU32 bottom_col = status_ok_ ? kAccent : kError;
    if (picker_.valid()) {
      bottom = "Waiting for you to choose in the Windows file dialog...";
      bottom_col = kGrey;
    }
    y += lh * 0.4f;
    if (!bottom.empty()) y += p.Wrapped(px + tx, y, tw, bottom.c_str(), bottom_col, 0.75f);
    else y += lh * 0.75f;
    const char* hint = worker_.joinable() ? "Esc / B: cancel"
                                          : "Up/Down select, Enter/A choose. Mouse works too.";
    p.Text(px + tx, y + lh * 0.3f, hint, kGrey, 0.75f);
    ImGui::End();
    return keep_open;
  }

 private:
  void DrawProgress(const Painter& p, float x, float y, float w, float lh, float scale) {
    const uint64_t total = progress_.total.load(), done = progress_.done.load();
    const float frac = total ? static_cast<float>(static_cast<double>(done) / total) : 0.0f;
    char buf[96];
    if (total) {
      std::snprintf(buf, sizeof(buf), "Unpacking your disc...  %.1f / %.1f GB  (%d%%)",
                    static_cast<double>(done) / (1ull << 30),
                    static_cast<double>(total) / (1ull << 30), static_cast<int>(frac * 100));
    } else {
      std::snprintf(buf, sizeof(buf), "Checking your disc image...");
    }
    p.Text(x, y, buf, kWhite);
    const float by = y + lh * 1.1f, bh = 14 * scale;
    p.Rect(x, by, w, bh, kTrack);
    p.Rect(x, by, w * frac, bh, kAccent);
    std::string file;
    {
      std::lock_guard lock(progress_.mutex);
      file = progress_.file;
    }
    if (!file.empty()) p.Text(x, by + bh + lh * 0.4f, file.c_str(), kGrey, 0.75f);
  }

  void StartUnpack(const fs::path& iso) {
    progress_.done.store(0);
    progress_.total.store(0);
    progress_.cancel.store(false);
    progress_.finished.store(false);
    {
      std::lock_guard lock(progress_.mutex);
      progress_.error.clear();
      progress_.file.clear();
    }
    worker_ = std::thread(Unpack, iso, BundledGameFolder(), std::ref(progress_));
  }

  void Poll() {
    if (picker_.valid() &&
        picker_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      const auto picked = picker_.get();
      wait_release_ = true;  // the click/Enter that closed the picker
      if (picked) {
        if (picking_folder_) {
          if (auto bad = CheckGameFolder(*picked); bad.empty()) {
            game_root_ = *picked;
            done_ = true;
          } else {
            SetStatus(bad, false);
          }
        } else {
          StartUnpack(*picked);
        }
      }
    }
    if (worker_.joinable() && progress_.finished.load()) {
      worker_.join();
      std::string error;
      {
        std::lock_guard lock(progress_.mutex);
        error = progress_.error;
      }
      if (error.empty()) {
        game_root_ = BundledGameFolder();
        done_ = true;
      } else {
        SetStatus(error, false);
      }
    }
  }

  void SetStatus(std::string msg, bool ok) {
    REXLOG_WARN("blitz setup: {}", msg);
    status_ = std::move(msg);
    status_ok_ = ok;
  }

  std::atomic<SetupChoice>& choice_;
  fs::path& game_root_;
  std::string status_;
  bool status_ok_ = false;
  int sel_ = 0;
  bool wait_release_ = true;
  bool done_ = false;
  bool picking_folder_ = false;
  std::future<std::optional<fs::path>> picker_;
  Progress progress_;
  std::thread worker_;
  Repeater up_, down_, accept_, back_;
};

}  // namespace

fs::path BundledGameFolder() { return rex::platform::GetExecutableFolder() / "game"; }

std::string CheckGameFolder(const fs::path& dir) {
  std::error_code ec;
  if (dir.empty() || !fs::is_directory(dir, ec)) return "That game folder doesn't exist.";
  const fs::path xex = dir / "default.xex";
  if (!fs::is_regular_file(xex, ec)) return "There's no default.xex in that folder.";
  auto data = ReadFile(xex);
  if (!data) return "Could not read default.xex.";
  return CheckXex(*data);
}

std::unique_ptr<rex::ui::IDialog> MakeSetup(std::atomic<SetupChoice>& choice, fs::path& game_root,
                                            fs::path iso, std::string problem) {
  return std::make_unique<SetupDialog>(choice, game_root, std::move(iso), std::move(problem));
}

}  // namespace blitz
