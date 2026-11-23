# `progblocks`: progressive block processing

`progblocks` feeds successive windows of a mono audio signal to an AUX user-defined function (UDF). The UDF returns an accumulator, which becomes the previous result for the next window. This supports stateful operations such as block analysis or an adaptive filter while making completed results available between windows.

## Calling it

```aux
result <~ progblocks(source, window_ms, "callback_name", extra1, extra2)
```

- `result` must be a plain top-level variable. `progblocks` is recognized specifically on the right side of `<~`; it is not a general-purpose ordinary function call.
- `source` is nonempty, contiguous, real mono audio. It may be complete audio or a progressive audio result. Stereo, complex audio, and discontinuous audio are not supported.
- `window_ms` is a positive finite duration in milliseconds. Window boundaries are computed from the sample rate, with at least one sample per window. Rounding permits successive windows to have different sample counts when the duration is not an integer number of samples.
- `"callback_name"` is the name of an AUX UDF as a string, not a builtin function. The UDF must be discoverable through the usual UDF search path or current directory.
- Any remaining arguments are passed to the UDF after its four standard arguments. They are evaluated when the job starts; they are not separately streamed or synchronized by `progblocks`.

For a complete input, processing begins at the first sample and the engine keeps a copy of the input. For a progressive input, the consumer reads the retained source from the beginning, including samples published before it attached, and waits for a complete window while the producer is still running. At the end, a shorter final window is passed at its actual length. If the producer is canceled, the available final prefix is processed and the reducer ends with `canceled` status.

## Callback contract

```aux
function out = my_callback(chunk, timestamp, prior, first, extra1, extra2)
```

The engine invokes the UDF in order, once for each window:

- `chunk` is the current window as mono audio at the source sample rate.
- `timestamp` is the window's start time in milliseconds: the source start time plus the number of preceding samples divided by the sample rate.
- `prior` is the accumulator returned by the previous invocation. On the first invocation it is empty.
- `first` is true only on the first invocation. Use it to initialize state rather than reading fields from the empty `prior`.
- The extra arguments are those supplied after the callback name, in the same order.

Return the state and any accumulated output in `out`. It is normally a struct. `progblocks` uses `out.h` for job status; a callback that produces audio can publish its completed output as `out.h.completed`. For example, the detector in [examples/progressive/detect_loud_blocks.aux](examples/progressive/detect_loud_blocks.aux) retains filter state and appends timestamps and levels above a threshold. To run it from its directory:

```aux
#cd /Users/bkwon/dev/auxlab2/examples/progressive
x = tone(440,2000);
ev <~ progblocks(x, 10, "detect_loud_blocks", [0.5 0.5], -20)
ev.h.status
ev.times
ev.levels
```

## Progress, errors, and output

The destination initially has an `h` status member. After each completed callback, the returned accumulator is published with `h` attached. Fields created by the callback are available only after its first successful invocation.

| Field | Meaning |
| --- | --- |
| `result.h.status` | `running`, `done`, `canceled`, or `error`. |
| `result.h.samples_completed` | Number of input samples successfully processed. It is populated as processing advances. |
| `result.h.completed` | Completed audio, when the callback explicitly publishes it. Analysis callbacks need not define this field. |
| `result.h.error` | Callback error text, present on error. Completed output from earlier successful blocks is retained when available. |
| `result.h.id`, `result.h.type` | Internal job identifier and type. |

The callback executes on the evaluator thread, one window per polling advance. The application can process events and publish results between callbacks, but a slow callback can still block the UI for the duration of one window. Smaller windows can improve responsiveness, with extra callback overhead. This is not a hardware audio callback or a guarantee of real-time throughput.

The accumulator is a progressively updated struct. A callback can opt in to continuous audio playback and plotting by returning `out.h.stream = 1` and `out.h.completed` as contiguous mono audio after each block. The audio must have the input sample rate, contain exactly one output sample per processed input sample, and preserve all previously published samples. Violating these requirements stops the job with `h.status = "error"`. After the first block, use `play(result.h.completed)` or `plot(result.h.completed)` directly to follow the growing output. Playback inserts silence during shortages, then resumes at the next unplayed sample. An ordinary assignment such as `snapshot = result.h.completed` takes a snapshot; plotting or playing it does not follow later updates. Callbacks that do not opt in continue to return ordinary results. Replacing or clearing the destination ends the reducer and terminates its stream.

## AEC example and the microphone argument

From [examples/aec](examples/aec/README.md), at an 8 kHz runtime sample rate:

```aux
#cd /Users/bkwon/dev/auxlab2/examples/aec
s = aec_setup(10);
r <~ progblocks(s.reference, 100, "aec_progress_block", s.microphone, s.adapt, s.taps, .5)
r.h.status
r.h.samples_completed
```

The third argument of `progblocks` is `"aec_progress_block"`. `s.microphone` is its **fourth** argument and the first extra argument passed to that UDF. An earlier version used `vector(s.microphone)` to convert the microphone to a sample vector; that conversion is optional here. `progblocks` does not require extra arguments to be vectors; the reference source and each `chunk` are audio.

The current AEC callback also accepts the aligned microphone as audio in this example, so `s.microphone` can be passed instead. A short test produced the same clean samples with both forms. In either case the microphone is an extra, complete input: `progblocks` does not independently track or align a second progressive audio source. The caller must supply matching sample rates, lengths and sample alignment; the callback uses its accumulated sample count to select the matching microphone and adaptation-mask range.

`r.clean` and `r.estimated` are accumulated audio objects, and `r.h.completed` contains the same clean audio as `r.clean`; `r.state.w` and `r.state.u` carry NLMS state between windows. Once a block has completed, use `p = play(r.h.completed)` and `plot(r.h.completed)` to follow the growing clean audio. `clean = r.h.completed; plot(clean)` instead plots a snapshot. See the [AEC guide](examples/aec/README.md) for the simulation, limitations, and complete workflow.
