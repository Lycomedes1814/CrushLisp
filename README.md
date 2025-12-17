# CrushLisp

A minimal Lisp interpreter written in C.

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

### Special Forms

- `(quote x)` - Return x without evaluation
- `(if test then else)` - Conditional branching
- `(def name value)` - Bind a global name
- `(let (name value ...) body...)` - Scoped local bindings
- `(fn (params...) body...)` - Anonymous function
- `(do expr...)` - Evaluate expressions sequentially

### Built-in Functions

**Arithmetic:**
- `+`, `-`, `*`, `/`, `mod` - Standard arithmetic
- `inc`, `dec` - Increment/decrement by 1

**Comparisons:**
- `=`, `<`, `<=`, `>`, `>=`

**Lists:**
- `(list values...)` - Create a list
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
(def factorial (fn (n)
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

; String operations
(str "Hello" " " "World")  ; => "Hello World"
(println "Result:" 42)     ; Prints: Result: 42
```

## Installation

```bash
make install
```

This installs the `crushlisp` binary to `/usr/local/bin/`.

## License

See LICENSE file for details.
