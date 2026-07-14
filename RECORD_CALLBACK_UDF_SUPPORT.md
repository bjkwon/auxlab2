# Record Callback UDF Support

This note summarizes the current `record(...).callback` support in auxlab2 and the auxe engine. It is intended as the starting point before designing a similar callback feature for `play()`.

## Current User Model

Direct recording still returns an audio value:

```aux
x = record()
y = record(0, 1000, 1)
```

Callback recording starts an asynchronous recording session and returns a runtime recording handle:

```aux
h = record(0, -1, 1, 100).cb1
```

The callback form accepts:

- `device_id`
- `duration_ms`
- `channels`
- `block_ms`

For callback recording, `duration_ms` may be `-1` for indefinite recording. `channels` is currently limited to `1` or `2`, and `block_ms` must be positive.

The suffix after the dot is the callback UDF name. In the example above, auxe invokes the UDF named `cb1`.

## Callback UDF Shape

The callback UDF may take zero or one input argument. The normal pattern is one input struct:

```aux
function [out,h,ax]=cb1(in)
if in.?index==0
    out = []
    h=figure
    ax = axes(h)
    ax.xlim=[0 10]
else
    out ++= in.?data
    plot(ax, out)
    ax.xlim=[0 10]
    repaint(h)
end
```

The callback input currently includes:

- `in.?index`: callback index
- `in.?fs`: sample rate
- `in.?data`: audio block

The `auxRecordCallbackPayload` also carries `sample_rate`, `num_channels`, `callback_index`, and interleaved sample data. The engine maps this payload into the AUX callback input.

Callback index `0` is the open/setup event. It is delivered once before normal block callbacks. Indices `1` and later deliver recorded audio blocks through `in.?data`.

## Persistent Callback State

The engine keeps per-session callback state in `aux_invoke_record_callback(...)`.

For a given session id, auxe remembers:

- the resolved callback UDF
- previous callback output values
- callback output names
- the input state struct

Before each invocation, previous output values are installed into the child UDF scope. After the invocation, updated output values are copied back into the session state. This is why a callback can accumulate `out` over many blocks without needing a global variable.

The API `aux_attach_record_callback_outputs_to_handle(...)` can attach callback outputs back onto the runtime recording handle. This lets callback-created values such as figure and axes handles remain reachable through the handle state when possible.

Callback output names cannot collide with reserved recording-handle member names.

## Runtime Handle

`record(...).callback` returns a handle marked as an AUX runtime handle. Its `type` member is `audio_record`.

The initial handle includes members such as:

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

auxlab2 updates these members as the Qt recording session runs. `stop(h)`, `pause(h)`, and `resume(h)` also recognize recording handles and route to the async recording control backend.

## Engine Responsibilities

The engine side lives mainly in:

- `aux_engine/src/func/record_builtin.cpp`
- `aux_engine/src/api/interface.cpp`
- `aux_engine/include/auxe/auxe.h`
- `aux_engine/src/func/play_builtin.cpp`
- `aux_engine/test/regression_record_callback.cpp`

`record_builtin.cpp` detects whether the AST represents a callback-suffix form. For a callback recording call, it:

1. validates argument count and types
2. extracts the callback name from the suffix
3. creates a recording handle id
4. initializes the `audio_record` handle members
5. calls `playback_backend.record_async_start(...)`

`interface.cpp` provides the narrow callback bridge:

```cpp
int aux_invoke_record_callback(
    auxContext** ctx,
    uint64_t session_id,
    const string& callback_name,
    const auxRecordCallbackPayload& payload,
    const auxConfig& cfg,
    string& preview_or_error);
```

That bridge invokes the UDF directly with prepared state. The earlier alternative was reconstructing a text expression and passing it through `eval(...)`; the implemented bridge is safer because audio blocks, special fields, persistent outputs, debug pause state, and errors are all handled structurally.

`play_builtin.cpp` does not implement play callbacks, but it already participates in recording control. Its `stop/pause/resume` path distinguishes playback handles from `audio_record` handles and dispatches recording handles to `record_async_control`.

## auxlab2 Responsibilities

The auxlab2 side lives mainly in:

- `auxlab2/src/MainWindow.cpp`
- `auxlab2/src/MainWindow.h`
- `auxlab2/src/AuxEngineFacade.cpp`
- `auxlab2/src/AuxEngineFacade.h`
- `auxlab2/cb1.aux`

auxlab2 installs a playback backend containing both playback and recording hooks:

- `start`
- `control`
- `record`
- `record_async_start`
- `record_async_control`

The async recording session is owned by `MainWindow::RecordingSession`. It stores the Qt capture objects, the callback name, block timing, converted pending samples, ready callback blocks, active/paused state, and progress counters.

The important design decision from the Qt callback-minimum work is:

- capture is frontend-owned
- callback execution is delivered on the main Qt/app thread
- callbacks are not executed directly from the audio capture path

This keeps audio capture and AUX execution separated enough to avoid reentrancy and thread-safety problems.

`AuxEngineFacade::invokeRecordCallback(...)` wraps `aux_invoke_record_callback(...)`, captures callback stdout, appends preview/error text, updates the active context if a callback enters the debugger, and reports success/failure back to `MainWindow`.

## Console Assignment Quirk

There is auxlab2-side repair logic in `MainWindow::runCommand(...)` for commands such as:

```aux
h = record(0, -1, 1, 100).cb1
```

After `engine_.eval(...)`, auxlab2 checks whether an async recording handle was just started and whether the command matched the `record(...).callback` shape. It then ensures the left-hand variable receives the real handle and normalizes the console output to:

```text
audio_record handle <id>
```

This exists because member-style callback syntax can otherwise be interpreted in ways that leave a stray callback-named variable or misleading echo text. Any future `play(...).callback` syntax should avoid depending on a similar UI-level cleanup if the parser and builtin can represent the call more directly.

## Error Behavior

Start-time validation rejects invalid device ids, invalid durations, unsupported channel counts, invalid block sizes, missing async backend support, and missing callback names.

Runtime callback errors are surfaced through the callback bridge. In auxlab2, callback failures should stop the recording session, mark the handle inactive, and print the callback error/output in the console.

The regression test covers important engine semantics:

- callback output state persists across invocations
- callback state is isolated by session id
- callback output can be attached back to a handle
- malformed callback signatures fail
- callback execution errors are reported

## What This Means For `play()`

Playback already has runtime handles and backend hooks:

- `auxPlaybackStartHook`
- `auxPlaybackControlHook`
- `playback_backend.start`
- `playback_backend.control`

But playback does not yet have the callback equivalents of:

- an async callback spec type
- a callback payload type
- a callback invocation bridge
- per-session callback state
- a frontend-owned callback delivery loop

A play callback design should probably mirror the record callback design instead of inventing a second mechanism.

Likely pieces:

1. Define a playback callback spec, for example callback name, block size or interval, sample rate, total duration, repeat state, and optional mode fields.
2. Define a playback callback payload, for example sample rate, channel count, callback index, current playback frame/time, progress, repeat index, and possibly the current audio block.
3. Add an engine bridge such as `aux_invoke_play_callback(...)`.
4. Store persistent callback outputs per playback session, similar to record callback sessions.
5. Extend `play_builtin.cpp` to detect callback-suffix syntax and create a playback handle with callback metadata.
6. Extend auxlab2 playback sessions to queue callback events from playback progress and deliver them on the main Qt thread.
7. Decide whether callbacks are time-based, block-based, event-based, or all three.

The most important constraint should carry over unchanged:

- never invoke a UDF callback directly from the audio playback callback/device thread

## Open Design Questions For Play Callbacks

- Should syntax be `play(x).cb`, `play(x, repeat).cb`, `h.play(x).cb`, or a different explicit form?
- Should a playback callback receive audio samples, playback position metadata, or both?
- Should callback index `0` mean playback-open/setup, matching `record()`?
- Should callbacks fire per audio block, per fixed millisecond interval, on play/pause/resume/stop events, or at named markers?
- Should callback outputs attach to the playback handle the same way record callback outputs attach to recording handles?
- How should repeated playback be represented in callback payloads?
- Should callback failure stop playback immediately?
- Can the parser/builtin path be improved enough to avoid auxlab2 command-string repair logic?

## Source Basis

This document is based on the current source tree plus the prior `Find Qt callback minimum` and `Port record() to auxlab2` design direction. The most relevant local files are:

- `auxlab2/ASYNC_RECORDING_IMPLEMENTATION_PLAN.md`
- `auxlab2/src/MainWindow.cpp`
- `auxlab2/src/MainWindow.h`
- `auxlab2/src/AuxEngineFacade.cpp`
- `auxlab2/src/AuxEngineFacade.h`
- `auxlab2/cb1.aux`
- `aux_engine/src/func/record_builtin.cpp`
- `aux_engine/src/func/play_builtin.cpp`
- `aux_engine/src/api/interface.cpp`
- `aux_engine/include/auxe/auxe.h`
- `aux_engine/test/regression_record_callback.cpp`
