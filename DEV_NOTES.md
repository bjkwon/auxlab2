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
