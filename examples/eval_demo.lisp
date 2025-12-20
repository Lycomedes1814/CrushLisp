; Demonstration of eval and dynamic code execution

(println "=== CrushLisp eval Demo ===\n")

; Basic eval with string
(println "1. Basic eval:")
(def result (eval "(+ 10 20 30)"))
(println (str "   (eval \"(+ 10 20 30)\") = " result "\n"))

; Dynamic code generation
(println "2. Dynamic code generation:")
(def operator "+")
(def operands "5 10 15")
(def expression (str "(" operator " " operands ")"))
(println (str "   Generated expression: " expression))
(println (str "   Result: " (eval expression) "\n"))

; Eval with quoted expressions
(println "3. Eval with quoted expressions:")
(def expr '(* 7 8))
(println (str "   expr = " expr))
(println (str "   (eval expr) = " (eval expr) "\n"))

; Multiple expressions in one eval
(println "4. Multiple expressions:")
(eval "(def square (fn [x] (* x x)))")
(eval "(def cube (fn [x] (* x x x)))")
(println (str "   square(6) = " (square 6)))
(println (str "   cube(4) = " (cube 4)))
