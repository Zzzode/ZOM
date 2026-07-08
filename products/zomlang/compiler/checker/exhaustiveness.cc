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

#include "zomlang/compiler/checker/exhaustiveness.h"

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/type/named-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/tuple-type.h"
#include "zomlang/compiler/type/union-type.h"

namespace zomlang {
namespace compiler {
namespace checker {

using namespace zomlang::compiler::type;
using namespace zomlang::compiler::ast;
using namespace zomlang::compiler::diagnostics;

// ============================================================================
// Constructor factory methods
// ============================================================================

Constructor Constructor::makeBoolTrue() { return Constructor{Kind::BoolTrue, "true"_zc, 0, {}}; }

Constructor Constructor::makeBoolFalse() { return Constructor{Kind::BoolFalse, "false"_zc, 0, {}}; }

Constructor Constructor::makeUnit() { return Constructor{Kind::Unit, "()"_zc, 0, {}}; }

Constructor Constructor::makeNull() { return Constructor{Kind::Null, "null"_zc, 0, {}}; }

Constructor Constructor::makeOpen(zc::StringPtr typeName) {
  return Constructor{Kind::Open, typeName, 0, {}};
}

bool Constructor::operator==(const Constructor& other) const {
  if (kind != other.kind) return false;
  if (name != other.name) return false;
  if (arity != other.arity) return false;
  return true;
}

// ============================================================================
// Impl
// ============================================================================

struct ExhaustivenessChecker::Impl {
  TypeEnv& typeEnv;
  const Tree& tree;
  DiagnosticEngine& diags;

  // Cache for constructors of the current scrutinee type, used during
  // a single checkMatchExhaustiveness call.
  zc::Vector<Constructor> scrutineeConstructors;

  // The scrutinee type for the current check (used by specialize internally).
  zc::Maybe<const Type&> currentScrutineeType;

  Impl(TypeEnv& te, const Tree& t, DiagnosticEngine& d) : typeEnv(te), tree(t), diags(d) {}
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

ExhaustivenessChecker::ExhaustivenessChecker(TypeEnv& typeEnv, const Tree& tree,
                                             DiagnosticEngine& diags) noexcept
    : impl(zc::heap<Impl>(typeEnv, tree, diags)) {}

ExhaustivenessChecker::~ExhaustivenessChecker() noexcept(false) = default;

// ============================================================================
// Helpers
// ============================================================================

static source::SourceLoc getNodeLoc(const Tree& tree, NodeId id) {
  return tree.node(id).range.getStart();
}

bool ExhaustivenessChecker::isOpenType(const Type& ty) {
  const Type& resolved = impl->typeEnv.resolve(ty);

  if (isPrimitive(resolved)) {
    const auto& prim = static_cast<const PrimitiveType&>(resolved);
    auto kind = prim.getPrimitiveKind();
    switch (kind) {
      case PrimitiveKind::I8:
      case PrimitiveKind::I16:
      case PrimitiveKind::I32:
      case PrimitiveKind::I64:
      case PrimitiveKind::U8:
      case PrimitiveKind::U16:
      case PrimitiveKind::U32:
      case PrimitiveKind::U64:
      case PrimitiveKind::F32:
      case PrimitiveKind::F64:
      case PrimitiveKind::Str:
      case PrimitiveKind::Char:
      case PrimitiveKind::Any:
        return true;
      default:
        return false;
    }
  }
  // Named types: we determine "openness" by looking at whether we can
  // enumerate all constructors. Enums are closed; other named types are open
  // (we can't list all possible subclasses/instances).
  // We detect this in getConstructors by searching the AST for enum decls.
  return false;
}

// ============================================================================
// Pattern classification
// ============================================================================

bool ExhaustivenessChecker::isWildcardPattern(NodeId patId) const {
  if (!impl->tree.contains(patId)) return true;  // Invalid = wildcard

  const auto& node = impl->tree.node(patId);
  switch (node.kind) {
    case SyntaxKind::WildcardPattern:
      return true;
    case SyntaxKind::BindingPattern:
      // A binding pattern (e.g., `x` or `mut x`) always matches - it's
      // effectively a wildcard that also binds the value.
      return true;
    case SyntaxKind::IdentifierPattern: {
      // Bare identifier patterns are treated as bindings (wildcards).
      // Explicit enum variant patterns use EnumPattern node kind.
      return true;
    }
    default:
      return false;
  }
}

// Extract the literal node from a LiteralPattern.
// The LiteralPattern stores the literal's NodeId in kLiteralPatternLiteralWord.
static const Node* getLiteralPatternLit(const Tree& tree, const Node& litPatNode) {
  auto litNodeId = NodeId(litPatNode.payload.words[kLiteralPatternLiteralWord]);
  if (!tree.contains(litNodeId)) return nullptr;
  return &tree.node(litNodeId);
}

// Extract the last segment name from a ModulePath node.
static zc::StringPtr getPathLastName(const Tree& tree, NodeId pathId) {
  if (!tree.contains(pathId)) return ""_zc;
  const auto& pathNode = tree.node(pathId);

  if (pathNode.kind == SyntaxKind::ModulePath) {
    IdentList segments;
    segments.first = pathNode.payload.words[kModulePathSegmentsFirstWord];
    segments.size = pathNode.payload.words[kModulePathSegmentsSizeWord];

    auto segIds = tree.identList(segments);
    if (segIds.size() == 0) return ""_zc;
    return tree.ident(segIds.back());
  }

  // Also handle IdentExpr (used by makeNamedTypeExpr for simple type names like "i32")
  if (pathNode.kind == SyntaxKind::IdentExpr) {
    auto identId = IdentId(pathNode.payload.words[kIdentExprNameWord]);
    return tree.ident(identId);
  }

  return ""_zc;
}

zc::Maybe<const Constructor&> ExhaustivenessChecker::getPatternConstructor(
    NodeId patId, const Type& scrutineeType) {
  if (!impl->tree.contains(patId)) return zc::none;

  const auto& node = impl->tree.node(patId);

  // Cache the scrutinee constructors
  if (impl->scrutineeConstructors.empty()) {
    impl->scrutineeConstructors = getConstructors(scrutineeType);
  }

  switch (node.kind) {
    case SyntaxKind::LiteralPattern: {
      const auto* litNode = getLiteralPatternLit(impl->tree, node);
      if (!litNode) return zc::none;

      if (litNode->kind == SyntaxKind::BoolLiteral) {
        bool isTrue = litNode->payload.words[kBoolLiteralValueWord] != 0;
        for (const auto& ctor : impl->scrutineeConstructors) {
          if (isTrue && ctor.kind == Constructor::Kind::BoolTrue) { return ctor; }
          if (!isTrue && ctor.kind == Constructor::Kind::BoolFalse) { return ctor; }
        }
      } else if (litNode->kind == SyntaxKind::NullLiteral) {
        for (const auto& ctor : impl->scrutineeConstructors) {
          if (ctor.kind == Constructor::Kind::Null) { return ctor; }
        }
      } else if (litNode->kind == SyntaxKind::UnitLiteral) {
        for (const auto& ctor : impl->scrutineeConstructors) {
          if (ctor.kind == Constructor::Kind::Unit) { return ctor; }
        }
      }
      // For non-bool/non-null/non-unit literals (int, float, str, char), return zc::none
      // to indicate this is a constructor pattern for an open type.
      return zc::none;
    }

    case SyntaxKind::EnumPattern: {
      auto pathId = NodeId(node.payload.words[kEnumPatternPathWord]);
      auto variantName = getPathLastName(impl->tree, pathId);

      if (variantName.size() > 0) {
        for (const auto& ctor : impl->scrutineeConstructors) {
          if (ctor.kind == Constructor::Kind::EnumVariant && ctor.name == variantName) {
            return ctor;
          }
        }
      }
      return zc::none;
    }

    case SyntaxKind::IsPattern: {
      // `is Type` pattern - matches a specific union branch
      if (!isUnion(scrutineeType)) return zc::none;

      auto tyId = NodeId(node.payload.words[kIsPatternTyWord]);
      if (!impl->tree.contains(tyId)) return zc::none;

      const auto& tyNode = impl->tree.node(tyId);
      zc::StringPtr typeName;

      if (tyNode.kind == SyntaxKind::NamedTypeExpr) {
        auto typePathId = NodeId(tyNode.payload.words[kNamedTypeExprPathWord]);
        typeName = getPathLastName(impl->tree, typePathId);
      } else if (tyNode.kind == SyntaxKind::PredefinedTypeExpr) {
        auto primKind =
            static_cast<PrimitiveKind>(tyNode.payload.words[kPredefinedTypeExprKindWord]);
        switch (primKind) {
          case PrimitiveKind::I32:
            typeName = "i32"_zc;
            break;
          case PrimitiveKind::I64:
            typeName = "i64"_zc;
            break;
          case PrimitiveKind::Bool:
            typeName = "bool"_zc;
            break;
          case PrimitiveKind::Str:
            typeName = "str"_zc;
            break;
          case PrimitiveKind::F64:
            typeName = "f64"_zc;
            break;
          default:
            break;
        }
      }

      if (typeName.size() > 0) {
        for (const auto& ctor : impl->scrutineeConstructors) {
          if (ctor.kind == Constructor::Kind::UnionBranch && ctor.name == typeName) { return ctor; }
        }
      }
      return zc::none;
    }

    case SyntaxKind::TuplePattern: {
      // Tuple pattern matches the tuple constructor
      for (const auto& ctor : impl->scrutineeConstructors) {
        if (ctor.kind == Constructor::Kind::Unit) {
          // Tuple constructor is stored as Kind::Unit (synthetic)
          return ctor;
        }
      }
      return zc::none;
    }

    default:
      return zc::none;
  }
}

bool ExhaustivenessChecker::patternMatchesConstructor(NodeId patId, const Constructor& ctor,
                                                      const Type& scrutineeType) {
  if (isWildcardPattern(patId)) return true;

  ZC_IF_SOME(patCtor, getPatternConstructor(patId, scrutineeType)) { return patCtor == ctor; }
  else {
    // Pattern is for an open type (e.g., integer literal).
    // It only matches the Open constructor sentinel.
    return ctor.kind == Constructor::Kind::Open;
  }
}

PatternRow ExhaustivenessChecker::extractSubPatterns(NodeId patId, size_t arity) {
  PatternRow result;

  if (isWildcardPattern(patId)) {
    for (size_t i = 0; i < arity; ++i) {
      result.add(NodeId());  // Invalid NodeId = wildcard placeholder
    }
    return result;
  }

  if (!impl->tree.contains(patId)) {
    for (size_t i = 0; i < arity; ++i) { result.add(NodeId()); }
    return result;
  }

  const auto& node = impl->tree.node(patId);

  if (node.kind == SyntaxKind::EnumPattern) {
    NodeList args;
    args.first = node.payload.words[kEnumPatternArgsFirstWord];
    args.size = node.payload.words[kEnumPatternArgsSizeWord];

    if (args.size > 0) {
      for (NodeId argPat : impl->tree.list(args)) { result.add(argPat); }
    }
    while (result.size() < arity) { result.add(NodeId()); }
    return result;
  }

  if (node.kind == SyntaxKind::TuplePattern) {
    NodeList pats;
    pats.first = node.payload.words[kTuplePatternPatsFirstWord];
    pats.size = node.payload.words[kTuplePatternPatsSizeWord];

    if (pats.size > 0) {
      for (NodeId subPat : impl->tree.list(pats)) { result.add(subPat); }
    }
    while (result.size() < arity) { result.add(NodeId()); }
    return result;
  }

  // For other constructor patterns (literal, is-pattern), arity is 0
  for (size_t i = 0; i < arity; ++i) { result.add(NodeId()); }
  return result;
}

PatternRow ExhaustivenessChecker::buildPatternRow(NodeId patId) {
  PatternRow row;
  row.add(patId);
  return row;
}

// ============================================================================
// Constructor detection
// ============================================================================

zc::Vector<Constructor> ExhaustivenessChecker::getConstructors(const Type& ty) {
  zc::Vector<Constructor> result;

  const Type& resolved = impl->typeEnv.resolve(ty);

  if (isPrimitive(resolved)) {
    const auto& prim = static_cast<const PrimitiveType&>(resolved);
    switch (prim.getPrimitiveKind()) {
      case PrimitiveKind::Bool:
        result.add(Constructor::makeBoolTrue());
        result.add(Constructor::makeBoolFalse());
        return result;

      case PrimitiveKind::Unit:
        result.add(Constructor::makeUnit());
        return result;

      case PrimitiveKind::Null:
        result.add(Constructor::makeNull());
        return result;

      case PrimitiveKind::Never:
        return result;

      default:
        result.add(Constructor::makeOpen(prim.getName()));
        return result;
    }
  }

  if (isUnion(resolved)) {
    const auto& unionTy = static_cast<const UnionType&>(resolved);
    for (size_t i = 0; i < unionTy.getAlternativeCount(); ++i) {
      const auto& alt = unionTy.getAlternative(i);
      Constructor ctor;
      ctor.kind = Constructor::Kind::UnionBranch;
      ctor.arity = 0;
      if (isNamed(alt)) {
        ctor.name = static_cast<const NamedType&>(alt).getName();
      } else if (isPrimitive(alt)) {
        ctor.name = static_cast<const PrimitiveType&>(alt).getName();
      } else {
        ctor.name = "?"_zc;
      }
      ctor.fieldType = alt;
      result.add(zc::mv(ctor));
    }
    return result;
  }

  if (isTuple(resolved)) {
    // Tuple type has one constructor with arity = element count
    const auto& tupleTy = static_cast<const TupleType&>(resolved);
    Constructor ctor;
    ctor.kind = Constructor::Kind::Unit;  // Synthetic: use Unit kind for tuple ctor
    ctor.name = "(...)"_zc;
    ctor.arity = tupleTy.getElementCount();
    result.add(zc::mv(ctor));
    return result;
  }

  if (isNamed(resolved)) {
    const auto& named = static_cast<const NamedType&>(resolved);

    // Search the AST tree for a matching enum declaration.
    // If found, extract its variants as constructors.
    bool foundEnum = false;
    visitTreePreOrder(impl->tree, impl->tree.root(), [&](NodeId id, const Node& node) {
      if (foundEnum) return;
      if (node.kind != SyntaxKind::EnumDeclaration) return;

      auto enumName = impl->tree.ident(IdentId(node.payload.words[kEnumDeclarationNameWord]));
      if (enumName != named.getName()) return;

      // Found the matching enum declaration. Extract variants.
      auto variantsId = NodeId(node.payload.words[kEnumDeclarationVariantsIdWord]);
      if (!impl->tree.contains(variantsId)) return;

      const auto& varList = impl->tree.node(variantsId);
      if (varList.kind != SyntaxKind::EnumVariantList) return;

      NodeList varNodeList;
      varNodeList.first = varList.payload.words[kEnumVariantListVariantsFirstWord];
      varNodeList.size = varList.payload.words[kEnumVariantListVariantsSizeWord];

      for (NodeId varId : impl->tree.list(varNodeList)) {
        if (!impl->tree.contains(varId)) continue;
        const auto& varNode = impl->tree.node(varId);

        Constructor ctor;
        ctor.kind = Constructor::Kind::EnumVariant;

        switch (varNode.kind) {
          case SyntaxKind::UnitVariant:
            ctor.name = impl->tree.ident(IdentId(varNode.payload.words[kUnitVariantNameWord]));
            ctor.arity = 0;
            break;

          case SyntaxKind::TupleVariant:
            ctor.name = impl->tree.ident(IdentId(varNode.payload.words[kTupleVariantNameWord]));
            ctor.arity = varNode.payload.words[kTupleVariantNfieldsWord];
            break;

          case SyntaxKind::StructVariant:
            ctor.name = impl->tree.ident(IdentId(varNode.payload.words[kStructVariantNameWord]));
            ctor.arity = varNode.payload.words[kStructVariantNfieldsWord];
            break;

          default:
            continue;
        }

        result.add(zc::mv(ctor));
      }
      foundEnum = true;
    });

    if (foundEnum) return result;

    // Non-enum named type - treat as open
    result.add(Constructor::makeOpen(named.getName()));
    return result;
  }

  if (isNever(resolved)) { return result; }

  // Fallback: treat as open type
  result.add(Constructor::makeOpen("?"_zc));
  return result;
}

bool ExhaustivenessChecker::isComplete(const zc::Vector<Constructor>& seen, const Type& ty) {
  const Type& resolved = impl->typeEnv.resolve(ty);

  if (isNever(resolved)) { return true; }

  auto allCtors = getConstructors(resolved);

  if (allCtors.empty()) {
    return true;  // No constructors (e.g., Never) - complete
  }

  // If the type has an Open constructor, it can never be complete
  for (const auto& ctor : allCtors) {
    if (ctor.kind == Constructor::Kind::Open) { return false; }
  }

  // Check that every constructor in allCtors is present in seen
  for (const auto& required : allCtors) {
    bool found = false;
    for (const auto& s : seen) {
      if (s == required) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }

  return true;
}

// ============================================================================
// Matrix specialization
// ============================================================================

PatternMatrix ExhaustivenessChecker::specialize(const PatternMatrix& matrix,
                                                const Constructor& ctor) {
  PatternMatrix result;

  for (const auto& row : matrix) {
    if (row.empty()) continue;

    NodeId firstPat = row[0];
    bool matches = false;

    if (isWildcardPattern(firstPat)) {
      matches = true;
    } else if (impl->tree.contains(firstPat)) {
      const auto& patNode = impl->tree.node(firstPat);

      switch (ctor.kind) {
        case Constructor::Kind::BoolTrue:
        case Constructor::Kind::BoolFalse:
          if (patNode.kind == SyntaxKind::LiteralPattern) {
            const auto* litNode = getLiteralPatternLit(impl->tree, patNode);
            if (litNode && litNode->kind == SyntaxKind::BoolLiteral) {
              bool isTrue = litNode->payload.words[kBoolLiteralValueWord] != 0;
              matches = (isTrue && ctor.kind == Constructor::Kind::BoolTrue) ||
                        (!isTrue && ctor.kind == Constructor::Kind::BoolFalse);
            }
          }
          break;

        case Constructor::Kind::EnumVariant:
          if (patNode.kind == SyntaxKind::EnumPattern) {
            auto pathId = NodeId(patNode.payload.words[kEnumPatternPathWord]);
            auto variantName = getPathLastName(impl->tree, pathId);
            matches = (variantName == ctor.name);
          }
          break;

        case Constructor::Kind::UnionBranch:
          if (patNode.kind == SyntaxKind::IsPattern) {
            auto tyId = NodeId(patNode.payload.words[kIsPatternTyWord]);
            if (impl->tree.contains(tyId)) {
              const auto& tyNode = impl->tree.node(tyId);
              zc::StringPtr typeName;
              if (tyNode.kind == SyntaxKind::NamedTypeExpr) {
                auto typePathId = NodeId(tyNode.payload.words[kNamedTypeExprPathWord]);
                typeName = getPathLastName(impl->tree, typePathId);
              } else if (tyNode.kind == SyntaxKind::PredefinedTypeExpr) {
                auto pk =
                    static_cast<PrimitiveKind>(tyNode.payload.words[kPredefinedTypeExprKindWord]);
                switch (pk) {
                  case PrimitiveKind::I32:
                    typeName = "i32"_zc;
                    break;
                  case PrimitiveKind::I64:
                    typeName = "i64"_zc;
                    break;
                  case PrimitiveKind::Bool:
                    typeName = "bool"_zc;
                    break;
                  case PrimitiveKind::Str:
                    typeName = "str"_zc;
                    break;
                  case PrimitiveKind::F64:
                    typeName = "f64"_zc;
                    break;
                  default:
                    break;
                }
              }
              matches = (typeName == ctor.name);
            }
          }
          break;

        case Constructor::Kind::Unit:
          if (patNode.kind == SyntaxKind::TuplePattern) {
            matches = true;
          } else if (patNode.kind == SyntaxKind::LiteralPattern) {
            const auto* litNode = getLiteralPatternLit(impl->tree, patNode);
            if (litNode && litNode->kind == SyntaxKind::UnitLiteral) { matches = true; }
          }
          break;

        case Constructor::Kind::Null:
          if (patNode.kind == SyntaxKind::LiteralPattern) {
            const auto* litNode = getLiteralPatternLit(impl->tree, patNode);
            if (litNode) { matches = (litNode->kind == SyntaxKind::NullLiteral); }
          }
          break;

        case Constructor::Kind::Open:
          if (patNode.kind == SyntaxKind::ExpressionPattern) {
            matches = true;
          } else if (patNode.kind == SyntaxKind::LiteralPattern) {
            const auto* litNode = getLiteralPatternLit(impl->tree, patNode);
            if (litNode) {
              // Open constructor matches only non-boolean, non-null, non-unit
              // literals (int, float, str, char, etc.)
              if (litNode->kind != SyntaxKind::BoolLiteral &&
                  litNode->kind != SyntaxKind::NullLiteral &&
                  litNode->kind != SyntaxKind::UnitLiteral) {
                matches = true;
              }
            }
          }
          break;

        case Constructor::Kind::SealedSubclass:
          break;
      }
    }

    if (matches) {
      PatternRow subPats = extractSubPatterns(firstPat, ctor.arity);
      PatternRow newRow;
      for (auto subPat : subPats) { newRow.add(subPat); }
      for (size_t i = 1; i < row.size(); ++i) { newRow.add(row[i]); }
      result.add(zc::mv(newRow));
    }
  }

  return result;
}

PatternMatrix ExhaustivenessChecker::defaultMatrix(const PatternMatrix& matrix) {
  PatternMatrix result;

  for (const auto& row : matrix) {
    if (row.empty()) continue;

    NodeId firstPat = row[0];
    if (isWildcardPattern(firstPat)) {
      PatternRow newRow;
      for (size_t i = 1; i < row.size(); ++i) { newRow.add(row[i]); }
      result.add(zc::mv(newRow));
    }
  }

  return result;
}

// ============================================================================
// Usefulness algorithm
// ============================================================================

bool ExhaustivenessChecker::isUseful(const PatternMatrix& matrix, const PatternRow& newRow,
                                     const Type& scrutineeType) {
  // Base case: empty matrix means newRow is definitely useful
  if (matrix.empty()) return true;

  // If newRow is empty, it was only useful if matrix is empty
  if (newRow.empty()) return false;

  // If matrix has any empty rows, they match everything
  for (const auto& row : matrix) {
    if (row.empty()) return false;
  }

  NodeId firstPat = newRow[0];

  if (isWildcardPattern(firstPat)) {
    // Collect constructors seen in the first column of the matrix
    zc::Vector<Constructor> seenCtors;
    bool hasWildcardInMatrix = false;

    for (const auto& row : matrix) {
      if (row.empty()) continue;
      NodeId matFirstPat = row[0];

      if (isWildcardPattern(matFirstPat)) {
        hasWildcardInMatrix = true;
      } else {
        ZC_IF_SOME(ctor, getPatternConstructor(matFirstPat, scrutineeType)) {
          bool alreadySeen = false;
          for (const auto& s : seenCtors) {
            if (s == ctor) {
              alreadySeen = true;
              break;
            }
          }
          if (!alreadySeen) { seenCtors.add(ctor); }
        }
        else {
          // Pattern for an open type
          bool openSeen = false;
          for (const auto& s : seenCtors) {
            if (s.kind == Constructor::Kind::Open) {
              openSeen = true;
              break;
            }
          }
          if (!openSeen) { seenCtors.add(Constructor::makeOpen("_"_zc)); }
        }
      }
    }

    if (hasWildcardInMatrix) {
      // There's already a wildcard. Check if the default projection is useful.
      auto defMat = defaultMatrix(matrix);
      PatternRow restRow;
      for (size_t i = 1; i < newRow.size(); ++i) { restRow.add(newRow[i]); }
      return isUseful(defMat, restRow, scrutineeType);
    }

    // No wildcard in matrix. Check completeness.
    if (!isComplete(seenCtors, scrutineeType)) {
      return true;  // Some constructor not covered
    }

    // All constructors covered. Check each specialization.
    auto allCtors = getConstructors(scrutineeType);
    impl->currentScrutineeType = scrutineeType;

    for (const auto& ctor : allCtors) {
      if (ctor.kind == Constructor::Kind::Open) continue;

      auto specMat = specialize(matrix, ctor);
      PatternRow specRow;
      for (size_t i = 0; i < ctor.arity; ++i) {
        specRow.add(NodeId());  // Wildcard placeholder
      }
      for (size_t i = 1; i < newRow.size(); ++i) { specRow.add(newRow[i]); }

      ZC_IF_SOME(ft, ctor.fieldType) {
        if (isUseful(specMat, specRow, ft)) { return true; }
      }
      else {
        if (isUseful(specMat, specRow, scrutineeType)) { return true; }
      }
    }

    return false;

  } else {
    // First pattern is a constructor pattern (not wildcard).
    ZC_IF_SOME(ctor, getPatternConstructor(firstPat, scrutineeType)) {
      // Constructor pattern: specialize and check
      impl->currentScrutineeType = scrutineeType;
      auto specMat = specialize(matrix, ctor);
      PatternRow specRow = extractSubPatterns(firstPat, ctor.arity);
      for (size_t i = 1; i < newRow.size(); ++i) { specRow.add(newRow[i]); }

      ZC_IF_SOME(ft, ctor.fieldType) { return isUseful(specMat, specRow, ft); }
      return isUseful(specMat, specRow, scrutineeType);
    }
    else {
      // Open type literal pattern. Useful if there's no wildcard.
      for (const auto& row : matrix) {
        if (!row.empty() && isWildcardPattern(row[0])) { return false; }
      }
      return true;
    }
  }
}

// ============================================================================
// Missing pattern computation
// ============================================================================

zc::String ExhaustivenessChecker::describeMissingConstructor(const Constructor& ctor) {
  switch (ctor.kind) {
    case Constructor::Kind::BoolTrue:
      return zc::str("true"_zc);
    case Constructor::Kind::BoolFalse:
      return zc::str("false"_zc);
    case Constructor::Kind::Unit:
      return zc::str("()"_zc);
    case Constructor::Kind::Null:
      return zc::str("null"_zc);
    case Constructor::Kind::EnumVariant:
      if (ctor.arity > 0) { return zc::str(ctor.name, "(..)"_zc); }
      return zc::str(ctor.name);
    case Constructor::Kind::UnionBranch:
      return zc::str("is "_zc, ctor.name);
    case Constructor::Kind::SealedSubclass:
      return zc::str(ctor.name);
    case Constructor::Kind::Open:
      return zc::str("_"_zc);
  }
  return zc::str("_"_zc);
}

zc::Vector<zc::String> ExhaustivenessChecker::computeMissingPatterns(const PatternMatrix& matrix,
                                                                     const Type& scrutineeType) {
  zc::Vector<zc::String> missing;

  auto ctors = getConstructors(scrutineeType);

  for (const auto& ctor : ctors) {
    if (ctor.kind == Constructor::Kind::Open) {
      PatternRow wildcardRow;
      wildcardRow.add(NodeId());
      if (isUseful(matrix, wildcardRow, scrutineeType)) { missing.add(zc::str("_"_zc)); }
      continue;
    }

    bool covered = false;

    // Check if there's a wildcard in the first column
    for (const auto& row : matrix) {
      if (!row.empty() && isWildcardPattern(row[0])) {
        covered = true;
        break;
      }
    }

    if (!covered) {
      // Check if any row's first pattern matches this constructor
      for (const auto& row : matrix) {
        if (row.empty()) continue;
        if (patternMatchesConstructor(row[0], ctor, scrutineeType)) {
          covered = true;
          break;
        }
      }
    }

    if (!covered) { missing.add(describeMissingConstructor(ctor)); }
  }

  // If no specific missing constructors found but match is still non-exhaustive
  if (missing.empty()) {
    PatternRow wildcardRow;
    wildcardRow.add(NodeId());
    if (isUseful(matrix, wildcardRow, scrutineeType)) { missing.add(zc::str("_"_zc)); }
  }

  return missing;
}

// ============================================================================
// Main entry point
// ============================================================================

void ExhaustivenessChecker::checkMatchExhaustiveness(NodeId matchStmt, const Type& scrutineeType) {
  const auto& matchNode = impl->tree.node(matchStmt);

  // Reset caches
  impl->scrutineeConstructors.clear();
  impl->currentScrutineeType = scrutineeType;
  impl->scrutineeConstructors = getConstructors(scrutineeType);

  // Extract match arms
  NodeList arms;
  arms.first = matchNode.payload.words[kMatchStmtArmsFirstWord];
  arms.size = matchNode.payload.words[kMatchStmtArmsSizeWord];

  auto matchLoc = getNodeLoc(impl->tree, matchStmt);

  if (arms.size == 0) {
    if (!isNever(scrutineeType)) {
      auto missing = computeMissingPatterns({}, scrutineeType);
      zc::String missingStr;
      if (!missing.empty()) {
        missingStr = zc::str(missing[0].asPtr());
        for (size_t i = 1; i < missing.size(); ++i) {
          missingStr = zc::str(missingStr.asPtr(), ", "_zc, missing[i].asPtr());
        }
      } else {
        missingStr = zc::str("_"_zc);
      }
      impl->diags.diagnose<DiagID::CheckerNonExhaustiveMatch>(matchLoc, missingStr.asPtr());
    }
    return;
  }

  // Build two matrices: guarded arms participate in reachability checks, but
  // only unguarded arms prove exhaustiveness.
  PatternMatrix reachabilityMatrix;
  PatternMatrix coverageMatrix;
  bool foundWildcard = false;

  for (NodeId armId : impl->tree.list(arms)) {
    const auto& armNode = impl->tree.node(armId);
    if (armNode.kind != SyntaxKind::MatchArmStmt) continue;

    NodeId patId = NodeId(armNode.payload.words[kMatchArmStmtPatternWord]);
    NodeId guardId = NodeId(armNode.payload.words[kMatchArmStmtGuardWord]);
    bool hasGuard = impl->tree.contains(guardId);

    PatternRow row = buildPatternRow(patId);

    // Check if this arm is unreachable against all prior arms. A previous
    // unguarded wildcard makes every later arm unreachable.
    if (!reachabilityMatrix.empty()) {
      if (!isUseful(reachabilityMatrix, row, scrutineeType)) {
        auto armLoc = getNodeLoc(impl->tree, armId);
        impl->diags.diagnose<DiagID::CheckerUnreachableMatchArm>(armLoc);
      }
    }

    reachabilityMatrix.add(zc::mv(row));
    if (!hasGuard) { coverageMatrix.add(buildPatternRow(patId)); }

    if (isWildcardPattern(patId) && !hasGuard) { foundWildcard = true; }
  }

  // Check exhaustiveness
  const Type& resolvedScrutinee = impl->typeEnv.resolve(scrutineeType);
  if (isNever(resolvedScrutinee)) { return; }

  // Quick check: unguarded wildcard makes it exhaustive
  if (foundWildcard) { return; }

  // Full usefulness check
  PatternRow wildcardTest;
  wildcardTest.add(NodeId());

  bool useful = isUseful(coverageMatrix, wildcardTest, scrutineeType);
  if (useful) {
    auto missing = computeMissingPatterns(coverageMatrix, scrutineeType);

    zc::String missingStr;
    if (!missing.empty()) {
      missingStr = zc::str(missing[0].asPtr());
      for (size_t i = 1; i < missing.size(); ++i) {
        missingStr = zc::str(missingStr.asPtr(), ", "_zc, missing[i].asPtr());
      }
    } else {
      missingStr = zc::str("_"_zc);
    }

    impl->diags.diagnose<DiagID::CheckerNonExhaustiveMatch>(matchLoc, missingStr.asPtr());
  }
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
