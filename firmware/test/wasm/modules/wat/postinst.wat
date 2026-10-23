;; A module exporting __post_instantiate that never returns: the runtime calls that
;; export by name while it instantiates, before any limit, so the core must refuse it.
(module
  (func (export "__post_instantiate") (loop $again (br $again)))
  (func (export "draw")))
