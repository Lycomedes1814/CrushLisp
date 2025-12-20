; File I/O example for CrushLisp

; Write data to a file
(spit "data.txt" "Hello, World!\nThis is CrushLisp.")

; Read the file back
(def content (slurp "data.txt"))
(println "File contents:")
(println content)

; Create a library file
(spit "mylib.lisp" "(def square (fn [x] (* x x)))\n(def double (fn [x] (+ x x)))")

; Load the library
(load "mylib.lisp")

; Use the loaded functions
(println "\nTesting loaded functions:")
(println "square(5) =" (square 5))
(println "double(10) =" (double 10))
