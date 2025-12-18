;; -*- clojure -*-
(def map (fn (f coll)
           (if (= coll nil)
             nil
             (cons (f (first coll))
                   (map f (rest coll))))))
