# CLAUDE.md - CrushLisp AI Assistant Guide

This document provides everything an AI assistant needs to work effectively on the CrushLisp codebase.

## Project Overview

CrushLisp is a Clojure-inspired Lisp interpreter written in C11. It features a REPL, lexical scoping, closures, vectors, file I/O, shell integration, and runtime safety protections. The entire interpreter is ~2360 lines in a single C file.

**Stack**: C11, POSIX, GNU Make  
**Platform**: Linux/POSIX (POSIX functions: `isatty`, `getline`)  
**Files**: `src/crushlisp.c` (interpreter), `src/functions.cl` (optional stdlib)

## Essential Commands

```bash
make              # Build with -O2 optimizations → produces ./crushlisp
make debug        # Build with -g -O0 -DDEBUG
make clean        # Remove build/ and ./crushlisp binary
make test         # Run all 25 tests (all must pass)
make run          # Build and launch REPL
make install      # Install to /usr/local/bin/

./crushlisp       # Interactive REPL (detects tty)
./crushlisp -s    # Silent mode (suppress eval output; print/println still work)
./crushlisp -h    # Help text
echo "(+ 1 2)" | ./crushlisp  # Pipe mode
./crushlisp < script.lisp     # File redirect
```

**After any code change**: always run `make && make test` to verify zero warnings and all tests pass.

## Repository Structure

```
CrushLisp/
├── src/
│   ├── crushlisp.c      # Complete interpreter (~2360 lines)
│   └── functions.cl     # Optional stdlib: map, filter (NOT auto-loaded)
├── examples/
│   ├── eval_demo.lisp   # Demonstrates eval and dynamic code
│   └── file_io.lisp     # Demonstrates slurp/spit/load
├── build/               # Build artifacts (generated, gitignored)
├── Makefile             # Build + test configuration
├── README.md            # User documentation
├── AGENTS.md            # Legacy development guide (may be slightly out of date)
└── CLAUDE.md            # This file
```

## Code Architecture (`src/crushlisp.c`)

### Data Structures (lines ~12–77)

```c
typedef enum { NIL, NUMBER, BOOL, STRING, SYMBOL, LIST, VECTOR, FUNCTION, NATIVE_FUNCTION } ValueType;

typedef struct Value {
    ValueType type;
    union {
        double number;
        bool boolean;
        char *text;          // STRING, SYMBOL
        struct { struct Value *car, *cdr; } list;  // LIST and VECTOR
        struct { char **params; int param_count; struct Value *body; struct Env *closure; } fn;
        struct { char *name; struct Value *(*func)(struct Value*, struct Env*, char**); } native;
    } data;
} Value;

typedef struct Env { struct Binding *bindings; struct Env *parent; } Env;
```

### Key Singleton Values

```c
VALUE_NIL    // nil
VALUE_TRUE   // true
VALUE_FALSE  // false
```

### Component Summary

| Component | Location | Purpose |
|-----------|----------|---------|
| Memory / StringBuilder | ~123–207 | `checked_malloc`, `sb_*` utilities |
| Value constructors | ~209–340 | `make_number`, `cons`, `vcons`, etc. |
| Environment | ~285–330 | `env_create`, `env_get`, `env_define`, `env_global` |
| Printer | ~340–520 | `value_to_string(value, readable)` |
| Parser | ~660–871 | `parse_expr` — returns `ParseStatus` |
| Evaluator | ~873–1192 | `eval`, special forms |
| Built-ins | ~1194–2200 | 40+ native functions |
| REPL | ~2200–2330 | `repl()` — multi-line, continuation prompts |
| Main | ~2336–2360 | Flag parsing, env init |

### Special Forms (evaluated without evaluating args first)

| Form | Syntax | Notes |
|------|--------|-------|
| `quote` | `(quote x)` or `'x` | Returns unevaluated |
| `if` | `(if test then [else])` | else is optional → returns nil |
| `def` | `(def name value)` | Binds in global env |
| `let` | `(let [name val ...] body...)` | Local scope; `[]` or `()` accepted |
| `fn` | `(fn [params...] body...)` | Closure; `[]` or `()` for params; variadic: `(fn [a & rest] ...)` |
| `do` | `(do expr...)` | Sequential eval, returns last |
| `and` | `(and expr...)` | Short-circuit; returns last truthy or first falsy |
| `or` | `(or expr...)` | Short-circuit; returns first truthy or last falsy |

### Built-in Functions (40+)

```
Arithmetic:     +  -  *  /  mod  inc  dec
Comparison:     =  <  <=  >  >=
Collections:    list  first  rest  cons  conj  count  nth
Strings:        str  split
I/O:            print  println  slurp  spit  load
Eval:           eval
System:         sh  run
Meta:           help
Type predicates: nil?  number?  string?  bool?  symbol?  list?  vector?  fn?
Logic:          not
```

**`sh` vs `run`**:
- `sh`: `(sh "cmd | pipe")` — runs via `popen`, supports shell features
- `run`: `(run "prog" "arg1")` — uses `fork`+`execvp`, no shell, safer for untrusted input

**`eval`**: accepts a Value or a string. Strings are parsed then evaluated.

**`split`**: splits a string, returns a list.

**`load`**: reads a file with `slurp` then evals all expressions in it.

### Memory Model

No garbage collection. All `Value*` allocations are permanent (never freed). This is intentional — acceptable for REPL sessions and scripts. Do **not** add `free()` calls for Values unless implementing full GC.

- Always use `checked_malloc`/`checked_realloc` (abort on OOM)
- Use `copy_text()` for string duplication
- Use `StringBuilder` for dynamic string assembly; caller frees result of `sb_take()`

### Stack Overflow Protection

Global counter `current_eval_depth`, max 1000. Incremented at entry to `eval`, decremented on all return paths. Error: `"Stack overflow: recursion depth exceeded"`.

Special forms (`if`, `let`, `do`, `and`, `or`) use `goto tco_loop` inside `eval` to avoid recursing for their tail positions — this does not affect the depth counter. User-defined function calls do **not** use TCO; each call increments `current_eval_depth` normally, so infinite recursion is always caught.

### Environment Scoping

- `env_get`: searches current env, then walks `parent` chain (lexical scope)
- `env_define`: only modifies the current env
- `env_global`: walks to root — used by `def`
- Native functions are called with `env=NULL`; use `global_environment` static if needed

## Coding Conventions

### C Style

- **Functions**: `snake_case` (e.g., `builtin_conj`, `env_create`)
- **Types**: `PascalCase` (e.g., `ValueType`, `StringBuilder`)
- **Constants**: `SCREAMING_SNAKE_CASE` (e.g., `MAX_EVAL_DEPTH`)
- **Indentation**: 4 spaces, no tabs
- **Comments**: Minimal — prefer clear names

### Error Handling Pattern

```c
// Setting an error:
set_error(error, "yourfunc: expected number, got %s", type_name(val));
return NULL;

// Propagating an error:
Value *result = some_op(args, env, error);
if (!result || (error && *error)) return NULL;
```

### Type Checking Pattern

```c
if (!value || value->type != TYPE_NUMBER) {
    set_error(error, "expected number");
    return NULL;
}
double n = value->data.number;
```

### List Iteration Pattern

```c
Value *iter = list;
while (!is_nil(iter)) {
    if (!is_list(iter)) break;  // improper list guard
    Value *item = iter->data.list.car;
    // process item
    iter = iter->data.list.cdr;
}
```

### Printer: `value_to_string(value, readable)`

- `readable=1`: strings include quotes and escape sequences (REPL display)
- `readable=0`: raw strings (for `str`, `print`, `println`)

## Testing

### Running Tests

```bash
make test   # 25 shell-based tests, all must pass
```

### Adding a Test

Edit the `test` target in `Makefile`:

```makefile
@echo "LISP_EXPRESSION" | ./$(TARGET) | grep -q "EXPECTED_OUTPUT" && echo "✓ Test description" || (echo "✗ Test description" && exit 1)
```

For stderr (errors, stack overflow, etc.):
```makefile
@echo "LISP_EXPRESSION" | ./$(TARGET) 2>&1 | grep -q "ERROR_TEXT" && echo "✓ ..." || (echo "✗ ..." && exit 1)
```

### Current Test Coverage

Arithmetic, variables, conditionals, lists, vectors, vector evaluation, vector collection ops, functions with vector params, silent mode, stack overflow, eval, arity validation, shell exit status, file I/O (slurp/spit roundtrip), load, let with vector syntax, conj on vectors, cons on vectors, and all 8 type predicates.

## Common Development Tasks

### Adding a Built-in Function

1. **Implement** (follow existing patterns in lines ~1194–2200):
   ```c
   static Value *builtin_yourfunc(Value *args, Env *env, char **error) {
       (void)env;  // env is always NULL for native functions
       if (is_nil(args)) {
           set_error(error, "yourfunc: requires at least one argument");
           return NULL;
       }
       Value *first = args->data.list.car;
       // type check, compute, return result
       return make_number(42.0);
   }
   ```

2. **Register** in `install_builtins()`:
   ```c
   register_builtin(env, "yourfunc", builtin_yourfunc, "yourfunc");
   ```

3. **Update `HELP_TEXT`** (near top of file) if user-facing.

4. **Add a test** in `Makefile`.

5. **Run** `make && make test`.

### Adding a Special Form

1. **Implement** an `eval_yourform(Value *args, Env *env, char **error)` function. Args are **unevaluated**.

2. **Dispatch** in the `eval` function's special-form block:
   ```c
   if (strcmp(name, "yourform") == 0) {
       result = eval_yourform(args, env, error);
       break;
   }
   ```

3. Update `HELP_TEXT`, add test, run `make && make test`.

### Modifying the Parser

- Parser: `parse_expr` / `parse_expr_internal` (~lines 660–871)
- Hand-written recursive descent; returns `ParseStatus` (OK, INCOMPLETE, ERROR, END)
- State tracked via `*index` pointer into input string
- `skip_ignored()` handles whitespace and `;` comments
- Map literals `{}` are **explicitly rejected**

### Debugging

```bash
make debug
gdb ./crushlisp
(gdb) break eval
(gdb) break set_error
(gdb) run
```

Quick cycles:
```bash
make && echo "(your expression)" | ./crushlisp
make clean && make 2>&1 | grep warning   # Check for new warnings
```

## Language Reference (CrushLisp)

### Data Types

| Type | Examples | Notes |
|------|---------|-------|
| Number | `42`, `3.14`, `-1` | All doubles |
| Bool | `true`, `false` | |
| String | `"hello\nworld"` | Escapes: `\n \r \t \\ \"` |
| Symbol | `foo`, `my-var` | Evaluates to bound value |
| List | `(1 2 3)`, `nil` | `()` = nil; evaluates as call |
| Vector | `[1 2 3]` | Evaluates contents; never a call |
| nil | `nil` | Empty list; falsy |
| Function | `(fn [x] x)` | Closure |

### Evaluation Rules

- **Lists** in expression position: head evaluated as function, tail as args
- **Vectors** in expression position: contents evaluated, returned as vector (not a call)
- **`nil` and `false`** are the only falsy values
- **Symbols**: looked up in lexical env chain

### Standard Library (`src/functions.cl`)

Embedded as `STDLIB_SOURCE` in `crushlisp.c` and evaluated automatically at startup before the REPL begins. `src/functions.cl` is kept as the canonical source; if you edit it, sync the embedded copy in `STDLIB_SOURCE`.

```lisp
(def map    (fn [f coll] ...))   ; list transformation
(def filter (fn [f coll] ...))   ; list filtering
```

## Differences from Clojure/Standard Lisps

- No macros, no `defmacro`
- No tail-call optimization (TCO) for user functions (special forms use internal goto loop)
- No continuations (`call/cc`)
- All numbers are doubles (no integer type, no bignums)
- No keywords (`:foo`); use symbols
- No map literals `{}` (explicitly rejected by parser)
- No destructuring in `let` or `fn` params; variadic `& rest` is supported in `fn`
- `fn` accepts `[]` or `()` for parameter lists
- `let` accepts `[]` or `()` for binding vector
- Recursion limit: 1000 eval depth

## Git Conventions

**Commit style**: Short imperative mood — `"add split"`, `"fix null handling"`, `"implement eval"`  
**Branch**: `master` for main development  
**Clean state**: Repository should be clean before committing new features

## Performance Characteristics

- Parsing: O(n) single-pass, no backtracking
- Eval: recursive, bounded at depth 1000
- Env lookup: O(depth) linked list (no hash tables)
- List ops: O(n) traversal; `nth` is O(n)
- Memory: unbounded growth (no GC)

Optimization opportunities (not currently implemented): hash-table envs, full TCO for user functions, GC.
