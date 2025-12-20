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
- `(def name value)` - Bind a global name
- `(let (name value ...) body...)` - Scoped local bindings
- `(fn [params...] body...)` - Anonymous function (can use `[]` or `()` for params)
- `(do expr...)` - Evaluate expressions sequentially

### Built-in Functions

**Arithmetic:**
- `+`, `-`, `*`, `/`, `mod` - Standard arithmetic
- `inc`, `dec` - Increment/decrement by 1

**Comparisons:**
- `=`, `<`, `<=`, `>`, `>=`

**Lists & Vectors:**
- `(list values...)` - Create a list
- `[values...]` - Create a vector
- `(first coll)` - First element
- `(rest coll)` - Remaining elements
- `(cons x coll)` - Prepend value
- `(conj coll values...)` - Prepend multiple values
- `(count coll)` - Collection size
- `(nth coll index)` - Element at index

**String & I/O:**
- `(str values...)` - Concatenate to string
- `(print values...)` - Print without newline
- `(println values...)` - Print with newline
- `(eval expr)` - Evaluate expression or string containing code
- `(slurp filename)` - Read entire file as string
- `(spit filename content)` - Write string to file
- `(load filename)` - Read and evaluate Lisp file (equivalent to `(eval (slurp filename))`)

**Help:**
- `(help)` - Show help message

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
```

## Installation

```bash
make install
```

This installs the `crushlisp` binary to `/usr/local/bin/`.

## License

See LICENSE file for details.
