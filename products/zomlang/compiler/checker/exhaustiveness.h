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

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/type/type-env.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace checker {

/// \brief Represents a constructor of a type for exhaustiveness checking.
///
/// A constructor is a way to construct a value of the scrutinee type.
/// For example, `bool` has two constructors: `true` and `false`.
/// An enum `Option<T>` has two: `None` and `Some(T)`.
struct Constructor {
  enum class Kind {
    BoolTrue,        ///< Boolean true
    BoolFalse,       ///< Boolean false
    Unit,            ///< Unit value
    Null,            ///< Null value
    EnumVariant,     ///< An enum variant
    UnionBranch,     ///< A branch of a union type
    SealedSubclass,  ///< A direct subclass of a sealed type
    Open,            ///< Sentinel for types with infinitely many constructors
  };

  Kind kind;
  zc::StringPtr name;  ///< Human-readable name for diagnostics
  size_t arity;        ///< Number of fields / sub-patterns

  /// For UnionBranch: the branch type.
  /// For EnumVariant: the first associated data type.
  zc::Maybe<const type::Type&> fieldType;

  /// Construct a simple constructor with no fields.
  static Constructor makeBoolTrue();
  static Constructor makeBoolFalse();
  static Constructor makeUnit();
  static Constructor makeNull();
  static Constructor makeOpen(zc::StringPtr typeName);

  bool operator==(const Constructor& other) const;
  bool operator!=(const Constructor& other) const { return !(*this == other); }
};

/// \brief A single row in the pattern matrix.
///
/// Each element is a NodeId referencing a pattern AST node.
/// The row has N columns where N is the number of "components" being
/// matched against. Initially there is 1 column (the scrutinee itself).
/// Specialization expands constructor patterns into their sub-patterns.
using PatternRow = zc::Vector<ast::NodeId>;

/// \brief The pattern matrix for usefulness checking.
///
/// Each row represents one match arm's patterns. The matrix is used by
/// the usefulness algorithm to determine if a new pattern row adds
/// information not covered by existing rows.
using PatternMatrix = zc::Vector<PatternRow>;

/// \brief ExhaustivenessChecker - verifies that match statements cover all
/// possible cases.
///
/// Implements the usefulness matrix algorithm from Maranget (2007) for
/// checking pattern match exhaustiveness and detecting unreachable arms.
///
/// The algorithm works by constructing a matrix of patterns from existing
/// match arms, then checking if a hypothetical "missing" pattern would be
/// "useful" (i.e., not covered by the existing patterns). If any useful
/// missing pattern exists, the match is non-exhaustive.
///
/// Unreachable arms are detected by checking if each successive arm's
/// pattern is useful relative to the arms that came before it.
///
/// Exhaustiveness rules (RFC 0005):
/// - Enum types: must cover all variants
/// - Boolean: must cover true and false
/// - Union types: must cover each branch
/// - Wildcard pattern always makes the match exhaustive
/// - Sealed types: must cover all direct subclasses (same-file requirement
///   is enforced separately)
/// - Types with infinite constructors (int, str, etc.): always require a
///   wildcard, as literal patterns can never be complete
class ExhaustivenessChecker final {
public:
  /// \brief Construct an ExhaustivenessChecker.
  ///
  /// \param typeEnv   The type environment with inferred types.
  /// \param tree      The AST tree containing the match statement.
  /// \param diags     Diagnostic engine for error reporting.
  ExhaustivenessChecker(type::TypeEnv& typeEnv, const ast::Tree& tree,
                        diagnostics::DiagnosticEngine& diags) noexcept;

  ~ExhaustivenessChecker() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ExhaustivenessChecker);

  /// \brief Check that a match statement is exhaustive.
  ///
  /// Walks the match arms, builds a pattern matrix, and reports:
  /// 1. NonExhaustiveMatch error if patterns don't cover all cases
  /// 2. UnreachableMatchArm warning for arms that can never match
  ///
  /// \param matchStmt     The NodeId of the MatchStmt node.
  /// \param scrutineeType The resolved type of the scrutinee expression.
  void checkMatchExhaustiveness(ast::NodeId matchStmt, const type::Type& scrutineeType);

private:
  struct Impl;
  zc::Own<Impl> impl;

  // ==========================================================================
  // Usefulness matrix algorithm
  // ==========================================================================

  /// \brief Check if a new pattern row is useful relative to the matrix.
  ///
  /// A row is useful if there exists a value that it matches but no row
  /// in the existing matrix matches. This is the core of the Maranget
  /// exhaustiveness algorithm.
  ///
  /// \param matrix The existing pattern matrix.
  /// \param newRow The row to test for usefulness.
  /// \param scrutineeType The type being matched on.
  /// \return true if newRow adds new coverage.
  bool isUseful(const PatternMatrix& matrix, const PatternRow& newRow,
                const type::Type& scrutineeType);

  /// \brief Compute the list of missing patterns for a non-exhaustive match.
  ///
  /// Returns a list of human-readable pattern descriptions that would be
  /// needed to make the match exhaustive. Used for diagnostic messages.
  zc::Vector<zc::String> computeMissingPatterns(const PatternMatrix& matrix,
                                                const type::Type& scrutineeType);

  // ==========================================================================
  // Matrix specialization
  // ==========================================================================

  /// \brief Specialize the matrix for a given constructor.
  ///
  /// For each row in the matrix:
  /// - If the first pattern matches the constructor, decompose it into
  ///   its sub-patterns (arity columns replace the first column).
  /// - If the first pattern is a wildcard, replace it with `arity`
  ///   wildcard columns.
  /// - If the first pattern is a different constructor, drop the row.
  PatternMatrix specialize(const PatternMatrix& matrix, const Constructor& ctor);

  /// \brief Extract the "default" of a matrix.
  ///
  /// Returns rows where the first column is a wildcard or binding pattern,
  /// with the first column removed. Equivalent to specializing for a
  /// hypothetical constructor not covered by any constructor pattern.
  PatternMatrix defaultMatrix(const PatternMatrix& matrix);

  // ==========================================================================
  // Constructor detection
  // ==========================================================================

  /// \brief Get the complete set of constructors for a type.
  ///
  /// Returns the list of constructors that can produce values of this type.
  /// For open types (int, str, etc.), returns a single Open constructor.
  zc::Vector<Constructor> getConstructors(const type::Type& ty);

  /// \brief Check if the seen constructors form a complete set for the type.
  ///
  /// Returns true if every constructor of the type is present in `seen`.
  bool isComplete(const zc::Vector<Constructor>& seen, const type::Type& ty);

  // ==========================================================================
  // Pattern classification helpers
  // ==========================================================================

  /// \brief Check if a pattern is a wildcard (matches everything).
  ///
  /// Wildcard patterns include:
  /// - WildcardPattern (`_`)
  /// - BindingPattern (`x`, `mut x`) - these always match
  /// - IdentifierPattern that refers to a non-enum-variant binding
  bool isWildcardPattern(ast::NodeId patId) const;

  /// \brief Get the constructor that a pattern matches, if it matches
  /// exactly one constructor.
  ///
  /// Returns zc::none if the pattern is a wildcard or otherwise doesn't
  /// correspond to a specific constructor.
  zc::Maybe<const Constructor&> getPatternConstructor(ast::NodeId patId,
                                                      const type::Type& scrutineeType);

  /// \brief Check if a pattern matches a given constructor.
  bool patternMatchesConstructor(ast::NodeId patId, const Constructor& ctor,
                                 const type::Type& scrutineeType);

  /// \brief Extract sub-patterns from a constructor pattern.
  ///
  /// For an EnumPattern with args, returns the argument patterns.
  /// For a wildcard, returns `arity` wildcard NodeIds (all invalid).
  PatternRow extractSubPatterns(ast::NodeId patId, size_t arity);

  /// \brief Build a pattern row from a match arm's pattern.
  PatternRow buildPatternRow(ast::NodeId patId);

  /// \brief Generate a human-readable description of a missing constructor.
  zc::String describeMissingConstructor(const Constructor& ctor);

  // ==========================================================================
  // Helpers
  // ==========================================================================

  /// \brief Report a diagnostic at the given AST node's location.
  void reportError(ast::NodeId node, diagnostics::DiagID id, zc::StringPtr message);

  /// \brief Report a warning at the given AST node's location.
  void reportWarning(ast::NodeId node, diagnostics::DiagID id);

  /// \brief Get source location for a node.
  source::SourceLoc nodeLoc(ast::NodeId id) const;

  /// \brief Extract enum variant names from a named type referencing an enum.
  zc::Vector<zc::StringPtr> getEnumVariantNames(const type::NamedType& namedTy);

  /// \brief Check if the type is "open" (infinitely many constructors).
  bool isOpenType(const type::Type& ty);
};

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
