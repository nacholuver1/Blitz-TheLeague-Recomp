@echo off
rem Builds and installs the ReXGlue SDK (Release) from deps\rexglue-sdk.
rem   build_sdk.bat             build + install (configures first if needed)
rem   build_sdk.bat configure   force a fresh configure, e.g. after moving the project folder
rem Environment setup is inlined: calling rexenv.bat from another .bat breaks
rem vcvars64 when the project path contains spaces.
setlocal
for %%I in ("%~dp0..") do set "BLITZ_ROOT=%%~fI"
set "SDK=%BLITZ_ROOT%\deps\rexglue-sdk"
rem Tool locations; set BLITZ_VCVARS / LLVM_DIR / VULKAN_SDK first to override.
if not defined BLITZ_VCVARS set "BLITZ_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined LLVM_DIR set "LLVM_DIR=C:\Program Files\LLVM"
if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\1.4.363.0"
call "%BLITZ_VCVARS%" >nul 2>&1
if errorlevel 1 exit /b 1
set "PATH=%LLVM_DIR%\bin;%VULKAN_SDK%\Bin;%PATH%"
cd /d "%SDK%"
if errorlevel 1 exit /b 1
if /i "%~1"=="configure" goto fresh
if not exist "out\build\win-amd64\CMakeCache.txt" goto fresh
goto build

:fresh
if exist "out\build\win-amd64\CMakeCache.txt" del /q "out\build\win-amd64\CMakeCache.txt"
if exist "out\build\win-amd64\CMakeFiles" rmdir /s /q "out\build\win-amd64\CMakeFiles"
cmake --preset win-amd64 -DREXGLUE_BUILD_TESTS=OFF
if errorlevel 1 exit /b 1

:build
cmake --build out\build\win-amd64 --config Release --target install
exit /b %errorlevel%
