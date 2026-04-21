# Test Plan: Graphics and Play/Record

This plan covers:

- graphics functions
- graphics handle properties
- play/record functions
- playback/recording handle properties
- command-line execution
- `auxlab2` GUI execution

The plan is based on the current implementation in:

- `/Users/bkwon/dev/aux_engine/src/func/graphics_builtin.cpp`
- `/Users/bkwon/dev/aux_engine/src/func/play_builtin.cpp`
- `/Users/bkwon/dev/aux_engine/src/func/record_builtin.cpp`
- `/Users/bkwon/dev/auxlab2/src/MainWindow.cpp`
- `/Users/bkwon/dev/auxlab2/src/SignalGraphWindow.cpp`

## 1. Objectives

- Verify each supported graphics builtin works with its accepted call forms.
- Verify each supported graphics property can be read, written, and rejected correctly when invalid.
- Verify handle identity and parent/child relationships stay correct across focus, deletion, and stereo-layout changes.
- Verify `play`, `pause`, `resume`, `stop`, and `record` behave correctly in both normal and edge cases.
- Verify runtime handle members update correctly while playback or recording is active.
- Verify command-line behavior and `auxlab2` GUI behavior match the intended frontend split.

## 2. Frontends Under Test

Use two frontends:

1. `aux2` command line
2. `auxlab2`

For `auxlab2`, test both:

- command console entry
- GUI interaction on variable browser and graph windows

## 3. Expected Frontend Split

### `aux2` command line

- `play`, `pause`, `resume`, `stop` may work if the app is built with playback support.
- `record`, `pause`, `resume`, `stop` for recording may work if the app is built with record support.
- graphics builtins should fail gracefully with a backend-not-available error, not with crash or unregistered-function behavior.

### `auxlab2`

- graphics builtins and graphics properties should work through the GUI backend.
- `play`, `record`, async record callbacks, and graphics updates from callbacks should work.
- GUI window focus should drive `gcf` and `gca`.

## 4. Test Setup

Use these prerequisites:

- a build of `aux2`
- a build of `auxlab2`
- at least one working audio output device
- at least one working audio input device
- microphone permission granted to `auxlab2`

Suggested sample data:

- mono audio: a short sine or noise buffer
- stereo audio: two-channel signal with visibly different left/right channels
- non-audio vector: `[1 3 2 5 4]`
- x/y vector pair: `x=[0 1 2 3 4]`, `y=[10 20 15 25 22]`

Suggested helper commands:

```aux
fs=16000;
t=(0:15999)/fs;
mono=sin(2*pi*440*t);
st=[mono; .4*sin(2*pi*660*t)];
x=[0 1 2 3 4];
y=[10 20 15 25 22];
v=[1 3 2 5 4];
```

## 5. Graphics: Command-Line Tests

Run these in `aux2`.

### G-CLI-01 Graphics unavailable path

- Execute `figure()`, `axes()`, `plot(v)`, `line(x,y)`, `delete(gcf)`, `repaint(gca)`.
- Expected:
  - each command returns a clear graphics-backend-unavailable error
  - no crash
  - no hang
  - no unregistered-function error

### G-CLI-02 Current-handle queries without GUI

- Execute `gcf` and `gca`.
- Expected:
  - either empty/null-style result or a clear backend/current-handle error path
  - no crash

## 6. Graphics: `auxlab2` Console Tests

Run these in the `auxlab2` command console.

### G-AUX-01 `figure` call forms

- Test `h=figure()`
- Test `h=figure([100 100 640 480])`
- Test `h2=figure(h)`
- Test `hn=figure("mono")`
- Expected:
  - handle returned
  - `gcf` updated to target figure
  - named figure reuses existing window when called again with same source path

### G-AUX-02 `axes` call forms

- Test `ax=axes()`
- Test `ax2=axes([.13 .11 .775 .815])`
- Test `ax3=axes(ax2)`
- Test `ax4=h.axes()`
- Expected:
  - axes handle returned
  - `gca` updated
  - `gcf` points to parent figure

### G-AUX-03 `plot` call forms

- Test `hp=plot(v)`
- Test `hp=plot(v,"r--")`
- Test `hp=plot(ax,v)`
- Test `hp=plot(ax,v,"go:")`
- Test method form `ax.plot(v)` and `ax.plot(v,"b+")`
- Expected:
  - returned handle is a figure handle
  - target figure/axes selected correctly
  - style string applied to lines

### G-AUX-04 `line` call forms

- Test `ln=line(v)`
- Test `ln=line(x,y)`
- Test `ln=line(ax,v)`
- Test `ln=line(ax,x,y)`
- Test method form `ax.line(v)` and `ax.line(x,y)`
- Expected:
  - returned handle is a line handle
  - repeated `line(ax,...)` appends new children instead of replacing

### G-AUX-05 `text` call forms

- Test `tx=text(.2,.8,"hello")`
- Test `tx=text(ax,.2,.8,"hello axes")`
- Test `tx=h.text(.1,.1,"hello fig")`
- Expected:
  - returned handle is a text handle
  - parent is figure or axes as requested
  - text is visible in the graph window

### G-AUX-06 Current handle behavior

- Create two figures and several axes.
- Click each graph window and switch focus.
- Query `gcf` and `gca` after each focus change.
- Expected:
  - focused figure becomes `gcf`
  - current axes in that figure becomes `gca`

### G-AUX-07 Delete and repaint

- Delete a line, then text, then axes, then figure.
- Re-query deleted handles and parent `.children`.
- Call `repaint(handle)` on live handles.
- Expected:
  - deleted handle lookup fails cleanly
  - parent `children` updates correctly
  - deleting a bound variable handle clears the variable
  - repaint causes redraw without changing properties

### G-AUX-08 Stereo plot stability

- Run `hs=plot(st)`
- Record figure id, left/right axes ids, line ids, `gca`
- Press `F2` through all display modes
- Re-check ids and children
- Expected:
  - handle ids do not change across `F2`
  - left-channel axes remains logical `gca`
  - only layout/visibility changes

## 7. Graphics Property Matrix

Test every property with:

- read existing value
- write valid value
- read back and compare
- verify GUI redraw if visually relevant
- write invalid value
- access on wrong handle type

### Figure properties

Supported reads/writes:

- `pos`
- `color`
- `visible`

Supported read-only/reference:

- `type`
- `parent`
- `children`

Test cases:

- `h.pos`
- `h.pos=[120 120 700 420]`
- `h.color=[.9 .9 .95]`
- `h.visible=0`
- `h.visible=1`
- `h.type`
- `h.parent`
- `h.children`

Negative cases:

- `h.pos=[1 2 3]`
- `h.color=[1 0]`
- `h.color="red"`
- `h.type="figure"`
- `h.parent=1`
- `h.children=[]`
- `h.fontsize=12`

### Axes properties

Supported reads/writes:

- `pos`
- `color`
- `visible`
- `box`
- `linewidth`
- `xlim`
- `ylim`
- `fontname`
- `fontsize`
- `xscale`
- `yscale`
- `xgrid`
- `ygrid`

Supported read-only/reference:

- `type`
- `parent`
- `children`

Test cases:

- `ax.pos=[.15 .15 .7 .7]`
- `ax.color=[1 1 .95]`
- `ax.visible=0`, then `1`
- `ax.box=0`, then `1`
- `ax.linewidth=3`
- `ax.xlim=[0 4]`
- `ax.ylim=[0 30]`
- `ax.fontname="Helvetica"`
- `ax.fontsize=14`
- `ax.xscale="linear"`
- `ax.yscale="linear"`
- `ax.xgrid=0`, then `1`
- `ax.ygrid=0`, then `1`

Negative cases:

- `ax.xlim=[1]`
- `ax.ylim="bad"`
- `ax.box="yes"`
- `ax.fontname=5`
- `ax.fontsize="12"`
- `ax.parent=0`
- `ax.children=[]`
- `ax.marker="o"`

### Line properties

Supported reads/writes:

- `pos`
- `color`
- `visible`
- `xdata`
- `ydata`
- `linewidth`
- `linestyle`
- `marker`
- `markersize`

Supported read-only/reference:

- `type`
- `parent`
- `children`

Test cases:

- `ln.color=[1 0 0]`
- `ln.visible=0`, then `1`
- `ln.xdata=[0 1 2 3 4]`
- `ln.ydata=[4 3 2 1 0]`
- `ln.linewidth=4`
- `ln.linestyle="--"`
- `ln.marker="o"`
- `ln.markersize=8`

Negative cases:

- `ln.xdata=[1 2 3]` when `ydata` length is different
- `ln.ydata=[1 2]` when `xdata` length is different
- `ln.linewidth="thick"`
- `ln.marker=1`
- `ln.parent=0`
- `ln.box=1`

### Text properties

Supported reads/writes:

- `pos`
- `color`
- `visible`
- `fontname`
- `fontsize`
- `string`

Supported read-only/reference:

- `type`
- `parent`
- `children`

Test cases:

- `tx.pos=[.3 .7 0 0]`
- `tx.color=[0 .2 .8]`
- `tx.visible=0`, then `1`
- `tx.fontname="Helvetica"`
- `tx.fontsize=16`
- `tx.string="updated"`

Negative cases:

- `tx.string=5`
- `tx.fontsize="16"`
- `tx.parent=h`
- `tx.children=[]`
- `tx.xdata=[1 2 3]`

## 8. Graphics Property Access by Path

Exercise path-based access and array/reference behavior:

- `gcf.color`
- `gca.xlim`
- `h.children`
- `h.children{1}`
- `ax.parent`
- `ln.parent`

Expected:

- scalar handle references resolve correctly
- children arrays can be indexed
- invalid index returns empty handle array or clean error

## 9. Graphics GUI Interaction Tests

Use mouse and keyboard in `auxlab2`.

### G-GUI-01 Figure/axes visual refresh

- Change `figure.color`, `axes.color`, `axes.visible`, `line.visible`, `text.visible`
- Expected:
  - graph window redraws immediately
  - hidden objects disappear
  - restoring visibility redraws them correctly

### G-GUI-02 Range controls with property updates

- Set `ax.xlim` and `ax.ylim` by command
- Then use drag-zoom, `Enter`, `Cmd+/`, `Cmd+,`, `Cmd+.`
- Expected:
  - window navigation still works after property assignment
  - no stale range state

### G-GUI-03 Variable-browser integration

- Plot named figure with `figure("mono")`
- Change source variable data
- Refresh variable browser and graph
- Expected:
  - named figure remains bound to source path
  - supported auto-refreshed data updates correctly

### G-GUI-04 Scope-aware graphics

- Open windows in base scope and child UDF debug scope
- Pause in debugger
- Resume and exit child scope
- Expected:
  - out-of-scope windows become inactive while paused in other scope
  - child-scope windows close when child scope ends

## 10. Playback Function Tests

Run in both `aux2` and `auxlab2` where supported. For GUI-specific progress/refresh checks, use `auxlab2`.

### P-01 `play` accepted forms

- `ph=play(mono)`
- `ph=play(mono,2)`
- `ph=play(ph,mono)`
- `ph=play(ph,mono,3)`
- method forms `mono.play()` and `ph.play(mono)`

Expected:

- handle returned
- handle members include `fs`, `dur`, `repeat_left`, `prog`
- queued play on active handle extends duration and repeat count

### P-02 Playback handle member checks

Verify:

- `ph.fs`
- `ph.dur`
- `ph.repeat_left`
- `ph.prog`

Checks:

- immediately after start
- mid-playback
- after last repeat finishes
- after `stop`

Expected:

- `prog` increases toward `100`
- `repeat_left` decrements after each completed segment
- `stop` forces `repeat_left=0`, `prog=100`

### P-03 Playback control

- `pause(ph)`
- `resume(ph)`
- `stop(ph)`
- method forms `ph.pause`, `ph.resume`, `ph.stop`

Expected:

- pause halts audible progress
- resume continues from paused position
- stop ends session and future control on inactive handle fails cleanly

### P-04 Playback error handling

- `play(1)`
- `play(mono,0)`
- `play(mono,1.5)`
- `pause(123456789)`
- `resume(123456789)`
- `stop(123456789)`

Expected:

- clear validation errors
- no crash

### P-05 Playback plus GUI variable audio preview

In `auxlab2`:

- press `Space` on an audio variable row
- pause/resume from same UI path
- stop via selection change or explicit stop
- start `play(mono)` while variable preview is active

Expected:

- variable preview and playback-handle path do not leave stale audio state
- progress values remain correct for handle-driven playback

## 11. Record Function Tests

### R-01 Synchronous record

- `r=record()`
- `r=record(0,500,1)`
- `r=record(0,500,2)`

Expected:

- returns audio object, not handle
- mono returns single-channel audio
- stereo returns chained second channel
- duration and sample rate are plausible

### R-02 Sync record argument validation

- `record(-1,500,1)`
- `record(0,0,1)`
- `record(0,-1,1)`
- `record(0,500,3)`
- `record(0,500,"2")`

Expected:

- clear validation errors for each invalid argument

### R-03 Async record start

- `rh=record().callback`
- `rh=record(0,1000,1,100).callback`
- `rh=record(0,-1,1,100).callback`

Expected:

- returns recording handle
- handle members include:
  - `type="audio_record"`
  - `id`
  - `devID`
  - `fs`
  - `channels`
  - `dur`
  - `block`
  - `durRec`
  - `durLeft`
  - `prog`
  - `active`
  - `paused`

### R-04 Async record member updates

While recording is active, sample several times:

- `rh.durRec`
- `rh.durLeft`
- `rh.prog`
- `rh.active`
- `rh.paused`

Expected for finite duration:

- `durRec` increases
- `durLeft` decreases to `0`
- `prog` increases to `100`
- `active` remains `1` until stopped/completed
- `paused` toggles only on pause/resume

Expected for indefinite duration:

- `durRec` increases
- `durLeft` stays `0`
- `prog` stays `0`

### R-05 Async record control

- `pause(rh)`
- `resume(rh)`
- `stop(rh)`
- method forms `rh.pause`, `rh.resume`, `rh.stop`

Expected:

- pause sets `paused=1`
- resume sets `paused=0`
- stop sets `active=0`
- finite record stop sets `durLeft=0`, `prog=100`

### R-06 Async record callback behavior

Use a callback UDF that:

- receives open event
- receives one or more audio blocks
- emits one or more graphics handles

Checks:

- callback open event delivered once with `callback_index=0`
- block callbacks increment `callback_index`
- callback outputs attach to the record handle
- graphics outputs from callback are discoverable and refreshed

### R-07 Async record errors

- invalid callback name
- no input device
- out-of-range device id
- stereo request on mono-only device
- microphone permission denied
- callback runtime failure

Expected:

- start fails cleanly, or active session stops cleanly after callback failure
- console shows callback error message
- no crash or leaked active handle state

## 12. GUI Record/Graphics Integration

These are `auxlab2` only.

### RG-01 Callback-created graphics update while recording

- callback creates figure/axes/line handles on open
- callback updates `xlim` and `ylim` during streaming

Expected:

- graph windows appear and refresh during active recording
- `syncRecordCallbackGraphicsOutputs` keeps GUI range in sync with callback-written properties

### RG-02 Stop flush behavior

- finite and indefinite async record
- stop before final block boundary

Expected:

- final buffered samples are flushed to callback once more
- handle members transition to inactive cleanly
- final graphics update is visible

## 13. Unsupported/Gap Regression Checks

The current implementation should reject these cleanly.

### Graphics properties planned but not currently exposed in `auxlab2`

- `tag`
- `userdata`
- `xtick`
- `ytick`
- `xticklabel`
- `yticklabel`

For each applicable handle:

- attempt read
- attempt write
- expect clear unsupported-property error

### Invalid handle-type/property pairings

- `figure.linewidth`
- `axes.marker`
- `line.fontsize`
- `text.xlim`

Expected:

- clear wrong-handle-type error

## 14. Regression Exit Criteria

A run passes when:

- no crash, assert, or deadlock occurs
- every supported call form works
- every supported property round-trips correctly
- every unsupported property is rejected cleanly
- `gcf` and `gca` track focus/current-object behavior correctly
- playback and recording handle members update correctly over time
- async callback failures fail safely
- `aux2` graphics path degrades gracefully

## 15. Recommended Automation Split

Automate first:

- `aux2` negative graphics tests
- engine-level record callback regression tests
- property parse/validation tests
- playback/record handle member transition tests where backend hooks can be stubbed

Keep manual for now:

- `auxlab2` focus-driven `gcf`/`gca`
- graph redraw verification
- stereo `F2` layout stability
- microphone permission/device selection behavior
- variable-browser and graph-window audio interactions
