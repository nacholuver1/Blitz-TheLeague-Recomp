@echo off
rem Starts the packaged game. You can also just double-click BlitzPC\blitz.exe.
rem Extra settings can still be passed through, e.g.:  play.bat --gpu.driver=vulkan
start "" /D "%~dp0BlitzPC" "%~dp0BlitzPC\blitz.exe" %*
