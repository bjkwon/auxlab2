# Building auxlab2

The detailed build procedure for macOS, Windows, and Linux: dependency
installation, exact configure commands, packaging, and the failure modes that
actually come up.

For the release/signing/notarization process and the smoke-test matrix, see
[`RELEASE.md`](RELEASE.md). For packaging the engine on its own, see
`../aux_engine/RELEASE_WINDOWS.md`.

## Quick start (Windows)

If the prerequisites below are installed, this is the whole build:

```powershell
$env:VCPKG_ROOT = "C:\dev\vcpkg"
$env:QT_ROOT    = "C:\Qt\6.7.3\msvc2019_64"
```

```powershell
cmake --preset windows
cmake --build --preset windows
.\build\Release\auxlab2.exe
```

`cmake --preset windows` supplies the generator, architecture, vcpkg toolchain,
and configuration list from [`CMakePresets.json`](CMakePresets.json), so there
is no long command line to copy and mistype. Use `--preset windows-vs2019` on a
machine without VS 2022. `cmake --list-presets` shows what is available.

The exe runs straight from `build\Release\` — a `POST_BUILD` step runs
`windeployqt` into the build tree, so no manual deployment is needed.

Prerequisites, in one pass:

1. Visual Studio 2022 (or 2019) with *Desktop development with C++*.
2. Qt 6.5+ MSVC 64-bit, **including the Qt Multimedia component** — it is not
   installed by default, so verify it before configuring:
   ```powershell
   dir $env:QT_ROOT\lib\cmake\Qt6Multimedia
   ```
   If that path is missing, add it with the Qt Maintenance Tool. See
   [First, confirm Qt Multimedia is actually installed](#first-confirm-qt-multimedia-is-actually-installed).
3. `aux_engine` checked out as a sibling of `auxlab2`.
4. vcpkg bootstrapped, with the three ports installed:
   ```powershell
   & "$env:VCPKG_ROOT\vcpkg.exe" install fftw3:x64-windows libsamplerate:x64-windows nlohmann-json:x64-windows
   ```

Set `VCPKG_ROOT` and `QT_ROOT` permanently so you do not repeat step one of the
quick start every session:

```powershell
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\dev\vcpkg", "User")
[Environment]::SetEnvironmentVariable("QT_ROOT", "C:\Qt\6.7.3\msvc2019_64", "User")
```

The rest of this document is the detailed version: what each step does, and what
to do when one of them fails.

## Contents

- [Quick start (Windows)](#quick-start-windows)
- [Common prerequisites](#common-prerequisites)
- [macOS](#macos)
- [Windows](#windows)
  - [Confirm Qt Multimedia is installed](#first-confirm-qt-multimedia-is-actually-installed)
  - [Qt version and toolset compatibility](#qt-version-and-toolset-compatibility)
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
| Qt 6.5+ `Widgets` | auxlab2 | 6.5 is the floor — `QPermission` API |
| Qt 6.5+ `Multimedia` | auxlab2 | audio playback/recording |
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

## macOS

Homebrew is the supported dependency source. Apple Silicon prefixes
(`/opt/homebrew`) are what the project tracks; Intel (`/usr/local`) paths are in
the search list too.

### 1. Install dependencies

```bash
brew install cmake qt fftw libsamplerate nlohmann-json
```

Xcode command line tools are also needed (`xcode-select --install`).

### 2. Configure and build

```bash
cmake --preset macos
cmake --build --preset macos -j
```

The `macos` preset sets `CMAKE_BUILD_TYPE=Release` and the Homebrew
`CMAKE_PREFIX_PATH`. The equivalent explicit form:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt;/opt/homebrew/opt/fftw;/opt/homebrew/opt/libsamplerate"
cmake --build build -j
```

`CMakeLists.txt` already appends the Homebrew Qt/FFTW/libsamplerate prefixes on
Apple, so a bare `cmake -S . -B build` usually resolves everything as well.

### 3. Run

```bash
./build/auxlab2
```

macOS builds an app bundle, so the real executable is
`build/auxlab2.app/Contents/MacOS/auxlab2-<version>`. A `POST_BUILD` step keeps
`build/auxlab2` as a symlink to it, which is why the short path works and why it
never points at a stale binary. Double-clicking `build/auxlab2.app` in Finder
works too.

Microphone access depends on `src/Info.plist.in`; recording needs OS permission
and a usable input device.

### 4. App icon

To produce a Finder-launchable `.app` with a custom icon:

1. Prepare a square PNG (recommended `1024x1024`).
2. Generate the icon file:

   ```bash
   ./scripts/make_icns.sh /absolute/path/to/icon-1024.png
   ```

   This writes `resources/icons/auxlab2.icns`.

3. Reconfigure and rebuild:

   ```bash
   cmake --preset macos
   cmake --build --preset macos -j
   ```

If neither `resources/icons/auxlab2.icns` nor `resources/icons/auxlab-icon.png`
exists, configure warns and the bundle gets the default icon — the build still
succeeds.

### 5. Install and package

```bash
cmake --install build --prefix /tmp/auxlab2-stage
cmake --build build --target package
```

`package` produces a `DragNDrop` `.dmg`. The install step runs `macdeployqt`,
bundles `libauxe.dylib` plus the Qt platform/multimedia plugins into
`auxlab2.app`, writes `Contents/Resources/qt.conf`, and applies an ad-hoc
signature. For a signed and notarized release artifact use
[`scripts/release_macos.sh`](scripts/release_macos.sh) — see
[`RELEASE.md`](RELEASE.md).

### macOS troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `Qt6 (Widgets, Multimedia) not found` | `brew install qt`, or pass `-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt`. |
| `macdeployqt is required for macOS deployment but was not found` | Only affects install/package. Pass `-DQT_ROOT=$(brew --prefix qt)` or add Qt's `bin` to `PATH`. |
| `macOS deployment needs macdeployqt and libqcocoa.dylib` warning | The hardcoded `libqcocoa.dylib` candidate paths in `CMakeLists.txt` are Homebrew-version-specific and may need updating after a Qt upgrade. Build/run are unaffected; only packaging is. |
| App runs from the build tree but the `.app` shows a generic icon | Generate `resources/icons/auxlab2.icns` with `scripts/make_icns.sh`, then reconfigure. |

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

- the MSVC 64-bit build for that Qt version
- `Qt Multimedia` (under *Additional Libraries*)

This gives you a prefix like `C:\Qt\6.10.1\msvc2022_64`.

#### First, confirm Qt Multimedia is actually installed

This is the one real risk in the whole Qt step — Qt Multimedia is a separate
component in the Qt installer and is **not** selected by default, so a normal
"install Qt 6" leaves it out. Check before you configure:

```powershell
dir C:\Qt\6.7.3\msvc2019_64\lib\cmake\Qt6Multimedia
```

If that directory does not exist, add the component — you do not need to
reinstall Qt. Run the **Qt Maintenance Tool** → *Add or remove components* →
expand your Qt version → *Additional Libraries* → check **Qt Multimedia** →
*Next*.

Without it, configure stops at:

```
Qt6 (Widgets, Multimedia) not found.
```

which is the single most common first-time failure on Windows.

#### Qt version and toolset compatibility

**Minimum Qt is 6.5.** The app's newest Qt API is `QMicrophonePermission` /
`QPermission` (Qt 6.5); everything else is Qt 6.0-era. There is no version floor
in `CMakeLists.txt` and no `QT_VERSION` guards in the source, so any Qt >= 6.5
works. 6.7.x and 6.10.x are both known-good.

**ABI compatibility.** MSVC has been binary-compatible across VS 2015 / 2017 /
2019 / 2022, so a Qt built with the v142 toolset links correctly into a VS 2022
(v143) build. Qt's `msvc2019_64` directory name is just what Qt called their
MSVC build at that point — Qt 6.7 never shipped an `msvc2022_64`. Nothing to
work around: point `QT_ROOT` at whichever MSVC directory your Qt version
actually has.

| Qt version | Directory to point `QT_ROOT` at | Use with |
| --- | --- | --- |
| 6.5 – 6.7 | `C:\Qt\<ver>\msvc2019_64` | VS 2019 **or** VS 2022 |
| 6.8+ | `C:\Qt\<ver>\msvc2022_64` | VS 2022 |

Only the generator changes for an older Visual Studio; the Qt path does not.

One caveat on direction: the guarantee covers linking *older* toolset output
into a *newer* build, not the reverse. So if both VS 2019 and VS 2022 are
installed, vcpkg builds its ports with v143 by default — pick VS 2022 as your
generator in that case, so the final link is on the newest toolset among the
inputs.

Do not mix a MinGW Qt (`mingw_64`) into an MSVC build — that is a genuine ABI
mismatch, unlike the toolset-name difference above.

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
# EDIT -G to match the Visual Studio you have installed:
#   VS 2022 -> "Visual Studio 17 2022"
#   VS 2019 -> "Visual Studio 16 2019"
# and EDIT -DQT_ROOT to your actual Qt prefix (see the table above).
cmake -S C:\dev\auxlab2 -B C:\dev\auxlab2\build `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DQT_ROOT="C:\Qt\6.10.1\msvc2022_64"
```

Check which generator you need with:

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -all -products * -property displayName
```

`QT_ROOT` is prepended to `CMAKE_PREFIX_PATH` before `find_package(Qt6)` and is
also used as a hint for `windeployqt`. `-DCMAKE_PREFIX_PATH="C:\Qt\6.10.1\msvc2022_64"`
works equally well for finding Qt; `windeployqt` is then located from Qt's own
exported tool paths.

Ninja works too, from a *Developer PowerShell for VS 2022* prompt (so `cl.exe`
is on `PATH`):

```powershell
# EDIT -DQT_ROOT to your actual Qt prefix. Ninja needs no -G edit per VS
# version, but the prompt must be the Developer PowerShell for your VS.
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

The build produces `build\Release\auxlab2.exe` and `auxe.dll`. Two `POST_BUILD`
steps make the build tree runnable:

1. `$<TARGET_RUNTIME_DLLS:auxlab2>` copies the DLLs the exe *imports* — Qt6Core,
   Qt6Gui, Qt6Widgets, Qt6Multimedia, `fftw3*`, `samplerate*`.
2. A plugin copy places `platforms\qwindows.dll` (plus the Widgets style and
   Qt Multimedia backend plugins, when those targets exist in your Qt version)
   into subdirectories next to the exe.

Step 2 exists because Qt plugins are loaded at runtime through the plugin path
rather than imported, so `TARGET_RUNTIME_DLLS` cannot see them. Without
`platforms\qwindows.dll` the app builds fine and then dies at startup with:

> This application failed to start because no Qt platform plugin could be
> initialized. Reinstalling the application may fix this problem.

If you hit that on an older checkout, either point Qt at its own plugins:

```powershell
$env:QT_PLUGIN_PATH = "C:\Qt\6.7.3\msvc2019_64\plugins"
```

or populate the build tree once with `windeployqt`:

```powershell
& "C:\Qt\6.7.3\msvc2019_64\bin\windeployqt.exe" C:\dev\auxlab2\build\Release\auxlab2.exe
```

`windeployqt` is the more complete of the two — it also brings the FFmpeg DLLs
the Qt Multimedia backend needs, which the plugin copy alone does not.

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

Qt 6.5+ defaults Qt Multimedia to the **FFmpeg** backend on Windows and ships
its own FFmpeg DLLs in Qt's `bin\`. `windeployqt` normally copies them, but
confirm `av*.dll` (`avformat`, `avcodec`, `avutil`, `swresample`, `swscale`) are
in the archive. If they are missing, audio works on the build machine and fails
on a clean one — the failure this checklist exists to catch.

### Windows troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `Qt6 (Widgets, Multimedia) not found` | Qt Multimedia not installed, or wrong prefix. Check `C:\Qt\<ver>\msvc<year>_64\lib\cmake\Qt6Multimedia` exists; add the component with the Qt Maintenance Tool if not. |
| `QPermission`/`QMicrophonePermission` undeclared | Qt older than 6.5. Upgrade Qt; there is no fallback path in the source. |
| `Could not find a package configuration file provided by "nlohmann_json"` | vcpkg toolchain file not passed, or the port not installed for `x64-windows`. |
| `Could not find any of: targets=[...] or libraries=[fftw3]` | Same — the engine's FFTW3/libsamplerate lookup ran without the vcpkg toolchain. |
| `windeployqt not found` warning at configure | Neither Qt's exported tool path nor `QT_ROOT\bin` resolved. Pass `-DQT_ROOT=C:\Qt\<ver>\msvc2022_64`. Packaging will otherwise fall back to DLL scanning and miss Qt plugins. |
| `C1083: Cannot open include file: 'unistd.h'` at `regression_record_callback.cpp(11,10)` | Stale `aux_engine` checkout — this was POSIX-only and is fixed. Pull `aux_engine`; the two repos version independently, so updating only `auxlab2` does not help. Workaround: `-DAUXE_BUILD_TESTS=OFF`. Note this failure does not block `auxlab2.exe`, which is a separate target. |
| `no Qt platform plugin could be initialized` when running from the build tree | `platforms\qwindows.dll` missing. Fixed by a `POST_BUILD` plugin copy; on older checkouts set `QT_PLUGIN_PATH` or run `windeployqt` against the build-tree exe. |
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
| `AUXLAB2_DEPLOY_QT_BUILD_TREE` | `ON` | Windows: run `windeployqt` after linking so `build\<cfg>\auxlab2.exe` runs with no extra setup. Set `OFF` to skip it in tight edit-build loops. |
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
