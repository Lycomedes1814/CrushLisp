# CrushLisp TODO

## Standard library

- [ ] `reduce` — promote to builtin (currently implemented; verify edge cases)
- [ ] `sort` / `sort-by`
- [ ] `str/join` — join a collection into a string with a separator
- [ ] `parse-number` — convert string to number

## String functions

- [ ] `upper-case` / `lower-case`
- [ ] `trim`
- [ ] `substring`
- [ ] `starts-with?` / `ends-with?`
- [ ] `replace`
- [ ] `contains?` (string variant — already exists for maps)
- [ ] `index-of`
- [ ] `format` / sprintf-style string formatting

## Language

- [ ] `doseq` / `dotimes` — iteration over collections without manual recursion
- [ ] `->` / `->>` threading macros (requires macros or built-in special forms)
- [ ] Tail-call optimization (TCO) for user-defined functions — removes 1000-depth ceiling for recursive code
- [ ] Macros (`defmacro`) — user-defined control flow and syntactic sugar
- [ ] Integer type — doubles work to 2^53 but `mod` and bitwise ops feel wrong on floats
- [ ] Regex — `(re-find pattern str)`

## Runtime

- [ ] Garbage collection — memory grows unboundedly; fatal for long-running programs
- [ ] Namespaces / modules — `load` is a start but there is no isolation or qualified names

## Longer term

- [ ] Network I/O — at minimum HTTP via `curl` wrapping, or a socket primitive
