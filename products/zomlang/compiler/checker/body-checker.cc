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

#include "zomlang/compiler/checker/body-checker.h"

#include "zc/core/common.h"
#include "zc/core/map.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/checker/exhaustiveness.h"
#include "zomlang/compiler/checker/trait-resolver.h"
#include "zomlang/compiler/checker/type-expr-utils.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/compiler/symbol/symbol.h"
#include "zomlang/compiler/symbol/type-symbol.h"
#include "zomlang/compiler/symbol/value-symbol.h"
#include "zomlang/compiler/type/array-type.h"
#include "zomlang/compiler/type/associated-type.h"
#include "zomlang/compiler/type/coercion.h"
#include "zomlang/compiler/type/constraint-set.h"
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
#include "zomlang/compiler/type/type-algebra.h"
#include "zomlang/compiler/type/type-env.h"
#include "zomlang/compiler/type/type-var.h"
#include "zomlang/compiler/type/type.h"
#include "zomlang/compiler/type/unification.h"
#include "zomlang/compiler/type/union-type.h"

namespace zomlang {
namespace compiler {
namespace checker {

using namespace zomlang::compiler::type;
using namespace zomlang::compiler::symbol;
using namespace zomlang::compiler::ast;
using namespace zomlang::compiler::diagnostics;

// ============================================================================
// Impl struct (PIMPL)
// ============================================================================

struct BodyChecker::Impl {
  type::TypeEnv& typeEnv;
  type::UnificationEngine& unifier;
  type::ConstraintSet& constraints;
  symbol::SymbolTable& symbols;
  const ast::Tree& tree;
  const ast::BindingMetadata& metadata;
  diagnostics::DiagnosticEngine& diags;
  type::CoercionResolver coercions;

  // The expected return type for the current function context
  zc::Maybe<const type::Type&> expectedRetType;
  zc::Maybe<const type::Type&> expectedRaisesType;

  // Error type singleton (lazily created)
  zc::Own<type::ErrorType> errorTy;

  // Tracks whether the current expression is checked inside an unsafe block.
  uint32_t unsafeDepth = 0;

  // Track whether we had errors
  bool hadErrors = false;

  // Map from scope ID (as stored in BindingMetadata) to scope pointer.
  // Used to enter the correct scope when checking function/class bodies.
  zc::HashMap<uint32_t, const symbol::Scope*> scopeIdMap;  // non-owning

  // Scope stack for tracking pushed scopes
  zc::Vector<const symbol::Scope*> scopeStack;  // non-owning

  // Unannotated local bindings can be refined by later use-site constraints.
  zc::HashMap<uint32_t, ast::NodeId> identExprDeclarations;
  zc::HashMap<uint32_t, symbol::SymbolId> memberExprSymbols;
  zc::HashMap<uint32_t, bool> memberExprIsStatic;
  zc::HashMap<uint32_t, zc::StringPtr> memberExprDynInterfaceNames;
  zc::HashMap<uint32_t, zc::StringPtr> memberExprDynMethodNames;
  zc::HashMap<uint32_t, uint32_t> memberExprDynVTableSlots;
  zc::Vector<ast::NodeId> pendingLocalIntDeclarations;

  Impl(type::TypeEnv& te, type::UnificationEngine& u, type::ConstraintSet& cs,
       symbol::SymbolTable& sym, const ast::Tree& t, const ast::BindingMetadata& meta,
       diagnostics::DiagnosticEngine& d)
      : typeEnv(te), unifier(u), constraints(cs), symbols(sym), tree(t), metadata(meta), diags(d) {
    buildScopeIdMap();
  }

  void buildScopeIdMap() {
    auto allScopes = symbols.getScopeManager().getAllScopes();
    for (size_t i = 0; i < allScopes.size(); ++i) {
      const auto& own = allScopes[i];
      if (own) {
        const symbol::Scope& s = *own;
        uint32_t sid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&s) & 0xFFFFFFFF);
        scopeIdMap.insert(sid, &s);
      }
    }
  }

  bool pushNodeScope(ast::NodeId node) {
    uint32_t sid = metadata.scope(node);
    if (sid == 0) return false;

    auto it = scopeIdMap.find(sid);
    ZC_IF_SOME(scopePtr, it) {
      scopeStack.add(scopePtr);
      symbols.getScopeManager().pushScope(*scopePtr);
      return true;
    }
    return false;
  }

  void popNodeScope() {
    if (!scopeStack.empty()) {
      scopeStack.removeLast();
      symbols.getScopeManager().popScope();
    }
  }
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

BodyChecker::BodyChecker(type::TypeEnv& typeEnv, type::UnificationEngine& unifier,
                         type::ConstraintSet& constraints, symbol::SymbolTable& symbols,
                         const ast::Tree& tree, const ast::BindingMetadata& metadata,
                         diagnostics::DiagnosticEngine& diags) noexcept
    : impl(zc::heap<Impl>(typeEnv, unifier, constraints, symbols, tree, metadata, diags)) {}

BodyChecker::~BodyChecker() noexcept(false) = default;

const type::ConstraintSet& BodyChecker::getConstraints() const { return impl->constraints; }

// ============================================================================
// Helpers
// ============================================================================

static source::SourceLoc getNodeLoc(const ast::Tree& tree, ast::NodeId id) {
  return tree.node(id).range.getStart();
}

static void recordPrimitiveDispatch(type::TypeEnv& typeEnv, ast::NodeId node,
                                    type::ReceiverMode receiverMode,
                                    zc::ArrayPtr<const type::TypeId> argumentTypes,
                                    const type::Type& resultType) {
  type::CallDispatchRecord record;
  record.targetKind = type::CallTargetKind::PrimitiveOperator;
  record.receiverMode = receiverMode;
  for (auto argType : argumentTypes) { record.argumentTypes.add(argType); }
  record.resultType = typeEnv.internType(resultType);
  typeEnv.setDispatch(node, zc::mv(record));
}

static void recordFreeFunctionDispatch(type::TypeEnv& typeEnv, ast::NodeId node,
                                       symbol::SymbolId targetSymbol,
                                       zc::ArrayPtr<const type::TypeId> argumentTypes,
                                       const type::Type& resultType) {
  type::CallDispatchRecord record;
  record.targetKind = type::CallTargetKind::FreeFunction;
  record.receiverMode = type::ReceiverMode::None;
  record.targetSymbol = targetSymbol;
  for (auto argType : argumentTypes) { record.argumentTypes.add(argType); }
  record.resultType = typeEnv.internType(resultType);
  typeEnv.setDispatch(node, zc::mv(record));
}

static void recordInstanceMethodDispatch(type::TypeEnv& typeEnv, ast::NodeId node,
                                         symbol::SymbolId targetSymbol,
                                         zc::ArrayPtr<const type::TypeId> argumentTypes,
                                         const type::Type& resultType) {
  type::CallDispatchRecord record;
  record.targetKind = type::CallTargetKind::InstanceMethod;
  record.receiverMode = type::ReceiverMode::ImplicitSelf;
  record.targetSymbol = targetSymbol;
  for (auto argType : argumentTypes) { record.argumentTypes.add(argType); }
  record.resultType = typeEnv.internType(resultType);
  typeEnv.setDispatch(node, zc::mv(record));
}

static void recordStaticMethodDispatch(type::TypeEnv& typeEnv, ast::NodeId node,
                                       symbol::SymbolId targetSymbol,
                                       zc::ArrayPtr<const type::TypeId> argumentTypes,
                                       const type::Type& resultType) {
  type::CallDispatchRecord record;
  record.targetKind = type::CallTargetKind::StaticMethod;
  record.receiverMode = type::ReceiverMode::None;
  record.targetSymbol = targetSymbol;
  for (auto argType : argumentTypes) { record.argumentTypes.add(argType); }
  record.resultType = typeEnv.internType(resultType);
  typeEnv.setDispatch(node, zc::mv(record));
}

static void recordDynVTableDispatch(type::TypeEnv& typeEnv, ast::NodeId node,
                                    zc::StringPtr interfaceName, zc::StringPtr methodName,
                                    uint32_t slot, zc::ArrayPtr<const type::TypeId> argumentTypes,
                                    const type::Type& resultType) {
  type::CallDispatchRecord record;
  record.targetKind = type::CallTargetKind::DynVTable;
  record.receiverMode = type::ReceiverMode::ImplicitSelf;
  record.interfaceName = zc::str(interfaceName);
  record.methodName = zc::str(methodName);
  record.vtableSlot = slot;
  for (auto argType : argumentTypes) { record.argumentTypes.add(argType); }
  record.resultType = typeEnv.internType(resultType);
  typeEnv.setDispatch(node, zc::mv(record));
}

static zc::Vector<type::TypeId> dispatchArgTypeIds(type::TypeEnv& typeEnv,
                                                   zc::ArrayPtr<const ast::NodeId> argNodes) {
  zc::Vector<type::TypeId> result;
  for (auto argNode : argNodes) {
    if (typeEnv.hasType(argNode)) {
      result.add(typeEnv.internType(typeEnv.find(typeEnv.getType(argNode))));
    }
  }
  return result;
}

void BodyChecker::reportTypeMismatch(ast::NodeId node, const type::Type& expected,
                                     const type::Type& actual) {
  auto loc = getNodeLoc(impl->tree, node);
  impl->diags.diagnose<DiagID::TypeCheckerTypeMismatch>(loc, expected.toString(),
                                                        actual.toString());
  impl->hadErrors = true;
}

void BodyChecker::reportCannotUnify(ast::NodeId node, const type::Type& expected,
                                    const type::Type& actual, zc::StringPtr context) {
  auto loc = getNodeLoc(impl->tree, node);
  impl->diags.diagnose<DiagID::CannotUnifyTypes>(loc, expected.toString(), actual.toString(),
                                                 context);
  impl->hadErrors = true;
}

void BodyChecker::reportInfiniteType(ast::NodeId node, zc::StringPtr description) {
  auto loc = getNodeLoc(impl->tree, node);
  impl->diags.diagnose<DiagID::InfiniteType>(loc, description);
  impl->hadErrors = true;
}

zc::Maybe<symbol::Symbol&> BodyChecker::lookupSymbol(zc::StringPtr name) {
  auto& scope = currentScope();
  auto result = impl->symbols.lookupRecursive(name, scope);
  if (result != zc::none) return result;

  auto global = impl->symbols.getScopeManager().getGlobalScope();
  ZC_IF_SOME(g, global) {
    result = impl->symbols.lookupRecursive(name, g);
    if (result != zc::none) return result;
  }

  return zc::none;
}

const symbol::Scope& BodyChecker::currentScope() {
  // Use our own scope stack (maintained by pushNodeScope/popNodeScope and
  // checkFunctionDecl), matching the NameResolver pattern. This avoids the
  // SymbolTable vs ScopeManager currentScope desync: pushScope() updates
  // ScopeManager::currentScope but symbols.getCurrentScope() reads a separate
  // SymbolTable::currentScope that is never updated by scope pushes.
  if (!impl->scopeStack.empty()) { return *impl->scopeStack.back(); }

  auto constGlobal = impl->symbols.getScopeManager().getGlobalScope();
  ZC_IF_SOME(cg, constGlobal) { return cg; }

  ZC_UNREACHABLE;
}

static ast::IdentList modulePathSegments(const ast::Node& path) {
  ast::IdentList segments;
  segments.first = path.payload.words[ast::kModulePathSegmentsFirstWord];
  segments.size = path.payload.words[ast::kModulePathSegmentsSizeWord];
  return segments;
}

zc::Maybe<const type::Type&> BodyChecker::getSymbolType(const symbol::Symbol& sym) {
  // Try to get type from TypeEnv via declaration node
  auto declRefs = sym.getDeclarationRefs();
  for (const auto& ref : declRefs) {
    if (impl->typeEnv.hasType(ref.node)) { return impl->typeEnv.getType(ref.node); }
  }

  for (const auto& ref : declRefs) {
    if (!impl->tree.contains(ref.node)) continue;
    const auto& decl = impl->tree.node(ref.node);
    ast::NodeId tyId;
    if (decl.kind == SyntaxKind::VariableDeclarator) {
      tyId = ast::NodeId(decl.payload.words[kVariableDeclaratorTyWord]);
    } else if (decl.kind == SyntaxKind::FieldDecl) {
      tyId = ast::NodeId(decl.payload.words[kFieldDeclTyWord]);
    } else if (decl.kind == SyntaxKind::FunctionParameterDecl) {
      tyId = ast::NodeId(decl.payload.words[kFunctionParameterDeclTyWord]);
    }
    if (impl->tree.contains(tyId)) {
      auto ty = resolveTypeExpr(tyId);
      if (ty) {
        impl->typeEnv.setType(ref.node, zc::mv(ty));
        return impl->typeEnv.getType(ref.node);
      }
    }
  }

  // Try TypeSymbol - create a NamedType from the symbol name
  if (sym.isTypeSymbol()) {
    auto& typeSym = static_cast<const symbol::TypeSymbol&>(sym);
    auto typeName = typeSym.getName();
    if (typeName.size() > 0) {
      auto astType = typeSym.getAstType();
      ZC_IF_SOME(nodeId, astType) {
        if (!impl->typeEnv.hasType(nodeId)) {
          impl->typeEnv.setType(nodeId, zc::heap<type::NamedType>(typeName));
        }
        return impl->typeEnv.getType(nodeId);
      }
      // No AST type set - use first declaration ref node as storage key
      auto declRefs = sym.getDeclarationRefs();
      if (declRefs.size() > 0) {
        auto& ref = *declRefs.begin();
        if (!impl->typeEnv.hasType(ref.node)) {
          impl->typeEnv.setType(ref.node, zc::heap<type::NamedType>(typeName));
        }
        return impl->typeEnv.getType(ref.node);
      }
    }
  }

  // Try ValueSymbol - get its TypeSymbol and create a NamedType
  if (sym.isValueSymbol()) {
    auto symType = sym.getType();
    ZC_IF_SOME(typeSym, symType) {
      auto typeName = typeSym.getName();
      if (typeName.size() > 0) {
        return storeType(sym.getDeclarationRefs().begin()->node,
                         zc::heap<type::NamedType>(typeName));
      }
    }
  }

  return zc::none;
}

const type::Type& BodyChecker::storeType(ast::NodeId node, zc::Own<type::Type> ty) {
  impl->typeEnv.setType(node, zc::mv(ty));
  return impl->typeEnv.getType(node);
}

const type::Type& BodyChecker::errorType() {
  if (!impl->errorTy) { impl->errorTy = zc::heap<type::ErrorType>(); }
  return *impl->errorTy;
}

static bool isAllowedRaiseType(const type::Type& errorAlt, const type::Type& raisesType) {
  if (errorAlt.equals(raisesType) || errorAlt.isSubtypeOf(raisesType)) { return true; }

  if (isUnion(raisesType)) {
    auto& raisesUnion = static_cast<const type::UnionType&>(raisesType);
    for (size_t i = 0; i < raisesUnion.getAlternativeCount(); ++i) {
      const auto& alternative = raisesUnion.getAlternative(i);
      if (errorAlt.equals(alternative) || errorAlt.isSubtypeOf(alternative)) { return true; }
    }
  }

  return false;
}

static zc::StringPtr simpleTypeName(const type::Type& ty) {
  if (isNamed(ty)) { return static_cast<const type::NamedType&>(ty).getName(); }
  if (isInterface(ty)) { return static_cast<const type::InterfaceType&>(ty).getName(); }
  return ""_zc;
}

static zc::StringPtr arithmeticOperatorTrait(ast::BinaryOperatorKind op) {
  switch (op) {
    case ast::BinaryOperatorKind::Add:
      return "Add"_zc;
    case ast::BinaryOperatorKind::Sub:
      return "Sub"_zc;
    case ast::BinaryOperatorKind::Mul:
      return "Mul"_zc;
    case ast::BinaryOperatorKind::Div:
      return "Div"_zc;
    case ast::BinaryOperatorKind::Mod:
      return "Rem"_zc;
    case ast::BinaryOperatorKind::Pow:
      return "Pow"_zc;
    default:
      return ""_zc;
  }
}

static zc::StringPtr comparisonOperatorTrait(ast::BinaryOperatorKind op) {
  switch (op) {
    case ast::BinaryOperatorKind::Eq:
    case ast::BinaryOperatorKind::Ne:
      return "Eq"_zc;
    case ast::BinaryOperatorKind::Lt:
    case ast::BinaryOperatorKind::Le:
    case ast::BinaryOperatorKind::Gt:
    case ast::BinaryOperatorKind::Ge:
      return "Ord"_zc;
    default:
      return ""_zc;
  }
}

static zc::StringPtr unaryOperatorTrait(ast::UnaryOperatorKind op) {
  switch (op) {
    case ast::UnaryOperatorKind::Minus:
      return "Neg"_zc;
    case ast::UnaryOperatorKind::LogicalNot:
      return "Not"_zc;
    default:
      return ""_zc;
  }
}

static zc::StringPtr binaryOperatorTraitMethod(zc::StringPtr traitName) {
  if (traitName == "Add"_zc) return "add"_zc;
  if (traitName == "Sub"_zc) return "sub"_zc;
  if (traitName == "Mul"_zc) return "mul"_zc;
  if (traitName == "Div"_zc) return "div"_zc;
  if (traitName == "Rem"_zc) return "rem"_zc;
  if (traitName == "Pow"_zc) return "pow"_zc;
  if (traitName == "Eq"_zc) return "eq"_zc;
  if (traitName == "Ord"_zc) return "cmp"_zc;
  return ""_zc;
}

static zc::StringPtr unaryOperatorTraitMethod(zc::StringPtr traitName) {
  if (traitName == "Neg"_zc) return "neg"_zc;
  if (traitName == "Not"_zc) return "not"_zc;
  return ""_zc;
}

static bool containsUnresolvedTypeVar(const type::Type& ty, const type::TypeEnv& env) {
  const auto& resolved = env.find(ty);
  if (isTypeVar(resolved)) { return true; }

  if (isFunction(resolved)) {
    auto& fn = static_cast<const type::FunctionType&>(resolved);
    for (size_t i = 0; i < fn.getParamCount(); ++i) {
      if (containsUnresolvedTypeVar(fn.getParamType(i), env)) return true;
    }
    if (containsUnresolvedTypeVar(fn.getReturnType(), env)) return true;
    ZC_IF_SOME(raises, fn.getRaisesType()) {
      if (containsUnresolvedTypeVar(raises, env)) return true;
    }
    return false;
  }

  if (isTuple(resolved)) {
    auto& tuple = static_cast<const type::TupleType&>(resolved);
    for (size_t i = 0; i < tuple.getElementCount(); ++i) {
      if (containsUnresolvedTypeVar(tuple.getElementType(i), env)) return true;
    }
    return false;
  }

  if (isArray(resolved)) {
    return containsUnresolvedTypeVar(static_cast<const type::ArrayType&>(resolved).getElementType(),
                                     env);
  }

  if (isNamed(resolved)) {
    auto& named = static_cast<const type::NamedType&>(resolved);
    for (size_t i = 0; i < named.getTypeArgCount(); ++i) {
      if (containsUnresolvedTypeVar(named.getTypeArg(i), env)) return true;
    }
    return false;
  }

  if (isUnion(resolved)) {
    auto& unionTy = static_cast<const type::UnionType&>(resolved);
    for (size_t i = 0; i < unionTy.getAlternativeCount(); ++i) {
      if (containsUnresolvedTypeVar(unionTy.getAlternative(i), env)) return true;
    }
    return false;
  }

  if (isReference(resolved)) {
    auto& ref = static_cast<const type::ReferenceType&>(resolved);
    return containsUnresolvedTypeVar(ref.getPointeeType(), env);
  }

  if (isRawPointer(resolved)) {
    auto& ptr = static_cast<const type::RawPointerType&>(resolved);
    return containsUnresolvedTypeVar(ptr.getPointeeType(), env);
  }

  if (isObject(resolved)) {
    auto& object = static_cast<const type::ObjectType&>(resolved);
    auto members = object.getMembers();
    for (size_t i = 0; i < members.size(); ++i) {
      ZC_IF_SOME(memberType, members[i].type) {
        if (containsUnresolvedTypeVar(memberType, env)) return true;
      }
    }
    return false;
  }

  if (isExistential(resolved)) {
    auto& existential = static_cast<const type::ExistentialType&>(resolved);
    return containsUnresolvedTypeVar(existential.getInterfaceType(), env);
  }

  if (isIntersection(resolved)) {
    auto& intersection = static_cast<const type::IntersectionType&>(resolved);
    for (size_t i = 0; i < intersection.getConjunctCount(); ++i) {
      if (containsUnresolvedTypeVar(intersection.getConjunct(i), env)) return true;
    }
    return false;
  }

  if (isAssociated(resolved)) {
    auto& associated = static_cast<const type::AssociatedType&>(resolved);
    return containsUnresolvedTypeVar(associated.getParentType(), env);
  }

  return false;
}

static zc::StringPtr simpleTypeExprName(const ast::Tree& tree, ast::NodeId typeExpr) {
  if (!tree.contains(typeExpr)) return ""_zc;
  const auto& node = tree.node(typeExpr);
  if (node.kind != ast::SyntaxKind::NamedTypeExpr) return ""_zc;

  auto pathId = ast::NodeId(node.payload.words[ast::kNamedTypeExprPathWord]);
  if (!tree.contains(pathId)) return ""_zc;
  const auto& path = tree.node(pathId);
  if (path.kind == ast::SyntaxKind::IdentExpr) {
    return tree.ident(ast::IdentId(path.payload.words[ast::kIdentExprNameWord]));
  }
  if (path.kind != ast::SyntaxKind::ModulePath) return ""_zc;

  ast::IdentList segments;
  segments.first = path.payload.words[ast::kModulePathSegmentsFirstWord];
  segments.size = path.payload.words[ast::kModulePathSegmentsSizeWord];
  auto names = tree.identList(segments);
  if (names.size() == 0) return ""_zc;
  return tree.ident(names.back());
}

static bool namedInterfaceExtends(const ast::Tree& tree, zc::StringPtr childName,
                                  zc::StringPtr parentName) {
  if (childName.size() == 0 || parentName.size() == 0) return false;
  if (childName == parentName) return true;

  const auto root = tree.root();
  if (!tree.contains(root)) return false;

  bool result = false;
  visitTreePreOrder(tree, root, [&](ast::NodeId, const ast::Node& node) {
    if (result || node.kind != ast::SyntaxKind::InterfaceDecl) return;

    auto name = tree.ident(ast::IdentId(node.payload.words[ast::kInterfaceDeclNameWord]));
    if (name != childName) return;

    auto ifacesId = ast::NodeId(node.payload.words[ast::kInterfaceDeclIfacesIdWord]);
    if (!tree.contains(ifacesId)) return;
    const auto& ifacesNode = tree.node(ifacesId);
    if (ifacesNode.kind != ast::SyntaxKind::ImplIfaceList) return;

    ast::NodeList ifaces;
    ifaces.first = ifacesNode.payload.words[ast::kImplIfaceListIfacesFirstWord];
    ifaces.size = ifacesNode.payload.words[ast::kImplIfaceListIfacesSizeWord];
    for (ast::NodeId ifaceId : tree.list(ifaces)) {
      auto parentCandidate = simpleTypeExprName(tree, ifaceId);
      if (parentCandidate.size() == 0) continue;
      if (parentCandidate == parentName ||
          namedInterfaceExtends(tree, parentCandidate, parentName)) {
        result = true;
        return;
      }
    }
  });

  return result;
}

static bool dynTypeExtends(const type::Type& sourceIface, const type::Type& targetIface,
                           const ast::Tree& tree) {
  if (sourceIface.equals(targetIface) || sourceIface.isSubtypeOf(targetIface)) { return true; }

  auto targetName = simpleTypeName(targetIface);
  if (targetName.size() == 0) return false;

  if (isIntersection(sourceIface)) {
    auto& intersection = static_cast<const type::IntersectionType&>(sourceIface);
    for (size_t i = 0; i < intersection.getConjunctCount(); ++i) {
      if (dynTypeExtends(intersection.getConjunct(i), targetIface, tree)) return true;
    }
    return false;
  }

  auto sourceName = simpleTypeName(sourceIface);
  if (sourceName.size() == 0) return false;
  return namedInterfaceExtends(tree, sourceName, targetName);
}

struct DynMethodLookup {
  zc::StringPtr interfaceName;
  zc::StringPtr methodName;
  ast::NodeId methodDecl;
  uint32_t slot = 0;
};

static zc::Maybe<DynMethodLookup> findDynInterfaceMethod(const ast::Tree& tree,
                                                         const type::Type& receiver,
                                                         zc::StringPtr methodName) {
  if (!isExistential(receiver) || methodName.size() == 0) return zc::none;

  const auto& existential = static_cast<const type::ExistentialType&>(receiver);
  auto interfaceName = simpleTypeName(existential.getInterfaceType());
  if (interfaceName.size() == 0) return zc::none;

  const auto root = tree.root();
  if (!tree.contains(root)) return zc::none;

  zc::Maybe<DynMethodLookup> result = zc::none;
  visitTreePreOrder(tree, root, [&](ast::NodeId, const ast::Node& node) {
    if (result != zc::none || node.kind != ast::SyntaxKind::InterfaceDecl) return;

    auto name = tree.ident(ast::IdentId(node.payload.words[ast::kInterfaceDeclNameWord]));
    if (name != interfaceName) return;

    auto membersId = ast::NodeId(node.payload.words[ast::kInterfaceDeclMembersIdWord]);
    if (!tree.contains(membersId)) return;
    const auto& membersNode = tree.node(membersId);
    if (membersNode.kind != ast::SyntaxKind::ClassMemberList) return;

    ast::NodeList members;
    members.first = membersNode.payload.words[ast::kClassMemberListMembersFirstWord];
    members.size = membersNode.payload.words[ast::kClassMemberListMembersSizeWord];

    uint32_t slot = 0;
    for (ast::NodeId memberId : tree.list(members)) {
      if (!tree.contains(memberId)) continue;
      const auto& member = tree.node(memberId);
      if (member.kind != ast::SyntaxKind::MethodDecl) continue;

      auto memberName = tree.ident(ast::IdentId(member.payload.words[ast::kMethodDeclNameWord]));
      if (memberName == methodName) {
        result = DynMethodLookup{interfaceName, memberName, memberId, slot};
        return;
      }
      ++slot;
    }
  });

  return result;
}

zc::Own<type::Type> BodyChecker::cloneResolvedType(const type::Type& ty) {
  const auto& resolved = impl->typeEnv.find(ty);
  return type::cloneType(resolved);
}

void BodyChecker::bindTypeVarsByName(const type::Type& ty, zc::StringPtr name,
                                     const type::Type& value) {
  const auto& resolved = impl->typeEnv.find(ty);

  if (isTypeVar(resolved)) {
    auto& var = static_cast<const type::TypeVar&>(resolved);
    if (var.getName() == name) { impl->typeEnv.bind(var, cloneResolvedType(value)); }
    return;
  }

  if (isFunction(resolved)) {
    auto& fn = static_cast<const type::FunctionType&>(resolved);
    for (size_t i = 0; i < fn.getParamCount(); ++i) {
      bindTypeVarsByName(fn.getParamType(i), name, value);
    }
    bindTypeVarsByName(fn.getReturnType(), name, value);
    ZC_IF_SOME(raises, fn.getRaisesType()) { bindTypeVarsByName(raises, name, value); }
    return;
  }

  if (isTuple(resolved)) {
    auto& tuple = static_cast<const type::TupleType&>(resolved);
    for (size_t i = 0; i < tuple.getElementCount(); ++i) {
      bindTypeVarsByName(tuple.getElementType(i), name, value);
    }
    return;
  }

  if (isArray(resolved)) {
    auto& arr = static_cast<const type::ArrayType&>(resolved);
    bindTypeVarsByName(arr.getElementType(), name, value);
    return;
  }

  if (isReference(resolved)) {
    auto& ref = static_cast<const type::ReferenceType&>(resolved);
    bindTypeVarsByName(ref.getPointeeType(), name, value);
    return;
  }

  if (isRawPointer(resolved)) {
    auto& ptr = static_cast<const type::RawPointerType&>(resolved);
    bindTypeVarsByName(ptr.getPointeeType(), name, value);
    return;
  }

  if (isUnion(resolved)) {
    auto& unionTy = static_cast<const type::UnionType&>(resolved);
    for (size_t i = 0; i < unionTy.getAlternativeCount(); ++i) {
      bindTypeVarsByName(unionTy.getAlternative(i), name, value);
    }
    return;
  }

  if (isObject(resolved)) {
    auto& object = static_cast<const type::ObjectType&>(resolved);
    auto members = object.getMembers();
    for (size_t i = 0; i < members.size(); ++i) {
      ZC_IF_SOME(memberType, members[i].type) { bindTypeVarsByName(memberType, name, value); }
    }
    return;
  }

  if (isInterface(resolved)) {
    auto& iface = static_cast<const type::InterfaceType&>(resolved);
    for (size_t i = 0; i < iface.getParentInterfaceCount(); ++i) {
      bindTypeVarsByName(iface.getParentInterface(i), name, value);
    }
    return;
  }

  if (isIntersection(resolved)) {
    auto& intersection = static_cast<const type::IntersectionType&>(resolved);
    for (size_t i = 0; i < intersection.getConjunctCount(); ++i) {
      bindTypeVarsByName(intersection.getConjunct(i), name, value);
    }
    return;
  }

  if (isExistential(resolved)) {
    auto& existential = static_cast<const type::ExistentialType&>(resolved);
    bindTypeVarsByName(existential.getInterfaceType(), name, value);
    return;
  }

  if (isAssociated(resolved)) {
    auto& associated = static_cast<const type::AssociatedType&>(resolved);
    bindTypeVarsByName(associated.getParentType(), name, value);
    return;
  }
}

zc::Own<type::Type> BodyChecker::makePrimitiveType(zc::StringPtr name) {
  using type::PrimitiveKind;
  using type::PrimitiveType;

  if (name == "i8"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::I8);
  if (name == "i16"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::I16);
  if (name == "i32"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::I32);
  if (name == "i64"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::I64);
  if (name == "u8"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::U8);
  if (name == "u16"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::U16);
  if (name == "u32"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::U32);
  if (name == "u64"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::U64);
  if (name == "f32"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::F32);
  if (name == "f64"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::F64);
  if (name == "bool"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::Bool);
  if (name == "str"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::Str);
  if (name == "char"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::Char);
  if (name == "null"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::Null);
  if (name == "unit"_zc || name == "void"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::Unit);
  if (name == "never"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::Never);
  if (name == "any"_zc) return zc::heap<PrimitiveType>(PrimitiveKind::Any);
  return zc::Own<type::Type>();
}

zc::Own<type::Type> BodyChecker::resolveTypeExpr(ast::NodeId tyExpr) {
  if (!impl->tree.contains(tyExpr)) return zc::Own<type::Type>();
  const auto& node = impl->tree.node(tyExpr);

  if (node.kind == SyntaxKind::PredefinedTypeExpr) {
    auto kind = static_cast<type::PrimitiveKind>(node.payload.words[kPredefinedTypeExprKindWord]);
    return zc::heap<type::PrimitiveType>(kind);
  }

  if (node.kind == SyntaxKind::NamedTypeExpr) {
    auto pathId = NodeId(node.payload.words[kNamedTypeExprPathWord]);
    if (!impl->tree.contains(pathId)) return zc::Own<type::Type>();

    auto& pathNode = impl->tree.node(pathId);
    zc::StringPtr typeName;

    if (pathNode.kind == SyntaxKind::IdentExpr) {
      // Simple type name: e.g., `i32`, `MyClass`
      typeName = impl->tree.ident(IdentId(pathNode.payload.words[kIdentExprNameWord]));
    } else if (pathNode.kind == SyntaxKind::ModulePath) {
      // Qualified type name: e.g., `foo::Bar`
      auto segments = modulePathSegments(pathNode);
      auto segIds = impl->tree.identList(segments);
      NodeList argsList;
      argsList.first = node.payload.words[kNamedTypeExprArgsFirstWord];
      argsList.size = node.payload.words[kNamedTypeExprArgsSizeWord];
      if (segIds.size() == 2 && argsList.empty()) {
        auto baseName = impl->tree.ident(segIds[0]);
        auto assocName = impl->tree.ident(segIds[1]);
        auto baseSym = lookupSymbol(baseName);
        ZC_IF_SOME(base, baseSym) {
          if (base.isTypeSymbol()) {
            auto baseType = zc::heap<type::NamedType>(baseName);
            baseType->setSymbol(static_cast<const symbol::TypeSymbol&>(base));
            TraitResolver resolver(impl->typeEnv, impl->symbols, impl->tree, impl->metadata,
                                   impl->diags);
            auto result = resolver.resolveAssociatedTypeWithStatus(*baseType, assocName);
            if (result.kind == AssociatedTypeResolutionKind::Resolved) {
              ZC_IF_SOME(resolved, result.type) { return cloneResolvedType(resolved); }
            }

            if (result.kind == AssociatedTypeResolutionKind::Ambiguous) {
              auto baseTypeText = baseType->toString();
              impl->diags.diagnose<DiagID::AmbiguousAssociatedTypeProjection>(
                  node.range.getStart(), assocName, baseTypeText.asPtr(), baseTypeText.asPtr(),
                  assocName);
              impl->hadErrors = true;
              return zc::heap<type::ErrorType>("ambiguous associated type projection");
            }

            impl->diags.diagnose<DiagID::NoAssociatedTypeProjection>(
                node.range.getStart(), assocName, baseType->toString());
            impl->hadErrors = true;
            return zc::heap<type::ErrorType>("missing associated type projection");
          }
        }
      }
      if (segIds.size() > 0) { typeName = impl->tree.ident(segIds.back()); }
    }

    if (typeName.size() > 0) {
      // Check if it's a primitive type
      auto primTy = makePrimitiveType(typeName);
      if (primTy) return zc::mv(primTy);
      // Otherwise return a NamedType (user-defined type)
      return zc::heap<type::NamedType>(typeName);
    }
  }

  if (node.kind == SyntaxKind::UnionTypeExpr) {
    NodeList altsList;
    altsList.first = node.payload.words[kUnionTypeExprAltsFirstWord];
    altsList.size = node.payload.words[kUnionTypeExprAltsSizeWord];

    zc::Vector<zc::Own<type::Type>> alternatives;
    for (NodeId altId : impl->tree.list(altsList)) {
      auto altTy = resolveTypeExpr(altId);
      if (!altTy) { return zc::Own<type::Type>(); }
      alternatives.add(zc::mv(altTy));
    }

    if (alternatives.empty()) { return zc::heap<type::PrimitiveType>(type::PrimitiveKind::Never); }
    return zc::heap<type::UnionType>(zc::mv(alternatives));
  }

  if (node.kind == SyntaxKind::OptionalTypeExpr) {
    auto innerId = NodeId(node.payload.words[kOptionalTypeExprInnerWord]);
    auto innerType = resolveTypeExpr(innerId);
    if (!innerType) { return zc::Own<type::Type>(); }

    zc::Vector<zc::Own<type::Type>> alternatives;
    alternatives.add(zc::mv(innerType));
    alternatives.add(type::PrimitiveType::createNull());
    return zc::heap<type::UnionType>(zc::mv(alternatives));
  }

  if (node.kind == SyntaxKind::DynTypeExpr) {
    auto ifacesId = NodeId(node.payload.words[kDynTypeExprIfacesIdWord]);
    auto markerNames = dynMarkerNames(impl->tree, node);
    if (!impl->tree.contains(ifacesId)) {
      return zc::heap<type::ErrorType>("dyn type requires at least one interface");
    }

    const auto& ifaceListNode = impl->tree.node(ifacesId);
    if (ifaceListNode.kind != SyntaxKind::DynTypeIfaceList) {
      auto ifaceType = resolveTypeExpr(ifacesId);
      if (!ifaceType) { return zc::heap<type::ErrorType>(); }
      return zc::heap<type::ExistentialType>(zc::mv(ifaceType), markerNames.asPtr());
    }

    NodeList ifaceNodeList;
    ifaceNodeList.first = ifaceListNode.payload.words[kDynTypeIfaceListIfacesFirstWord];
    ifaceNodeList.size = ifaceListNode.payload.words[kDynTypeIfaceListIfacesSizeWord];
    auto ifaces = impl->tree.list(ifaceNodeList);
    if (ifaces.size() == 0) {
      return zc::heap<type::ErrorType>("dyn type requires at least one interface");
    }
    if (ifaces.size() == 1) {
      auto ifaceType = resolveTypeExpr(ifaces.front());
      if (!ifaceType) { return zc::heap<type::ErrorType>(); }
      return zc::heap<type::ExistentialType>(zc::mv(ifaceType), markerNames.asPtr());
    }

    zc::Vector<zc::Own<type::Type>> conjuncts;
    for (NodeId ifaceId : ifaces) {
      auto ifaceType = resolveTypeExpr(ifaceId);
      if (!ifaceType) { return zc::heap<type::ErrorType>(); }
      conjuncts.add(zc::mv(ifaceType));
    }
    zc::Own<type::Type> intersection = zc::heap<type::IntersectionType>(zc::mv(conjuncts));
    return zc::heap<type::ExistentialType>(zc::mv(intersection), markerNames.asPtr());
  }

  if (node.kind == SyntaxKind::ReferenceTypeExpr) {
    auto elemId = NodeId(node.payload.words[kReferenceTypeExprElemWord]);
    auto elemType = resolveTypeExpr(elemId);
    if (!elemType) { return zc::Own<type::Type>(); }
    auto mutability = node.payload.words[kReferenceTypeExprIsMutWord] != 0
                          ? type::Mutability::Mutable
                          : type::Mutability::Const;
    return zc::heap<type::ReferenceType>(zc::mv(elemType), mutability);
  }

  if (node.kind == SyntaxKind::RawPointerTypeExpr) {
    auto elemId = NodeId(node.payload.words[kRawPointerTypeExprElemWord]);
    auto elemType = resolveTypeExpr(elemId);
    if (!elemType) { return zc::Own<type::Type>(); }
    auto mutability = node.payload.words[kRawPointerTypeExprIsMutWord] != 0
                          ? type::Mutability::Mutable
                          : type::Mutability::Const;
    return zc::heap<type::RawPointerType>(zc::mv(elemType), mutability);
  }

  if (node.kind == SyntaxKind::UnaryExpression) {
    auto op = static_cast<ast::UnaryOperatorKind>(node.payload.words[kUnaryExpressionOpWord]);
    auto operandId = NodeId(node.payload.words[kUnaryExpressionOperandWord]);

    if (op == ast::UnaryOperatorKind::Deref) {
      auto mutability = type::Mutability::Const;
      if (impl->tree.contains(operandId)) {
        const auto& operand = impl->tree.node(operandId);
        if (operand.kind == SyntaxKind::NamedTypeExpr) {
          auto pathId = NodeId(operand.payload.words[kNamedTypeExprPathWord]);
          zc::StringPtr name;
          if (impl->tree.contains(pathId)) {
            const auto& path = impl->tree.node(pathId);
            if (path.kind == SyntaxKind::IdentExpr) {
              name = impl->tree.ident(IdentId(path.payload.words[kIdentExprNameWord]));
            }
          }
          if (name == "mut"_zc || name == "const"_zc) {
            if (name == "mut"_zc) { mutability = type::Mutability::Mutable; }
            NodeList args;
            args.first = operand.payload.words[kNamedTypeExprArgsFirstWord];
            args.size = operand.payload.words[kNamedTypeExprArgsSizeWord];
            if (!args.empty()) {
              auto pointee = resolveTypeExpr(impl->tree.list(args).front());
              if (!pointee) { return zc::Own<type::Type>(); }
              return zc::heap<type::RawPointerType>(zc::mv(pointee), mutability);
            }
          }
        }
      }
      auto innerType = resolveTypeExpr(operandId);
      if (!innerType) { return zc::Own<type::Type>(); }
      return zc::heap<type::RawPointerType>(zc::mv(innerType), type::Mutability::Const);
    }
    if (op == ast::UnaryOperatorKind::Ref) {
      auto mutability = type::Mutability::Const;
      if (impl->tree.contains(operandId)) {
        const auto& operand = impl->tree.node(operandId);
        if (operand.kind == SyntaxKind::NamedTypeExpr) {
          auto pathId = NodeId(operand.payload.words[kNamedTypeExprPathWord]);
          zc::StringPtr name;
          if (impl->tree.contains(pathId)) {
            const auto& path = impl->tree.node(pathId);
            if (path.kind == SyntaxKind::IdentExpr) {
              name = impl->tree.ident(IdentId(path.payload.words[kIdentExprNameWord]));
            }
          }
          if (name == "mut"_zc) {
            mutability = type::Mutability::Mutable;
            NodeList args;
            args.first = operand.payload.words[kNamedTypeExprArgsFirstWord];
            args.size = operand.payload.words[kNamedTypeExprArgsSizeWord];
            if (!args.empty()) {
              auto pointee = resolveTypeExpr(impl->tree.list(args).front());
              if (!pointee) { return zc::Own<type::Type>(); }
              return zc::heap<type::ReferenceType>(zc::mv(pointee), mutability);
            }
          }
        }
      }
      auto innerType = resolveTypeExpr(operandId);
      if (!innerType) { return zc::Own<type::Type>(); }
      return zc::heap<type::ReferenceType>(zc::mv(innerType), type::Mutability::Const);
    }
  }

  return zc::Own<type::Type>();
}

bool BodyChecker::validateSingleParamTraitMethod(ast::NodeId expr, zc::StringPtr traitName,
                                                 zc::StringPtr methodName,
                                                 const type::Type& selfType,
                                                 const type::Type& paramType,
                                                 const type::Type& returnType) {
  TraitResolver traitResolver(impl->typeEnv, impl->symbols, impl->tree, impl->metadata,
                              impl->diags);
  traitResolver.discoverImpls();

  ZC_IF_SOME(implNodeId, traitResolver.findImpl(selfType, traitName)) {
    if (!impl->tree.contains(implNodeId)) { return false; }
    const auto& implNode = impl->tree.node(implNodeId);
    if (implNode.kind != SyntaxKind::StandaloneImplDecl) { return false; }

    auto membersId = NodeId(implNode.payload.words[kStandaloneImplDeclMembersIdWord]);
    if (!impl->tree.contains(membersId)) { return false; }

    const auto& membersNode = impl->tree.node(membersId);
    if (membersNode.kind != SyntaxKind::ClassMemberList) { return false; }

    NodeList members;
    members.first = membersNode.payload.words[kClassMemberListMembersFirstWord];
    members.size = membersNode.payload.words[kClassMemberListMembersSizeWord];
    for (NodeId memberId : impl->tree.list(members)) {
      if (!impl->tree.contains(memberId)) { continue; }
      const auto& member = impl->tree.node(memberId);
      if (member.kind != SyntaxKind::MethodDecl) { continue; }

      auto memberName = impl->tree.ident(IdentId(member.payload.words[kMethodDeclNameWord]));
      if (memberName != methodName) { continue; }

      auto paramsId = NodeId(member.payload.words[kMethodDeclParamsIdWord]);
      if (!impl->tree.contains(paramsId)) { return false; }
      const auto& paramsNode = impl->tree.node(paramsId);
      if (paramsNode.kind != SyntaxKind::FunctionParameterList) { return false; }

      NodeList params;
      params.first = paramsNode.payload.words[kFunctionParameterListParamsFirstWord];
      params.size = paramsNode.payload.words[kFunctionParameterListParamsSizeWord];
      auto paramIds = impl->tree.list(params);
      if (paramIds.size() != 1) { return false; }

      const auto& paramNode = impl->tree.node(paramIds[0]);
      if (paramNode.kind != SyntaxKind::FunctionParameterDecl) { return false; }
      auto paramTyId = NodeId(paramNode.payload.words[kFunctionParameterDeclTyWord]);
      auto paramTy = resolveTypeExpr(paramTyId);
      if (!paramTy) { return false; }
      if (!impl->typeEnv.find(*paramTy).equals(paramType)) { return false; }

      auto retTyId = NodeId(member.payload.words[kMethodDeclRetTyWord]);
      auto retTy = resolveTypeExpr(retTyId);
      if (!retTy) { return false; }
      return impl->typeEnv.find(*retTy).equals(returnType);
    }
  }

  return false;
}

bool BodyChecker::validateUnaryOperatorTrait(ast::NodeId expr, zc::StringPtr traitName,
                                             zc::StringPtr methodName,
                                             const type::Type& operandType,
                                             const type::Type& returnType) {
  TraitResolver traitResolver(impl->typeEnv, impl->symbols, impl->tree, impl->metadata,
                              impl->diags);
  traitResolver.discoverImpls();

  ZC_IF_SOME(implNodeId, traitResolver.findImpl(operandType, traitName)) {
    if (!impl->tree.contains(implNodeId)) { return false; }
    const auto& implNode = impl->tree.node(implNodeId);
    if (implNode.kind != SyntaxKind::StandaloneImplDecl) { return false; }

    auto membersId = NodeId(implNode.payload.words[kStandaloneImplDeclMembersIdWord]);
    if (!impl->tree.contains(membersId)) { return false; }

    const auto& membersNode = impl->tree.node(membersId);
    if (membersNode.kind != SyntaxKind::ClassMemberList) { return false; }

    NodeList members;
    members.first = membersNode.payload.words[kClassMemberListMembersFirstWord];
    members.size = membersNode.payload.words[kClassMemberListMembersSizeWord];
    for (NodeId memberId : impl->tree.list(members)) {
      if (!impl->tree.contains(memberId)) { continue; }
      const auto& member = impl->tree.node(memberId);
      if (member.kind != SyntaxKind::MethodDecl) { continue; }

      auto memberName = impl->tree.ident(IdentId(member.payload.words[kMethodDeclNameWord]));
      if (memberName != methodName) { continue; }

      auto paramsId = NodeId(member.payload.words[kMethodDeclParamsIdWord]);
      if (impl->tree.contains(paramsId)) {
        const auto& paramsNode = impl->tree.node(paramsId);
        if (paramsNode.kind != SyntaxKind::FunctionParameterList) { return false; }
        if (paramsNode.payload.words[kFunctionParameterListParamsSizeWord] != 0) { return false; }
      }

      auto retTyId = NodeId(member.payload.words[kMethodDeclRetTyWord]);
      auto retTy = resolveTypeExpr(retTyId);
      if (!retTy) { return false; }
      return impl->typeEnv.find(*retTy).equals(returnType);
    }
  }

  return false;
}

const type::Type& BodyChecker::expectedReturnType() const {
  ZC_IF_SOME(rt, impl->expectedRetType) { return rt; }
  // Default: unit type for functions without explicit return type
  static type::PrimitiveType unitTy(type::PrimitiveKind::Unit);
  return unitTy;
}

void BodyChecker::setExpectedReturnType(const type::Type& ty) { impl->expectedRetType = ty; }

void BodyChecker::checkAssignable(const type::Type& target, const type::Type& source,
                                  ast::NodeId node, ast::NodeId coercionSite) {
  if (!coercionSite) { coercionSite = node; }

  // Resolve both types
  const auto& resolvedTarget = impl->typeEnv.find(target);
  const auto& resolvedSource = impl->typeEnv.find(source);

  // Error types are always assignable (prevents cascading errors)
  if (isError(resolvedTarget) || isError(resolvedSource)) return;

  impl->constraints.addSub(impl->typeEnv.internType(resolvedSource),
                           impl->typeEnv.internType(resolvedTarget),
                           zc::str("assignability check"));

  // Try unification for type variables
  if (isTypeVar(resolvedTarget) || isTypeVar(resolvedSource)) {
    if (impl->unifier.unify(resolvedTarget, resolvedSource)) return;
  }

  if (isExistential(resolvedTarget)) {
    auto& existential = static_cast<const type::ExistentialType&>(resolvedTarget);
    auto& ifaceType = impl->typeEnv.find(existential.getInterfaceType());
    zc::StringPtr ifaceName;
    if (isNamed(ifaceType)) {
      ifaceName = static_cast<const type::NamedType&>(ifaceType).getName();
    } else if (isInterface(ifaceType)) {
      ifaceName = static_cast<const type::InterfaceType&>(ifaceType).getName();
    }
    if (ifaceName.size() > 0) {
      TraitResolver traitResolver(impl->typeEnv, impl->symbols, impl->tree, impl->metadata,
                                  impl->diags);
      traitResolver.discoverImpls();
      if (traitResolver.implements(resolvedSource, ifaceName)) {
        for (size_t i = 0; i < existential.getMarkerCount(); ++i) {
          auto markerName = existential.getMarkerName(i);
          if (!traitResolver.implements(resolvedSource, markerName)) {
            auto loc = getNodeLoc(impl->tree, node);
            impl->diags.diagnose<DiagID::CheckerTraitNotImplemented>(loc, resolvedSource.toString(),
                                                                     markerName);
            impl->hadErrors = true;
            return;
          }
        }
        impl->typeEnv.setCoercion(coercionSite, type::CoercionKind::ExistentialErasure);
        return;
      }
    }
  }

  auto coercion = impl->coercions.check(resolvedSource, resolvedTarget);
  if (coercion.success) {
    if (coercion.kind != type::CoercionKind::Identity) {
      impl->typeEnv.setCoercion(coercionSite, coercion.kind);
    }
    return;
  }

  reportTypeMismatch(node, resolvedTarget, resolvedSource);
}

// ============================================================================
// Expression type inference
// ============================================================================

const type::Type& BodyChecker::checkExpr(ast::NodeId expr) {
  if (!impl->tree.contains(expr)) return errorType();

  // Return cached type if available
  if (impl->typeEnv.hasType(expr)) { return impl->typeEnv.getType(expr); }

  const auto& node = impl->tree.node(expr);

  switch (node.kind) {
    case SyntaxKind::IdentExpr:
      return checkIdentExpr(expr);
    case SyntaxKind::IntLiteral:
      return checkLiteral(expr);
    case SyntaxKind::FloatLiteralExpr:
      return checkLiteral(expr);
    case SyntaxKind::StrLiteral:
      return checkLiteral(expr);
    case SyntaxKind::BoolLiteral:
      return checkLiteral(expr);
    case SyntaxKind::NullLiteral:
      return checkLiteral(expr);
    case SyntaxKind::UnitLiteral:
      return checkLiteral(expr);
    case SyntaxKind::BigIntLiteral:
      return checkLiteral(expr);
    case SyntaxKind::BinaryExpr:
      return checkBinaryExpr(expr);
    case SyntaxKind::UnaryExpression:
      return checkUnaryExpr(expr);
    case SyntaxKind::PostfixExpression:
      return checkPostfixExpr(expr);
    case SyntaxKind::CallExpression:
      return checkCallExpr(expr);
    case SyntaxKind::MemberExpression:
      return checkMemberExpr(expr);
    case SyntaxKind::IndexExpression:
      return checkIndexExpr(expr);
    case SyntaxKind::NewExpression:
      return checkNewExpr(expr);
    case SyntaxKind::CastExpression:
      return checkCastExpr(expr);
    case SyntaxKind::ConditionalExpr:
      return checkConditionalExpr(expr);
    case SyntaxKind::AssignmentExpr:
      return checkAssignmentExpr(expr);
    case SyntaxKind::LambdaExpression:
      return checkLambdaExpr(expr);
    case SyntaxKind::FunctionExpression:
      return checkFunctionExpr(expr);
    case SyntaxKind::ObjectLiteralExpr:
      return checkObjectLiteral(expr);
    case SyntaxKind::StructLiteralExpr:
      return checkStructLiteralExpr(expr);
    case SyntaxKind::ArrayLiteral:
      return checkArrayLiteral(expr);
    case SyntaxKind::TupleLiteral:
      return checkTupleLiteral(expr);
    case SyntaxKind::TupleLiteral1:
      return checkTupleLiteral(expr);
    case SyntaxKind::IsExpression:
      return checkIsExpr(expr);
    case SyntaxKind::ThisExpr:
      return checkThisExpr(expr);
    case SyntaxKind::SuperExpr:
      return checkSuperExpr(expr);
    case SyntaxKind::UnsafeBlockExpr: {
      auto bodyId = NodeId(node.payload.words[kUnsafeBlockExprBodyWord]);
      ++impl->unsafeDepth;
      checkBlockStmt(bodyId);
      --impl->unsafeDepth;
      return storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::Unit));
    }
    case SyntaxKind::CommaExpr: {
      // Comma expression: evaluate all, return last
      NodeList elems;
      elems.first = node.payload.words[kCommaExprElemsFirstWord];
      elems.size = node.payload.words[kCommaExprElemsSizeWord];
      const type::Type* lastType = &errorType();
      for (NodeId subId : impl->tree.list(elems)) { lastType = &checkExpr(subId); }
      return *lastType;
    }
    case SyntaxKind::NullCoalesceExpr: {
      auto lhsId = NodeId(node.payload.words[kNullCoalesceExprPrimaryWord]);
      auto rhsId = NodeId(node.payload.words[kNullCoalesceExprFallbackWord]);
      auto& lhsType = checkExpr(lhsId);
      auto& rhsType = checkExpr(rhsId);
      auto& resolvedLhs = impl->typeEnv.find(lhsType);
      auto& resolvedRhs = impl->typeEnv.find(rhsType);
      if (isError(resolvedLhs) || isError(resolvedRhs)) {
        return storeType(expr, zc::heap<type::ErrorType>());
      }

      if (isNull(resolvedLhs)) { return storeType(expr, cloneResolvedType(resolvedRhs)); }

      if (isUnion(resolvedLhs)) {
        auto& unionTy = static_cast<const type::UnionType&>(resolvedLhs);
        if (unionTy.isNullable()) {
          zc::Vector<zc::Own<type::Type>> nonNullAlternatives;
          for (size_t i = 0; i < unionTy.getAlternativeCount(); ++i) {
            auto& alt = unionTy.getAlternative(i);
            if (!isNull(alt)) { nonNullAlternatives.add(cloneResolvedType(alt)); }
          }

          if (nonNullAlternatives.empty()) {
            return storeType(expr, cloneResolvedType(resolvedRhs));
          }

          zc::Own<type::Type> nonNullType;
          if (nonNullAlternatives.size() == 1) {
            nonNullType = zc::mv(nonNullAlternatives[0]);
          } else {
            nonNullType = zc::heap<type::UnionType>(zc::mv(nonNullAlternatives));
          }

          auto& resolvedNonNull = impl->typeEnv.find(*nonNullType);
          checkAssignable(resolvedNonNull, resolvedRhs, rhsId);
          if (impl->hadErrors) { return storeType(expr, zc::heap<type::ErrorType>()); }
          return storeType(expr, zc::mv(nonNullType));
        }
      }

      return storeType(expr, cloneResolvedType(resolvedLhs));
    }
    case SyntaxKind::ErrorDefaultExpr:
      // `error(...)` - returns error type
      return storeType(expr, zc::heap<type::ErrorType>());
    default:
      // Unknown expression kind - return error type
      return storeType(expr, zc::heap<type::ErrorType>());
  }
}

const type::Type& BodyChecker::checkIdentExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  auto nameId = IdentId(node.payload.words[kIdentExprNameWord]);
  auto name = impl->tree.ident(nameId);

  auto symResult = lookupSymbol(name);
  if (symResult == zc::none) {
    auto loc = getNodeLoc(impl->tree, expr);
    impl->diags.diagnose<DiagID::UndeclaredValue>(loc, name);
    impl->hadErrors = true;
    return storeType(expr, zc::heap<type::ErrorType>());
  }

  ZC_IF_SOME(sym, symResult) {
    auto declRefs = sym.getDeclarationRefs();
    if (declRefs.size() > 0) { impl->identExprDeclarations.upsert(expr.value, declRefs[0].node); }

    auto ty = getSymbolType(sym);
    ZC_IF_SOME(t, ty) {
      // Found the symbol's type. Store a cloned copy for this ident expression.
      return storeType(expr, cloneResolvedType(t));
    }
  }

  // If we can't determine the type, return error
  return storeType(expr, zc::heap<type::ErrorType>());
}

const type::Type& BodyChecker::checkLiteral(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);

  switch (node.kind) {
    case SyntaxKind::IntLiteral:
      return storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::I32));
    case SyntaxKind::FloatLiteralExpr:
      return storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::F64));
    case SyntaxKind::StrLiteral:
      return storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::Str));
    case SyntaxKind::BoolLiteral:
      return storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::Bool));
    case SyntaxKind::NullLiteral:
      return storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::Null));
    case SyntaxKind::UnitLiteral:
      return storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::Unit));
    case SyntaxKind::BigIntLiteral:
      return storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::I64));
    default:
      return storeType(expr, zc::heap<type::ErrorType>());
  }
}

const type::Type& BodyChecker::checkBinaryExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  auto op = static_cast<ast::BinaryOperatorKind>(node.payload.words[kBinaryExprOpWord]);
  auto lhsId = NodeId(node.payload.words[kBinaryExprLhsWord]);
  auto rhsId = NodeId(node.payload.words[kBinaryExprRhsWord]);

  auto& lhsType = checkExpr(lhsId);
  auto& rhsType = checkExpr(rhsId);

  auto& resolvedLhs = impl->typeEnv.find(lhsType);
  auto& resolvedRhs = impl->typeEnv.find(rhsType);

  (void)resolvedRhs;

  // Helper: get primitive kind from resolved type
  auto getPrimKind = [](const type::Type& t) -> type::PrimitiveKind {
    if (isPrimitive(t)) { return static_cast<const type::PrimitiveType&>(t).getPrimitiveKind(); }
    return type::PrimitiveKind::I32;
  };

  // Helper: check if type is str
  auto isStrType = [](const type::Type& t) -> bool {
    if (isPrimitive(t)) {
      return static_cast<const type::PrimitiveType&>(t).getPrimitiveKind() ==
             type::PrimitiveKind::Str;
    }
    return false;
  };

  // Determine result type based on operator
  switch (op) {
    case ast::BinaryOperatorKind::Add:
    case ast::BinaryOperatorKind::Sub:
    case ast::BinaryOperatorKind::Mul:
    case ast::BinaryOperatorKind::Div:
    case ast::BinaryOperatorKind::Mod:
    case ast::BinaryOperatorKind::Pow: {
      // Arithmetic: result is the wider numeric type
      // Simplified: return LHS type (should unify with RHS)
      if (isNumeric(resolvedLhs) && isNumeric(resolvedRhs)) {
        auto unified = impl->unifier.tryUnify(resolvedLhs, resolvedRhs);
        if (!unified.success) {
          if (unified.failureKind ==
              type::UnificationEngine::UnifyResult::FailureKind::InfiniteType) {
            reportInfiniteType(expr, unified.errorMsg);
          } else {
            reportCannotUnify(expr, resolvedLhs, resolvedRhs, "binary arithmetic operator"_zc);
          }
          return storeType(expr, zc::heap<type::ErrorType>());
        }
        auto& result = storeType(expr, zc::heap<type::PrimitiveType>(getPrimKind(resolvedLhs)));
        zc::Vector<type::TypeId> args;
        args.add(impl->typeEnv.internType(resolvedLhs));
        args.add(impl->typeEnv.internType(resolvedRhs));
        recordPrimitiveDispatch(impl->typeEnv, expr, type::ReceiverMode::OperatorLeftHandSide,
                                args.asPtr(), result);
        return result;
      }
      if (isStrType(resolvedLhs) && op == ast::BinaryOperatorKind::Add) {
        auto& result = storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::Str));
        zc::Vector<type::TypeId> args;
        args.add(impl->typeEnv.internType(resolvedLhs));
        args.add(impl->typeEnv.internType(resolvedRhs));
        recordPrimitiveDispatch(impl->typeEnv, expr, type::ReceiverMode::OperatorLeftHandSide,
                                args.asPtr(), result);
        return result;
      }
      auto traitName = arithmeticOperatorTrait(op);
      if (traitName.size() > 0 && resolvedLhs.equals(resolvedRhs) && isNamed(resolvedLhs)) {
        TraitResolver traitResolver(impl->typeEnv, impl->symbols, impl->tree, impl->metadata,
                                    impl->diags);
        traitResolver.discoverImpls();
        if (traitResolver.implements(resolvedLhs, traitName)) {
          auto methodName = binaryOperatorTraitMethod(traitName);
          if (!validateSingleParamTraitMethod(expr, traitName, methodName, resolvedLhs, resolvedRhs,
                                              resolvedLhs)) {
            auto loc = getNodeLoc(impl->tree, expr);
            impl->diags.diagnose<DiagID::OperatorTraitSignatureMismatch>(
                loc, traitName, resolvedLhs.toString(), methodName, resolvedRhs.toString(),
                resolvedLhs.toString());
            impl->hadErrors = true;
            return storeType(expr, zc::heap<type::ErrorType>());
          }
          auto& result = storeType(expr, cloneResolvedType(resolvedLhs));
          ZC_IF_SOME(implNode, traitResolver.findImpl(resolvedLhs, traitName)) {
            type::CallDispatchRecord record;
            record.targetKind = type::CallTargetKind::OperatorMethod;
            record.receiverMode = type::ReceiverMode::OperatorLeftHandSide;
            record.interfaceName = zc::str(traitName);
            record.methodName = zc::str(methodName);
            record.implNode = implNode;
            record.argumentTypes.add(impl->typeEnv.internType(resolvedLhs));
            record.argumentTypes.add(impl->typeEnv.internType(resolvedRhs));
            record.resultType = impl->typeEnv.internType(result);
            impl->typeEnv.setDispatch(expr, zc::mv(record));
          }
          return result;
        }
        auto loc = getNodeLoc(impl->tree, expr);
        impl->diags.diagnose<DiagID::CheckerTraitNotImplemented>(loc, resolvedLhs.toString(),
                                                                 traitName);
        impl->hadErrors = true;
        return storeType(expr, zc::heap<type::ErrorType>());
      }
      // If either operand is ErrorType or TypeVar, suppress cascading errors.
      if (isError(resolvedLhs) || isError(resolvedRhs) || isTypeVar(resolvedLhs) ||
          isTypeVar(resolvedRhs)) {
        return storeType(expr, zc::heap<type::ErrorType>());
      }
      auto loc = getNodeLoc(impl->tree, expr);
      impl->diags.diagnose<DiagID::InvalidBinaryOperands>(loc);
      impl->hadErrors = true;
      return storeType(expr, zc::heap<type::ErrorType>());
    }
    case ast::BinaryOperatorKind::StrictEq:
    case ast::BinaryOperatorKind::StrictNe: {
      auto& result = storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::Bool));
      zc::Vector<type::TypeId> args;
      args.add(impl->typeEnv.internType(resolvedLhs));
      args.add(impl->typeEnv.internType(resolvedRhs));
      recordPrimitiveDispatch(impl->typeEnv, expr, type::ReceiverMode::OperatorLeftHandSide,
                              args.asPtr(), result);
      return result;
    }
    case ast::BinaryOperatorKind::Eq:
    case ast::BinaryOperatorKind::Ne:
    case ast::BinaryOperatorKind::Lt:
    case ast::BinaryOperatorKind::Le:
    case ast::BinaryOperatorKind::Gt:
    case ast::BinaryOperatorKind::Ge: {
      auto traitName = comparisonOperatorTrait(op);
      if (traitName.size() > 0 && (isNamed(resolvedLhs) || isNamed(resolvedRhs))) {
        if (isError(resolvedLhs) || isError(resolvedRhs) || isTypeVar(resolvedLhs) ||
            isTypeVar(resolvedRhs)) {
          return storeType(expr, zc::heap<type::ErrorType>());
        }
        if (!resolvedLhs.equals(resolvedRhs) || !isNamed(resolvedLhs) || !isNamed(resolvedRhs)) {
          auto loc = getNodeLoc(impl->tree, expr);
          impl->diags.diagnose<DiagID::InvalidComparisonOperands>(loc);
          impl->hadErrors = true;
          return storeType(expr, zc::heap<type::ErrorType>());
        }

        TraitResolver traitResolver(impl->typeEnv, impl->symbols, impl->tree, impl->metadata,
                                    impl->diags);
        traitResolver.discoverImpls();
        if (!traitResolver.implements(resolvedLhs, traitName)) {
          auto loc = getNodeLoc(impl->tree, expr);
          impl->diags.diagnose<DiagID::CheckerTraitNotImplemented>(loc, resolvedLhs.toString(),
                                                                   traitName);
          impl->hadErrors = true;
          return storeType(expr, zc::heap<type::ErrorType>());
        }

        auto methodName = binaryOperatorTraitMethod(traitName);
        auto expectedReturn = traitName == "Eq"_zc
                                  ? zc::heap<type::PrimitiveType>(type::PrimitiveKind::Bool)
                                  : zc::heap<type::PrimitiveType>(type::PrimitiveKind::I32);
        if (!validateSingleParamTraitMethod(expr, traitName, methodName, resolvedLhs, resolvedRhs,
                                            *expectedReturn)) {
          auto loc = getNodeLoc(impl->tree, expr);
          impl->diags.diagnose<DiagID::OperatorTraitSignatureMismatch>(
              loc, traitName, resolvedLhs.toString(), methodName, resolvedRhs.toString(),
              expectedReturn->toString());
          impl->hadErrors = true;
          return storeType(expr, zc::heap<type::ErrorType>());
        }
        auto& result = storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::Bool));
        ZC_IF_SOME(implNode, traitResolver.findImpl(resolvedLhs, traitName)) {
          type::CallDispatchRecord record;
          record.targetKind = type::CallTargetKind::OperatorMethod;
          record.receiverMode = type::ReceiverMode::OperatorLeftHandSide;
          record.interfaceName = zc::str(traitName);
          record.methodName = zc::str(methodName);
          record.implNode = implNode;
          record.argumentTypes.add(impl->typeEnv.internType(resolvedLhs));
          record.argumentTypes.add(impl->typeEnv.internType(resolvedRhs));
          record.resultType = impl->typeEnv.internType(result);
          impl->typeEnv.setDispatch(expr, zc::mv(record));
        }
        return result;
      }
      auto& result = storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::Bool));
      zc::Vector<type::TypeId> args;
      args.add(impl->typeEnv.internType(resolvedLhs));
      args.add(impl->typeEnv.internType(resolvedRhs));
      recordPrimitiveDispatch(impl->typeEnv, expr, type::ReceiverMode::OperatorLeftHandSide,
                              args.asPtr(), result);
      return result;
    }
    case ast::BinaryOperatorKind::LogAnd:
    case ast::BinaryOperatorKind::LogOr:
      // Logical: always returns bool, operands must be bool
      return storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::Bool));
    case ast::BinaryOperatorKind::BitAnd:
    case ast::BinaryOperatorKind::BitOr:
    case ast::BinaryOperatorKind::BitXor:
    case ast::BinaryOperatorKind::Shl:
    case ast::BinaryOperatorKind::Shr:
    case ast::BinaryOperatorKind::UShr:
      // Bitwise: result is operand type
      return storeType(expr, zc::heap<type::PrimitiveType>(getPrimKind(resolvedLhs)));
    default:
      return storeType(expr, zc::heap<type::ErrorType>());
  }
}

const type::Type& BodyChecker::checkUnaryExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  auto operandId = NodeId(node.payload.words[kUnaryExpressionOperandWord]);
  auto& operandType = checkExpr(operandId);
  auto& resolved = impl->typeEnv.find(operandType);

  // Helper: get primitive kind from resolved type
  auto getPrimKind = [](const type::Type& t) -> type::PrimitiveKind {
    if (isPrimitive(t)) { return static_cast<const type::PrimitiveType&>(t).getPrimitiveKind(); }
    return type::PrimitiveKind::I32;
  };

  auto op = static_cast<ast::UnaryOperatorKind>(node.payload.words[kUnaryExpressionOpWord]);
  switch (op) {
    case ast::UnaryOperatorKind::Plus:
    case ast::UnaryOperatorKind::Minus:
    case ast::UnaryOperatorKind::PreIncrement:
    case ast::UnaryOperatorKind::PreDecrement:
      if (isNamed(resolved) && op == ast::UnaryOperatorKind::Minus) {
        TraitResolver traitResolver(impl->typeEnv, impl->symbols, impl->tree, impl->metadata,
                                    impl->diags);
        traitResolver.discoverImpls();
        auto traitName = unaryOperatorTrait(op);
        if (!traitResolver.implements(resolved, traitName)) {
          auto loc = getNodeLoc(impl->tree, expr);
          impl->diags.diagnose<DiagID::CheckerTraitNotImplemented>(loc, resolved.toString(),
                                                                   traitName);
          impl->hadErrors = true;
          return storeType(expr, zc::heap<type::ErrorType>());
        }
        auto methodName = unaryOperatorTraitMethod(traitName);
        if (!validateUnaryOperatorTrait(expr, traitName, methodName, resolved, resolved)) {
          auto loc = getNodeLoc(impl->tree, expr);
          impl->diags.diagnose<DiagID::OperatorTraitSignatureMismatch>(
              loc, traitName, resolved.toString(), methodName, ""_zc, resolved.toString());
          impl->hadErrors = true;
          return storeType(expr, zc::heap<type::ErrorType>());
        }
        auto& result = storeType(expr, cloneResolvedType(resolved));
        ZC_IF_SOME(implNode, traitResolver.findImpl(resolved, traitName)) {
          type::CallDispatchRecord record;
          record.targetKind = type::CallTargetKind::OperatorMethod;
          record.receiverMode = type::ReceiverMode::OperatorOperand;
          record.interfaceName = zc::str(traitName);
          record.methodName = zc::str(methodName);
          record.implNode = implNode;
          record.argumentTypes.add(impl->typeEnv.internType(resolved));
          record.resultType = impl->typeEnv.internType(result);
          impl->typeEnv.setDispatch(expr, zc::mv(record));
        }
        return result;
      }
      // Numeric unary: result is operand type
      {
        auto& result = storeType(expr, zc::heap<type::PrimitiveType>(getPrimKind(resolved)));
        zc::Vector<type::TypeId> args;
        args.add(impl->typeEnv.internType(resolved));
        recordPrimitiveDispatch(impl->typeEnv, expr, type::ReceiverMode::OperatorOperand,
                                args.asPtr(), result);
        return result;
      }
    case ast::UnaryOperatorKind::LogicalNot:
      if (isNamed(resolved)) {
        TraitResolver traitResolver(impl->typeEnv, impl->symbols, impl->tree, impl->metadata,
                                    impl->diags);
        traitResolver.discoverImpls();
        auto traitName = unaryOperatorTrait(op);
        if (!traitResolver.implements(resolved, traitName)) {
          auto loc = getNodeLoc(impl->tree, expr);
          impl->diags.diagnose<DiagID::CheckerTraitNotImplemented>(loc, resolved.toString(),
                                                                   traitName);
          impl->hadErrors = true;
          return storeType(expr, zc::heap<type::ErrorType>());
        }
        auto methodName = unaryOperatorTraitMethod(traitName);
        type::PrimitiveType boolTy(type::PrimitiveKind::Bool);
        if (!validateUnaryOperatorTrait(expr, traitName, methodName, resolved, boolTy)) {
          auto loc = getNodeLoc(impl->tree, expr);
          impl->diags.diagnose<DiagID::OperatorTraitSignatureMismatch>(
              loc, traitName, resolved.toString(), methodName, ""_zc, boolTy.toString());
          impl->hadErrors = true;
          return storeType(expr, zc::heap<type::ErrorType>());
        }
        auto& result = storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::Bool));
        ZC_IF_SOME(implNode, traitResolver.findImpl(resolved, traitName)) {
          type::CallDispatchRecord record;
          record.targetKind = type::CallTargetKind::OperatorMethod;
          record.receiverMode = type::ReceiverMode::OperatorOperand;
          record.interfaceName = zc::str(traitName);
          record.methodName = zc::str(methodName);
          record.implNode = implNode;
          record.argumentTypes.add(impl->typeEnv.internType(resolved));
          record.resultType = impl->typeEnv.internType(result);
          impl->typeEnv.setDispatch(expr, zc::mv(record));
        }
        return result;
      }
      // Logical not: returns bool
      {
        auto& result = storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::Bool));
        zc::Vector<type::TypeId> args;
        args.add(impl->typeEnv.internType(resolved));
        recordPrimitiveDispatch(impl->typeEnv, expr, type::ReceiverMode::OperatorOperand,
                                args.asPtr(), result);
        return result;
      }
    case ast::UnaryOperatorKind::BitNot:
      // Bitwise not: result is operand type
      {
        auto& result = storeType(expr, zc::heap<type::PrimitiveType>(getPrimKind(resolved)));
        zc::Vector<type::TypeId> args;
        args.add(impl->typeEnv.internType(resolved));
        recordPrimitiveDispatch(impl->typeEnv, expr, type::ReceiverMode::OperatorOperand,
                                args.asPtr(), result);
        return result;
      }
    case ast::UnaryOperatorKind::Deref: {
      // Dereference: returns the pointed-to type
      if (isReference(resolved)) {
        auto& refTy = static_cast<const type::ReferenceType&>(resolved);
        (void)refTy;
        return storeType(expr, zc::heap<type::ErrorType>());
      }
      if (isRawPointer(resolved)) {
        auto& ptrTy = static_cast<const type::RawPointerType&>(resolved);
        (void)ptrTy;
        return storeType(expr, zc::heap<type::ErrorType>());
      }
      auto loc = getNodeLoc(impl->tree, expr);
      impl->diags.diagnose<DiagID::CannotDereferenceType>(loc);
      impl->hadErrors = true;
      return storeType(expr, zc::heap<type::ErrorType>());
    }
    case ast::UnaryOperatorKind::Ref:
      // Address-of: returns a reference to the operand type
      if (isError(resolved)) { return storeType(expr, zc::heap<type::ErrorType>()); }
      return storeType(expr, zc::heap<type::ReferenceType>(cloneResolvedType(resolved),
                                                           type::Mutability::Const));
    default:
      return storeType(expr, zc::heap<type::ErrorType>());
  }
}

const type::Type& BodyChecker::checkPostfixExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  auto operandId = NodeId(node.payload.words[kPostfixExpressionOperandWord]);
  auto& operandType = checkExpr(operandId);
  auto& resolved = impl->typeEnv.find(operandType);

  auto op = static_cast<ast::PostfixOperatorKind>(node.payload.words[kPostfixExpressionOpWord]);
  switch (op) {
    case ast::PostfixOperatorKind::Increment:
    case ast::PostfixOperatorKind::Decrement:
      if (isNumeric(resolved)) { return storeType(expr, cloneResolvedType(resolved)); }
      if (!isError(resolved)) {
        auto loc = getNodeLoc(impl->tree, expr);
        impl->diags.diagnose<DiagID::PostfixUpdateRequiresNumeric>(loc);
        impl->hadErrors = true;
      }
      return storeType(expr, zc::heap<type::ErrorType>());
    case ast::PostfixOperatorKind::ErrorPropagate:
    case ast::PostfixOperatorKind::ErrorUnwrap: {
      if (isError(resolved)) { return storeType(expr, zc::heap<type::ErrorType>()); }
      if (!isUnion(resolved)) {
        if (op == ast::PostfixOperatorKind::ErrorUnwrap) {
          auto loc = getNodeLoc(impl->tree, expr);
          impl->diags.diagnose<DiagID::ErrorUnwrapNonUnion>(loc, resolved.toString());
          impl->hadErrors = true;
        } else {
          auto loc = getNodeLoc(impl->tree, expr);
          impl->diags.diagnose<DiagID::ErrorPropagateNonUnion>(loc, resolved.toString());
          impl->hadErrors = true;
        }
        return storeType(expr, zc::heap<type::ErrorType>());
      }

      const auto& unionTy = static_cast<const type::UnionType&>(resolved);
      if (unionTy.getAlternativeCount() == 0) {
        auto loc = getNodeLoc(impl->tree, expr);
        impl->diags.diagnose<DiagID::ErrorUnionEmpty>(loc);
        impl->hadErrors = true;
        return storeType(expr, zc::heap<type::ErrorType>());
      }

      if (op == ast::PostfixOperatorKind::ErrorPropagate && unionTy.getAlternativeCount() > 1) {
        const auto& errorAlt = unionTy.getAlternative(1);
        if (impl->expectedRaisesType == zc::none) {
          auto loc = getNodeLoc(impl->tree, expr);
          auto errorText = errorAlt.toString();
          impl->diags.diagnose<DiagID::ErrorPropagateOutsideRaises>(loc, errorText.asPtr(),
                                                                    errorText.asPtr());
          impl->hadErrors = true;
          return storeType(expr, zc::heap<type::ErrorType>());
        }
        ZC_IF_SOME(raisesType, impl->expectedRaisesType) {
          auto& resolvedRaises = impl->typeEnv.find(raisesType);
          if (!isAllowedRaiseType(errorAlt, resolvedRaises)) {
            auto loc = getNodeLoc(impl->tree, expr);
            auto errorText = errorAlt.toString();
            impl->diags.diagnose<DiagID::ErrorPropagateOutsideRaises>(loc, errorText.asPtr(),
                                                                      errorText.asPtr());
            impl->hadErrors = true;
            return storeType(expr, zc::heap<type::ErrorType>());
          }
        }
      }

      return storeType(expr, cloneResolvedType(unionTy.getAlternative(0)));
    }
  }

  return storeType(expr, zc::heap<type::ErrorType>());
}

const type::Type& BodyChecker::checkCallExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  auto calleeId = NodeId(node.payload.words[kCallExpressionCalleeWord]);
  auto typeArgsFirst = node.payload.words[kCallExpressionTypeArgsFirstWord];
  auto typeArgsSize = node.payload.words[kCallExpressionTypeArgsSizeWord];
  auto argsFirst = node.payload.words[kCallExpressionArgsFirstWord];
  auto argsSize = node.payload.words[kCallExpressionArgsSizeWord];

  auto& calleeType = checkExpr(calleeId);
  auto& resolvedCallee = impl->typeEnv.find(calleeType);

  symbol::SymbolId freeFunctionTarget;
  symbol::SymbolId instanceMethodTarget;
  bool isStaticMethodTarget = false;
  zc::StringPtr dynInterfaceName;
  zc::StringPtr dynMethodName;
  uint32_t dynVTableSlot = 0;
  bool isDynVTableTarget = false;
  NodeId instanceReceiverId;
  if (impl->tree.contains(calleeId) && impl->tree.node(calleeId).kind == SyntaxKind::IdentExpr) {
    const auto& calleeNode = impl->tree.node(calleeId);
    auto calleeName = impl->tree.ident(IdentId(calleeNode.payload.words[kIdentExprNameWord]));
    auto calleeSym = lookupSymbol(calleeName);
    ZC_IF_SOME(sym, calleeSym) {
      if (sym.isFunctionSymbol()) { freeFunctionTarget = sym.getId(); }
    }
  } else if (impl->tree.contains(calleeId) &&
             impl->tree.node(calleeId).kind == SyntaxKind::MemberExpression) {
    const auto& calleeNode = impl->tree.node(calleeId);
    instanceReceiverId = NodeId(calleeNode.payload.words[kMemberExpressionObjectWord]);
    ZC_IF_SOME(symbolId, impl->memberExprSymbols.find(calleeId.value)) {
      instanceMethodTarget = symbolId;
    }
    ZC_IF_SOME(isStatic, impl->memberExprIsStatic.find(calleeId.value)) {
      isStaticMethodTarget = isStatic;
    }
    ZC_IF_SOME(interfaceName, impl->memberExprDynInterfaceNames.find(calleeId.value)) {
      dynInterfaceName = interfaceName;
      isDynVTableTarget = true;
    }
    ZC_IF_SOME(methodName, impl->memberExprDynMethodNames.find(calleeId.value)) {
      dynMethodName = methodName;
    }
    ZC_IF_SOME(slot, impl->memberExprDynVTableSlots.find(calleeId.value)) { dynVTableSlot = slot; }
  }

  NodeList typeArgList;
  typeArgList.first = typeArgsFirst;
  typeArgList.size = typeArgsSize;
  zc::Vector<zc::Own<type::Type>> explicitTypeArgs;
  for (NodeId typeArgId : impl->tree.list(typeArgList)) {
    auto typeArg = resolveTypeExpr(typeArgId);
    if (!typeArg) {
      auto loc = getNodeLoc(impl->tree, typeArgId);
      impl->diags.diagnose<DiagID::UnsupportedExplicitTypeArgument>(loc);
      impl->hadErrors = true;
      return storeType(expr, zc::heap<type::ErrorType>());
    }
    explicitTypeArgs.add(zc::mv(typeArg));
  }

  // Check all arguments
  NodeList argList;
  argList.first = argsFirst;
  argList.size = argsSize;
  zc::Vector<const type::Type*> argTypes;
  for (NodeId argId : impl->tree.list(argList)) { argTypes.add(&checkExpr(argId)); }

  // If callee is a function type, validate arguments and return return type
  if (isFunction(resolvedCallee)) {
    auto& funcTy = static_cast<const type::FunctionType&>(resolvedCallee);

    // --- Let-polymorphism: instantiate generic function ---
    // If the function has generic parameters (e.g., `fn identity<T>(x: T) -> T`),
    // we need to create a fresh instance with fresh type variables for each
    // generic parameter. This allows the same function to be used at different
    // types in different call sites.
    zc::Own<type::Type> instantiatedFn;
    const type::FunctionType* effectiveFn = &funcTy;

    if (funcTy.isGeneric()) {
      // Instantiate: create fresh type vars for each generic parameter
      instantiatedFn = impl->typeEnv.instantiateFunction(funcTy);
      auto& resolved = impl->typeEnv.find(*instantiatedFn);
      if (isFunction(resolved)) { effectiveFn = &static_cast<const type::FunctionType&>(resolved); }

      if (explicitTypeArgs.size() > 0) {
        if (explicitTypeArgs.size() != funcTy.getGenericParamCount()) {
          auto loc = getNodeLoc(impl->tree, expr);
          impl->diags.diagnose<DiagID::ExplicitTypeArgumentCountMismatch>(
              loc, zc::str(funcTy.getGenericParamCount()), zc::str(explicitTypeArgs.size()));
          impl->hadErrors = true;
          return storeType(expr, zc::heap<type::ErrorType>());
        }
        for (size_t i = 0; i < explicitTypeArgs.size(); ++i) {
          auto& resolvedTypeArg = impl->typeEnv.find(*explicitTypeArgs[i]);
          auto name = funcTy.getGenericParam(i).name;
          bindTypeVarsByName(*effectiveFn, name, resolvedTypeArg);
        }
      }
    } else if (explicitTypeArgs.size() > 0) {
      auto loc = getNodeLoc(impl->tree, expr);
      impl->diags.diagnose<DiagID::ExplicitTypeArgumentsRequireGenericCallee>(loc);
      impl->hadErrors = true;
      return storeType(expr, zc::heap<type::ErrorType>());
    }

    // Check argument count
    if (argTypes.size() != effectiveFn->getParamCount()) {
      auto loc = getNodeLoc(impl->tree, expr);
      impl->diags.diagnose<DiagID::CallArgumentCountMismatch>(
          loc, zc::str(effectiveFn->getParamCount()), zc::str(argTypes.size()));
      impl->hadErrors = true;
      return storeType(expr, zc::heap<type::ErrorType>());
    }

    // Check each argument type against the (possibly instantiated) parameter types
    bool hadCallArgError = false;
    for (size_t i = 0; i < argTypes.size(); ++i) {
      auto& paramTy = effectiveFn->getParamType(i);
      auto& resolvedArg = impl->typeEnv.find(*argTypes[i]);

      auto argId = impl->tree.list(argList)[i];
      bool refinedLocal = false;
      ZC_IF_SOME(declId, impl->identExprDeclarations.find(argId.value)) {
        if (impl->typeEnv.hasType(declId)) {
          auto& declTy = impl->typeEnv.getType(declId);
          auto& resolvedDecl = impl->typeEnv.find(declTy);
          if (isTypeVar(resolvedDecl)) {
            if (impl->unifier.unify(resolvedDecl, paramTy)) {
              auto& refined = impl->typeEnv.find(paramTy);
              impl->typeEnv.setType(declId, cloneResolvedType(refined));
              impl->typeEnv.setType(argId, cloneResolvedType(refined));
              refinedLocal = true;
            }
          }
        }
      }
      if (!refinedLocal) {
        bool hadErrorsBeforeArg = impl->hadErrors;
        checkAssignable(paramTy, resolvedArg, expr, argId);
        if (!hadErrorsBeforeArg && impl->hadErrors) { hadCallArgError = true; }
      }
    }

    if (hadCallArgError) { return storeType(expr, zc::heap<type::ErrorType>()); }

    if (funcTy.isGeneric()) {
      TraitResolver traitResolver(impl->typeEnv, impl->symbols, impl->tree, impl->metadata,
                                  impl->diags);
      traitResolver.discoverImpls();

      for (size_t i = 0; i < funcTy.getGenericParamCount(); ++i) {
        auto& generic = funcTy.getGenericParam(i);
        if (generic.upperBounds.empty()) continue;

        auto typeVar = type::findTypeVarByName(*effectiveFn, generic.name);
        if (typeVar == zc::none) continue;

        ZC_IF_SOME(varTy, typeVar) {
          auto& resolvedTypeArg = impl->typeEnv.find(varTy);
          if (isTypeVar(resolvedTypeArg) || isError(resolvedTypeArg)) continue;

          for (size_t boundIndex = 0; boundIndex < generic.upperBounds.size(); ++boundIndex) {
            auto& bound = impl->typeEnv.find(*generic.upperBounds[boundIndex]);
            zc::StringPtr boundName;
            if (isNamed(bound)) {
              boundName = static_cast<const type::NamedType&>(bound).getName();
            } else if (isInterface(bound)) {
              boundName = static_cast<const type::InterfaceType&>(bound).getName();
            }

            if (boundName.size() == 0) continue;
            if (!traitResolver.implements(resolvedTypeArg, boundName)) {
              auto loc = getNodeLoc(impl->tree, expr);
              impl->diags.diagnose<DiagID::CheckerTraitNotImplemented>(
                  loc, resolvedTypeArg.toString(), boundName);
              impl->hadErrors = true;
              return storeType(expr, zc::heap<type::ErrorType>());
            }
          }
        }
      }

      for (size_t i = 0; i < funcTy.getGenericParamCount(); ++i) {
        auto& generic = funcTy.getGenericParam(i);
        auto typeVar = type::findTypeVarByName(*effectiveFn, generic.name);
        if (typeVar == zc::none) continue;

        ZC_IF_SOME(varTy, typeVar) {
          auto& resolvedTypeArg = impl->typeEnv.find(varTy);
          if (containsUnresolvedTypeVar(resolvedTypeArg, impl->typeEnv)) {
            auto loc = getNodeLoc(impl->tree, expr);
            impl->diags.diagnose<DiagID::CannotInferTypeParameter>(loc, generic.name);
            impl->hadErrors = true;
            return storeType(expr, zc::heap<type::ErrorType>());
          }
        }
      }
    }

    // Return the function result as an error union when the callee can raise.
    auto& retTy = effectiveFn->getReturnType();
    auto& resolvedRet = impl->typeEnv.find(retTy);
    auto raisesType = effectiveFn->getRaisesType();
    ZC_IF_SOME(raises, raisesType) {
      auto& resolvedRaises = impl->typeEnv.find(raises);
      zc::Vector<zc::Own<type::Type>> alternatives;
      alternatives.add(cloneResolvedType(resolvedRet));
      alternatives.add(cloneResolvedType(resolvedRaises));
      auto& result = storeType(expr, zc::heap<type::UnionType>(zc::mv(alternatives)));
      if (instanceMethodTarget.isValid()) {
        auto dispatchArgs = dispatchArgTypeIds(impl->typeEnv, impl->tree.list(argList));
        if (isStaticMethodTarget) {
          recordStaticMethodDispatch(impl->typeEnv, expr, instanceMethodTarget,
                                     dispatchArgs.asPtr(), result);
        } else if (impl->typeEnv.hasType(instanceReceiverId)) {
          zc::Vector<type::TypeId> argsWithReceiver;
          argsWithReceiver.add(impl->typeEnv.internType(
              impl->typeEnv.find(impl->typeEnv.getType(instanceReceiverId))));
          for (auto argType : dispatchArgs) { argsWithReceiver.add(argType); }
          recordInstanceMethodDispatch(impl->typeEnv, expr, instanceMethodTarget,
                                       argsWithReceiver.asPtr(), result);
        }
      } else if (isDynVTableTarget && impl->typeEnv.hasType(instanceReceiverId)) {
        auto dispatchArgs = dispatchArgTypeIds(impl->typeEnv, impl->tree.list(argList));
        zc::Vector<type::TypeId> argsWithReceiver;
        argsWithReceiver.add(impl->typeEnv.internType(
            impl->typeEnv.find(impl->typeEnv.getType(instanceReceiverId))));
        for (auto argType : dispatchArgs) { argsWithReceiver.add(argType); }
        recordDynVTableDispatch(impl->typeEnv, expr, dynInterfaceName, dynMethodName, dynVTableSlot,
                                argsWithReceiver.asPtr(), result);
      } else if (freeFunctionTarget.isValid()) {
        auto dispatchArgs = dispatchArgTypeIds(impl->typeEnv, impl->tree.list(argList));
        recordFreeFunctionDispatch(impl->typeEnv, expr, freeFunctionTarget, dispatchArgs.asPtr(),
                                   result);
      }
      return result;
    }

    auto& result = storeType(expr, cloneResolvedType(resolvedRet));
    if (instanceMethodTarget.isValid()) {
      auto dispatchArgs = dispatchArgTypeIds(impl->typeEnv, impl->tree.list(argList));
      if (isStaticMethodTarget) {
        recordStaticMethodDispatch(impl->typeEnv, expr, instanceMethodTarget, dispatchArgs.asPtr(),
                                   result);
      } else if (impl->typeEnv.hasType(instanceReceiverId)) {
        zc::Vector<type::TypeId> argsWithReceiver;
        argsWithReceiver.add(impl->typeEnv.internType(
            impl->typeEnv.find(impl->typeEnv.getType(instanceReceiverId))));
        for (auto argType : dispatchArgs) { argsWithReceiver.add(argType); }
        recordInstanceMethodDispatch(impl->typeEnv, expr, instanceMethodTarget,
                                     argsWithReceiver.asPtr(), result);
      }
    } else if (isDynVTableTarget && impl->typeEnv.hasType(instanceReceiverId)) {
      auto dispatchArgs = dispatchArgTypeIds(impl->typeEnv, impl->tree.list(argList));
      zc::Vector<type::TypeId> argsWithReceiver;
      argsWithReceiver.add(
          impl->typeEnv.internType(impl->typeEnv.find(impl->typeEnv.getType(instanceReceiverId))));
      for (auto argType : dispatchArgs) { argsWithReceiver.add(argType); }
      recordDynVTableDispatch(impl->typeEnv, expr, dynInterfaceName, dynMethodName, dynVTableSlot,
                              argsWithReceiver.asPtr(), result);
    } else if (freeFunctionTarget.isValid()) {
      auto dispatchArgs = dispatchArgTypeIds(impl->typeEnv, impl->tree.list(argList));
      recordFreeFunctionDispatch(impl->typeEnv, expr, freeFunctionTarget, dispatchArgs.asPtr(),
                                 result);
    }
    return result;
  }

  // If callee is an interface type, it might be callable (function interface)
  if (isInterface(resolvedCallee)) {
    // Simplified: return error type for now
    return storeType(expr, zc::heap<type::ErrorType>());
  }

  if (isError(resolvedCallee)) { return storeType(expr, zc::heap<type::ErrorType>()); }

  // If callee is a named type, check if it's a class with operator()
  if (isNamed(resolvedCallee)) {
    // Simplified: try to treat as constructor call
    return storeType(expr, zc::heap<type::NamedType>(
                               static_cast<const type::NamedType&>(resolvedCallee).getName()));
  }

  auto loc = getNodeLoc(impl->tree, expr);
  impl->diags.diagnose<DiagID::CannotCallNonFunction>(loc, resolvedCallee.toString());
  impl->hadErrors = true;
  return storeType(expr, zc::heap<type::ErrorType>());
}

const type::Type& BodyChecker::checkMemberExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  auto objId = NodeId(node.payload.words[kMemberExpressionObjectWord]);
  auto rawProp = node.payload.words[kMemberExpressionPropertyWord];

  auto& objType = checkExpr(objId);
  auto& resolvedObj = impl->typeEnv.find(objType);

  // Get property name
  zc::StringPtr propName;
  auto propId = IdentId(rawProp);
  if (propId.value != 0) { propName = impl->tree.ident(propId); }
  if (propName.size() == 0) {
    auto propNodeId = NodeId(rawProp);
    if (impl->tree.contains(propNodeId)) {
      const auto& propNode = impl->tree.node(propNodeId);
      if (propNode.kind == SyntaxKind::IdentExpr) {
        propName = impl->tree.ident(IdentId(propNode.payload.words[kIdentExprNameWord]));
      }
    }
  }

  if (propName.size() == 0) { return storeType(expr, zc::heap<type::ErrorType>()); }

  // Look up member in object type
  if (isObject(resolvedObj)) {
    auto& objTy = static_cast<const type::ObjectType&>(resolvedObj);
    auto memberTy = objTy.getMember(propName);
    ZC_IF_SOME(mTy, memberTy) { return storeType(expr, cloneResolvedType(mTy)); }
  }

  // Named type: look up in symbol table
  if (isNamed(resolvedObj)) {
    auto& namedTy = static_cast<const type::NamedType&>(resolvedObj);
    auto memberSym = lookupSymbol(zc::str(namedTy.getName(), "."_zc, propName));
    ZC_IF_SOME(sym, memberSym) {
      auto ty = getSymbolType(sym);
      ZC_IF_SOME(memberType, ty) {
        impl->memberExprSymbols.upsert(expr.value, sym.getId());
        impl->memberExprIsStatic.upsert(expr.value, sym.isStatic());
        return storeType(expr, cloneResolvedType(memberType));
      }
    }

    auto typeSym = namedTy.getSymbol();
    ZC_IF_SOME(sym, typeSym) {
      auto refs = sym.getDeclarationRefs();
      if (refs.size() > 0) {
        auto scopeId = impl->metadata.scope(refs[0].node);
        auto scopeEntry = impl->scopeIdMap.find(scopeId);
        ZC_IF_SOME(scopePtr, scopeEntry) {
          auto scopedMember = impl->symbols.lookup(propName, *scopePtr);
          ZC_IF_SOME(member, scopedMember) {
            auto ty = getSymbolType(member);
            ZC_IF_SOME(memberType, ty) {
              impl->memberExprSymbols.upsert(expr.value, member.getId());
              impl->memberExprIsStatic.upsert(expr.value, member.isStatic());
              return storeType(expr, cloneResolvedType(memberType));
            }
          }
        }
      }
    }

    auto classScope = impl->symbols.getScopeManager().getClassScope(namedTy.getName());
    ZC_IF_SOME(scope, classScope) {
      auto scopedMember = impl->symbols.lookup(propName, scope);
      ZC_IF_SOME(member, scopedMember) {
        auto ty = getSymbolType(member);
        ZC_IF_SOME(memberType, ty) {
          impl->memberExprSymbols.upsert(expr.value, member.getId());
          impl->memberExprIsStatic.upsert(expr.value, member.isStatic());
          return storeType(expr, cloneResolvedType(memberType));
        }
      }
    }
  }

  if (isExistential(resolvedObj)) {
    auto dynMethod = findDynInterfaceMethod(impl->tree, resolvedObj, propName);
    ZC_IF_SOME(method, dynMethod) {
      const auto& methodNode = impl->tree.node(method.methodDecl);
      zc::Vector<zc::Own<type::Type>> paramTypes;

      auto paramsId = NodeId(methodNode.payload.words[kMethodDeclParamsIdWord]);
      if (impl->tree.contains(paramsId)) {
        const auto& paramsNode = impl->tree.node(paramsId);
        if (paramsNode.kind == SyntaxKind::FunctionParameterList) {
          NodeList params;
          params.first = paramsNode.payload.words[kFunctionParameterListParamsFirstWord];
          params.size = paramsNode.payload.words[kFunctionParameterListParamsSizeWord];
          for (NodeId paramId : impl->tree.list(params)) {
            if (!impl->tree.contains(paramId)) continue;
            const auto& param = impl->tree.node(paramId);
            if (param.kind != SyntaxKind::FunctionParameterDecl) continue;
            auto tyId = NodeId(param.payload.words[kFunctionParameterDeclTyWord]);
            auto paramTy = resolveTypeExpr(tyId);
            if (!paramTy) { return storeType(expr, zc::heap<type::ErrorType>()); }
            paramTypes.add(zc::mv(paramTy));
          }
        }
      }

      auto retTyId = NodeId(methodNode.payload.words[kMethodDeclRetTyWord]);
      auto retTy = impl->tree.contains(retTyId)
                       ? resolveTypeExpr(retTyId)
                       : zc::heap<type::PrimitiveType>(type::PrimitiveKind::Unit);
      if (!retTy) { return storeType(expr, zc::heap<type::ErrorType>()); }

      impl->memberExprDynInterfaceNames.upsert(expr.value, method.interfaceName);
      impl->memberExprDynMethodNames.upsert(expr.value, method.methodName);
      impl->memberExprDynVTableSlots.upsert(expr.value, method.slot);
      return storeType(expr, zc::heap<type::FunctionType>(zc::mv(paramTypes), zc::mv(retTy)));
    }
  }

  auto memberLoc = getNodeLoc(impl->tree, expr);
  impl->diags.diagnose<DiagID::MemberNotFound>(memberLoc, propName, resolvedObj.toString());
  impl->hadErrors = true;
  return storeType(expr, zc::heap<type::ErrorType>());
}

const type::Type& BodyChecker::checkIndexExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  auto objId = NodeId(node.payload.words[kIndexExpressionObjectWord]);
  auto idxId = NodeId(node.payload.words[kIndexExpressionIndexWord]);

  auto& objType = checkExpr(objId);
  auto& idxType = checkExpr(idxId);
  auto& resolvedObj = impl->typeEnv.find(objType);
  auto& resolvedIdx = impl->typeEnv.find(idxType);

  // Array indexing
  if (isArray(resolvedObj)) {
    auto& arrTy = static_cast<const type::ArrayType&>(resolvedObj);
    // Index must be integer
    if (!isInteger(resolvedIdx)) {
      auto loc = getNodeLoc(impl->tree, idxId);
      impl->diags.diagnose<DiagID::IndexRequiresInteger>(loc);
      impl->hadErrors = true;
    }
    auto& result = storeType(expr, cloneResolvedType(arrTy.getElementType()));
    zc::Vector<type::TypeId> args;
    args.add(impl->typeEnv.internType(resolvedObj));
    args.add(impl->typeEnv.internType(resolvedIdx));
    recordPrimitiveDispatch(impl->typeEnv, expr, type::ReceiverMode::IndexBase, args.asPtr(),
                            result);
    return result;
  }

  // Tuple indexing
  if (isTuple(resolvedObj)) {
    auto& tupleTy = static_cast<const type::TupleType&>(resolvedObj);
    if (!isInteger(resolvedIdx)) {
      auto loc = getNodeLoc(impl->tree, idxId);
      impl->diags.diagnose<DiagID::TupleIndexRequiresIntegerLiteral>(loc);
      impl->hadErrors = true;
      return storeType(expr, zc::heap<type::ErrorType>());
    }
    if (!impl->tree.contains(idxId) || impl->tree.node(idxId).kind != SyntaxKind::IntLiteral) {
      auto loc = getNodeLoc(impl->tree, idxId);
      impl->diags.diagnose<DiagID::TupleIndexRequiresIntegerLiteral>(loc);
      impl->hadErrors = true;
      return storeType(expr, zc::heap<type::ErrorType>());
    }

    const auto& idxNode = impl->tree.node(idxId);
    auto indexText = impl->tree.bigInt(BigIntId(idxNode.payload.words[kIntLiteralValueWord]));
    auto index = indexText.parseAs<unsigned long long>();
    if (index >= tupleTy.getElementCount()) {
      auto loc = getNodeLoc(impl->tree, idxId);
      impl->diags.diagnose<DiagID::TupleIndexOutOfBounds>(loc);
      impl->hadErrors = true;
      return storeType(expr, zc::heap<type::ErrorType>());
    }

    auto& result =
        storeType(expr, cloneResolvedType(tupleTy.getElementType(static_cast<size_t>(index))));
    zc::Vector<type::TypeId> args;
    args.add(impl->typeEnv.internType(resolvedObj));
    args.add(impl->typeEnv.internType(resolvedIdx));
    recordPrimitiveDispatch(impl->typeEnv, expr, type::ReceiverMode::IndexBase, args.asPtr(),
                            result);
    return result;
  }

  if (isNamed(resolvedObj)) {
    TraitResolver traitResolver(impl->typeEnv, impl->symbols, impl->tree, impl->metadata,
                                impl->diags);
    traitResolver.discoverImpls();

    if (!traitResolver.implements(resolvedObj, "Index"_zc)) {
      auto loc = getNodeLoc(impl->tree, expr);
      impl->diags.diagnose<DiagID::CheckerTraitNotImplemented>(loc, resolvedObj.toString(),
                                                               "Index"_zc);
      impl->hadErrors = true;
      return storeType(expr, zc::heap<type::ErrorType>());
    }

    auto output =
        traitResolver.resolveAssociatedTypeWithStatus(resolvedObj, "Index"_zc, "Output"_zc);
    if (output.kind == AssociatedTypeResolutionKind::Resolved) {
      ZC_IF_SOME(outputTy, output.type) {
        if (!validateSingleParamTraitMethod(expr, "Index"_zc, "index"_zc, resolvedObj, resolvedIdx,
                                            outputTy)) {
          auto loc = getNodeLoc(impl->tree, expr);
          impl->diags.diagnose<DiagID::OperatorTraitSignatureMismatch>(
              loc, "Index"_zc, resolvedObj.toString(), "index"_zc, resolvedIdx.toString(),
              outputTy.toString());
          impl->hadErrors = true;
          return storeType(expr, zc::heap<type::ErrorType>());
        }
        auto& result = storeType(expr, cloneResolvedType(outputTy));
        ZC_IF_SOME(implNode, traitResolver.findImpl(resolvedObj, "Index"_zc)) {
          type::CallDispatchRecord record;
          record.targetKind = type::CallTargetKind::IndexMethod;
          record.receiverMode = type::ReceiverMode::IndexBase;
          record.interfaceName = zc::str("Index"_zc);
          record.methodName = zc::str("index"_zc);
          record.implNode = implNode;
          record.argumentTypes.add(impl->typeEnv.internType(resolvedObj));
          record.argumentTypes.add(impl->typeEnv.internType(resolvedIdx));
          record.resultType = impl->typeEnv.internType(result);
          impl->typeEnv.setDispatch(expr, zc::mv(record));
        }
        return result;
      }
    }

    auto loc = getNodeLoc(impl->tree, expr);
    if (output.kind == AssociatedTypeResolutionKind::Ambiguous) {
      auto typeText = resolvedObj.toString();
      impl->diags.diagnose<DiagID::AmbiguousAssociatedTypeProjection>(
          loc, "Output"_zc, typeText.asPtr(), typeText.asPtr(), "Output"_zc);
    } else {
      impl->diags.diagnose<DiagID::NoAssociatedTypeProjection>(loc, "Output"_zc,
                                                               resolvedObj.toString());
    }
    impl->hadErrors = true;
    return storeType(expr, zc::heap<type::ErrorType>());
  }

  auto indexLoc = getNodeLoc(impl->tree, expr);
  impl->diags.diagnose<DiagID::CannotIndexType>(indexLoc);
  impl->hadErrors = true;
  return storeType(expr, zc::heap<type::ErrorType>());
}

const type::Type& BodyChecker::checkNewExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  auto calleeId = NodeId(node.payload.words[kNewExpressionCalleeWord]);

  // The type being constructed is the callee's resolved type
  auto& calleeType = checkExpr(calleeId);
  auto& resolved = impl->typeEnv.find(calleeType);

  if (isNamed(resolved)) {
    return storeType(
        expr, zc::heap<type::NamedType>(static_cast<const type::NamedType&>(resolved).getName()));
  }

  // For any other type (including error types from failed lookup),
  // return a clone of the resolved type
  if (!isError(resolved)) { return storeType(expr, cloneResolvedType(resolved)); }

  return storeType(expr, zc::heap<type::ErrorType>());
}

const type::Type& BodyChecker::checkCastExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  auto exprId = NodeId(node.payload.words[kCastExpressionExprWord]);
  auto tyId = NodeId(node.payload.words[kCastExpressionTyWord]);

  // Check the source expression
  auto& sourceType = checkExpr(exprId);
  auto& resolvedSource = impl->typeEnv.find(sourceType);

  auto targetType = resolveTypeExpr(tyId);
  if (!targetType) {
    if (!isError(resolvedSource)) {
      auto loc = getNodeLoc(impl->tree, expr);
      impl->diags.diagnose<DiagID::UnsupportedCastTarget>(loc);
      impl->hadErrors = true;
    }
    return storeType(expr, zc::heap<type::ErrorType>());
  }

  auto& resolvedTarget = impl->typeEnv.find(*targetType);
  if (isError(resolvedSource) || isError(resolvedTarget)) {
    return storeType(expr, zc::heap<type::ErrorType>());
  }

  if (resolvedSource.equals(resolvedTarget)) {
    return storeType(expr, cloneResolvedType(resolvedTarget));
  }

  if (isPrimitive(resolvedSource) && isPrimitive(resolvedTarget)) {
    auto& sourcePrim = static_cast<const type::PrimitiveType&>(resolvedSource);
    auto& targetPrim = static_cast<const type::PrimitiveType&>(resolvedTarget);
    if ((sourcePrim.isIntegerType() || sourcePrim.isFloatingPointType()) &&
        (targetPrim.isIntegerType() || targetPrim.isFloatingPointType())) {
      return storeType(expr, cloneResolvedType(resolvedTarget));
    }
  }

  if (isRawPointer(resolvedSource) && isRawPointer(resolvedTarget)) {
    auto& sourcePtr = static_cast<const type::RawPointerType&>(resolvedSource);
    auto& targetPtr = static_cast<const type::RawPointerType&>(resolvedTarget);
    if (sourcePtr.getPointeeType().equals(targetPtr.getPointeeType()) &&
        sourcePtr.getMutability() == type::Mutability::Mutable &&
        targetPtr.getMutability() == type::Mutability::Const) {
      return storeType(expr, cloneResolvedType(resolvedTarget));
    }
    if (impl->unsafeDepth > 0) { return storeType(expr, cloneResolvedType(resolvedTarget)); }

    auto loc = getNodeLoc(impl->tree, expr);
    impl->diags.diagnose<DiagID::RawPointerCastRequiresUnsafe>(loc, resolvedSource.toString(),
                                                               resolvedTarget.toString());
    impl->hadErrors = true;
    return storeType(expr, zc::heap<type::ErrorType>());
  }

  if (isReference(resolvedSource) && isRawPointer(resolvedTarget)) {
    auto& sourceRef = static_cast<const type::ReferenceType&>(resolvedSource);
    auto& targetPtr = static_cast<const type::RawPointerType&>(resolvedTarget);
    if (sourceRef.getPointeeType().equals(targetPtr.getPointeeType())) {
      if (sourceRef.getMutability() == type::Mutability::Mutable ||
          targetPtr.getMutability() == type::Mutability::Const) {
        return storeType(expr, cloneResolvedType(resolvedTarget));
      }
    }
  }

  if (isExistential(resolvedSource) && isExistential(resolvedTarget)) {
    auto& sourceExistential = static_cast<const type::ExistentialType&>(resolvedSource);
    auto& targetExistential = static_cast<const type::ExistentialType&>(resolvedTarget);
    if (dynTypeExtends(sourceExistential.getInterfaceType(), targetExistential.getInterfaceType(),
                       impl->tree)) {
      impl->typeEnv.setCoercion(expr, type::CoercionKind::DynUpcast);
      return storeType(expr, cloneResolvedType(resolvedTarget));
    }

    auto loc = getNodeLoc(impl->tree, expr);
    impl->diags.diagnose<DiagID::InvalidDynUpcast>(loc, resolvedSource.toString(),
                                                   resolvedTarget.toString());
    impl->hadErrors = true;
    return storeType(expr, zc::heap<type::ErrorType>());
  }

  auto loc = getNodeLoc(impl->tree, expr);
  impl->diags.diagnose<DiagID::CheckerInvalidCast>(loc, resolvedSource.toString(),
                                                   resolvedTarget.toString());
  impl->hadErrors = true;
  return storeType(expr, zc::heap<type::ErrorType>());
}

const type::Type& BodyChecker::checkConditionalExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  auto condId = NodeId(node.payload.words[kConditionalExprCondWord]);
  auto thenId = NodeId(node.payload.words[kConditionalExprThenExprWord]);
  auto elseId = NodeId(node.payload.words[kConditionalExprElseExprWord]);

  auto& condType = checkExpr(condId);
  auto& thenType = checkExpr(thenId);
  auto& elseType = checkExpr(elseId);

  // Condition must be bool
  auto& resolvedCond = impl->typeEnv.find(condType);
  if (!isPrimitive(resolvedCond) ||
      static_cast<const type::PrimitiveType&>(resolvedCond).getPrimitiveKind() !=
          type::PrimitiveKind::Bool) {
    if (!isError(resolvedCond)) {
      auto loc = getNodeLoc(impl->tree, condId);
      impl->diags.diagnose<DiagID::ConditionMustBeBool>(loc);
      impl->hadErrors = true;
    }
  }

  // Result is the join of then and else types.
  auto& resolvedThen = impl->typeEnv.find(thenType);
  auto& resolvedElse = impl->typeEnv.find(elseType);

  if (isError(resolvedThen)) return storeType(expr, zc::heap<type::ErrorType>());
  if (isError(resolvedElse)) return storeType(expr, zc::heap<type::ErrorType>());

  // If both are the same type, return it
  if (resolvedThen.equals(resolvedElse)) {
    return storeType(expr, cloneResolvedType(resolvedThen));
  }

  auto thenToElse = impl->coercions.check(resolvedThen, resolvedElse);
  if (thenToElse.success) {
    if (thenToElse.kind != type::CoercionKind::Identity) {
      impl->typeEnv.setCoercion(thenId, thenToElse.kind);
    }
    return storeType(expr, cloneResolvedType(resolvedElse));
  }

  auto elseToThen = impl->coercions.check(resolvedElse, resolvedThen);
  if (elseToThen.success) {
    if (elseToThen.kind != type::CoercionKind::Identity) {
      impl->typeEnv.setCoercion(elseId, elseToThen.kind);
    }
    return storeType(expr, cloneResolvedType(resolvedThen));
  }

  zc::Vector<zc::Own<type::Type>> alternatives;
  alternatives.add(cloneResolvedType(resolvedThen));
  alternatives.add(cloneResolvedType(resolvedElse));
  impl->typeEnv.setCoercion(thenId, type::CoercionKind::UnionInjection);
  impl->typeEnv.setCoercion(elseId, type::CoercionKind::UnionInjection);
  return storeType(expr, zc::heap<type::UnionType>(zc::mv(alternatives)));
}

const type::Type& BodyChecker::checkAssignmentExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  auto lhsId = NodeId(node.payload.words[kAssignmentExprLhsWord]);
  auto rhsId = NodeId(node.payload.words[kAssignmentExprRhsWord]);

  auto& lhsType = checkExpr(lhsId);
  auto& rhsType = checkExpr(rhsId);

  if (impl->tree.contains(lhsId) && impl->tree.node(lhsId).kind == SyntaxKind::IdentExpr) {
    const auto& lhsNode = impl->tree.node(lhsId);
    auto name = impl->tree.ident(IdentId(lhsNode.payload.words[kIdentExprNameWord]));
    auto sym = lookupSymbol(name);
    ZC_IF_SOME(s, sym) {
      if (s.isVariableSymbol() && !s.isMutable()) {
        auto loc = getNodeLoc(impl->tree, lhsId);
        impl->diags.diagnose<DiagID::CannotMutateImmutableVariable>(loc, name);
        impl->hadErrors = true;
        return storeType(expr, zc::heap<type::ErrorType>());
      }
    }
  }

  // Check assignability
  checkAssignable(lhsType, rhsType, expr);
  if (impl->hadErrors) { return storeType(expr, zc::heap<type::ErrorType>()); }

  auto& resolvedRhs = impl->typeEnv.find(rhsType);
  return storeType(expr, cloneResolvedType(resolvedRhs));
}

const type::Type& BodyChecker::checkLambdaExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  auto paramsId = NodeId(node.payload.words[kLambdaExpressionParamsIdWord]);
  auto retTyId = NodeId(node.payload.words[kLambdaExpressionRetTyWord]);
  auto bodyId = NodeId(node.payload.words[kLambdaExpressionBodyWord]);

  // Build function type from parameters and return type
  zc::Vector<zc::Own<type::Type>> paramTypes;

  // Extract parameter types (simplified: use type vars for untyped params)
  if (impl->tree.contains(paramsId)) {
    // Walk parameter list
    visitTreePreOrder(impl->tree, paramsId, [&](NodeId, const Node& pNode) {
      if (pNode.kind == SyntaxKind::FunctionParameterDecl) {
        auto tyId = NodeId(pNode.payload.words[kFunctionParameterDeclTyWord]);
        if (impl->tree.contains(tyId)) {
          auto paramType = resolveTypeExpr(tyId);
          if (paramType) {
            paramTypes.add(zc::mv(paramType));
            return;
          }
        }
        auto paramName =
            impl->tree.ident(IdentId(pNode.payload.words[kFunctionParameterDeclNameWord]));
        paramTypes.add(zc::heap<type::TypeVar>(paramName, impl->typeEnv.freshId()));
      }
    });
  }

  // Determine return type from annotation or body
  zc::Own<type::Type> retType;
  if (impl->tree.contains(retTyId) && impl->tree.node(retTyId).kind != SyntaxKind::Unknown) {
    retType = resolveTypeExpr(retTyId);
    if (impl->tree.contains(bodyId)) {
      auto& bodyType = checkExpr(bodyId);
      auto& resolvedBody = impl->typeEnv.find(bodyType);
      auto& resolvedRet = impl->typeEnv.find(*retType);
      bool hadErrorsBefore = impl->hadErrors;
      checkAssignable(resolvedRet, resolvedBody, expr, bodyId);
      if (!hadErrorsBefore && impl->hadErrors) {
        return storeType(expr, zc::heap<type::ErrorType>());
      }
    }
  } else if (impl->tree.contains(bodyId)) {
    // Infer from body
    auto& bodyType = checkExpr(bodyId);
    (void)bodyType;
    retType = zc::heap<type::PrimitiveType>(type::PrimitiveKind::Unit);
  } else {
    retType = zc::heap<type::PrimitiveType>(type::PrimitiveKind::Unit);
  }

  return storeType(expr, zc::heap<type::FunctionType>(zc::mv(paramTypes), zc::mv(retType)));
}

const type::Type& BodyChecker::checkFunctionExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  auto paramsId = NodeId(node.payload.words[kFunctionExpressionParamsIdWord]);
  auto retTyId = NodeId(node.payload.words[kFunctionExpressionRetTyWord]);
  auto bodyId = NodeId(node.payload.words[kFunctionExpressionBodyWord]);

  // Build function type
  zc::Vector<zc::Own<type::Type>> paramTypes;

  if (impl->tree.contains(paramsId)) {
    visitTreePreOrder(impl->tree, paramsId, [&](NodeId, const Node& pNode) {
      if (pNode.kind == SyntaxKind::FunctionParameterDecl) {
        auto tyId = NodeId(pNode.payload.words[kFunctionParameterDeclTyWord]);
        if (impl->tree.contains(tyId)) {
          auto paramType = resolveTypeExpr(tyId);
          if (paramType) {
            paramTypes.add(zc::mv(paramType));
            return;
          }
        }
        auto paramName =
            impl->tree.ident(IdentId(pNode.payload.words[kFunctionParameterDeclNameWord]));
        paramTypes.add(zc::heap<type::TypeVar>(paramName, impl->typeEnv.freshId()));
      }
    });
  }

  zc::Own<type::Type> retType;
  if (impl->tree.contains(bodyId)) {
    // Save and set expected return type
    auto oldRetType = impl->expectedRetType;

    if (impl->tree.contains(retTyId)) {
      retType = resolveTypeExpr(retTyId);
      impl->expectedRetType = *retType;
    }

    auto& bodyType = checkExpr(bodyId);
    if (retType) {
      auto& resolvedBody = impl->typeEnv.find(bodyType);
      auto& resolvedRet = impl->typeEnv.find(*retType);
      bool hadErrorsBefore = impl->hadErrors;
      checkAssignable(resolvedRet, resolvedBody, expr, bodyId);
      if (!hadErrorsBefore && impl->hadErrors) {
        impl->expectedRetType = oldRetType;
        return storeType(expr, zc::heap<type::ErrorType>());
      }
    }

    impl->expectedRetType = oldRetType;
  }

  if (!retType) { retType = zc::heap<type::PrimitiveType>(type::PrimitiveKind::Unit); }

  return storeType(expr, zc::heap<type::FunctionType>(zc::mv(paramTypes), zc::mv(retType)));
}

const type::Type& BodyChecker::checkObjectLiteral(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  (void)node;

  // Build object type from properties
  auto objTy = zc::heap<type::ObjectType>();

  NodeList properties;
  properties.first = node.payload.words[kObjectLiteralExprPropertiesFirstWord];
  properties.size = node.payload.words[kObjectLiteralExprPropertiesSizeWord];

  for (NodeId propId : impl->tree.list(properties)) {
    if (!impl->tree.contains(propId)) continue;
    const auto& prop = impl->tree.node(propId);
    if (prop.kind != SyntaxKind::ObjectProperty) continue;

    auto rawName = prop.payload.words[kObjectPropertyNameWord];
    auto propValueId = NodeId(prop.payload.words[kObjectPropertyValueWord]);
    zc::StringPtr propName;

    auto propNameId = IdentId(rawName);
    if (propNameId.value != 0) { propName = impl->tree.ident(propNameId); }
    if (propName.size() == 0) {
      auto propNameNodeId = NodeId(rawName);
      if (impl->tree.contains(propNameNodeId)) {
        const auto& propNameNode = impl->tree.node(propNameNodeId);
        if (propNameNode.kind == SyntaxKind::IdentExpr) {
          propName = impl->tree.ident(IdentId(propNameNode.payload.words[kIdentExprNameWord]));
        }
      }
    }

    if (propName.size() > 0 && impl->tree.contains(propValueId)) {
      auto& valueType = checkExpr(propValueId);
      auto& resolvedValue = impl->typeEnv.find(valueType);
      objTy->addMember(propName, cloneResolvedType(resolvedValue));
    }
  }

  return storeType(expr, zc::mv(objTy));
}

const type::Type& BodyChecker::checkStructLiteralExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  auto tyId = NodeId(node.payload.words[kStructLiteralExprTyWord]);
  auto targetType = resolveTypeExpr(tyId);
  if (!targetType) {
    auto loc = getNodeLoc(impl->tree, expr);
    impl->diags.diagnose<DiagID::UnsupportedStructLiteralTarget>(loc);
    impl->hadErrors = true;
    return storeType(expr, zc::heap<type::ErrorType>());
  }

  zc::HashMap<zc::StringPtr, zc::Own<type::Type>> fieldTypes;
  auto& initialTarget = impl->typeEnv.find(*targetType);
  if (isNamed(initialTarget)) {
    auto targetName = static_cast<const type::NamedType&>(initialTarget).getName();
    auto targetSym = lookupSymbol(targetName);
    ZC_IF_SOME(sym, targetSym) {
      auto refs = sym.getDeclarationRefs();
      for (const auto& ref : refs) {
        if (!impl->tree.contains(ref.node)) continue;
        const auto& decl = impl->tree.node(ref.node);

        NodeId membersId;
        if (decl.kind == SyntaxKind::StructDecl) {
          membersId = NodeId(decl.payload.words[kStructDeclMembersIdWord]);
        } else if (decl.kind == SyntaxKind::ClassDecl) {
          membersId = NodeId(decl.payload.words[kClassDeclMembersIdWord]);
        } else {
          continue;
        }

        if (!impl->tree.contains(membersId)) continue;
        const auto& membersNode = impl->tree.node(membersId);
        if (membersNode.kind != SyntaxKind::ClassMemberList) continue;

        NodeList members;
        members.first = membersNode.payload.words[kClassMemberListMembersFirstWord];
        members.size = membersNode.payload.words[kClassMemberListMembersSizeWord];
        for (NodeId memberId : impl->tree.list(members)) {
          if (!impl->tree.contains(memberId)) continue;
          const auto& member = impl->tree.node(memberId);
          if (member.kind != SyntaxKind::FieldDecl) continue;

          auto name = impl->tree.ident(IdentId(member.payload.words[kFieldDeclNameWord]));
          auto fieldTyId = NodeId(member.payload.words[kFieldDeclTyWord]);
          if (name.size() == 0 || !impl->tree.contains(fieldTyId)) continue;

          auto resolvedFieldType = resolveTypeExpr(fieldTyId);
          if (resolvedFieldType) { fieldTypes.upsert(name, zc::mv(resolvedFieldType)); }
        }
        break;
      }
    }
  }

  auto findFieldType = [&](zc::StringPtr fieldName) -> zc::Own<type::Type> {
    ZC_IF_SOME(fieldType, fieldTypes.find(fieldName)) { return cloneResolvedType(*fieldType); }

    auto& resolvedTarget = impl->typeEnv.find(*targetType);
    if (!isNamed(resolvedTarget)) { return zc::Own<type::Type>(); }

    auto targetName = static_cast<const type::NamedType&>(resolvedTarget).getName();
    auto targetSym = lookupSymbol(targetName);
    if (targetSym == zc::none) { return zc::Own<type::Type>(); }

    ZC_IF_SOME(sym, targetSym) {
      auto refs = sym.getDeclarationRefs();
      for (const auto& ref : refs) {
        if (!impl->tree.contains(ref.node)) continue;
        const auto& decl = impl->tree.node(ref.node);

        NodeId membersId;
        if (decl.kind == SyntaxKind::StructDecl) {
          membersId = NodeId(decl.payload.words[kStructDeclMembersIdWord]);
        } else if (decl.kind == SyntaxKind::ClassDecl) {
          membersId = NodeId(decl.payload.words[kClassDeclMembersIdWord]);
        } else {
          continue;
        }

        if (!impl->tree.contains(membersId)) continue;
        const auto& membersNode = impl->tree.node(membersId);
        if (membersNode.kind != SyntaxKind::ClassMemberList) continue;

        NodeList members;
        members.first = membersNode.payload.words[kClassMemberListMembersFirstWord];
        members.size = membersNode.payload.words[kClassMemberListMembersSizeWord];
        for (NodeId memberId : impl->tree.list(members)) {
          if (!impl->tree.contains(memberId)) continue;
          const auto& member = impl->tree.node(memberId);
          if (member.kind != SyntaxKind::FieldDecl) continue;

          auto name = impl->tree.ident(IdentId(member.payload.words[kFieldDeclNameWord]));
          if (name != fieldName) continue;

          auto fieldTyId = NodeId(member.payload.words[kFieldDeclTyWord]);
          if (!impl->tree.contains(fieldTyId)) { return zc::Own<type::Type>(); }
          return resolveTypeExpr(fieldTyId);
        }
      }
    }

    return zc::Own<type::Type>();
  };

  NodeList properties;
  properties.first = node.payload.words[kStructLiteralExprPropertiesFirstWord];
  properties.size = node.payload.words[kStructLiteralExprPropertiesSizeWord];
  bool hadFieldError = false;
  zc::HashSet<zc::StringPtr> seenFields;

  for (NodeId propId : impl->tree.list(properties)) {
    if (!impl->tree.contains(propId)) continue;
    const auto& prop = impl->tree.node(propId);
    if (prop.kind != SyntaxKind::ObjectProperty) continue;

    auto rawName = prop.payload.words[kObjectPropertyNameWord];
    auto valueId = NodeId(prop.payload.words[kObjectPropertyValueWord]);
    zc::StringPtr propName;
    auto propNameId = IdentId(rawName);
    if (propNameId.value != 0) { propName = impl->tree.ident(propNameId); }

    if (!impl->tree.contains(valueId)) continue;
    auto& valueType = checkExpr(valueId);
    auto& resolvedValue = impl->typeEnv.find(valueType);
    if (isError(resolvedValue)) { hadFieldError = true; }

    if (propName.size() > 0) {
      seenFields.insert(propName);
      auto fieldType = findFieldType(propName);
      if (fieldType) {
        bool hadErrorsBefore = impl->hadErrors;
        auto& resolvedField = impl->typeEnv.find(*fieldType);
        checkAssignable(resolvedField, resolvedValue, expr, valueId);
        if (!hadErrorsBefore && impl->hadErrors) { hadFieldError = true; }
      } else if (fieldTypes.size() != 0) {
        auto loc = getNodeLoc(impl->tree, propId);
        impl->diags.diagnose<DiagID::UnknownStructField>(loc, propName);
        impl->hadErrors = true;
        hadFieldError = true;
      }
    }
  }

  for (auto& entry : fieldTypes) {
    if (!seenFields.contains(entry.key)) {
      auto loc = getNodeLoc(impl->tree, expr);
      impl->diags.diagnose<DiagID::MissingStructField>(loc, entry.key);
      impl->hadErrors = true;
      hadFieldError = true;
    }
  }

  if (hadFieldError) { return storeType(expr, zc::heap<type::ErrorType>()); }

  auto& resolvedTarget = impl->typeEnv.find(*targetType);
  return storeType(expr, cloneResolvedType(resolvedTarget));
}

const type::Type& BodyChecker::checkArrayLiteral(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  NodeList elems;
  elems.first = node.payload.words[kArrayLiteralElemsFirstWord];
  elems.size = node.payload.words[kArrayLiteralElemsSizeWord];

  zc::Maybe<const type::Type&> elemType;
  bool elementMismatch = false;

  for (NodeId elemId : impl->tree.list(elems)) {
    auto& elemTy = checkExpr(elemId);
    auto& resolved = impl->typeEnv.find(elemTy);
    if (isError(resolved)) {
      elementMismatch = true;
      continue;
    }

    if (elemType == zc::none) {
      elemType = resolved;
    } else {
      ZC_IF_SOME(et, elemType) {
        if (!resolved.equals(et)) {
          auto currentToNew = impl->coercions.check(et, resolved);
          auto newToCurrent = impl->coercions.check(resolved, et);
          if (newToCurrent.success) {
            if (newToCurrent.kind != type::CoercionKind::Identity) {
              impl->typeEnv.setCoercion(elemId, newToCurrent.kind);
            }
          } else if (currentToNew.success) {
            elemType = resolved;
          } else {
            auto loc = getNodeLoc(impl->tree, elemId);
            impl->diags.diagnose<DiagID::ArrayElementTypeMismatch>(loc, et.toString(),
                                                                   resolved.toString());
            impl->hadErrors = true;
            elementMismatch = true;
          }
        }
      }
    }
  }

  if (elementMismatch) { return storeType(expr, zc::heap<type::ErrorType>()); }

  if (elemType == zc::none) {
    auto& tv = impl->typeEnv.freshTypeVar("array_elem"_zc);
    return storeType(expr, zc::heap<type::ArrayType>(cloneResolvedType(tv)));
  }

  ZC_IF_SOME(et, elemType) {
    return storeType(expr, zc::heap<type::ArrayType>(cloneResolvedType(et)));
  }

  return storeType(expr, zc::heap<type::ArrayType>(zc::heap<type::ErrorType>()));
}

const type::Type& BodyChecker::checkTupleLiteral(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);

  zc::Vector<zc::Own<type::Type>> elemTypes;
  bool hasErrorElement = false;

  if (node.kind == SyntaxKind::TupleLiteral) {
    NodeList elems;
    elems.first = node.payload.words[kTupleLiteralElemsFirstWord];
    elems.size = node.payload.words[kTupleLiteralElemsSizeWord];

    for (NodeId elemId : impl->tree.list(elems)) {
      auto& elemTy = checkExpr(elemId);
      auto& resolved = impl->typeEnv.find(elemTy);
      if (isError(resolved)) { hasErrorElement = true; }
      elemTypes.add(cloneResolvedType(resolved));
    }
  } else if (node.kind == SyntaxKind::TupleLiteral1) {
    // Single-element tuple
    auto elemId = NodeId(node.payload.words[kTupleLiteral1ElemWord]);
    auto& elemTy = checkExpr(elemId);
    auto& resolved = impl->typeEnv.find(elemTy);
    if (isError(resolved)) { hasErrorElement = true; }
    elemTypes.add(cloneResolvedType(resolved));
  }

  if (hasErrorElement) { return storeType(expr, zc::heap<type::ErrorType>()); }

  return storeType(expr, zc::heap<type::TupleType>(zc::mv(elemTypes)));
}

const type::Type& BodyChecker::checkIsExpr(ast::NodeId expr) {
  const auto& node = impl->tree.node(expr);
  (void)node;
  // `expr is Type` always returns bool
  return storeType(expr, zc::heap<type::PrimitiveType>(type::PrimitiveKind::Bool));
}

const type::Type& BodyChecker::checkAsExpr(ast::NodeId expr) { return checkCastExpr(expr); }

const type::Type& BodyChecker::checkThisExpr(ast::NodeId expr) {
  // `this` returns the enclosing class type
  // Simplified: return a named type for the current class context
  auto& scope = currentScope();
  auto scopeName = scope.getName();
  if (scopeName.size() > 0) { return storeType(expr, zc::heap<type::NamedType>(scopeName)); }
  return storeType(expr, zc::heap<type::ErrorType>());
}

const type::Type& BodyChecker::checkSuperExpr(ast::NodeId expr) {
  // `super` returns the superclass type
  // Simplified: return error type for now
  return storeType(expr, zc::heap<type::ErrorType>());
}

// ============================================================================
// Statement checking
// ============================================================================

void BodyChecker::checkStmt(ast::NodeId stmt) {
  if (!impl->tree.contains(stmt)) return;

  const auto& node = impl->tree.node(stmt);
  if (node.kind == SyntaxKind::StatementListItem) {
    checkStmt(NodeId(node.payload.words[kStatementListItemItemWord]));
    return;
  }

  switch (node.kind) {
    case SyntaxKind::BlockStmt:
      checkBlockStmt(stmt);
      break;
    case SyntaxKind::IfStmt:
      checkIfStmt(stmt);
      break;
    case SyntaxKind::WhileStmt:
      checkWhileStmt(stmt);
      break;
    case SyntaxKind::ForStmt:
      checkForStmt(stmt);
      break;
    case SyntaxKind::ReturnStmt:
      checkReturnStmt(stmt);
      break;
    case SyntaxKind::LetStmt:
      checkLetStmt(stmt);
      break;
    case SyntaxKind::MatchStmt:
      checkMatchStmt(stmt);
      break;
    case SyntaxKind::BreakStmt:
      break;  // Nothing to type-check
    case SyntaxKind::ContinueStatement:
      break;  // Nothing to type-check
    case SyntaxKind::ExpressionStatement: {
      auto exprId = NodeId(node.payload.words[kExpressionStatementExpressionWord]);
      if (impl->tree.contains(exprId)) { checkExpr(exprId); }
      break;
    }
    default:
      // For declaration statements, check their bodies
      if (node.kind == SyntaxKind::FunctionDecl) { checkFunctionDecl(stmt); }
      break;
  }
}

void BodyChecker::checkBlockStmt(ast::NodeId stmt) {
  const auto& node = impl->tree.node(stmt);
  NodeList stmts;
  stmts.first = node.payload.words[kBlockStmtStmtsFirstWord];
  stmts.size = node.payload.words[kBlockStmtStmtsSizeWord];

  for (NodeId childId : impl->tree.list(stmts)) { checkStmt(childId); }
}

void BodyChecker::checkIfStmt(ast::NodeId stmt) {
  const auto& node = impl->tree.node(stmt);
  auto condId = NodeId(node.payload.words[kIfStmtCondWord]);
  auto thenId = NodeId(node.payload.words[kIfStmtThenStmtWord]);
  auto elseId = NodeId(node.payload.words[kIfStmtElseStmtWord]);

  // Check condition is bool
  auto& condType = checkExpr(condId);
  auto& resolvedCond = impl->typeEnv.find(condType);
  if (!isPrimitive(resolvedCond) && !isError(resolvedCond)) {
    auto loc = getNodeLoc(impl->tree, condId);
    impl->diags.diagnose<DiagID::ConditionMustBeBool>(loc);
    impl->hadErrors = true;
  }
  if (isPrimitive(resolvedCond) &&
      static_cast<const type::PrimitiveType&>(resolvedCond).getPrimitiveKind() !=
          type::PrimitiveKind::Bool &&
      !isError(resolvedCond)) {
    auto loc = getNodeLoc(impl->tree, condId);
    impl->diags.diagnose<DiagID::ConditionMustBeBool>(loc);
    impl->hadErrors = true;
  }

  // Check then branch
  checkStmt(thenId);

  // Check else branch if present
  if (impl->tree.contains(elseId)) { checkStmt(elseId); }
}

void BodyChecker::checkWhileStmt(ast::NodeId stmt) {
  const auto& node = impl->tree.node(stmt);
  auto condId = NodeId(node.payload.words[kWhileStmtCondWord]);
  auto bodyId = NodeId(node.payload.words[kWhileStmtBodyWord]);

  auto& condType = checkExpr(condId);
  auto& resolvedCond = impl->typeEnv.find(condType);
  if (!isError(resolvedCond) &&
      (!isPrimitive(resolvedCond) ||
       static_cast<const type::PrimitiveType&>(resolvedCond).getPrimitiveKind() !=
           type::PrimitiveKind::Bool)) {
    auto loc = getNodeLoc(impl->tree, condId);
    impl->diags.diagnose<DiagID::ConditionMustBeBool>(loc);
    impl->hadErrors = true;
  }

  checkStmt(bodyId);
}

void BodyChecker::checkForStmt(ast::NodeId stmt) {
  const auto& node = impl->tree.node(stmt);
  auto initId = NodeId(node.payload.words[kForStmtInitWord]);
  auto condId = NodeId(node.payload.words[kForStmtCondWord]);
  auto updateId = NodeId(node.payload.words[kForStmtUpdateWord]);
  auto bodyId = NodeId(node.payload.words[kForStmtBodyWord]);

  // Check init (can be empty or a declaration/expression)
  if (impl->tree.contains(initId)) { checkStmt(initId); }

  // Check condition
  if (impl->tree.contains(condId)) {
    auto& condType = checkExpr(condId);
    auto& resolvedCond = impl->typeEnv.find(condType);
    if (!isError(resolvedCond) &&
        (!isPrimitive(resolvedCond) ||
         static_cast<const type::PrimitiveType&>(resolvedCond).getPrimitiveKind() !=
             type::PrimitiveKind::Bool)) {
      auto loc = getNodeLoc(impl->tree, condId);
      impl->diags.diagnose<DiagID::ConditionMustBeBool>(loc);
      impl->hadErrors = true;
    }
  }

  // Check update
  if (impl->tree.contains(updateId)) { checkExpr(updateId); }

  // Check body
  checkStmt(bodyId);
}

void BodyChecker::checkReturnStmt(ast::NodeId stmt) {
  const auto& node = impl->tree.node(stmt);
  auto valueId = NodeId(node.payload.words[kReturnStmtValueWord]);

  // If there is no expected return type (function without explicit return type
  // annotation), we are in "inference mode": accept any return statement and
  // do not report type mismatches.
  if (impl->expectedRetType == zc::none) {
    if (impl->tree.contains(valueId) && valueId != NodeId()) { checkExpr(valueId); }
    return;
  }

  if (!impl->tree.contains(valueId) || valueId == NodeId()) {
    // Return without value: expected return type should be unit
    auto& expected = expectedReturnType();
    auto& resolvedExpected = impl->typeEnv.find(expected);
    if (!isUnit(resolvedExpected) && !isError(resolvedExpected) && !isTypeVar(resolvedExpected)) {
      auto loc = getNodeLoc(impl->tree, stmt);
      impl->diags.diagnose<DiagID::MissingReturnValue>(loc, resolvedExpected.toString());
      impl->hadErrors = true;
    }
    return;
  }

  // Check return value type against expected
  auto& valueType = checkExpr(valueId);
  auto& resolvedValue = impl->typeEnv.find(valueType);
  auto& expected = expectedReturnType();
  auto& resolvedExpected = impl->typeEnv.find(expected);

  checkAssignable(resolvedExpected, resolvedValue, stmt);
}

void BodyChecker::checkLetStmt(ast::NodeId stmt) {
  const auto& node = impl->tree.node(stmt);
  auto declsId = NodeId(node.payload.words[kLetStmtDeclarationsWord]);

  if (!impl->tree.contains(declsId)) return;

  // Walk variable declarators
  visitTreePreOrder(impl->tree, declsId, [&](NodeId declId, const Node& declNode) {
    if (declNode.kind != SyntaxKind::VariableDeclarator) return;

    auto initId = NodeId(declNode.payload.words[kVariableDeclaratorInitWord]);
    auto tyId = NodeId(declNode.payload.words[kVariableDeclaratorTyWord]);

    if (!impl->tree.contains(initId)) return;

    // Check initializer
    auto& initType = checkExpr(initId);
    auto& resolvedInit = impl->typeEnv.find(initType);

    // If there's an explicit type annotation, check compatibility
    if (impl->tree.contains(tyId)) {
      auto annotatedType = resolveTypeExpr(tyId);
      if (annotatedType) {
        auto& resolvedAnnotated = impl->typeEnv.find(*annotatedType);
        checkAssignable(resolvedAnnotated, resolvedInit, declId);
        if (impl->hadErrors) {
          impl->typeEnv.setType(declId, zc::heap<type::ErrorType>());
          return;
        }
        impl->typeEnv.setType(declId, cloneResolvedType(resolvedAnnotated));
        return;
      }
    }

    if (isNull(resolvedInit)) {
      auto loc = getNodeLoc(impl->tree, declId);
      impl->diags.diagnose<DiagID::CannotInferNullInitializer>(loc);
      impl->hadErrors = true;
      impl->typeEnv.setType(declId, zc::heap<type::ErrorType>());
      return;
    }

    if (impl->tree.node(initId).kind == SyntaxKind::IntLiteral) {
      auto& tv = impl->typeEnv.freshTypeVar("local_int"_zc);
      impl->typeEnv.setType(declId, cloneResolvedType(tv));
      impl->pendingLocalIntDeclarations.add(declId);
      return;
    }

    // Store the type for the declarator node
    impl->typeEnv.setType(declId, cloneResolvedType(resolvedInit));
  });
}

void BodyChecker::checkMatchStmt(ast::NodeId stmt) {
  const auto& node = impl->tree.node(stmt);
  auto scrutineeId = NodeId(node.payload.words[kMatchStmtScrutineeWord]);
  (void)scrutineeId;

  // Check scrutinee
  auto& scrutineeType = checkExpr(scrutineeId);
  auto& resolvedScrutinee = impl->typeEnv.find(scrutineeType);
  (void)resolvedScrutinee;

  // Store the scrutinee type for exhaustiveness checking
  impl->typeEnv.setType(stmt, zc::heap<type::ErrorType>());

  // Check each match arm
  NodeList arms;
  arms.first = node.payload.words[kMatchStmtArmsFirstWord];
  arms.size = node.payload.words[kMatchStmtArmsSizeWord];

  for (NodeId armId : impl->tree.list(arms)) {
    if (!impl->tree.contains(armId)) continue;
    const auto& armNode = impl->tree.node(armId);
    if (armNode.kind != SyntaxKind::MatchArmStmt) continue;

    // Check the arm body statement.
    auto bodyId = NodeId(armNode.payload.words[kMatchArmStmtBodyWord]);
    if (impl->tree.contains(bodyId)) { checkStmt(bodyId); }

    // Check guard if present
    auto guardId = NodeId(armNode.payload.words[kMatchArmStmtGuardWord]);
    if (impl->tree.contains(guardId)) {
      auto& guardType = checkExpr(guardId);
      auto& resolvedGuard = impl->typeEnv.find(guardType);
      if (!isError(resolvedGuard) &&
          (!isPrimitive(resolvedGuard) ||
           static_cast<const type::PrimitiveType&>(resolvedGuard).getPrimitiveKind() !=
               type::PrimitiveKind::Bool)) {
        auto loc = getNodeLoc(impl->tree, guardId);
        impl->diags.diagnose<DiagID::MatchGuardMustBeBool>(loc);
        impl->hadErrors = true;
      }
    }
  }

  // Run exhaustiveness checker
  ExhaustivenessChecker exhaustChecker(impl->typeEnv, impl->tree, impl->diags);
  exhaustChecker.checkMatchExhaustiveness(stmt, resolvedScrutinee);
}

void BodyChecker::checkFunctionDecl(ast::NodeId declId) {
  const auto& node = impl->tree.node(declId);
  auto bodyId = NodeId(node.payload.words[kFunctionDeclBodyWord]);
  auto retTyId = NodeId(node.payload.words[kFunctionDeclRetTyWord]);

  if (!impl->tree.contains(bodyId)) return;

  // Enter the function's scope so that parameter names are visible during
  // body checking. Look up the function scope by name (registered by DeclCollector
  // via ScopeManager::createScope).
  bool pushedScope = false;
  auto fnName = impl->tree.ident(IdentId(node.payload.words[kFunctionDeclNameWord]));
  if (fnName.size() > 0) {
    auto funcScope = impl->symbols.getScopeManager().getFunctionScope(fnName);
    ZC_IF_SOME(scope, funcScope) {
      impl->scopeStack.add(&scope);
      impl->symbols.getScopeManager().pushScope(scope);
      pushedScope = true;
    }
  }

  // Save and set expected return type.
  // Only enforce return type checking if the function has an explicit return type
  // annotation in the source. Functions without annotations get `unit` from
  // DeclSignatureComputer, but we don't want to report errors for `return expr`
  // in those cases — the return type is effectively inferred.
  auto oldRetType = impl->expectedRetType;
  auto oldRaisesType = impl->expectedRaisesType;

  bool hasExplicitRetTy =
      impl->tree.contains(retTyId) && impl->tree.node(retTyId).kind != SyntaxKind::Unknown;

  if (hasExplicitRetTy && impl->typeEnv.hasType(declId)) {
    auto& declType = impl->typeEnv.getType(declId);
    auto& resolved = impl->typeEnv.find(declType);
    if (isFunction(resolved)) {
      auto& funcTy = static_cast<const type::FunctionType&>(resolved);
      impl->expectedRetType = funcTy.getReturnType();
      auto raisesType = funcTy.getRaisesType();
      ZC_IF_SOME(rt, raisesType) { impl->expectedRaisesType = rt; }
    }
  } else {
    // No explicit return type: leave expectedRetType as-is (none or
    // inherited from outer context).  checkReturnStmt() treats a none
    // expectedRetType as "inference mode" and does not report type mismatches.
  }

  // Check the function body
  checkStmt(bodyId);

  // Restore expected return type
  impl->expectedRetType = oldRetType;
  impl->expectedRaisesType = oldRaisesType;

  // Pop the function scope if we pushed it
  if (pushedScope) { impl->popNodeScope(); }
}

// ============================================================================
// Main entry point
// ============================================================================

bool BodyChecker::checkBodies() {
  const auto rootId = impl->tree.root();
  if (!impl->tree.contains(rootId)) return true;

  // Extract the top-level statement list from the SourceFile root.
  // SourceFile.statements is a NodeList of StatementListItem at
  // kSourceFileStatementsFirstWord / kSourceFileStatementsSizeWord.
  const auto& rootNode = impl->tree.node(rootId);
  NodeList topStmts;
  topStmts.first = rootNode.payload.words[kSourceFileStatementsFirstWord];
  topStmts.size = rootNode.payload.words[kSourceFileStatementsSizeWord];

  // Process only top-level items. We do NOT recurse into function/class bodies
  // here — those are checked by checkFunctionDecl / checkClassDecl themselves,
  // which push the correct scope before checking. A full pre-order walk would
  // re-check body expressions without the proper scope context, causing false
  // "undeclared identifier" errors for parameters.
  for (NodeId id : impl->tree.list(topStmts)) {
    if (!impl->tree.contains(id)) continue;
    const auto& node = impl->tree.node(id);
    // StatementListItem wraps the actual statement.
    NodeId itemId = id;
    if (node.kind == SyntaxKind::StatementListItem) {
      itemId = NodeId(node.payload.words[kStatementListItemItemWord]);
      if (!impl->tree.contains(itemId)) continue;
    }
    const auto& itemNode = impl->tree.node(itemId);

    switch (itemNode.kind) {
      case SyntaxKind::FunctionDecl:
      case SyntaxKind::MethodDecl:
        checkFunctionDecl(itemId);
        break;
      case SyntaxKind::VariableDeclarator: {
        auto initId = NodeId(itemNode.payload.words[kVariableDeclaratorInitWord]);
        if (impl->tree.contains(initId)) {
          auto& initTy = checkExpr(initId);
          impl->typeEnv.setType(itemId, cloneResolvedType(initTy));
        }
        break;
      }
      case SyntaxKind::LetStmt: {
        checkLetStmt(itemId);
        break;
      }
      case SyntaxKind::ExpressionStatement: {
        auto exprId = NodeId(itemNode.payload.words[kExpressionStatementExpressionWord]);
        if (impl->tree.contains(exprId)) { checkExpr(exprId); }
        break;
      }
      default:
        // For any other top-level expression-like node, try to check it.
        if (itemNode.kind == SyntaxKind::IntLiteral ||
            itemNode.kind == SyntaxKind::FloatLiteralExpr ||
            itemNode.kind == SyntaxKind::StrLiteral || itemNode.kind == SyntaxKind::BoolLiteral ||
            itemNode.kind == SyntaxKind::NullLiteral || itemNode.kind == SyntaxKind::UnitLiteral ||
            itemNode.kind == SyntaxKind::BinaryExpr ||
            itemNode.kind == SyntaxKind::UnaryExpression ||
            itemNode.kind == SyntaxKind::PostfixExpression ||
            itemNode.kind == SyntaxKind::ConditionalExpr ||
            itemNode.kind == SyntaxKind::NullCoalesceExpr ||
            itemNode.kind == SyntaxKind::AssignmentExpr ||
            itemNode.kind == SyntaxKind::CallExpression || itemNode.kind == SyntaxKind::IdentExpr ||
            itemNode.kind == SyntaxKind::MemberExpression ||
            itemNode.kind == SyntaxKind::IndexExpression ||
            itemNode.kind == SyntaxKind::NewExpression ||
            itemNode.kind == SyntaxKind::CastExpression ||
            itemNode.kind == SyntaxKind::LambdaExpression ||
            itemNode.kind == SyntaxKind::FunctionExpression ||
            itemNode.kind == SyntaxKind::UnsafeBlockExpr ||
            itemNode.kind == SyntaxKind::IsExpression ||
            itemNode.kind == SyntaxKind::ObjectLiteralExpr ||
            itemNode.kind == SyntaxKind::StructLiteralExpr ||
            itemNode.kind == SyntaxKind::ArrayLiteral ||
            itemNode.kind == SyntaxKind::TupleLiteral) {
          checkExpr(itemId);
        }
        break;
    }
  }

  for (NodeId declId : impl->pendingLocalIntDeclarations) {
    if (!impl->tree.contains(declId) || !impl->typeEnv.hasType(declId)) continue;
    auto& declTy = impl->typeEnv.getType(declId);
    auto& resolvedDecl = impl->typeEnv.find(declTy);
    if (isTypeVar(resolvedDecl)) {
      impl->typeEnv.setType(declId, zc::heap<type::PrimitiveType>(type::PrimitiveKind::I32));
      for (const auto& entry : impl->identExprDeclarations) {
        if (entry.value != declId) continue;
        NodeId identId(entry.key);
        if (impl->tree.contains(identId)) {
          impl->typeEnv.setType(identId, zc::heap<type::PrimitiveType>(type::PrimitiveKind::I32));
        }
      }
    }
  }

  return !impl->hadErrors;
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
