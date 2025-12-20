# AGENTS.md - CrushLisp Development Guide

This document provides essential information for AI agents working on the CrushLisp codebase.

## Project Overview

CrushLisp is a subset of Clojure written in C. It's a single-file implementation (~1625 lines) with a REPL, standard Lisp features, and runtime protections.

**Language**: C11 with POSIX extensions
**Files**: Single source file (`src/crushlisp.c`), standard library in Lisp (`src/functions.cl`)
**Build System**: GNU Make

## Essential Commands

### Building

```bash
make              # Build with optimizations (-O2)
make debug        # Build with debug symbols (-g -O0 -DDEBUG)
make clean        # Remove build artifacts
```

**Build Output**:
- Binary: `./crushlisp` (root directory)
- Objects: `build/*.o`

**Compiler**: gcc with flags: `-std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L`

### Running

```bash
make run          # Build and launch REPL
./crushlisp       # Direct REPL execution
./crushlisp -s    # Silent mode (suppress eval results)
./crushlisp -h    # Help message
```

**Input Methods**:
- Interactive REPL (when stdin is tty)
- Pipe: `echo "(+ 1 2 3)" | ./crushlisp`
- Redirect: `./crushlisp < script.lisp`

### Testing

```bash
make test         # Run all built-in tests
```

**Test Suite** (in Makefile):
- Arithmetic: `(+ 1 2 3)` → `6`
- Variables: `(def x 10) (* x x)` → `100`
- Conditionals: `(if (< 5 10) "yes" "no")` → `yes`
- Lists: `(list 1 2 3)` → `(1 2 3)`
- Vectors: `[3 4 5]` → `[3 4 5]`
- Vector evaluation: `[(+ 1 2) (* 3 4)]` → `[3 12]`
- Vector collection ops: `(def v [1 2 3]) (first v)` → `1`
- Silent mode: `-s` flag behavior
- Stack overflow protection

Tests use shell commands with `grep` to verify output.

### Installation

```bash
make install      # Install to /usr/local/bin/ (requires permissions)
```

## Project Structure

```
CrushLisp/
├── src/
│   ├── crushlisp.c      # Complete interpreter implementation (~1625 lines)
│   └── functions.cl     # Standard library (currently: map)
├── build/               # Build artifacts (generated)
├── Makefile             # Build configuration
├── README.md            # User documentation
├── .gitignore           # Git ignore patterns
└── AGENTS.md            # This file
```

## Code Architecture

### Core Components (all in `crushlisp.c`)

1. **Data Structures** (lines 12-70):
   - `ValueType` enum: NIL, NUMBER, BOOL, STRING, SYMBOL, LIST, VECTOR, FUNCTION, NATIVE_FUNCTION
   - `Value` struct: Tagged union with type and data
   - `Env` struct: Environment with bindings chain (lexical scoping)
   - `Binding` struct: Linked list of name-value pairs

2. **Memory Management** (lines 103-190):
   - `checked_malloc/checked_realloc`: Allocation with OOM handling
   - `copy_text`: String duplication
   - `StringBuilder`: Dynamic string building (for output)
   - No garbage collection - values are never freed (simple but leaky)

3. **Value Constructors** (lines 192-242):
   - Singleton values: `VALUE_NIL`, `VALUE_TRUE`, `VALUE_FALSE`
   - `make_number`, `make_string_owned`, `make_symbol`, `make_native`, `make_function`
   - `cons`: List construction (car/cdr pairs)
   - `vcons`: Vector construction (car/cdr pairs)

4. **Parser** (lines 518-747):
   - `parse_expr`: Entry point, returns `ParseStatus` (OK, INCOMPLETE, ERROR, END)
   - Supports: numbers, strings, symbols, lists `()`, vectors `[]`
   - Lists `()` create TYPE_LIST, vectors `[]` create TYPE_VECTOR
   - String escapes: `\n \r \t \\ \"`
   - Comments: `;` to end of line
   - Quote sugar: `'x` → `(quote x)`
   - No map literals `{}` (explicitly rejected)

5. **Evaluator** (lines 749-1068):
   - `eval`: Main evaluation dispatch (898-985)
   - **Stack overflow protection**: `current_eval_depth` counter, max 1000 (lines 100-101, 903-908)
   - Special forms: `quote`, `if`, `def`, `let`, `fn`, `do`
   - Lists evaluate as function calls
   - Vectors evaluate their contents but return as vectors (not function calls)
   - Lexical scoping with closure support
   - Tail-call-unsafe (no TCO)

6. **Special Forms**:
   - `quote` (754-764): Return without evaluation
   - `if` (766-791): Conditional with optional else
   - `def` (797-820): Global binding (uses `env_global`)
   - `let` (822-859): Local bindings (creates child env)
   - `fn` (876-896): Anonymous functions (closure over current env)
   - `do` (793-795): Sequential evaluation (implicit body)

7. **Built-in Functions** (lines 1070-1496):
   - Arithmetic: `+`, `-`, `*`, `/`, `mod`, `inc`, `dec`
   - Comparisons: `=`, `<`, `<=`, `>`, `>=`
   - Lists: `list`, `first`, `rest`, `cons`, `conj`, `count`, `nth`
   - Strings: `str` (concatenate)
   - I/O: `print`, `println` (flush stdout)
   - Meta: `help`

8. **REPL** (lines 1531-1600):
   - `repl`: Read-eval-print loop with continuation support
   - Detects interactive mode via `isatty(STDIN_FILENO)`
   - Multi-line input: accumulates when `PARSE_INCOMPLETE`
   - Prompts: `CrushLisp> ` (normal), `... ` (continuation)
   - Silent mode: skips printing eval results (but `print`/`println` still work)

9. **Main** (lines 1602-1625):
   - Parses `-s` (silent) and `-h/--help` flags
   - Creates global env, installs builtins, starts REPL

### Standard Library (`functions.cl`)

Currently contains:
- `map`: Higher-order function for list transformation
  ```lisp
  (def map (fn (f coll)
             (if (= coll nil)
               nil
               (cons (f (first coll))
                     (map f (rest coll))))))
  ```

**Note**: This file is NOT automatically loaded by the interpreter. Users must manually load it or copy definitions into their session.

## Coding Conventions

### C Style

1. **Naming**:
   - Functions: `snake_case` (e.g., `value_to_string`, `env_create`)
   - Types: `PascalCase` (e.g., `ValueType`, `StringBuilder`)
   - Constants: `SCREAMING_SNAKE_CASE` (e.g., `MAX_EVAL_DEPTH`, `HELP_TEXT`)
   - Static globals: `snake_case` with descriptive names

2. **Memory**:
   - Use `checked_malloc`/`checked_realloc` for all allocations
   - String allocation via `copy_text` helper
   - Never free values (no GC, permanent allocation model)
   - StringBuilder pattern for dynamic string building

3. **Error Handling**:
   - Functions return `NULL` on error
   - Error messages via `set_error(char **error, const char *fmt, ...)`
   - Always check error parameter: `if (error && *error)` before continuing
   - Error strings are heap-allocated, caller's responsibility (REPL frees them)

4. **Code Organization**:
   - Forward declarations for mutually recursive functions (line 749-752)
   - Static functions throughout (no external linkage except `main`)
   - Helpers before callers when possible
   - Grouped by functionality (parsing, eval, builtins, etc.)

5. **Indentation**: 4 spaces, no tabs

6. **Comments**: Minimal. Code is self-documenting with clear names.

### Lisp Style (for `.cl` files)

1. **Formatting**:
   - 2-space indentation within forms
   - Closing parens on same line as last element
   - Align parameters vertically in multi-line definitions

2. **Naming**: `kebab-case` (not enforced, but conventional)

## Important Patterns & Gotchas

### Value Type Checking

Always check types before accessing union fields:

```c
if (value && value->type == TYPE_NUMBER) {
    double n = value->data.number;
    // ...
}
```

### List Iteration

Standard pattern:

```c
Value *iter = list;
while (!is_nil(iter)) {
    if (!is_list(iter)) {
        // Handle improper list
        break;
    }
    Value *item = iter->data.list.car;
    // Process item
    iter = iter->data.list.cdr;
}
```

### Error Propagation

```c
Value *result = some_operation(args, env, error);
if (!result || (error && *error)) {
    return NULL;  // Propagate error upward
}
```

### Number Comparison

Use `EPSILON` for floating-point equality:

```c
if (fabs(a - b) < EPSILON) {
    // Numbers are equal
}
```

### StringBuilder Usage

```c
StringBuilder sb;
sb_init(&sb);
sb_append(&sb, "text");
sb_append_char(&sb, 'x');
char *result = sb_take(&sb);  // Transfers ownership
free(result);  // Caller must free
```

### Stack Overflow Protection

- Recursion limit: 1000 eval calls (lines 100-101)
- `current_eval_depth` incremented at start of `eval`, decremented before return
- Error message: "Stack overflow: recursion depth exceeded"
- Applied to ALL evaluation (user code and built-ins calling eval)

### Environment Lookup

Environments chain via `parent` pointers (lexical scoping):
- `env_get`: Searches current env, then parent recursively (lines 312-323)
- `env_define`: Only modifies current env (lines 288-302)
- `env_global`: Walks to root for `def` bindings (lines 304-310)

### Value Printing

Two modes via `value_to_string(value, readable)`:
- `readable=1`: Strings with quotes and escapes (REPL output)
- `readable=0`: Raw strings (for `str`, `print`, `println`)

## Testing Strategy

### Running Tests

```bash
make test
```

### Adding Tests

Edit `Makefile` test target. Pattern:

```makefile
@echo "LISP_CODE" | ./$(TARGET) | grep -q "EXPECTED" && echo "✓ Test name"
```

For stderr:
```makefile
@echo "LISP_CODE" | ./$(TARGET) 2>&1 | grep -q "ERROR_TEXT" && echo "✓ Test name"
```

### Manual Testing

```bash
./crushlisp
CrushLisp> (your code here)
```

Or:
```bash
echo "(test expression)" | ./crushlisp
```

## Common Development Tasks

### Adding a Built-in Function

1. **Implement function** (follow pattern in lines 1070-1465):
   ```c
   static Value *builtin_yourfunc(Value *args, Env *env, char **error) {
       (void)env;  // If unused
       // Validate args
       if (is_nil(args)) {
           set_error(error, "yourfunc expects arguments");
           return NULL;
       }
       // Extract values with type checking
       // Compute result
       return result_value;
   }
   ```

2. **Register in `install_builtins`** (add line ~1472-1496):
   ```c
   register_builtin(env, "yourfunc", builtin_yourfunc, "yourfunc");
   ```

3. **Update `HELP_TEXT`** (lines 73-94) if user-facing

4. **Add test** in Makefile

### Adding a Special Form

1. **Implement evaluator** (follow pattern lines 754-896):
   ```c
   static Value *eval_yourform(Value *args, Env *env, char **error) {
       // Parse args (unevaluated!)
       // Evaluate as needed
       return result;
   }
   ```

2. **Add dispatch in `eval`** (lines 930-962):
   ```c
   if (strcmp(name, "yourform") == 0) {
       result = eval_yourform(args, env, error);
       break;
   }
   ```

3. **Update `HELP_TEXT`**

4. **Add test**

### Modifying Parser

- Parser is hand-written recursive descent (lines 518-747)
- Entry: `parse_expr_internal`
- Returns: `ParseStatus` enum (OK, INCOMPLETE, ERROR, END)
- State is maintained via `*index` pointer into input string
- `skip_ignored` handles whitespace and comments

### Debugging

Build with debug symbols:
```bash
make debug
gdb ./crushlisp
```

GDB tips:
- Break on `eval`: `break eval`
- Break on error: `break set_error`
- Print value: `call value_to_string($value_ptr, 1)` then `x/s $`

## Memory Model

**Critical**: CrushLisp has no garbage collection. All allocated values persist until program exit. This is acceptable for:
- REPL sessions (finite lifespan)
- Short scripts
- Learning/experimentation

**Not suitable for**:
- Long-running servers
- Processing large datasets
- Production use without GC

When extending the interpreter, maintain this pattern (don't add free logic without implementing full GC).

## Differences from Other Lisps

1. **No macros** (no compile-time code generation)
2. **No TCO** (tail calls consume stack - respects recursion limit)
3. **No continuations** (`call/cc` not supported)
4. **Numbers are doubles** (no integer type, no bignums)
5. **No keywords** (symbols only)
6. **Vectors vs Lists**: `[]` creates TYPE_VECTOR (data), `()` creates TYPE_LIST (code)
   - Vectors evaluate their contents but never as function calls
   - Lists evaluate as function calls when in expression position
7. **No destructuring** (in `let` or `fn` params)
8. **Limited standard library** (only `map` in functions.cl)

## Git Workflow

**Current branch**: master
**Recent commits**:
- `239adba` - display strings with quotes in repl
- `249c480` - implement map
- `e83652e` - add stack overflow protection

**Commit style**: Short imperative mood ("implement X", "add Y", "fix Z")

**Status**: Repository is currently clean (no uncommitted changes).

## Platform Notes

**Target**: Linux (POSIX)
**Tested**: Primary development on Linux
**Dependencies**:
- `libc` (standard C library)
- `libm` (math library, linked with `-lm`)
- POSIX functions: `isatty`, `getline`

**Portability**: Should work on macOS/BSD with minor adjustments. Windows requires POSIX compatibility layer (Cygwin, WSL, MinGW).

## Performance Characteristics

- **Parsing**: Linear in input size (single-pass, no backtracking)
- **Evaluation**: Recursive, bounded by `MAX_EVAL_DEPTH` (1000)
- **Environment lookup**: Linear in chain depth (no hash tables)
- **List operations**: Linear traversal (no random access except `nth`)
- **Memory**: Unbounded growth (no GC)

Optimization opportunities (if needed):
- Hash tables for environments (large codebases)
- Tail call optimization (deep recursion)
- Garbage collection (long-running processes)

## Useful Development Commands

```bash
# Quick test cycle
make && echo "(+ 1 2 3)" | ./crushlisp

# Check for warnings
make clean && make 2>&1 | grep warning

# Count lines of code
wc -l src/crushlisp.c

# Find function definition
grep -n "^static Value \*builtin_" src/crushlisp.c

# Search for pattern usage
grep -n "set_error" src/crushlisp.c

# Test specific feature interactively
./crushlisp
```

## When to Update This Document

- Adding/removing commands (Makefile targets, CLI flags)
- Changing architecture (new modules, major refactors)
- Adding language features (special forms, built-ins)
- Modifying conventions (style, patterns)
- Discovering non-obvious behaviors (gotchas, edge cases)

Keep this document synchronized with code changes for future agent effectiveness.
