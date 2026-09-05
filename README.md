# auxlab2

Qt-based GUI application built on top of `auxe` (AUX Engine).

## Features

- Main command console with persistent `AUX> ` prompt
- Variable box showing workspace variables (`name`, `type`, `preview`)
- History box with persistent history file
- UDF debug window (step over/in/out, continue, abort)
- Signal graph windows (multiple)
  - graph tabs can be detached into independent windows and docked back into the main window
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

## App Icon (macOS)

To produce a Finder-launchable `.app` with a custom icon:

1. Prepare a square PNG (recommended `1024x1024`).
2. Generate the icon file:

```bash
/Users/bkwon/dev/auxlab2/scripts/make_icns.sh /absolute/path/to/icon-1024.png
```

This writes:

- `/Users/bkwon/dev/auxlab2/resources/icons/auxlab2.icns`

3. Reconfigure/build:

```bash
cmake -S /Users/bkwon/dev/auxlab2 -B /Users/bkwon/dev/auxlab2/build
cmake --build /Users/bkwon/dev/auxlab2/build -j
```

4. Launch by double-clicking:

- `/Users/bkwon/dev/auxlab2/build/auxlab2.app`

## UI Behavior Summary

### Command Console

- Editable only at the last input line
- Immutable colored prompt: `AUX> `
- `Enter`: execute command
- `Up/Down`: history navigation
- `Ctrl+R`: reverse history search
- `Ctrl+A`, `Ctrl+E`, `Ctrl+U`, `Ctrl+K`, `Ctrl+P`, `Ctrl+N`: readline-style keys (platform behavior may vary)

### Console Method Syntax

The console accepts method-style forms on a variable or handle and rewrites them to
the equivalent AUX function call before evaluation. Both the bare form and the
empty-parentheses form are accepted and behave identically.

Playback / recording handles:

| Method form | Rewritten as |
| --- | --- |
| `x.play` / `x.play()` | `play(x)` |
| `x.play(0~1)` | `play(x, 0~1)` |
| `h.stop` / `h.stop()` | `stop(h)` |
| `h.pause` / `h.pause()` | `pause(h)` |
| `h.resume` / `h.resume()` | `resume(h)` |
| `h.delete` / `h.delete()` | `delete(h)` |

Graphics handles:

| Method form | Rewritten as |
| --- | --- |
| `f.axes` / `f.axes()` | `axes(f)` |
| `ax.plot` | `plot(ax)` |
| `ax.plot(v)` / `ax.plot(v,"b+")` | `plot(ax, v)` / `plot(ax, v,"b+")` |
| `ax.line(v)` / `ax.line(x,y)` | `line(ax, v)` / `line(ax, x,y)` |
| `ax.text(...)` | `text(ax, ...)` |
| `h.delete` / `h.delete()` | `delete(h)` |

Two details worth knowing:

- Rewriting is name-based only. Identifiers that merely start with a method name
  (`x.stopped`, `x.playback`) are left untouched.
- The playback/recording forms are rewritten anywhere in the command line, while the
  graphics forms are recognized only when the method call is the whole command
  (optionally with an assignment target, e.g. `ln=ax.line(x,y)`).

### History Box

- `Enter` on selected row: inject command into console input line
- Double-click: inject and execute
- Object undo/redo from the menu or shortcuts leaves a comment line such as `// undo x` or `// redo x2`
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
- `Detach`: move the current graph tab into an independent window
- `Dock`: move a detached graph window back into the main window
- `+`: zoom in (center-based)
- `-`: zoom out
- `Left` / `Right`: pan view
- Mouse drag: select range
- `Shift` + click with an existing selection: extend the selection edge to the clicked point
- Left `Shift` + `Left` / `Right`: move the selection start by about 1/100 of the visible range
- Right `Shift` + `Left` / `Right`: move the selection end by about 1/100 of the visible range
- `Enter`: zoom to selected range
- Selected ranges are available from graphics handles:
  - `r = fig.axes.selrange` returns `[]` or `[start end]` in the axes x-coordinate system.
  - `fig.axes.selrange = [start end]` sets the selected range.
  - `fig.axes.selrange = []` clears it.
  - `fig2.axes.selrange = fig.axes.selrange` copies it when each `axes` reference resolves to one axes.
  - Named figures also expose `fig.selrange` as the shared selection mirror for stereo/namesake plots.
- For a named plot, `x.?sel` returns the data block inside the named figure's selected range.
- Range navigation shortcuts:
  - macOS: `Ctrl+Left`: set view start to `0`
  - Windows/Linux: `Alt+Left`: set view start to `0`
  - macOS: `Ctrl+Right`: set view end to the signal end
  - Windows/Linux: `Alt+Right`: set view end to the signal end
  - macOS: `Ctrl+/`: reset to the full signal range
  - Windows/Linux: `Alt+/`: reset to the full signal range
  - macOS: `Ctrl+,`: go back to the previous range
  - Windows/Linux: `Alt+,`: go back to the previous range
  - macOS: `Ctrl+.`: go forward again after stepping back
  - Windows/Linux: `Alt+.`: go forward again after stepping back
  - When zoomed in, `Home`, `,`, or `<`: move the visible range start to `0`
  - When zoomed in, `End`, `.`, or `>`: move the visible range end to the signal end
  - When zoomed in, `/` or `?`: reset to the full signal range
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
