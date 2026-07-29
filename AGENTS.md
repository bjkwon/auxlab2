# AGENTS.md

## Purpose

This repository contains `auxlab2`, the Qt 6 GUI workbench and debugger for AUX scripts. It embeds the sibling `aux_engine` library (`auxe`) and owns the application experience: command console, workspace panes, graph/table/debug windows, graphics rendering, audio device I/O, history/settings persistence, packaging, and release artifacts.

Keep runtime semantics in `/Users/bkwon/dev/aux_engine`. Use this repo for app behavior, Qt integration, rendering, backend installation, GUI verification, and release/deployment work.

## Repository Map

- `src/main.cpp`: app startup.
- `src/MainWindow.*`: main GUI orchestration, command dispatch, backend installation, graphics bridge paths, playback/record sessions, history/settings, and most app workflows.
- `src/AuxEngineFacade.*`: wrapper around the `auxe` public API and runtime context/debug helpers.
- `src/GraphicsObjects.*`: app-side figure/axes/line/text model.
- `src/GraphicsManager.*`: graph-window registry, current figure/axes tracking, named-figure lookup.
- `src/SignalGraphWindow.*`: graph rendering, waveform display, zoom/pan/selection, graph-window audio playback.
- `src/*MembersWindow.*`, `src/*ObjectWindow.*`, `src/SignalTableWindow.*`, `src/UdfDebugWindow.*`, `src/DebugCodeEditor.*`: variable/member/binary/text/table/debug views.
- `resources/`: icons and platform resources.
- `cmake/Auxlab2RuntimeDeploy.cmake.in`: runtime dependency deployment for install/package flows.
- `scripts/`: release and icon helper scripts.
- `../aux_engine`: required sibling source tree; CMake adds it with `add_subdirectory(../aux_engine ...)`.

## Build, Run, And Package

Typical macOS/Homebrew build:

```sh
cmake -S /Users/bkwon/dev/auxlab2 -B /Users/bkwon/dev/auxlab2/build \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt;/opt/homebrew/opt/fftw;/opt/homebrew/opt/libsamplerate"
cmake --build /Users/bkwon/dev/auxlab2/build -j
/Users/bkwon/dev/auxlab2/build/auxlab2
```

Release/package build:

```sh
cmake -S /Users/bkwon/dev/auxlab2 -B /Users/bkwon/dev/auxlab2/build-release \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /Users/bkwon/dev/auxlab2/build-release --config Release -j
cmake --build /Users/bkwon/dev/auxlab2/build-release --target package --config Release
```

Important CMake options:

- `AUXLAB2_ENABLE_QT_DEPLOYMENT=ON|OFF`
- `AUXLAB2_ENABLE_CPACK=ON|OFF`
- `AUXLAB2_ENABLE_NATIVE_LINUX_PACKAGES=ON|OFF`
- `AUXLAB2_ENABLE_WINDOWS_NSIS=ON|OFF`
- `AUXLAB2_WIN32_GUI=ON|OFF`

There is no dedicated lint or formatter target. Match the local Qt/C++17 style and build after changes.

## Architecture Invariants

- `auxlab2` owns Qt objects, windows, painting, focus behavior, repaint scheduling, audio devices, packaging, and user-facing workflow.
- `aux_engine` owns AUX runtime semantics, builtin registration, type/handle behavior, parser/evaluator logic, and public API contracts. Consult `/Users/bkwon/dev/aux_engine/AGENTS.md` before moving behavior across the boundary.
- Install graphics and playback/recording backends through `AuxEngineFacade`; do not make `auxe` depend on Qt or app object lifetimes.
- Graphics rendering belongs in `SignalGraphWindow` and the app graphics model. Runtime graphics semantics should not be duplicated casually in command-string bridge code.
- `GraphicsManager` is the source of truth for app-side graph-window registry and current figure/axes lookup.
- GUI focus must keep `gcf`/`gca` behavior consistent with the graphics handle docs.
- Stereo plot layout toggles such as `F2` must not recreate axes/line handles or change handle identity.
- Playback and recording sessions must keep runtime handle members (`prog`, `durLeft`, `repeat_left`, `active`, `paused`, etc.) synchronized through the facade.
- Async recording callbacks may update runtime variables and graphics; keep callback output attachment and graph refresh paths coherent.
- Console method-call sugar in `MainWindow::runCommand` (`x.play`, `x.stop`, `x.pause`, `x.resume`, `x.delete`) must accept both the bare and empty-parentheses forms and must reject identifiers that only share the prefix (`x.stopped`). Terminate these patterns with a negative lookahead for an identifier char, not `\b`: after an optional `(?:\s*\(\s*\))?` group matches `()`, the position between `)` and a non-word char fails `\b`, so the engine backtracks to the empty alternative and leaves the parentheses in the rewritten command (`x.stop()` -> `stop(x)()`).
- Replacement order in that block matters: the empty-arg `play` pattern must run before the general `\.play\s*\((.*)\)` pattern, whose greedy capture would otherwise rewrite `x.play()` as `play(x, )`.

## Coding Conventions

- Use C++17 and Qt 6 idioms already present in the file being edited.
- Keep UI wiring in `MainWindow` only when it truly coordinates multiple app subsystems; put rendering/model details in the dedicated graph/model classes.
- Use `AuxEngineFacade` for engine access instead of calling low-level `aux_*` APIs throughout UI code.
- Prefer clear runtime errors in the console over silent failures, crashes, or stale UI state.
- Keep platform resources and generated build metadata (`BuildInfo.h`) flowing through CMake rather than hardcoding version/build strings.

## Platform Notes

- macOS builds as an app bundle and may use `macdeployqt`, codesigning, notarization, and `scripts/release_macos.sh`.
- Windows builds can use the GUI subsystem, `windeployqt`, runtime DLL copying, ZIP packaging, and optional NSIS.
- Linux packages default to TGZ, with optional DEB/RPM generation.
- Microphone access on macOS depends on `src/Info.plist.in`; recording tests require OS permission and a usable input device.
- App history is stored via `QStandardPaths::AppDataLocation/auxlab2.history`; do not treat local `aux2.history` files in the repo as source truth.

## Do Not Edit Casually

- Build/package outputs: `build*/`, `_CPack_Packages/`, generated `auxe_build/`, `.app`, `.dmg`, install manifests, generated CMake package files, and copied Qt frameworks/plugins.
- Local scratch/sample files at repo root (`*.wav`, `*.aux`, `*.txt`, `*.diff`, `auxenv.json`, `aux2.history`) unless the task explicitly targets them.
- Generated files under build directories, including `generated/BuildInfo.h` and Qt `*_autogen/` outputs.
- Icon/package resources only when the task is about branding, platform resources, or release packaging.

## Verification Expectations

- For ordinary code changes, build `auxlab2`.
- For `auxe` API/backend contract changes, build both `/Users/bkwon/dev/aux_engine` tests and `auxlab2`.
- For graphics behavior, use `TEST_PLAN_GRAPHICS_PLAY_RECORD.md` and/or `MANUAL_CHECKLIST_AUXLAB2_GRAPHICS_PLAY_RECORD.md`; automated build success is not enough.
- For playback/recording changes, manually verify with real audio output/input devices and microphone permission.
- For release/deployment changes, follow `RELEASE.md` and inspect a staged install/package on the target platform.

## Common Pitfalls

- Do not push Qt, rendering, or package/deploy logic into `aux_engine`.
- Do not fix runtime type/handle semantics only in auxlab2 bridge code if the behavior now belongs in `auxe`.
- Do not break named-figure refresh rules: user-overridden style/layout should persist while linked data and still-auto axes properties update.
- Do not let deleting a non-current figure/axes disturb `gcf`/`gca`.
- Do not recreate stereo axes/lines during layout toggles.
- Do not leave playback/recording handles active in the engine after the Qt session has stopped or errored.
- Do not rely on build-tree artifacts when validating release packages.

## Documents To Consult

- `README.md`: app features, local build/run commands, and UI behavior summary.
- `/Users/bkwon/dev/aux_engine/AGENTS.md`: engine/library boundaries and verification.
- `/Users/bkwon/dev/aux_engine/GRAPHICS_RUNTIME_BACKEND_SPLIT.md`: runtime-vs-rendering split.
- `/Users/bkwon/dev/aux_engine/GRAPHICS_MIGRATION_ROADMAP.md`: graphics migration direction; verify against current code before treating tasks as pending.
- `GRAPHICS_HANDLE_IMPLEMENTATION_PLAN.md`: figure/axes/line/text handle behavior, properties, current-handle rules, named plots, and stereo invariants.
- `TEST_PLAN_GRAPHICS_PLAY_RECORD.md`: broad graphics/play/record test plan across `aux2` and `auxlab2`.
- `MANUAL_CHECKLIST_AUXLAB2_GRAPHICS_PLAY_RECORD.md`: GUI manual validation checklist.
- `TUTORIAL_GUI_AND_DEBUGGING.md`: expected user workflows for GUI and debugger behavior.
- `RELEASE.md`: release, packaging, signing, and smoke-test process.
