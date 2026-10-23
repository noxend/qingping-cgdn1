;; An led that takes an argument: the core calls it with none.
(module
  (memory (export "memory") 1 1)
  (func (export "draw"))
  (func (export "led") (param i32) (result i32) (i32.const 0)))
