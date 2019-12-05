(defn worker-main
  [parent]
  (def name (:receive parent))
  (def interval (:receive parent))
  (for i 0 10
    (os/sleep interval)
    (printf "thread %s wakeup no. %d" name i))
  (:send parent name))

(defn make-worker
  [name interval]
  (-> (thread/new)
      (:send worker-main)
      (:send name)
      (:send interval)))

(def bob (make-worker "bob" 0.2))
(def joe (make-worker "joe" 0.3))
(def sam (make-worker "sam" 0.5))

# Receive out of order
(for i 0 3
  (print "worker " (thread/receive [bob sam joe]) " finished!"))
