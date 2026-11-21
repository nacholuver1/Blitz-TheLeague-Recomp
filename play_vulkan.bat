@echo off
rem Starts the game with the Vulkan renderer instead of Direct3D 12 (for testing).
rem Runs at 1x resolution with FXAA off: Vulkan is slower here, and this is only
rem for checking graphics bugs.
rem
rem Uses its own settings file (blitz_vulkan.toml), freshly copied from your real
rem blitz.toml each time, because the game saves every active option - including
rem the ones below - when you change something in the menu. Your normal settings
rem for play.bat are never touched. Saves (user\) are shared as usual.
set "PC=%~dp0BlitzPC"
copy /y "%PC%\blitz.toml" "%PC%\blitz_vulkan.toml" >nul
start "" /D "%PC%" "%PC%\blitz.exe" "--app.config_file=%PC%\blitz_vulkan.toml" --gpu.driver=vulkan --gpu.resolution_scale_x=1 --gpu.resolution_scale_y=1 --gpu.xenos.swap_post_effect=none
