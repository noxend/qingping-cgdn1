;; A module whose start function never returns: it runs at instantiation, before
;; any limit applies, so the core must refuse the module. Written by hand because
;; clang emits no start section for a module without an entry point.
(module
  (func $spin (loop $again (br $again)))
  (func (export "draw"))
  (start $spin))
