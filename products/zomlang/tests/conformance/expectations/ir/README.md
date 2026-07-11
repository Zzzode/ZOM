# IR Expectations

Each `.check` file invokes `zomc compile --emit ir` for a same-relative-path
source under `conformance/corpus/` and validates deterministic lowering with
FileCheck.
