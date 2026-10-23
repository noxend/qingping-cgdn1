;; Two 64 KB pages of memory that cannot grow: taken, weighed against the PSRAM free.
(module
  (memory (export "memory") 2 2)
  (func (export "draw")))
