#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum {
    TYPE_NIL,
    TYPE_NUMBER,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_SYMBOL,
    TYPE_LIST,
    TYPE_VECTOR,
    TYPE_FUNCTION,
    TYPE_NATIVE_FUNCTION,
    TYPE_RECUR,      /* sentinel: carries recur args back to loop */
    TYPE_MAP         /* linked list of (key . value) pairs */
} ValueType;

typedef struct Value Value;
typedef struct Env Env;
typedef struct Binding Binding;
typedef Value *(*NativeFn)(Value *, Env *, char **);

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} StringBuilder;

typedef enum {
    PARSE_OK,
    PARSE_INCOMPLETE,
    PARSE_ERROR,
    PARSE_END
} ParseStatus;

struct Value {
    ValueType type;
    union {
        double number;
        int boolean;
        char *string;
        struct {
            Value *car;
            Value *cdr;
        } list;
        struct {
            Value *car;
            Value *cdr;
        } vector;
        struct {
            Value *params;
            Value *body;
            Env *env;
        } function;
        NativeFn native;
    } data;
    const char *doc;
};

struct Binding {
    char *name;
    Value *value;
    Binding *next;
};

struct Env {
    Binding *bindings;
    Env *parent;
};

static const double EPSILON = 1e-9;
static const char *STDLIB_SOURCE =
"(def map (fn [f coll]"
"  (if (= coll nil)"
"    nil"
"    (cons (f (first coll)) (map f (rest coll))))))"
"(def filter (fn [f coll]"
"  (if (= coll nil)"
"    nil"
"    (if (f (first coll))"
"      (cons (first coll) (filter f (rest coll)))"
"      (filter f (rest coll))))))"
;

static const char *HELP_TEXT =
"CrushLisp special forms:\n"
"  (quote x)              return x without evaluation\n"
"  (if test then [else])  conditional; else optional, defaults to nil\n"
"  (when test body...)    if truthy, eval body, else nil\n"
"  (def name value)       bind a global name\n"
"  (let [name val ...] body...) scoped locals\n"
"  (fn [params...] body...) anonymous function; variadic: (fn [a & rest] ...)\n"
"  (do expr...)           evaluate sequentially, return last\n"
"  (and expr...)          short-circuit and\n"
"  (or expr...)           short-circuit or\n"
"  (loop [name val ...] body...) iteration; use recur to jump back\n"
"  (recur args...)        jump to enclosing loop with new values\n"
"  (try body (catch e handler...)) catch errors thrown by throw\n"
"  (throw message)        signal an error\n\n"
"Data structures:\n"
"  (list values...)       list\n"
"  [values...]            vector\n"
"  (hash-map k v ...)     map\n\n"
"Functions:\n"
"  (+ - * / mod inc dec)  arithmetic\n"
"  (= < <= > >=)          comparisons\n"
"  (not x)                logical negation\n"
"  (apply f arg... list)  call f with args spliced from final list\n"
"  (reduce f init coll)   fold collection\n"
"  (map f coll)           transform collection (stdlib)\n"
"  (filter f coll)        filter collection (stdlib)\n"
"  (first coll)           first element\n"
"  (rest coll)            remaining elements\n"
"  (cons x coll)          prepend value\n"
"  (conj coll values...)  append values\n"
"  (count coll)           collection size\n"
"  (nth coll index)       element at index\n"
"  (get map key [default]) look up key in map (or index in list/vector)\n"
"  (assoc map k v ...)    add/update map entries\n"
"  (dissoc map k ...)     remove map entries\n"
"  (keys map)             list of keys\n"
"  (vals map)             list of values\n"
"  (contains? map key)    true if map has key\n"
"  (str values...)        concatenate as strings\n"
"  (split s delim)        split string on one-character delimiter\n"
"  (print values...)      write without newline\n"
"  (println values...)    write with newline\n"
"  (eval expr)            evaluate expression or string\n"
"  (slurp filename)       read file contents\n"
"  (spit filename content) write string to file\n"
"  (load filename)        read and evaluate file\n"
"  (sh command)           execute shell command, return output\n"
"  (run program args...)  execute program directly (no shell)\n"
"  (help)                 show this message\n\n"
"Type predicates:\n"
"  (nil? x)  (number? x)  (string? x)  (bool? x)  (symbol? x)\n"
"  (list? x) (vector? x)  (fn? x)      (map? x)\n";

static Value VALUE_NIL = { TYPE_NIL, {0}, NULL };
static Value VALUE_TRUE = { TYPE_BOOL, { .boolean = 1 }, NULL };
static Value VALUE_FALSE = { TYPE_BOOL, { .boolean = 0 }, NULL };

static const int MAX_EVAL_DEPTH = 1000;
static int current_eval_depth = 0;
static Env *global_environment = NULL;

static void *checked_realloc(void *ptr, size_t size) {
    void *result = realloc(ptr, size);
    if (!result) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return result;
}

static void *checked_malloc(size_t size) {
    return checked_realloc(NULL, size);
}

static char *copy_text(const char *text) {
    size_t len = text ? strlen(text) : 0;
    char *copy = checked_malloc(len + 1);
    if (len > 0) {
        memcpy(copy, text, len);
    }
    copy[len] = '\0';
    return copy;
}

static void set_error(char **error, const char *fmt, ...) {
    if (!error) {
        return;
    }
    if (*error) {
        free(*error);
    }
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    char *message = checked_malloc((size_t)needed + 1);
    vsnprintf(message, (size_t)needed + 1, fmt, args);
    va_end(args);
    *error = message;
}

static void sb_init(StringBuilder *sb) {
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
}

static void sb_reserve(StringBuilder *sb, size_t needed) {
    if (needed <= sb->capacity) {
        return;
    }
    size_t capacity = sb->capacity ? sb->capacity : 32;
    while (capacity < needed) {
        capacity *= 2;
    }
    sb->data = checked_realloc(sb->data, capacity);
    sb->capacity = capacity;
}

static void sb_append(StringBuilder *sb, const char *text) {
    size_t len = text ? strlen(text) : 0;
    sb_reserve(sb, sb->length + len + 1);
    if (len > 0) {
        memcpy(sb->data + sb->length, text, len);
    }
    sb->length += len;
    sb->data[sb->length] = '\0';
}

static void sb_append_char(StringBuilder *sb, char c) {
    sb_reserve(sb, sb->length + 2);
    sb->data[sb->length++] = c;
    sb->data[sb->length] = '\0';
}

static char *sb_take(StringBuilder *sb) {
    if (!sb->data) {
        return copy_text("");
    }
    char *result = sb->data;
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
    return result;
}

static Value *allocate_value(ValueType type) {
    Value *value = checked_malloc(sizeof(Value));
    value->type = type;
    value->doc = NULL;
    return value;
}

static Value *value_nil(void) {
    return &VALUE_NIL;
}

static Value *value_true(void) {
    return &VALUE_TRUE;
}

static Value *value_false(void) {
    return &VALUE_FALSE;
}

static Value *make_number(double number) {
    Value *value = allocate_value(TYPE_NUMBER);
    value->data.number = number;
    return value;
}

static Value *make_string_owned(char *text) {
    Value *value = allocate_value(TYPE_STRING);
    value->data.string = text ? text : copy_text("");
    return value;
}

static Value *make_symbol(const char *name) {
    Value *value = allocate_value(TYPE_SYMBOL);
    value->data.string = copy_text(name);
    return value;
}

static Value *make_native(NativeFn fn, const char *doc) {
    Value *value = allocate_value(TYPE_NATIVE_FUNCTION);
    value->data.native = fn;
    value->doc = doc;
    return value;
}

static Value *make_function(Value *params, Value *body, Env *env) {
    Value *value = allocate_value(TYPE_FUNCTION);
    value->data.function.params = params;
    value->data.function.body = body;
    value->data.function.env = env;
    return value;
}

static Value *make_recur(Value *args) {
    Value *value = allocate_value(TYPE_RECUR);
    value->data.list.car = args;  /* reuse list.car to hold the recur args */
    value->data.list.cdr = NULL;
    return value;
}

static int is_nil(Value *value) {
    return value == NULL || value->type == TYPE_NIL;
}

static int is_list(Value *value) {
    return value && value->type == TYPE_LIST;
}

static int is_vector(Value *value) {
    return value && value->type == TYPE_VECTOR;
}

static Value *cons(Value *car, Value *cdr) {
    Value *value = allocate_value(TYPE_LIST);
    value->data.list.car = car;
    value->data.list.cdr = cdr ? cdr : value_nil();
    return value;
}

static Value *vcons(Value *car, Value *cdr) {
    Value *value = allocate_value(TYPE_VECTOR);
    value->data.vector.car = car;
    value->data.vector.cdr = cdr ? cdr : value_nil();
    return value;
}

static int value_equals(Value *a, Value *b);  /* forward declaration */

/* Map: TYPE_MAP node whose list.car holds a list of (key . val) cons pairs */
static Value *make_empty_map(void) {
    Value *m = allocate_value(TYPE_MAP);
    m->data.list.car = value_nil();  /* pair list */
    m->data.list.cdr = value_nil();
    return m;
}

static int is_map(Value *v) { return v && v->type == TYPE_MAP; }

/* Return the value for key in map, or NULL if not found */
static Value *map_get(Value *map, Value *key) {
    Value *pairs = map->data.list.car;
    while (!is_nil(pairs)) {
        Value *pair = pairs->data.list.car;  /* (key . val) cons */
        if (value_equals(pair->data.list.car, key)) return pair->data.list.cdr;
        pairs = pairs->data.list.cdr;
    }
    return NULL;
}

/* Return new map with key -> val set (copy-on-write) */
static Value *map_assoc(Value *map, Value *key, Value *val) {
    Value *new_map = make_empty_map();
    /* Copy existing pairs, skipping old binding for key */
    Value *src = map->data.list.car;
    Value *head = value_nil();
    Value *tail = NULL;
    while (!is_nil(src)) {
        Value *pair = src->data.list.car;
        if (!value_equals(pair->data.list.car, key)) {
            Value *node = cons(pair, value_nil());
            if (!tail) head = node; else tail->data.list.cdr = node;
            tail = node;
        }
        src = src->data.list.cdr;
    }
    /* Prepend new pair */
    Value *new_pair = cons(key, val);
    Value *new_node = cons(new_pair, head);
    new_map->data.list.car = new_node;
    return new_map;
}

/* Return new map with key removed */
static Value *map_dissoc(Value *map, Value *key) {
    Value *new_map = make_empty_map();
    Value *src = map->data.list.car;
    Value *head = value_nil();
    Value *tail = NULL;
    while (!is_nil(src)) {
        Value *pair = src->data.list.car;
        if (!value_equals(pair->data.list.car, key)) {
            Value *node = cons(pair, value_nil());
            if (!tail) head = node; else tail->data.list.cdr = node;
            tail = node;
        }
        src = src->data.list.cdr;
    }
    new_map->data.list.car = head;
    return new_map;
}

static size_t list_length(Value *list) {
    size_t count = 0;
    Value *current = list;
    while (!is_nil(current)) {
        if (!is_list(current)) {
            break;
        }
        count += 1;
        current = current->data.list.cdr;
    }
    return count;
}

static int require_exact_args(Value *args, size_t expected, char **error, const char *name) {
    size_t count = 0;
    Value *iter = args;
    while (!is_nil(iter)) {
        if (!is_list(iter)) {
            set_error(error, "%s received a malformed argument list", name);
            return 0;
        }
        count += 1;
        iter = iter->data.list.cdr;
    }
    if (count != expected) {
        set_error(error, "%s expects exactly %zu argument%s", name, expected, expected == 1 ? "" : "s");
        return 0;
    }
    return 1;
}

static int require_arg_range(Value *args, size_t minimum, size_t maximum, char **error, const char *name) {
    size_t count = 0;
    Value *iter = args;
    while (!is_nil(iter)) {
        if (!is_list(iter)) {
            set_error(error, "%s received a malformed argument list", name);
            return 0;
        }
        count += 1;
        iter = iter->data.list.cdr;
    }
    if (count < minimum || count > maximum) {
        if (minimum == maximum) {
            set_error(error, "%s expects exactly %zu argument%s", name, minimum, minimum == 1 ? "" : "s");
        } else {
            set_error(error, "%s expects between %zu and %zu arguments", name, minimum, maximum);
        }
        return 0;
    }
    return 1;
}

static Value *list_from_array(Value **items, size_t count) {
    Value *result = value_nil();
    for (size_t i = 0; i < count; ++i) {
        size_t idx = count - 1 - i;
        result = cons(items[idx], result);
    }
    return result;
}

static Value *vector_from_array(Value **items, size_t count) {
    Value *result = value_nil();
    for (size_t i = 0; i < count; ++i) {
        size_t idx = count - 1 - i;
        result = vcons(items[idx], result);
    }
    return result;
}

static Env *env_create(Env *parent) {
    Env *env = checked_malloc(sizeof(Env));
    env->bindings = NULL;
    env->parent = parent;
    return env;
}

static void env_define(Env *env, const char *name, Value *value) {
    Binding *current = env->bindings;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            current->value = value;
            return;
        }
        current = current->next;
    }
    Binding *binding = checked_malloc(sizeof(Binding));
    binding->name = copy_text(name);
    binding->value = value;
    binding->next = env->bindings;
    env->bindings = binding;
}

static Env *env_global(Env *env) {
    Env *current = env;
    while (current && current->parent) {
        current = current->parent;
    }
    return current;
}

static Value *env_get(Env *env, const char *name) {
    for (Env *current = env; current; current = current->parent) {
        Binding *binding = current->bindings;
        while (binding) {
            if (strcmp(binding->name, name) == 0) {
                return binding->value;
            }
            binding = binding->next;
        }
    }
    return NULL;
}

static int is_truthy(Value *value) {
    if (is_nil(value)) {
        return 0;
    }
    if (value->type == TYPE_BOOL) {
        return value->data.boolean != 0;
    }
    return 1;
}

static int value_equals(Value *a, Value *b) {
    if (is_nil(a) && is_nil(b)) {
        return 1;
    }
    if (is_nil(a) || is_nil(b)) {
        return 0;
    }
    if (a->type != b->type) {
        return 0;
    }
    switch (a->type) {
        case TYPE_NIL:
            return 1;
        case TYPE_NUMBER:
            return a->data.number == b->data.number;
        case TYPE_BOOL:
            return a->data.boolean == b->data.boolean;
        case TYPE_STRING:
        case TYPE_SYMBOL:
            return strcmp(a->data.string, b->data.string) == 0;
        case TYPE_LIST: {
            Value *curr_a = a;
            Value *curr_b = b;
            while (!is_nil(curr_a) && !is_nil(curr_b)) {
                if (!is_list(curr_a) || !is_list(curr_b)) {
                    break;
                }
                if (!value_equals(curr_a->data.list.car, curr_b->data.list.car)) {
                    return 0;
                }
                curr_a = curr_a->data.list.cdr;
                curr_b = curr_b->data.list.cdr;
            }
            return is_nil(curr_a) && is_nil(curr_b);
        }
        case TYPE_VECTOR: {
            Value *curr_a = a;
            Value *curr_b = b;
            while (!is_nil(curr_a) && !is_nil(curr_b)) {
                if (!is_vector(curr_a) || !is_vector(curr_b)) {
                    break;
                }
                if (!value_equals(curr_a->data.vector.car, curr_b->data.vector.car)) {
                    return 0;
                }
                curr_a = curr_a->data.vector.cdr;
                curr_b = curr_b->data.vector.cdr;
            }
            return is_nil(curr_a) && is_nil(curr_b);
        }
        case TYPE_FUNCTION:
        case TYPE_NATIVE_FUNCTION:
        case TYPE_RECUR:
        case TYPE_MAP:
            return a == b;
    }
    return 0;
}

static void append_number(StringBuilder *sb, double number) {
    if (isinf(number)) {
        sb_append(sb, number > 0 ? "Inf" : "-Inf");
        return;
    }
    if (isnan(number)) {
        sb_append(sb, "NaN");
        return;
    }
    // Only use integer representation if the value fits in a long long
    if (fabs(number) < 9.2e18) {
        long long integral = llround(number);
        if (fabs(number - (double)integral) < EPSILON) {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "%lld", integral);
            sb_append(sb, buffer);
            return;
        }
    }
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.10g", number);
    sb_append(sb, buffer);
}

static void write_value(StringBuilder *sb, Value *value, int readable) {
    if (is_nil(value)) {
        sb_append(sb, "nil");
        return;
    }
    switch (value->type) {
        case TYPE_NUMBER:
            append_number(sb, value->data.number);
            break;
        case TYPE_BOOL:
            sb_append(sb, value->data.boolean ? "true" : "false");
            break;
        case TYPE_STRING:
            if (readable) {
                sb_append_char(sb, '"');
                const char *text = value->data.string ? value->data.string : "";
                for (size_t i = 0; text[i]; ++i) {
                    char c = text[i];
                    switch (c) {
                        case '\\':
                            sb_append(sb, "\\\\");
                            break;
                        case '"':
                            sb_append(sb, "\\\"");
                            break;
                        case '\n':
                            sb_append(sb, "\\n");
                            break;
                        case '\r':
                            sb_append(sb, "\\r");
                            break;
                        case '\t':
                            sb_append(sb, "\\t");
                            break;
                        default:
                            sb_append_char(sb, c);
                            break;
                    }
                }
                sb_append_char(sb, '"');
            } else {
                sb_append(sb, value->data.string ? value->data.string : "");
            }
            break;
        case TYPE_SYMBOL:
            sb_append(sb, value->data.string ? value->data.string : "");
            break;
        case TYPE_LIST: {
            sb_append_char(sb, '(');
            Value *current = value;
            int first = 1;
            while (!is_nil(current)) {
                if (!is_list(current)) {
                    sb_append(sb, " . ");
                    write_value(sb, current, readable);
                    break;
                }
                if (!first) {
                    sb_append_char(sb, ' ');
                }
                write_value(sb, current->data.list.car, readable);
                current = current->data.list.cdr;
                first = 0;
            }
            sb_append_char(sb, ')');
            break;
        }
        case TYPE_VECTOR: {
            sb_append_char(sb, '[');
            Value *current = value;
            int first = 1;
            while (!is_nil(current)) {
                if (!is_vector(current)) {
                    sb_append(sb, " . ");
                    write_value(sb, current, readable);
                    break;
                }
                if (!first) {
                    sb_append_char(sb, ' ');
                }
                write_value(sb, current->data.vector.car, readable);
                current = current->data.vector.cdr;
                first = 0;
            }
            sb_append_char(sb, ']');
            break;
        }
        case TYPE_FUNCTION:
            sb_append(sb, "<function>");
            break;
        case TYPE_NATIVE_FUNCTION:
            if (value->doc) {
                sb_append(sb, "<native ");
                sb_append(sb, value->doc);
                sb_append_char(sb, '>');
            } else {
                sb_append(sb, "<native>");
            }
            break;
        case TYPE_NIL:
            sb_append(sb, "nil");
            break;
        case TYPE_RECUR:
            sb_append(sb, "<recur>");
            break;
        case TYPE_MAP: {
            sb_append_char(sb, '{');
            Value *pairs = value->data.list.car;
            int first = 1;
            while (!is_nil(pairs)) {
                Value *pair = pairs->data.list.car;
                if (!first) sb_append_char(sb, ' ');
                write_value(sb, pair->data.list.car, readable);
                sb_append_char(sb, ' ');
                write_value(sb, pair->data.list.cdr, readable);
                first = 0;
                pairs = pairs->data.list.cdr;
            }
            sb_append_char(sb, '}');
            break;
        }
    }
}

static char *value_to_string(Value *value, int readable) {
    StringBuilder sb;
    sb_init(&sb);
    write_value(&sb, value, readable);
    return sb_take(&sb);
}

static int value_to_number(Value *value, double *out, char **error, const char *context) {
    if (!value || value->type != TYPE_NUMBER) {
        set_error(error, "%s expects numbers", context);
        return 0;
    }
    *out = value->data.number;
    return 1;
}

static int value_to_integer(Value *value, long long *out, char **error, const char *context) {
    double number;
    if (!value_to_number(value, &number, error, context)) {
        return 0;
    }
    double rounded = round(number);
    if (fabs(number - rounded) > EPSILON) {
        set_error(error, "%s expects integer inputs", context);
        return 0;
    }
    *out = (long long)rounded;
    return 1;
}

static int value_to_index(Value *value, size_t *out, char **error, const char *context) {
    long long integer;
    if (!value_to_integer(value, &integer, error, context)) {
        return 0;
    }
    if (integer < 0) {
        set_error(error, "%s expects non-negative index", context);
        return 0;
    }
    *out = (size_t)integer;
    return 1;
}

static size_t skip_ignored(const char *input, size_t index) {
    while (input[index]) {
        char c = input[index];
        if (c == ';') {
            while (input[index] && input[index] != '\n') {
                index += 1;
            }
            continue;
        }
        if (isspace((unsigned char)c) || c == ',') {
            index += 1;
            continue;
        }
        break;
    }
    return index;
}

static ParseStatus parse_expr_internal(const char *input, size_t *index, Value **out, char **error);

static Value *parse_number_token(const char *input, size_t *index) {
    char *endptr = NULL;
    double number = strtod(input + *index, &endptr);
    if (endptr == input + *index) {
        return NULL;
    }
    *index = (size_t)(endptr - input);
    return make_number(number);
}

static Value *parse_string_token(const char *input, size_t *index, char **error) {
    (void)error;
    size_t i = *index + 1;
    StringBuilder sb;
    sb_init(&sb);
    while (1) {
        char c = input[i];
        if (c == '\0') {
            return NULL;
        }
        if (c == '"') {
            i += 1;
            break;
        }
        if (c == '\\') {
            char next = input[i + 1];
            if (next == '\0') {
                return NULL;
            }
            switch (next) {
                case 'n':
                    sb_append_char(&sb, '\n');
                    break;
                case 'r':
                    sb_append_char(&sb, '\r');
                    break;
                case 't':
                    sb_append_char(&sb, '\t');
                    break;
                case '\\':
                    sb_append_char(&sb, '\\');
                    break;
                case '"':
                    sb_append_char(&sb, '"');
                    break;
                default:
                    sb_append_char(&sb, next);
                    break;
            }
            i += 2;
            continue;
        }
        sb_append_char(&sb, c);
        i += 1;
    }
    *index = i;
    return make_string_owned(sb_take(&sb));
}

static int is_symbol_char(char c) {
    switch (c) {
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case '"':
        case '\'':
        case ';':
        case ',':
            return 0;
        default:
            return !isspace((unsigned char)c) && c != '\0';
    }
}

static Value *parse_symbol_token(const char *input, size_t *index) {
    size_t start = *index;
    while (is_symbol_char(input[*index])) {
        *index += 1;
    }
    size_t len = *index - start;
    if (len == 0) {
        return NULL;
    }
    char *text = checked_malloc(len + 1);
    memcpy(text, input + start, len);
    text[len] = '\0';
    if (strcmp(text, "nil") == 0) {
        free(text);
        return value_nil();
    }
    if (strcmp(text, "true") == 0) {
        free(text);
        return value_true();
    }
    if (strcmp(text, "false") == 0) {
        free(text);
        return value_false();
    }
    Value *symbol = make_symbol(text);
    free(text);
    return symbol;
}

static ParseStatus parse_list_data(const char *input, size_t *index, char closing, int is_vector, Value **out, char **error) {
    size_t i = *index + 1;
    Value **items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    while (1) {
        i = skip_ignored(input, i);
        char c = input[i];
        if (c == '\0') {
            free(items);
            return PARSE_INCOMPLETE;
        }
        if (c == closing) {
            i += 1;
            break;
        }
        Value *element = NULL;
        size_t item_index = i;
        ParseStatus status = parse_expr_internal(input, &item_index, &element, error);
        if (status != PARSE_OK) {
            free(items);
            return status;
        }
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 4;
            items = checked_realloc(items, capacity * sizeof(Value *));
        }
        items[count++] = element;
        i = item_index;
    }
    *index = i;
    Value *result = is_vector ? vector_from_array(items, count) : list_from_array(items, count);
    free(items);
    *out = result;
    return PARSE_OK;
}

static ParseStatus parse_expr_internal(const char *input, size_t *index, Value **out, char **error) {
    size_t i = skip_ignored(input, *index);
    *index = i;
    char c = input[i];
    if (c == '\0') {
        return PARSE_END;
    }
    if (c == '(') {
        return parse_list_data(input, index, ')', 0, out, error);
    }
    if (c == '[') {
        return parse_list_data(input, index, ']', 1, out, error);
    }
    if (c == ')') {
        set_error(error, "Unexpected )");
        return PARSE_ERROR;
    }
    if (c == ']') {
        set_error(error, "Unexpected ]");
        return PARSE_ERROR;
    }
    if (c == '{' || c == '}') {
        set_error(error, "Maps are not supported");
        return PARSE_ERROR;
    }
    if (c == '\'') {
        size_t next_index = i + 1;
        Value *quoted = NULL;
        ParseStatus status = parse_expr_internal(input, &next_index, &quoted, error);
        if (status != PARSE_OK) {
            return status;
        }
        Value *symbol = make_symbol("quote");
        Value *tail = cons(quoted, value_nil());
        Value *list = cons(symbol, tail);
        *index = next_index;
        *out = list;
        return PARSE_OK;
    }
    if (c == '"') {
        Value *string = parse_string_token(input, index, error);
        if (!string) {
            return PARSE_INCOMPLETE;
        }
        *out = string;
        return PARSE_OK;
    }
    Value *number = parse_number_token(input, index);
    if (number) {
        *out = number;
        return PARSE_OK;
    }
    Value *symbol = parse_symbol_token(input, index);
    if (!symbol) {
        set_error(error, "Unable to parse token");
        return PARSE_ERROR;
    }
    *out = symbol;
    return PARSE_OK;
}

static ParseStatus parse_expr(const char *input, size_t *consumed, Value **out, char **error) {
    size_t index = 0;
    ParseStatus status = parse_expr_internal(input, &index, out, error);
    *consumed = index;
    return status;
}

static Value *eval(Value *expr, Env *env, char **error);
static Value *eval_args(Value *list, Env *env, char **error);
static Value *vector_to_list(Value *vec);

static Value *eval_quote(Value *args, char **error) {
    if (!require_exact_args(args, 1, error, "quote")) {
        return NULL;
    }
    return args->data.list.car;
}

static Value *eval_def(Value *args, Env *env, char **error) {
    if (!require_exact_args(args, 2, error, "def")) {
        return NULL;
    }
    Value *name_form = args->data.list.car;
    Value *rest = args->data.list.cdr;
    if (!name_form || name_form->type != TYPE_SYMBOL) {
        set_error(error, "def name must be a symbol");
        return NULL;
    }
    Value *value_expr = rest->data.list.car;
    Value *evaluated = eval(value_expr, env, error);
    if (!evaluated || (error && *error)) {
        return NULL;
    }
    Env *target = env_global(env);
    env_define(target, name_form->data.string, evaluated);
    return evaluated;
}

static int params_are_symbols(Value *params) {
    Value *iter = params;
    int seen_rest = 0;
    int rest_count = 0;
    while (!is_nil(iter)) {
        Value *item = NULL;
        if (is_list(iter)) {
            item = iter->data.list.car;
            iter = iter->data.list.cdr;
        } else if (is_vector(iter)) {
            item = iter->data.vector.car;
            iter = iter->data.vector.cdr;
        } else {
            return 0;
        }
        if (!item || item->type != TYPE_SYMBOL) {
            return 0;
        }
        if (strcmp(item->data.string, "&") == 0) {
            if (seen_rest) return 0;  /* & used twice */
            seen_rest = 1;
        } else if (seen_rest) {
            rest_count++;
            if (rest_count > 1) return 0;  /* more than one param after & */
        }
    }
    if (seen_rest && rest_count == 0) return 0;  /* & with no following param */
    return 1;
}

static Value *vector_to_list(Value *vec) {
    if (is_nil(vec)) {
        return value_nil();
    }
    if (is_list(vec)) {
        return vec;
    }
    if (!is_vector(vec)) {
        return vec;
    }
    Value **items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    Value *iter = vec;
    while (!is_nil(iter)) {
        if (!is_vector(iter)) {
            break;
        }
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 4;
            items = checked_realloc(items, capacity * sizeof(Value *));
        }
        items[count++] = iter->data.vector.car;
        iter = iter->data.vector.cdr;
    }
    Value *result = list_from_array(items, count);
    free(items);
    return result;
}

static Value *eval_fn(Value *args, Env *env, char **error) {
    if (is_nil(args) || !is_list(args)) {
        set_error(error, "fn expects parameters and body");
        return NULL;
    }
    Value *params = args->data.list.car;
    if (!(is_nil(params) || is_list(params) || is_vector(params))) {
        set_error(error, "fn parameters must be a list or vector");
        return NULL;
    }
    if (!params_are_symbols(params)) {
        set_error(error, "fn parameters must be symbols");
        return NULL;
    }
    Value *body = args->data.list.cdr;
    if (is_nil(body)) {
        set_error(error, "fn requires a body");
        return NULL;
    }
    // Convert vector params to list for internal consistency
    Value *params_list = vector_to_list(params);
    return make_function(params_list, body, env);
}

static Value *eval_args(Value *list, Env *env, char **error) {
    Value *head = value_nil();
    Value *tail = NULL;
    Value *iter = list;
    while (!is_nil(iter)) {
        if (!is_list(iter)) {
            set_error(error, "Invalid argument list");
            return NULL;
        }
        Value *value = eval(iter->data.list.car, env, error);
        if (!value || (error && *error)) {
            return NULL;
        }
        Value *node = cons(value, value_nil());
        if (!tail) {
            head = node;
        } else {
            tail->data.list.cdr = node;
        }
        tail = node;
        iter = iter->data.list.cdr;
    }
    return head;
}

/* Bind args to params in frame, supporting variadic & rest parameter.
   Returns 1 on success, 0 on error (error string set via *error). */
static int bind_params(Value *params, Value *args, Env *frame, char **error) {
    Value *param_iter = params;
    Value *arg_iter = args;
    /* count required params for error messages */
    size_t required = 0;
    int has_rest = 0;
    {
        Value *pi = params;
        while (!is_nil(pi)) {
            Value *pn = pi->data.list.car;
            if (pn->type == TYPE_SYMBOL && strcmp(pn->data.string, "&") == 0) {
                has_rest = 1;
                break;
            }
            required++;
            pi = pi->data.list.cdr;
        }
    }
    while (!is_nil(param_iter)) {
        Value *pname = param_iter->data.list.car;
        if (pname->type == TYPE_SYMBOL && strcmp(pname->data.string, "&") == 0) {
            param_iter = param_iter->data.list.cdr;
            if (is_nil(param_iter)) {
                set_error(error, "& must be followed by a parameter name");
                return 0;
            }
            Value *rest_name = param_iter->data.list.car;
            env_define(frame, rest_name->data.string, arg_iter);
            return 1;
        }
        if (is_nil(arg_iter)) {
            size_t actual = list_length(args);
            set_error(error, "Expected %s%zu argument%s, got %zu",
                      has_rest ? "at least " : "", required,
                      required == 1 ? "" : "s", actual);
            return 0;
        }
        env_define(frame, pname->data.string, arg_iter->data.list.car);
        param_iter = param_iter->data.list.cdr;
        arg_iter = arg_iter->data.list.cdr;
    }
    if (!is_nil(arg_iter)) {
        size_t actual = list_length(args);
        set_error(error, "Too many arguments: expected %zu, got %zu", required, actual);
        return 0;
    }
    return 1;
}

static Value *eval(Value *expr, Env *env, char **error) {
    if (!expr) {
        return value_nil();
    }

    current_eval_depth++;
    if (current_eval_depth > MAX_EVAL_DEPTH) {
        current_eval_depth--;
        set_error(error, "Stack overflow: recursion depth exceeded");
        return NULL;
    }

    Value *result = NULL;

tco_loop:
    result = NULL;

    if (!expr) {
        result = value_nil();
        goto done;
    }

    switch (expr->type) {
        case TYPE_NUMBER:
        case TYPE_BOOL:
        case TYPE_STRING:
        case TYPE_FUNCTION:
        case TYPE_NATIVE_FUNCTION:
        case TYPE_NIL:
            result = expr;
            goto done;
        case TYPE_VECTOR: {
            if (is_nil(expr)) {
                result = expr;
                goto done;
            }
            Value *viter = expr;
            Value **items = NULL;
            size_t count = 0;
            size_t capacity = 0;
            int had_error = 0;
            while (!is_nil(viter)) {
                if (!is_vector(viter)) {
                    set_error(error, "Malformed vector");
                    had_error = 1;
                    break;
                }
                Value *element = viter->data.vector.car;
                Value *evaluated = eval(element, env, error);
                if (!evaluated || (error && *error)) {
                    had_error = 1;
                    break;
                }
                if (count == capacity) {
                    capacity = capacity ? capacity * 2 : 4;
                    items = checked_realloc(items, capacity * sizeof(Value *));
                }
                items[count++] = evaluated;
                viter = viter->data.vector.cdr;
            }
            if (had_error) {
                free(items);
            } else {
                result = vector_from_array(items, count);
                free(items);
            }
            goto done;
        }
        case TYPE_SYMBOL: {
            Value *value = env_get(env, expr->data.string);
            if (!value) {
                set_error(error, "Undefined symbol %s", expr->data.string);
            } else {
                result = value;
            }
            goto done;
        }
        case TYPE_LIST: {
            if (is_nil(expr)) {
                result = expr;
                goto done;
            }
            Value *op = expr->data.list.car;
            Value *args = expr->data.list.cdr;
            if (op && op->type == TYPE_SYMBOL) {
                const char *sname = op->data.string;

                if (strcmp(sname, "quote") == 0) {
                    result = eval_quote(args, error);
                    goto done;
                }
                if (strcmp(sname, "if") == 0) {
                    if (!require_arg_range(args, 2, 3, error, "if")) goto done;
                    Value *test_e = args->data.list.car;
                    Value *then_e = args->data.list.cdr->data.list.car;
                    Value *else_rest = args->data.list.cdr->data.list.cdr;
                    Value *cond_val = eval(test_e, env, error);
                    if (!cond_val || (error && *error)) goto done;
                    if (is_truthy(cond_val)) {
                        expr = then_e;
                    } else if (!is_nil(else_rest)) {
                        expr = else_rest->data.list.car;
                    } else {
                        result = value_nil();
                        goto done;
                    }
                    goto tco_loop;
                }
                if (strcmp(sname, "def") == 0) {
                    result = eval_def(args, env, error);
                    goto done;
                }
                if (strcmp(sname, "let") == 0) {
                    if (is_nil(args) || !is_list(args)) {
                        set_error(error, "let expects binding list and body");
                        goto done;
                    }
                    Value *binding_form = args->data.list.car;
                    if (!(is_nil(binding_form) || is_list(binding_form) || is_vector(binding_form))) {
                        set_error(error, "let bindings must be a list or vector");
                        goto done;
                    }
                    Value *bindings = is_vector(binding_form) ? vector_to_list(binding_form) : binding_form;
                    Env *child = env_create(env);
                    Value *biter = bindings;
                    int berr = 0;
                    while (!is_nil(biter)) {
                        if (!is_list(biter)) {
                            set_error(error, "let bindings must be pairs");
                            berr = 1; break;
                        }
                        Value *bname = biter->data.list.car;
                        biter = biter->data.list.cdr;
                        if (is_nil(biter)) {
                            set_error(error, "let binding missing value");
                            berr = 1; break;
                        }
                        Value *bexpr = biter->data.list.car;
                        biter = biter->data.list.cdr;
                        if (!bname || bname->type != TYPE_SYMBOL) {
                            set_error(error, "let binding name must be symbol");
                            berr = 1; break;
                        }
                        Value *bval = eval(bexpr, child, error);
                        if (!bval || (error && *error)) { berr = 1; break; }
                        env_define(child, bname->data.string, bval);
                    }
                    if (berr) goto done;
                    Value *let_body = args->data.list.cdr;
                    if (is_nil(let_body)) { result = value_nil(); goto done; }
                    Value *let_iter = let_body;
                    while (!is_nil(let_iter->data.list.cdr)) {
                        Value *v = eval(let_iter->data.list.car, child, error);
                        if (!v || (error && *error)) goto done;
                        let_iter = let_iter->data.list.cdr;
                    }
                    expr = let_iter->data.list.car;
                    env = child;
                    goto tco_loop;
                }
                if (strcmp(sname, "fn") == 0) {
                    result = eval_fn(args, env, error);
                    goto done;
                }
                if (strcmp(sname, "do") == 0) {
                    if (is_nil(args)) { result = value_nil(); goto done; }
                    Value *do_iter = args;
                    while (!is_nil(do_iter->data.list.cdr)) {
                        Value *v = eval(do_iter->data.list.car, env, error);
                        if (!v || (error && *error)) goto done;
                        do_iter = do_iter->data.list.cdr;
                    }
                    expr = do_iter->data.list.car;
                    goto tco_loop;
                }
                if (strcmp(sname, "and") == 0) {
                    if (is_nil(args)) { result = value_true(); goto done; }
                    Value *and_iter = args;
                    while (!is_nil(and_iter->data.list.cdr)) {
                        Value *v = eval(and_iter->data.list.car, env, error);
                        if (!v || (error && *error)) goto done;
                        if (!is_truthy(v)) { result = v; goto done; }
                        and_iter = and_iter->data.list.cdr;
                    }
                    expr = and_iter->data.list.car;
                    goto tco_loop;
                }
                if (strcmp(sname, "or") == 0) {
                    if (is_nil(args)) { result = value_nil(); goto done; }
                    Value *or_iter = args;
                    while (!is_nil(or_iter->data.list.cdr)) {
                        Value *v = eval(or_iter->data.list.car, env, error);
                        if (!v || (error && *error)) goto done;
                        if (is_truthy(v)) { result = v; goto done; }
                        or_iter = or_iter->data.list.cdr;
                    }
                    expr = or_iter->data.list.car;
                    goto tco_loop;
                }
                /* when: (when test body...) — if test is truthy, eval body, else nil */
                if (strcmp(sname, "when") == 0) {
                    if (is_nil(args)) { result = value_nil(); goto done; }
                    Value *w_cond = eval(args->data.list.car, env, error);
                    if (error && *error) goto done;
                    if (!is_truthy(w_cond)) { result = value_nil(); goto done; }
                    Value *w_body = args->data.list.cdr;
                    if (is_nil(w_body)) { result = value_nil(); goto done; }
                    Value *witer = w_body;
                    while (!is_nil(witer->data.list.cdr)) {
                        Value *v = eval(witer->data.list.car, env, error);
                        if (!v || (error && *error)) goto done;
                        witer = witer->data.list.cdr;
                    }
                    expr = witer->data.list.car;
                    goto tco_loop;
                }
                /* throw: (throw message) — signal an error */
                if (strcmp(sname, "throw") == 0) {
                    if (!require_exact_args(args, 1, error, "throw")) goto done;
                    Value *msg = eval(args->data.list.car, env, error);
                    if (error && *error) goto done;
                    char *msg_str = value_to_string(msg, 0);
                    set_error(error, "%s", msg_str);
                    free(msg_str);
                    goto done;
                }
                /* try: (try body (catch e handler...))
                   Evaluates body; if it errors, binds the message to e and runs handler. */
                if (strcmp(sname, "try") == 0) {
                    if (is_nil(args)) { result = value_nil(); goto done; }
                    Value *try_body = args->data.list.car;
                    Value *catch_form = is_nil(args->data.list.cdr) ? NULL
                                       : args->data.list.cdr->data.list.car;
                    char *try_err = NULL;
                    current_eval_depth = 0;
                    Value *try_result = eval(try_body, env, &try_err);
                    if (try_err) {
                        /* Error occurred — run catch clause if present */
                        if (!catch_form || !is_list(catch_form) ||
                            is_nil(catch_form) ||
                            catch_form->data.list.car->type != TYPE_SYMBOL ||
                            strcmp(catch_form->data.list.car->data.string, "catch") != 0) {
                            /* No valid catch — re-raise */
                            set_error(error, "%s", try_err);
                            free(try_err);
                            goto done;
                        }
                        Value *catch_args = catch_form->data.list.cdr;
                        if (is_nil(catch_args) || !is_list(catch_args) ||
                            catch_args->data.list.car->type != TYPE_SYMBOL) {
                            set_error(error, "catch expects a binding name");
                            free(try_err);
                            goto done;
                        }
                        const char *bind_name = catch_args->data.list.car->data.string;
                        Value *handler_body = catch_args->data.list.cdr;
                        Env *catch_env = env_create(env);
                        Value *err_val = make_string_owned(try_err); /* try_err ownership transferred */
                        env_define(catch_env, bind_name, err_val);
                        if (is_nil(handler_body)) { result = value_nil(); goto done; }
                        Value *hiter = handler_body;
                        while (!is_nil(hiter->data.list.cdr)) {
                            Value *v = eval(hiter->data.list.car, catch_env, error);
                            if (!v || (error && *error)) goto done;
                            hiter = hiter->data.list.cdr;
                        }
                        result = eval(hiter->data.list.car, catch_env, error);
                        goto done;
                    }
                    result = try_result;
                    goto done;
                }
                /* recur: evaluate args and return a RECUR sentinel */
                if (strcmp(sname, "recur") == 0) {
                    Value *recur_args = eval_args(args, env, error);
                    if (error && *error) goto done;
                    result = make_recur(recur_args);
                    goto done;
                }
                /* loop: (loop [bindings...] body...)
                   Like let but loops when body returns a RECUR sentinel. */
                if (strcmp(sname, "loop") == 0) {
                    if (is_nil(args) || !is_list(args)) {
                        set_error(error, "loop expects binding vector and body");
                        goto done;
                    }
                    Value *binding_form = args->data.list.car;
                    if (!(is_nil(binding_form) || is_list(binding_form) || is_vector(binding_form))) {
                        set_error(error, "loop bindings must be a list or vector");
                        goto done;
                    }
                    Value *bindings = is_vector(binding_form) ? vector_to_list(binding_form) : binding_form;
                    /* Collect binding names so we can rebind on recur */
                    size_t nbinds = 0;
                    Value *bcount_iter = bindings;
                    while (!is_nil(bcount_iter)) {
                        nbinds++;
                        bcount_iter = bcount_iter->data.list.cdr;
                        if (!is_nil(bcount_iter)) { nbinds--; bcount_iter = bcount_iter->data.list.cdr; nbinds++; }
                    }
                    /* Re-count properly: pairs */
                    nbinds = 0;
                    bcount_iter = bindings;
                    while (!is_nil(bcount_iter) && !is_nil(bcount_iter->data.list.cdr)) {
                        nbinds++;
                        bcount_iter = bcount_iter->data.list.cdr->data.list.cdr;
                    }
                    char **bind_names = checked_malloc(nbinds * sizeof(char *));
                    Value *loop_body = args->data.list.cdr;
                    /* Initial binding evaluation */
                    Env *loop_frame = env_create(env);
                    {
                        Value *biter = bindings;
                        int berr = 0;
                        size_t bi = 0;
                        while (!is_nil(biter)) {
                            Value *bname = biter->data.list.car;
                            biter = biter->data.list.cdr;
                            if (is_nil(biter)) { set_error(error, "loop binding missing value"); berr = 1; break; }
                            Value *bexpr = biter->data.list.car;
                            biter = biter->data.list.cdr;
                            if (!bname || bname->type != TYPE_SYMBOL) {
                                set_error(error, "loop binding name must be a symbol");
                                berr = 1; break;
                            }
                            Value *bval = eval(bexpr, loop_frame, error);
                            if (!bval || (error && *error)) { berr = 1; break; }
                            env_define(loop_frame, bname->data.string, bval);
                            bind_names[bi++] = bname->data.string;
                        }
                        if (berr) { free(bind_names); goto done; }
                    }
                    /* Loop: eval body; if result is RECUR, rebind and repeat */
                    while (1) {
                        if (is_nil(loop_body)) { result = value_nil(); break; }
                        Value *liter = loop_body;
                        Value *lresult = value_nil();
                        int lerr = 0;
                        while (!is_nil(liter)) {
                            lresult = eval(liter->data.list.car, loop_frame, error);
                            if (!lresult || (error && *error)) { lerr = 1; break; }
                            liter = liter->data.list.cdr;
                        }
                        if (lerr) { result = NULL; break; }
                        if (lresult && lresult->type == TYPE_RECUR) {
                            /* Rebind loop vars with recur args */
                            Value *recur_args = lresult->data.list.car;
                            size_t ri = 0;
                            Value *riter = recur_args;
                            while (ri < nbinds && !is_nil(riter)) {
                                env_define(loop_frame, bind_names[ri], riter->data.list.car);
                                ri++; riter = riter->data.list.cdr;
                            }
                            if (ri != nbinds || !is_nil(riter)) {
                                set_error(error, "recur: wrong number of arguments for loop");
                                result = NULL; break;
                            }
                        } else {
                            result = lresult;
                            break;
                        }
                    }
                    free(bind_names);
                    goto done;
                }
            }
            /* function call */
            {
                Value *callable = eval(op, env, error);
                if (!callable || (error && *error)) goto done;
                Value *eargs = eval_args(args, env, error);
                if (!eargs || (error && *error)) goto done;
                if (callable->type == TYPE_NATIVE_FUNCTION) {
                    result = callable->data.native(eargs, NULL, error);
                    goto done;
                }
                if (callable->type == TYPE_FUNCTION) {
                    Env *frame = env_create(callable->data.function.env);
                    if (!bind_params(callable->data.function.params, eargs, frame, error)) {
                        goto done;
                    }
                    Value *fn_body = callable->data.function.body;
                    if (is_nil(fn_body)) { result = value_nil(); goto done; }
                    Value *fn_iter = fn_body;
                    while (!is_nil(fn_iter->data.list.cdr)) {
                        Value *v = eval(fn_iter->data.list.car, frame, error);
                        if (!v || (error && *error)) goto done;
                        fn_iter = fn_iter->data.list.cdr;
                    }
                    result = eval(fn_iter->data.list.car, frame, error);
                    goto done;
                }
                set_error(error, "Value is not callable");
                goto done;
            }
        }
        default:
            set_error(error, "Unsupported expression");
            goto done;
    }

done:
    current_eval_depth--;
    return result;
}

static Value *builtin_add(Value *args, Env *env, char **error) {
    (void)env;
    double sum = 0.0;
    Value *iter = args;
    while (!is_nil(iter)) {
        double number;
        if (!value_to_number(iter->data.list.car, &number, error, "+")) {
            return NULL;
        }
        sum += number;
        iter = iter->data.list.cdr;
    }
    return make_number(sum);
}

static Value *builtin_sub(Value *args, Env *env, char **error) {
    (void)env;
    if (is_nil(args)) {
        set_error(error, "- expects at least one argument");
        return NULL;
    }
    double first;
    if (!value_to_number(args->data.list.car, &first, error, "-")) {
        return NULL;
    }
    Value *rest = args->data.list.cdr;
    if (is_nil(rest)) {
        return make_number(-first);
    }
    double result = first;
    Value *iter = rest;
    while (!is_nil(iter)) {
        double number;
        if (!value_to_number(iter->data.list.car, &number, error, "-")) {
            return NULL;
        }
        result -= number;
        iter = iter->data.list.cdr;
    }
    return make_number(result);
}

static Value *builtin_mul(Value *args, Env *env, char **error) {
    (void)env;
    double product = 1.0;
    Value *iter = args;
    while (!is_nil(iter)) {
        double number;
        if (!value_to_number(iter->data.list.car, &number, error, "*")) {
            return NULL;
        }
        product *= number;
        iter = iter->data.list.cdr;
    }
    return make_number(product);
}

static Value *builtin_div(Value *args, Env *env, char **error) {
    (void)env;
    if (is_nil(args)) {
        set_error(error, "/ expects at least one argument");
        return NULL;
    }
    double first;
    if (!value_to_number(args->data.list.car, &first, error, "/")) {
        return NULL;
    }
    Value *rest = args->data.list.cdr;
    if (is_nil(rest)) {
        if (fabs(first) < EPSILON) {
            set_error(error, "Division by zero");
            return NULL;
        }
        return make_number(1.0 / first);
    }
    double result = first;
    Value *iter = rest;
    while (!is_nil(iter)) {
        double number;
        if (!value_to_number(iter->data.list.car, &number, error, "/")) {
            return NULL;
        }
        if (fabs(number) < EPSILON) {
            set_error(error, "Division by zero");
            return NULL;
        }
        result /= number;
        iter = iter->data.list.cdr;
    }
    return make_number(result);
}

static Value *builtin_mod(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 2, error, "mod")) {
        return NULL;
    }
    double a;
    double b;
    if (!value_to_number(args->data.list.car, &a, error, "mod") ||
        !value_to_number(args->data.list.cdr->data.list.car, &b, error, "mod")) {
        return NULL;
    }
    if (fabs(b) < EPSILON) {
        set_error(error, "mod divisor cannot be zero");
        return NULL;
    }
    return make_number(fmod(a, b));
}

static Value *builtin_inc(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "inc")) {
        return NULL;
    }
    double number;
    if (!value_to_number(args->data.list.car, &number, error, "inc")) {
        return NULL;
    }
    return make_number(number + 1.0);
}

static Value *builtin_dec(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "dec")) {
        return NULL;
    }
    double number;
    if (!value_to_number(args->data.list.car, &number, error, "dec")) {
        return NULL;
    }
    return make_number(number - 1.0);
}

static Value *builtin_equal(Value *args, Env *env, char **error) {
    (void)env;
    (void)error;
    Value *iter = args;
    if (is_nil(iter) || is_nil(iter->data.list.cdr)) {
        return value_true();
    }
    Value *prev = iter->data.list.car;
    iter = iter->data.list.cdr;
    while (!is_nil(iter)) {
        Value *current = iter->data.list.car;
        if (!value_equals(prev, current)) {
            return value_false();
        }
        prev = current;
        iter = iter->data.list.cdr;
    }
    return value_true();
}

static int compare_numbers(Value *args, char **error, const char *name, int (*comparator)(double, double)) {
    if (is_nil(args) || is_nil(args->data.list.cdr)) {
        set_error(error, "%s expects at least two numbers", name);
        return -1;
    }
    double prev;
    if (!value_to_number(args->data.list.car, &prev, error, name)) {
        return -1;
    }
    Value *iter = args->data.list.cdr;
    while (!is_nil(iter)) {
        double current;
        if (!value_to_number(iter->data.list.car, &current, error, name)) {
            return -1;
        }
        if (!comparator(prev, current)) {
            return 0;
        }
        prev = current;
        iter = iter->data.list.cdr;
    }
    return 1;
}

static int cmp_lt(double a, double b) { return a < b; }
static int cmp_lte(double a, double b) { return a <= b; }
static int cmp_gt(double a, double b) { return a > b; }
static int cmp_gte(double a, double b) { return a >= b; }

static Value *comparison_dispatch(Value *args, char **error, const char *name, int (*fn)(double, double)) {
    int result = compare_numbers(args, error, name, fn);
    if (result < 0) {
        return NULL;
    }
    return result ? value_true() : value_false();
}

static Value *builtin_lt(Value *args, Env *env, char **error) {
    (void)env;
    return comparison_dispatch(args, error, "<", cmp_lt);
}

static Value *builtin_lte(Value *args, Env *env, char **error) {
    (void)env;
    return comparison_dispatch(args, error, "<=", cmp_lte);
}

static Value *builtin_gt(Value *args, Env *env, char **error) {
    (void)env;
    return comparison_dispatch(args, error, ">", cmp_gt);
}

static Value *builtin_gte(Value *args, Env *env, char **error) {
    (void)env;
    return comparison_dispatch(args, error, ">=", cmp_gte);
}

static Value *builtin_list(Value *args, Env *env, char **error) {
    (void)env;
    (void)error;
    return args;
}

static Value *ensure_collection(Value *value, char **error, const char *name) {
    if (is_nil(value) || is_list(value) || is_vector(value)) {
        return value;
    }
    set_error(error, "%s expects a collection (list or vector)", name);
    return NULL;
}

static Value *coll_first(Value *coll) {
    if (is_nil(coll)) {
        return value_nil();
    }
    if (is_list(coll)) {
        return coll->data.list.car;
    }
    if (is_vector(coll)) {
        return coll->data.vector.car;
    }
    return value_nil();
}

static Value *coll_rest(Value *coll) {
    if (is_nil(coll)) {
        return value_nil();
    }
    if (is_list(coll)) {
        return coll->data.list.cdr;
    }
    if (is_vector(coll)) {
        return coll->data.vector.cdr;
    }
    return value_nil();
}

static size_t coll_length(Value *coll) {
    size_t count = 0;
    Value *current = coll;
    while (!is_nil(current)) {
        if (is_list(current)) {
            count += 1;
            current = current->data.list.cdr;
        } else if (is_vector(current)) {
            count += 1;
            current = current->data.vector.cdr;
        } else {
            break;
        }
    }
    return count;
}

static Value *builtin_first(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "first")) {
        return NULL;
    }
    Value *coll = ensure_collection(args->data.list.car, error, "first");
    if (!coll && !(error && *error)) {
        return NULL;
    }
    return coll_first(coll);
}

static Value *builtin_rest(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "rest")) {
        return NULL;
    }
    Value *coll = ensure_collection(args->data.list.car, error, "rest");
    if (!coll && !(error && *error)) {
        return NULL;
    }
    return coll_rest(coll);
}

static Value *builtin_cons(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 2, error, "cons")) {
        return NULL;
    }
    Value *value = args->data.list.car;
    Value *coll = ensure_collection(args->data.list.cdr->data.list.car, error, "cons");
    if (!coll && (error && *error)) {
        return NULL;
    }
    // cons always returns a list node; for vectors, convert to list first
    Value *tail = is_vector(coll) ? vector_to_list(coll) : coll;
    return cons(value, tail);
}

static Value *builtin_conj(Value *args, Env *env, char **error) {
    (void)env;
    if (is_nil(args)) {
        set_error(error, "conj expects a collection");
        return NULL;
    }
    Value *coll = ensure_collection(args->data.list.car, error, "conj");
    if (!coll && (error && *error)) {
        return NULL;
    }
    int coll_is_vector = is_vector(coll);
    Value *iter = args->data.list.cdr;
    if (coll_is_vector) {
        // For vectors: collect all existing elements plus new ones, build a new vector
        Value **items = NULL;
        size_t count = 0;
        size_t capacity = 0;
        // Collect existing vector elements
        Value *viter = coll;
        while (!is_nil(viter) && is_vector(viter)) {
            if (count == capacity) {
                capacity = capacity ? capacity * 2 : 4;
                items = checked_realloc(items, capacity * sizeof(Value *));
            }
            items[count++] = viter->data.vector.car;
            viter = viter->data.vector.cdr;
        }
        // Append new values
        while (!is_nil(iter)) {
            if (!is_list(iter)) {
                free(items);
                set_error(error, "Invalid conj arguments");
                return NULL;
            }
            if (count == capacity) {
                capacity = capacity ? capacity * 2 : 4;
                items = checked_realloc(items, capacity * sizeof(Value *));
            }
            items[count++] = iter->data.list.car;
            iter = iter->data.list.cdr;
        }
        Value *result = vector_from_array(items, count);
        free(items);
        return result;
    } else {
        // For lists: prepend each new value (Clojure semantics: each element prepended)
        Value *result = coll;
        while (!is_nil(iter)) {
            if (!is_list(iter)) {
                set_error(error, "Invalid conj arguments");
                return NULL;
            }
            result = cons(iter->data.list.car, result);
            iter = iter->data.list.cdr;
        }
        return result;
    }
}

static Value *builtin_count(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "count")) {
        return NULL;
    }
    Value *value = args->data.list.car;
    if (is_nil(value)) {
        return make_number(0);
    }
    if (value->type == TYPE_STRING) {
        return make_number((double)strlen(value->data.string));
    }
    Value *coll_value = ensure_collection(value, error, "count");
    if (!coll_value && !(error && *error)) {
        return NULL;
    }
    return make_number((double)coll_length(coll_value));
}

static Value *builtin_nth(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 2, error, "nth")) {
        return NULL;
    }
    Value *coll_val = args->data.list.car;
    Value *index_value = args->data.list.cdr->data.list.car;
    size_t index;
    if (!value_to_index(index_value, &index, error, "nth")) {
        return NULL;
    }
    // Support string character access
    if (coll_val && coll_val->type == TYPE_STRING) {
        const char *str = coll_val->data.string;
        size_t len = strlen(str);
        if (index >= len) {
            set_error(error, "nth: index %zu out of bounds (string length %zu)", index, len);
            return NULL;
        }
        char ch[2] = { str[index], '\0' };
        return make_string_owned(copy_text(ch));
    }
    Value *coll = ensure_collection(coll_val, error, "nth");
    if (!coll && (error && *error)) {
        return NULL;
    }
    Value *iter = coll;
    size_t pos = 0;
    while (!is_nil(iter)) {
        if (pos == index) {
            return coll_first(iter);
        }
        iter = coll_rest(iter);
        pos += 1;
    }
    set_error(error, "nth: index %zu out of bounds (collection size %zu)", index, pos);
    return NULL;
}

static Value *builtin_str(Value *args, Env *env, char **error) {
    (void)env;
    (void)error;
    StringBuilder sb;
    sb_init(&sb);
    Value *iter = args;
    while (!is_nil(iter)) {
        char *text = value_to_string(iter->data.list.car, 0);
        sb_append(&sb, text);
        free(text);
        iter = iter->data.list.cdr;
    }
    return make_string_owned(sb_take(&sb));
}

static Value *builtin_split(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 2, error, "split")) {
        return NULL;
    }
    Value *str_val = args->data.list.car;
    Value *delim_val = args->data.list.cdr->data.list.car;
    if (str_val->type != TYPE_STRING) {
        set_error(error, "split expects a string as first argument");
        return NULL;
    }
    if (delim_val->type != TYPE_STRING) {
        set_error(error, "split expects a string as delimiter");
        return NULL;
    }
    const char *str = str_val->data.string;
    const char *delim = delim_val->data.string;
    if (strlen(delim) != 1) {
        set_error(error, "split expects a single character delimiter");
        return NULL;
    }
    char delim_char = delim[0];
    Value *result = value_nil();
    const char *start = str;
    const char *end = str;
    while (*end != '\0') {
        if (*end == delim_char) {
            size_t len = end - start;
            char *substr = checked_malloc(len + 1);
            memcpy(substr, start, len);
            substr[len] = '\0';
            result = cons(make_string_owned(substr), result);
            start = end + 1;
        }
        end++;
    }
    size_t len = end - start;
    {
        char *substr = checked_malloc(len + 1);
        memcpy(substr, start, len);
        substr[len] = '\0';
        result = cons(make_string_owned(substr), result);
    }
    Value *reversed = value_nil();
    Value *iter = result;
    while (!is_nil(iter)) {
        reversed = cons(iter->data.list.car, reversed);
        iter = iter->data.list.cdr;
    }
    return reversed;
}

static Value *builtin_print(Value *args, Env *env, char **error) {
    (void)env;
    (void)error;
    Value *iter = args;
    int first = 1;
    while (!is_nil(iter)) {
        if (!first) {
            printf(" ");
        }
        char *text = value_to_string(iter->data.list.car, 0);
        printf("%s", text);
        free(text);
        iter = iter->data.list.cdr;
        first = 0;
    }
    fflush(stdout);
    return value_nil();
}

static Value *builtin_println(Value *args, Env *env, char **error) {
    Value *result = builtin_print(args, env, error);
    if (result || !(error && *error)) {
        printf("\n");
    }
    fflush(stdout);
    return result;
}

static Value *builtin_help(Value *args, Env *env, char **error) {
    (void)args;
    (void)env;
    (void)error;
    printf("%s", HELP_TEXT);
    return value_nil();
}

static Value *builtin_eval(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "eval")) {
        return NULL;
    }
    Value *expr_val = args->data.list.car;
    if (expr_val->type == TYPE_STRING) {
        const char *str = expr_val->data.string;
        size_t str_len = strlen(str);
        size_t total_consumed = 0;
        Value *last_result = value_nil();

        while (total_consumed < str_len) {
            Value *parsed = NULL;
            size_t consumed = 0;
            ParseStatus status = parse_expr(str + total_consumed, &consumed, &parsed, error);

            if (status == PARSE_ERROR) {
                if (!error || !*error) {
                    set_error(error, "eval: failed to parse string");
                }
                return NULL;
            }
            if (status == PARSE_END) {
                break;
            }
            if (status == PARSE_INCOMPLETE) {
                set_error(error, "eval: unexpected end of expression");
                return NULL;
            }
            if (!parsed) {
                break;
            }

            total_consumed += consumed;
            Value *result = eval(parsed, global_environment, error);
            if (!result || (error && *error)) {
                return NULL;
            }
            last_result = result;
        }
        return last_result;
    }
    return eval(expr_val, global_environment, error);
}

static Value *builtin_slurp(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "slurp")) {
        return NULL;
    }
    Value *filename_val = args->data.list.car;
    if (filename_val->type != TYPE_STRING) {
        set_error(error, "slurp expects a string filename");
        return NULL;
    }
    const char *filename = filename_val->data.string;
    FILE *file = fopen(filename, "r");
    if (!file) {
        set_error(error, "slurp: cannot open file '%s'", filename);
        return NULL;
    }
    StringBuilder sb;
    sb_init(&sb);
    char buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer) - 1, file)) > 0) {
        buffer[bytes_read] = '\0';
        sb_append(&sb, buffer);
    }
    if (ferror(file)) {
        fclose(file);
        char *partial = sb_take(&sb);
        free(partial);
        set_error(error, "slurp: I/O error reading '%s'", filename_val->data.string);
        return NULL;
    }
    fclose(file);
    return make_string_owned(sb_take(&sb));
}

static Value *builtin_spit(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 2, error, "spit")) {
        return NULL;
    }
    Value *filename_val = args->data.list.car;
    Value *content_val = args->data.list.cdr->data.list.car;
    if (filename_val->type != TYPE_STRING) {
        set_error(error, "spit expects a string filename");
        return NULL;
    }
    if (content_val->type != TYPE_STRING) {
        set_error(error, "spit expects string content");
        return NULL;
    }
    const char *filename = filename_val->data.string;
    const char *content = content_val->data.string;
    FILE *file = fopen(filename, "w");
    if (!file) {
        set_error(error, "spit: cannot open file '%s' for writing", filename);
        return NULL;
    }
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, file);
    fclose(file);
    if (written != len) {
        set_error(error, "spit: failed to write complete content to '%s'", filename);
        return NULL;
    }
    return value_nil();
}

static Value *builtin_load(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "load")) {
        return NULL;
    }
    Value *filename_val = args->data.list.car;
    if (filename_val->type != TYPE_STRING) {
        set_error(error, "load expects a string filename");
        return NULL;
    }

    // Call slurp to read the file
    Value *slurp_args = cons(filename_val, value_nil());
    Value *content = builtin_slurp(slurp_args, NULL, error);
    if (!content || (error && *error)) {
        return NULL;
    }

    // Call eval to evaluate the content
    Value *eval_args = cons(content, value_nil());
    return builtin_eval(eval_args, NULL, error);
}

static Value *builtin_sh(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "sh")) {
        return NULL;
    }

    Value *cmd_value = args->data.list.car;
    if (cmd_value->type != TYPE_STRING) {
        set_error(error, "sh expects a string argument");
        return NULL;
    }

    const char *cmd = cmd_value->data.string;

    // Execute command using popen
    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        set_error(error, "sh: failed to execute command");
        return NULL;
    }

    // Read output
    StringBuilder output;
    sb_init(&output);
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        sb_append(&output, buffer);
    }

    int status = pclose(pipe);

    if (status == -1) {
        char *output_str = sb_take(&output);
        free(output_str);
        set_error(error, "sh: failed to close command pipe");
        return NULL;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        char *output_str = sb_take(&output);
        free(output_str);
        set_error(error, "sh: command exited with status %d", WEXITSTATUS(status));
        return NULL;
    }

    if (WIFSIGNALED(status)) {
        char *output_str = sb_take(&output);
        free(output_str);
        set_error(error, "sh: command terminated by signal %d", WTERMSIG(status));
        return NULL;
    }

    char *result = sb_take(&output);
    Value *return_value = make_string_owned(result);
    return return_value;
}

static Value *builtin_run(Value *args, Env *env, char **error) {
    (void)env;
    if (is_nil(args)) {
        set_error(error, "run expects at least a program name");
        return NULL;
    }

    // Count arguments and build argv array
    size_t argc = 0;
    Value *iter = args;
    while (!is_nil(iter)) {
        if (!is_list(iter)) {
            set_error(error, "run: invalid argument list");
            return NULL;
        }
        Value *arg = iter->data.list.car;
        if (arg->type != TYPE_STRING && arg->type != TYPE_SYMBOL) {
            set_error(error, "run: arguments must be strings or symbols");
            return NULL;
        }
        argc++;
        iter = iter->data.list.cdr;
    }

    // Allocate argv array (NULL-terminated)
    char **argv = checked_malloc((argc + 1) * sizeof(char *));

    // Fill argv array
    iter = args;
    size_t i = 0;
    while (!is_nil(iter)) {
        Value *arg = iter->data.list.car;
        const char *arg_str = arg->data.string;
        argv[i] = copy_text(arg_str);
        i++;
        iter = iter->data.list.cdr;
    }
    argv[argc] = NULL;

    // Create pipe for reading child output
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        for (size_t j = 0; j < argc; j++) {
            free(argv[j]);
        }
        free(argv);
        set_error(error, "run: failed to create pipe");
        return NULL;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        for (size_t j = 0; j < argc; j++) {
            free(argv[j]);
        }
        free(argv);
        set_error(error, "run: failed to fork");
        return NULL;
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]); // Close read end
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
        dup2(pipefd[1], STDERR_FILENO); // Redirect stderr to pipe
        close(pipefd[1]);

        execvp(argv[0], argv);
        // If execvp returns, it failed
        fprintf(stderr, "run: failed to execute %s\n", argv[0]);
        _exit(127);
    }

    // Parent process
    close(pipefd[1]); // Close write end

    // Free argv in parent
    for (size_t j = 0; j < argc; j++) {
        free(argv[j]);
    }
    free(argv);

    // Read output from child
    StringBuilder output;
    sb_init(&output);
    char buffer[4096];
    ssize_t bytes_read;
    int read_error = 0;
    while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) != 0) {
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            read_error = 1;
            break;
        }
        buffer[bytes_read] = '\0';
        sb_append(&output, buffer);
    }
    close(pipefd[0]);
    if (read_error) {
        char *partial = sb_take(&output);
        free(partial);
        waitpid(pid, NULL, 0);
        set_error(error, "run: I/O error reading command output");
        return NULL;
    }

    // Wait for child to finish
    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        char *output_str = sb_take(&output);
        free(output_str);
        set_error(error, "run: command exited with status %d", WEXITSTATUS(status));
        return NULL;
    }

    if (WIFSIGNALED(status)) {
        char *output_str = sb_take(&output);
        free(output_str);
        set_error(error, "run: command terminated by signal %d", WTERMSIG(status));
        return NULL;
    }

    char *result = sb_take(&output);
    Value *return_value = make_string_owned(result);
    return return_value;
}

static Value *builtin_is_nil(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "nil?")) {
        return NULL;
    }
    return is_nil(args->data.list.car) ? value_true() : value_false();
}

static Value *builtin_is_number(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "number?")) {
        return NULL;
    }
    Value *v = args->data.list.car;
    return (v && v->type == TYPE_NUMBER) ? value_true() : value_false();
}

static Value *builtin_is_string(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "string?")) {
        return NULL;
    }
    Value *v = args->data.list.car;
    return (v && v->type == TYPE_STRING) ? value_true() : value_false();
}

static Value *builtin_is_bool(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "bool?")) {
        return NULL;
    }
    Value *v = args->data.list.car;
    return (v && v->type == TYPE_BOOL) ? value_true() : value_false();
}

static Value *builtin_is_symbol(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "symbol?")) {
        return NULL;
    }
    Value *v = args->data.list.car;
    return (v && v->type == TYPE_SYMBOL) ? value_true() : value_false();
}

static Value *builtin_is_list(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "list?")) {
        return NULL;
    }
    Value *v = args->data.list.car;
    return (is_nil(v) || is_list(v)) ? value_true() : value_false();
}

static Value *builtin_is_vector(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "vector?")) {
        return NULL;
    }
    Value *v = args->data.list.car;
    return is_vector(v) ? value_true() : value_false();
}

static Value *builtin_is_fn(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "fn?")) {
        return NULL;
    }
    Value *v = args->data.list.car;
    return (v && (v->type == TYPE_FUNCTION || v->type == TYPE_NATIVE_FUNCTION))
           ? value_true() : value_false();
}

static Value *builtin_not(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "not")) {
        return NULL;
    }
    return is_truthy(args->data.list.car) ? value_false() : value_true();
}

/* hash-map: (hash-map k1 v1 k2 v2 ...) */
static Value *builtin_hash_map(Value *args, Env *env, char **error) {
    (void)env;
    Value *m = make_empty_map();
    Value *iter = args;
    while (!is_nil(iter)) {
        Value *k = iter->data.list.car;
        iter = iter->data.list.cdr;
        if (is_nil(iter)) {
            set_error(error, "hash-map: odd number of arguments");
            return NULL;
        }
        Value *v = iter->data.list.car;
        iter = iter->data.list.cdr;
        m = map_assoc(m, k, v);
    }
    return m;
}

static Value *builtin_get(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_arg_range(args, 2, 3, error, "get")) return NULL;
    Value *coll = args->data.list.car;
    Value *key = args->data.list.cdr->data.list.car;
    Value *not_found = is_nil(args->data.list.cdr->data.list.cdr)
                       ? value_nil()
                       : args->data.list.cdr->data.list.cdr->data.list.car;
    if (is_map(coll)) {
        Value *found = map_get(coll, key);
        return found ? found : not_found;
    }
    /* get on list/vector by index */
    if (is_list(coll) || is_vector(coll)) {
        size_t idx;
        if (!value_to_index(key, &idx, error, "get")) return NULL;
        Value *it = coll;
        for (size_t i = 0; i < idx; i++) {
            if (is_nil(it)) return not_found;
            it = is_list(it) ? it->data.list.cdr : it->data.vector.cdr;
        }
        if (is_nil(it)) return not_found;
        return is_list(it) ? it->data.list.car : it->data.vector.car;
    }
    set_error(error, "get: expected map, list, or vector");
    return NULL;
}

static Value *builtin_assoc(Value *args, Env *env, char **error) {
    (void)env;
    if (is_nil(args) || is_nil(args->data.list.cdr)) {
        set_error(error, "assoc: requires map and key-value pairs");
        return NULL;
    }
    Value *m = args->data.list.car;
    if (!is_map(m)) { set_error(error, "assoc: first argument must be a map"); return NULL; }
    Value *iter = args->data.list.cdr;
    while (!is_nil(iter)) {
        Value *k = iter->data.list.car;
        iter = iter->data.list.cdr;
        if (is_nil(iter)) { set_error(error, "assoc: odd number of key-value pairs"); return NULL; }
        Value *v = iter->data.list.car;
        iter = iter->data.list.cdr;
        m = map_assoc(m, k, v);
    }
    return m;
}

static Value *builtin_dissoc(Value *args, Env *env, char **error) {
    (void)env;
    if (is_nil(args)) { set_error(error, "dissoc: requires a map"); return NULL; }
    Value *m = args->data.list.car;
    if (!is_map(m)) { set_error(error, "dissoc: first argument must be a map"); return NULL; }
    Value *iter = args->data.list.cdr;
    while (!is_nil(iter)) {
        m = map_dissoc(m, iter->data.list.car);
        iter = iter->data.list.cdr;
    }
    return m;
}

static Value *builtin_keys(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "keys")) return NULL;
    Value *m = args->data.list.car;
    if (!is_map(m)) { set_error(error, "keys: expected a map"); return NULL; }
    Value *head = value_nil(), *tail = NULL;
    Value *pairs = m->data.list.car;
    while (!is_nil(pairs)) {
        Value *node = cons(pairs->data.list.car->data.list.car, value_nil());
        if (!tail) head = node; else tail->data.list.cdr = node;
        tail = node;
        pairs = pairs->data.list.cdr;
    }
    return head;
}

static Value *builtin_vals(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "vals")) return NULL;
    Value *m = args->data.list.car;
    if (!is_map(m)) { set_error(error, "vals: expected a map"); return NULL; }
    Value *head = value_nil(), *tail = NULL;
    Value *pairs = m->data.list.car;
    while (!is_nil(pairs)) {
        Value *node = cons(pairs->data.list.car->data.list.cdr, value_nil());
        if (!tail) head = node; else tail->data.list.cdr = node;
        tail = node;
        pairs = pairs->data.list.cdr;
    }
    return head;
}

static Value *builtin_is_map(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 1, error, "map?")) return NULL;
    return is_map(args->data.list.car) ? value_true() : value_false();
}

/* contains?: (contains? map key) */
static Value *builtin_contains(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 2, error, "contains?")) return NULL;
    Value *m = args->data.list.car;
    Value *key = args->data.list.cdr->data.list.car;
    if (!is_map(m)) { set_error(error, "contains?: expected a map"); return NULL; }
    return map_get(m, key) ? value_true() : value_false();
}

/* reduce: (reduce f init coll) */
static Value *builtin_reduce(Value *args, Env *env, char **error) {
    (void)env;
    if (!require_exact_args(args, 3, error, "reduce")) return NULL;
    Value *callable = args->data.list.car;
    if (callable->type != TYPE_FUNCTION && callable->type != TYPE_NATIVE_FUNCTION) {
        set_error(error, "reduce: first argument must be a function");
        return NULL;
    }
    Value *acc = args->data.list.cdr->data.list.car;
    Value *coll = args->data.list.cdr->data.list.cdr->data.list.car;
    Value *iter = coll;
    while (!is_nil(iter)) {
        if (!is_list(iter)) { set_error(error, "reduce: collection must be a list"); return NULL; }
        Value *item = iter->data.list.car;
        Value *call_args = cons(acc, cons(item, value_nil()));
        if (callable->type == TYPE_NATIVE_FUNCTION) {
            acc = callable->data.native(call_args, NULL, error);
        } else {
            Env *frame = env_create(callable->data.function.env);
            if (!bind_params(callable->data.function.params, call_args, frame, error)) return NULL;
            Value *body = callable->data.function.body;
            Value *bit = body;
            while (!is_nil(bit->data.list.cdr)) {
                Value *v = eval(bit->data.list.car, frame, error);
                if (!v || (error && *error)) return NULL;
                bit = bit->data.list.cdr;
            }
            acc = eval(bit->data.list.car, frame, error);
        }
        if (!acc || (error && *error)) return NULL;
        iter = iter->data.list.cdr;
    }
    return acc;
}

/* apply: (apply f arg1 ... arglist)
   All args before the last are prepended to the final list arg. */
static Value *builtin_apply(Value *args, Env *env, char **error) {
    (void)env;
    if (is_nil(args) || is_nil(args->data.list.cdr)) {
        set_error(error, "apply: requires at least 2 arguments");
        return NULL;
    }
    Value *callable = args->data.list.car;
    if (callable->type != TYPE_FUNCTION && callable->type != TYPE_NATIVE_FUNCTION) {
        set_error(error, "apply: first argument must be a function");
        return NULL;
    }
    /* Collect leading args, then splice in the final list */
    Value *rest = args->data.list.cdr;
    /* Build arg list: all but last are individual values, last must be a list/nil */
    Value *head = value_nil();
    Value *tail = NULL;
    while (!is_nil(rest->data.list.cdr)) {
        Value *node = cons(rest->data.list.car, value_nil());
        if (!tail) head = node; else tail->data.list.cdr = node;
        tail = node;
        rest = rest->data.list.cdr;
    }
    /* rest->car is the final list; splice it on */
    Value *final_list = rest->data.list.car;
    if (!is_nil(final_list) && !is_list(final_list)) {
        set_error(error, "apply: last argument must be a list");
        return NULL;
    }
    if (tail) tail->data.list.cdr = final_list;
    else head = final_list;

    if (callable->type == TYPE_NATIVE_FUNCTION) {
        return callable->data.native(head, NULL, error);
    }
    /* User function: bind params and eval body */
    Env *frame = env_create(callable->data.function.env);
    if (!bind_params(callable->data.function.params, head, frame, error)) return NULL;
    Value *body = callable->data.function.body;
    if (is_nil(body)) return value_nil();
    Value *iter = body;
    while (!is_nil(iter->data.list.cdr)) {
        Value *v = eval(iter->data.list.car, frame, error);
        if (!v || (error && *error)) return NULL;
        iter = iter->data.list.cdr;
    }
    return eval(iter->data.list.car, frame, error);
}

static void register_builtin(Env *env, const char *name, NativeFn fn, const char *doc) {
    Value *value = make_native(fn, doc);
    env_define(env, name, value);
}

static void install_builtins(Env *env) {
    register_builtin(env, "+", builtin_add, "+");
    register_builtin(env, "-", builtin_sub, "-");
    register_builtin(env, "*", builtin_mul, "*");
    register_builtin(env, "/", builtin_div, "/");
    register_builtin(env, "mod", builtin_mod, "mod");
    register_builtin(env, "inc", builtin_inc, "inc");
    register_builtin(env, "dec", builtin_dec, "dec");
    register_builtin(env, "=", builtin_equal, "=");
    register_builtin(env, "<", builtin_lt, "<");
    register_builtin(env, "<=", builtin_lte, "<=");
    register_builtin(env, ">", builtin_gt, ">");
    register_builtin(env, ">=", builtin_gte, ">=");
    register_builtin(env, "list", builtin_list, "list");
    register_builtin(env, "first", builtin_first, "first");
    register_builtin(env, "rest", builtin_rest, "rest");
    register_builtin(env, "cons", builtin_cons, "cons");
    register_builtin(env, "conj", builtin_conj, "conj");
    register_builtin(env, "count", builtin_count, "count");
    register_builtin(env, "nth", builtin_nth, "nth");
    register_builtin(env, "str", builtin_str, "str");
    register_builtin(env, "split", builtin_split, "split");
    register_builtin(env, "print", builtin_print, "print");
    register_builtin(env, "println", builtin_println, "println");
    register_builtin(env, "eval", builtin_eval, "eval");
    register_builtin(env, "slurp", builtin_slurp, "slurp");
    register_builtin(env, "spit", builtin_spit, "spit");
    register_builtin(env, "load", builtin_load, "load");
    register_builtin(env, "sh", builtin_sh, "sh");
    register_builtin(env, "run", builtin_run, "run");
    register_builtin(env, "help", builtin_help, "help");
    register_builtin(env, "nil?", builtin_is_nil, "nil?");
    register_builtin(env, "number?", builtin_is_number, "number?");
    register_builtin(env, "string?", builtin_is_string, "string?");
    register_builtin(env, "bool?", builtin_is_bool, "bool?");
    register_builtin(env, "symbol?", builtin_is_symbol, "symbol?");
    register_builtin(env, "list?", builtin_is_list, "list?");
    register_builtin(env, "vector?", builtin_is_vector, "vector?");
    register_builtin(env, "fn?", builtin_is_fn, "fn?");
    register_builtin(env, "not", builtin_not, "not");
    register_builtin(env, "apply", builtin_apply, "apply");
    register_builtin(env, "reduce", builtin_reduce, "reduce");
    register_builtin(env, "hash-map", builtin_hash_map, "hash-map");
    register_builtin(env, "get", builtin_get, "get");
    register_builtin(env, "assoc", builtin_assoc, "assoc");
    register_builtin(env, "dissoc", builtin_dissoc, "dissoc");
    register_builtin(env, "keys", builtin_keys, "keys");
    register_builtin(env, "vals", builtin_vals, "vals");
    register_builtin(env, "map?", builtin_is_map, "map?");
    register_builtin(env, "contains?", builtin_contains, "contains?");
}

static void append_input(char **buffer, size_t *length, size_t *capacity, const char *line) {
    size_t len = line ? strlen(line) : 0;
    size_t needed = *length + len + 1;
    if (needed > *capacity) {
        size_t new_capacity = *capacity ? *capacity : 128;
        while (new_capacity < needed) {
            new_capacity *= 2;
        }
        *buffer = checked_realloc(*buffer, new_capacity);
        *capacity = new_capacity;
    }
    if (len > 0) {
        memcpy(*buffer + *length, line, len);
    }
    *length += len;
    (*buffer)[*length] = '\0';
}

static void consume_buffer(char **buffer, size_t *length, size_t consumed) {
    if (!*buffer || consumed == 0) {
        return;
    }
    if (consumed >= *length) {
        *length = 0;
        (*buffer)[0] = '\0';
        return;
    }
    size_t remaining = *length - consumed;
    memmove(*buffer, *buffer + consumed, remaining);
    *length = remaining;
    (*buffer)[*length] = '\0';
}

static void repl(Env *env, int silent) {
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int waiting = 0;
    int interactive = isatty(STDIN_FILENO);
    if (interactive) {
        printf("CrushLisp ready. Type (help) for a list of forms.\n");
    }
    while (1) {
        if (interactive) {
            fputs(waiting ? "... " : "CrushLisp> ", stdout);
            fflush(stdout);
        }
        char *line = NULL;
        size_t line_capacity = 0;
        ssize_t read = getline(&line, &line_capacity, stdin);
        if (read == -1) {
            if (interactive) {
                printf("\n");
            }
            free(line);
            break;
        }
        append_input(&buffer, &length, &capacity, line);
        free(line);
        waiting = 0;
        size_t consumed_total = 0;
        while (1) {
            if (consumed_total >= length) {
                break;
            }
            Value *expr = NULL;
            char *error = NULL;
            size_t consumed = 0;
            ParseStatus status = parse_expr(buffer + consumed_total, &consumed, &expr, &error);
            if (status == PARSE_OK) {
                consumed_total += consumed;
                current_eval_depth = 0;
                Value *result = eval(expr, env, &error);
                if (error) {
                    fprintf(stderr, "Error: %s\n", error);
                    free(error);
                    consumed_total = length;
                    break;
                }
                if (!silent) {
                    char *text = value_to_string(result, 1);
                    printf("%s\n", text);
                    free(text);
                }
            } else if (status == PARSE_END) {
                consumed_total += consumed;
                break;
            } else if (status == PARSE_INCOMPLETE) {
                waiting = 1;
                break;
            } else if (status == PARSE_ERROR) {
                fprintf(stderr, "Parse error: %s\n", error ? error : "unknown");
                free(error);
                consumed_total = length;
                break;
            }
        }
        if (consumed_total > 0) {
            consume_buffer(&buffer, &length, consumed_total);
        }
    }
    free(buffer);
}

int main(int argc, char **argv) {
    int silent = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            silent = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  -s        Suppress output (except explicit printing)\n");
            printf("  -h, --help Show this help message\n");
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Use -h or --help for usage information\n");
            return 1;
        }
    }

    Env *global = env_create(NULL);
    global_environment = global;
    install_builtins(global);

    /* Load embedded stdlib */
    {
        const char *src = STDLIB_SOURCE;
        size_t src_len = strlen(src);
        size_t consumed = 0;
        while (consumed < src_len) {
            Value *expr = NULL;
            char *error = NULL;
            size_t n = 0;
            ParseStatus status = parse_expr(src + consumed, &n, &expr, &error);
            if (status == PARSE_ERROR || status == PARSE_INCOMPLETE) {
                fprintf(stderr, "stdlib error: %s\n", error ? error : "parse failed");
                free(error);
                return 1;
            }
            if (status == PARSE_END) break;
            consumed += n;
            current_eval_depth = 0;
            Value *result = eval(expr, global, &error);
            if (!result || error) {
                fprintf(stderr, "stdlib error: %s\n", error ? error : "eval failed");
                free(error);
                return 1;
            }
        }
    }

    repl(global, silent);
    return 0;
}
