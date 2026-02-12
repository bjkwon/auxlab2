# auxlab2

Qt-based GUI application built on top of `auxe` (AUX Engine).

## Features

- Main command console with persistent `AUX> ` prompt
- Variable box showing workspace variables (`name`, `type`, `preview`)
- History box with persistent history file
- UDF debug window (step over/in/out, continue, abort)
- Signal graph windows (multiple)
- Signal table windows (multiple)

## Workspace Layout

`auxlab2` is intended to live as a sibling of `aux_engine`:

- `/Users/bkwon/dev/aux_engine`
- `/Users/bkwon/dev/auxlab2`

The CMake project links to `aux_engine` via:

- `add_subdirectory(../aux_engine ...)`

## Requirements (macOS/Homebrew)

- `cmake`
- Qt 6 (`qt`)
- `fftw`
- `libsamplerate`
- `nlohmann-json`

Example install:

```bash
brew install cmake qt fftw libsamplerate nlohmann-json
```

## Build

```bash
cmake -S /Users/bkwon/dev/auxlab2 -B /Users/bkwon/dev/auxlab2/build \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt;/opt/homebrew/opt/fftw;/opt/homebrew/opt/libsamplerate"

cmake --build /Users/bkwon/dev/auxlab2/build -j
```

## Run

```bash
/Users/bkwon/dev/auxlab2/build/auxlab2
```

## UI Behavior Summary

### Command Console

- Editable only at the last input line
- Immutable colored prompt: `AUX> `
- `Enter`: execute command
- `Up/Down`: history navigation
- `Ctrl+R`: reverse history search
- `Ctrl+A`, `Ctrl+E`, `Ctrl+U`, `Ctrl+K`, `Ctrl+P`, `Ctrl+N`: readline-style keys (platform behavior may vary)

### History Box

- `Enter` on selected row: inject command into console input line
- Double-click: inject and execute
- History is saved/restored automatically

History file:

- `QStandardPaths::AppDataLocation/auxlab2.history`

### Variable Box

- `Enter`: open signal graph window (if variable is displayable)
- `Space`: play audio (if variable is audio)
- Double-click: open signal table window

### Signal Graph Window

- x-axis: time (audio) or index (non-audio)
- y-axis: `[-1, 1]` (audio) or auto-fit (non-audio)
- x/y ticks and labels
- `+`: zoom in (center-based)
- `-`: zoom out
- Mouse drag: select range
- `Enter`: zoom to selected range
- Stereo audio:
  - default: vertical stacked channels
  - `F2`: cycle vertical -> overlay (blue/red) -> overlay (red/blue)
- Audio playback:
  - `Space`: play selected range or current view range
  - `Space` while playing: pause/resume
  - `Esc`: stop
  - moving playhead line during playback

## Notes

- Graph/table windows are tracked per workspace scope.
- During UDF child-scope debugging, windows from other scope are deactivated.
- Windows are closed when their variable is removed from active scope.
