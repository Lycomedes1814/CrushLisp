CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L
DEBUG_FLAGS = -g -O0 -DDEBUG
SRC_DIR = src
BUILD_DIR = build
TARGET = crushlisp

OBJECTS = $(BUILD_DIR)/crushlisp.o

.PHONY: all clean debug test run install

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

debug: CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L $(DEBUG_FLAGS)
debug: clean all

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	@echo "Running CrushLisp tests..."
	@echo "(+ 1 2 3)" | ./$(TARGET) | grep -qx "6" && echo "✓ Arithmetic test passed"
	@echo "(def x 10) (* x x)" | ./$(TARGET) | tail -1 | grep -qx "100" && echo "✓ Variable binding test passed"
	@echo "(if (< 5 10) \"yes\" \"no\")" | ./$(TARGET) | grep -qx '"yes"' && echo "✓ Conditional test passed"
	@echo "(list 1 2 3)" | ./$(TARGET) | grep -qx "(1 2 3)" && echo "✓ List test passed"
	@echo "[3 4 5]" | ./$(TARGET) | grep -qx "\[3 4 5\]" && echo "✓ Vector literal test passed"
	@echo "[(+ 1 2) (* 3 4)]" | ./$(TARGET) | grep -qx "\[3 12\]" && echo "✓ Vector evaluation test passed"
	@echo "(def v [1 2 3]) (first v)" | ./$(TARGET) | tail -1 | grep -qx "1" && echo "✓ Vector collection ops test passed"
	@echo "((fn [x y] (+ x y)) 3 5)" | ./$(TARGET) | grep -qx "8" && echo "✓ Function with vector params test passed"
	@test "$$(echo '(+ 2 2)' | ./$(TARGET) -s | wc -l)" -eq 0 && echo "✓ Silent mode test passed"
	@echo "(println \"test\")" | ./$(TARGET) -s | grep -qx "test" && echo "✓ Silent mode println test passed"
	@echo "(def recurse-inf (fn () (recurse-inf))) (recurse-inf)" | ./$(TARGET) 2>&1 | grep -q "Stack overflow" && echo "✓ Stack overflow protection test passed"
	@echo '(eval "(+ 1 2 3)")' | ./$(TARGET) | grep -qx "6" && echo "✓ Eval test passed"
	@echo '(quote)' | ./$(TARGET) 2>&1 | grep -q "quote expects exactly 1 argument" && echo "✓ Quote arity validation test passed"
	@echo '(inc 1 2)' | ./$(TARGET) 2>&1 | grep -q "inc expects exactly 1 argument" && echo "✓ Builtin arity validation test passed"
	@echo '(sh "false")' | ./$(TARGET) 2>&1 | grep -q "status 1" && echo "✓ Shell exit status test passed"
	@echo '(spit "test_file.txt" "Hello CrushLisp") (slurp "test_file.txt")' | ./$(TARGET) | tail -1 | grep -q "Hello CrushLisp"; result=$$?; rm -f test_file.txt; [ $$result -eq 0 ] && echo "✓ File I/O (slurp/spit) test passed"
	@echo '(def x 10) x' > test_load.lisp; echo '(load "test_load.lisp") (+ x 5)' | ./$(TARGET) | tail -1 | grep -qx "15"; result=$$?; rm -f test_load.lisp; [ $$result -eq 0 ] && echo "✓ File loading (load) test passed"
	@echo "(let [x 1 y 2] (+ x y))" | ./$(TARGET) | grep -qx "3" && echo "✓ let with vector bindings test passed"
	@echo "(conj [1 2] 3)" | ./$(TARGET) | grep -qx "\[1 2 3\]" && echo "✓ conj on vector test passed"
	@echo "(cons 0 [1 2 3])" | ./$(TARGET) | grep -qx "(0 1 2 3)" && echo "✓ cons on vector test passed"
	@echo "(nil? nil)" | ./$(TARGET) | grep -qx "true" && echo "✓ nil? predicate test passed"
	@echo "(number? 42)" | ./$(TARGET) | grep -qx "true" && echo "✓ number? predicate test passed"
	@echo "(string? \"hi\")" | ./$(TARGET) | grep -qx "true" && echo "✓ string? predicate test passed"
	@echo "(list? (list 1 2))" | ./$(TARGET) | grep -qx "true" && echo "✓ list? predicate test passed"
	@echo "(vector? [1 2])" | ./$(TARGET) | grep -qx "true" && echo "✓ vector? predicate test passed"
	@echo "All tests passed!"

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

.PHONY: help
help:
	@echo "CrushLisp Makefile targets:"
	@echo "  all     - Build the project (default)"
	@echo "  debug   - Build with debug symbols"
	@echo "  clean   - Remove build artifacts"
	@echo "  run     - Build and run the REPL"
	@echo "  test    - Build and run tests"
	@echo "  install - Install to /usr/local/bin"
	@echo "  help    - Show this help message"
