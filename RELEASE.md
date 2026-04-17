# auxlab2 Release Plan

This document turns `auxlab2` into a repeatable release process for Windows, macOS, and Linux. It covers the build inputs, packaging outputs, signing steps, and validation checks needed before publishing a versioned release.

## Goals

- Produce a clean install tree from CMake instead of shipping from the build tree.
- Bundle or declare all runtime dependencies needed on a machine that does not have the development environment installed.
- Generate user-facing release artifacts per platform.
- Capture release metadata in the app and in the shipped package.
- Add a smoke-test checklist that catches missing DLLs, Qt plugins, and broken runtime paths.

## Current State

Already present in the repo:

- App version from [`VERSION`](/Users/bkwon/dev/auxlab2/VERSION:1)
- Build metadata embedded through [`src/BuildInfo.h.in`](/Users/bkwon/dev/auxlab2/src/BuildInfo.h.in:1)
- Windows icon resource in [`resources/windows/auxlab2.rc`](/Users/bkwon/dev/auxlab2/resources/windows/auxlab2.rc:1)
- macOS bundle target in [`CMakeLists.txt`](/Users/bkwon/dev/auxlab2/CMakeLists.txt:1)
- Basic `install()` support for the app target

New release-oriented infrastructure added:

- Runtime dependency deploy script in [`cmake/Auxlab2RuntimeDeploy.cmake.in`](/Users/bkwon/dev/auxlab2/cmake/Auxlab2RuntimeDeploy.cmake.in:1)
- Explicit Qt deployment on macOS and Windows
- CPack generators for archive/installer-style packaging

## Release Artifacts

Windows:

- Primary: `.zip` from CPack today
- Optional: NSIS installer when `AUXLAB2_ENABLE_WINDOWS_NSIS=ON`
- Recommended next step: sign the `.zip` contents and/or the NSIS installer on the Windows release machine

macOS:

- Primary: signed `.dmg` via CPack `DragNDrop`
- Contents: `auxlab2.app` with bundled frameworks/plugins

Linux:

- Primary: `.tar.gz` today
- Optional: `.deb` and `.rpm` when `AUXLAB2_ENABLE_NATIVE_LINUX_PACKAGES=ON`
- Recommended next step: AppImage if you want a single portable desktop download

## Shared Release Checklist

1. Bump [`VERSION`](/Users/bkwon/dev/auxlab2/VERSION:1).
2. Build in `Release` mode.
3. Install into a clean staging prefix.
4. Run packaging from that installable build.
5. Smoke-test on a clean machine or VM.
6. Sign platform artifacts.
7. Publish checksums and release notes.

## Build Inputs

Required dependencies:

- Qt 6: `Widgets`, `Multimedia`
- `aux_engine` sibling checkout
- `fftw3`
- `libsamplerate`
- `nlohmann-json`

Recommended conventions:

- Use the same compiler family for the full dependency closure.
- Prefer a reproducible package manager per platform:
  - Windows: `vcpkg`
  - macOS: Homebrew or a pinned Qt install
  - Linux: distro packages inside a release container/VM

## Standard Packaging Commands

Configure:

```bash
cmake -S /Users/bkwon/dev/auxlab2 -B /Users/bkwon/dev/auxlab2/build-release \
  -DCMAKE_BUILD_TYPE=Release
```

Build:

```bash
cmake --build /Users/bkwon/dev/auxlab2/build-release --config Release -j
```

Package:

```bash
cmake --build /Users/bkwon/dev/auxlab2/build-release --target package --config Release
```

Install to a staging prefix for inspection:

```bash
cmake --install /Users/bkwon/dev/auxlab2/build-release \
  --config Release \
  --prefix /tmp/auxlab2-stage
```

## Windows Release Plan

### Prerequisites

- MSVC toolchain
- Qt 6 for MSVC
- `vcpkg` packages for `fftw3`, `libsamplerate`, `nlohmann-json`
- Code-signing certificate

### What the package must contain

- `auxlab2.exe`
- `auxe.dll`
- Qt runtime DLLs
- `platforms/qwindows.dll`
- Qt Multimedia plugins/backends
- `fftw3*.dll`
- `samplerate*.dll`

### Flow

1. Configure with the MSVC generator and Qt/vcpkg paths.
2. Build `Release`.
3. Run `package`.
4. Expand the resulting `.zip` onto a clean Windows VM.
5. Launch `auxlab2.exe` directly and verify startup, audio playback, graphs, and debugger.
6. Sign binaries and the final installer/archive.

### Configure Example

```powershell
cmake -S C:\Users\you\dev\auxlab2 -B C:\Users\you\dev\auxlab2\build-release `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.10.1\msvc2022_64" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
```

### Packaging Notes

- `windeployqt` is now run in release mode with `--compiler-runtime`, `--no-translations`, and `--no-opengl-sw`.
- Runtime DLL scanning still runs after `windeployqt` to catch non-Qt dependencies such as `auxe.dll`, `fftw3*.dll`, and `samplerate*.dll`.
- If you want an installer artifact, configure with:

```powershell
cmake -S C:\Users\you\dev\auxlab2 -B C:\Users\you\dev\auxlab2\build-release `
  -G "Visual Studio 17 2022" -A x64 `
  -DAUXLAB2_ENABLE_WINDOWS_NSIS=ON
```

### Recommended next improvement

- Add WiX if you want a more enterprise-style installer than NSIS.

## macOS Release Plan

### Prerequisites

- Xcode command line tools
- Qt 6 for macOS
- Apple Developer ID Application certificate
- Apple notarization credentials

### What the package must contain

- `auxlab2.app`
- Qt frameworks inside the app bundle
- Qt platform and multimedia plugins
- bundled `libauxe.dylib`
- bundled `fftw` and `libsamplerate` dylibs
- app icon from [`resources/icons/auxlab2.icns`](/Users/bkwon/dev/auxlab2/resources/icons/auxlab2.icns)

### Flow

1. Generate or refresh the `.icns` if needed with [`scripts/make_icns.sh`](/Users/bkwon/dev/auxlab2/scripts/make_icns.sh:1).
2. Build `Release`.
3. Run `package` to produce the `.dmg`.
4. Inspect the staged `.app` with:

```bash
codesign --verify --deep --strict /tmp/auxlab2-stage/auxlab2.app
spctl --assess --type execute /tmp/auxlab2-stage/auxlab2.app
```

5. Sign the app bundle.
6. Notarize the `.dmg`.
7. Staple notarization tickets.
8. Re-test on a clean macOS machine with Gatekeeper enabled.

### Notes

- The CMake deployment hook should handle the Qt side when the Qt deployment helper is available.
- If the release machine’s Qt package does not expose that helper, run `macdeployqt` as a fallback.

## Linux Release Plan

### Prerequisites

- Target distro or container image
- Qt 6 runtime/devel packages
- `fftw3`, `libsamplerate`, `nlohmann-json`

### Packaging options

- `TGZ` is enabled by default and is the lowest-friction artifact.
- `DEB` and `RPM` can be enabled with:

```bash
cmake -S /Users/bkwon/dev/auxlab2 -B /Users/bkwon/dev/auxlab2/build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DAUXLAB2_ENABLE_NATIVE_LINUX_PACKAGES=ON
```

### Flow

1. Build on the oldest distro you intend to support, or inside a compatible container.
2. Run `package`.
3. Test startup on a clean machine that does not have the build tree present.
4. Verify `LD_LIBRARY_PATH` is not required for normal startup.
5. Confirm audio playback and graph rendering.

### Recommended next improvement

- Add an AppImage pipeline if you want a single-click desktop download for users outside a specific distro family.

## Smoke Test Matrix

Run these checks on every release candidate:

- App launches by double-click and by command line.
- About dialog shows expected app version and git hash.
- Command console accepts input and preserves history.
- Open at least one signal graph and one signal table.
- Audio playback works for a sample waveform.
- UDF debugger opens, pauses, and resumes.
- No missing Qt platform plugin errors.
- No missing FFTW or samplerate loader errors.

## CI Recommendation

Add one release workflow per OS:

- Windows runner builds and packages `.zip`, then smoke-tests app launch.
- macOS runner builds `.dmg`, signs, notarizes, and smoke-tests launch.
- Linux runner builds `.tar.gz`, optionally `.deb`/`.rpm`, and smoke-tests on a clean environment.

Recommended outputs:

- packaged artifacts
- SHA-256 checksums
- build log
- smoke-test log

## Remaining Gaps

These are still worth addressing after the current packaging foundation:

- real Windows installer target
- Linux `.desktop` launcher and desktop-file validation
- release signing automation in CI secrets
- explicit license file for packaged artifacts
- per-platform release notes/changelog generation
