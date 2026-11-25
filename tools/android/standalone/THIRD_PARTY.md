# Riders Plus standalone Android third-party software

This archive accompanies the standalone APK. It contains license texts, license notices,
and corresponding source where required. The APK also carries the Termux package notices
in `assets/standalone-tools.zip` under `notices/`.

| Component | Source and license |
| --- | --- |
| Clang, LLD and libLLVM 21.1.8-3 | LLVM 21.1.8, Apache 2.0 with LLVM exceptions. Android packaging and patches in the Termux [libllvm recipe](https://github.com/termux/termux-packages/tree/master/packages/libllvm). |
| NDK r27d sysroot headers, link stubs, compiler-rt and game libc++ | Open-source components listed in the accompanying NDK notices. Includes Bionic, Khronos headers and LLVM runtimes. |
| Tool libc++ from Termux | Android NDK r30 libc++ from the [Termux libc++ recipe](https://github.com/termux/termux-packages/tree/master/packages/libc%2B%2B). LLVM and legacy NCSA notices accompany the toolchain. |
| GNU libiconv 1.19.1 | LGPL 2.1 or later for the shared library. The standalone APK includes the library and uses it dynamically through libxml2. The corresponding GNU source archive and the matching Termux recipe are under `sources/libiconv/`. |
| libzstd 1.6.0 | Zstandard shared library. Upstream ships BSD and GPL license text. The exact source, recipe and patches used by the Termux package accompany this archive. |
| libxml2 2.15.4-1, libffi 3.8.1, zlib 1.4 | Licensed under MIT or MIT-style terms for libxml2 and libffi, and the zlib license for zlib. See their package notices in the tool bundle. |
| CPython 3.13.9-0 for Android | PSF license and bundled notices. Chaquopy packages the [CPython Android runtime](https://github.com/chaquo/chaquopy/tree/master/target). Its payload also includes OpenSSL 3.0.18 (Apache 2.0) and SQLite 3.50.4 (public domain). |
| dtk 1.8.4 | MIT or Apache 2.0. The resolved crate inventory and notices accompany this archive. Corresponding source for the MPL-covered crates is under `sources/dtk-crates/`. |
| SDL2 2.32.10 and Dear ImGui 1.92.9b | zlib and MIT, respectively. The APK includes libraries built from the project's pinned source releases. |

The runtime compiles locally from a user-supplied compatible disc. The APK contains no disc image,
game program, disc extraction, generated game C or game data files.

To replace libiconv, build an ABI-compatible Android shared library from the supplied source and Termux
recipe. The file used by the tool process is `tool_libs/libiconv.so` in `assets/standalone-tools.zip`.
The tool binaries are separate ELF files. They link against the replacement library at runtime. A rebuilt
APK can use your own signing key and a separate package name for testing. Riders Plus does not restrict
reverse engineering for debugging changes to the LGPL library. The Android system's signature checks
apply to in-place updates of an existing package.
