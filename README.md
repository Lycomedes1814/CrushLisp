# CrushLisp

A subset of Clojure implemented in C.

## Building

```bash
make        # Build the project
make debug  # Build with debug symbols
make clean  # Remove build artifacts
make test   # Run tests
```

## Usage

### Interactive Mode (REPL)

```bash
./crushlisp
```

### Running Scripts

```bash
./crushlisp < script.lisp
echo "(+ 1 2 3)" | ./crushlisp
```

### Command-line Options

- `-s` : Suppress output (except explicit printing with `print`/`println`)
- `-h`, `--help` : Show help message

#### Examples

```bash
# Normal mode - shows all evaluation results
echo "(+ 1 2 3)" | ./crushlisp
# Output: 6

# Silent mode - no evaluation results
echo "(+ 1 2 3)" | ./crushlisp -s
# Output: (none)

# Silent mode with explicit printing
echo "(println \"Result:\" (+ 1 2 3))" | ./crushlisp -s
# Output: Result: 6
```

## Language Features

### Data Structures

**Lists** - For code and function calls:
```lisp
(list 1 2 3)        ; => (1 2 3)
(+ 1 2 3)           ; Lists evaluate as function calls
```

**Vectors** - For data (evaluate their contents but don't call as functions):
```lisp
[1 2 3]             ; => [1 2 3]
[(+ 1 2) (* 3 4)]   ; => [3 12]  (expressions inside are evaluated)
```

### Special Forms

- `(quote x)` - Return x without evaluation
- `(if test then else)` - Conditional branching
- `(when test body...)` - Evaluate body if truthy, else nil
- `(def name value)` - Bind a global name
- `(let [name val ...] body...)` - Scoped local bindings (accepts `[]` or `()`)
- `(fn [params...] body...)` - Anonymous function; variadic: `(fn [a & rest] ...)`
- `(do expr...)` - Evaluate expressions sequentially
- `(and expr...)` / `(or expr...)` - Short-circuit logic
- `(loop [name val ...] body...)` - Iteration; use `recur` to jump back
- `(recur args...)` - Restart enclosing `loop` with new values
- `(try body (catch e handler...))` - Catch errors thrown by `throw`
- `(throw message)` - Signal an error
- `(-> x (f a) (g b))` - Thread-first: `(g (f x a) b)`
- `(->> x (f a) (g b))` - Thread-last: `(g b (f a x))`
- `(doseq [x coll] body...)` - Iterate over collection for side effects
- `(dotimes [i n] body...)` - Iterate `i` from 0 to n-1

### Built-in Functions

**Arithmetic:**
- `+`, `-`, `*`, `/`, `mod` - Standard arithmetic
- `inc`, `dec` - Increment/decrement by 1

**Comparisons:**
- `=`, `<`, `<=`, `>`, `>=`

**Arithmetic:** `+` `-` `*` `/` `mod` `inc` `dec`

**Comparisons:** `=` `<` `<=` `>` `>=`

**Logic:** `not` `and` `or`

**Higher-order:**
- `(apply f arg... list)` - Call `f` with args spliced from final list
- `(reduce f init coll)` - Fold collection
- `(map f coll)` - Transform collection
- `(filter f coll)` - Filter collection

**Lists & Vectors:**
- `(list values...)` - Create a list
- `[values...]` - Create a vector
- `(first coll)` - First element
- `(rest coll)` - Remaining elements
- `(cons x coll)` - Prepend value
- `(conj coll values...)` - Append values
- `(count coll)` - Collection size
- `(nth coll index)` - Element at index
- `(sort coll)` - Sort list of numbers or strings
- `(sort-by f coll)` - Sort by key function

**Maps:**
- `(hash-map k v ...)` - Create a map
- `(get map key [default])` - Look up key
- `(assoc map k v ...)` - Add/update entries
- `(dissoc map k ...)` - Remove entries
- `(keys map)` / `(vals map)` - List keys or values
- `(contains? map key)` - True if map has key; also works for string substrings

**Strings:**
- `(str values...)` - Concatenate to string
- `(str/join sep coll)` - Join collection with separator
- `(split s delim)` - Split string on single-character delimiter
- `(upper-case s)` / `(lower-case s)` - Case conversion
- `(trim s)` - Strip leading/trailing whitespace
- `(substring s start [end])` - Extract substring
- `(starts-with? s prefix)` / `(ends-with? s suffix)` - Prefix/suffix check
- `(replace s from to)` - Replace all occurrences
- `(index-of coll item)` - Index of item in string/list/vector, or -1
- `(parse-number s)` - Convert string to number, nil on failure
- `(format fmt args...)` - sprintf-style formatting: `%s %d %f %g %%`

**I/O:**
- `(print values...)` - Print without newline
- `(println values...)` - Print with newline
- `(slurp filename)` - Read entire file as string
- `(spit filename content)` - Write string to file
- `(load filename)` - Read and evaluate Lisp file

**Eval:** `(eval expr)` - Evaluate expression or string containing code

**System:**
- `(sh command)` - Execute shell command string, return output (supports pipes, wildcards, etc.)
- `(run program args...)` - Execute program directly without shell (safer for untrusted input)

**Type predicates:** `nil?` `number?` `string?` `bool?` `symbol?` `list?` `vector?` `fn?` `map?`

**Help:** `(help)` - Show help message

## Runtime Protection

CrushLisp includes built-in protection against common runtime issues:

- **Stack Overflow Protection**: Recursion is limited to a maximum depth of 1000 evaluation steps to prevent segmentation faults from infinite recursion. If exceeded, you'll receive a "Stack overflow: recursion depth exceeded" error instead of a crash.

```lisp
; This will error gracefully instead of crashing
(def loop (fn () (loop)))
(loop)
; => Error: Stack overflow: recursion depth exceeded
```

## Examples

```lisp
; Define a factorial function
(def factorial (fn [n]
  (if (= n 0)
    1
    (* n (factorial (dec n))))))

(factorial 5)
; => 120

; List operations
(def nums (list 1 2 3 4 5))
(first nums)      ; => 1
(rest nums)       ; => (2 3 4 5)
(count nums)      ; => 5
(nth nums 2)      ; => 3

; Vector operations
(def data [10 20 30])
(first data)      ; => 10
(count data)      ; => 3
[(+ 1 2) 4 5]     ; => [3 4 5] (elements are evaluated)

; String operations
(str "Hello" " " "World")  ; => "Hello World"
(println "Result:" 42)     ; Prints: Result: 42

; Eval
(eval "(+ 1 2 3)")         ; => 6
(def code "(* 5 5)")
(eval code)                ; => 25

; File I/O
(spit "data.txt" "Hello, World!")
(def content (slurp "data.txt"))
(println content)  ; Prints: Hello, World!

; Loading external files
(spit "lib.lisp" "(def square (fn [x] (* x x)))")
(load "lib.lisp")          ; Same as (eval (slurp "lib.lisp"))
(square 5)  ; => 25

; Shell commands (passed to shell, supports pipes/wildcards)
(sh "echo hello | wc -c")  ; => "6\n"
(sh "ls *.txt")            ; => list of .txt files

; Direct program execution (no shell, safer)
(run "cat" "file.txt")     ; => file contents
(run "echo" "hello world") ; => "hello world\n"

; Threading macros
(-> "hello world"
    upper-case
    (replace "WORLD" "LISP"))  ; => "HELLO LISP"

(->> (list 1 2 3 4 5)
     (filter (fn [x] (> x 2)))
     (map (fn [x] (* x x))))  ; => (9 16 25)

; Iteration
(doseq [x (list "a" "b" "c")]
  (println x))  ; prints a, b, c

(dotimes [i 3]
  (println i))  ; prints 0, 1, 2

; String operations
(upper-case "hello")              ; => "HELLO"
(trim "  spaced  ")               ; => "spaced"
(substring "hello" 1 3)          ; => "el"
(starts-with? "foobar" "foo")    ; => true
(replace "cat and cat" "cat" "dog") ; => "dog and dog"
(str/join ", " (list "a" "b" "c")) ; => "a, b, c"
(format "Hi %s, you have %d messages" "Bob" 5) ; => "Hi Bob, you have 5 messages"
(parse-number "3.14")            ; => 3.14
(parse-number "oops")            ; => nil

; Sorting
(sort (list 3 1 4 1 5))                         ; => (1 1 3 4 5)
(sort-by count (list "banana" "fig" "apple"))   ; => ("fig" "apple" "banana")
```

## Installation

```bash
make install
```

This installs the `crushlisp` binary to `/usr/local/bin/`.

## License

See LICENSE file for details.
