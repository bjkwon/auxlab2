# Manual Checklist: `auxlab2` Graphics and Play/Record

This checklist is for manual validation in the `auxlab2` GUI.

It focuses on:

- graphics builtins and graphics handles
- graphics property read/write behavior
- GUI interaction for graph windows
- audio playback handles
- synchronous and async recording
- async recording callback integration with graphics

## 1. Test Environment

Confirm all of the following before starting:

- `auxlab2` is built and launches successfully
- at least one audio output device is available
- at least one audio input device is available
- microphone permission is granted to `auxlab2`
- the command console is responsive
- graph windows can open normally

Launch:

```bash
/Users/bkwon/dev/auxlab2/build/auxlab2
```

## 2. Basic Test Data

Enter these in the `auxlab2` command console:

```aux
fs=16000;
t=(0:15999)/fs;
mono=sin(2*pi*440*t);
st=[mono; .4*sin(2*pi*660*t)];
x=[0 1 2 3 4];
y=[10 20 15 25 22];
v=[1 3 2 5 4];
```

Expected:

- variables appear in the variable panes
- `mono` is shown as audio
- `st` is shown as stereo audio
- `x`, `y`, `v` are available for plotting

## 3. Graphics Builtins

### 3.1 `figure`

Run:

```aux
h=figure([100 100 640 480])
hn=figure("mono")
h2=figure(h)
```

Check:

- `h` is created as a handle
- a graph window appears for `hn`
- `figure(h)` focuses the existing figure instead of creating a duplicate
- no error is shown

### 3.2 `axes`

Run:

```aux
ax=axes([.13 .11 .775 .815])
ax2=h.axes()
ax3=axes(ax2)
```

Check:

- each command returns a handle
- the current figure is updated as expected
- `axes(ax2)` focuses/reselects existing axes

### 3.3 `plot`

Run:

```aux
hp1=plot(v)
hp2=plot(v,"r--")
hp3=plot(ax,v)
hp4=ax.plot(v,"go:")
```

Check:

- each command succeeds
- returned value is a figure handle
- style changes are visible
- target figure/axes behavior matches the call form

### 3.4 `line`

Run:

```aux
ln1=line(v)
ln2=line(x,y)
ln3=line(ax,x,y)
ln4=ax.line(v)
```

Check:

- each command returns a line handle
- repeated `line(ax,...)` calls append to axes instead of replacing the plot
- line color/style/marker changes are visible if applicable

### 3.5 `text`

Run:

```aux
tx1=text(.2,.8,"hello")
tx2=text(ax,.2,.7,"axes text")
tx3=h.text(.1,.1,"figure text")
```

Check:

- each command returns a text handle
- text is visible in the figure
- text lands in the correct parent figure or axes

### 3.6 `delete` and `repaint`

Run:

```aux
repaint(h)
delete(tx3)
delete(ln4)
```

Check:

- `repaint` causes redraw without errors
- deleted objects disappear
- later access to deleted handles fails cleanly

## 4. Current Handle Tracking

Create two figure windows and click between them.

Run after each focus change:

```aux
gcf
gca
```

Check:

- `gcf` follows the focused graph window
- `gca` follows the current axes in that figure
- focus changes in the GUI are reflected in the console results

## 5. Graphics Property Checklist

For each property below:

1. read the current value
2. assign a valid value
3. read it again
4. confirm visual effect if relevant
5. try an invalid value
6. confirm a clean error is reported

### 5.1 Figure properties

Run:

```aux
h.pos
h.color
h.visible
h.type
h.parent
h.children
```

Then test valid writes:

```aux
h.pos=[120 120 700 420]
h.color=[.92 .92 .97]
h.visible=0
h.visible=1
```

Then test invalid writes:

```aux
h.pos=[1 2 3]
h.color=[1 0]
h.type="figure"
h.children=[]
```

Check:

- `pos`, `color`, `visible` update correctly
- figure moves/resizes when `pos` changes
- background changes when `color` changes
- hidden figure becomes invisible or non-displayed as implemented
- invalid assignments fail cleanly

### 5.2 Axes properties

Run:

```aux
ax.pos
ax.color
ax.visible
ax.box
ax.linewidth
ax.xlim
ax.ylim
ax.fontname
ax.fontsize
ax.xscale
ax.yscale
ax.xgrid
ax.ygrid
ax.type
ax.parent
ax.children
```

Then valid writes:

```aux
ax.pos=[.15 .15 .7 .7]
ax.color=[1 1 .95]
ax.visible=0
ax.visible=1
ax.box=0
ax.box=1
ax.linewidth=3
ax.xlim=[0 4]
ax.ylim=[0 30]
ax.fontname="Helvetica"
ax.fontsize=14
ax.xscale="linear"
ax.yscale="linear"
ax.xgrid=0
ax.xgrid=1
ax.ygrid=0
ax.ygrid=1
```

Then invalid writes:

```aux
ax.xlim=[1]
ax.ylim="bad"
ax.box="yes"
ax.fontname=5
ax.marker="o"
```

Check:

- axes layout and colors update visually
- x/y range changes are reflected immediately
- grid and box toggle correctly
- invalid values fail cleanly

### 5.3 Line properties

Run:

```aux
ln2.color
ln2.visible
ln2.xdata
ln2.ydata
ln2.linewidth
ln2.linestyle
ln2.marker
ln2.markersize
ln2.type
ln2.parent
ln2.children
```

Then valid writes:

```aux
ln2.color=[1 0 0]
ln2.visible=0
ln2.visible=1
ln2.xdata=[0 1 2 3 4]
ln2.ydata=[4 3 2 1 0]
ln2.linewidth=4
ln2.linestyle="--"
ln2.marker="o"
ln2.markersize=8
```

Then invalid writes:

```aux
ln2.xdata=[1 2 3]
ln2.ydata=[1 2]
ln2.linewidth="thick"
ln2.marker=1
ln2.box=1
```

Check:

- line redraws correctly after each valid change
- mismatched `xdata` / `ydata` is rejected
- invalid property/type combinations fail cleanly

### 5.4 Text properties

Run:

```aux
tx1.pos
tx1.color
tx1.visible
tx1.fontname
tx1.fontsize
tx1.string
tx1.type
tx1.parent
tx1.children
```

Then valid writes:

```aux
tx1.pos=[.3 .7 0 0]
tx1.color=[0 .2 .8]
tx1.visible=0
tx1.visible=1
tx1.fontname="Helvetica"
tx1.fontsize=16
tx1.string="updated"
```

Then invalid writes:

```aux
tx1.string=5
tx1.fontsize="16"
tx1.xdata=[1 2 3]
```

Check:

- text updates visually
- invalid values fail cleanly

## 6. Property Path Access

Run:

```aux
gcf.color
gca.xlim
h.children
h.children{1}
ax.parent
ln2.parent
```

Check:

- path-based property access works
- children arrays can be inspected
- indexed child access behaves correctly

## 7. Graph Window GUI Behavior

Open at least one graph window and verify:

### 7.1 Navigation

- `+` zooms in
- `-` zooms out
- `Left` / `Right` pans
- drag-select then `Enter` zooms to selection
- `Cmd+/` resets full range
- `Cmd+,` goes back in range history
- `Cmd+.` goes forward in range history

Expected:

- no visual corruption
- range changes track the displayed data correctly

### 7.2 Stereo display

Open stereo graph from `st` and press `F2` repeatedly.

Expected:

- cycles vertical split -> overlay -> overlay reversed
- same figure remains active
- same underlying plot remains associated with the same content

### 7.3 FFT overlay

Press `F4` and `Shift+F4`.

Expected:

- FFT overlay toggles on/off
- reset works without disturbing the base plot

## 8. Variable Browser Integration

### 8.1 Open/focus graph from variables

- select `mono` or `v`
- press `Enter`

Expected:

- graph opens or focuses

### 8.2 Audio preview from variables

- select `mono`
- press `Space`
- press `Space` again
- stop by changing selection or using the implemented stop path

Expected:

- audio plays
- second `Space` pauses/resumes as implemented
- UI remains responsive

## 9. Playback Handle Checklist

### 9.1 Start playback

Run:

```aux
ph=play(mono)
ph2=play(mono,2)
```

Check:

- playback starts audibly
- handle is returned
- console remains responsive

### 9.2 Inspect playback handle members

During playback, run:

```aux
ph.fs
ph.dur
ph.repeat_left
ph.prog
```

Check:

- `fs` matches sample rate
- `dur` is plausible
- `repeat_left` decreases during repeated playback
- `prog` increases toward `100`

### 9.3 Playback control

Run:

```aux
pause(ph)
resume(ph)
stop(ph)
```

Also try method forms:

```aux
ph.pause
ph.resume
ph.stop
```

Check:

- pause halts playback
- resume continues playback
- stop ends playback
- after stop, `prog` reaches `100` and `repeat_left` becomes `0`

### 9.4 Playback negative cases

Run:

```aux
play(1)
play(mono,0)
play(mono,1.5)
pause(123456789)
resume(123456789)
stop(123456789)
```

Check:

- each invalid command fails cleanly
- no hang or crash

## 10. Synchronous Recording Checklist

### 10.1 Mono and stereo sync recording

Run:

```aux
r1=record(0,500,1)
r2=record(0,500,2)
```

Check:

- microphone capture starts and ends
- mono result is created
- stereo result is created if supported by the chosen device
- resulting variables appear in the variable panes

### 10.2 Open recorded results

- open `r1` and `r2` in graph view
- play them from the variable pane

Expected:

- data looks plausible
- playback works

### 10.3 Sync recording negative cases

Run:

```aux
record(-1,500,1)
record(0,0,1)
record(0,-1,1)
record(0,500,3)
record(0,500,"2")
```

Check:

- each invalid call fails cleanly

## 11. Async Recording Checklist

Use a callback UDF already available in your search path, or prepare one before starting this section.

### 11.1 Start async recording

Run:

```aux
rh=record(0,1000,1,100).callback
```

If indefinite capture is needed:

```aux
rh2=record(0,-1,1,100).callback
```

Check:

- handle is returned
- recording starts
- no immediate callback error appears

### 11.2 Inspect async recording handle members

While recording is active, run several times:

```aux
rh.type
rh.id
rh.devID
rh.fs
rh.channels
rh.dur
rh.block
rh.durRec
rh.durLeft
rh.prog
rh.active
rh.paused
```

Check finite-duration case:

- `type` is `audio_record`
- `durRec` increases
- `durLeft` decreases to `0`
- `prog` increases toward `100`
- `active` stays `1` until completion/stop
- `paused` is `0` unless paused

Check indefinite case:

- `durRec` increases
- `durLeft` stays `0`
- `prog` stays `0`

### 11.3 Async control

Run:

```aux
pause(rh)
resume(rh)
stop(rh)
```

Also test:

```aux
rh.pause
rh.resume
rh.stop
```

Check:

- pause sets `paused`
- resume clears `paused`
- stop ends recording
- after stop, `active` becomes `0`

## 12. Async Recording Callback + Graphics

This section is for callback UDFs that create or update graphics.

### 12.1 Callback open event

Start async recording with the callback.

Check:

- callback open event is delivered once
- no duplicate startup behavior occurs

### 12.2 Callback block updates

While recording continues:

- watch console messages from the callback
- watch any callback-created graph windows

Check:

- block updates continue as audio arrives
- graph windows refresh during recording
- callback-created graphics remain stable and usable

### 12.3 Callback-written axis updates

If the callback changes `xlim` or `ylim`, check:

- graph range visibly updates during recording
- no stale window state remains after recording stops

### 12.4 Final flush on stop

Stop recording before a natural block boundary if possible.

Check:

- final callback output still appears
- no data is obviously lost at stop
- handle state transitions to inactive cleanly

## 13. Scope and Debugger Interaction

If graphics or audio objects are used while debugging a UDF:

### 13.1 Pause inside child scope

- trigger a breakpoint in a UDF
- open or inspect relevant graph windows

Check:

- variable panes reflect the active paused scope
- windows from other scopes become inactive as expected

### 13.2 Resume back to base scope

- continue or abort out of child scope

Check:

- child-scope windows close when expected
- base-scope windows remain usable

## 14. Unsupported / Error Cases

Try unsupported or wrong-type properties:

```aux
h.fontsize=12
ax.marker="o"
ln2.fontsize=10
tx1.xlim=[0 1]
```

Try unsupported planned-but-not-exposed properties if applicable:

```aux
h.tag
h.userdata
ax.xtick
ax.ytick
ax.xticklabel
ax.yticklabel
```

Check:

- each fails cleanly
- errors are understandable
- app remains stable

## 15. Pass / Fail Summary

Mark the session as passing when all of the following are true:

- graphics builtins work in `auxlab2`
- property reads and valid writes behave correctly
- invalid property writes fail cleanly
- `gcf` and `gca` track GUI focus/current-object behavior
- graph window navigation and stereo display remain stable
- playback handles update correctly during playback
- sync and async recording work with expected handle/state behavior
- callback-created or callback-updated graphics refresh correctly
- no crash, hang, corrupted window state, or stuck audio state occurs

## 16. Notes

Record any of the following during the run:

- exact failing command
- expected behavior
- actual behavior
- whether the issue is deterministic
- whether it is audio-device-specific, scope-specific, or callback-specific
