# Blitz: The League — native PC recompilation

An unofficial native Windows and Linux port of **Blitz: The League** (Xbox 360, 2006), made by
*static recompilation* with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).
The game's PowerPC code is translated to C++ ahead of time and compiled into a normal
x64 program (`blitz.exe` on Windows, `blitz` on Linux). No emulator runs at play time; the SDK provides the Xbox 360 kernel,
GPU (Direct3D 12 / Vulkan), audio and input layers.

Sequel: [Blitz-TheLeague-II-Recomp](https://github.com/nacholuver1/Blitz-TheLeague-II-Recomp)
does the same for **Blitz: The League II**.

**This repository contains no game data.** No disc image, no game files, and none of the
C++ that the recompiler generates from the game's executable. To play, you need your own
copy of the game.

> **AI-assisted project.** The code, build scripts, debugging and documentation in this
> repository were written by [Claude](https://claude.com/claude-code) (Anthropic's AI, via
> Claude Code), directed and play-tested by [nacholuver1](https://github.com/nacholuver1).
> Every commit carries a `Co-Authored-By: Claude` trailer. Treat it like any community
> project: read the code, and report problems in Issues.

## Playing

1. Download the latest release from [Releases](../../releases): the `-win64.zip` for Windows or
   the `-linux-x64.tar.gz` for Linux. Extract it anywhere (it's portable).
2. Dump your own **Blitz: The League** disc (USA / Europe release) to an `.iso`.
3. Run `blitz.exe` (Windows) or `./blitz` (Linux), choose **Use my disc image (.iso)** and pick your dump. It checks
   the game executable, then unpacks the disc into a `game` folder next to the exe (one time).
4. The launcher opens: pick settings, then **START GAME**.

The player README ([release/README.txt](release/README.txt)) covers controls, settings
and command-line flags (`--blitz.iso=`, `--title.game_root=`, `--blitz.launcher=false`,
`--ui.fullscreen=`, `--gpu.driver=vulkan`, and any `--section.name=value` from `blitz.toml`).

Everything (game files, saves, settings, shader cache, logs) lives in the exe's folder.

## Building from source

Requirements (Windows x64):

- Visual Studio 2022 Build Tools (C++ workload, Windows SDK 10.0.26100)
- LLVM / Clang ≥ 20 (built with Clang 23) at `C:\Program Files\LLVM`
- Vulkan SDK (built with 1.4.363) at `C:\VulkanSDK\1.4.363.0`
- CMake ≥ 3.25 and Ninja (both come with the Build Tools)
- Python 3 (only for the helper scripts in `tools/`)
- Your own dump of the game

Different install paths: set `BLITZ_VCVARS` (path to `vcvars64.bat`), `LLVM_DIR` and
`VULKAN_SDK` before running the scripts.

```bat
git clone --recursive https://github.com/nacholuver1/Blitz-TheLeague-Recomp.git
cd Blitz-TheLeague-Recomp

rem 1. ReXGlue SDK v0.11.0 (submodule) + this project's patches, then build and install it
git -C deps/rexglue-sdk apply ../../patches/rexglue-dcbz-128.patch ../../patches/rexglue-audio-diag.patch
tools\build_sdk.bat

rem 2. Put your extracted disc in game\ (must contain default.xex), e.g.
python tools\extract_xiso.py "path\to\your.iso" game

rem 3. Configure and build. The build runs `rexglue codegen` first, which reads
rem    game\default.xex and writes the recompiled C++ to blitz\generated\.
tools\build_blitz.bat configure
tools\build_blitz.bat release

rem 4. Player zip (blitz.exe + DLLs + README + licenses, no game data) in dist\
powershell -ExecutionPolicy Bypass -File tools\make_release.ps1 -Version 1.0.0
```

`tools\build_blitz.bat package` builds Release and copies the program files into a local
`BlitzPC\` play folder without touching its saves or settings.

### Linux (x86-64)

Tested on Ubuntu 24.04 (also under WSL2). Build on the oldest distro you want to support:
the binary needs that glibc or newer.

```bash
git clone --recursive https://github.com/nacholuver1/Blitz-TheLeague-Recomp.git
cd Blitz-TheLeague-Recomp
sudo bash tools/linux_deps.sh          # cmake, ninja, clang-20 (apt.llvm.org), SDL/GTK/audio dev packages

# Vulkan SDK 1.4.313 or newer from https://vulkan.lunarg.com/sdk/home#linux, then:
source ~/vulkan/<version>/setup-env.sh

tools/build_linux.sh sdk               # applies patches/, builds and installs the SDK
python3 tools/extract_xiso.py /path/to/your.iso game
tools/build_linux.sh release           # codegen + build: blitz/out/build/linux-amd64/Release/blitz
tools/make_release_linux.sh 1.0.0 --no-build   # dist/BlitzTheLeague-1.0.0-linux-x64.tar.gz
```

With 8 GB of RAM or less, limit parallel jobs (`export CMAKE_BUILD_PARALLEL_LEVEL=6`); the
generated sources are large.

### Game version

The setup screen only accepts the exact `default.xex` this project was made against
(SHA-256 in [blitz/src/blitz_setup.cpp](blitz/src/blitz_setup.cpp)). Another release of the
game needs its own addresses in the manifest and hooks, and a new hash.

## How it works (and what it took)

The project is small: the recompiler does the heavy lifting, and the work was finding
the places where a straight translation behaves differently from a real Xbox 360.

| Where | What |
|---|---|
| [blitz/blitz_manifest.toml](blitz/blitz_manifest.toml) | Codegen manifest. Hand-found CRT `setjmp`/`longjmp`; the statically linked XAPI fiber functions mapped to SDK host-fiber shims (the guest `SwitchToFiber` swaps stacks mid-function, which native code can't); console-exact vector math (fused multiply-add, estimates, denormal flush). |
| [blitz/src/blitz_hooks.cpp](blitz/src/blitz_hooks.cpp) | **Boot deadlock fix**: the game's recursive-lock unlock cleared its owner field *after* `RtlLeaveCriticalSection`. On the 360 the woken thread didn't run yet; on a PC it does, and the late store erases its ownership — the resource loader hung about a second into boot. **Create-a-team saves**: the campaign checks the Xbox Live "user-created content" privilege; granted offline. |
| [blitz/src/main.cpp](blitz/src/main.cpp) | App entry. GPU defaults the game needs: resolve readback (the game renders helmets and sideline crowds on the GPU, then processes them on the CPU) and synchronous shader compilation (the custom-team helmet logo is baked by a one-shot draw that async compilation skipped). Game-folder discovery. |
| [blitz/src/blitz_setup.cpp](blitz/src/blitz_setup.cpp) | First-run setup: XDVDFS disc-image unpacker, XEX title-ID / hash check, setup screen. |
| [blitz/src/blitz_menu.cpp](blitz/src/blitz_menu.cpp) | Pre-game launcher and in-game options menu (F1 / Guide / LB+RB+Back). |
| [blitz/src/blitz_fp_guard.h](blitz/src/blitz_fp_guard.h), [blitz_crash_dump.h](blitz/src/blitz_crash_dump.h) | Masks host SSE floating-point exceptions; writes minidumps to `crashes\`. |
| [patches/](patches/) | SDK patches: `dcbz` clears a 128-byte cache line on Xenon (the codegen cleared 32), and extra audio-stream diagnostics in the log. `rexglue-cs-debug-trace.patch` is an optional contended-lock tracer used to find the boot deadlock. |
| [tools/](tools/) | Build scripts (Windows and Linux), release packers, ISO extractor, XEX header dump, and the debugging helpers used along the way (guest memory peek, LLDB probe, thread stack dump, screenshot automation). |

## Known issues

- During the helmet-lift animation a small helmet piece can stay on the player's forehead
  (same on D3D12 and Vulkan).
- Brief stutter the first time a new effect appears while its shader compiles.
- Linux: builds and boots to the title screen (tested under WSL2 with software rendering);
  not yet play-tested on real Linux hardware with a GPU driver.

## Credits

- Written with [Claude Code](https://claude.com/claude-code) (Anthropic); project direction
  and play-testing by nacholuver1.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay, built on [Xenia](https://xenia.jp).
- Menu layout adapted from [darkuni's outrun-arcade-recomp](https://github.com/darkuni/outrun-arcade-recomp) (MIT).
- Other ReXGlue projects studied while working out the manifest, hooks and SDK fixes (no code
  copied): [NHL2K8-Recompiled](https://github.com/anishchandra24/NHL2K8-Recompiled) (it carries
  the same 128-byte `dcbz` fix), [KameoRePowered](https://github.com/birabittoh/KameoRePowered),
  [PerfectDarkZeroRecomp](https://github.com/nikolaygorb/PerfectDarkZeroRecomp) and
  [afterburner-climax-recomp](https://github.com/darkuni/afterburner-climax-recomp).
- See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Legal

This project's own code is MIT licensed ([LICENSE](LICENSE)). Blitz: The League and its
code, assets and trademarks belong to their owners; this project is not affiliated with or
endorsed by them. The repository contains none of the game's files; the release
`blitz.exe` contains code recompiled from the game's executable and loads all game data
(models, textures, audio, video) from your own disc. Use it only with a copy of the game
you own.
