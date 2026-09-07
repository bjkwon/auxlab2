# Building auxlab2 on Windows and Linux

`README.md` covers the macOS/Homebrew build. This document is the detailed
procedure for Windows and Linux, including dependency installation, exact
configure commands, packaging, and the failure modes that actually come up.

For the release/signing/notarization process and the smoke-test matrix, see
[`RELEASE.md`](RELEASE.md). For packaging the engine on its own, see
`../aux_engine/RELEASE_WINDOWS.md`.

## Contents

- [Common prerequisites](#common-prerequisites)
- [Windows](#windows)
- [Linux](#linux)
- [Shared CMake options](#shared-cmake-options)
- [Known platform constraints](#known-platform-constraints)

---

## Common prerequisites

### Workspace layout

`auxlab2` does **not** vendor or fetch the engine. It expects `aux_engine` as a
sibling directory and pulls it in with `add_subdirectory(../aux_engine ...)`:

```
<workspace>/
  aux_engine/      <- required, same parent directory
  auxlab2/
```

If `../aux_engine` is missing, configure fails immediately in
`add_subdirectory`. Both repos must be checked out; there is no submodule.

Configure also reads `../aux_engine/VERSION` and runs `git rev-parse` in both
trees to stamp `BuildInfo.h`. A missing git or a missing `VERSION` file is not
fatal (the hash falls back to `unknown`), but a missing `../aux_engine`
directory is.

### Dependency closure

| Dependency | Used by | Notes |
| --- | --- | --- |
| CMake >= 3.21 | both | 3.21 is the floor for `auxlab2` |
| C++17 compiler | both | MSVC 2022, GCC, or Clang |
| Qt 6 `Widgets` | auxlab2 | |
| Qt 6 `Multimedia` | auxlab2 | audio playback/recording |
| FFTW3 | auxe | `find_package(fftw3/FFTW3)`, falls back to `find_library(fftw3)` |
| libsamplerate | auxe | falls back to `find_library(samplerate)` / `samplerate-0` |
| nlohmann-json | auxe | **config-mode only**: `find_package(nlohmann_json CONFIG REQUIRED)` |

`nlohmann_json` is required in CONFIG mode, so a bare header drop-in is not
enough — the package must ship `nlohmann_jsonConfig.cmake`. Distro `-dev`
packages and the vcpkg port both do.

Use one toolchain family for the whole closure. Mixing an MSVC-built Qt with a
MinGW-built FFTW, or a GCC-built Qt with a Clang-built engine, produces link
errors or runtime crashes that look unrelated to the mismatch.

---

## Windows

MSVC is the supported toolchain. Qt for MSVC, vcpkg's `x64-windows` triplet, and
the `Visual Studio 17 2022` generator are the tested combination.

### 1. Install the toolchain

1. **Visual Studio 2022** (Community is fine) or **Visual Studio 2022 Build
   Tools**, with the *Desktop development with C++* workload. This provides
   `cl.exe`, the Windows SDK, and `rc.exe` (needed for the app icon resource).
2. **CMake 3.21+** — either the standalone installer from cmake.org, or the copy
   bundled with the VS C++ workload.
3. **Git for Windows** — used at configure time for the build hash.

### 2. Install Qt 6 for MSVC

Use the Qt online installer and select, under a Qt 6 version (6.5+ works;
6.10/6.11 is what the macOS build tracks):

- `MSVC 2022 64-bit`
- `Qt Multimedia` (under *Additional Libraries* — it is **not** in the default
  selection, and omitting it is the single most common configure failure)

This gives you a prefix like `C:\Qt\6.10.1\msvc2022_64`.

### 3. Install the engine's dependencies with vcpkg

```powershell
git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = "C:\dev\vcpkg"
```

```powershell
& "$env:VCPKG_ROOT\vcpkg.exe" install fftw3:x64-windows libsamplerate:x64-windows nlohmann-json:x64-windows
```

Set `VCPKG_ROOT` persistently (`[Environment]::SetEnvironmentVariable(...)` or
System Properties) — the CMake project also reads it to find runtime DLLs during
the install/package step.

### 4. Configure

```powershell
cmake -S C:\dev\auxlab2 -B C:\dev\auxlab2\build `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DQT_ROOT="C:\Qt\6.10.1\msvc2022_64"
```

`QT_ROOT` is prepended to `CMAKE_PREFIX_PATH` before `find_package(Qt6)` and is
also used as a hint for `windeployqt`. `-DCMAKE_PREFIX_PATH="C:\Qt\6.10.1\msvc2022_64"`
works equally well for finding Qt; `windeployqt` is then located from Qt's own
exported tool paths.

Ninja works too, from a *Developer PowerShell for VS 2022* prompt (so `cl.exe`
is on `PATH`):

```powershell
cmake -S C:\dev\auxlab2 -B C:\dev\auxlab2\build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DQT_ROOT="C:\Qt\6.10.1\msvc2022_64"
```

### 5. Build

```powershell
cmake --build C:\dev\auxlab2\build --config Release -j
```

Visual Studio is a multi-config generator, so `--config` is required on every
build/install/package invocation. `CMAKE_BUILD_TYPE` is ignored by it.

The build produces `build\Release\auxlab2.exe`, `auxe.dll`, and — via a
`POST_BUILD` copy of `$<TARGET_RUNTIME_DLLS:auxlab2>` — the Qt and vcpkg DLLs
next to the executable, so the build tree is directly runnable.

### 6. Run from the build tree

```powershell
C:\dev\auxlab2\build\Release\auxlab2.exe
```

`AUXLAB2_WIN32_GUI` defaults to `ON`, which links the app as a GUI-subsystem
binary: no console window, and `printf`/`std::cout` output from the engine goes
nowhere visible. To get a console for debugging:

```powershell
cmake -S C:\dev\auxlab2 -B C:\dev\auxlab2\build-console -DAUXLAB2_WIN32_GUI=OFF ...
```

### 7. Install and package

```powershell
cmake --install C:\dev\auxlab2\build --config Release --prefix C:\dev\auxlab2\stage
cmake --build C:\dev\auxlab2\build --target package --config Release
```

The install step runs `cmake/Auxlab2RuntimeDeploy.cmake.in`, which:

1. runs `windeployqt --release --compiler-runtime --no-translations --no-opengl-sw`
   against the installed `auxlab2.exe`, and
2. re-scans with `file(GET_RUNTIME_DEPENDENCIES)` to pick up non-Qt DLLs
   (`auxe.dll`, `fftw3*.dll`, `samplerate*.dll`), excluding the Windows system
   directories.

`package` produces `auxlab2-<version>-win64.zip`. For an NSIS installer as well
(requires NSIS on `PATH`):

```powershell
cmake -S C:\dev\auxlab2 -B C:\dev\auxlab2\build -DAUXLAB2_ENABLE_WINDOWS_NSIS=ON ...
```

Verify the archive contains at minimum:

```
bin/auxlab2.exe
bin/auxe.dll
bin/Qt6Core.dll  Qt6Gui.dll  Qt6Widgets.dll  Qt6Multimedia.dll  Qt6Network.dll
bin/platforms/qwindows.dll
bin/multimedia/*.dll
bin/fftw3*.dll
bin/samplerate*.dll
```

A missing `platforms/qwindows.dll` is the classic cause of *"This application
failed to start because no Qt platform plugin could be initialized"* on a clean
machine.

### Windows troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `Qt6 (Widgets, Multimedia) not found` | Qt Multimedia not installed, or wrong prefix. Check `C:\Qt\<ver>\msvc2022_64\lib\cmake\Qt6Multimedia` exists. |
| `Could not find a package configuration file provided by "nlohmann_json"` | vcpkg toolchain file not passed, or the port not installed for `x64-windows`. |
| `Could not find any of: targets=[...] or libraries=[fftw3]` | Same — the engine's FFTW3/libsamplerate lookup ran without the vcpkg toolchain. |
| `windeployqt not found` warning at configure | Neither Qt's exported tool path nor `QT_ROOT\bin` resolved. Pass `-DQT_ROOT=C:\Qt\<ver>\msvc2022_64`. Packaging will otherwise fall back to DLL scanning and miss Qt plugins. |
| `unistd.h: No such file or directory` in `regression_record_callback.cpp` | Stale `aux_engine` checkout. This was POSIX-only and is fixed; pull `aux_engine`. Workaround: `-DAUXE_BUILD_TESTS=OFF`. |
| `LNK2019` on `__imp_aux*` symbols | `auxe.dll` built without `AUXE_BUILD_DLL`, i.e. `-DAUXE_BUILD_SHARED=OFF` mixed with a dllimport-expecting consumer. Keep `AUXE_BUILD_SHARED=ON`. |
| `MinSizeRel` configuration errors from vcpkg | Constrain the config list: `-DCMAKE_CONFIGURATION_TYPES="Debug;Release"`. |
| App icon warning at configure | `resources/icons/auxlab-icon.ico` or `resources/windows/auxlab2.rc` missing. Non-fatal; the exe gets the default icon. |

---

## Linux

Build on the **oldest** distro you intend to support — glibc symbol versions are
forward-compatible, not backward.

### 1. Install dependencies

**Debian / Ubuntu:**

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake git pkg-config \
  qt6-base-dev qt6-base-dev-tools qt6-multimedia-dev \
  libgl1-mesa-dev \
  libfftw3-dev libsamplerate0-dev nlohmann-json3-dev
```

`libgl1-mesa-dev` is not optional: `find_package(Qt6 COMPONENTS Widgets)` pulls
in `Qt6::Gui`, whose CMake config requires `OpenGL::GL`, and fails with
*"Could NOT find OpenGL"* without it.

For Qt Multimedia to actually play audio at runtime you also need a backend:

```bash
sudo apt install -y libqt6multimedia6 gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-pulseaudio
```

**Fedora / RHEL:**

```bash
sudo dnf install -y \
  gcc-c++ cmake git \
  qt6-qtbase-devel qt6-qtmultimedia-devel \
  mesa-libGL-devel \
  fftw-devel libsamplerate-devel json-devel
```

**Arch:**

```bash
sudo pacman -S --needed base-devel cmake git qt6-base qt6-multimedia fftw libsamplerate nlohmann-json
```

Package names drift between releases. If `find_package` fails, confirm the
`-dev`/`-devel` package that owns `Qt6MultimediaConfig.cmake`,
`nlohmann_jsonConfig.cmake`, `libfftw3.so`, and `libsamplerate.so` is installed.

### 2. Configure and build

```bash
cmake -S ~/dev/auxlab2 -B ~/dev/auxlab2/build -DCMAKE_BUILD_TYPE=Release
cmake --build ~/dev/auxlab2/build -j"$(nproc)"
```

Distro Qt lives on the default search path, so no `CMAKE_PREFIX_PATH` is needed.
For a Qt installed from the Qt online installer instead:

```bash
cmake -S ~/dev/auxlab2 -B ~/dev/auxlab2/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.10.1/gcc_64"
```

Because the `auxlab2` target carries a `VERSION` property, CMake emits
`auxlab2-<version>` plus an `auxlab2` symlink pointing at it. Both are installed
and packaged; run either.

### 3. Run from the build tree

```bash
~/dev/auxlab2/build/auxlab2
```

The target's `INSTALL_RPATH` is `$ORIGIN/../lib;$ORIGIN`, but
`BUILD_WITH_INSTALL_RPATH` is `OFF`, so the build tree uses CMake's own build
rpath and finds `libauxe.so` without `LD_LIBRARY_PATH`. Needing
`LD_LIBRARY_PATH` to launch is a bug, not a workaround — see the smoke test in
`RELEASE.md`.

Headless/CI environments need a display for the Qt Widgets app:

```bash
xvfb-run -a ~/dev/auxlab2/build/auxlab2
```

`QT_QPA_PLATFORM=offscreen` also starts the app without a display, but audio
device enumeration still requires a working sound server.

### 4. Install and package

```bash
cmake --install ~/dev/auxlab2/build --prefix /tmp/auxlab2-stage
cmake --build ~/dev/auxlab2/build --target package
```

`package` produces `auxlab2-<version>-Linux.tar.gz`. To also build native
packages (`dpkg-deb` / `rpmbuild` must be installed):

```bash
cmake -S ~/dev/auxlab2 -B ~/dev/auxlab2/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DAUXLAB2_ENABLE_NATIVE_LINUX_PACKAGES=ON
cmake --build ~/dev/auxlab2/build --target package
```

The TGZ layout is:

```
bin/auxlab2  ->  bin/auxlab2-<version>
lib/libauxe.so
share/auxlab2/{README.md,TUTORIAL_GUI_AND_DEBUGGING.md,RELEASE.md}
share/icons/hicolor/512x512/apps/auxlab2.png
```

**The Linux TGZ is not self-contained.** The deploy script's
`POST_EXCLUDE_REGEXES` drop everything under `/lib`, `/usr/lib`, and
`/usr/local/lib`, so distro-provided Qt, FFTW, and libsamplerate are
deliberately *not* bundled — the archive assumes the target machine has the
runtime packages installed. That is the right default for a distro-matched
build; if you need a portable single-file download, the AppImage path noted
under *Remaining Gaps* in `RELEASE.md` is the way to get it, not this archive.

There is also no `.desktop` file yet, so the packaged app does not appear in
desktop menus.

### Linux troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `Could NOT find OpenGL` | Install `libgl1-mesa-dev` / `mesa-libGL-devel`. |
| `Could not find a package configuration file provided by "Qt6Multimedia"` | Install `qt6-multimedia-dev` / `qt6-qtmultimedia-devel`. |
| `Could not find a package configuration file provided by "nlohmann_json"` | Install `nlohmann-json3-dev` / `json-devel`. Header-only copies do not satisfy `CONFIG REQUIRED`. |
| `qt.qpa.plugin: Could not load the Qt platform plugin "xcb"` | Missing X11/xcb runtime libs, or no display. Install `libxcb-cursor0` (Qt 6.5+ requires it) or use `xvfb-run`. |
| No audio devices listed | No PulseAudio/PipeWire session, or no Qt Multimedia backend plugin installed. |
| `error: 'x' was not declared in this scope` on GCC but not Clang | libstdc++ includes fewer headers transitively than libc++. Add the missing `<cstdint>`/`<cstring>`/`<algorithm>` include to the reported file. |

---

## Shared CMake options

| Option | Default | Effect |
| --- | --- | --- |
| `AUXLAB2_WIN32_GUI` | `ON` | Windows GUI subsystem (no console window) |
| `AUXLAB2_ENABLE_QT_DEPLOYMENT` | `ON` | Run `windeployqt`/`macdeployqt` during install |
| `AUXLAB2_ENABLE_CPACK` | `ON` | Register CPack generators |
| `AUXLAB2_ENABLE_WINDOWS_NSIS` | `OFF` | Add NSIS installer alongside the ZIP |
| `AUXLAB2_ENABLE_NATIVE_LINUX_PACKAGES` | `OFF` | Add DEB/RPM alongside the TGZ |
| `QT_ROOT` | *(unset)* | Qt prefix; prepended to `CMAKE_PREFIX_PATH` and used as a deploy-tool hint |
| `AUXE_BUILD_SHARED` | `ON` | Build `auxe` as a shared library. Keep `ON`. |
| `AUXE_BUILD_TESTS` | `ON` | Build the engine's regression tests. These are part of the `auxlab2` build because the engine is added with `add_subdirectory`; set `OFF` to skip them. |

Engine options are declared by `../aux_engine/CMakeLists.txt` and can be passed
on the `auxlab2` configure line, since it is the same CMake tree.

---

## Known platform constraints

- **MSVC is the supported Windows compiler.** The engine relies on
  MSVC-specific CRT functions (`_splitpath`) and on `<windows.h>` in its
  `_WIN32` branches. Platform guards were normalized to `_WIN32` (previously a
  mix of `_WIN32` and `_WINDOWS`, the latter of which is only defined because
  CMake passes `/D_WINDOWS` for MSVC), so a MinGW build is no longer
  guaranteed-broken — but it is untested and unsupported.
- **Native engine modules must export `auxe_module_init`.** The loader resolves
  it with `GetProcAddress` on Windows, so `extern "C"` alone is not sufficient;
  add `__declspec(dllexport)`, a `.def` file, or set
  `WINDOWS_EXPORT_ALL_SYMBOLS` on the module target. See
  `../aux_engine/docs/external_modules.md`.
- **`aux_engine` uses `file(GLOB CONFIGURE_DEPENDS)`** for its source list.
  Adding or removing an engine source file re-runs configure automatically on
  Makefile/Ninja generators; with the Visual Studio generator this is checked at
  build time as well, but a stale `build\` after a large engine change is worth
  deleting rather than debugging.
- **No cross-compilation.** Each platform's artifact is built on that platform;
  there is no toolchain file for cross builds and the deploy scripts call
  native tools (`windeployqt`, `macdeployqt`, `codesign`).
