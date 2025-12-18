;; -*- clojure -*-
(def map (fn [f coll]
           (if (= coll nil)
             nil
             (cons (f (first coll))
                   (map f (rest coll))))))

(def filter (fn [f coll]
              (if (= coll nil)
                nil
                (if (f (first coll))
                  (cons (first coll) (filter f (rest coll)))
                  (filter f (rest coll))))))
