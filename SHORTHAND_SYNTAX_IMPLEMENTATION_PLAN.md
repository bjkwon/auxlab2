> Applies beginning with **v0.9.7**.

# Shorthand (`//`) assignment syntax for the AUXLAB2 console

## Context

auxe's string syntax requires double-quoting every string literal, which is
especially cumbersome inside nested parentheses (e.g.
`x=wave(P++"/48429439.wav")`). The user wants a quote-free shorthand notation
usable directly in the AUXLAB2 console, without touching auxe itself.
AUXLAB2 already does source-line preprocessing before handing text to the
engine (the `.play()`/`.stop()` "method sugar" rewrites in `runCommand`), so
this shorthand is implemented as another preprocessing pass in AUXLAB2 only —
the auxe engine never sees the shorthand form, only the translated,
fully-quoted equivalent.

The grammar below was iteratively nailed down against ~10 worked examples
from the user; every example below is verified consistent with the final
rule set (no known contradictions remaining).

## Trigger

A line (after trimming leading whitespace) that starts with `//` enters
shorthand mode. This was chosen over `#` because auxe's own grammar already
treats a leading `#` as a shell-escape (`psycon.y:229`) while `//` is already
a no-op comment in auxe (`psycon.l:199`) — so repurposing `//` in AUXLAB2
shadows nothing real, whereas `#` would silently swallow genuine shell-escape
usage. Since AUXLAB2 fully consumes and translates `//` lines before they
ever reach `engine_.eval`, there is no collision.

Consequence: a genuine free-text `//` comment with 2+ words is now parsed as
shorthand rather than staying inert (e.g. `// just a comment` becomes
`just="a"++"comment"`). Only a bare `//` or a single-word `// x` degrades
back to a harmless comment. `//` previously did nothing in the console, so
this was accepted as the right tradeoff.

## Grammar

Split the line (content after `//`) into whitespace-separated words.
`word[0]` is always the target variable name.

**Mode is decided by `word[1]`:**

- **Ends with `(`** → function-call mode. Strip the trailing `(` to get the
  function name. Every word from `word[2]` onward becomes one
  comma-separated argument (each translated per the per-word rule below).
  Zero trailing words → zero-arg call.
  ```
  x=funcname(arg1, arg2, ...)
  ```
- **No trailing `(`** → concatenation/assignment mode. `word[1]` onward
  (each translated per the per-word rule) are joined with `++` and assigned
  directly. A single word needs no `++`.
  ```
  x=piece1++piece2++...
  ```

**Per-word translation** (applied to each argument in function mode, or each
piece in concatenation mode):

- Starts with `$` → strip the `$`; if the remainder matches auxe's numeric
  literal grammar (`NUM` in `psycon.l:73`:
  `[[:digit:]]+\.?[[:digit:]]*([eE][+-]?[[:digit:]]+)?` or `\.[[:digit:]]+`),
  emit it verbatim as a bare number. Otherwise emit the remainder verbatim
  as a bare identifier (variable reference).
- No `$` prefix → always a quoted string literal, verbatim text, regardless
  of whether it looks numeric. Any embedded `"` must be doubled (auxe's
  in-string escape rule, `psycon.l:233`: `<STR>\"\"`) before wrapping in
  `"..."`.

Each word is atomic — there is no scanning for a `$VAR` embedded partway
through a word (e.g. `$P/foo.wav` as one token is *not* supported). To build
a compound value, assign it to an intermediate variable on its own line
first, then reference that variable with `$`.

**AUXLAB2 performs no semantic validation.** Unknown function names, and
type mismatches like concatenating a string with a bare number, are left to
produce ordinary auxe errors when the translated line is evaluated — the
translator is a pure syntactic rewrite.

### Worked examples (all verified against the rule above)

| Shorthand | Translated |
|---|---|
| `// P /Users/bkwon/newdisk/humana_2020/audio` | `P="/Users/bkwon/newdisk/humana_2020/audio"` |
| `// Q $P /48429439.wav` | `Q=P++"/48429439.wav"` |
| `// x wave( $Q` | `x=wave(Q)` |
| `// z wave( mypath/ bkwon` | `z=wave("mypath/", "bkwon")` |
| `// z wave mypath` | `z="wave"++"mypath"` |
| `// x $P 3` | `x=P++"3"` |
| `// z this $2` | `z="this"++2` (auxe raises the LHS/RHS type error at eval time) |

## Implementation

Two functions in `src/MainWindow.cpp`, near the existing console-sugar
helpers (style-matched to `rewriteSelectedRangeCaptures` for hand-rolled
scanning, since this needs real tokenization rather than a single regex):

- `QString MainWindow::translateShorthandLines(const QString& text) const` —
  splits `text` on `\n`, and for each line whose trimmed form starts with
  `//`, replaces it with the output of `translateShorthandLine`; other lines
  pass through unchanged. Rejoins with `\n`.
- `QString MainWindow::translateShorthandLine(const QString& line) const` —
  implements the grammar above for a single `//`-prefixed line: tokenize by
  whitespace, apply the mode/per-word rules, and build the `name=...`
  output string.

Hook point: `actual = translateShorthandLines(cmd);` at the very top of
`MainWindow::runCommand` (`src/MainWindow.cpp:2058`), before the
`.play()`-style method-sugar block. Everything downstream (history
recording, `rewriteSelectedRangeCaptures`, `splitTopLevelStatements`,
`tryHandleGraphicsCommand`, `engine_.eval`) then sees ordinary, fully-quoted
auxe syntax and requires no changes.

## Explicit limitations (by design, already accepted by the user)

- No bare numeric/variable value as the sole 2-word assignment without `$`
  (e.g. a lone number can't be assigned without going through `$`, and a
  variable can't be the whole value without `$` prefix).
- No intra-word splicing — compound values need an intermediate variable.
- No multi-argument concatenation within a single function argument (each
  space-separated word after `word[1](` is its own comma-separated arg).
- Silent-looking failures are intentional: mistakes surface as ordinary
  auxe errors ("function not available", type-mismatch on `++`), not as
  AUXLAB2-side diagnostics.

## Verification

The full app could not be launched in the sandboxed dev environment (no
display server), so the exact translation logic was extracted into a
standalone QtCore-only test program and run against all worked examples
above plus edge cases (zero-arg calls, embedded-quote escaping, single-word
lines degrading to comments, plain auxe lines passing through untouched) —
all passed. Recommended manual follow-up once the GUI can be exercised:

1. Type each worked example above into the console and confirm the
   resulting variable's value/type matches expectations.
2. Confirm a non-`//` line (plain auxe syntax) and the existing `.play()`
   method-sugar still behave exactly as before (regression check).
3. Type a deliberately-wrong example, e.g. `// z wnave( mypath`, and confirm
   it surfaces as a normal auxe "function not available"-style error rather
   than an AUXLAB2-side crash or silent no-op.
