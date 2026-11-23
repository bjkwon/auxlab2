# Offline acoustic echo cancellation in auxlab2

This example processes a complete, predefined mono recording. It needs no audio
device, microphone permission, callback, or streaming backend. The two UDFs use
ordinary AUX vector arithmetic, indexing, `filt`, and audio conversion.

## Run in auxlab2

For a quick demonstration, choose **Settings > View Runtime Settings**, set
**Sampling Rate** to **8000**, and accept the dialog. The UDF also works at the
current rate without changing settings, but higher rates require more samples and
more filter taps. Run these commands in the console:

```aux
#cd /Users/bkwon/dev/auxlab2/examples/aec
r = aec_demo(60);
before = r.microphone;
after = r.clean;
desired = r.near;
plot(before)
plot(after)
plot(desired)
```

Each `plot` opens a graph. Alternatively, select a signal in Audio Objects and
press Enter. Zoom into the double-talk intervals to compare `after` and `desired`.
Optional playback is `play(before)` followed separately by `play(after)`; the
identification probe sounds like noise. Playback is not part of the algorithm.

Open `aec_nlms.aux` with **File > Open UDF...**, put a breakpoint on the echo
estimate or coefficient update, and run the demo again to inspect `u`, `w`,
`energy`, and `clean(n)`. Use a shorter `aec_demo(10)` while debugging. See the
repository tutorial for stepping and continuing.

## What is being emulated?

```text
reference x -----> simulated room FIR h ----> echo ----+
     |                                               +--> microphone d
     |                          near-end signal s ---+
     v
adaptive FIR w --> estimated echo y
                                  microphone d - y --> clean output e
```

The simulated microphone is `d = filt(x, h) + s`. The canceller receives only
`x`, `d`, and an adaptation mask. It does not receive `h`, the true echo, or `s`.
It estimates the loudspeaker-to-microphone path from the reference and microphone:

```aux
u = [reference(n) u(1:M-1)]
estimated(n) = sum(w * u)
clean(n) = microphone(n) - estimated(n)
energy = sum(u * u)
if adapt(n) > .5 && energy > 1e-10
    w += (mu * clean(n) / (energy + 1e-8)) * u
end
```

Here `*` is elementwise multiplication in AUX, and `sum(w*u)` is the dot product.
`u` holds the current reference sample and its previous `M-1` samples.
The NLMS update normalizes adaptation by reference energy. The example uses
`mu = .5`; the UDF requires `0 < mu < 2`.

`aec_demo` generates a deterministic broadband probe once, constructs the entire
microphone signal, and then processes it in 100 ms chunks. Both `state.w` and
`state.u` survive chunk boundaries. Chunking emulates sequential processing of a
long recording; it does not make a sample loop faster or make this a real-time
implementation. This demo keeps the complete input and output arrays in memory.

For the 60-second example:

| Time | Scenario | Expected output |
| --- | --- | --- |
| 0–15 s | Far-end reference and echo only | Initial convergence, then nearly silence |
| 15–25.2 s | Near-end signal and echo together | Near-end signal preserved |
| 25.2–34.8 s | Echo only | Nearly silence |
| About 34.8 s | Room impulse response changes | Temporary residual echo, then re-convergence |
| 45–49.8 s | Second double-talk interval | Near-end signal preserved |
| Remainder | Echo only | Nearly silence |

The double-talk mask is an **oracle** derived from the known simulation schedule.
Adaptation freezes during those intervals, but echo subtraction continues. A real
AEC needs a double-talk detector or a robust learning-rate controller.

## Inspect the results

```aux
r.erle_db
r.erle_valid
r.weights
r.room_final
```

`erle_db(b)` is echo return loss enhancement for the one-second window ending at
second `b`: `10*log10(echo_energy / residual_energy)`, with a numerical floor.
Only entries where `erle_valid(b) == 1` are measurements. Other entries are zero
placeholders for windows containing near-end activity; do not interpret those
zeros as failed cancellation. With real recordings, true echo is usually unknown,
so use microphone/output power only during independently identified echo-only
intervals. During double talk, a quiet output alone is not evidence of success:
compare the clean output with the known near-end signal in this simulation.

Local verification with the app's built engine at 8 kHz, 60 seconds:

- 480,000 samples, 256 adaptive taps, 600 chunks.
- First-second ERLE about 15.8 dB; second-second ERLE about 132.7 dB.
- Peak `clean - near` error in seconds 20–24: approximately `2.735e-8`.
- Maximum final coefficient error relative to the second room: `6.204e-9`.
- Separate short checks: chunked versus whole output and coefficient differences
  were exactly zero; frozen coefficients and silent-reference pass-through also
  had zero error.

The huge settled ERLE is a consequence of matching finite, linear FIR paths and
no microphone noise. It is not a real-world performance claim. The near-end signal
is a synthetic two-tone stand-in, not recorded speech. This is a transparent
algorithm/debugging example, not a perceptual speech benchmark.

## Use your own long recording

Load two aligned mono recordings at the same sampling rate: `referenceAudio` is
the signal sent to the loudspeaker; `micAudio` contains its echo plus near-end
speech. Convert them to vectors before calling the UDF. Initialize once:

```aux
x = vector(referenceAudio);
d = vector(micAudio);
state.w = zeros(256);
state.u = zeros(256);
adapt = ones(length(x));
[e, y, state] = aec_nlms(x, d, adapt, state, .5);
cleanAudio = audio(e);
```

The all-ones mask above assumes echo-only training. Set mask samples to zero
during known near-end speech, or supply a separately implemented detector.
For long files, use the chunk loop in `aec_demo.aux` and initialize state before
the loop. `audio(e)` uses the runtime sampling rate, which must match the files;
perform any required resampling/alignment first. Keep the common amplitude scale
of reference and microphone; avoid independently normalizing each chunk.

At 8 kHz, 256 taps cover 32 ms including propagation and device delay. The demo
uses reflections at 6–27 ms, not a long reverberant room. Increase the tap count
to cover the actual delay and room tail, or compensate a known bulk delay before
adaptation. Longer recordings need more processing time and memory, not more taps.
Longer echo paths need more taps. This interpreted time-domain implementation
costs O(samples × taps); frequency-domain block adaptation is a better next step
for long impulse responses. Clipping, nonlinear loudspeakers, clock drift, robust
double-talk handling, and residual-echo suppression are outside this example.

Background: [MathWorks acoustic echo cancellation example](https://www.mathworks.com/help/audio/ug/acoustic-echo-cancellation-aec.html)
describes the same reference/microphone/near-end arrangement and adaptation control.

