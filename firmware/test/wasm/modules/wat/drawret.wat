;; A module whose draw returns a value: the core calls draw with no buffer for one.
(module
  (func (export "draw") (result i32) i32.const 7))
