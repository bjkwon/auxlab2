# AUXLAB2 Tutorial: GUI Workflow and Debugging

This guide assumes you already know AUX language syntax and focuses on how to use AUXLAB2 as a GUI workbench and debugger.

## 1. Start AUXLAB2

If you already built the app:

```bash
/Users/bkwon/dev/auxlab2/build/auxlab2
```

Main layout:

- Left: command console (`AUX> ` prompt)
- Center: variable browser
  - `Audio Objects`
  - `Non-Audio Objects`
- Right: history list
- Separate window (optional): `UDF Debug Window`

## 2. Daily GUI Workflow

### Run commands in the console

- Type at the last `AUX> ` line and press `Enter` to execute.
- Command history:
  - `Up` / `Down` navigate previous commands.
  - `Ctrl+R` reverse search.
  - `Ctrl+A`, `Ctrl+E`, `Ctrl+U`, `Ctrl+K`, `Ctrl+P`, `Ctrl+N` work in the current input line.
- If a command ends with `;`, successful output echo is suppressed.

### Reuse command history quickly

In the History pane:

- `Enter` on a row: inject into console (editable before running).
- Double-click: inject and execute immediately.

History persists across app restarts.

### Inspect variables from the variable browser

Audio rows show:

- Name
- dbRMS
- Size
- Signal interval preview

Non-audio rows show:

- Name
- Type tag (`SCLR`, `VECT`, `TEXT`, `BIN`, etc.)
- Size
- Content preview

Useful actions from the variable panes:

- `Enter`:
  - Audio object: open/focus signal graph.
  - Non-audio `VECT`: open/focus signal graph.
- `Space` on audio object: play/pause/resume audio preview.
- Double-click:
  - Signal-like object: signal table window.
  - String (`TEXT`): text object window.
  - Binary (`BIN`): hex+ASCII dump window.

## 3. Signal Graph Window

Open from variable list with `Enter`.

### Navigation and view control

- `+`, `=`, `Up`: zoom in
- `-`, `_`, `Down`: zoom out
- `Left` / `Right`: pan view
- Mouse drag: select a range
- `Enter`: zoom to selected range

### Stereo display modes

- `F2` cycles:
  1. Vertical split channels
  2. Overlay (blue/red)
  3. Overlay (red/blue)

### Audio playback from graph

- `Space`: play current selection (or current view if no selection)
- `Space` while playing: pause/resume
- `Esc`: stop

A moving playhead line is shown during playback.

### FFT overlay

- `F4`: toggle FFT overlay
- `Shift+F4`: reset FFT pane offsets

Tip: FFT panes can be repositioned by pressing and holding in their left margin, then dragging.

## 4. Table/Text/Binary Windows

- Signal table: tabular sample view (up to 5000 rows), channels as columns.
- Text object window: read-only full text view.
- Binary object window: offset + hex + ASCII dump.

## 5. Window Management Shortcuts

Most shortcuts are from the `Window` menu.

On macOS use `Cmd` where shown as primary modifier; on Windows/Linux use `Ctrl`.

- Primary+`Tab`: next scoped window
- Primary+`Shift+Tab`: previous scoped window
- Primary+`1..9`: focus window by index
- Primary+`G` / Primary+`Shift+G`: next/previous graph window
- Primary+`T` / Primary+`Shift+T`: next/previous table window
- Primary+`` ` ``: toggle last two focused windows
- Primary+`Shift+W`: close all windows in current scope

Each child window also supports close shortcut (`Cmd+W` or `Ctrl+W`).

## 6. UDF Debugging: End-to-End

## 6.1 Open a UDF in the debugger

Use:

- `File > Open UDF...`
- or `File > Open Recent`

This loads/registers the UDF and opens the `UDF Debug Window`.

You can also show/hide the debug window via:

- `View > Show Debug Window` (`Ctrl+Alt+D`)

## 6.2 Set breakpoints

From the debug editor:

- Move cursor to a line and press `F9`.
- Or use `Debug > Toggle Breakpoint`.

Breakpoint markers:

- Red dot in line-number gutter
- Highlighted line background in the editor

Important: breakpoints are tied to the currently active UDF tab.

## 6.3 Run code and pause

Run your normal AUX command in the main console that executes the UDF.

When execution hits a breakpoint:

- Debug state switches to `paused`.
- Debug window auto-focuses and jumps to current file/line.
- Paused line is highlighted.
- Variable panes reflect the active paused (child) context.

## 6.4 Step and continue

Use either Debug menu, function keys, or debug window buttons:

- `F5`: Continue
- `F10`: Step Over
- `F11`: Step In
- `Shift+F11`: Step Out
- `Shift+F5`: Abort to base scope

During pause, step/continue actions are enabled; when not paused, they are disabled.

## 6.5 Edit-and-save while debugging

In debug editor:

- `Cmd+S` / `Ctrl+S` saves current tab.
- Unsaved tabs prompt when closing.

This is useful for rapid fix-and-rerun loops.

## 7. Scope-Aware Behavior (Important)

AUXLAB2 windows are tracked per workspace/debug scope.

- While paused inside a child UDF scope:
  - windows from other scopes are deactivated.
  - variable view reflects the active paused scope.
- When child scope ends and execution returns to base:
  - child-scope windows are closed.
- If a variable disappears from active scope:
  - related graph/table/text window is closed automatically.

This prevents stale visualizations from unrelated contexts.

## 8. Runtime Settings for Better Debug Sessions

Open `Settings > View Runtime Settings` to tune:

- Sampling rate
- Display precision
- Display limits (X/Y/bytes/string)
- UDF search paths (one per line)

Settings are persisted and reloaded on startup.

## 9. Practical Debug Recipe

1. Open UDF file in debug window.
2. Set breakpoints (`F9`) at entry and suspect lines.
3. Execute UDF call from console.
4. At pause, inspect variables in the side panes.
5. Open graph/table windows for key variables.
6. Step (`F10`/`F11`) while watching values/graphs update.
7. Continue (`F5`) or abort (`Shift+F5`) as needed.
8. Edit UDF, save, rerun, and iterate.

## 10. Troubleshooting

- `Open a UDF file first` when toggling breakpoint:
  - Load a UDF via `File > Open UDF...`.
- Breakpoint key does nothing:
  - Ensure debug window/editor has focus and a valid line is selected.
- No graph opens on `Enter`:
  - Selected variable may not be signal-displayable in current scope.
- Audio preview silent:
  - Verify the selected object is audio and contains samples.
