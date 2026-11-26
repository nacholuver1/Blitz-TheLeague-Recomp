# Third-party notices

## ReXGlue SDK

https://github.com/rexglue/rexglue-sdk — BSD 3-Clause License.
Copyright (c) 2026, Tom Clay. Portions derived from the Xenia project,
Copyright (c) 2022, Ben Vanik and Xenia project contributors (https://xenia.jp).

The SDK and the libraries it bundles (SDL3, Dear ImGui, spdlog, toml++,
FFmpeg, glslang, xxHash, Tracy, libmspack and others) are distributed under
their own licenses; the release zip includes each one in `licenses/`.
This repository applies small patches to the SDK (`patches/`), which stay
under the SDK's license.

## AMD FidelityFX CAS and FSR 1

The SDK patch `patches/rexglue-ffx-present.patch` adds the presenter's AMD FidelityFX
Contrast Adaptive Sharpening and Super Resolution 1 passes, using the shaders as built by the
Xenia project (BSD 3-Clause, see the SDK notice above) from AMD's
[FidelityFX-CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS) and
[FidelityFX-FSR](https://github.com/GPUOpen-Effects/FidelityFX-FSR) sources.

```
MIT License

Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved. (FidelityFX CAS)
Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved. (FidelityFX FSR)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## darkuni's recomp menus

The launcher / options menu (`blitz/src/blitz_menu.cpp`, `blitz/src/blitz_ui.h`)
adapts the layout and behaviour of the menus in
[darkuni/outrun-arcade-recomp](https://github.com/darkuni/outrun-arcade-recomp).

```
MIT License

Copyright (c) 2026 darkuni

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
