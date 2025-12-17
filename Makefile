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
	@test "$$(echo '(+ 2 2)' | ./$(TARGET) -s | wc -l)" -eq 0 && echo "✓ Silent mode test passed"
	@echo "(println \"test\")" | ./$(TARGET) -s | grep -q "test" && echo "✓ Silent mode println test passed"
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
