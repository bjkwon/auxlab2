# Async Recording Callback Implementation Plan

## Goal

Add legacy-style asynchronous recording to auxlab2 while preserving the current synchronous `record()` API.

The async path should:

- return a recording handle immediately
- capture audio continuously in the Qt frontend
- invoke an AUX callback once per recorded block
- expose handle state that can be inspected from AUX
- support `stop(handle)`, `pause(handle)`, and `resume(handle)`

The current synchronous behavior should remain available:

- `record()` and `record(dev, dur, ch)` return an audio object directly

The new async behavior should be enabled by callback syntax, matching legacy AUXLAB intent:

- `record(...).cbname(...)`

## Legacy Behavior Summary

Legacy AUXLAB recording was organized as two cooperating layers:

- low-level capture thread in `wavplay/record.cpp`
- AUX callback execution thread in `xcom/showvar.cpp`

The important behavior to preserve is:

- `record()` returns a handle immediately, not captured audio
- capture happens asynchronously in fixed-size blocks
- callback 0 runs before true recording begins
- each recorded block triggers one callback with `?data` and `?index`
- handle members such as duration/progress are updated as recording proceeds
- `stop(handle)` ends recording early

The legacy implementation did not have a meaningful `pause/resume` implementation for recording, so auxlab2 is free to define this cleanly using Qt.

## Non-Goals

- do not remove or replace the current synchronous `record()` implementation
- do not run AUX callback code directly on the audio capture thread
- do not emulate Win32 message semantics literally
- do not silently fake stereo from mono devices in async mode

## User-Facing API

### Synchronous forms

- `x = record`
- `x = record(deviceId, durationMs, channels)`

These continue to return an audio object.

### Async forms

- `h = record.cbname`
- `h = record(deviceId, durationMs, channels, blockMs).cbname`
- `h = record(recorderStruct).cbname`

Where:

- `h` is an audio recording handle
- `cbname` is the callback UDF
- `blockMs` is the callback block size in milliseconds

### Control forms

- `stop(h)`
- `pause(h)`
- `resume(h)`

These should work for both playback handles and recording handles.

## Callback Semantics

Recording callbacks are intentionally not ordinary AUX UDF calls.

They should be treated as session-persistent event handlers tied to one live recording handle.

### Persistent callback state

For a single recording session:

- callback input/output state is persistent across callback invocations
- `in` is reused across calls, with callback-specific members updated each time
- `out` is reused across calls and should preserve prior callback state unless the callback overwrites it

This unusual persistence is intentional and matches the legacy recording callback model.

It is acceptable here because recording callbacks are stream-processing handlers rather than ordinary pure function calls.

### Initialization callback

The first callback uses:

- `in.?index == 0`

This call is the session initialization event.

Expected behavior:

- `in.?data` is empty
- `in.?fs` is already set
- callback-local persistent state may be initialized here
- `out` for this session begins here

### Block callbacks

Subsequent callbacks use:

- `in.?index >= 1`

Each such callback represents one recorded audio block:

- `in.?data` contains the newly captured block
- `in.?fs` is the sample rate of that block
- `out` retains its previous value from the prior callback in the same session

### Session reset boundary

Persistent callback state is scoped to one recording session only.

It must be reset when:

- a new `record(...).callback` handle is started
- the previous recording session has been stopped or terminated

It must not leak between separate recording handles.

### Documentation note

This persistence rule should be documented as a special recording-callback execution model, not as a general AUX UDF rule.

## Handle Model

The engine should treat recording handles the same way it treats playback handles:

- lightweight runtime-handle struct in AUX variables
- frontend-owned live session object in Qt

Suggested recording handle members:

- `type`
  - string
  - `"audio_record"`
- `id`
  - scalar handle id
- `devID`
  - input device id used for capture
- `fs`
  - requested AUX sample rate used for callback data
- `channels`
  - requested output channel count
- `dur`
  - target duration in milliseconds or negative for indefinite
- `block`
  - callback block duration in milliseconds
- `durRec`
  - recorded duration so far in milliseconds
- `durLeft`
  - remaining duration in milliseconds for finite recording
- `prog`
  - 0 to 100 for finite recordings, or 0 for indefinite recordings
- `active`
  - `1` while recording is live, `0` after close/error/stop
- `paused`
  - `1` while suspended, `0` otherwise

Optional future members:

- `state`
  - `"open"`, `"recording"`, `"paused"`, `"stopped"`, `"error"`
- `device_name`
  - frontend-only convenience string if string handle updates are added later

## High-Level Architecture

### Engine responsibilities

- parse sync vs async `record()` forms
- create recording runtime handles for async mode
- validate recording-handle use in `stop/pause/resume`
- call frontend backend hooks for:
  - sync finite capture
  - async session start
  - async session control
- expose callback metadata needed by the frontend

### Frontend responsibilities

- own all live recording sessions
- start and control `QAudioSource`
- capture native-format input audio
- convert/resample into AUX callback format
- break stream into callback blocks
- queue callback delivery onto the Qt main thread
- update runtime handle members in the engine
- shut down sessions cleanly on stop/error/close

### Important rule

No AUX callback execution should happen on the audio input thread or in a direct `QIODevice::writeData()` path.

Audio capture should only:

- append bytes
- mark blocks ready
- queue delivery work to the main thread

This avoids audio starvation and engine reentrancy problems.

## Qt Session Design

Add a `RecordingSession` in `MainWindow` parallel to the existing `PlaybackSession`.

Suggested fields:

- `std::uint64_t handleId`
- `int deviceId`
- `int requestedSampleRate`
- `int requestedChannels`
- `double durationMs`
- `double blockMs`
- `bool finiteDuration`
- `bool active`
- `bool paused`
- `bool stopping`
- `bool callbackOpenDelivered`
- `int callbackIndex`
- `qsizetype targetFrames`
- `qsizetype blockFrames`
- `qsizetype capturedFrames`
- `QAudioDevice device`
- `QAudioFormat captureFormat`
- `QAudioSource* source`
- `AudioCaptureSink* sink`
- `QByteArray nativeBytes`
- `std::vector<double> convertedInterleavedPending`
- callback descriptor data

Suggested container in `MainWindow`:

- `std::map<std::uint64_t, RecordingSession> recordingSessions_;`

## Callback Descriptor

The async recording backend needs enough information to call the user callback later.

Suggested callback descriptor:

- callback UDF name
- original callback AST or a resolved callback descriptor
- any extra callback arguments supplied in `.cbname(...)`

Preferred long-term representation:

- engine-side callback descriptor object passed through the backend hook

Avoid relying on raw command-string reconstruction in the frontend as the long-term design.

## Engine API Changes

### Backend interface changes

Extend the playback/recording backend in `auxe.h` with separate async recording hooks.

Suggested additions:

- `record_sync`
  - keep current finite synchronous recording behavior
- `record_async_start`
  - starts an async recording session
- `record_async_control`
  - controls async recording session using stop/pause/resume

Suggested control enum:

- `AUX_RECORD_STOP`
- `AUX_RECORD_PAUSE`
- `AUX_RECORD_RESUME`

Do not overload the current synchronous `record` backend hook to do both jobs.

### Builtin changes

Update `record_builtin.cpp` to support:

- sync path when no callback suffix is present
- async path when callback suffix is present

For async mode:

- create a runtime handle
- populate initial handle members
- call `record_async_start`
- return the handle immediately

### Handle detection helpers

Add helpers similar to playback:

- `is_recording_handle(const CVar&)`
- `set_recording_handle_result(...)`
- `next_recording_handle_id()`

### `stop/pause/resume`

Extend `play_builtin.cpp` or split control builtins so they accept either:

- playback handle
- recording handle

Dispatch based on `type` or recording-handle shape.

## Frontend Backend Hooks

### `recordSync`

This is the current implementation:

- finite duration
- returns captured audio

Keep it intact.

### `startAsyncRecord`

Responsibilities:

- validate device selection
- validate stereo availability if `channels == 2`
- capture in device preferred/native format
- request microphone permission if needed
- create and store a `RecordingSession`
- open/start `QAudioSource`
- schedule block delivery
- initialize runtime handle members

### `controlAsyncRecord`

Responsibilities:

- find session by handle id
- stop:
  - stop source
  - flush final data if needed
  - deliver close/final state
  - mark inactive
- pause:
  - `source->suspend()`
  - update `paused`
- resume:
  - `source->resume()`
  - update `paused`

## Audio Data Flow

### Capture format

Always capture in the device's preferred/native format.

Do not request arbitrary low-rate formats like `8000 Hz` directly from the device.

### Conversion pipeline

For async mode, each session should:

1. collect native PCM bytes from `QAudioSource`
2. decode into normalized doubles
3. resample to requested AUX sample rate
4. downmix or preserve channels according to requested output channels
5. accumulate converted output into callback-sized blocks

Stereo policy:

- if requested output is stereo and the selected device exposes fewer than 2 input channels:
  - fail session start with an explicit error

### Block slicing

Use output-frame block slicing, not native-device-frame slicing.

This keeps callback blocks consistent regardless of device sample rate.

Given:

- `blockMs`
- `requestedSampleRate`

Compute:

- `blockFrames = round(blockMs * requestedSampleRate / 1000.0)`

Deliver callbacks only when at least one full block is ready, except for the final partial block on stop/close if finite recording ends before an exact block boundary.

## Callback Delivery Model

### Open callback

Legacy behavior invoked callback 0 before true recording began.

In auxlab2:

- create the handle first
- queue an initial callback delivery onto the main thread before or immediately after audio start
- set:
  - `?index = 0`
  - `?data = []` or an empty audio value

This preserves the important semantic:

- user callback receives an initialization event before block 1

### Block callback

For each full block:

- increment callback index
- build AUX audio object from block data
- call the callback with:
  - `?data`
  - `?index`
- update runtime handle members after successful callback execution

### Close/error handling

On close:

- mark `active = 0`
- set `paused = 0`
- finalize `durRec`, `durLeft`, `prog`

On error:

- mark inactive
- store/log error message
- surface error to console/UI

MVP can omit a dedicated close callback if needed, as long as handle state is updated correctly.

## Callback Invocation Strategy

There are two possible implementations.

### Option A: `engine_.eval(...)` reconstruction

Use the frontend to build an AUX expression and call it through `engine_.eval(...)`.

Pros:

- fast to prototype
- no new engine callback API needed immediately

Cons:

- brittle for passing audio blocks and structured metadata
- awkward for extra callback arguments
- harder to make robust for quoting, temp vars, and error propagation

### Option B: explicit engine callback bridge

Add a narrow engine API for invoking a callback UDF with prepared values.

Pros:

- much cleaner
- closer to legacy semantics
- easier to pass `?data`, `?index`, and extra callback args
- better control of error propagation and scope

Cons:

- requires engine API work

Recommendation:

- implement MVP with a minimal explicit engine callback bridge
- avoid committing to string-eval as the permanent architecture

## Proposed Engine Callback Bridge

Add an API that lets the frontend request:

- invoke callback UDF `name`
- provide implicit input struct fields:
  - `?data`
  - `?index`
- provide any extra bound callback args

Possible API shape:

- `aux_invoke_callback(...)`

Inputs:

- context
- callback descriptor
- audio block object
- callback index
- extra args

Outputs:

- success/failure
- error string

This API should execute only on the main app thread.

## Error Handling Rules

### Start-time failures

Return a normal AUX error immediately for:

- invalid device id
- unsupported stereo request
- microphone permission denied
- unusable device format
- callback resolution failure

### Runtime failures

If an async session fails after start:

- stop the session
- mark handle inactive
- surface an error in the console/UI

If callback execution throws:

- stop the session
- mark handle inactive
- surface the callback error

Avoid continuing recording after repeated callback failures.

## Interaction With Existing Async Poll

auxlab2 already calls `engine_.pollAsync()` on a timer.

This should remain separate from recording callback delivery.

Recommended model:

- recording session queues callback delivery onto main Qt thread
- callback delivery runs immediately on main thread
- `pollAsync()` continues to serve engine-owned async jobs

Do not make recording callback delivery depend on `pollAsync()` unless the engine callback bridge explicitly requires it.

## File-Level Implementation Plan

### aux_engine

#### `include/auxe/auxe.h`

- add async recording backend function pointers
- add recording control enum
- add callback descriptor types if needed

#### `src/engine/builtin_functions.h`

- add any new declarations needed for async record helpers

#### `src/engine/AuxFunc.cpp`

- register any new builtins or updated builtin behavior

#### `src/func/record_builtin.cpp`

- split sync and async record paths
- detect callback-suffix usage
- create runtime recording handle for async path
- start async recording through backend hook

#### `src/func/play_builtin.cpp`

- extend `stop/pause/resume` dispatch to recording handles
- or split control builtin implementation cleanly

#### `src/api/interface.cpp`

- add callback invocation API if implementing explicit engine bridge

### auxlab2

#### `src/MainWindow.h`

- declare:
  - `startAsyncRecord(...)`
  - `controlAsyncRecord(...)`
  - callback delivery helpers
  - `RecordingSession`

#### `src/MainWindow.cpp`

- add `RecordingSession` storage
- implement Qt recording session lifecycle
- implement block accumulation and delivery
- update runtime handle members
- surface errors and cleanup

#### `src/AuxEngineFacade.h/.cpp`

- add callback-bridge wrapper if engine API is introduced

## MVP Milestones

### Milestone 1: Engine and handle scaffolding

- detect async `record(...).cbname`
- create and return runtime record handle
- add frontend async start/control hooks
- support `stop(handle)` for recording handles

Deliverable:

- async record can start and stop without callback delivery yet

### Milestone 2: Qt session and block capture

- add `RecordingSession`
- capture native audio continuously
- convert/resample to requested AUX format
- slice into block-sized output frames

Deliverable:

- frontend can accumulate ready callback blocks for an active session

### Milestone 3: Callback invocation

- implement callback bridge
- deliver callback 0
- deliver per-block callbacks with `?data` and `?index`
- propagate callback errors cleanly

Deliverable:

- real legacy-style async callback recording works end-to-end

### Milestone 4: Pause/resume and polish

- map `pause/resume` to `QAudioSource::suspend/resume`
- update handle members correctly while paused
- finalize close/error reporting

Deliverable:

- full handle-style recording control

## Suggested Testing Matrix

### Basic startup

- `h=record.cbname`
- `h=record(0, 3000, 1, 100).cbname`
- invalid device id
- denied microphone permission

### Callback flow

- callback 0 delivered once
- multiple block callbacks delivered in order
- `?index` increments monotonically
- `?data` block size matches expected duration

### Format behavior

- mono device with mono request
- stereo device with stereo request
- mono device with stereo request fails immediately
- native capture format differs from AUX callback format

### Control behavior

- `stop(h)` while active
- `pause(h)` then `resume(h)`
- `stop(h)` while paused
- invalid handle control errors

### Error behavior

- callback UDF missing
- callback throws runtime error
- audio device unplugged or stopped unexpectedly

## Recommended Order Of Work

1. add async recording backend/control API to `aux_engine`
2. add recording-handle shape and builtin parsing
3. add `MainWindow` recording session storage and lifecycle
4. add block accumulation without callback invocation
5. add engine callback bridge
6. add per-block callback delivery
7. add pause/resume support
8. harden error paths and diagnostics

## Open Questions

- whether callback 0 should expose empty `?data` or omit it
- whether final partial block should always be delivered on stop/close
- whether to expose string handle members such as device name in v1
- whether async recording should allow indefinite duration by default when a callback is present
- whether callback execution should be serialized globally or only per session

## Recommendation

Build the async recorder as a frontend-owned Qt session system with engine-owned lightweight runtime handles.

The most important implementation constraint is:

- queued callback delivery on the main thread, never direct callback execution from the audio capture path

If that rule is maintained, the legacy callback model is practical to reproduce in auxlab2 without inheriting the old Win32 message-thread complexity.
