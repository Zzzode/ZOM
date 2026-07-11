# ZOM IR Conformance Runner

This runner checks deterministic target-independent IR emitted by the fully
checked compiler pipeline. Expectation files live under
`conformance/expectations/ir/` and invoke `zomc compile --emit ir` against the
shared source corpus.

IR tests verify lowering and ABI contracts. Parser AST shape and user-facing
diagnostics remain owned by their existing runners.
