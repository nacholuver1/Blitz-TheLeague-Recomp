BLITZ: THE LEAGUE - native PC recompilation
===========================================

An unofficial native Windows port of Blitz: The League (Xbox 360), made by
static recompilation with the ReXGlue SDK. No emulator. This download contains
none of the game's data (models, textures, audio, video): you need your own
copy of the game.

What you need
  - Your own Blitz: The League disc for Xbox 360 (the USA / Europe release),
    dumped to a disc image (.iso). Other releases won't work: the program only
    matches that exact game executable.
  - Windows 10 or 11, 64-bit, and a GPU with Direct3D 12 (or Vulkan);
    or Linux x86-64 with glibc 2.38 and GCC 13's libstdc++ or newer (Ubuntu
    24.04, Fedora 39, current Arch / SteamOS / Bazzite) and a Vulkan driver.
  - About 7 GB of free space next to the program for the unpacked game files.

First run
  1. Unzip (Windows) or untar (Linux) this folder anywhere you like - it is
     fully portable.
  2. Run blitz.exe (Windows) or ./blitz (Linux). On Linux the file picker needs
     xdg-desktop-portal or zenity; without one, use --blitz.iso=PATH.
  3. Choose "Use my disc image (.iso)" and pick your dump. It is checked, then
     unpacked into a "game" folder next to blitz.exe (one time only; you can
     delete the .iso afterwards if you want).
     Already extracted the disc with another tool? Choose "Use an extracted
     game folder" and pick the folder that contains default.xex.
  4. The launcher opens: pick your settings, then START GAME.

Controls
  Launcher / menu : arrows or D-pad / left stick to move, Left/Right to change,
                    Enter, A or Start to choose. Mouse works too.
  In game         : F1 or the Guide button (or LB + RB + Back) opens the menu.
                    B, Esc or F1 closes it.  F3 toggles the FPS counter.
  Xbox 360 / XInput controllers work out of the box.

Settings
  Saved to blitz.toml next to the exe. Render resolution, anti-aliasing and the
  audio buffer apply at startup; changing them in the launcher relaunches once.
  Recommended: 2x render resolution, FXAA, 16x texture filtering. 4x render
  resolution is very heavy; the game runs in slow motion if your GPU can't
  keep up, because the game logic runs per frame.

Command-line options (every setting in blitz.toml works as --section.name=value)
  --blitz.iso=PATH            set up from this disc image without the menu
  --title.game_root=PATH      play from this extracted game folder
  --blitz.launcher=false      skip the launcher and start the game directly
  --ui.fullscreen=true|false  fullscreen or windowed
  --gpu.driver=vulkan         use Vulkan instead of Direct3D 12 (Linux always
                              uses Vulkan)
  --gpu.resolution_scale_x=2 --gpu.resolution_scale_y=2   render resolution
  Note: options given on the command line are saved to blitz.toml if you
  change a setting in the menu during that run.

Everything stays in this folder
  game\      the unpacked game files (from your disc)
  user\      profiles, campaign saves, achievements
  blitz.toml settings
  cache\     compiled shaders (safe to delete; rebuilt on first use)
  logs\      a log of each run
  crashes\   crash dumps, if it ever crashes
  Delete the folder and it's all gone. When updating, copy the new blitz.exe
  and DLLs over the old ones; keep game\, user\ and blitz.toml.

Known issues
  - Rare: a small piece of a helmet can stay on a player's forehead during the
    helmet-lift animation.
  - The first time a new effect appears there can be a short stutter while its
    shader compiles (cached afterwards).

Credits and licenses
  AI-assisted: the code was written by Claude (Anthropic's AI, via Claude
  Code), directed and play-tested by nacholuver1.
  ReXGlue SDK (https://github.com/rexglue/rexglue-sdk), based on Xenia
  (https://xenia.jp). Options menu design adapted from darkuni's MIT-licensed
  recomp menus (https://github.com/darkuni/outrun-arcade-recomp). See the
  licenses folder. Blitz: The League is a trademark of its owners; this
  project is not affiliated with or endorsed by them.
