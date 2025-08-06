# Zom Language Specification

This is the official specification for the Zom programming language. The specification has been organized into individual chapters for better maintainability and navigation.

## Quick Navigation

For the complete specification, please refer to the individual chapter files in the [`chapters/`](chapters/) directory:

### Core Language
- [Introduction](chapters/01-introduction.md) - Design principles and language overview
- [Lexical Structure](chapters/02-lexical-structure.md) - Tokens, keywords, and syntax elements
- [Types](chapters/03-types.md) - Type system and type expressions
- [Expressions](chapters/04-expressions.md) - Expression syntax and evaluation
- [Statements](chapters/05-statements.md) - Control flow and execution
- [Declarations](chapters/06-declarations.md) - Variable, function, and type declarations
- [Patterns](chapters/07-patterns.md) - Pattern matching and destructuring

### Object-Oriented Features
- [Classes and Structures](chapters/08-classes-and-structures.md) - Object-oriented programming
- [Interfaces](chapters/09-interfaces.md) - Interface definitions and contracts
- [Enumerations](chapters/10-enumerations.md) - Enum types and algebraic data types

### Advanced Features
- [Error Handling](chapters/11-error-handling.md) - Exception handling and error propagation
- [Generics](chapters/12-generics.md) - Generic programming and type parameters
- [Modules and Imports](chapters/13-modules-and-imports.md) - Code organization and namespaces
- [Memory Management](chapters/14-memory-management.md) - Memory safety and resource management
- [Concurrency](chapters/15-concurrency.md) - Async/await and concurrent programming
- [Attributes and Annotations](chapters/16-attributes-and-annotations.md) - Metadata and compile-time directives

### Reference
- [Grammar Reference](chapters/17-grammar-reference.md) - Complete ANTLR4 grammar definitions

## About This Organization

The specification was previously contained in a single large file, which led to:
- Difficulty in navigation and maintenance
- Potential for content duplication
- Challenges in collaborative editing

By splitting the specification into focused chapters, we achieve:
- **Better Organization**: Each topic has its own dedicated file
- **Improved Navigation**: Easy to find specific language features
- **Enhanced Maintainability**: Changes can be made to individual sections
- **Reduced Redundancy**: Eliminates duplicate content
- **Collaborative Friendly**: Multiple contributors can work simultaneously

## Grammar Consistency

The grammar definitions are maintained in sync with the ANTLR4 parser files:
- [`ZomParser.g4`](../../grammar/ZomParser.g4) - Parser grammar rules
- [`ZomLexer.g4`](../../grammar/ZomLexer.g4) - Lexer tokens and keywords

For details on grammar consistency, see [`CONSISTENCY_REVIEW.md`](CONSISTENCY_REVIEW.md).

## Contributing

When contributing to the specification:

1. **Edit the appropriate chapter file** in the `chapters/` directory
2. **Maintain consistency** across related chapters
3. **Update cross-references** when adding new content
4. **Keep grammar in sync** with ANTLR4 files
5. **Follow the established format** and style

## Version History

- **v2.0** - Restructured into modular chapters (current)
- **v1.x** - Single-file specification with grammar consistency updates

---

*For the complete specification content, navigate to the individual chapter files listed above.*
