# CrushLisp TODO

## Standard library

- [x] `reduce` — builtin
- [x] `sort` / `sort-by` — builtin (qsort-based, numbers and strings)
- [x] `str/join` — builtin
- [x] `parse-number` — builtin

## String functions

- [x] `upper-case` / `lower-case`
- [x] `trim`
- [x] `substring`
- [x] `starts-with?` / `ends-with?`
- [x] `replace`
- [x] `contains?` (string variant — extended existing `contains?`)
- [x] `index-of` (string and list/vector)
- [x] `format` / sprintf-style (`%s %d %f %g %%`)

## Language

- [x] `doseq` / `dotimes` — special forms
- [x] `->` / `->>` threading — special forms
- [ ] Tail-call optimization (TCO) for user-defined functions — removes 1000-depth ceiling for recursive code
- [ ] Macros (`defmacro`) — user-defined control flow and syntactic sugar
- [ ] Integer type — doubles work to 2^53 but `mod` and bitwise ops feel wrong on floats
- [ ] Regex — `(re-find pattern str)`

## Runtime

- [ ] Garbage collection — memory grows unboundedly; fatal for long-running programs
- [ ] Namespaces / modules — `load` is a start but there is no isolation or qualified names

## Longer term

- [ ] Network I/O — at minimum HTTP via `curl` wrapping, or a socket primitive
