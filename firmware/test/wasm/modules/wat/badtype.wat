;; A module whose net_buffer returns eight values: the core would copy all eight into
;; a buffer for one, so the module must be refused at load.
(module
  (func (export "net_buffer") (param i32) (result i32 i32 i32 i32 i32 i32 i32 i32)
    i32.const 1 i32.const 1 i32.const 1 i32.const 1
    i32.const 1 i32.const 1 i32.const 1 i32.const 1)
  (func (export "draw")))
