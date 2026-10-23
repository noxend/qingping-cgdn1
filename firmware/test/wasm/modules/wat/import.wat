;; A module that calls a function the SDK does not have: the core refuses it at the
;; upload and names the call, so its author knows what to remove.
(module
  (import "env" "gfx_line" (func $line (param i32 i32 i32 i32)))
  (func (export "draw") (call $line (i32.const 0) (i32.const 0) (i32.const 9) (i32.const 9))))
