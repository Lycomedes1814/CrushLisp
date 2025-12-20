CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L
DEBUG_FLAGS = -g -O0 -DDEBUG
SRC_DIR = src
BUILD_DIR = build
TARGET = crushlisp

SOURCES = $(SRC_DIR)/crushlisp.c
OBJECTS = $(BUILD_DIR)/crushlisp.o

.PHONY: all clean debug test run install

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

debug: CFLAGS = -std=c11 -Wall -Wextra -Wpedantic $(DEBUG_FLAGS)
debug: clean all

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	@echo "Running CrushLisp tests..."
	@echo "(+ 1 2 3)" | ./$(TARGET) | grep -q "6" && echo "✓ Arithmetic test passed"
	@echo "(def x 10) (* x x)" | ./$(TARGET) | tail -1 | grep -q "100" && echo "✓ Variable binding test passed"
	@echo "(if (< 5 10) \"yes\" \"no\")" | ./$(TARGET) | grep -q "yes" && echo "✓ Conditional test passed"
	@echo "(list 1 2 3)" | ./$(TARGET) | grep -q "(1 2 3)" && echo "✓ List test passed"
	@echo "[3 4 5]" | ./$(TARGET) | grep -q "\[3 4 5\]" && echo "✓ Vector literal test passed"
	@echo "[(+ 1 2) (* 3 4)]" | ./$(TARGET) | grep -q "\[3 12\]" && echo "✓ Vector evaluation test passed"
	@echo "(def v [1 2 3]) (first v)" | ./$(TARGET) | tail -1 | grep -q "1" && echo "✓ Vector collection ops test passed"
	@echo "((fn [x y] (+ x y)) 3 5)" | ./$(TARGET) | grep -q "8" && echo "✓ Function with vector params test passed"
	@test "$$(echo '(+ 2 2)' | ./$(TARGET) -s | wc -l)" -eq 0 && echo "✓ Silent mode test passed"
	@echo "(println \"test\")" | ./$(TARGET) -s | grep -q "test" && echo "✓ Silent mode println test passed"
	@echo "(def loop (fn () (loop))) (loop)" | ./$(TARGET) 2>&1 | grep -q "Stack overflow" && echo "✓ Stack overflow protection test passed"
	@echo '(spit "test_file.txt" "Hello CrushLisp") (slurp "test_file.txt")' | ./$(TARGET) | tail -1 | grep -q "Hello CrushLisp" && rm -f test_file.txt && echo "✓ File I/O (slurp/spit) test passed"
	@echo '(def x 10) x' > test_load.lisp && echo '(load "test_load.lisp") (+ x 5)' | ./$(TARGET) | tail -1 | grep -q "15" && rm -f test_load.lisp && echo "✓ File loading (load) test passed"
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
