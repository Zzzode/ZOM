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

#include "zc/core/string.h"
#include "zomlang/compiler/type/type-env.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace type {

class PrimitiveType;
class FunctionType;
class TupleType;
class ObjectType;
class ArrayType;
class NamedType;
class TypeVar;
class UnionType;
class ReferenceType;
class RawPointerType;
class ExistentialType;

/// \brief UnificationEngine - Performs first-order unification of types.
///
/// The unification engine implements the unification rules defined in
/// RFC 0005 Section on Type Unification. It supports:
///
/// 1. Type variable binding (TypeVar vs any type)
/// 2. Structural unification of compound types
/// 3. Error recovery (ErrorType unifies with everything)
///
/// Unification results (type variable bindings) are recorded in the
/// associated TypeEnv. Use TypeEnv::resolve() to obtain the final
/// substituted type after unification.
///
/// Unification is equality-only. Directional subtype and coercion relations
/// such as `never -> T`, `T -> any`, `&mut T -> &T`,
/// `*mut T -> *const T`, and `T -> T | E` are handled by the coercion
/// resolver, not by this engine.
///
/// Unification rules (RFC 0005):
/// 1.  TypeVar vs any type -> bind variable (with occurs check)
/// 2.  Primitive vs same Primitive -> succeed
/// 3.  Function vs Function -> unify params + return type
/// 4.  Tuple vs Tuple (same arity) -> unify elements pairwise
/// 5.  Object vs Object -> equal member set and unifiable member types
/// 6.  Array vs Array -> unify element types
/// 7.  Named vs Named (same symbol) -> succeed, unify type args
/// 8.  Union vs Union -> same alternative set, pairwise unification
/// 9.  Reference vs Reference -> same mutability, unify pointee
/// 10. RawPointer vs RawPointer -> same mutability, unify pointee
/// 11. Existential vs Existential -> compare interface types
/// 12. ErrorType vs any -> succeed (error recovery)
class UnificationEngine {
public:
  /// \brief Result of a unification attempt with diagnostic information.
  struct UnifyResult {
    enum class FailureKind {
      None,
      CannotUnify,
      InfiniteType,
    };

    bool success;         ///< True if unification succeeded
    zc::String errorMsg;  ///< Human-readable error message on failure
    FailureKind failureKind = FailureKind::None;

    /// Implicit conversion to bool for convenience.
    operator bool() const { return success; }
  };

  /// \brief Construct a unification engine using the given type environment.
  ///
  /// \param env The type environment where variable bindings are recorded.
  ///            The engine does NOT take ownership; env must outlive the engine.
  explicit UnificationEngine(TypeEnv& env);

  /// \brief Attempt to unify two types.
  ///
  /// Returns true if the types can be made equal through type variable
  /// substitution. On success, any type variable bindings are recorded in
  /// the associated TypeEnv.
  ///
  /// Note: Takes const references because unification modifies the TypeEnv's
  /// internal union-find state, not the type objects themselves.
  ///
  /// \param a First type to unify.
  /// \param b Second type to unify.
  /// \return true if unification succeeded.
  bool unify(const Type& a, const Type& b);

  /// \brief Attempt to unify two types with diagnostic output.
  ///
  /// Like unify(), but returns a detailed error message on failure.
  ///
  /// \param a First type to unify.
  /// \param b Second type to unify.
  /// \return UnifyResult indicating success or failure with error message.
  UnifyResult tryUnify(const Type& a, const Type& b);

  /// \brief Check if sub is a subtype of super.
  ///
  /// Performs subtype checking according to RFC 0005 rules.
  /// Resolves type variables before checking.
  ///
  /// \param sub The putative subtype.
  /// \param super The putative supertype.
  /// \return true if sub is a subtype of super.
  bool isSubtype(const Type& sub, const Type& super);

private:
  // Kind-specific unification helpers
  bool unifyPrimitives(const PrimitiveType& a, const PrimitiveType& b);
  bool unifyFunctions(const FunctionType& a, const FunctionType& b);
  bool unifyTuples(const TupleType& a, const TupleType& b);
  bool unifyObjects(const ObjectType& a, const ObjectType& b);
  bool unifyArrays(const ArrayType& a, const ArrayType& b);
  bool unifyNamed(const NamedType& a, const NamedType& b);
  bool unifyUnions(const UnionType& a, const UnionType& b);
  bool unifyReferences(const ReferenceType& a, const ReferenceType& b);
  bool unifyRawPointers(const RawPointerType& a, const RawPointerType& b);
  bool unifyExistentials(const ExistentialType& a, const ExistentialType& b);

  /// \brief Unify a type variable with another type.
  ///
  /// Performs occurs check and bound validation before binding.
  bool unifyVarWith(const TypeVar& var, const Type& other);

  /// \brief Build an error message for failed unification.
  zc::String buildError(const Type& a, const Type& b, const char* reason) const;

  TypeEnv& env_;
};

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
