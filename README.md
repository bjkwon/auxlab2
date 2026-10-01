# auxlab2

Qt-based GUI application built on top of `auxe` (AUX Engine).

## Features

- Main command console with persistent `AUX> ` prompt
  - `#` lines run OS shell commands (`#pwd`, `#cd`, `#ls`, ...); see [Shell Commands](#shell-commands-)
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

### Shell Commands (`#`)

A console line whose first non-space character is `#` runs as an OS shell command.
This is an auxlab2 console feature only. It does not apply to `.aux` script files,
UDFs, or `/` debugger commands, and `#` elsewhere in a line is still the AUX
`pitchscale` operator (`y = x # 1.5`).

- Everything after `#` goes to the shell exactly as typed, including `;`, `,`, `|` and `&&`.
- Output (stdout and stderr) appears in the console as it arrives. A non-zero exit
  status is reported as `(exit code N)`.
- Input is blocked while a command runs. Press `Ctrl+C` to stop it; on macOS that is the
  physical Control key (`Cmd+C` still copies). A second `Ctrl+C`, or a command that
  ignores the first one for 2 seconds, is force-killed.
- The command gets no input (stdin is closed), so interactive programs such as
  `python`, `vim` or `top` end immediately or misbehave. Run those in a terminal.
- `#pwd` shows the working directory.
- `#cd <dir>` changes auxlab2's working directory. It accepts relative paths, `~`,
  `~/...`, `-` (previous directory), and a quoted path; `#cd` alone goes home. The new
  directory is used for relative file paths (`wave("a.wav")`, `dir`, ...) and UDF lookup.
  Cached UDFs that were found through the old directory, or that a same-named file in
  the new directory now overrides, are looked up again on their next call. UDFs open in
  the debug window keep their breakpoints. `#cd` is refused while the debugger is paused.
- A `cd` combined with shell syntax (`#cd build && make`, `#cd $HOME`) runs in the shell
  and does **not** change auxlab2's directory.

A multi-line submission (pasted, or entered with `Shift+Enter`) can mix `#` lines and
AUX code. It runs top to bottom: each `#` line on its own, and the AUX lines between them
as one chunk, exactly as if submitted alone. Execution stops at the first failure (a
non-zero exit, `Ctrl+C`, an AUX error, or a debugger pause), and the rest is reported as
skipped. A `#` line inside an AUX block (`if`/`for`/`while`/`switch`/`try`/`function` …
`end`) or inside an open bracket is rejected before anything runs. The whole
submission is saved as one history item.

```
#cd ~/sounds/experiment1
x = wave("tone440.wav");
y = x # 1.5;
#ls -l
```

Limitations:

- **Environment when launched from Finder/Dock (macOS).** GUI apps do not read your shell
  profile, so `PATH` is minimal (`/usr/bin:/bin:/usr/sbin:/sbin`). Tools installed by
  Homebrew (`/opt/homebrew/bin`) or set up in `.zshrc` may be "not found". Use full paths
  (`#/opt/homebrew/bin/ffmpeg ...`) or start auxlab2 from a terminal. The starting working
  directory is also `/` in that case. Check with `#pwd` and `#cd` where you need to be.
- **Shell differs by platform.** macOS/Linux use `/bin/sh -c`, Windows uses `cmd.exe /c`,
  so commands, quoting and built-ins differ (`ls` vs `dir`, `'...'` vs `"..."`). Shell
  lines saved in history are not portable between platforms. On Windows, `Ctrl+C` ends
  `cmd.exe` but may not stop programs it started. `#cd` follows the rules above on all
  platforms (`#cd` alone goes home; `cd /d` is accepted).

### UDF Lookup and Reloading

A UDF is called by name (`y = concat_lr(indir, "out.wav")`), not by file path. On the
first call, auxe looks for `concat_lr.aux` in the current working directory, then in
each UDF search path in order. The first file found is loaded and cached for the rest
of the session.

- To make a UDF folder available in every session, add it under
  `Settings > View Runtime Settings > UDF Paths (one per line)`. The paths are saved and
  reloaded on startup, and UDFs found through them stay cached across `#cd`.
- A same-named `.aux` file in the current directory takes priority over one on a UDF path.

**Editing a cached UDF does not reload it automatically.** auxe does not check whether
the file has changed, so later calls keep running the version loaded first. The only
exception is the UDF open in the debug window (`File > Open UDF...`): auxlab2 reloads it
when it is saved and before each console command if the file is newer. To pick up edits
to another UDF:

- open it with `File > Open UDF...` (recommended while editing; later saves are then
  picked up automatically),
- if it was found through the current directory, `#cd` elsewhere and back, which makes
  it be looked up again, or
- restart auxlab2.

`clear` removes variables only; it does not unload a UDF.

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
