// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include <cstdint>

namespace zomlang {
namespace compiler {
namespace symbol {

/// \brief Unified symbol flags
///
/// This integrates TypeScript's SymbolFlags, NodeFlags, and TypeFlags
/// into a unified 64-bit system that captures all symbol properties.
/// Organized by logical groups with clear bit allocation.
/// \brief Comprehensive documentation for SymbolFlags
///
/// This enumeration provides a complete set of flags for symbol classification
/// in the ZomLang compiler. The flags are organized into logical groups with
/// careful bit allocation to avoid conflicts and enable efficient operations.
///
/// \section Flag Categories
///
/// 1. **Basic Classification**: Core symbol types
/// 2. **Symbol Kinds**: Specific symbol categories
/// 3. **Visibility**: Access control levels
/// 4. **Storage**: Storage and lifetime semantics
/// 5. **Mutability**: Modification permissions
/// 6. **Inheritance**: Object-oriented features
/// 7. **Special Properties**: Function and value properties
/// 8. **Type System**: Generic and type-level features
/// 9. **Compiler & Meta**: Compiler-generated symbols
/// 10. **Status & Diagnostics**: Symbol status and warnings
/// 11. **Reserved**: Future expansion
///
/// \section Usage Examples
///
/// ```cpp
/// // Public mutable variable
/// auto flags = SymbolFlags::Variable | SymbolFlags::Public | SymbolFlags::Mutable;
///
/// // Private async method
/// auto methodFlags = SymbolFlags::Method | SymbolFlags::Private | SymbolFlags::Async;
///
/// // Generic interface with covariant type parameter
/// auto interfaceFlags = SymbolFlags::Interface | SymbolFlags::Generic |
///                       SymbolFlags::Covariant;
/// ```
///
/// \section Validation
///
/// Use the provided validation functions to ensure flag combinations are valid:
/// - `isValidSymbolFlags()`: Comprehensive validation
/// - `hasValidVisibility()`: Check visibility exclusivity
/// - `hasValidStorage()`: Check storage exclusivity
///
/// \see utility for utility functions
/// \see Symbol for usage in symbol table
enum class SymbolFlags : uint64_t {
  None = 0,

  /// \brief Kind flags - Fundamental classification
  TermKind = 1ULL << 0,  // Term-level symbols (values, functions, variables)
  TypeKind = 1ULL << 1,  // Type-level symbols (classes, interfaces, type aliases)

  /// \brief Core symbol categories - What the symbol represents
  Variable = 1ULL << 2,   // Variables (mutable or immutable)
  Function = 1ULL << 3,   // Functions and methods
  Class = 1ULL << 4,      // Classes and structs
  Interface = 1ULL << 5,  // Interfaces/traits
  Enum = 1ULL << 6,       // Enumerations
  Module = 1ULL << 7,     // Modules/namespaces
  Package = 1ULL << 8,    // Packages
  TypeAlias = 1ULL << 9,  // Type aliases

  /// \brief Symbol roles - How the symbol is used
  Parameter = 1ULL << 10,    // Function/method parameters
  Field = 1ULL << 11,        // Class/struct fields
  Method = 1ULL << 12,       // Instance/static methods
  Constructor = 1ULL << 13,  // Constructors
  Destructor = 1ULL << 14,   // Destructors
  Property = 1ULL << 15,     // Properties with getters/setters
  Constant = 1ULL << 16,     // Compile-time constants

  /// \brief Visibility modifiers
  Public = 1ULL << 17,     // Public visibility
  Private = 1ULL << 18,    // Private visibility
  Protected = 1ULL << 19,  // Protected visibility
  Internal = 1ULL << 20,   // Internal/module-private visibility

  /// \brief Storage & mutability
  Mutable = 1ULL << 21,    // Mutable storage
  Immutable = 1ULL << 22,  // Immutable storage (explicit)
  Static = 1ULL << 23,     // Static lifetime/storage
  Instance = 1ULL << 24,   // Instance member
  Local = 1ULL << 25,      // Local scope
  Global = 1ULL << 26,     // Global scope

  /// \brief Inheritance & override
  Abstract = 1ULL << 27,  // Abstract classes/methods
  Virtual = 1ULL << 28,   // Virtual methods
  Override = 1ULL << 29,  // Overriding members
  Final = 1ULL << 30,     // Final (non-overridable)
  Sealed = 1ULL << 31,    // Sealed classes
  Open = 1ULL << 32,      // Open for extension

  /// \brief Special properties
  Inline = 1ULL << 33,     // Inline functions/values
  Lazy = 1ULL << 34,       // Lazy evaluation
  Async = 1ULL << 35,      // Async functions
  Generator = 1ULL << 36,  // Generator functions
  Extension = 1ULL << 37,  // Extension methods
  Operator = 1ULL << 38,   // Operator overloads
  Implicit = 1ULL << 39,   // Implicit conversions/parameters
  Explicit = 1ULL << 40,   // Explicit constructors/conversions
  Pure = 1ULL << 41,       // Pure functions (no side effects)
  Tail = 1ULL << 42,       // Tail recursive functions

  /// \brief Type system
  Generic = 1ULL << 43,        // Generic types/functions
  Covariant = 1ULL << 44,      // Covariant type parameter
  Contravariant = 1ULL << 45,  // Contravariant type parameter
  Invariant = 1ULL << 46,      // Invariant type parameter (explicit)
  Phantom = 1ULL << 47,        // Phantom type parameter
  Associated = 1ULL << 48,     // Associated types
  Dependent = 1ULL << 49,      // Dependent types
  Higher = 1ULL << 50,         // Higher-kinded types

  /// \brief Compiler & meta
  Synthetic = 1ULL << 51,  // Compiler-generated symbols
  Builtin = 1ULL << 52,    // Built-in symbols
  Macro = 1ULL << 53,      // Macro definitions
  Template = 1ULL << 54,   // Template symbols
  Deferred = 1ULL << 55,   // Declared but not defined
  Forward = 1ULL << 56,    // Forward declarations
  Extern = 1ULL << 57,     // External linkage
  Export = 1ULL << 58,     // Exported symbols

  /// \brief Status & diagnostics
  Error = 1ULL << 59,         // Error symbols
  Deprecated = 1ULL << 60,    // Deprecated symbols
  Experimental = 1ULL << 61,  // Experimental features
  Unsafe = 1ULL << 62,        // Unsafe operations

  /// \brief Reserved for future use
  Reserved1 = 1ULL << 63,

  // Composite masks
  // These provide convenient groupings for common flag combinations

  /// \brief Visibility mask - mutually exclusive visibility levels
  VisibilityMask = Public | Private | Protected | Internal,

  /// \brief Storage mask - mutually exclusive storage types
  StorageMask = Static | Instance | Local | Global,

  /// \brief Mutability mask - mutually exclusive mutability states
  MutabilityMask = Mutable | Immutable,

  /// \brief Inheritance mask - inheritance-related flags
  InheritanceMask = Abstract | Virtual | Override | Final | Sealed | Open,

  /// \brief Special properties mask - function properties
  SpecialMask =
      Inline | Lazy | Async | Generator | Extension | Operator | Implicit | Explicit | Pure | Tail,

  /// \brief Type parameter variance mask
  VarianceMask = Covariant | Contravariant | Invariant,

  /// \brief Compiler-generated mask
  CompilerMask = Synthetic | Builtin | Template,

  /// \brief Status mask - symbol status flags
  StatusMask = Error | Deprecated | Experimental | Unsafe,

  /// \brief All function-related flags
  FunctionFlags = Function | SpecialMask | Generic | VarianceMask,

  /// \brief All type-related flags
  TypeFlags = TypeKind | Class | Interface | Enum | TypeAlias | Generic | VarianceMask |
              Associated | Dependent | Higher,

  /// \brief All term-related flags
  TermFlags = TermKind | Variable | Function | Parameter | Property | Method | SpecialMask |
              StorageMask | MutabilityMask,

  /// \brief All declaration flags
  DeclarationFlags = VisibilityMask | StorageMask | MutabilityMask | InheritanceMask | Inline |
                     Async | Extension | Operator,

  /// \brief All meta-programming flags
  MetaFlags = Macro | Template | Implicit | Explicit | Deferred | Forward | Extern | Export,

  /// \brief Commonly used combinations
  PublicMethod = Public | Method | TermKind,
  PrivateField = Private | Field | TermKind,
  AbstractClass = Abstract | Class | TypeKind,
  StaticFunction = Static | Function | TermKind,
  GenericInterface = Generic | Interface | TypeKind,
  AsyncFunction = Async | Function | TermKind,
  ConstantValue = Constant | Immutable | TermKind,
  VirtualMethod = Virtual | Method | TermKind,
  OverrideMethod = Override | Method | TermKind,
  ExtensionMethod = Extension | Method | TermKind,
  OperatorOverload = Operator | Method | TermKind,
  TypeParameter = Generic | TypeKind,
  ForwardDeclaration = Forward | Deferred,
  ExportedSymbol = Export | Public,
  DeprecatedAPI = Deprecated | Public,
  ExperimentalFeature = Experimental | Unsafe,

  /// \brief Validation masks for mutually exclusive flags
  /// These help ensure only one flag from each group is set
  KindMask = TermKind | TypeKind,
  AllVisibilityMask = Public | Private | Protected | Internal,
  AllStorageMask = Static | Instance | Local | Global,
  AllMutabilityMask = Mutable | Immutable,
  AllVarianceMask = Covariant | Contravariant | Invariant,
};

inline SymbolFlags operator|(SymbolFlags a, SymbolFlags b) {
  return static_cast<SymbolFlags>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

inline SymbolFlags operator&(SymbolFlags a, SymbolFlags b) {
  return static_cast<SymbolFlags>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}

inline SymbolFlags operator^(SymbolFlags a, SymbolFlags b) {
  return static_cast<SymbolFlags>(static_cast<uint64_t>(a) ^ static_cast<uint64_t>(b));
}

inline SymbolFlags operator~(SymbolFlags a) {
  return static_cast<SymbolFlags>(~static_cast<uint64_t>(a));
}

inline SymbolFlags& operator|=(SymbolFlags& a, SymbolFlags b) {
  a = a | b;
  return a;
}

inline SymbolFlags& operator&=(SymbolFlags& a, SymbolFlags b) {
  a = a & b;
  return a;
}

inline bool hasFlag(SymbolFlags flags, SymbolFlags flag) {
  return (flags & flag) != SymbolFlags::None;
}

inline bool hasAnyFlag(SymbolFlags flags, SymbolFlags mask) {
  return (flags & mask) != SymbolFlags::None;
}

/// \brief Utility functions for SymbolFlags operations
///
/// This namespace provides helper functions for working with SymbolFlags,
/// including validation, querying, and manipulation operations.
namespace utility {

/// \brief Check if a symbol has all specified flags
/// \param flags The symbol's flags
/// \param requiredFlags The flags to check for
/// \return true if all required flags are present
inline bool hasAllFlags(SymbolFlags flags, SymbolFlags requiredFlags) noexcept {
  return (static_cast<uint64_t>(flags) & static_cast<uint64_t>(requiredFlags)) ==
         static_cast<uint64_t>(requiredFlags);
}

/// \brief Check if a symbol has any of the specified flags
/// \param flags The symbol's flags
/// \param testFlags The flags to check for
/// \return true if any of the test flags are present
inline bool hasAnyFlags(SymbolFlags flags, SymbolFlags testFlags) noexcept {
  return (static_cast<uint64_t>(flags) & static_cast<uint64_t>(testFlags)) != 0;
}

/// \brief Add flags to a symbol
/// \param flags The symbol's current flags
/// \param newFlags The flags to add
/// \return The combined flags
inline SymbolFlags addFlags(SymbolFlags flags, SymbolFlags newFlags) noexcept {
  return static_cast<SymbolFlags>(static_cast<uint64_t>(flags) | static_cast<uint64_t>(newFlags));
}

/// \brief Remove flags from a symbol
/// \param flags The symbol's current flags
/// \param flagsToRemove The flags to remove
/// \return The flags with specified flags removed
inline SymbolFlags removeFlags(SymbolFlags flags, SymbolFlags flagsToRemove) noexcept {
  return static_cast<SymbolFlags>(static_cast<uint64_t>(flags) &
                                  ~static_cast<uint64_t>(flagsToRemove));
}

/// \brief Check if visibility flags are valid (mutually exclusive)
/// \param flags The flags to validate
/// \return true if visibility flags are valid
inline bool hasValidVisibility(SymbolFlags flags) noexcept {
  const auto visibilityBits =
      static_cast<uint64_t>(flags) & static_cast<uint64_t>(SymbolFlags::VisibilityMask);
  // Check if at most one visibility flag is set (power of 2 or 0)
  return (visibilityBits & (visibilityBits - 1)) == 0;
}

/// \brief Check if storage flags are valid (mutually exclusive)
/// \param flags The flags to validate
/// \return true if storage flags are valid
inline bool hasValidStorage(SymbolFlags flags) noexcept {
  const auto storageBits =
      static_cast<uint64_t>(flags) & static_cast<uint64_t>(SymbolFlags::StorageMask);
  // Check if at most one storage flag is set (power of 2 or 0)
  return (storageBits & (storageBits - 1)) == 0;
}

/// \brief Check if mutability flags are valid (mutually exclusive)
/// \param flags The flags to validate
/// \return true if mutability flags are valid
inline bool hasValidMutability(SymbolFlags flags) noexcept {
  const auto mutabilityBits =
      static_cast<uint64_t>(flags) & static_cast<uint64_t>(SymbolFlags::MutabilityMask);
  // Check if at most one mutability flag is set (power of 2 or 0)
  return (mutabilityBits & (mutabilityBits - 1)) == 0;
}

/// \brief Check if variance flags are valid (mutually exclusive)
/// \param flags The flags to validate
/// \return true if variance flags are valid
inline bool hasValidVariance(SymbolFlags flags) noexcept {
  const auto varianceBits =
      static_cast<uint64_t>(flags) & static_cast<uint64_t>(SymbolFlags::VarianceMask);
  // Check if at most one variance flag is set (power of 2 or 0)
  return (varianceBits & (varianceBits - 1)) == 0;
}

/// \brief Comprehensive validation of symbol flags
/// \param flags The flags to validate
/// \return true if all flag combinations are valid
inline bool isValidSymbolFlags(SymbolFlags flags) noexcept {
  // Check basic requirements
  if (!hasValidVisibility(flags)) return false;
  if (!hasValidStorage(flags)) return false;
  if (!hasValidMutability(flags)) return false;
  if (!hasValidVariance(flags)) return false;

  // Check that at least one kind flag is set
  if (!hasAnyFlags(flags, static_cast<SymbolFlags>(static_cast<uint64_t>(SymbolFlags::TermKind) |
                                                   static_cast<uint64_t>(SymbolFlags::TypeKind)))) {
    return false;
  }

  return true;
}

/// \brief Check if a symbol is a valid function
/// \param flags The flags to check
/// \return true if the symbol represents a valid function
inline bool isFunction(SymbolFlags flags) noexcept {
  return hasAnyFlags(flags, SymbolFlags::Function | SymbolFlags::Method | SymbolFlags::Constructor);
}

/// \brief Check if a symbol is a valid type
/// \param flags The flags to check
/// \return true if the symbol represents a valid type
inline bool isType(SymbolFlags flags) noexcept { return hasAnyFlags(flags, SymbolFlags::TypeKind); }

/// \brief Check if a symbol is publicly accessible
/// \param flags The flags to check
/// \return true if the symbol is public
inline bool isPublic(SymbolFlags flags) noexcept { return hasAllFlags(flags, SymbolFlags::Public); }

/// \brief Check if a symbol is compiler-generated
/// \param flags The flags to check
/// \return true if the symbol is synthetic/builtin
inline bool isCompilerGenerated(SymbolFlags flags) noexcept {
  return hasAnyFlags(flags, SymbolFlags::CompilerMask);
}

}  // namespace utility

/// \brief Get term flags (remove type kind, add term kind)
inline SymbolFlags toTermFlags(SymbolFlags flags) noexcept {
  // Remove type-specific flags and add TermKind
  SymbolFlags typeSpecificFlags =
      SymbolFlags::Class | SymbolFlags::Interface | SymbolFlags::Enum | SymbolFlags::TypeAlias;
  return static_cast<SymbolFlags>((flags & ~(SymbolFlags::TypeKind | typeSpecificFlags)) |
                                  SymbolFlags::TermKind);
}

/// \brief Get type flags (remove term kind, add type kind)
inline SymbolFlags toTypeFlags(SymbolFlags flags) noexcept {
  // Remove term-specific flags and add TypeKind
  SymbolFlags termSpecificFlags = SymbolFlags::Function | SymbolFlags::Variable |
                                  SymbolFlags::Parameter | SymbolFlags::Method |
                                  SymbolFlags::Property | SymbolFlags::Constant;
  return static_cast<SymbolFlags>((flags & ~(SymbolFlags::TermKind | termSpecificFlags)) |
                                  SymbolFlags::TypeKind);
}

/// \brief Get common flags (both term and type kinds)
inline SymbolFlags toCommonFlags(SymbolFlags flags) noexcept {
  // Extract only common flags that apply to both term and type symbols
  SymbolFlags commonFlags = flags & (SymbolFlags::VisibilityMask | SymbolFlags::StorageMask |
                                     SymbolFlags::MutabilityMask | SymbolFlags::InheritanceMask |
                                     SymbolFlags::StatusMask | SymbolFlags::CompilerMask);

  // Convert specific combinations to common patterns
  if (utility::hasAllFlags(flags, SymbolFlags::ExportedSymbol)) {
    commonFlags = utility::addFlags(commonFlags, SymbolFlags::Export | SymbolFlags::Public);
  }

  return commonFlags;
}

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
