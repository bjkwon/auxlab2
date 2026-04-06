# Graphics Handle Implementation Plan

## Goal

Add MATLAB-style graphics handles to auxlab2 with four handle types:

- `figure`
- `axes`
- `line`
- `text`

These handles should support:

- parent/child hierarchy
- property access and mutation
- current-handle pseudo-vars `gcf` and `gca`
- unnamed plots via `plot(...)`
- named plots via `figure("varname")`
- deletion and handle invalidation
- stereo waveform layout with stable handle identity across `F2`

## Design Summary

### Handle hierarchy

- Root
  - nominal program root; no deep implementation needed
- Figure
  - parent: root
  - children: `axes`, `text`
- Axes
  - parent: `figure`
  - children: `line`, optionally `text`
- Line
  - parent: `axes`
- Text
  - parent: `figure` or `axes`

### Current handles

- `gcf`
  - read-only pseudo-var
  - most recently created or focused figure
  - updated by graphics creation/focus and GUI focus
  - becomes `TYPEBIT_NULL` only if the current figure itself is deleted and no later action sets a new one
- `gca`
  - read-only pseudo-var
  - current axes in the current figure
  - for stereo waveform plots, defined as the axes for the left channel
  - becomes `TYPEBIT_NULL` only if the current axes itself is deleted and no later action sets a new one

Deleting a non-current figure or axes must not change `gcf` or `gca`.

### Named vs unnamed figures

- Unnamed figure
  - created by `plot(...)`
  - title: `Figure N`
  - plotted data is a snapshot
  - later source-variable changes do not update it
- Named figure
  - created by `figure("x")`
  - associated with the source variable/path
  - later source-variable changes may recompute linked `xdata`/`ydata`
  - user-overridden style/layout properties are preserved

### Coordinate conventions

For MATLAB compatibility:

- `figure.pos`
  - `[x y w h]`
  - pixel units
  - referenced from bottom-left of the screen
- `axes.pos`
  - `[x y w h]`
  - normalized units
  - referenced from bottom-left of the parent figure
- `text.pos`
  - stored as `[x y 0 0]` in v1
  - normalized units
  - referenced from bottom-left of the parent (`figure` or `axes`)
- `line.pos`
  - ignored / not meaningful in v1

### Stereo behavior

For stereo waveform plots:

- create two axes and two line objects at creation time
- left-channel axes is the logical current axes (`gca`)
- `F2` toggles layout/visibility only
- object identity must remain stable across `F2`
- no axes/line recreation during `F2`

## Property Model

### Common graphics properties

- `pos`
- `color`
- `userdata`
- `tag`
- `visible`
- `type`
- `parent`
- `children`

### Figure

- common properties only

### Axes

- `box`
- `linewidth`
- `xlim`
- `ylim`
- `fontname`
- `fontsize`
- `xscale`
- `yscale`
- `xtick`
- `ytick`
- `xticklabel`
- `yticklabel`
- `xgrid`
- `ygrid`

### Line

- `xdata`
- `ydata`
- `linewidth`
- `linestyle`
- `marker`
- `markersize`

### Text

- `fontname`
- `fontsize`
- `string`

### Property semantics

- Read-only:
  - `type`
  - `parent`
  - `children`
- Writable:
  - all remaining applicable properties
- Invalid assignments:
  - raise an error
- Invalid property access for a given handle type:
  - raise an error

### Auto vs manual property state

Some axes/data properties begin in auto mode and become manual after explicit user assignment:

- `xlim`
- `ylim`
- `xtick`
- `ytick`
- `xticklabel`
- `yticklabel`

Named-plot refresh may recompute only still-auto properties plus linked data.

## Builtin Function Plan

### `figure`

Forms:

- `y = figure()`
- `y = figure(h_or_pos)`

Behavior:

- `figure()`
  - create new empty figure at default position
  - set title to `Figure N`
  - return figure handle
  - set `gcf`
- `figure(hFigure)`
  - focus existing figure
  - return same figure handle
  - update `gcf`
  - update `gca` to that figure's current axes if available
- `figure(pos)`
  - create new empty figure at given position
  - no default axes created
  - return figure handle
  - set `gcf`

Also support named figure creation:

- `figure("x")`
  - create named figure associated with variable/path `x`
  - initialize graph content using current plotting defaults
  - return figure handle
  - set `gcf` and `gca`

### `plot`

Forms:

- `y = plot(x, y_opt="", opt="")`
- `y = plot(handle, x, y_opt="", opt="")`
- `y = handle.plot(x, y_opt="", opt="")`

Behavior:

- returns figure handle
- creates unnamed plot
- updates `gcf` and `gca`
- if no handle is given:
  - create new figure and default axes
- if figure handle is given:
  - reuse current axes in that figure
- if axes handle is given:
  - use that axes
- current v1 `nextplot` behavior:
  - replace

Data rules:

- `plot(x)`:
  - plot `x` against implicit index for non-audio
  - plot `x` against time for audio
- `plot(x, y)`:
  - plot `y` as a function of `x`
  - error if `y` is audio
- stereo `plot(x)`:
  - create two axes and two lines

Style-string v1:

- fixed order: `color-marker-linestyle`
- accepted color chars:
  - `r`, `g`, `b`, `y`, `c`, `m`, `h`, `k`
- marker empty may be `""` or `" "`

### `axes`

Forms:

- `y = axes(h_or_pos)`
- `y = hFigure.axes()`

Behavior:

- `axes(hAxes)`
  - make existing axes current
  - return same axes handle
  - set `gca`
  - set `gcf` to its parent figure
- `axes(pos)`
  - create new axes in current figure
  - if no current figure exists, create one first
  - return new axes handle
  - set `gca` and `gcf`
- `fig.axes()`
  - create new default axes in the figure
  - return new axes handle
  - set `gca` and `gcf`

### `text`

Forms:

- `y = text(pos_x, pos_y, string)`
- `y = text(handle, pos_x, pos_y, string)`
- `y = handle.text(pos_x, pos_y, string)`

Behavior:

- returns text handle
- if no handle is given:
  - use current figure
  - create one if needed
- if handle is a figure:
  - create figure-child text
- if handle is an axes:
  - create axes-child text
- store `text.pos` as `[x y 0 0]`

### `line`

Forms:

- `y = line(x, y_opt="")`
- `y = line(handle, x, y_opt="")`
- `y = handle.line(x, y_opt="")`

Behavior:

- returns line handle
- low-level primitive
- appends to axes children instead of replace behavior
- if no handle is given:
  - create new figure and default axes
- if figure handle is given:
  - use current/default axes in that figure
- if axes handle is given:
  - add line directly to that axes

Data rules:

- `line(x)`:
  - use implicit index for non-audio
  - use time for audio
- `line(x, y)`:
  - plot `y` as a function of `x`

Repeated `line(ax, ...)` calls accumulate children.

### `delete`

Forms:

- `y = delete(object)`
- `y = object.delete()`

Behavior:

- deletes valid existing graphics handle
- returns empty object
- the variable used to call delete becomes `TYPEBIT_NULL`
- invalid or stale handle raises an error
- deleting a figure deletes descendants
- deleting an axes deletes descendant lines/text

## Implementation Order

### Phase 1: Core graphics object model

1. Add internal C++ graphics object classes / structs for:
   - figure
   - axes
   - line
   - text
2. Add stable object identity and parent/child links.
3. Add handle registry / lookup owned by the UI/runtime layer.
4. Add invalidation state for deleted handles.

### Phase 2: Engine handle bridging

1. Decide how auxlab2 graphics handles are represented using aux_engine GO semantics.
2. Add read-only pseudo-vars:
   - `gcf`
   - `gca`
3. Add property get/set plumbing between AUX objects and UI graphics objects.
4. Ensure deleted handles become invalid and surface as `TYPEBIT_NULL` where required.

### Phase 3: SignalGraphWindow refactor

1. Refactor `SignalGraphWindow` so rendering uses figure/axes/line/text object state instead of direct ad hoc fields only.
2. Preserve current visual defaults from auxlab2.
3. Keep stereo layout toggle as a property/layout update, not object recreation.
4. Add focus/click hooks that update `gcf` and `gca`.

### Phase 4: Builtin functions

1. Implement `figure`
2. Implement `axes`
3. Implement `line`
4. Implement `text`
5. Implement `plot`
6. Implement `delete`

Recommended order is from low-level creation to high-level convenience.

### Phase 5: Named-plot refresh

1. Track source-variable/path binding for named figures.
2. Detect variable updates in the UI refresh path.
3. Recompute linked `xdata`/`ydata`.
4. Recompute only still-auto properties.
5. Preserve manual/user-overridden properties.

### Phase 6: Validation

1. Manual tests for:
   - figure creation/focus
   - axes creation/selection
   - text creation under figure and axes
   - line accumulation
   - plot replace semantics
   - stereo `F2` stability
   - named plot refresh
   - handle deletion from API and GUI
2. Error-path tests for:
   - invalid handles
   - invalid property access
   - invalid property assignment
   - invalid style strings
   - invalid plot argument combinations

## Open Implementation Notes

- aux_engine already has GO-related concepts that should be reused instead of inventing pure value-copy structs for handles.
- Public coordinate semantics should follow MATLAB bottom-left conventions even if Qt rendering remains top-left internally.
- `plot(...)` and `figure("x")` share some creation logic but differ in named-vs-unnamed refresh semantics.
- Keep v1 conservative:
  - no exposed `nextplot` property yet
  - no flexible style-string parser beyond fixed order
  - no direct reparenting by assigning `parent`

## Borrow From Old Graffy

There is prior Windows-only graphics code in the old `auxlab` repository under `graffy/`.
That code should be used as a behavioral and architectural reference, not as a direct code-port target.

Useful ideas to borrow:

- graphics-object hierarchy and stable handle identity
- GO-backed property storage for `figure`, `axes`, `line`, and `text`
- `gcf` / `gca` update patterns
- builtin-function semantics for:
  - `figure`
  - `axes`
  - `plot`
  - `line`
  - `text`
  - `delete`
- style-string parsing for `plot`
- bottom-left normalized coordinate semantics for axes/text positioning
- delete/invalidation edge-case handling

Parts not to port directly:

- Win32 / Win32++ dialog and message-loop code
- `HWND`-centric window ownership and painting
- old `PlotDlg` rendering/event infrastructure
- legacy naming/property differences that conflict with current auxlab2 design

Implementation stance:

- reimplement graphics in auxlab2 using Qt-native structures and rendering
- reuse old `graffy` semantics where they align with the current design
- prefer the current design doc when old `graffy` behavior and auxlab2 plans differ
