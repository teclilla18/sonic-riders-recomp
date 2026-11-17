# Diagnostic and dependency notices

Diagnostic source: MIT License, copyright (c) 2026 Sonic Riders recomp contributors.

Native Vulkan integration adapts the structure of the ounce-devkit Vulkan-clear
example (https://github.com/ouncebrew/ounce-devkit, b11c29cba8d476f193bdc78ac98f344c167e541c):

MIT License

Copyright (c) 2026 the ounce-devkit contributors

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

The independently written probes also use published APIs in libounce (ISC;
libnx and libounce contributors, with mimalloc under MIT), devkitA64/newlib/libgcc (their
respective licenses and runtime exceptions), and OunceVK/Mesa (MIT and other
component licenses). The pinned SDK license texts are supplied alongside the
local build under its deps/ tree; the build package includes the available
SDK notices. No upstream license is replaced by this diagnostic's MIT notice.

Implementation references, not linked or copied: Dusklight Switch 2 platform
helpers at tag v2.0.3-switch2-1 (CC0 1.0), and its Aurora dependency (MIT).
No Nintendo code, game data, proprietary SDK or console secrets are included.
