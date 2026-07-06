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

#include "zomlang/compiler/checker/decl-signature.h"

#include "zc/core/common.h"
#include "zc/core/map.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/checker/query-cycle-detector.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/compiler/symbol/symbol.h"
#include "zomlang/compiler/symbol/type-symbol.h"
#include "zomlang/compiler/symbol/value-symbol.h"
#include "zomlang/compiler/type/array-type.h"
#include "zomlang/compiler/type/associated-type.h"
#include "zomlang/compiler/type/error-type.h"
#include "zomlang/compiler/type/existential-type.h"
#include "zomlang/compiler/type/function-type.h"
#include "zomlang/compiler/type/interface-type.h"
#include "zomlang/compiler/type/intersection-type.h"
#include "zomlang/compiler/type/named-type.h"
#include "zomlang/compiler/type/object-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/raw-pointer-type.h"
#include "zomlang/compiler/type/reference-type.h"
#include "zomlang/compiler/type/tuple-type.h"
#include "zomlang/compiler/type/type-env.h"
#include "zomlang/compiler/type/type-var.h"
#include "zomlang/compiler/type/type.h"
#include "zomlang/compiler/type/union-type.h"

namespace zomlang {
namespace compiler {
namespace checker {

using namespace zomlang::compiler::type;
using namespace zomlang::compiler::symbol;
using namespace zomlang::compiler::ast;
using namespace zomlang::compiler::diagnostics;

// ============================================================================
// Impl
// ============================================================================

struct DeclSignatureComputer::Impl {
  type::TypeEnv& typeEnv;
  symbol::SymbolTable& symbols;
  const ast::Tree& tree;
  const ast::BindingMetadata& metadata;
  diagnostics::DiagnosticEngine& diags;
  QueryCycleDetector cycles;
  zc::HashMap<zc::StringPtr, const type::TypeVar*> genericTypeVars;  // non-owning
  bool hadErrors = false;

  Impl(type::TypeEnv& te, symbol::SymbolTable& sym, const ast::Tree& t,
       const ast::BindingMetadata& meta, diagnostics::DiagnosticEngine& d)
      : typeEnv(te), symbols(sym), tree(t), metadata(meta), diags(d) {}
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

DeclSignatureComputer::DeclSignatureComputer(type::TypeEnv& typeEnv, symbol::SymbolTable& symbols,
                                             const ast::Tree& tree,
                                             const ast::BindingMetadata& metadata,
                                             diagnostics::DiagnosticEngine& diags) noexcept
    : impl(zc::heap<Impl>(typeEnv, symbols, tree, metadata, diags)) {}

DeclSignatureComputer::~DeclSignatureComputer() noexcept(false) = default;

// ============================================================================
// Helpers
// ============================================================================

static source::SourceLoc nodeLoc(const ast::Tree& tree, ast::NodeId id) {
  return tree.node(id).range.getStart();
}

static uint32_t querySymbolId(const symbol::Symbol& symbol) {
  return static_cast<uint32_t>(symbol.getId().getRaw());
}

/// \brief Clone a type, creating a new owned copy.
///
/// Used when we need to store the same type in multiple places (e.g., both
/// in a FunctionType's param list and in TypeEnv keyed by parameter node).
static zc::Own<type::Type> cloneType(const type::Type& ty) {
  using namespace type;

  if (ty.isError()) { return zc::heap<ErrorType>(); }
  if (ty.isPrimitive()) {
    auto& prim = static_cast<const PrimitiveType&>(ty);
    return zc::heap<PrimitiveType>(prim.getPrimitiveKind());
  }
  if (ty.isNamed()) {
    auto& named = static_cast<const NamedType&>(ty);
    return zc::heap<NamedType>(named.getName());
  }
  if (ty.isObject()) {
    auto& object = static_cast<const ObjectType&>(ty);
    auto result = zc::heap<ObjectType>();
    auto members = object.getMembers();
    for (size_t i = 0; i < members.size(); ++i) {
      ZC_IF_SOME(memberType, members[i].type) {
        result->addMember(members[i].name, cloneType(memberType));
      }
    }
    return zc::mv(result);
  }
  if (ty.isFunction()) {
    auto& fn = static_cast<const FunctionType&>(ty);
    zc::Vector<zc::Own<Type>> params;
    for (size_t i = 0; i < fn.getParamCount(); ++i) { params.add(cloneType(fn.getParamType(i))); }
    auto ret = cloneType(fn.getReturnType());
    auto result = zc::heap<FunctionType>(zc::mv(params), zc::mv(ret));
    result->setVariadic(fn.isVariadic());
    for (size_t i = 0; i < fn.getGenericParamCount(); ++i) {
      auto& generic = fn.getGenericParam(i);
      auto clone = zc::heap<GenericParam>(generic.name);
      for (size_t j = 0; j < generic.upperBounds.size(); ++j) {
        clone->upperBounds.add(cloneType(*generic.upperBounds[j]));
      }
      for (size_t j = 0; j < generic.lowerBounds.size(); ++j) {
        clone->lowerBounds.add(cloneType(*generic.lowerBounds[j]));
      }
      result->addGenericParam(zc::mv(clone));
    }
    auto raises = fn.getRaisesType();
    ZC_IF_SOME(r, raises) { result->setRaisesType(cloneType(r)); }
    return zc::mv(result);
  }
  if (ty.isReference()) {
    auto& ref = static_cast<const ReferenceType&>(ty);
    auto pointee = cloneType(ref.getPointeeType());
    return zc::heap<ReferenceType>(zc::mv(pointee), ref.getMutability());
  }
  if (ty.isRawPointer()) {
    auto& ptr = static_cast<const RawPointerType&>(ty);
    auto pointee = cloneType(ptr.getPointeeType());
    return zc::heap<RawPointerType>(zc::mv(pointee), ptr.getMutability());
  }
  if (ty.isArray()) {
    auto& arr = static_cast<const ArrayType&>(ty);
    auto elem = cloneType(arr.getElementType());
    return zc::heap<ArrayType>(zc::mv(elem));
  }
  if (ty.isTuple()) {
    auto& tup = static_cast<const TupleType&>(ty);
    zc::Vector<zc::Own<Type>> elems;
    for (size_t i = 0; i < tup.getElementCount(); ++i) {
      elems.add(cloneType(tup.getElementType(i)));
    }
    return zc::heap<TupleType>(zc::mv(elems));
  }
  if (ty.isUnion()) {
    auto& unionTy = static_cast<const UnionType&>(ty);
    zc::Vector<zc::Own<Type>> alts;
    for (size_t i = 0; i < unionTy.getAlternativeCount(); ++i) {
      alts.add(cloneType(unionTy.getAlternative(i)));
    }
    return zc::heap<UnionType>(zc::mv(alts));
  }
  if (ty.isTypeVar()) {
    auto& tv = static_cast<const TypeVar&>(ty);
    auto result = zc::heap<TypeVar>(tv.getName(), tv.getId());
    for (size_t i = 0; i < tv.getUpperBoundCount(); ++i) {
      result->addUpperBound(cloneType(tv.getUpperBound(i)));
    }
    for (size_t i = 0; i < tv.getLowerBoundCount(); ++i) {
      result->addLowerBound(cloneType(tv.getLowerBound(i)));
    }
    return zc::mv(result);
  }
  if (ty.isInterface()) {
    auto& iface = static_cast<const InterfaceType&>(ty);
    auto result = zc::heap<InterfaceType>(iface.getName());
    for (size_t i = 0; i < iface.getParentInterfaceCount(); ++i) {
      result->addParentInterface(cloneType(iface.getParentInterface(i)));
    }
    return zc::mv(result);
  }
  if (ty.isIntersection()) {
    auto& intersection = static_cast<const IntersectionType&>(ty);
    zc::Vector<zc::Own<Type>> conjuncts;
    for (size_t i = 0; i < intersection.getConjunctCount(); ++i) {
      conjuncts.add(cloneType(intersection.getConjunct(i)));
    }
    return zc::heap<IntersectionType>(zc::mv(conjuncts));
  }
  if (ty.isExistential()) {
    auto& existential = static_cast<const ExistentialType&>(ty);
    return zc::heap<ExistentialType>(cloneType(existential.getInterfaceType()));
  }
  if (ty.isAssociated()) {
    auto& associated = static_cast<const AssociatedType&>(ty);
    return zc::heap<AssociatedType>(cloneType(associated.getParentType()), associated.getName());
  }

  // Fallback
  return zc::heap<ErrorType>();
}

zc::StringPtr DeclSignatureComputer::resolvePathName(ast::NodeId pathNode) const {
  if (!impl->tree.contains(pathNode)) return ""_zc;
  const auto& path = impl->tree.node(pathNode);

  // Handle simple IdentExpr used as a type path (e.g. "i32" in type annotation position).
  if (path.kind == SyntaxKind::IdentExpr) {
    auto nameId = IdentId(path.payload.words[kIdentExprNameWord]);
    return impl->tree.ident(nameId);
  }

  if (path.kind != SyntaxKind::ModulePath) return ""_zc;

  IdentList segments;
  segments.first = path.payload.words[kModulePathSegmentsFirstWord];
  segments.size = path.payload.words[kModulePathSegmentsSizeWord];

  auto segIds = impl->tree.identList(segments);
  if (segIds.size() == 0) return ""_zc;

  // Return the last segment (the simple name). For fully qualified lookup,
  // we would join with "::", but symbol lookup works best with simple names
  // and recursive scope resolution.
  auto lastSeg = segIds.back();
  return impl->tree.ident(lastSeg);
}

const symbol::Scope& DeclSignatureComputer::currentScope() {
  auto scope = impl->symbols.getCurrentScope();
  ZC_IF_SOME(s, scope) { return s; }

  auto constGlobal = impl->symbols.getScopeManager().getGlobalScope();
  ZC_IF_SOME(cg, constGlobal) { return cg; }

  ZC_UNREACHABLE;
}

zc::Maybe<symbol::Symbol&> DeclSignatureComputer::lookupSymbol(zc::StringPtr name) {
  // Try recursive lookup from current scope
  auto& scope = currentScope();
  auto result = impl->symbols.lookupRecursive(name, scope);
  ZC_IF_SOME(s, result) { return s; }

  // Fallback: try global scope
  auto global = impl->symbols.getScopeManager().getGlobalScope();
  ZC_IF_SOME(g, global) {
    result = impl->symbols.lookupRecursive(name, g);
    ZC_IF_SOME(s2, result) { return s2; }
  }

  return zc::none;
}

void DeclSignatureComputer::reportError(ast::NodeId node, zc::StringPtr message) {
  auto loc = nodeLoc(impl->tree, node);
  impl->diags.diagnose<DiagID::SemanticError>(loc, message);
  impl->hadErrors = true;
}

// ============================================================================
// computeSignatures - main entry point
// ============================================================================

bool DeclSignatureComputer::computeSignatures() {
  impl->hadErrors = false;

  const auto rootId = impl->tree.root();
  if (!impl->tree.contains(rootId)) return !impl->hadErrors;

  // Walk the AST in pre-order and compute signatures for each declaration.
  visitTreePreOrder(impl->tree, rootId, [this](ast::NodeId id, const ast::Node& node) {
    switch (node.kind) {
      case SyntaxKind::FunctionDecl: {
        auto name = impl->tree.ident(IdentId(node.payload.words[kFunctionDeclNameWord]));
        auto sym = lookupSymbol(name);
        ZC_IF_SOME(s, sym) {
          if (s.isFunctionSymbol()) {
            computeFunctionSignature(static_cast<symbol::FunctionSymbol&>(s), id);
          }
        }
        break;
      }
      case SyntaxKind::ClassDecl: {
        auto name = impl->tree.ident(IdentId(node.payload.words[kClassDeclNameWord]));
        auto sym = lookupSymbol(name);
        ZC_IF_SOME(s, sym) {
          if (s.isClassSymbol()) {
            computeClassSignature(static_cast<symbol::ClassSymbol&>(s), id);
          }
        }
        break;
      }
      case SyntaxKind::InterfaceDecl: {
        auto name = impl->tree.ident(IdentId(node.payload.words[kInterfaceDeclNameWord]));
        auto sym = lookupSymbol(name);
        ZC_IF_SOME(s, sym) {
          if (s.getKind() == symbol::SymbolKind::Interface) {
            computeInterfaceSignature(static_cast<symbol::InterfaceSymbol&>(s), id);
          }
        }
        break;
      }
      case SyntaxKind::MethodDecl: {
        auto name = impl->tree.ident(IdentId(node.payload.words[kMethodDeclNameWord]));
        auto sym = lookupSymbol(name);
        ZC_IF_SOME(s, sym) {
          if (s.isFunctionSymbol()) {
            computeMethodSignature(static_cast<symbol::FunctionSymbol&>(s), id);
          }
        }
        break;
      }
      case SyntaxKind::VariableDeclarator: {
        // Extract the variable name from the pattern and find its symbol
        auto patternId = ast::NodeId(node.payload.words[kVariableDeclaratorPatternWord]);
        if (impl->tree.contains(patternId)) {
          const auto& pattern = impl->tree.node(patternId);
          zc::StringPtr varName;

          if (pattern.kind == SyntaxKind::IdentifierPattern) {
            varName = impl->tree.ident(IdentId(pattern.payload.words[kIdentifierPatternNameWord]));
          } else if (pattern.kind == SyntaxKind::BindingPattern) {
            varName = impl->tree.ident(IdentId(pattern.payload.words[kBindingPatternNameWord]));
          }

          if (varName.size() > 0) {
            auto sym = lookupSymbol(varName);
            ZC_IF_SOME(s, sym) {
              if (s.isVariableSymbol()) {
                computeVariableSignature(static_cast<symbol::VariableSymbol&>(s), id);
              }
            }
          }
        }
        break;
      }
      case SyntaxKind::FieldDecl: {
        auto name = impl->tree.ident(IdentId(node.payload.words[kFieldDeclNameWord]));
        auto sym = lookupSymbol(name);
        ZC_IF_SOME(s, sym) {
          if (s.isVariableSymbol()) {
            computeVariableSignature(static_cast<symbol::VariableSymbol&>(s), id);
          }
        }
        break;
      }
      case SyntaxKind::AliasDecl: {
        // For type aliases, resolve the target type and store it
        auto targetId = ast::NodeId(node.payload.words[kAliasDeclTargetWord]);
        if (impl->tree.contains(targetId)) {
          auto symbolId = impl->metadata.symbol(id);
          auto guard =
              impl->cycles.enter(QueryKey::typeAliasOf(static_cast<uint32_t>(symbolId.getRaw())));
          if (guard.hasCycle()) {
            reportError(id, "recursive type alias cycle");
            break;
          }
          auto targetType = resolveTypeExpr(targetId);
          impl->typeEnv.setType(id, zc::mv(targetType));
        }
        break;
      }
      case SyntaxKind::StructDecl: {
        // Struct declarations are treated similarly to classes
        auto name = impl->tree.ident(IdentId(node.payload.words[kStructDeclNameWord]));
        auto sym = lookupSymbol(name);
        ZC_IF_SOME(s, sym) {
          if (s.isClassSymbol()) {
            computeClassSignature(static_cast<symbol::ClassSymbol&>(s), id);
          }
        }
        break;
      }
      case SyntaxKind::EnumDeclaration: {
        // Enum declarations produce a named type
        auto name = impl->tree.ident(IdentId(node.payload.words[kEnumDeclarationNameWord]));
        auto sym = lookupSymbol(name);
        ZC_IF_SOME(s, sym) {
          auto namedTy = zc::heap<type::NamedType>(name);
          if (s.isTypeSymbol()) { namedTy->setSymbol(static_cast<const symbol::TypeSymbol&>(s)); }
          impl->typeEnv.setType(id, zc::mv(namedTy));
        }
        break;
      }
      default:
        break;
    }
  });

  return !impl->hadErrors;
}

// ============================================================================
// Function signature computation
// ============================================================================

void DeclSignatureComputer::computeFunctionSignature(symbol::FunctionSymbol& fn,
                                                     ast::NodeId fnDecl) {
  (void)fn;  // Symbol used for identity; type computed from AST below
  const auto& node = impl->tree.node(fnDecl);

  auto oldGenericTypeVars = zc::mv(impl->genericTypeVars);
  impl->genericTypeVars = zc::HashMap<zc::StringPtr, const type::TypeVar*>();
  zc::Vector<zc::Own<type::GenericParam>> genericParams;

  auto typeParamsId = ast::NodeId(node.payload.words[kFunctionDeclTypeParamsIdWord]);
  if (impl->tree.contains(typeParamsId)) {
    const auto& typeParams = impl->tree.node(typeParamsId);
    if (typeParams.kind == SyntaxKind::GenericParams) {
      NodeList genericList;
      genericList.first = typeParams.payload.words[kGenericParamsParamsFirstWord];
      genericList.size = typeParams.payload.words[kGenericParamsParamsSizeWord];
      for (ast::NodeId genericId : impl->tree.list(genericList)) {
        if (!impl->tree.contains(genericId)) continue;
        const auto& genericNode = impl->tree.node(genericId);
        if (genericNode.kind != SyntaxKind::GenericTypeParam) continue;
        auto name = impl->tree.ident(IdentId(genericNode.payload.words[kGenericTypeParamNameWord]));
        if (name.size() == 0) continue;
        auto& typeVar = impl->typeEnv.freshTypeVar(name);
        impl->genericTypeVars.upsert(name, &typeVar);
        auto genericParam = zc::heap<type::GenericParam>(name);
        auto boundId = ast::NodeId(genericNode.payload.words[kGenericTypeParamBoundWord]);
        if (impl->tree.contains(boundId)) {
          auto boundType = resolveTypeExpr(boundId);
          typeVar.addUpperBound(cloneType(*boundType));
          genericParam->upperBounds.add(zc::mv(boundType));
        }
        auto defaultTyId = ast::NodeId(genericNode.payload.words[kGenericTypeParamDefaultTyWord]);
        if (impl->tree.contains(defaultTyId)) { (void)resolveTypeExpr(defaultTyId); }
        genericParams.add(zc::mv(genericParam));
        impl->typeEnv.setType(genericId, cloneType(typeVar));
      }
    }
  }

  // Extract parameter list
  auto paramsId = ast::NodeId(node.payload.words[kFunctionDeclParamsIdWord]);
  zc::Vector<zc::Own<type::Type>> paramTypes;

  if (impl->tree.contains(paramsId)) {
    const auto& paramList = impl->tree.node(paramsId);
    if (paramList.kind == SyntaxKind::FunctionParameterList) {
      NodeList paramNodeList;
      paramNodeList.first = paramList.payload.words[kFunctionParameterListParamsFirstWord];
      paramNodeList.size = paramList.payload.words[kFunctionParameterListParamsSizeWord];

      for (ast::NodeId paramId : impl->tree.list(paramNodeList)) {
        const auto& paramNode = impl->tree.node(paramId);
        if (paramNode.kind != SyntaxKind::FunctionParameterDecl) continue;

        auto paramTyId = ast::NodeId(paramNode.payload.words[kFunctionParameterDeclTyWord]);

        zc::Own<type::Type> paramType;
        if (impl->tree.contains(paramTyId)) {
          paramType = resolveTypeExpr(paramTyId);
        } else {
          // No type annotation - create a fresh type variable for inference.
          // This allows the body checker to accept operations on parameters
          // without reporting spurious "invalid operands" errors.
          auto paramName =
              impl->tree.ident(IdentId(paramNode.payload.words[kFunctionParameterDeclNameWord]));
          paramType = zc::heap<type::TypeVar>(paramName);
        }

        // Store parameter type in TypeEnv keyed by parameter node, so that
        // BodyChecker::checkIdentExpr can resolve parameter names via
        // getSymbolType().
        impl->typeEnv.setType(paramId, cloneType(*paramType));
        paramTypes.add(zc::mv(paramType));
      }
    }
  }

  // Extract return type
  auto retTyId = ast::NodeId(node.payload.words[kFunctionDeclRetTyWord]);
  zc::Own<type::Type> returnType;

  if (impl->tree.contains(retTyId) && impl->tree.node(retTyId).kind != SyntaxKind::Unknown) {
    returnType = resolveTypeExpr(retTyId);
  } else {
    // No explicit return type - default to unit.
    // The body checker will not enforce return type validation for functions
    // without explicit return type annotations (inferred return types are
    // not yet fully implemented; this is a pragmatic fallback).
    returnType = type::PrimitiveType::createUnit();
  }

  // Build function type
  auto fnType = zc::heap<type::FunctionType>(zc::mv(paramTypes), zc::mv(returnType));
  for (size_t i = 0; i < genericParams.size(); ++i) {
    fnType->addGenericParam(zc::mv(genericParams[i]));
  }

  // Handle raises type if present
  auto raisesTyId = ast::NodeId(node.payload.words[kFunctionDeclRaisesTyWord]);
  if (impl->tree.contains(raisesTyId)) {
    auto raisesType = resolveTypeExpr(raisesTyId);
    fnType->setRaisesType(zc::mv(raisesType));
  }

  // Store in type environment
  impl->typeEnv.setType(fnDecl, zc::mv(fnType));
  impl->genericTypeVars = zc::mv(oldGenericTypeVars);
}

// ============================================================================
// Class signature computation
// ============================================================================

void DeclSignatureComputer::computeClassSignature(symbol::ClassSymbol& cls, ast::NodeId classDecl) {
  const auto& node = impl->tree.node(classDecl);
  auto name = impl->tree.ident(IdentId(node.payload.words[kClassDeclNameWord]));

  // Create a named type referencing the class symbol.
  // ClassSymbol inherits from TypeSymbol, so we can pass it directly.
  auto namedTy = zc::heap<type::NamedType>(name, cls);

  // Store in type environment
  impl->typeEnv.setType(classDecl, zc::mv(namedTy));
}

// ============================================================================
// Interface signature computation
// ============================================================================

void DeclSignatureComputer::computeInterfaceSignature(symbol::InterfaceSymbol& iface,
                                                      ast::NodeId ifaceDecl) {
  const auto& node = impl->tree.node(ifaceDecl);
  auto name = impl->tree.ident(IdentId(node.payload.words[kInterfaceDeclNameWord]));

  // Create a named type referencing the interface symbol.
  // InterfaceSymbol inherits from TypeSymbol, so we can pass it directly.
  auto namedTy = zc::heap<type::NamedType>(name, iface);

  // Store in type environment
  impl->typeEnv.setType(ifaceDecl, zc::mv(namedTy));
}

// ============================================================================
// Method signature computation
// ============================================================================

void DeclSignatureComputer::computeMethodSignature(symbol::FunctionSymbol& method,
                                                   ast::NodeId methodDecl) {
  (void)method;  // Symbol used for identity; type computed from AST below
  const auto& node = impl->tree.node(methodDecl);

  // Extract parameter list
  auto paramsId = ast::NodeId(node.payload.words[kMethodDeclParamsIdWord]);
  zc::Vector<zc::Own<type::Type>> paramTypes;

  if (impl->tree.contains(paramsId)) {
    const auto& paramList = impl->tree.node(paramsId);
    if (paramList.kind == SyntaxKind::FunctionParameterList) {
      NodeList paramNodeList;
      paramNodeList.first = paramList.payload.words[kFunctionParameterListParamsFirstWord];
      paramNodeList.size = paramList.payload.words[kFunctionParameterListParamsSizeWord];

      for (ast::NodeId paramId : impl->tree.list(paramNodeList)) {
        const auto& paramNode = impl->tree.node(paramId);
        if (paramNode.kind != SyntaxKind::FunctionParameterDecl) continue;

        auto paramTyId = ast::NodeId(paramNode.payload.words[kFunctionParameterDeclTyWord]);

        zc::Own<type::Type> paramType;
        if (impl->tree.contains(paramTyId)) {
          paramType = resolveTypeExpr(paramTyId);
        } else {
          // No type annotation - create a fresh type variable for inference.
          auto paramName =
              impl->tree.ident(IdentId(paramNode.payload.words[kFunctionParameterDeclNameWord]));
          paramType = zc::heap<type::TypeVar>(paramName);
        }

        // Store parameter type in TypeEnv keyed by parameter node
        impl->typeEnv.setType(paramId, cloneType(*paramType));
        paramTypes.add(zc::mv(paramType));
      }
    }
  }

  // Extract return type
  auto retTyId = ast::NodeId(node.payload.words[kMethodDeclRetTyWord]);
  zc::Own<type::Type> returnType;

  if (impl->tree.contains(retTyId)) {
    returnType = resolveTypeExpr(retTyId);
  } else {
    returnType = type::PrimitiveType::createUnit();
  }

  // Build function type
  auto fnType = zc::heap<type::FunctionType>(zc::mv(paramTypes), zc::mv(returnType));

  impl->typeEnv.setType(methodDecl, zc::mv(fnType));
}

// ============================================================================
// Variable signature computation
// ============================================================================

void DeclSignatureComputer::computeVariableSignature(symbol::VariableSymbol& var,
                                                     ast::NodeId varDecl) {
  (void)var;  // Symbol used for identity; type computed from AST below
  const auto& node = impl->tree.node(varDecl);

  // Extract the type annotation NodeId
  ast::NodeId tyId;
  if (node.kind == SyntaxKind::VariableDeclarator) {
    tyId = ast::NodeId(node.payload.words[kVariableDeclaratorTyWord]);
  } else if (node.kind == SyntaxKind::FieldDecl) {
    tyId = ast::NodeId(node.payload.words[kFieldDeclTyWord]);
  } else {
    tyId = ast::NodeId();
  }

  if (impl->tree.contains(tyId)) {
    auto ty = resolveTypeExpr(tyId);
    impl->typeEnv.setType(varDecl, zc::mv(ty));
  } else {
    // No explicit type annotation - use error type as placeholder;
    // actual type inference happens in Phase B
    impl->typeEnv.setType(varDecl, zc::heap<type::ErrorType>("type needs inference"));
  }
}

// ============================================================================
// Type expression resolution - dispatch
// ============================================================================

static zc::Maybe<type::PrimitiveKind> resolvePrimitiveKind(zc::StringPtr name) {
  using type::PrimitiveKind;
  if (name == "i8"_zc) return PrimitiveKind::I8;
  if (name == "i16"_zc) return PrimitiveKind::I16;
  if (name == "i32"_zc) return PrimitiveKind::I32;
  if (name == "i64"_zc) return PrimitiveKind::I64;
  if (name == "u8"_zc) return PrimitiveKind::U8;
  if (name == "u16"_zc) return PrimitiveKind::U16;
  if (name == "u32"_zc) return PrimitiveKind::U32;
  if (name == "u64"_zc) return PrimitiveKind::U64;
  if (name == "f32"_zc) return PrimitiveKind::F32;
  if (name == "f64"_zc) return PrimitiveKind::F64;
  if (name == "bool"_zc) return PrimitiveKind::Bool;
  if (name == "str"_zc || name == "string"_zc) return PrimitiveKind::Str;
  if (name == "unit"_zc || name == "void"_zc) return PrimitiveKind::Unit;
  return zc::none;
}

zc::Own<type::Type> DeclSignatureComputer::resolveTypeExpr(ast::NodeId typeExprId) {
  if (!impl->tree.contains(typeExprId)) {
    return zc::heap<type::ErrorType>("invalid type expression");
  }

  const auto& node = impl->tree.node(typeExprId);

  switch (node.kind) {
    case SyntaxKind::PredefinedTypeExpr:
      return resolvePredefinedType(node);

    case SyntaxKind::NamedTypeExpr:
      return resolveNamedType(node);

    case SyntaxKind::FunctionTypeExpr:
      return resolveFunctionType(node);

    case SyntaxKind::TupleTypeExpr:
      return resolveTupleType(node);

    case SyntaxKind::ObjectTypeExpr:
      return resolveObjectType(node);

    case SyntaxKind::ArrayTypeExpr:
    case SyntaxKind::FixedArrayTypeExpr:
    case SyntaxKind::SliceArrayTypeExpr:
      return resolveArrayType(node);

    case SyntaxKind::UnionTypeExpr:
      return resolveUnionType(node);

    case SyntaxKind::IntersectionTypeExpr:
      return resolveIntersectionType(node);

    case SyntaxKind::OptionalTypeExpr:
      return resolveOptionalType(node);

    case SyntaxKind::DynTypeExpr:
      return resolveDynType(node);

    case SyntaxKind::BottomTypeExpr:
      return type::PrimitiveType::createNever();

    case SyntaxKind::UnaryExpression: {
      auto op = static_cast<ast::UnaryOperatorKind>(node.payload.words[kUnaryExpressionOpWord]);
      if (op == ast::UnaryOperatorKind::Ref) { return resolveReferenceType(typeExprId); }
      if (op == ast::UnaryOperatorKind::Deref) { return resolveRawPointerType(typeExprId); }
      break;
    }

    default:
      break;
  }

  // Unknown type expression kind
  reportError(typeExprId, "unsupported type expression");
  return zc::heap<type::ErrorType>("unsupported type expression");
}

// ============================================================================
// Predefined (primitive) type resolution
// ============================================================================

zc::Own<type::Type> DeclSignatureComputer::resolvePredefinedType(const ast::Node& node) {
  auto kindVal = static_cast<uint8_t>(node.payload.words[kPredefinedTypeExprKindWord]);
  auto primKind = static_cast<type::PrimitiveKind>(kindVal);
  return type::PrimitiveType::create(primKind);
}

// ============================================================================
// Named type resolution
// ============================================================================

zc::Own<type::Type> DeclSignatureComputer::resolveNamedType(const ast::Node& node) {
  auto pathId = ast::NodeId(node.payload.words[kNamedTypeExprPathWord]);
  auto name = resolvePathName(pathId);

  if (name.size() == 0) { return zc::heap<type::ErrorType>("empty type name"); }

  ZC_IF_SOME(typeVar, impl->genericTypeVars.find(name)) {
    if (typeVar != nullptr) { return cloneType(*typeVar); }
  }

  // Resolve well-known primitive type names directly to PrimitiveType.
  auto primKind = resolvePrimitiveKind(name);
  ZC_IF_SOME(kind, primKind) { return zc::heap<type::PrimitiveType>(kind); }

  // Create the named type
  auto namedTy = zc::heap<type::NamedType>(name);

  // Try to resolve the symbol
  auto sym = lookupSymbol(name);
  ZC_IF_SOME(s, sym) {
    if (s.getKind() == symbol::SymbolKind::TypeAlias) { return resolveTypeAliasTarget(s, pathId); }
    if (s.isTypeSymbol()) { namedTy->setSymbol(static_cast<const symbol::TypeSymbol&>(s)); }
  }
  // Note: if the symbol is not found, we still create the NamedType with
  // just the name. This allows forward references and supports error
  // recovery. The unresolved status can be detected later via getSymbol().

  // Resolve generic type arguments if present
  NodeList argsList;
  argsList.first = node.payload.words[kNamedTypeExprArgsFirstWord];
  argsList.size = node.payload.words[kNamedTypeExprArgsSizeWord];

  if (!argsList.empty()) {
    for (ast::NodeId argId : impl->tree.list(argsList)) {
      auto argType = resolveTypeExpr(argId);
      namedTy->addTypeArg(zc::mv(argType));
    }
  }

  return namedTy;
}

zc::Own<type::Type> DeclSignatureComputer::resolveTypeAliasTarget(symbol::Symbol& symbol,
                                                                  ast::NodeId useSite) {
  auto guard = impl->cycles.enter(QueryKey::typeAliasOf(querySymbolId(symbol)));
  if (guard.hasCycle()) {
    reportError(useSite, "recursive type alias cycle");
    return zc::heap<type::ErrorType>("recursive type alias cycle");
  }

  auto refs = symbol.getDeclarationRefs();
  if (refs.size() == 0) { return zc::heap<type::NamedType>(symbol.getName()); }

  ast::NodeId aliasDecl = refs[0].node;
  if (!impl->tree.contains(aliasDecl)) { return zc::heap<type::NamedType>(symbol.getName()); }

  const auto& aliasNode = impl->tree.node(aliasDecl);
  if (aliasNode.kind != SyntaxKind::AliasDecl) {
    return zc::heap<type::NamedType>(symbol.getName());
  }

  auto targetId = ast::NodeId(aliasNode.payload.words[kAliasDeclTargetWord]);
  if (!impl->tree.contains(targetId)) {
    return zc::heap<type::ErrorType>("type alias has no target");
  }

  return resolveTypeExpr(targetId);
}

// ============================================================================
// Function type resolution
// ============================================================================

zc::Own<type::Type> DeclSignatureComputer::resolveFunctionType(const ast::Node& node) {
  // Extract parameter types
  NodeList paramsList;
  paramsList.first = node.payload.words[kFunctionTypeExprParamsFirstWord];
  paramsList.size = node.payload.words[kFunctionTypeExprParamsSizeWord];

  zc::Vector<zc::Own<type::Type>> paramTypes;
  for (ast::NodeId paramId : impl->tree.list(paramsList)) {
    paramTypes.add(resolveTypeExpr(paramId));
  }

  // Extract return type
  auto retTyId = ast::NodeId(node.payload.words[kFunctionTypeExprRetTyWord]);
  zc::Own<type::Type> returnType;

  if (impl->tree.contains(retTyId)) {
    returnType = resolveTypeExpr(retTyId);
  } else {
    returnType = type::PrimitiveType::createUnit();
  }

  auto fnType = zc::heap<type::FunctionType>(zc::mv(paramTypes), zc::mv(returnType));

  // Handle raises type
  auto raisesId = ast::NodeId(node.payload.words[kFunctionTypeExprRaisesWord]);
  if (impl->tree.contains(raisesId)) {
    auto raisesType = resolveTypeExpr(raisesId);
    fnType->setRaisesType(zc::mv(raisesType));
  }

  return fnType;
}

// ============================================================================
// Tuple type resolution
// ============================================================================

zc::Own<type::Type> DeclSignatureComputer::resolveTupleType(const ast::Node& node) {
  NodeList elemsList;
  elemsList.first = node.payload.words[kTupleTypeExprElemsFirstWord];
  elemsList.size = node.payload.words[kTupleTypeExprElemsSizeWord];

  zc::Vector<zc::Own<type::Type>> elemTypes;
  for (ast::NodeId elemId : impl->tree.list(elemsList)) { elemTypes.add(resolveTypeExpr(elemId)); }

  // Empty tuple = unit type
  if (elemTypes.size() == 0) { return type::PrimitiveType::createUnit(); }

  return zc::heap<type::TupleType>(zc::mv(elemTypes));
}

// ============================================================================
// Object type resolution
// ============================================================================

zc::Own<type::Type> DeclSignatureComputer::resolveObjectType(const ast::Node& node) {
  NodeList membersList;
  membersList.first = node.payload.words[kObjectTypeExprMembersFirstWord];
  membersList.size = node.payload.words[kObjectTypeExprMembersSizeWord];

  auto objType = zc::heap<type::ObjectType>();

  for (ast::NodeId memberId : impl->tree.list(membersList)) {
    const auto& memberNode = impl->tree.node(memberId);
    if (memberNode.kind != SyntaxKind::ObjectTypeMember) continue;

    auto memberName =
        impl->tree.ident(IdentId(memberNode.payload.words[kObjectTypeMemberNameWord]));
    auto memberTyId = ast::NodeId(memberNode.payload.words[kObjectTypeMemberTyWord]);

    auto memberType = resolveTypeExpr(memberTyId);
    objType->addMember(memberName, zc::mv(memberType));
  }

  return objType;
}

// ============================================================================
// Array type resolution
// ============================================================================

zc::Own<type::Type> DeclSignatureComputer::resolveArrayType(const ast::Node& node) {
  ast::NodeId elemId;

  if (node.kind == SyntaxKind::ArrayTypeExpr) {
    elemId = ast::NodeId(node.payload.words[kArrayTypeExprElemWord]);
  } else if (node.kind == SyntaxKind::FixedArrayTypeExpr) {
    elemId = ast::NodeId(node.payload.words[kFixedArrayTypeExprElemWord]);
  } else if (node.kind == SyntaxKind::SliceArrayTypeExpr) {
    elemId = ast::NodeId(node.payload.words[kSliceArrayTypeExprElemWord]);
  } else {
    return zc::heap<type::ErrorType>("invalid array type");
  }

  if (!impl->tree.contains(elemId)) {
    return zc::heap<type::ErrorType>("missing array element type");
  }

  auto elemType = resolveTypeExpr(elemId);
  return zc::heap<type::ArrayType>(zc::mv(elemType));
}

// ============================================================================
// Union type resolution
// ============================================================================

zc::Own<type::Type> DeclSignatureComputer::resolveUnionType(const ast::Node& node) {
  NodeList altsList;
  altsList.first = node.payload.words[kUnionTypeExprAltsFirstWord];
  altsList.size = node.payload.words[kUnionTypeExprAltsSizeWord];

  zc::Vector<zc::Own<type::Type>> alternatives;
  for (ast::NodeId altId : impl->tree.list(altsList)) { alternatives.add(resolveTypeExpr(altId)); }

  if (alternatives.size() == 0) { return type::PrimitiveType::createNever(); }

  return zc::heap<type::UnionType>(zc::mv(alternatives));
}

// ============================================================================
// Intersection type resolution
// ============================================================================

zc::Own<type::Type> DeclSignatureComputer::resolveIntersectionType(const ast::Node& node) {
  NodeList altsList;
  altsList.first = node.payload.words[kIntersectionTypeExprAltsFirstWord];
  altsList.size = node.payload.words[kIntersectionTypeExprAltsSizeWord];

  zc::Vector<zc::Own<type::Type>> conjuncts;
  for (ast::NodeId altId : impl->tree.list(altsList)) { conjuncts.add(resolveTypeExpr(altId)); }

  if (conjuncts.size() == 0) { return type::PrimitiveType::createAny(); }

  return zc::heap<type::IntersectionType>(zc::mv(conjuncts));
}

// ============================================================================
// Reference type resolution
// ============================================================================

zc::Own<type::Type> DeclSignatureComputer::resolveReferenceType(ast::NodeId typeExprId) {
  if (!impl->tree.contains(typeExprId)) {
    return zc::heap<type::ErrorType>("invalid reference type");
  }

  const auto& node = impl->tree.node(typeExprId);

  // Handle UnaryExpression with Ref operator: &T or &mut T
  if (node.kind == SyntaxKind::UnaryExpression) {
    auto operandId = ast::NodeId(node.payload.words[kUnaryExpressionOperandWord]);

    // Determine mutability.
    // Default to Const. The parser may encode &mut T differently (e.g., as
    // a nested structure or with a keyword marker). For now, we check if
    // the operand has any mutability indicator.
    auto mutability = type::Mutability::Const;

    // Check if the operand indicates mutability (e.g., the inner expression
    // starts with a `mut` keyword). This handles the case where the parser
    // represents `&mut T` as &(mut T) with mut as a prefix NamedTypeExpr.
    if (impl->tree.contains(operandId)) {
      const auto& operand = impl->tree.node(operandId);
      if (operand.kind == SyntaxKind::NamedTypeExpr) {
        auto pathId = ast::NodeId(operand.payload.words[kNamedTypeExprPathWord]);
        auto name = resolvePathName(pathId);
        if (name == "mut"_zc) {
          // This is `&mut T` where `mut T` is represented as a NamedTypeExpr
          // with path "mut". The actual pointee type is in the type args.
          mutability = type::Mutability::Mutable;
          NodeList args;
          args.first = operand.payload.words[kNamedTypeExprArgsFirstWord];
          args.size = operand.payload.words[kNamedTypeExprArgsSizeWord];
          if (!args.empty()) {
            auto firstArg = impl->tree.list(args).front();
            auto pointeeType = resolveTypeExpr(firstArg);
            return zc::heap<type::ReferenceType>(zc::mv(pointeeType), mutability);
          }
        }
      }
    }

    // Standard case: &T where T is the operand
    auto innerType = resolveTypeExpr(operandId);
    return zc::heap<type::ReferenceType>(zc::mv(innerType), mutability);
  }

  return zc::heap<type::ErrorType>("invalid reference type syntax");
}

// ============================================================================
// Raw pointer type resolution
// ============================================================================

zc::Own<type::Type> DeclSignatureComputer::resolveRawPointerType(ast::NodeId typeExprId) {
  if (!impl->tree.contains(typeExprId)) {
    return zc::heap<type::ErrorType>("invalid raw pointer type");
  }

  const auto& node = impl->tree.node(typeExprId);

  // Handle UnaryExpression with Deref operator: *T
  // For *const T and *mut T, the parser may represent this as *(mut T)
  // or *(const T) with the mutability keyword as a NamedTypeExpr prefix.
  if (node.kind == SyntaxKind::UnaryExpression) {
    auto operandId = ast::NodeId(node.payload.words[kUnaryExpressionOperandWord]);

    auto mutability = type::Mutability::Const;

    // Check if the operand indicates mutability (*mut T) or const (*const T).
    if (impl->tree.contains(operandId)) {
      const auto& operand = impl->tree.node(operandId);
      if (operand.kind == SyntaxKind::NamedTypeExpr) {
        auto pathId = ast::NodeId(operand.payload.words[kNamedTypeExprPathWord]);
        auto name = resolvePathName(pathId);
        if (name == "mut"_zc) {
          mutability = type::Mutability::Mutable;
          // Extract actual pointee from type args
          NodeList args;
          args.first = operand.payload.words[kNamedTypeExprArgsFirstWord];
          args.size = operand.payload.words[kNamedTypeExprArgsSizeWord];
          if (!args.empty()) {
            auto firstArg = impl->tree.list(args).front();
            auto pointeeType = resolveTypeExpr(firstArg);
            return zc::heap<type::RawPointerType>(zc::mv(pointeeType), mutability);
          }
        }
        if (name == "const"_zc) {
          mutability = type::Mutability::Const;
          // Extract actual pointee from type args
          NodeList args;
          args.first = operand.payload.words[kNamedTypeExprArgsFirstWord];
          args.size = operand.payload.words[kNamedTypeExprArgsSizeWord];
          if (!args.empty()) {
            auto firstArg = impl->tree.list(args).front();
            auto pointeeType = resolveTypeExpr(firstArg);
            return zc::heap<type::RawPointerType>(zc::mv(pointeeType), mutability);
          }
        }
      }
    }

    // Standard case: *T where T is the operand
    auto innerType = resolveTypeExpr(operandId);
    return zc::heap<type::RawPointerType>(zc::mv(innerType), mutability);
  }

  return zc::heap<type::ErrorType>("invalid raw pointer type syntax");
}

// ============================================================================
// Dynamic/existential type resolution
// ============================================================================

zc::Own<type::Type> DeclSignatureComputer::resolveDynType(const ast::Node& node) {
  // Extract interface list
  auto ifacesId = ast::NodeId(node.payload.words[kDynTypeExprIfacesIdWord]);

  if (!impl->tree.contains(ifacesId)) {
    return zc::heap<type::ErrorType>("dyn type requires at least one interface");
  }

  const auto& ifaceListNode = impl->tree.node(ifacesId);
  if (ifaceListNode.kind != SyntaxKind::DynTypeIfaceList) {
    // The ifaces_id points directly to a type expression (e.g., a NamedTypeExpr)
    auto ifaceType = resolveTypeExpr(ifacesId);
    return zc::heap<type::ExistentialType>(zc::mv(ifaceType));
  }

  // Resolve the first interface from the list
  NodeList ifaceNodeList;
  ifaceNodeList.first = ifaceListNode.payload.words[kDynTypeIfaceListIfacesFirstWord];
  ifaceNodeList.size = ifaceListNode.payload.words[kDynTypeIfaceListIfacesSizeWord];

  auto ifaces = impl->tree.list(ifaceNodeList);
  if (ifaces.size() == 0) {
    return zc::heap<type::ErrorType>("dyn type requires at least one interface");
  }

  // If there are multiple interfaces, wrap them in an intersection type
  // first, then package as an existential.
  if (ifaces.size() > 1) {
    zc::Vector<zc::Own<type::Type>> conjuncts;
    for (ast::NodeId ifaceId : ifaces) { conjuncts.add(resolveTypeExpr(ifaceId)); }
    auto interTy = zc::heap<type::IntersectionType>(zc::mv(conjuncts));
    return zc::heap<type::ExistentialType>(zc::mv(interTy));
  }

  // Single interface
  auto firstIfaceId = ifaces.front();
  auto ifaceType = resolveTypeExpr(firstIfaceId);
  return zc::heap<type::ExistentialType>(zc::mv(ifaceType));
}

// ============================================================================
// Optional type resolution
// ============================================================================

zc::Own<type::Type> DeclSignatureComputer::resolveOptionalType(const ast::Node& node) {
  auto innerId = ast::NodeId(node.payload.words[kOptionalTypeExprInnerWord]);

  if (!impl->tree.contains(innerId)) { return zc::heap<type::ErrorType>("invalid optional type"); }

  auto innerType = resolveTypeExpr(innerId);

  // Optional T is represented as T | null (union with null)
  zc::Vector<zc::Own<type::Type>> alternatives;
  alternatives.add(zc::mv(innerType));
  alternatives.add(type::PrimitiveType::createNull());

  return zc::heap<type::UnionType>(zc::mv(alternatives));
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
