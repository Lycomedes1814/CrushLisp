#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>

typedef enum {
    TYPE_NIL,
    TYPE_NUMBER,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_SYMBOL,
    TYPE_LIST,
    TYPE_FUNCTION,
    TYPE_NATIVE_FUNCTION
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
static const char *HELP_TEXT =
"CrushLisp special forms:\n"
"  (quote x)          return x without evaluation\n"
"  (if test then else) conditional branching\n"
"  (def name value)   bind a global name\n"
"  (let (name value ...) body...) scoped locals\n"
"  (fn (params...) body...) anonymous function\n"
"  (do expr...)       evaluate expressions sequentially\n\n"
"Functions:\n"
"  (+ - * / mod inc dec) arithmetic\n"
"  (= < <= > >=)         comparisons\n"
"  (list values...)      list literal\n"
"  (first coll)          first element\n"
"  (rest coll)           remaining elements\n"
"  (cons x coll)         prepend value\n"
"  (conj coll values...) prepend values\n"
"  (count coll)          collection size\n"
"  (nth coll index)      element at index\n"
"  (str values...)       concatenate\n"
"  (print values...)     write without newline\n"
"  (println values...)   write with newline\n"
"  (help)                show this message\n";

static Value VALUE_NIL = { TYPE_NIL, {0}, NULL };
static Value VALUE_TRUE = { TYPE_BOOL, { .boolean = 1 }, NULL };
static Value VALUE_FALSE = { TYPE_BOOL, { .boolean = 0 }, NULL };

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
    if (sb->data) {
        sb->data[sb->length] = '\0';
    }
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

static int is_nil(Value *value) {
    return value == NULL || value->type == TYPE_NIL;
}

static int is_list(Value *value) {
    return value && value->type == TYPE_LIST;
}

static Value *cons(Value *car, Value *cdr) {
    Value *value = allocate_value(TYPE_LIST);
    value->data.list.car = car;
    value->data.list.cdr = cdr ? cdr : value_nil();
    return value;
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

static Value *list_from_array(Value **items, size_t count) {
    Value *result = value_nil();
    for (size_t i = 0; i < count; ++i) {
        size_t idx = count - 1 - i;
        result = cons(items[idx], result);
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
            return fabs(a->data.number - b->data.number) < EPSILON;
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
        case TYPE_FUNCTION:
        case TYPE_NATIVE_FUNCTION:
            return a == b;
    }
    return 0;
}

static void append_number(StringBuilder *sb, double number) {
    long long integral = (long long)llround(number);
    if (fabs(number - (double)integral) < EPSILON) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%lld", integral);
        sb_append(sb, buffer);
    } else {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.10g", number);
        sb_append(sb, buffer);
    }
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
    double rounded = floor(number + 0.5);
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

static ParseStatus parse_list_data(const char *input, size_t *index, char closing, Value **out, char **error) {
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
    Value *list = list_from_array(items, count);
    free(items);
    *out = list;
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
        return parse_list_data(input, index, ')', out, error);
    }
    if (c == '[') {
        return parse_list_data(input, index, ']', out, error);
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
static Value *eval_block(Value *forms, Env *env, char **error);
static Value *eval_args(Value *list, Env *env, char **error);
static Value *apply_function_value(Value *callable, Value *args, char **error);

static Value *eval_quote(Value *args, char **error) {
    (void)error;
    if (is_nil(args) || !is_list(args) || (!is_nil(args->data.list.cdr) && !is_list(args->data.list.cdr))) {
        return args->data.list.car;
    }
    if (is_nil(args)) {
        return value_nil();
    }
    Value *value = args->data.list.car;
    return value;
}

static Value *eval_if(Value *args, Env *env, char **error) {
    if (is_nil(args) || !is_list(args)) {
        set_error(error, "if expects test form");
        return NULL;
    }
    Value *test_expr = args->data.list.car;
    Value *rest = args->data.list.cdr;
    if (is_nil(rest) || !is_list(rest)) {
        set_error(error, "if expects then form");
        return NULL;
    }
    Value *then_expr = rest->data.list.car;
    Value *else_expr = rest->data.list.cdr;
    Value *cond = eval(test_expr, env, error);
    if (!cond || (error && *error)) {
        return NULL;
    }
    if (is_truthy(cond)) {
        return eval(then_expr, env, error);
    }
    if (is_nil(else_expr)) {
        return value_nil();
    }
    Value *else_form = else_expr->data.list.car;
    return eval(else_form, env, error);
}

static Value *eval_do(Value *args, Env *env, char **error) {
    return eval_block(args, env, error);
}

static Value *eval_def(Value *args, Env *env, char **error) {
    if (is_nil(args) || !is_list(args)) {
        set_error(error, "def expects symbol and value");
        return NULL;
    }
    Value *name_form = args->data.list.car;
    Value *rest = args->data.list.cdr;
    if (!name_form || name_form->type != TYPE_SYMBOL) {
        set_error(error, "def name must be a symbol");
        return NULL;
    }
    if (is_nil(rest)) {
        set_error(error, "def expects a value");
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

static Value *eval_let(Value *args, Env *env, char **error) {
    if (is_nil(args) || !is_list(args)) {
        set_error(error, "let expects binding list and body");
        return NULL;
    }
    Value *binding_form = args->data.list.car;
    if (!(is_nil(binding_form) || is_list(binding_form))) {
        set_error(error, "let bindings must be a list");
        return NULL;
    }
    Env *child = env_create(env);
    Value *iter = binding_form;
    while (!is_nil(iter)) {
        if (!is_list(iter)) {
            set_error(error, "let bindings must be pairs");
            return NULL;
        }
        Value *name_value = iter->data.list.car;
        iter = iter->data.list.cdr;
        if (is_nil(iter)) {
            set_error(error, "let binding missing value");
            return NULL;
        }
        Value *expr_value = iter->data.list.car;
        iter = iter->data.list.cdr;
        if (!name_value || name_value->type != TYPE_SYMBOL) {
            set_error(error, "let binding name must be symbol");
            return NULL;
        }
        Value *evaluated = eval(expr_value, child, error);
        if (!evaluated || (error && *error)) {
            return NULL;
        }
        env_define(child, name_value->data.string, evaluated);
    }
    Value *body = args->data.list.cdr;
    return eval_block(body, child, error);
}

static int params_are_symbols(Value *params) {
    Value *iter = params;
    while (!is_nil(iter)) {
        if (!is_list(iter)) {
            return 0;
        }
        Value *item = iter->data.list.car;
        if (!item || item->type != TYPE_SYMBOL) {
            return 0;
        }
        iter = iter->data.list.cdr;
    }
    return 1;
}

static Value *eval_fn(Value *args, Env *env, char **error) {
    if (is_nil(args) || !is_list(args)) {
        set_error(error, "fn expects parameters and body");
        return NULL;
    }
    Value *params = args->data.list.car;
    if (!(is_nil(params) || is_list(params))) {
        set_error(error, "fn parameters must be a list");
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
    return make_function(params, body, env);
}

static Value *eval(Value *expr, Env *env, char **error) {
    if (!expr) {
        return value_nil();
    }
    switch (expr->type) {
        case TYPE_NUMBER:
        case TYPE_BOOL:
        case TYPE_STRING:
        case TYPE_FUNCTION:
        case TYPE_NATIVE_FUNCTION:
        case TYPE_NIL:
            return expr;
        case TYPE_SYMBOL: {
            Value *value = env_get(env, expr->data.string);
            if (!value) {
                set_error(error, "Undefined symbol %s", expr->data.string);
                return NULL;
            }
            return value;
        }
        case TYPE_LIST: {
            if (is_nil(expr)) {
                return expr;
            }
            Value *op = expr->data.list.car;
            Value *args = expr->data.list.cdr;
            if (op && op->type == TYPE_SYMBOL) {
                const char *name = op->data.string;
                if (strcmp(name, "quote") == 0) {
                    return eval_quote(args, error);
                }
                if (strcmp(name, "if") == 0) {
                    return eval_if(args, env, error);
                }
                if (strcmp(name, "def") == 0) {
                    return eval_def(args, env, error);
                }
                if (strcmp(name, "let") == 0) {
                    return eval_let(args, env, error);
                }
                if (strcmp(name, "fn") == 0) {
                    return eval_fn(args, env, error);
                }
                if (strcmp(name, "do") == 0) {
                    return eval_do(args, env, error);
                }
            }
            Value *callable = eval(op, env, error);
            if (!callable || (error && *error)) {
                return NULL;
            }
            Value *evaluated_args = eval_args(args, env, error);
            if ((error && *error)) {
                return NULL;
            }
            return apply_function_value(callable, evaluated_args, error);
        }
    }
    set_error(error, "Unsupported expression");
    return NULL;
}

static Value *eval_block(Value *forms, Env *env, char **error) {
    Value *result = value_nil();
    Value *iter = forms;
    while (!is_nil(iter)) {
        if (!is_list(iter)) {
            set_error(error, "Malformed body");
            return NULL;
        }
        Value *form = iter->data.list.car;
        result = eval(form, env, error);
        if (!result || (error && *error)) {
            return NULL;
        }
        iter = iter->data.list.cdr;
    }
    return result;
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

static Value *apply_function_value(Value *callable, Value *args, char **error) {
    if (!callable) {
        set_error(error, "Cannot call nil");
        return NULL;
    }
    if (callable->type == TYPE_NATIVE_FUNCTION) {
        return callable->data.native(args, NULL, error);
    }
    if (callable->type == TYPE_FUNCTION) {
        Value *params = callable->data.function.params;
        Env *closure = callable->data.function.env;
        Env *frame = env_create(closure);
        Value *param_iter = params;
        Value *arg_iter = args;
        size_t expected = list_length(params);
        while (!is_nil(param_iter)) {
            if (is_nil(arg_iter)) {
                set_error(error, "Expected %zu arguments", expected);
                return NULL;
            }
            Value *param_name = param_iter->data.list.car;
            if (!param_name || param_name->type != TYPE_SYMBOL) {
                set_error(error, "Invalid parameter");
                return NULL;
            }
            Value *arg_value = arg_iter->data.list.car;
            env_define(frame, param_name->data.string, arg_value);
            param_iter = param_iter->data.list.cdr;
            arg_iter = arg_iter->data.list.cdr;
        }
        if (!is_nil(arg_iter)) {
            set_error(error, "Too many arguments");
            return NULL;
        }
        return eval_block(callable->data.function.body, frame, error);
    }
    set_error(error, "Value is not callable");
    return NULL;
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
    if (is_nil(args) || is_nil(args->data.list.cdr)) {
        set_error(error, "mod expects two arguments");
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
    if (is_nil(args)) {
        set_error(error, "inc expects one argument");
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
    if (is_nil(args)) {
        set_error(error, "dec expects one argument");
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

static Value *ensure_list(Value *value, char **error, const char *name) {
    if (is_nil(value) || is_list(value)) {
        return value;
    }
    set_error(error, "%s expects a list", name);
    return NULL;
}

static Value *builtin_first(Value *args, Env *env, char **error) {
    (void)env;
    if (is_nil(args)) {
        set_error(error, "first expects a collection");
        return NULL;
    }
    Value *coll = ensure_list(args->data.list.car, error, "first");
    if (!coll && !(error && *error)) {
        return NULL;
    }
    if (is_nil(coll)) {
        return value_nil();
    }
    return coll->data.list.car;
}

static Value *builtin_rest(Value *args, Env *env, char **error) {
    (void)env;
    if (is_nil(args)) {
        set_error(error, "rest expects a collection");
        return NULL;
    }
    Value *coll = ensure_list(args->data.list.car, error, "rest");
    if (!coll && !(error && *error)) {
        return NULL;
    }
    if (is_nil(coll)) {
        return value_nil();
    }
    return coll->data.list.cdr;
}

static Value *builtin_cons(Value *args, Env *env, char **error) {
    (void)env;
    if (is_nil(args) || is_nil(args->data.list.cdr)) {
        set_error(error, "cons expects value and list");
        return NULL;
    }
    Value *value = args->data.list.car;
    Value *list_value = ensure_list(args->data.list.cdr->data.list.car, error, "cons");
    if (!list_value && !(error && *error)) {
        return NULL;
    }
    return cons(value, list_value);
}

static Value *builtin_conj(Value *args, Env *env, char **error) {
    (void)env;
    if (is_nil(args)) {
        set_error(error, "conj expects a collection");
        return NULL;
    }
    Value *coll = ensure_list(args->data.list.car, error, "conj");
    if (!coll && !(error && *error)) {
        return NULL;
    }
    Value *result = coll;
    Value *iter = args->data.list.cdr;
    while (!is_nil(iter)) {
        if (!is_list(iter)) {
            set_error(error, "Invalid conj arguments");
            return NULL;
        }
        Value *value = iter->data.list.car;
        result = cons(value, result);
        iter = iter->data.list.cdr;
    }
    return result;
}

static Value *builtin_count(Value *args, Env *env, char **error) {
    (void)env;
    if (is_nil(args)) {
        set_error(error, "count expects a collection");
        return NULL;
    }
    Value *value = args->data.list.car;
    if (is_nil(value)) {
        return make_number(0);
    }
    if (value->type == TYPE_STRING) {
        return make_number((double)strlen(value->data.string));
    }
    Value *list_value = ensure_list(value, error, "count");
    if (!list_value && !(error && *error)) {
        return NULL;
    }
    return make_number((double)list_length(list_value));
}

static Value *builtin_nth(Value *args, Env *env, char **error) {
    (void)env;
    if (is_nil(args) || is_nil(args->data.list.cdr)) {
        set_error(error, "nth expects collection and index");
        return NULL;
    }
    Value *coll = ensure_list(args->data.list.car, error, "nth");
    if (!coll && !(error && *error)) {
        return NULL;
    }
    Value *index_value = args->data.list.cdr->data.list.car;
    size_t index;
    if (!value_to_index(index_value, &index, error, "nth")) {
        return NULL;
    }
    Value *iter = coll;
    size_t pos = 0;
    while (!is_nil(iter)) {
        if (pos == index) {
            return iter->data.list.car;
        }
        iter = iter->data.list.cdr;
        pos += 1;
    }
    set_error(error, "nth index out of bounds");
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
    register_builtin(env, "print", builtin_print, "print");
    register_builtin(env, "println", builtin_println, "println");
    register_builtin(env, "help", builtin_help, "help");
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
                Value *result = eval(expr, env, &error);
                if (error) {
                    fprintf(stderr, "Error: %s\n", error);
                    free(error);
                    consumed_total = length;
                    break;
                }
                if (!silent) {
                    char *text = value_to_string(result, 0);
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
    install_builtins(global);
    repl(global, silent);
    return 0;
}
