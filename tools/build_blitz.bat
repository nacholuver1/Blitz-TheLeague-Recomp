@echo off
rem Builds the Blitz project. Works from any folder name - paths are relative to this file.
rem   build_blitz.bat configure        re-run CMake configure
rem   build_blitz.bat release          build Release, the build BlitzPC ships
rem   build_blitz.bat relwithdebinfo   build with debug symbols
rem   build_blitz.bat package          build Release and copy blitz.exe + DLLs into BlitzPC
rem The environment setup is inlined (same as rexenv.bat): calling rexenv.bat from
rem here breaks vcvars64 when the project path contains spaces.
setlocal
for %%I in ("%~dp0..") do set "BLITZ_ROOT=%%~fI"
set "ACTION=%~1"
if "%ACTION%"=="" goto usage
rem Tool locations; set BLITZ_VCVARS / LLVM_DIR / VULKAN_SDK first to override.
if not defined BLITZ_VCVARS set "BLITZ_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined LLVM_DIR set "LLVM_DIR=C:\Program Files\LLVM"
if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\1.4.363.0"
call "%BLITZ_VCVARS%" >nul 2>&1
if errorlevel 1 exit /b 1
set "PATH=%LLVM_DIR%\bin;%VULKAN_SDK%\Bin;%BLITZ_ROOT%\deps\rexglue-sdk\out\install\win-amd64\bin;%PATH%"
cd /d "%BLITZ_ROOT%\blitz"
if errorlevel 1 exit /b 1
if /i "%ACTION%"=="configure" goto configure
if /i "%ACTION%"=="package" goto package
cmake --build --preset win-amd64-%ACTION%
exit /b %errorlevel%

:configure
cmake --preset win-amd64
exit /b %errorlevel%

:package
cmake --build --preset win-amd64-release
if errorlevel 1 exit /b 1
rem Program files only. Never touches BlitzPC\user or BlitzPC\blitz.toml.
copy /y "out\build\win-amd64\Release\blitz.exe" "..\BlitzPC" >nul
if errorlevel 1 exit /b 1
copy /y "out\build\win-amd64\Release\*.dll" "..\BlitzPC" >nul
if errorlevel 1 exit /b 1
echo Packaged into BlitzPC.
exit /b 0

:usage
echo usage: build_blitz.bat configure / release / relwithdebinfo / package
exit /b 1
