# Concurrency

Concurrency syntax is reserved for future language design.

The current parser grammar does not define `async`, `await`, actor declarations, task groups,
channels, or concurrency block syntax. These words may remain reserved lexically, but source code
that uses them as concurrency constructs is not valid Zom today.

Future concurrency design must be added to the grammar, parser, AST, semantic analysis, and tests
as one coherent feature rather than inferred from reserved keywords.
