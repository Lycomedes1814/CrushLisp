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
	@echo "(map? (hash-map \"a\" 1))" | ./$(TARGET) | grep -qx "true" && echo "✓ map? predicate test passed"
	@echo "(apply + (list 1 2 3 4 5))" | ./$(TARGET) | grep -qx "15" && echo "✓ apply basic test passed"
	@echo "(apply + 1 2 (list 3 4))" | ./$(TARGET) | grep -qx "10" && echo "✓ apply with leading args test passed"
	@echo "(apply str (list \"a\" \"b\" \"c\"))" | ./$(TARGET) | grep -qx '"abc"' && echo "✓ apply with str test passed"
	@echo "(reduce + 0 (list 1 2 3 4 5))" | ./$(TARGET) | grep -qx "15" && echo "✓ reduce sum test passed"
	@echo "(reduce * 1 (list 1 2 3 4 5))" | ./$(TARGET) | grep -qx "120" && echo "✓ reduce product test passed"
	@echo "(reduce + 0 nil)" | ./$(TARGET) | grep -qx "0" && echo "✓ reduce empty list test passed"
	@echo "(loop [i 0 acc 0] (if (> i 5) acc (recur (+ i 1) (+ acc i))))" | ./$(TARGET) | grep -qx "15" && echo "✓ loop/recur accumulator test passed"
	@echo "(loop [i 10] (if (= i 0) \"done\" (recur (- i 1))))" | ./$(TARGET) | grep -qx '"done"' && echo "✓ loop/recur countdown test passed"
	@echo "(try (throw \"oops\") (catch e (str \"caught: \" e)))" | ./$(TARGET) | grep -qx '"caught: oops"' && echo "✓ try/throw test passed"
	@echo "(try (+ 1 2) (catch e \"error\"))" | ./$(TARGET) | grep -qx "3" && echo "✓ try without error test passed"
	@echo "(try (/ 1 0) (catch e \"zero\"))" | ./$(TARGET) | grep -qx '"zero"' && echo "✓ try catches runtime error test passed"
	@echo "(when true 42)" | ./$(TARGET) | grep -qx "42" && echo "✓ when truthy test passed"
	@echo "(when false 42)" | ./$(TARGET) | grep -qx "nil" && echo "✓ when falsy test passed"
	@echo "(get (hash-map \"a\" 1 \"b\" 2) \"a\")" | ./$(TARGET) | grep -qx "1" && echo "✓ hash-map get test passed"
	@echo "(get (hash-map \"a\" 1) \"z\" 99)" | ./$(TARGET) | grep -qx "99" && echo "✓ hash-map get with default test passed"
	@echo "(assoc (hash-map \"a\" 1) \"b\" 2 \"c\" 3)" | ./$(TARGET) | grep -q '"a"' && echo "✓ assoc test passed"
	@echo "(get (dissoc (hash-map \"a\" 1 \"b\" 2) \"a\") \"a\" nil)" | ./$(TARGET) | grep -qx "nil" && echo "✓ dissoc test passed"
	@echo "(contains? (hash-map \"x\" 1) \"x\")" | ./$(TARGET) | grep -qx "true" && echo "✓ contains? present test passed"
	@echo "(contains? (hash-map \"x\" 1) \"y\")" | ./$(TARGET) | grep -qx "false" && echo "✓ contains? absent test passed"
	@echo "(map (fn [x] (* x x)) (list 1 2 3))" | ./$(TARGET) | grep -qx "(1 4 9)" && echo "✓ stdlib map test passed"
	@echo "(filter (fn [x] (> x 2)) (list 1 2 3 4 5))" | ./$(TARGET) | grep -qx "(3 4 5)" && echo "✓ stdlib filter test passed"
	@echo "(upper-case \"hello\")" | ./$(TARGET) | grep -qx '"HELLO"' && echo "✓ upper-case test passed" || (echo "✗ upper-case test failed" && exit 1)
	@echo "(lower-case \"WORLD\")" | ./$(TARGET) | grep -qx '"world"' && echo "✓ lower-case test passed" || (echo "✗ lower-case test failed" && exit 1)
	@echo "(trim \"  hi  \")" | ./$(TARGET) | grep -qx '"hi"' && echo "✓ trim test passed" || (echo "✗ trim test failed" && exit 1)
	@echo "(substring \"hello\" 1 3)" | ./$(TARGET) | grep -qx '"el"' && echo "✓ substring test passed" || (echo "✗ substring test failed" && exit 1)
	@echo "(starts-with? \"foobar\" \"foo\")" | ./$(TARGET) | grep -qx "true" && echo "✓ starts-with? test passed" || (echo "✗ starts-with? test failed" && exit 1)
	@echo "(ends-with? \"foobar\" \"bar\")" | ./$(TARGET) | grep -qx "true" && echo "✓ ends-with? test passed" || (echo "✗ ends-with? test failed" && exit 1)
	@echo "(replace \"hello world\" \"world\" \"lisp\")" | ./$(TARGET) | grep -qx '"hello lisp"' && echo "✓ replace test passed" || (echo "✗ replace test failed" && exit 1)
	@echo "(index-of \"hello\" \"ll\")" | ./$(TARGET) | grep -qx "2" && echo "✓ index-of string test passed" || (echo "✗ index-of string test failed" && exit 1)
	@echo "(index-of (list 1 2 3) 2)" | ./$(TARGET) | grep -qx "1" && echo "✓ index-of list test passed" || (echo "✗ index-of list test failed" && exit 1)
	@echo "(str/join \", \" (list \"a\" \"b\" \"c\"))" | ./$(TARGET) | grep -qx '"a, b, c"' && echo "✓ str/join test passed" || (echo "✗ str/join test failed" && exit 1)
	@echo "(format \"%s has %d items\" \"Alice\" 3)" | ./$(TARGET) | grep -qx '"Alice has 3 items"' && echo "✓ format test passed" || (echo "✗ format test failed" && exit 1)
	@echo "(parse-number \"42.5\")" | ./$(TARGET) | grep -qx "42.5" && echo "✓ parse-number test passed" || (echo "✗ parse-number test failed" && exit 1)
	@echo "(parse-number \"bad\")" | ./$(TARGET) | grep -qx "nil" && echo "✓ parse-number nil test passed" || (echo "✗ parse-number nil test failed" && exit 1)
	@echo "(sort (list 3 1 2))" | ./$(TARGET) | grep -qx "(1 2 3)" && echo "✓ sort test passed" || (echo "✗ sort test failed" && exit 1)
	@echo "(sort-by count (list \"banana\" \"apple\" \"fig\"))" | ./$(TARGET) | grep -qx '("fig" "apple" "banana")' && echo "✓ sort-by test passed" || (echo "✗ sort-by test failed" && exit 1)
	@echo "(contains? \"foobar\" \"foo\")" | ./$(TARGET) | grep -qx "true" && echo "✓ contains? string test passed" || (echo "✗ contains? string test failed" && exit 1)
	@echo "(-> 1 inc inc)" | ./$(TARGET) | grep -qx "3" && echo "✓ -> thread-first test passed" || (echo "✗ -> thread-first test failed" && exit 1)
	@echo "(->> (list 1 2 3) (map inc) (filter (fn [x] (> x 2))))" | ./$(TARGET) | grep -qx "(3 4)" && echo "✓ ->> thread-last test passed" || (echo "✗ ->> thread-last test failed" && exit 1)
	@echo "(do (doseq [x (list 1 2 3)] (print x)) nil)" | ./$(TARGET) | grep -q "123" && echo "✓ doseq test passed" || (echo "✗ doseq test failed" && exit 1)
	@echo "(do (dotimes [i 3] (print i)) nil)" | ./$(TARGET) | grep -q "012" && echo "✓ dotimes test passed" || (echo "✗ dotimes test failed" && exit 1)
	@echo "(do (defn square [x] (* x x)) (square 5))" | ./$(TARGET) | grep -qx "25" && echo "✓ defn basic test passed" || (echo "✗ defn basic test failed" && exit 1)
	@echo "(do (defn add [a b] (+ a b)) (add 3 4))" | ./$(TARGET) | grep -qx "7" && echo "✓ defn multi-arg test passed" || (echo "✗ defn multi-arg test failed" && exit 1)
	@echo "(do (defn fact [n] (if (= n 0) 1 (* n (fact (- n 1))))) (fact 5))" | ./$(TARGET) | grep -qx "120" && echo "✓ defn recursive test passed" || (echo "✗ defn recursive test failed" && exit 1)
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
