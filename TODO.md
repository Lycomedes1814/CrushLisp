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

### Garbage Collection — Implementation Plan

**Strategy**: stop-the-world mark-and-sweep, triggered between top-level evaluations.
The GC never runs mid-eval, so the only live roots at collection time are bindings in
`global_environment` and the singletons (`VALUE_NIL/TRUE/FALSE`).

#### Phase 1 — Value mark-and-sweep

1. **Extend `Value` struct** (lines ~46–68): add two fields
   ```c
   int    gc_marked;   /* mark bit */
   Value *gc_next;     /* intrusive singly-linked list of all heap Values */
   ```

2. **Add global list head** near the singletons (line ~166):
   ```c
   static Value *gc_all_values = NULL;
   static size_t gc_alloc_count = 0;
   static const size_t GC_THRESHOLD = 10000; /* tune as needed */
   ```

3. **Register every allocation** in `allocate_value()` (line ~261):
   ```c
   value->gc_marked = 0;
   value->gc_next   = gc_all_values;
   gc_all_values    = value;
   gc_alloc_count++;
   ```

4. **`gc_mark(Value *v)`** — recursive mark, guards on `marked` to break cycles:
   - Skip singletons (`== &VALUE_NIL` etc.) — they are static, never freed.
   - Set `v->gc_marked = 1`, then recurse into children based on type:
     - `TYPE_LIST / TYPE_VECTOR / TYPE_MAP`: mark `car` and `cdr`
     - `TYPE_FUNCTION`: mark `params`, `body`, and call `gc_mark_env(env)`
     - `TYPE_RECUR`: mark `list.car`
     - Leaf types (`NUMBER`, `BOOL`, `STRING`, `SYMBOL`, `NATIVE_FUNCTION`): no children

5. **`gc_mark_env(Env *env)`** — walk the parent chain; for each `Env`, iterate its
   `Binding *` list and call `gc_mark(binding->value)`. (Envs themselves are not
   freed in Phase 1 — see Phase 2.)

6. **`gc_sweep()`** — traverse `gc_all_values`, unlink and free unmarked nodes:
   ```c
   Value **cursor = &gc_all_values;
   while (*cursor) {
       Value *v = *cursor;
       if (v->gc_marked) {
           v->gc_marked = 0;   /* reset for next cycle */
           cursor = &v->gc_next;
       } else {
           *cursor = v->gc_next;
           /* free owned strings */
           if (v->type == TYPE_STRING || v->type == TYPE_SYMBOL)
               free(v->data.string);
           free(v);
       }
   }
   ```

7. **`gc_collect()`** — orchestrate a cycle:
   ```c
   gc_mark_env(global_environment);
   gc_sweep();
   gc_alloc_count = 0;
   ```

8. **Trigger point**: call `gc_collect()` in two places:
   - `repl()` — after printing each result, before reading the next line.
   - Pipe/file mode (`main`) — after each top-level expression is evaluated.
   - Optionally also trigger inside `allocate_value()` when
     `gc_alloc_count >= GC_THRESHOLD` (opportunistic mid-script GC).

9. **Tests**: add a Makefile test that runs a tight loop allocating many values
   and checks that the process does not grow without bound (use `/usr/bin/time -v`
   or check `/proc/self/status` via `(sh ...)`).

#### Phase 2 — Environment GC (follow-up)

Envs and Bindings are also heap-allocated and currently never freed. After Phase 1
is working, add a parallel intrusive list for `Env` nodes:

- Add `gc_marked` + `gc_next` to `struct Env`.
- `env_create()` registers each new `Env` in `gc_all_envs`.
- During `gc_mark_env()`, set `env->gc_marked = 1` before recursing into parent.
- Add `gc_sweep_envs()`: free unmarked `Env` nodes and all their `Binding` chains
  (freeing `binding->name` strings too).
- `global_environment` and the root env must always be treated as roots even if
  not reachable through a Value (hold a permanent mark, or special-case them).

#### Phase 3 — GC root stack (optional, for mid-eval GC)

If the `GC_THRESHOLD` trigger fires mid-eval, C-stack temporaries won't be marked.
Guard against this with an explicit root stack:

```c
static Value **gc_roots[4096];
static int    gc_root_count = 0;
#define GC_PUSH(v) (gc_roots[gc_root_count++] = &(v))
#define GC_POP()   (gc_root_count--)
```

Call `gc_mark(*gc_roots[i])` for each root before sweeping.  This is only needed
if threshold-triggered GC is enabled; skip-between-evals approach avoids it entirely.

## Longer term

- [ ] Network I/O — at minimum HTTP via `curl` wrapping, or a socket primitive
