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

#include "zomlang/compiler/checker/trait-resolver.h"

#include "zc/core/common.h"
#include "zc/core/map.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/checker/type-expr-utils.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/symbol.h"
#include "zomlang/compiler/symbol/type-symbol.h"
#include "zomlang/compiler/symbol/value-symbol.h"
#include "zomlang/compiler/type/array-type.h"
#include "zomlang/compiler/type/error-type.h"
#include "zomlang/compiler/type/existential-type.h"
#include "zomlang/compiler/type/function-type.h"
#include "zomlang/compiler/type/intersection-type.h"
#include "zomlang/compiler/type/named-type.h"
#include "zomlang/compiler/type/object-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/raw-pointer-type.h"
#include "zomlang/compiler/type/reference-type.h"
#include "zomlang/compiler/type/tuple-type.h"
#include "zomlang/compiler/type/type-env.h"
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
// Impl struct (PIMPL)
// ============================================================================

struct TraitResolver::Impl {
  type::TypeEnv& typeEnv;
  symbol::SymbolTable& symbols;
  const ast::Tree& tree;
  const ast::BindingMetadata& metadata;
  diagnostics::DiagnosticEngine& diags;

  // Cache of discovered impls: (typeName, ifaceName) -> impl NodeId
  zc::HashMap<zc::String, ast::NodeId> implCache;
  zc::HashSet<zc::String> positiveMarkerImpls;
  zc::HashSet<zc::String> negativeMarkerImpls;

  // Track which (type, interface) pairs we've seen for coherence checking
  zc::HashMap<zc::String, ast::NodeId> seenImpls;
  struct ImplRecord {
    zc::String typeKey;
    zc::String ifaceKey;
    ast::NodeId node;
    bool blanket;
  };
  zc::Vector<ImplRecord> coherenceImpls;

  bool hadErrors = false;

  Impl(type::TypeEnv& te, symbol::SymbolTable& sym, const ast::Tree& t,
       const ast::BindingMetadata& meta, diagnostics::DiagnosticEngine& d)
      : typeEnv(te), symbols(sym), tree(t), metadata(meta), diags(d) {}
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

TraitResolver::TraitResolver(type::TypeEnv& typeEnv, symbol::SymbolTable& symbols,
                             const ast::Tree& tree, const ast::BindingMetadata& metadata,
                             diagnostics::DiagnosticEngine& diags) noexcept
    : impl(zc::heap<Impl>(typeEnv, symbols, tree, metadata, diags)) {}

TraitResolver::~TraitResolver() noexcept(false) = default;

// ============================================================================
// Helpers
// ============================================================================

static source::SourceLoc nodeLoc(const ast::Tree& tree, ast::NodeId id) {
  return tree.node(id).range.getStart();
}

zc::StringPtr TraitResolver::resolvePathName(ast::NodeId pathNode) {
  if (!impl->tree.contains(pathNode)) return ""_zc;
  const auto& path = impl->tree.node(pathNode);

  if (path.kind == SyntaxKind::NamedTypeExpr) {
    auto nestedPath = ast::NodeId(path.payload.words[kNamedTypeExprPathWord]);
    return resolvePathName(nestedPath);
  }

  if (path.kind == SyntaxKind::ModulePath) {
    IdentList segments;
    segments.first = path.payload.words[kModulePathSegmentsFirstWord];
    segments.size = path.payload.words[kModulePathSegmentsSizeWord];

    auto segIds = impl->tree.identList(segments);
    if (segIds.size() == 0) return ""_zc;

    // Return the last segment (simple name)
    auto lastSeg = segIds.back();
    return impl->tree.ident(lastSeg);
  }

  if (path.kind == SyntaxKind::AttributePath) {
    IdentList segments;
    segments.first = path.payload.words[kAttributePathSegmentsFirstWord];
    segments.size = path.payload.words[kAttributePathSegmentsSizeWord];

    auto segIds = impl->tree.identList(segments);
    if (segIds.size() == 0) return ""_zc;

    auto lastSeg = segIds.back();
    return impl->tree.ident(lastSeg);
  }

  // If it's an identifier expression, extract the name
  if (path.kind == SyntaxKind::IdentExpr) {
    auto name = impl->tree.ident(IdentId(path.payload.words[kIdentExprNameWord]));
    return name;
  }

  return ""_zc;
}

const symbol::Scope& TraitResolver::currentScope() {
  auto scope = impl->symbols.getCurrentScope();
  ZC_IF_SOME(s, scope) { return s; }

  auto constGlobal = impl->symbols.getScopeManager().getGlobalScope();
  ZC_IF_SOME(cg, constGlobal) { return cg; }

  ZC_UNREACHABLE;
}

zc::Maybe<symbol::Symbol&> TraitResolver::lookupSymbol(zc::StringPtr name) {
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

zc::StringPtr TraitResolver::getTypeName(const type::Type& ty) {
  const auto& resolved = impl->typeEnv.find(ty);

  if (isNamed(resolved)) {
    const auto& named = static_cast<const NamedType&>(resolved);
    return named.getName();
  }

  if (isPrimitive(resolved)) {
    const auto& prim = static_cast<const PrimitiveType&>(resolved);
    return prim.getName();
  }

  // For compound types, generate a string representation
  return ""_zc;
}

static zc::String markerImplKey(zc::StringPtr typeName, zc::StringPtr markerName) {
  return zc::str(typeName, "::", markerName);
}

bool TraitResolver::isTypeLocal(const type::Type& ty) {
  auto name = getTypeName(ty);
  if (name.size() == 0) return false;

  auto sym = lookupSymbol(name);
  if (sym == zc::none) return false;

  // A type is "local" if it has declaration refs in the current AST
  ZC_IF_SOME(s, sym) {
    auto declRefs = s.getDeclarationRefs();
    for (const auto& ref : declRefs) {
      if (impl->tree.contains(ref.node)) return true;
    }
  }

  return false;
}

bool TraitResolver::isInterfaceLocal(zc::StringPtr ifaceName) {
  if (ifaceName.size() == 0) return false;

  auto sym = lookupSymbol(ifaceName);
  if (sym == zc::none) return false;

  ZC_IF_SOME(s, sym) {
    auto declRefs = s.getDeclarationRefs();
    for (const auto& ref : declRefs) {
      if (impl->tree.contains(ref.node)) return true;
    }
  }

  return false;
}

// ============================================================================
// Type expression resolution (simplified - reuses DeclSignatureComputer patterns)
// ============================================================================

zc::Own<type::Type> TraitResolver::resolveTypeExpr(ast::NodeId typeExprId) {
  if (!impl->tree.contains(typeExprId)) {
    return zc::heap<type::ErrorType>("invalid type expression");
  }

  const auto& node = impl->tree.node(typeExprId);

  switch (node.kind) {
    case SyntaxKind::PredefinedTypeExpr: {
      auto kindVal = static_cast<uint8_t>(node.payload.words[kPredefinedTypeExprKindWord]);
      auto primKind = static_cast<type::PrimitiveKind>(kindVal);
      return type::PrimitiveType::create(primKind);
    }

    case SyntaxKind::NamedTypeExpr: {
      auto pathId = ast::NodeId(node.payload.words[kNamedTypeExprPathWord]);
      auto name = resolvePathName(pathId);

      if (name.size() == 0) { return zc::heap<type::ErrorType>("empty type name"); }

      auto primitiveKind = type::PrimitiveType::findByName(name);
      if (primitiveKind != zc::none) {
        ZC_IF_SOME(kind, primitiveKind) { return type::PrimitiveType::create(kind); }
      }

      auto namedTy = zc::heap<type::NamedType>(name);
      auto sym = lookupSymbol(name);
      ZC_IF_SOME(s, sym) {
        if (s.isTypeSymbol()) { namedTy->setSymbol(static_cast<const symbol::TypeSymbol&>(s)); }
      }

      // Resolve generic type arguments
      NodeList argsList;
      argsList.first = node.payload.words[kNamedTypeExprArgsFirstWord];
      argsList.size = node.payload.words[kNamedTypeExprArgsSizeWord];

      if (argsList.size > 0) {
        for (ast::NodeId argId : impl->tree.list(argsList)) {
          auto argType = resolveTypeExpr(argId);
          namedTy->addTypeArg(zc::mv(argType));
        }
      }

      return namedTy;
    }

    case SyntaxKind::TupleTypeExpr: {
      NodeList elemsList;
      elemsList.first = node.payload.words[kTupleTypeExprElemsFirstWord];
      elemsList.size = node.payload.words[kTupleTypeExprElemsSizeWord];

      zc::Vector<zc::Own<type::Type>> elemTypes;
      for (ast::NodeId elemId : impl->tree.list(elemsList)) {
        elemTypes.add(resolveTypeExpr(elemId));
      }

      if (elemTypes.empty()) { return type::PrimitiveType::createUnit(); }

      return zc::heap<type::TupleType>(zc::mv(elemTypes));
    }

    case SyntaxKind::ArrayTypeExpr:
    case SyntaxKind::FixedArrayTypeExpr:
    case SyntaxKind::SliceArrayTypeExpr: {
      ast::NodeId elemId;
      if (node.kind == SyntaxKind::ArrayTypeExpr) {
        elemId = ast::NodeId(node.payload.words[kArrayTypeExprElemWord]);
      } else if (node.kind == SyntaxKind::FixedArrayTypeExpr) {
        elemId = ast::NodeId(node.payload.words[kFixedArrayTypeExprElemWord]);
      } else {
        elemId = ast::NodeId(node.payload.words[kSliceArrayTypeExprElemWord]);
      }

      if (!impl->tree.contains(elemId)) {
        return zc::heap<type::ErrorType>("missing array element type");
      }

      auto elemType = resolveTypeExpr(elemId);
      return zc::heap<type::ArrayType>(zc::mv(elemType));
    }

    case SyntaxKind::FunctionTypeExpr: {
      NodeList paramsList;
      paramsList.first = node.payload.words[kFunctionTypeExprParamsFirstWord];
      paramsList.size = node.payload.words[kFunctionTypeExprParamsSizeWord];

      zc::Vector<zc::Own<type::Type>> paramTypes;
      for (ast::NodeId paramId : impl->tree.list(paramsList)) {
        paramTypes.add(resolveTypeExpr(paramId));
      }

      auto retTyId = ast::NodeId(node.payload.words[kFunctionTypeExprRetTyWord]);
      zc::Own<type::Type> returnType;
      if (impl->tree.contains(retTyId)) {
        returnType = resolveTypeExpr(retTyId);
      } else {
        returnType = type::PrimitiveType::createUnit();
      }

      return zc::heap<type::FunctionType>(zc::mv(paramTypes), zc::mv(returnType));
    }

    case SyntaxKind::UnionTypeExpr: {
      NodeList altsList;
      altsList.first = node.payload.words[kUnionTypeExprAltsFirstWord];
      altsList.size = node.payload.words[kUnionTypeExprAltsSizeWord];

      zc::Vector<zc::Own<type::Type>> alternatives;
      for (ast::NodeId altId : impl->tree.list(altsList)) {
        alternatives.add(resolveTypeExpr(altId));
      }

      if (alternatives.empty()) { return type::PrimitiveType::createNever(); }

      return zc::heap<type::UnionType>(zc::mv(alternatives));
    }

    case SyntaxKind::IntersectionTypeExpr: {
      NodeList altsList;
      altsList.first = node.payload.words[kIntersectionTypeExprAltsFirstWord];
      altsList.size = node.payload.words[kIntersectionTypeExprAltsSizeWord];

      zc::Vector<zc::Own<type::Type>> conjuncts;
      for (ast::NodeId altId : impl->tree.list(altsList)) { conjuncts.add(resolveTypeExpr(altId)); }

      if (conjuncts.empty()) { return type::PrimitiveType::createAny(); }

      return zc::heap<type::IntersectionType>(zc::mv(conjuncts));
    }

    case SyntaxKind::BottomTypeExpr:
      return type::PrimitiveType::createNever();

    case SyntaxKind::UnaryExpression: {
      auto op = static_cast<ast::UnaryOperatorKind>(node.payload.words[kUnaryExpressionOpWord]);
      auto operandId = ast::NodeId(node.payload.words[kUnaryExpressionOperandWord]);

      if (op == ast::UnaryOperatorKind::Ref) {
        // &T or &mut T
        auto mutability = type::Mutability::Const;

        if (impl->tree.contains(operandId)) {
          const auto& operand = impl->tree.node(operandId);
          if (operand.kind == SyntaxKind::NamedTypeExpr) {
            auto pathId = ast::NodeId(operand.payload.words[kNamedTypeExprPathWord]);
            auto name = resolvePathName(pathId);
            if (name == "mut"_zc) {
              mutability = type::Mutability::Mutable;
              NodeList args;
              args.first = operand.payload.words[kNamedTypeExprArgsFirstWord];
              args.size = operand.payload.words[kNamedTypeExprArgsSizeWord];
              if (args.size > 0) {
                auto firstArg = impl->tree.list(args).front();
                auto pointeeType = resolveTypeExpr(firstArg);
                return zc::heap<type::ReferenceType>(zc::mv(pointeeType), mutability);
              }
            }
          }
        }

        auto innerType = resolveTypeExpr(operandId);
        return zc::heap<type::ReferenceType>(zc::mv(innerType), mutability);
      }

      if (op == ast::UnaryOperatorKind::Deref) {
        // *T, *const T, *mut T
        auto mutability = type::Mutability::Const;

        if (impl->tree.contains(operandId)) {
          const auto& operand = impl->tree.node(operandId);
          if (operand.kind == SyntaxKind::NamedTypeExpr) {
            auto pathId = ast::NodeId(operand.payload.words[kNamedTypeExprPathWord]);
            auto name = resolvePathName(pathId);
            if (name == "mut"_zc) {
              mutability = type::Mutability::Mutable;
              NodeList args;
              args.first = operand.payload.words[kNamedTypeExprArgsFirstWord];
              args.size = operand.payload.words[kNamedTypeExprArgsSizeWord];
              if (args.size > 0) {
                auto firstArg = impl->tree.list(args).front();
                auto pointeeType = resolveTypeExpr(firstArg);
                return zc::heap<type::RawPointerType>(zc::mv(pointeeType), mutability);
              }
            }
            if (name == "const"_zc) {
              mutability = type::Mutability::Const;
              NodeList args;
              args.first = operand.payload.words[kNamedTypeExprArgsFirstWord];
              args.size = operand.payload.words[kNamedTypeExprArgsSizeWord];
              if (args.size > 0) {
                auto firstArg = impl->tree.list(args).front();
                auto pointeeType = resolveTypeExpr(firstArg);
                return zc::heap<type::RawPointerType>(zc::mv(pointeeType), mutability);
              }
            }
          }
        }

        auto innerType = resolveTypeExpr(operandId);
        return zc::heap<type::RawPointerType>(zc::mv(innerType), mutability);
      }

      break;
    }

    case SyntaxKind::OptionalTypeExpr: {
      auto innerId = ast::NodeId(node.payload.words[kOptionalTypeExprInnerWord]);
      if (!impl->tree.contains(innerId)) {
        return zc::heap<type::ErrorType>("invalid optional type");
      }

      auto innerType = resolveTypeExpr(innerId);
      zc::Vector<zc::Own<type::Type>> alternatives;
      alternatives.add(zc::mv(innerType));
      alternatives.add(type::PrimitiveType::createNull());
      return zc::heap<type::UnionType>(zc::mv(alternatives));
    }

    case SyntaxKind::DynTypeExpr: {
      auto ifacesId = ast::NodeId(node.payload.words[kDynTypeExprIfacesIdWord]);
      auto markerNames = dynMarkerNames(impl->tree, node);
      if (!impl->tree.contains(ifacesId)) {
        return zc::heap<type::ErrorType>("dyn type requires at least one interface");
      }

      const auto& ifaceListNode = impl->tree.node(ifacesId);
      if (ifaceListNode.kind != SyntaxKind::DynTypeIfaceList) {
        auto ifaceType = resolveTypeExpr(ifacesId);
        return zc::heap<type::ExistentialType>(zc::mv(ifaceType), markerNames.asPtr());
      }

      NodeList ifaceNodeList;
      ifaceNodeList.first = ifaceListNode.payload.words[kDynTypeIfaceListIfacesFirstWord];
      ifaceNodeList.size = ifaceListNode.payload.words[kDynTypeIfaceListIfacesSizeWord];

      auto ifaces = impl->tree.list(ifaceNodeList);
      if (ifaces.size() == 0) {
        return zc::heap<type::ErrorType>("dyn type requires at least one interface");
      }

      if (ifaces.size() > 1) {
        zc::Vector<zc::Own<type::Type>> conjuncts;
        for (ast::NodeId ifaceId : ifaces) { conjuncts.add(resolveTypeExpr(ifaceId)); }
        auto interTy = zc::heap<type::IntersectionType>(zc::mv(conjuncts));
        return zc::heap<type::ExistentialType>(zc::mv(interTy), markerNames.asPtr());
      }

      auto firstIfaceId = ifaces.front();
      auto ifaceType = resolveTypeExpr(firstIfaceId);
      return zc::heap<type::ExistentialType>(zc::mv(ifaceType), markerNames.asPtr());
    }

    case SyntaxKind::ObjectTypeExpr: {
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

    default:
      break;
  }

  return zc::heap<type::ErrorType>("unsupported type expression");
}

// ============================================================================
// Impl type and interface extraction
// ============================================================================

zc::Own<type::Type> TraitResolver::resolveImplForType(ast::NodeId implNode) {
  if (!impl->tree.contains(implNode)) { return zc::heap<type::ErrorType>("invalid impl node"); }

  const auto& node = impl->tree.node(implNode);

  ast::NodeId forTyId;
  if (node.kind == SyntaxKind::StandaloneImplDecl) {
    forTyId = ast::NodeId(node.payload.words[kStandaloneImplDeclForTyWord]);
  } else if (node.kind == SyntaxKind::MarkerImpl) {
    forTyId = ast::NodeId(node.payload.words[kMarkerImplForTyWord]);
  } else {
    return zc::heap<type::ErrorType>("not an impl declaration");
  }

  if (!impl->tree.contains(forTyId)) { return zc::heap<type::ErrorType>("missing impl for-type"); }

  return resolveTypeExpr(forTyId);
}

zc::Vector<zc::StringPtr> TraitResolver::resolveImplIfaceNames(ast::NodeId implNode) {
  zc::Vector<zc::StringPtr> names;

  if (!impl->tree.contains(implNode)) return names;

  const auto& node = impl->tree.node(implNode);

  if (node.kind == SyntaxKind::StandaloneImplDecl) {
    auto ifacesId = ast::NodeId(node.payload.words[kStandaloneImplDeclIfacesIdWord]);
    if (!impl->tree.contains(ifacesId)) return names;

    const auto& ifaceList = impl->tree.node(ifacesId);
    if (ifaceList.kind != SyntaxKind::ImplIfaceList) {
      // Single interface - might be a NamedTypeExpr directly
      auto name = resolvePathName(ifacesId);
      if (name.size() > 0) { names.add(name); }
      return names;
    }

    NodeList ifaceNodeList;
    ifaceNodeList.first = ifaceList.payload.words[kImplIfaceListIfacesFirstWord];
    ifaceNodeList.size = ifaceList.payload.words[kImplIfaceListIfacesSizeWord];

    for (ast::NodeId ifaceId : impl->tree.list(ifaceNodeList)) {
      auto name = resolvePathName(ifaceId);
      if (name.size() > 0) { names.add(name); }
    }
  }

  return names;
}

zc::StringPtr TraitResolver::resolveMarkerImplName(ast::NodeId markerImplNode) {
  if (!impl->tree.contains(markerImplNode)) return ""_zc;

  const auto& node = impl->tree.node(markerImplNode);
  if (node.kind != SyntaxKind::MarkerImpl) return ""_zc;

  auto markerPathId = ast::NodeId(node.payload.words[kMarkerImplMarkerPathWord]);
  return resolvePathName(markerPathId);
}

// ============================================================================
// discoverImpls
// ============================================================================

void TraitResolver::discoverImpls() {
  const auto rootId = impl->tree.root();
  if (!impl->tree.contains(rootId)) return;

  visitTreePreOrder(impl->tree, rootId, [this](ast::NodeId id, const ast::Node& node) {
    if (node.kind == SyntaxKind::StandaloneImplDecl) {
      auto forType = resolveImplForType(id);
      auto ifaceNames = resolveImplIfaceNames(id);

      for (auto ifaceName : ifaceNames) {
        if (ifaceName.size() == 0) continue;

        // Register in TypeEnv
        impl->typeEnv.registerImpl(ifaceName, *forType, id);

        // Cache for fast lookup
        impl->implCache.upsert(zc::str(getTypeName(*forType), "::", ifaceName), id);
      }
    } else if (node.kind == SyntaxKind::MarkerImpl) {
      auto forType = resolveImplForType(id);
      auto markerName = resolveMarkerImplName(id);

      if (markerName.size() > 0) {
        auto typeName = getTypeName(*forType);
        auto key = markerImplKey(typeName, markerName);
        const bool isNegated = node.payload.words[kMarkerImplIsNegatedWord] != 0;
        if (isNegated) {
          impl->negativeMarkerImpls.insert(zc::mv(key));
        } else {
          impl->positiveMarkerImpls.insert(zc::str(key));
          impl->typeEnv.registerImpl(markerName, *forType, id);
          impl->implCache.upsert(zc::mv(key), id);
        }
      }
    }
  });
}

// ============================================================================
// implements
// ============================================================================

bool TraitResolver::implements(const type::Type& ty, zc::StringPtr ifaceName) {
  if (ifaceName.size() == 0) return false;

  // Resolve the type (follow type variable bindings)
  const auto& resolved = impl->typeEnv.find(ty);
  auto typeName = getTypeName(resolved);
  auto markerKey = markerImplKey(typeName, ifaceName);

  // Marker trait auto-derivation
  if (ifaceName == "Sendable"_zc || ifaceName == "Shared"_zc) {
    if (impl->negativeMarkerImpls.contains(markerKey)) { return false; }
    if (impl->positiveMarkerImpls.contains(markerKey)) { return true; }
    if (impl->typeEnv.implements(resolved, ifaceName)) { return true; }
    if (ifaceName == "Sendable"_zc) { return isAutoSendable(resolved); }
    return isAutoShared(resolved);
  }

  // Check TypeEnv's impl table (registered via discoverImpls)
  if (impl->typeEnv.implements(resolved, ifaceName)) { return true; }

  // For named types, check class hierarchy: if the class extends/implements
  // an interface, check if that interface matches
  if (isNamed(resolved)) {
    const auto& named = static_cast<const NamedType&>(resolved);
    auto sym = named.getSymbol();
    ZC_IF_SOME(s, sym) {
      // If it's a class, check its interface list
      if (s.isClassSymbol()) {
        const auto& cls = static_cast<const symbol::ClassSymbol&>(s);
        auto interfaces = cls.getInterfaces();
        for (const auto& ifaceSym : interfaces) {
          if (ifaceSym == zc::none) continue;
          ZC_IF_SOME(is, ifaceSym) {
            auto ifaceSymName = is.getName();
            if (ifaceSymName == ifaceName) return true;
          }

          // Also check parent interfaces (interface inheritance)
          // This is a simplified check - we'd need to walk the interface
          // hierarchy for full coverage
        }

        // Check superclass chain
        auto superclass = cls.getSuperclass();
        ZC_IF_SOME(sc, superclass) {
          // Create a temporary named type for the superclass to check
          auto superName = sc.getName();
          auto superNamed = zc::heap<NamedType>(superName, sc);
          if (implements(*superNamed, ifaceName)) return true;
        }
      }

      // If it's an interface symbol, check parent interfaces
      if (s.getKind() == symbol::SymbolKind::Interface) {
        // Interface extends - check parent interfaces
        // The InterfaceType stores parent interfaces via addParentInterface
        // We'd need the InterfaceType to check this properly
      }
    }
  }

  // For existential types (dyn Interface), they implement their own interface
  if (isExistential(resolved)) {
    const auto& exist = static_cast<const ExistentialType&>(resolved);
    const auto& ifaceTy = exist.getInterfaceType();

    if (isNamed(ifaceTy)) {
      const auto& ifaceNamed = static_cast<const NamedType&>(ifaceTy);
      if (ifaceNamed.getName() == ifaceName) return true;
    }

    // For intersection of interfaces, check each conjunct
    if (isIntersection(ifaceTy)) {
      const auto& inter = static_cast<const IntersectionType&>(ifaceTy);
      for (size_t i = 0; i < inter.getConjunctCount(); ++i) {
        const auto& conjunct = inter.getConjunct(i);
        if (isNamed(conjunct)) {
          const auto& conjNamed = static_cast<const NamedType&>(conjunct);
          if (conjNamed.getName() == ifaceName) return true;
        }
      }
    }
  }

  return false;
}

// ============================================================================
// findImpl
// ============================================================================

zc::Maybe<ast::NodeId> TraitResolver::findImpl(const type::Type& ty, zc::StringPtr ifaceName) {
  if (ifaceName.size() == 0) return zc::none;

  const auto& resolved = impl->typeEnv.find(ty);

  // Check TypeEnv first
  auto envResult = impl->typeEnv.lookupImpl(ifaceName, resolved);
  if (envResult != zc::none) return envResult;

  // Fall back to cache
  ZC_IF_SOME(nodeId, impl->implCache.find(zc::str(getTypeName(resolved), "::", ifaceName))) {
    return nodeId;
  }

  // Walk AST to find matching impl
  const auto rootId = impl->tree.root();
  if (!impl->tree.contains(rootId)) return zc::none;

  zc::Maybe<ast::NodeId> result = zc::none;

  visitTreePreOrder(impl->tree, rootId, [&](ast::NodeId id, const ast::Node& node) {
    if (result != zc::none) return;

    if (node.kind == SyntaxKind::StandaloneImplDecl || node.kind == SyntaxKind::MarkerImpl) {
      if (checkImplMatches(id, resolved, ifaceName)) { result = id; }
    }
  });

  return result;
}

bool TraitResolver::checkImplMatches(ast::NodeId implNode, const type::Type& ty,
                                     zc::StringPtr ifaceName) {
  if (!impl->tree.contains(implNode)) return false;

  const auto& node = impl->tree.node(implNode);

  // Extract the for-type
  auto forType = resolveImplForType(implNode);
  if (!forType) return false;

  // Check if the for-type matches
  if (!forType->equals(ty)) {
    // Also check if ty is a named type with the same name
    if (isNamed(ty) && isNamed(*forType)) {
      const auto& tyNamed = static_cast<const NamedType&>(ty);
      const auto& forNamed = static_cast<const NamedType&>(*forType);
      if (tyNamed.getName() != forNamed.getName()) return false;
    } else {
      return false;
    }
  }

  // Check if the interface name matches
  if (node.kind == SyntaxKind::StandaloneImplDecl) {
    auto ifaceNames = resolveImplIfaceNames(implNode);
    for (auto name : ifaceNames) {
      if (name == ifaceName) return true;
    }
  } else if (node.kind == SyntaxKind::MarkerImpl) {
    auto markerName = resolveMarkerImplName(implNode);
    if (markerName == ifaceName) return true;
  }

  return false;
}

// ============================================================================
// resolveAssociatedType
// ============================================================================

AssociatedTypeResolution TraitResolver::resolveAssociatedTypeWithStatus(const type::Type& ty,
                                                                        zc::StringPtr assocName) {
  if (assocName.size() == 0) {
    return AssociatedTypeResolution{AssociatedTypeResolutionKind::NotFound, zc::none};
  }

  const auto& resolved = impl->typeEnv.find(ty);

  // Walk all impl blocks that match this type
  const auto rootId = impl->tree.root();
  if (!impl->tree.contains(rootId)) {
    return AssociatedTypeResolution{AssociatedTypeResolutionKind::NotFound, zc::none};
  }

  zc::Maybe<const type::Type&> result = zc::none;
  bool ambiguous = false;

  visitTreePreOrder(impl->tree, rootId, [&](ast::NodeId id, const ast::Node& node) {
    if (ambiguous) return;

    if (node.kind != SyntaxKind::StandaloneImplDecl) return;

    // Check if this impl is for our type
    auto forType = resolveImplForType(id);
    if (!forType) return;

    bool typeMatches = false;
    if (forType->equals(resolved)) {
      typeMatches = true;
    } else if (isNamed(resolved) && isNamed(*forType)) {
      const auto& tyNamed = static_cast<const NamedType&>(resolved);
      const auto& forNamed = static_cast<const NamedType&>(*forType);
      if (tyNamed.getName() == forNamed.getName()) { typeMatches = true; }
    }

    if (!typeMatches) return;

    // Check the impl's members for an associated type binding
    auto membersId = ast::NodeId(node.payload.words[kStandaloneImplDeclMembersIdWord]);
    if (!impl->tree.contains(membersId)) return;

    const auto& memberList = impl->tree.node(membersId);
    if (memberList.kind != SyntaxKind::ClassMemberList) return;

    NodeList memberNodeList;
    memberNodeList.first = memberList.payload.words[kClassMemberListMembersFirstWord];
    memberNodeList.size = memberList.payload.words[kClassMemberListMembersSizeWord];

    for (ast::NodeId memberId : impl->tree.list(memberNodeList)) {
      if (ambiguous) break;

      const auto& memberNode = impl->tree.node(memberId);
      if (memberNode.kind != SyntaxKind::AssociatedTypeDecl) continue;

      auto memberName =
          impl->tree.ident(IdentId(memberNode.payload.words[kAssociatedTypeDeclNameWord]));

      if (memberName == assocName) {
        // Found the associated type binding - resolve its type
        auto defaultTyId = ast::NodeId(memberNode.payload.words[kAssociatedTypeDeclDefaultTyWord]);

        if (impl->tree.contains(defaultTyId)) {
          auto assocTy = resolveTypeExpr(defaultTyId);
          // Store in TypeEnv so we can return a stable reference
          impl->typeEnv.setType(memberId, zc::mv(assocTy));
          if (result != zc::none) {
            ambiguous = true;
            result = zc::none;
            break;
          }
          result = impl->typeEnv.getType(memberId);
        }
      }
    }
  });

  if (ambiguous) {
    return AssociatedTypeResolution{AssociatedTypeResolutionKind::Ambiguous, zc::none};
  }
  if (result == zc::none) {
    return AssociatedTypeResolution{AssociatedTypeResolutionKind::NotFound, zc::none};
  }
  return AssociatedTypeResolution{AssociatedTypeResolutionKind::Resolved, result};
}

AssociatedTypeResolution TraitResolver::resolveAssociatedTypeWithStatus(const type::Type& ty,
                                                                        zc::StringPtr ifaceName,
                                                                        zc::StringPtr assocName) {
  if (ifaceName.size() == 0 || assocName.size() == 0) {
    return AssociatedTypeResolution{AssociatedTypeResolutionKind::NotFound, zc::none};
  }

  const auto& resolved = impl->typeEnv.find(ty);
  const auto rootId = impl->tree.root();
  if (!impl->tree.contains(rootId)) {
    return AssociatedTypeResolution{AssociatedTypeResolutionKind::NotFound, zc::none};
  }

  zc::Maybe<const type::Type&> result = zc::none;
  bool ambiguous = false;

  visitTreePreOrder(impl->tree, rootId, [&](ast::NodeId id, const ast::Node& node) {
    if (ambiguous || node.kind != SyntaxKind::StandaloneImplDecl) { return; }
    if (!checkImplMatches(id, resolved, ifaceName)) { return; }

    auto membersId = ast::NodeId(node.payload.words[kStandaloneImplDeclMembersIdWord]);
    if (!impl->tree.contains(membersId)) { return; }

    const auto& memberList = impl->tree.node(membersId);
    if (memberList.kind != SyntaxKind::ClassMemberList) { return; }

    NodeList memberNodeList;
    memberNodeList.first = memberList.payload.words[kClassMemberListMembersFirstWord];
    memberNodeList.size = memberList.payload.words[kClassMemberListMembersSizeWord];

    for (ast::NodeId memberId : impl->tree.list(memberNodeList)) {
      if (!impl->tree.contains(memberId)) { continue; }
      const auto& memberNode = impl->tree.node(memberId);
      if (memberNode.kind != SyntaxKind::AssociatedTypeDecl) { continue; }

      auto memberName =
          impl->tree.ident(IdentId(memberNode.payload.words[kAssociatedTypeDeclNameWord]));
      if (memberName != assocName) { continue; }

      auto defaultTyId = ast::NodeId(memberNode.payload.words[kAssociatedTypeDeclDefaultTyWord]);
      if (!impl->tree.contains(defaultTyId)) { continue; }

      auto assocTy = resolveTypeExpr(defaultTyId);
      impl->typeEnv.setType(memberId, zc::mv(assocTy));
      if (result != zc::none) {
        ambiguous = true;
        result = zc::none;
        return;
      }
      result = impl->typeEnv.getType(memberId);
    }
  });

  if (ambiguous) {
    return AssociatedTypeResolution{AssociatedTypeResolutionKind::Ambiguous, zc::none};
  }
  if (result == zc::none) {
    return AssociatedTypeResolution{AssociatedTypeResolutionKind::NotFound, zc::none};
  }
  return AssociatedTypeResolution{AssociatedTypeResolutionKind::Resolved, result};
}

zc::Maybe<const type::Type&> TraitResolver::resolveAssociatedType(const type::Type& ty,
                                                                  zc::StringPtr assocName) {
  auto result = resolveAssociatedTypeWithStatus(ty, assocName);
  if (result.kind == AssociatedTypeResolutionKind::Ambiguous) {
    auto rootId = impl->tree.root();
    auto loc = impl->tree.contains(rootId) ? nodeLoc(impl->tree, rootId) : source::SourceLoc();
    auto typeName = getTypeName(ty);
    if (typeName.size() == 0) {
      auto rendered = ty.toString();
      impl->diags.diagnose<DiagID::AmbiguousAssociatedTypeProjection>(
          loc, assocName, rendered.asPtr(), rendered.asPtr(), assocName);
    } else {
      impl->diags.diagnose<DiagID::AmbiguousAssociatedTypeProjection>(loc, assocName, typeName,
                                                                      typeName, assocName);
    }
    impl->hadErrors = true;
    return zc::none;
  }
  if (result.kind != AssociatedTypeResolutionKind::Resolved) {
    auto rootId = impl->tree.root();
    auto loc = impl->tree.contains(rootId) ? nodeLoc(impl->tree, rootId) : source::SourceLoc();
    auto typeName = getTypeName(ty);
    if (typeName.size() == 0) {
      auto rendered = ty.toString();
      impl->diags.diagnose<DiagID::NoAssociatedTypeProjection>(loc, assocName, zc::mv(rendered));
    } else {
      impl->diags.diagnose<DiagID::NoAssociatedTypeProjection>(loc, assocName, typeName);
    }
    impl->hadErrors = true;
    return zc::none;
  }
  return result.type;
}

// ============================================================================
// Marker trait auto-derivation
// ============================================================================

bool TraitResolver::isAutoSendable(const type::Type& ty) {
  const auto& resolved = impl->typeEnv.find(ty);

  // Primitive types are always Sendable
  if (isPrimitive(resolved)) {
    const auto& prim = static_cast<const PrimitiveType&>(resolved);
    auto kind = prim.getPrimitiveKind();
    // never, any, unit, null are all Sendable
    // All integer, float, bool, str, char are Sendable
    (void)kind;
    return true;
  }

  // Error types are Sendable (to avoid cascading errors)
  if (isError(resolved)) return true;

  // Type variables - can't determine statically, conservative false
  if (isTypeVar(resolved)) return false;

  // &T (shared reference) - NOT Sendable by default
  // &mut T - Sendable if T is Sendable
  if (isReference(resolved)) {
    const auto& ref = static_cast<const ReferenceType&>(resolved);
    if (ref.isMutable()) { return isAutoSendable(ref.getPointeeType()); }
    // Shared reference: not Sendable (can't move shared state across threads
    // safely without Shared guarantee on the pointee; but per RFC, &T is not Sendable)
    return false;
  }

  // Raw pointers: neither Sendable nor Shared
  if (isRawPointer(resolved)) { return false; }

  // Function types: Sendable (code pointers are safe to move)
  if (isFunction(resolved)) { return true; }

  // Tuple: Sendable if all elements are Sendable
  if (isTuple(resolved)) {
    const auto& tuple = static_cast<const TupleType&>(resolved);
    for (size_t i = 0; i < tuple.getElementCount(); ++i) {
      if (!isAutoSendable(tuple.getElementType(i))) return false;
    }
    return true;
  }

  // Array: Sendable if element type is Sendable
  if (isArray(resolved)) {
    const auto& arr = static_cast<const ArrayType&>(resolved);
    return isAutoSendable(arr.getElementType());
  }

  // Object type: Sendable if all members are Sendable
  if (isObject(resolved)) {
    const auto& obj = static_cast<const ObjectType&>(resolved);
    auto members = obj.getMembers();
    for (const auto& entry : members) {
      ZC_IF_SOME(memberType, entry.type) {
        if (!isAutoSendable(memberType)) return false;
      }
    }
    return true;
  }

  // Named types (struct/class): Sendable if all fields are Sendable
  if (isNamed(resolved)) {
    const auto& named = static_cast<const NamedType&>(resolved);
    return allFieldsAreSend(named);
  }

  // Union: Sendable if all alternatives are Sendable
  if (isUnion(resolved)) {
    const auto& unionTy = static_cast<const UnionType&>(resolved);
    for (size_t i = 0; i < unionTy.getAlternativeCount(); ++i) {
      if (!isAutoSendable(unionTy.getAlternative(i))) return false;
    }
    return true;
  }

  // Intersection: Sendable if all conjuncts are Sendable
  if (isIntersection(resolved)) {
    const auto& inter = static_cast<const IntersectionType&>(resolved);
    for (size_t i = 0; i < inter.getConjunctCount(); ++i) {
      if (!isAutoSendable(inter.getConjunct(i))) return false;
    }
    return true;
  }

  // Existential (dyn Interface): not Sendable by default (unknown concrete type)
  if (isExistential(resolved)) { return false; }

  // Associated types: can't determine, conservative
  if (isAssociated(resolved)) { return false; }

  // Unknown type form: conservative
  return false;
}

bool TraitResolver::isAutoShared(const type::Type& ty) {
  const auto& resolved = impl->typeEnv.find(ty);

  // Primitive types are always Shared
  if (isPrimitive(resolved)) { return true; }

  // Error types are Shared (to avoid cascading errors)
  if (isError(resolved)) return true;

  // Type variables: can't determine statically, conservative false
  if (isTypeVar(resolved)) return false;

  // &T (shared reference): Shared if T is Shared
  // &mut T: NOT Shared (mutable references can't be shared)
  if (isReference(resolved)) {
    const auto& ref = static_cast<const ReferenceType&>(resolved);
    if (ref.isMutable()) { return false; }
    return isAutoShared(ref.getPointeeType());
  }

  // Raw pointers: neither Sendable nor Shared
  if (isRawPointer(resolved)) { return false; }

  // Function types: Shared
  if (isFunction(resolved)) { return true; }

  // Tuple: Shared if all elements are Shared
  if (isTuple(resolved)) {
    const auto& tuple = static_cast<const TupleType&>(resolved);
    for (size_t i = 0; i < tuple.getElementCount(); ++i) {
      if (!isAutoShared(tuple.getElementType(i))) return false;
    }
    return true;
  }

  // Array: Shared if element type is Shared
  if (isArray(resolved)) {
    const auto& arr = static_cast<const ArrayType&>(resolved);
    return isAutoShared(arr.getElementType());
  }

  // Object type: Shared if all members are Shared
  if (isObject(resolved)) {
    const auto& obj = static_cast<const ObjectType&>(resolved);
    auto members = obj.getMembers();
    for (const auto& entry : members) {
      ZC_IF_SOME(memberType, entry.type) {
        if (!isAutoShared(memberType)) return false;
      }
    }
    return true;
  }

  // Named types (struct/class): Shared if all fields are Shared
  if (isNamed(resolved)) {
    const auto& named = static_cast<const NamedType&>(resolved);
    return allFieldsAreSync(named);
  }

  // Union: Shared if all alternatives are Shared
  if (isUnion(resolved)) {
    const auto& unionTy = static_cast<const UnionType&>(resolved);
    for (size_t i = 0; i < unionTy.getAlternativeCount(); ++i) {
      if (!isAutoShared(unionTy.getAlternative(i))) return false;
    }
    return true;
  }

  // Intersection: Shared if all conjuncts are Shared
  if (isIntersection(resolved)) {
    const auto& inter = static_cast<const IntersectionType&>(resolved);
    for (size_t i = 0; i < inter.getConjunctCount(); ++i) {
      if (!isAutoShared(inter.getConjunct(i))) return false;
    }
    return true;
  }

  // Existential: not Shared by default
  if (isExistential(resolved)) { return false; }

  // Associated types: can't determine
  if (isAssociated(resolved)) { return false; }

  return false;
}

// ============================================================================
// Field checking for named types
// ============================================================================

bool TraitResolver::allFieldsAreSend(const type::NamedType& namedTy) {
  auto sym = namedTy.getSymbol();
  ZC_IF_SOME(s, sym) {
    if (s.isClassSymbol()) {
      const auto& cls = static_cast<const symbol::ClassSymbol&>(s);
      if (cls.getMembers().size() > 0) { return allClassFieldsAreSend(cls); }
    }
  }

  // For non-class named types, check via AST walk
  // Try to find the struct/class declaration in the AST
  auto name = namedTy.getName();
  if (name.size() == 0) return false;

  const auto rootId = impl->tree.root();
  if (!impl->tree.contains(rootId)) return false;

  bool allSend = true;
  bool foundType = false;

  visitTreePreOrder(impl->tree, rootId, [&](ast::NodeId id, const ast::Node& node) {
    if (!allSend || foundType) return;

    zc::StringPtr declName;
    NodeList memberNodeList;
    memberNodeList.first = 0;
    memberNodeList.size = 0;

    if (node.kind == SyntaxKind::ClassDecl) {
      declName = impl->tree.ident(IdentId(node.payload.words[kClassDeclNameWord]));
      auto membersId = ast::NodeId(node.payload.words[kClassDeclMembersIdWord]);
      if (impl->tree.contains(membersId)) {
        const auto& memberList = impl->tree.node(membersId);
        if (memberList.kind == SyntaxKind::ClassMemberList) {
          memberNodeList.first = memberList.payload.words[kClassMemberListMembersFirstWord];
          memberNodeList.size = memberList.payload.words[kClassMemberListMembersSizeWord];
        }
      }
    } else if (node.kind == SyntaxKind::StructDecl) {
      declName = impl->tree.ident(IdentId(node.payload.words[kStructDeclNameWord]));
      auto membersId = ast::NodeId(node.payload.words[kStructDeclMembersIdWord]);
      if (impl->tree.contains(membersId)) {
        const auto& memberList = impl->tree.node(membersId);
        if (memberList.kind == SyntaxKind::ClassMemberList) {
          memberNodeList.first = memberList.payload.words[kClassMemberListMembersFirstWord];
          memberNodeList.size = memberList.payload.words[kClassMemberListMembersSizeWord];
        }
      }
    } else if (node.kind == SyntaxKind::PositionalStructDecl) {
      declName = impl->tree.ident(IdentId(node.payload.words[kPositionalStructDeclNameWord]));
      // Positional struct fields
      memberNodeList.first = node.payload.words[kPositionalStructDeclFieldsFirstWord];
      memberNodeList.size = node.payload.words[kPositionalStructDeclFieldsSizeWord];
    }

    if (declName.size() == 0 || declName != name) return;

    foundType = true;

    // Check all fields
    if (memberNodeList.size > 0) {
      for (ast::NodeId memberId : impl->tree.list(memberNodeList)) {
        if (!allSend) break;

        const auto& memberNode = impl->tree.node(memberId);

        if (memberNode.kind == SyntaxKind::FieldDecl) {
          auto fieldTyId = ast::NodeId(memberNode.payload.words[kFieldDeclTyWord]);
          if (impl->tree.contains(fieldTyId)) {
            // Try to get the type from TypeEnv first (if already computed)
            if (impl->typeEnv.hasType(memberId)) {
              const auto& fieldTy = impl->typeEnv.getType(memberId);
              if (!isAutoSendable(fieldTy)) {
                allSend = false;
                break;
              }
            } else {
              // Resolve from AST
              auto fieldTy = resolveTypeExpr(fieldTyId);
              if (!isAutoSendable(*fieldTy)) {
                allSend = false;
                break;
              }
            }
          }
        } else if (memberNode.kind == SyntaxKind::PositionalStructDecl) {
          // Positional struct field - the field type is the node itself
          auto fieldTy = resolveTypeExpr(memberId);
          if (!isAutoSendable(*fieldTy)) {
            allSend = false;
            break;
          }
        }
      }
    }
  });

  if (!foundType) return false;
  return allSend;
}

bool TraitResolver::allFieldsAreSync(const type::NamedType& namedTy) {
  auto sym = namedTy.getSymbol();
  ZC_IF_SOME(s, sym) {
    if (s.isClassSymbol()) {
      const auto& cls = static_cast<const symbol::ClassSymbol&>(s);
      if (cls.getMembers().size() > 0) { return allClassFieldsAreSync(cls); }
    }
  }

  auto name = namedTy.getName();
  if (name.size() == 0) return false;

  const auto rootId = impl->tree.root();
  if (!impl->tree.contains(rootId)) return false;

  bool allSync = true;
  bool foundType = false;

  visitTreePreOrder(impl->tree, rootId, [&](ast::NodeId id, const ast::Node& node) {
    if (!allSync || foundType) return;

    zc::StringPtr declName;
    NodeList memberNodeList;
    memberNodeList.first = 0;
    memberNodeList.size = 0;

    if (node.kind == SyntaxKind::ClassDecl) {
      declName = impl->tree.ident(IdentId(node.payload.words[kClassDeclNameWord]));
      auto membersId = ast::NodeId(node.payload.words[kClassDeclMembersIdWord]);
      if (impl->tree.contains(membersId)) {
        const auto& memberList = impl->tree.node(membersId);
        if (memberList.kind == SyntaxKind::ClassMemberList) {
          memberNodeList.first = memberList.payload.words[kClassMemberListMembersFirstWord];
          memberNodeList.size = memberList.payload.words[kClassMemberListMembersSizeWord];
        }
      }
    } else if (node.kind == SyntaxKind::StructDecl) {
      declName = impl->tree.ident(IdentId(node.payload.words[kStructDeclNameWord]));
      auto membersId = ast::NodeId(node.payload.words[kStructDeclMembersIdWord]);
      if (impl->tree.contains(membersId)) {
        const auto& memberList = impl->tree.node(membersId);
        if (memberList.kind == SyntaxKind::ClassMemberList) {
          memberNodeList.first = memberList.payload.words[kClassMemberListMembersFirstWord];
          memberNodeList.size = memberList.payload.words[kClassMemberListMembersSizeWord];
        }
      }
    } else if (node.kind == SyntaxKind::PositionalStructDecl) {
      declName = impl->tree.ident(IdentId(node.payload.words[kPositionalStructDeclNameWord]));
      memberNodeList.first = node.payload.words[kPositionalStructDeclFieldsFirstWord];
      memberNodeList.size = node.payload.words[kPositionalStructDeclFieldsSizeWord];
    }

    if (declName.size() == 0 || declName != name) return;

    foundType = true;

    if (memberNodeList.size > 0) {
      for (ast::NodeId memberId : impl->tree.list(memberNodeList)) {
        if (!allSync) break;

        const auto& memberNode = impl->tree.node(memberId);

        if (memberNode.kind == SyntaxKind::FieldDecl) {
          auto fieldTyId = ast::NodeId(memberNode.payload.words[kFieldDeclTyWord]);
          if (impl->tree.contains(fieldTyId)) {
            if (impl->typeEnv.hasType(memberId)) {
              const auto& fieldTy = impl->typeEnv.getType(memberId);
              if (!isAutoShared(fieldTy)) {
                allSync = false;
                break;
              }
            } else {
              auto fieldTy = resolveTypeExpr(fieldTyId);
              if (!isAutoShared(*fieldTy)) {
                allSync = false;
                break;
              }
            }
          }
        } else if (memberNode.kind == SyntaxKind::PositionalStructDecl) {
          auto fieldTy = resolveTypeExpr(memberId);
          if (!isAutoShared(*fieldTy)) {
            allSync = false;
            break;
          }
        }
      }
    }
  });

  if (!foundType) return false;
  return allSync;
}

bool TraitResolver::allClassFieldsAreSend(const symbol::ClassSymbol& cls) {
  auto members = cls.getMembers();
  for (const auto& memberSym : members) {
    if (memberSym == zc::none) continue;

    ZC_IF_SOME(m, memberSym) {
      if (m.getKind() != symbol::SymbolKind::Field && m.getKind() != symbol::SymbolKind::Variable) {
        continue;
      }

      // Get the field's type from TypeEnv via its declaration ref
      auto declRefs = m.getDeclarationRefs();
      bool foundType = false;

      for (const auto& ref : declRefs) {
        if (impl->tree.contains(ref.node) && impl->typeEnv.hasType(ref.node)) {
          const auto& fieldTy = impl->typeEnv.getType(ref.node);
          if (!isAutoSendable(fieldTy)) return false;
          foundType = true;
          break;
        }
      }

      if (!foundType) {
        // Can't determine field type - conservative
        return false;
      }
    }
  }

  return true;
}

bool TraitResolver::allClassFieldsAreSync(const symbol::ClassSymbol& cls) {
  auto members = cls.getMembers();
  for (const auto& memberSym : members) {
    if (memberSym == zc::none) continue;

    ZC_IF_SOME(m, memberSym) {
      if (m.getKind() != symbol::SymbolKind::Field && m.getKind() != symbol::SymbolKind::Variable) {
        continue;
      }

      auto declRefs = m.getDeclarationRefs();
      bool foundType = false;

      for (const auto& ref : declRefs) {
        if (impl->tree.contains(ref.node) && impl->typeEnv.hasType(ref.node)) {
          const auto& fieldTy = impl->typeEnv.getType(ref.node);
          if (!isAutoShared(fieldTy)) return false;
          foundType = true;
          break;
        }
      }

      if (!foundType) { return false; }
    }
  }

  return true;
}

// ============================================================================
// checkCoherence
// ============================================================================

void TraitResolver::checkCoherence() {
  impl->seenImpls.clear();
  impl->coherenceImpls.clear();
  impl->hadErrors = false;

  const auto rootId = impl->tree.root();
  if (!impl->tree.contains(rootId)) return;

  visitTreePreOrder(impl->tree, rootId, [this](ast::NodeId id, const ast::Node& node) {
    if (node.kind != SyntaxKind::StandaloneImplDecl && node.kind != SyntaxKind::MarkerImpl) {
      return;
    }

    // Extract for-type
    auto forType = resolveImplForType(id);
    if (!forType) return;

    // Extract interface name(s)
    zc::Vector<zc::StringPtr> ifaceNames;

    if (node.kind == SyntaxKind::StandaloneImplDecl) {
      ifaceNames = resolveImplIfaceNames(id);
    } else {
      auto markerName = resolveMarkerImplName(id);
      if (markerName.size() > 0) { ifaceNames.add(markerName); }
    }

    for (auto ifaceName : ifaceNames) {
      if (ifaceName.size() == 0) continue;

      const auto typeName = getTypeName(*forType);
      const auto key = zc::str(typeName, "::", ifaceName);
      const bool isBlanket =
          node.kind == SyntaxKind::StandaloneImplDecl &&
          impl->tree.contains(ast::NodeId(node.payload.words[kStandaloneImplDeclTypeParamsIdWord]));

      bool overlapsExisting = false;
      for (const auto& existing : impl->coherenceImpls) {
        if (existing.ifaceKey != ifaceName) continue;

        bool typeOverlap = existing.typeKey == typeName || existing.blanket || isBlanket;
        if (!typeOverlap) continue;

        overlapsExisting = true;
        break;
      }

      // Check for duplicate or overlapping impls.
      if (impl->seenImpls.find(key) != zc::none || overlapsExisting) {
        auto loc = nodeLoc(impl->tree, id);
        impl->diags.diagnose<DiagID::ConflictingImpl>(loc, ifaceName, typeName);
        impl->hadErrors = true;
        continue;
      }

      // Orphan rule check: at least one of the type or interface must be local
      bool typeLocal = isTypeLocal(*forType);
      bool ifaceLocal = isInterfaceLocal(ifaceName);

      if (!typeLocal && !ifaceLocal) {
        auto loc = nodeLoc(impl->tree, id);
        impl->diags.diagnose<DiagID::OrphanImpl>(loc, ifaceName, getTypeName(*forType));
        impl->hadErrors = true;
        continue;
      }

      impl->seenImpls.upsert(zc::str(key), id);
      impl->coherenceImpls.add(
          Impl::ImplRecord{zc::str(typeName), zc::str(ifaceName), id, isBlanket});
    }
  });
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
