;; A table of 1,500,000 entries: a few bytes here, about 6 MB of PSRAM once instantiated.
(module
  (memory (export "memory") 1 1)
  (table 1500000 1500000 funcref)
  (func (export "draw")))
