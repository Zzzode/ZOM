# Zom Language Specification - Chapters

This directory contains the Zom language specification split into individual chapters for better organization and maintenance.

## Table of Contents

1. [Introduction](01-introduction.md) - Design principles and language features
2. [Lexical Structure](02-lexical-structure.md) - Tokens, keywords, literals, and operators
3. [Types](03-types.md) - Type system, predefined types, and type expressions
4. [Expressions](04-expressions.md) - Expression categories and operator precedence
5. [Statements](05-statements.md) - Control flow and program execution
6. [Declarations](06-declarations.md) - Variables, functions, types, and classes
7. [Patterns](07-patterns.md) - Pattern matching and destructuring
8. [Classes and Structures](08-classes-and-structures.md) - Object-oriented programming
9. [Interfaces](09-interfaces.md) - Interface definitions and implementations
10. [Enumerations](10-enumerations.md) - Enum types and variants
11. [Error Handling](11-error-handling.md) - Exception handling and error types
12. [Generics](12-generics.md) - Generic programming and type parameters
13. [Modules and Imports](13-modules-and-imports.md) - Module system and code organization
14. [Memory Management](14-memory-management.md) - Memory safety and resource management
15. [Concurrency](15-concurrency.md) - Async/await and concurrent programming
16. [Attributes and Annotations](16-attributes-and-annotations.md) - Metadata and compile-time directives
17. [Grammar Reference](17-grammar-reference.md) - Complete ANTLR4 grammar definitions

## Organization

Each chapter is a self-contained Markdown file that covers a specific aspect of the Zom language. This modular approach provides several benefits:

- **Better Navigation**: Easy to find and reference specific topics
- **Maintainability**: Changes can be made to individual sections without affecting others
- **Collaboration**: Multiple contributors can work on different chapters simultaneously
- **Documentation Tools**: Each chapter can be processed independently by documentation generators

## Original Source

These chapters were extracted from the original `SPEC.md` file to eliminate redundancy and improve organization. The content has been preserved while being restructured for better accessibility.

## Contributing

When making changes to the specification:

1. Edit the appropriate chapter file
2. Ensure consistency with related chapters
3. Update cross-references as needed
4. Maintain the grammar definitions in sync with the ANTLR4 files

## Grammar Consistency

The grammar definitions in Chapter 17 should remain consistent with the ANTLR4 grammar files:

- `ZomParser.g4` - Parser grammar rules
- `ZomLexer.g4` - Lexer tokens and keywords

Refer to `CONSISTENCY_REVIEW.md` for details on maintaining this consistency.
