// blitz - ReXGlue Recompiled Project
//
// GitHub release updates, see blitz_updater.h. The steps:
//  1. Check: GET releases/latest, compare its tag with this build's version, and
//     find this platform's package and the "sha256:" digest GitHub lists for it.
//  2. Download into update/ beside the exe (WinHTTP on Windows, curl on Linux),
//     then refuse it unless its SHA-256 matches.
//  3. Unpack with tar (Windows' own tar.exe reads zip too).
//  4. Each new file goes over the old one, which is renamed to <name>.old first
//     (a running exe and its loaded libraries can be renamed, not overwritten);
//     a failed move puts every old file back.
//  5. Relaunch starts the new exe; its Init deletes the .old files and update/.
// External programs run without a shell, so nothing in a URL is interpreted.

#include "blitz_updater.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

#include <rex/logging/log_category.h>
#include <rex/platform/process.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winhttp.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#ifndef BLITZ_VERSION
#define BLITZ_VERSION "0.0.0"
#endif
#ifndef BLITZ_REPO
#error "BLITZ_REPO (owner/name) must come from CMakeLists.txt"
#endif

namespace fs = std::filesystem;

namespace blitz::updater {
namespace {

#ifdef _WIN32
constexpr const char* kAssetSuffix = "-win64.zip";
#else
constexpr const char* kAssetSuffix = "-linux-x64.tar.gz";
#endif

std::atomic<State> g_state{State::kIdle};
std::atomic<int> g_progress{0};
std::atomic<bool> g_stop{false};  // the app is closing: no file swap may start now
std::mutex g_mutex;  // the strings below
std::string g_latest, g_message, g_url, g_sha256;
uint64_t g_size = 0;
fs::path g_exe;
std::vector<std::string> g_args;

void Fail(const std::string& why) {
  REXLOG_WARN("update: {}", why);
  {
    std::lock_guard lock(g_mutex);
    g_message = why;
  }
  g_state = State::kFailed;
}

fs::path Folder() { return g_exe.parent_path(); }
fs::path UpdateDir() { return Folder() / "update"; }
fs::path OldList() { return UpdateDir() / "old.txt"; }

std::string Utf8(const fs::path& p) {
  const std::u8string u = p.u8string();
  return std::string(u.begin(), u.end());
}
fs::path FromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// "v1.10.2" -> {1, 10, 2}
std::vector<int> ParseVersion(const std::string& v) {
  std::vector<int> n;
  int cur = -1;
  for (char c : v) {
    if (c >= '0' && c <= '9') {
      cur = (cur < 0 ? 0 : cur * 10) + (c - '0');
    } else if (cur >= 0) {
      n.push_back(cur);
      cur = -1;
    }
  }
  if (cur >= 0) n.push_back(cur);
  while (n.size() < 3) n.push_back(0);
  return n;
}

// The string value of "key" at or after pos, with \" \\ \/ \n \t decoded; empty when missing.
std::string JsonString(const std::string& json, const char* key, size_t pos = 0, size_t* found = nullptr) {
  const std::string k = std::string("\"") + key + "\"";
  size_t p = json.find(k, pos);
  if (p == std::string::npos) return {};
  p = json.find(':', p + k.size());
  if (p == std::string::npos) return {};
  p = json.find_first_not_of(" \t\r\n", p + 1);
  if (p == std::string::npos || json[p] != '"') return {};
  std::string v;
  for (size_t i = p + 1; i < json.size(); i++) {
    char c = json[i];
    if (c == '"') {
      if (found) *found = p;
      return v;
    }
    if (c == '\\' && i + 1 < json.size()) {
      c = json[++i];
      if (c == 'n') c = '\n';
      else if (c == 't') c = '\t';
    }
    v += c;
  }
  return {};
}

uint64_t JsonNumber(const std::string& json, const char* key, size_t from, size_t to) {
  const std::string k = std::string("\"") + key + "\"";
  const size_t p = json.find(k, from);
  if (p == std::string::npos || p > to) return 0;
  const size_t c = json.find(':', p);
  return c == std::string::npos ? 0 : std::strtoull(json.c_str() + c + 1, nullptr, 10);
}

// ------------------------------------------------------------------ SHA-256 (FIPS 180-4)
class Sha256 {
 public:
  void Add(const uint8_t* p, size_t n) {
    total_ += n;
    while (n) {
      const size_t take = std::min(n, size_t(64) - used_);
      std::memcpy(buf_ + used_, p, take);
      used_ += take;
      p += take;
      n -= take;
      if (used_ == 64) {
        Block(buf_);
        used_ = 0;
      }
    }
  }
  std::string Hex() {
    const uint64_t bits = total_ * 8;
    const uint8_t one = 0x80, zero = 0;
    Add(&one, 1);
    while (used_ != 56) Add(&zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = uint8_t(bits >> (56 - 8 * i));
    Add(len, 8);
    char out[65];
    for (int i = 0; i < 8; i++) std::snprintf(out + i * 8, 9, "%08x", h_[i]);
    return out;
  }

 private:
  static uint32_t Rot(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
  void Block(const uint8_t* b) {
    static constexpr uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = uint32_t(b[i * 4]) << 24 | uint32_t(b[i * 4 + 1]) << 16 | uint32_t(b[i * 4 + 2]) << 8 | b[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
      const uint32_t s0 = Rot(w[i - 15], 7) ^ Rot(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = Rot(w[i - 2], 17) ^ Rot(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], bb = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; i++) {
      const uint32_t t1 = h + (Rot(e, 6) ^ Rot(e, 11) ^ Rot(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
      const uint32_t t2 = (Rot(a, 2) ^ Rot(a, 13) ^ Rot(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
      h = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
    }
    h_[0] += a; h_[1] += bb; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
  }
  uint32_t h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t buf_[64] = {};
  size_t used_ = 0;
  uint64_t total_ = 0;
};

std::string HashFile(const fs::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  Sha256 sha;
  std::vector<char> buf(1 << 20);
  while (f) {
    f.read(buf.data(), std::streamsize(buf.size()));
    sha.Add(reinterpret_cast<const uint8_t*>(buf.data()), size_t(f.gcount()));
  }
  return sha.Hex();
}

// ------------------------------------------------------------------ programs and downloads
// Runs a program with these arguments (no shell) and waits; true when it returned 0.
bool RunProgram(const std::vector<std::string>& argv) {
#ifdef _WIN32
  std::wstring cmd;
  for (const std::string& a : argv) {
    const fs::path w = FromUtf8(a);
    cmd += L"\"" + w.wstring() + L"\" ";
  }
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    return false;
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return code == 0;
#else
  std::vector<char*> args;
  for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
  args.push_back(nullptr);
  pid_t pid = 0;
  if (posix_spawnp(&pid, args[0], nullptr, nullptr, args.data(), environ) != 0) return false;
  int status = 0;
  pid_t r;
  while ((r = waitpid(pid, &status, 0)) < 0 && errno == EINTR) {
  }
  return r == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

// Downloads url into file; progress (0-100) when total is known. GitHub's
// download links redirect to its file servers: both WinHTTP and curl -L follow.
bool Download(const std::string& url, const fs::path& file, uint64_t total, bool api, std::string& err) {
#ifdef _WIN32
  const std::wstring wurl = FromUtf8(url).wstring();
  URL_COMPONENTS uc{sizeof(uc)};
  wchar_t host[256] = {}, path[2048] = {};
  uc.lpszHostName = host;
  uc.dwHostNameLength = DWORD(std::size(host));
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = DWORD(std::size(path));
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
    err = "bad address";
    return false;
  }
  const std::wstring agent = L"BlitzRecomp/" + FromUtf8(BLITZ_VERSION).wstring();
  HINTERNET s = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
  if (!s)
    s = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!s) {
    err = "no internet access (WinHTTP)";
    return false;
  }
  WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
  HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
  HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                  : nullptr;
  bool ok = false;
  const wchar_t* headers = api ? L"Accept: application/vnd.github+json\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS;
  if (r && WinHttpSendRequest(r, headers, api ? DWORD(-1) : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(r, nullptr)) {
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, nullptr);
    if (status != 200) {
      err = "server answered " + std::to_string(status);
    } else {
      std::ofstream out(file, std::ios::binary | std::ios::trunc);
      std::vector<char> buf(1 << 16);
      uint64_t got = 0;
      ok = bool(out);
      while (ok) {
        DWORD n = 0;
        if (!WinHttpReadData(r, buf.data(), DWORD(buf.size()), &n)) {
          err = "connection lost";
          ok = false;
          break;
        }
        if (n == 0) break;
        out.write(buf.data(), n);
        got += n;
        if (total) g_progress = int(got * 100 / total);
        if (!out) {
          err = "could not write the download";
          ok = false;
        }
      }
    }
  } else {
    err = "could not reach GitHub (offline?)";
  }
  if (r) WinHttpCloseHandle(r);
  if (c) WinHttpCloseHandle(c);
  WinHttpCloseHandle(s);
  return ok;
#else
  // curl writes the file; a second thread turns its size into progress
  std::atomic<bool> done{false};
  std::thread watch([&] {
    while (!done) {
      std::error_code ec;
      const uint64_t got = fs::exists(file, ec) ? fs::file_size(file, ec) : 0;
      if (total && !ec) g_progress = int(std::min<uint64_t>(got * 100 / total, 100));
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  });
  std::vector<std::string> argv = {"curl", "-fsSL", "--connect-timeout", "15", "-A",
                                   std::string("BlitzRecomp/") + BLITZ_VERSION, "-o", file.string()};
  if (api) {
    argv.push_back("-H");
    argv.push_back("Accept: application/vnd.github+json");
  }
  argv.push_back("--");
  argv.push_back(url);
  const bool ok = RunProgram(argv);
  done = true;
  watch.join();
  if (!ok) err = "could not download (is curl installed? offline?)";
  return ok;
#endif
}

void Check() {
  std::error_code ec;
  fs::create_directories(UpdateDir(), ec);
  const fs::path file = UpdateDir() / "latest.json";
  std::string url = "https://api.github.com/repos/" BLITZ_REPO "/releases/latest";
  if (const char* o = std::getenv("BLITZ_UPDATE_URL"); o && *o) url = o;  // tests: a local server
  std::string err;
  if (!Download(url, file, 0, true, err)) {
    fs::remove(file, ec);
    fs::remove(UpdateDir(), ec);
    Fail("Could not check for updates: " + err);
    return;
  }
  std::string json;
  {
    std::ifstream f(file, std::ios::binary);
    json.assign(std::istreambuf_iterator<char>(f), {});
  }
  fs::remove(file, ec);
  fs::remove(UpdateDir(), ec);  // only when empty: an unfinished update's old.txt stays
  const std::string tag = JsonString(json, "tag_name"), name = JsonString(json, "name");
  if (tag.empty()) {
    Fail("Could not check for updates: no release found");
    return;
  }
  if (!(ParseVersion(BLITZ_VERSION) < ParseVersion(tag))) {
    REXLOG_INFO("update: {} is the latest ({})", BLITZ_VERSION, tag);
    g_state = State::kUpToDate;
    return;
  }
  // this platform's package; its digest and size come before its download link in the same asset
  size_t pos = 0, at = 0, prev = 0;
  std::string asset_url, sha;
  uint64_t size = 0;
  for (;;) {
    const std::string u = JsonString(json, "browser_download_url", pos, &at);
    if (u.empty()) break;
    const size_t sl = std::strlen(kAssetSuffix);
    if (u.size() > sl && u.compare(u.size() - sl, sl, kAssetSuffix) == 0) {
      const size_t d = json.rfind("\"digest\"", at);
      if (d != std::string::npos && d > prev) {
        const std::string v = JsonString(json, "digest", d);
        if (v.rfind("sha256:", 0) == 0) sha = v.substr(7);
      }
      size = JsonNumber(json, "size", prev, at);
      asset_url = u;
      break;
    }
    prev = at;
    pos = at + 1;
  }
  if (asset_url.empty()) {
    Fail("Release " + tag + " has no download for this system yet");
    return;
  }
  const std::string latest = tag[0] == 'v' || tag[0] == 'V' ? tag.substr(1) : tag;
  REXLOG_INFO("update: {} available (this is {}): {}", latest, BLITZ_VERSION, asset_url);
  {
    std::lock_guard lock(g_mutex);
    g_latest = latest;
    g_message = name;
    g_url = asset_url;
    g_sha256 = sha;
    g_size = size;
  }
  g_state = State::kAvailable;
}

// player files and folders that an update never touches
bool PlayerFile(const fs::path& rel) {
  if (rel.extension() == ".toml") return true;
  for (const auto& part : rel) {
    const std::string s = part.string();
    if (s == "game" || s == "user" || s == "cache" || s == "logs" || s == "crashes" || s == "update") return true;
  }
  return false;
}

void Install() {
  std::string url, sha;
  uint64_t size = 0;
  {
    std::lock_guard lock(g_mutex);
    url = g_url;
    sha = g_sha256;
    size = g_size;
  }
  std::error_code ec;
  const fs::path dir = UpdateDir(), unpacked = dir / "unpacked";
  const fs::path package = dir / (std::string("download") + kAssetSuffix);
  fs::remove_all(unpacked, ec);
  fs::create_directories(unpacked, ec);

  // 1. download and check
  std::string err;
  if (!Download(url, package, size, false, err)) {
    Fail("Download failed: " + err);
    return;
  }
  const std::string have = HashFile(package);
  if (sha.size() != 64 || have != sha) {
    fs::remove(package, ec);
    Fail(sha.empty() ? "GitHub lists no checksum for the download" : "The download is damaged (checksum differs)");
    return;
  }
  REXLOG_INFO("update: downloaded, sha256 {} matches", have);

  // 2. unpack. kInstalling first, then the stop check: WaitForInstall sets the flag and then reads
  // the state, so either it sees kInstalling and waits, or this sees the flag and gives up.
  State expected = State::kDownloading;
  if (!g_state.compare_exchange_strong(expected, State::kInstalling) || g_stop) {
    fs::remove(package, ec);
    Fail("The game closed before the update was installed");
    return;
  }
#ifdef _WIN32
  wchar_t sys[MAX_PATH] = L"C:\\Windows\\System32";
  GetSystemDirectoryW(sys, MAX_PATH);
  const std::string tar = Utf8(fs::path(sys) / "tar.exe");
  if (!RunProgram({tar, "-xf", Utf8(package), "-C", Utf8(unpacked)})) {
    Fail("Could not unpack the download (needs Windows 10 1803 or later)");
    return;
  }
#else
  if (!RunProgram({"tar", "-xzf", package.string(), "-C", unpacked.string()})) {
    Fail("Could not unpack the download");
    return;
  }
#endif
  // the package holds one folder with the exe in it
  const fs::path exe_name = g_exe.filename();
  fs::path root;
  if (fs::exists(unpacked / exe_name, ec)) root = unpacked;
  for (const auto& e : fs::directory_iterator(unpacked, ec))
    if (root.empty() && e.is_directory() && fs::exists(e.path() / exe_name, ec)) root = e.path();
  if (root.empty()) {
    Fail("The download has no " + Utf8(exe_name));
    return;
  }
  std::vector<fs::path> files;
  bool links = false;
  for (const auto& e : fs::recursive_directory_iterator(root, ec)) {
    if (e.is_symlink()) links = true;
    else if (e.is_regular_file()) files.push_back(e.path());
  }
  if (links) {
    Fail("The download contains links; not installed");
    return;
  }
  if (ec) {  // a partial list would leave a mix of versions
    Fail("Could not read the unpacked files");
    return;
  }

  // 3. each new file over the old one, the old one kept as .old until all are in
  const fs::path folder = Folder();
  std::vector<std::pair<fs::path, bool>> done;  // target, whether an old file was set aside
  const auto old_of = [](fs::path p) { return p += ".old"; };
  const auto undo = [&] {
    for (auto it = done.rbegin(); it != done.rend(); ++it) {
      std::error_code e2;
      if (it->second) fs::rename(old_of(it->first), it->first, e2);
      else fs::remove(it->first, e2);
    }
  };
  for (const fs::path& src : files) {
    const fs::path rel = fs::relative(src, root, ec), target = folder / rel;
    if (ec) {  // skipping it would leave a mix of versions
      undo();
      Fail("Could not place " + Utf8(src.filename()));
      return;
    }
    if (PlayerFile(rel)) continue;
    fs::create_directories(target.parent_path(), ec);
    std::error_code e1;
    const bool had = fs::exists(target, e1);
    if (had) {
      fs::remove(old_of(target), e1);
      fs::rename(target, old_of(target), e1);
      if (e1) {
        undo();
        Fail("Could not replace " + Utf8(rel) + " (is it open?)");
        return;
      }
    }
    fs::rename(src, target, e1);
    if (e1) {  // another drive: copy instead
      e1.clear();
      fs::copy_file(src, target, fs::copy_options::overwrite_existing, e1);
    }
    if (e1) {
      std::error_code e2;
      if (had) fs::rename(old_of(target), target, e2);
      undo();
      Fail("Could not write the new files (is the folder read-only?)");
      return;
    }
    done.push_back({target, had});
  }
  {
    std::ofstream list(OldList(), std::ios::trunc);
    for (const auto& [target, had] : done)
      if (had) list << Utf8(old_of(fs::relative(target, folder, ec))) << "\n";
  }
  fs::remove(package, ec);
  fs::remove_all(unpacked, ec);
  REXLOG_INFO("update: installed {} ({} files)", Latest(), done.size());
  g_state = State::kReady;
}

}  // namespace

const char* Version() { return BLITZ_VERSION; }

void Init(const fs::path& exe, std::span<const std::string> args) {
  g_exe = exe;
  g_args.assign(args.begin(), args.end());
  std::error_code ec;
  const fs::path dir = UpdateDir();
  if (!fs::exists(dir, ec)) return;
  // the previous version may still be closing: tries for up to 3 seconds in all
  std::vector<std::string> left;
  std::ifstream list(OldList());
  std::string line;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::getline(list, line)) {
    if (line.empty()) continue;
    const fs::path old = Folder() / FromUtf8(line);
    for (;;) {
      std::error_code e1;
      if (!fs::exists(old, e1) || fs::remove(old, e1)) break;
      if (std::chrono::steady_clock::now() >= deadline) {
        left.push_back(line);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  list.close();
  if (left.empty()) {
    fs::remove_all(dir, ec);
  } else {
    for (const auto& e : fs::directory_iterator(dir, ec))
      if (e.path().filename() != "old.txt") fs::remove_all(e.path(), ec);
    std::ofstream out(OldList(), std::ios::trunc);
    for (const auto& l : left) out << l << "\n";
  }
}

void StartCheck() {
  const State s = g_state;
  if (s == State::kChecking || Busy() || s == State::kReady) return;
  g_state = State::kChecking;
  std::thread(Check).detach();
}

void StartInstall() {
  const State s = g_state;
  if (s != State::kAvailable && s != State::kFailed) return;
  {
    std::lock_guard lock(g_mutex);
    if (g_url.empty()) return;  // the check failed: nothing to install
  }
  g_progress = 0;
  g_state = State::kDownloading;
  std::thread(Install).detach();
}

State GetState() { return g_state; }

void WaitForInstall() {
  g_stop = true;  // a download still running gives up instead of starting the swap
  while (g_state == State::kInstalling) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
bool Busy() {
  const State s = g_state;
  return s == State::kDownloading || s == State::kInstalling;
}
int Progress() { return g_progress; }

std::string Latest() {
  std::lock_guard lock(g_mutex);
  return g_latest;
}

std::string Message() {
  std::lock_guard lock(g_mutex);
  return g_message;
}

bool Relaunch() {
  // the exe's own path from before the update (on Linux the running file now has the .old name)
  if (auto r = rex::platform::Relaunch(g_exe, g_args, Folder()); !r) {
    REXLOG_ERROR("update: could not start the new version: {}", r.error().GetDescription());
    return false;
  }
  return true;
}

}  // namespace blitz::updater
