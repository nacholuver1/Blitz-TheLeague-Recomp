@echo off
rem Sets up a shell for ReXGlue builds: VS 2022 libs/SDK, LLVM 23 clang, Vulkan SDK, rexglue on PATH.
rem Paths are relative to this file, so the project folder can be renamed or moved.
rem Resolve our own folder first: vcvars64 changes the current directory, after
rem which %~dp0 can resolve wrongly for a script started by a quoted path.
for %%I in ("%~dp0..") do set "BLITZ_ROOT=%%~fI"
if not defined BLITZ_VCVARS set "BLITZ_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined LLVM_DIR set "LLVM_DIR=C:\Program Files\LLVM"
if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\1.4.363.0"
call "%BLITZ_VCVARS%" >nul 2>&1
if errorlevel 1 exit /b 1
set "PATH=%LLVM_DIR%\bin;%VULKAN_SDK%\Bin;%BLITZ_ROOT%\deps\rexglue-sdk\out\install\win-amd64\bin;%PATH%"
if not "%~1"=="" %*
