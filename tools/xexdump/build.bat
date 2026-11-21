@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set XR=D:\recomp\deps\XenonRecomp
set BLD=D:\recomp\deps\build\XenonRecomp
clang-cl /nologo /O2 /std:c++20 /EHsc /MT -Wno-everything /I"%XR%\XenonUtils" /I"%XR%\thirdparty\simde" ^
  "%~dp0main.cpp" /Fe"%~dp0xexdump.exe" /Fo"%TEMP%\xexdump.obj" ^
  /link "%BLD%\XenonUtils\XenonUtils.lib" "%BLD%\thirdparty\disasm\disasm.lib" || exit /b 1
