2026-07-29 13:17
Allowed no-argument functions to use empty parentheses, e.g. `getfs()`; the historical shorthand `getfs` still works.
================================
2026-07-29 17:01
Added graphics handle `selrange` support for reading, setting, clearing, and copying selected graph ranges; documented named-figure `selrange` and named-plot `.?sel` access to selected data.
================================
2026-07-29 19:52
Preserved audio slicing behavior for the selected-range shortcut so playback/export paths continue to use the intended selected region.
================================
2026-07-29 19:53
Avoided copying signal data during audio checks by using facade-side accessors for lighter validation.
================================
2026-07-29 19:59
Fixed selected-range tail slices to use the range end correctly when the selection extends to the end of the signal.
================================
2026-07-29 20:12
Fixed detection of selected ranges that end at the final sample.
================================
2026-08-23 16:55
Added a quote-free `//` shorthand syntax to the console: a line starting with `//` is preprocessed into a fully-quoted assignment before it reaches the engine, so string arguments (e.g. file paths) no longer need manual quoting. (0.9.7)
================================
2026-08-24 23:13
Improved figure(x) graphics: reduced figure window margins, skipped RMS computation on a figure's first audio load, and eliminated redundant SignalData copies through the figure(x) display pipeline (existence checks no longer materialize the full signal, and SignalGraphWindow now shares a SignalDataPtr instead of deep-copying the signal at each step) — cutting the copy/allocation overhead for large stereo signals from ~5 full copies down to ~2. (0.9.7.1)
================================
2026-08-25 00:15
Sped up wave()/plot() for large signals: getSignalData() now caches the built SignalData per object instead of rebuilding it (full copy + RMS scan) every time reconcileScopedWindows() runs after any console command; buildSignalDataFromAuxObj() moves the sample buffer straight in for the common single-segment case instead of zero-filling then copying; and GraphicsFigureModel::syncLineData() skips the gap-fill pass when there are no gaps and bulk-copies instead of pushing samples one at a time. (Considered also folding the WAV int->float->double conversion into a single pass, but benchmarking showed it was a net loss — the wider intermediate buffer added more memory traffic than the removed pass saved — so that part was not applied.) (0.9.7.2)
================================
