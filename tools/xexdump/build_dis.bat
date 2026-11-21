@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
set XR=D:\recomp\deps\XenonRecomp
set BLD=D:\recomp\deps\build\XenonRecomp
clang-cl /nologo /O2 /std:c++20 /EHsc /MT -Wno-everything /I"%XR%\XenonUtils" /I"%XR%\thirdparty\disasm" ^
  "%~dp0disasm_main.cpp" /Fe"%~dp0ppcdis.exe" /Fo"%TEMP%\ppcdis.obj" ^
  /link "%BLD%\XenonUtils\XenonUtils.lib" "%BLD%\thirdparty\disasm\disasm.lib" || exit /b 1
