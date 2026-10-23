;; One page of memory with no maximum: it could grow with memory.grow once loaded.
(module
  (memory (export "memory") 1)
  (func (export "draw")))
