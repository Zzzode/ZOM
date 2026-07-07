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

#include "zomlang/compiler/type/type-env.h"

#include "zomlang/compiler/type/array-type.h"
#include "zomlang/compiler/type/error-type.h"
#include "zomlang/compiler/type/function-type.h"
#include "zomlang/compiler/type/named-type.h"
#include "zomlang/compiler/type/object-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/raw-pointer-type.h"
#include "zomlang/compiler/type/reference-type.h"
#include "zomlang/compiler/type/tuple-type.h"
#include "zomlang/compiler/type/type-interner.h"
#include "zomlang/compiler/type/type-var.h"
#include "zomlang/compiler/type/union-type.h"

namespace zomlang {
namespace compiler {
namespace type {

// ===========================================================================
// Impl struct
// ===========================================================================

struct TypeEnv::Impl {
  // --- Node type mapping ---
  zc::HashMap<uint32_t, zc::Own<Type>> nodeTypes;     // keyed by NodeId::value
  zc::HashMap<uint32_t, TypeId> nodeTypeIds;          // keyed by NodeId::value
  zc::HashMap<uint32_t, CoercionKind> nodeCoercions;  // keyed by NodeId::value
  TypeInterner interner;

  // --- Type variable storage ---
  zc::Vector<zc::Own<TypeVar>> typeVars;  // owns all created type variables
  uint64_t nextTypeVarId = 1;             // 0 means "no ID", start from 1

  // --- Union-Find for type variables ---
  // parent[varId] = parent varId; 0 means self (root).
  // Mutable to allow path compression in const methods.
  mutable zc::HashMap<uint64_t, uint64_t> unionParent;

  // rank[varId] = approximate tree depth for union by rank.
  mutable zc::HashMap<uint64_t, uint32_t> unionRank;

  // --- Type variable concrete bindings ---
  // binding[varId] = pointer to concrete type (non-owning).
  // Mutable to allow path compression in const methods.
  mutable zc::HashMap<uint64_t, const Type*> idBindings;  // non-owning

  // For TypeVars without IDs (legacy / named): keyed by string.
  zc::HashMap<zc::String, const Type*> stringBindings;  // non-owning

  // --- Owning storage for bind(Own<Type>) ---
  // When bind() is called with an Own<Type>, we take ownership. The pointer
  // in idBindings/stringBindings points into this vector.
  zc::Vector<zc::Own<Type>> ownedBindings;

  // --- Impl table ---
  // ifaceName -> list of (typeKey, implNode).
  // typeKey is toString() of the type, used for structural matching.
  struct ImplEntry {
    zc::String typeKey;
    ast::NodeId implNode;
  };
  zc::HashMap<zc::StringPtr, zc::Vector<ImplEntry>> implTable;

  // --- Error type singleton ---
  zc::Own<ErrorType> errorTypeInstance;

  Impl() : errorTypeInstance(zc::heap<ErrorType>()) {}
};

// ===========================================================================
// Construction / destruction
// ===========================================================================

TypeEnv::TypeEnv() : impl(zc::heap<Impl>()) {}

TypeEnv::~TypeEnv() noexcept(false) = default;

TypeEnv::TypeEnv(TypeEnv&& other) noexcept = default;

TypeEnv& TypeEnv::operator=(TypeEnv&& other) noexcept = default;

// ===========================================================================
// Node type mapping
// ===========================================================================

void TypeEnv::setType(ast::NodeId node, zc::Own<Type> ty) {
  TypeId id = impl->interner.intern(*ty);
  impl->nodeTypeIds.upsert(node.value, id);
  impl->nodeTypes.upsert(node.value, zc::mv(ty));
}

const Type& TypeEnv::getType(ast::NodeId node) const {
  auto found = impl->nodeTypes.find(node.value);
  ZC_IREQUIRE(found != zc::none, "TypeEnv::getType: node has no assigned type");
  ZC_IF_SOME(ty, found) { return *ty; }
  // Unreachable but satisfies the compiler.
  return *impl->errorTypeInstance;
}

bool TypeEnv::hasType(ast::NodeId node) const {
  return impl->nodeTypes.find(node.value) != zc::none;
}

TypeId TypeEnv::getTypeId(ast::NodeId node) const {
  auto found = impl->nodeTypeIds.find(node.value);
  ZC_IREQUIRE(found != zc::none, "TypeEnv::getTypeId: node has no assigned TypeId");
  ZC_IF_SOME(id, found) { return id; }
  return TypeId();
}

bool TypeEnv::hasTypeId(ast::NodeId node) const {
  return impl->nodeTypeIds.find(node.value) != zc::none;
}

size_t TypeEnv::nodeTypeCount() const { return impl->nodeTypes.size(); }

TypeId TypeEnv::internType(const Type& type) { return impl->interner.intern(type); }

void TypeEnv::setCoercion(ast::NodeId node, CoercionKind kind) {
  impl->nodeCoercions.upsert(node.value, kind);
}

bool TypeEnv::hasCoercion(ast::NodeId node) const {
  return impl->nodeCoercions.find(node.value) != zc::none;
}

CoercionKind TypeEnv::getCoercion(ast::NodeId node) const {
  auto found = impl->nodeCoercions.find(node.value);
  ZC_IREQUIRE(found != zc::none, "TypeEnv::getCoercion: node has no recorded coercion");
  ZC_IF_SOME(kind, found) { return kind; }
  return CoercionKind::Identity;
}

// ===========================================================================
// Type variable creation
// ===========================================================================

TypeVar& TypeEnv::freshTypeVar() { return freshTypeVar("?"_zc); }

TypeVar& TypeEnv::freshTypeVar(zc::StringPtr name) {
  uint64_t id = impl->nextTypeVarId++;
  auto var = zc::heap<TypeVar>(name, id);
  TypeVar& ref = *var;

  // Initialize union-find entry: parent = 0 (self), rank = 0.
  impl->unionParent.insert(id, 0);
  impl->unionRank.insert(id, 0);

  impl->typeVars.add(zc::mv(var));
  return ref;
}

// ===========================================================================
// Union-Find for type variables
// ===========================================================================

uint64_t TypeEnv::findRoot(uint64_t varId) const {
  auto parentEntry = impl->unionParent.find(varId);
  if (parentEntry == zc::none) {
    // Not in union-find (e.g. legacy TypeVar without ID).
    return varId;
  }

  ZC_IF_SOME(parentId, parentEntry) {
    if (parentId == 0) {
      // This is the root.
      return varId;
    }

    // Recursively find root of parent.
    uint64_t root = findRoot(parentId);

    // Path compression: point varId directly to root.
    if (parentId != root) { impl->unionParent.upsert(varId, root); }

    return root;
  }

  return varId;  // unreachable
}

Type& TypeEnv::find(Type& ty) const {
  // If not a TypeVar, it is its own representative.
  if (!isTypeVar(ty)) { return ty; }

  auto& var = static_cast<TypeVar&>(ty);
  uint64_t varId = var.getId();

  if (varId == 0) {
    // Legacy TypeVar without an ID — use string-keyed lookup.
    auto key = makeKey(var);
    auto found = impl->stringBindings.find(key);
    if (found != zc::none) {
      ZC_IF_SOME(bound, found) {
        if (bound) { return find(const_cast<Type&>(*bound)); }
      }
    }
    return ty;
  }

  // Find root in union-find.
  uint64_t rootId = findRoot(varId);

  // If the root has a concrete binding, resolve through it.
  auto bindingEntry = impl->idBindings.find(rootId);
  if (bindingEntry != zc::none) {
    ZC_IF_SOME(boundTy, bindingEntry) {
      if (boundTy) {
        // Recursively find representative of the bound type.
        Type& result = find(const_cast<Type&>(*boundTy));
        return result;
      }
    }
  }

  // Return the root TypeVar itself.
  if (rootId == varId) {
    return var;  // This var is its own representative.
  }

  // Find the TypeVar object for rootId.
  for (size_t i = 0; i < impl->typeVars.size(); ++i) {
    if (impl->typeVars[i]->getId() == rootId) { return const_cast<TypeVar&>(*impl->typeVars[i]); }
  }

  // Fallback: return original var.
  return var;
}

const Type& TypeEnv::find(const Type& ty) const {
  // Delegate to non-const overload via const_cast. Safe because find() only
  // performs path compression on internal mutable data.
  return find(const_cast<Type&>(ty));
}

void TypeEnv::unite(const Type& a, const Type& b) {
  const Type& repA = find(a);
  const Type& repB = find(b);

  if (&repA == &repB) { return; }  // Already unified.

  bool aIsVar = isTypeVar(repA);
  bool bIsVar = isTypeVar(repB);

  if (aIsVar && bIsVar) {
    // Both are TypeVars: link via union by rank.
    auto& varA = static_cast<const TypeVar&>(repA);
    auto& varB = static_cast<const TypeVar&>(repB);

    uint64_t idA = varA.getId();
    uint64_t idB = varB.getId();

    // If either lacks an ID, fall back to string-keyed binding.
    if (idA == 0 || idB == 0) {
      if (idA == 0) {
        auto key = makeKey(varA);
        impl->stringBindings.upsert(zc::mv(key), &varB);
      } else {
        // varA has an ID; bind varB (no ID) to varA's root.
        impl->idBindings.upsert(findRoot(idA), &varB);
      }
      return;
    }

    uint64_t rootA = findRoot(idA);
    uint64_t rootB = findRoot(idB);

    if (rootA == rootB) { return; }

    // Union by rank.
    uint32_t rankA = 0;
    auto rankAEntry = impl->unionRank.find(rootA);
    ZC_IF_SOME(r, rankAEntry) { rankA = r; }

    uint32_t rankB = 0;
    auto rankBEntry = impl->unionRank.find(rootB);
    ZC_IF_SOME(r, rankBEntry) { rankB = r; }

    if (rankA < rankB) {
      impl->unionParent.upsert(rootA, rootB);
    } else if (rankA > rankB) {
      impl->unionParent.upsert(rootB, rootA);
    } else {
      impl->unionParent.upsert(rootB, rootA);
      impl->unionRank.upsert(rootA, rankA + 1);
    }
  } else if (aIsVar && !bIsVar) {
    // Bind TypeVar repA to concrete type repB.
    auto& varA = static_cast<const TypeVar&>(repA);
    uint64_t idA = varA.getId();

    if (idA != 0) {
      uint64_t rootA = findRoot(idA);
      impl->idBindings.upsert(rootA, &repB);
    } else {
      auto key = makeKey(varA);
      impl->stringBindings.upsert(zc::mv(key), &repB);
    }
  } else if (!aIsVar && bIsVar) {
    // Bind TypeVar repB to concrete type repA.
    auto& varB = static_cast<const TypeVar&>(repB);
    uint64_t idB = varB.getId();

    if (idB != 0) {
      uint64_t rootB = findRoot(idB);
      impl->idBindings.upsert(rootB, &repA);
    } else {
      auto key = makeKey(varB);
      impl->stringBindings.upsert(zc::mv(key), &repA);
    }
  }
  // If neither is a TypeVar, nothing to do.
}

// ===========================================================================
// Let-polymorphism: Generalization and Instantiation
// ===========================================================================

void TypeEnv::collectFreeTypeVars(const Type& ty, zc::HashSet<uint64_t>& freeVars,
                                  const zc::HashSet<uint64_t>& exclude) const {
  const Type& resolved = find(ty);

  if (isTypeVar(resolved)) {
    auto& var = static_cast<const TypeVar&>(resolved);
    uint64_t id = var.getId();
    if (id == 0) return;  // Legacy var without ID - skip

    // Check if this var is bound to a concrete type
    auto binding = impl->idBindings.find(id);
    if (binding != zc::none) {
      ZC_IF_SOME(boundTy, binding) {
        if (boundTy && !isTypeVar(*boundTy)) {
          // Bound to concrete type: recurse into it
          collectFreeTypeVars(*boundTy, freeVars, exclude);
          return;
        }
      }
    }

    // Check if excluded (already quantified or in environment)
    if (exclude.contains(id)) return;

    // This is a free type variable. The same variable may appear in several
    // positions of one type, such as `fn<T>(T) -> T`.
    if (!freeVars.contains(id)) { freeVars.insert(id); }
    return;
  }

  // Recurse into compound types
  switch (resolved.getKind()) {
    case TypeKind::Function: {
      auto& fn = static_cast<const FunctionType&>(resolved);
      for (size_t i = 0; i < fn.getParamCount(); ++i) {
        collectFreeTypeVars(fn.getParamType(i), freeVars, exclude);
      }
      collectFreeTypeVars(fn.getReturnType(), freeVars, exclude);
      auto raises = fn.getRaisesType();
      ZC_IF_SOME(r, raises) { collectFreeTypeVars(r, freeVars, exclude); }
      break;
    }
    case TypeKind::Tuple: {
      auto& tuple = static_cast<const TupleType&>(resolved);
      for (size_t i = 0; i < tuple.getElementCount(); ++i) {
        collectFreeTypeVars(tuple.getElementType(i), freeVars, exclude);
      }
      break;
    }
    case TypeKind::Object: {
      auto& obj = static_cast<const ObjectType&>(resolved);
      auto members = obj.getMembers();
      for (size_t i = 0; i < members.size(); ++i) {
        ZC_IF_SOME(memberType, members[i].type) {
          collectFreeTypeVars(memberType, freeVars, exclude);
        }
      }
      break;
    }
    case TypeKind::Array: {
      auto& arr = static_cast<const ArrayType&>(resolved);
      collectFreeTypeVars(arr.getElementType(), freeVars, exclude);
      break;
    }
    case TypeKind::Named: {
      auto& named = static_cast<const NamedType&>(resolved);
      for (size_t i = 0; i < named.getTypeArgCount(); ++i) {
        collectFreeTypeVars(named.getTypeArg(i), freeVars, exclude);
      }
      break;
    }
    case TypeKind::Union: {
      auto& uni = static_cast<const UnionType&>(resolved);
      for (size_t i = 0; i < uni.getAlternativeCount(); ++i) {
        collectFreeTypeVars(uni.getAlternative(i), freeVars, exclude);
      }
      break;
    }
    case TypeKind::Reference: {
      auto& ref = static_cast<const ReferenceType&>(resolved);
      collectFreeTypeVars(ref.getPointeeType(), freeVars, exclude);
      break;
    }
    case TypeKind::RawPointer: {
      auto& ptr = static_cast<const RawPointerType&>(resolved);
      collectFreeTypeVars(ptr.getPointeeType(), freeVars, exclude);
      break;
    }
    default:
      // Primitive, Error, Interface, Intersection, Existential, Associated
      // are leaf types with no type variables.
      break;
  }
}

zc::Own<Type> TypeEnv::substituteType(const Type& ty,
                                      const zc::HashMap<uint64_t, TypeVar*>& subst) const {
  const Type& resolved = find(ty);

  if (isTypeVar(resolved)) {
    auto& var = static_cast<const TypeVar&>(resolved);
    uint64_t id = var.getId();

    // Check if this var is in the substitution map
    auto found = subst.find(id);
    if (found != zc::none) {
      ZC_IF_SOME(freshVar, found) {
        // Return a clone of the fresh type variable
        return zc::heap<TypeVar>(freshVar->getName(), freshVar->getId());
      }
    }

    // Check if bound to concrete type
    if (id != 0) {
      auto binding = impl->idBindings.find(id);
      if (binding != zc::none) {
        ZC_IF_SOME(boundTy, binding) {
          if (boundTy && !isTypeVar(*boundTy)) { return substituteType(*boundTy, subst); }
        }
      }
    }

    // Not substituted: return a clone
    return zc::heap<TypeVar>(var.getName(), var.getId());
  }

  // Clone compound types recursively
  switch (resolved.getKind()) {
    case TypeKind::Primitive: {
      auto& prim = static_cast<const PrimitiveType&>(resolved);
      return zc::heap<PrimitiveType>(prim.getPrimitiveKind());
    }
    case TypeKind::Function: {
      auto& fn = static_cast<const FunctionType&>(resolved);
      zc::Vector<zc::Own<Type>> params;
      for (size_t i = 0; i < fn.getParamCount(); ++i) {
        params.add(substituteType(fn.getParamType(i), subst));
      }
      auto ret = substituteType(fn.getReturnType(), subst);
      auto result = zc::heap<FunctionType>(zc::mv(params), zc::mv(ret));
      result->setVariadic(fn.isVariadic());
      auto raises = fn.getRaisesType();
      ZC_IF_SOME(r, raises) { result->setRaisesType(substituteType(r, subst)); }
      // Note: generic params are NOT copied - instantiation removes them
      return zc::mv(result);
    }
    case TypeKind::Tuple: {
      auto& tuple = static_cast<const TupleType&>(resolved);
      zc::Vector<zc::Own<Type>> elems;
      for (size_t i = 0; i < tuple.getElementCount(); ++i) {
        elems.add(substituteType(tuple.getElementType(i), subst));
      }
      return zc::heap<TupleType>(zc::mv(elems));
    }
    case TypeKind::Object: {
      auto& obj = static_cast<const ObjectType&>(resolved);
      auto result = zc::heap<ObjectType>();
      auto members = obj.getMembers();
      for (size_t i = 0; i < members.size(); ++i) {
        ZC_IF_SOME(memberType, members[i].type) {
          result->addMember(members[i].name, substituteType(memberType, subst));
        }
      }
      return zc::mv(result);
    }
    case TypeKind::Array: {
      auto& arr = static_cast<const ArrayType&>(resolved);
      auto elem = substituteType(arr.getElementType(), subst);
      return zc::heap<ArrayType>(zc::mv(elem));
    }
    case TypeKind::Named: {
      auto& named = static_cast<const NamedType&>(resolved);
      auto result = zc::heap<NamedType>(named.getName());
      for (size_t i = 0; i < named.getTypeArgCount(); ++i) {
        result->addTypeArg(substituteType(named.getTypeArg(i), subst));
      }
      return zc::mv(result);
    }
    case TypeKind::Union: {
      auto& uni = static_cast<const UnionType&>(resolved);
      zc::Vector<zc::Own<Type>> alts;
      for (size_t i = 0; i < uni.getAlternativeCount(); ++i) {
        alts.add(substituteType(uni.getAlternative(i), subst));
      }
      return zc::heap<UnionType>(zc::mv(alts));
    }
    case TypeKind::Reference: {
      auto& ref = static_cast<const ReferenceType&>(resolved);
      auto pointee = substituteType(ref.getPointeeType(), subst);
      return zc::heap<ReferenceType>(zc::mv(pointee), ref.getMutability());
    }
    case TypeKind::RawPointer: {
      auto& ptr = static_cast<const RawPointerType&>(resolved);
      auto pointee = substituteType(ptr.getPointeeType(), subst);
      return zc::heap<RawPointerType>(zc::mv(pointee), ptr.getMutability());
    }
    case TypeKind::Error: {
      return zc::heap<ErrorType>();
    }
    default:
      // For types we can't substitute into (Interface, Intersection, etc.),
      // return error type as fallback.
      return zc::heap<ErrorType>();
  }
}

zc::Own<TypeScheme> TypeEnv::generalize(const Type& ty) {
  // Collect all free (unbound) type variables in the type.
  // In a full HM system, we'd exclude vars that appear in the typing
  // context (Γ). For now, we quantify over all unbound type variables.
  zc::HashSet<uint64_t> freeVars;
  zc::HashSet<uint64_t> emptyExclude;
  collectFreeTypeVars(ty, freeVars, emptyExclude);

  // Build generic parameters for each free type variable
  zc::Vector<zc::Own<GenericParam>> params;
  for (uint64_t varId : freeVars) {
    // Find the TypeVar object for this ID
    zc::StringPtr name = "?"_zc;
    for (size_t i = 0; i < impl->typeVars.size(); ++i) {
      if (impl->typeVars[i]->getId() == varId) {
        name = impl->typeVars[i]->getName();
        break;
      }
    }
    params.add(zc::heap<GenericParam>(name));
  }

  // Clone the body type (we need an owned copy)
  zc::HashMap<uint64_t, TypeVar*> emptySubst;
  auto body = substituteType(ty, emptySubst);

  return zc::heap<TypeScheme>(zc::mv(params), zc::mv(body));
}

zc::Own<Type> TypeEnv::instantiate(const TypeScheme& scheme) {
  if (scheme.isMonomorphic()) {
    // Monomorphic: just clone the body
    zc::HashMap<uint64_t, TypeVar*> emptySubst;
    return substituteType(scheme.getBody(), emptySubst);
  }

  // Build substitution: for each quantified param, create a fresh type variable
  zc::HashMap<uint64_t, TypeVar*> subst;

  // We need to map from the scheme's quantified vars to fresh vars.
  // Since the scheme's body may contain TypeVar IDs from the original type,
  // we need to find those IDs and map them to fresh ones.

  // First, collect all type var IDs in the scheme body
  zc::HashSet<uint64_t> bodyVars;
  zc::HashSet<uint64_t> emptyExclude;
  collectFreeTypeVars(scheme.getBody(), bodyVars, emptyExclude);

  // For each free var in the body, create a fresh type variable
  for (uint64_t oldId : bodyVars) {
    // Find the name hint
    zc::StringPtr name = "?"_zc;
    for (size_t i = 0; i < impl->typeVars.size(); ++i) {
      if (impl->typeVars[i]->getId() == oldId) {
        name = impl->typeVars[i]->getName();
        break;
      }
    }

    // Create fresh type var with a new unique ID
    uint64_t freshId = impl->nextTypeVarId++;
    auto freshVar = zc::heap<TypeVar>(name, freshId);
    TypeVar* varPtr = freshVar.get();

    // Initialize union-find entry
    impl->unionParent.insert(freshId, 0);
    impl->unionRank.insert(freshId, 0);

    impl->typeVars.add(zc::mv(freshVar));
    subst.insert(oldId, varPtr);
  }

  // Substitute in the body
  return substituteType(scheme.getBody(), subst);
}

zc::Own<Type> TypeEnv::instantiateFunction(const FunctionType& fnTy) {
  if (!fnTy.isGeneric()) {
    // Monomorphic function: clone the type
    zc::HashMap<uint64_t, TypeVar*> emptySubst;
    return substituteType(fnTy, emptySubst);
  }

  // Build a type scheme from the generic function
  zc::Vector<zc::Own<GenericParam>> params;
  for (size_t i = 0; i < fnTy.getGenericParamCount(); ++i) {
    const auto& gp = fnTy.getGenericParam(i);
    auto param = zc::heap<GenericParam>(gp.name);
    // Copy bounds
    for (size_t j = 0; j < gp.upperBounds.size(); ++j) {
      // We can't easily clone bounds here, skip for now
      // In practice, bounds are checked after instantiation
    }
    params.add(zc::mv(param));
  }

  // Build substitution for the function's type parameters
  // The function's params/return may reference TypeVars with specific IDs.
  // We need to find those and map to fresh ones.

  // Collect free vars in the function type
  zc::HashSet<uint64_t> freeVars;
  zc::HashSet<uint64_t> emptyExclude;

  // Check param types
  for (size_t i = 0; i < fnTy.getParamCount(); ++i) {
    collectFreeTypeVars(fnTy.getParamType(i), freeVars, emptyExclude);
  }
  // Check return type
  collectFreeTypeVars(fnTy.getReturnType(), freeVars, emptyExclude);

  // Create fresh vars for each free var
  zc::HashMap<uint64_t, TypeVar*> subst;
  for (uint64_t oldId : freeVars) {
    zc::StringPtr name = "?"_zc;
    for (size_t i = 0; i < impl->typeVars.size(); ++i) {
      if (impl->typeVars[i]->getId() == oldId) {
        name = impl->typeVars[i]->getName();
        break;
      }
    }

    uint64_t freshId = impl->nextTypeVarId++;
    auto freshVar = zc::heap<TypeVar>(name, freshId);
    TypeVar* varPtr = freshVar.get();

    impl->unionParent.insert(freshId, 0);
    impl->unionRank.insert(freshId, 0);

    impl->typeVars.add(zc::mv(freshVar));
    subst.insert(oldId, varPtr);
  }

  // Create the instantiated function type
  zc::Vector<zc::Own<Type>> newParams;
  for (size_t i = 0; i < fnTy.getParamCount(); ++i) {
    newParams.add(substituteType(fnTy.getParamType(i), subst));
  }
  auto newRet = substituteType(fnTy.getReturnType(), subst);

  auto result = zc::heap<FunctionType>(zc::mv(newParams), zc::mv(newRet));
  result->setVariadic(fnTy.isVariadic());

  // Note: we do NOT copy generic params - they are "consumed" by instantiation

  return zc::mv(result);
}

// ===========================================================================
// Legacy binding API
// ===========================================================================

zc::String TypeEnv::makeKey(const TypeVar& var) {
  uint64_t id = var.getId();
  if (id != 0) { return zc::str("#", zc::str(id)); }
  return zc::heapString(var.getName());
}

void TypeEnv::bind(const TypeVar& var, const Type& type) {
  uint64_t id = var.getId();
  if (id != 0) {
    impl->idBindings.upsert(id, &type);
  } else {
    auto key = makeKey(var);
    impl->stringBindings.upsert(zc::mv(key), &type);
  }
}

void TypeEnv::bind(const TypeVar& var, zc::Own<Type> type) {
  const Type& ref = *type;
  impl->ownedBindings.add(zc::mv(type));
  bind(var, ref);
}

zc::Maybe<const Type&> TypeEnv::lookup(const TypeVar& var) const {
  uint64_t id = var.getId();
  if (id != 0) {
    // Check direct binding first.
    auto found = impl->idBindings.find(id);
    if (found != zc::none) {
      ZC_IF_SOME(binding, found) {
        if (binding) return *binding;
      }
    }
    // If this var has a union parent, the root might be bound.
    auto parentEntry = impl->unionParent.find(id);
    if (parentEntry != zc::none) {
      ZC_IF_SOME(parentId, parentEntry) {
        if (parentId != 0 && parentId != id) {
          uint64_t rootId = findRoot(id);
          if (rootId != id) {
            auto rootBinding = impl->idBindings.find(rootId);
            if (rootBinding != zc::none) {
              ZC_IF_SOME(binding, rootBinding) {
                if (binding) return *binding;
              }
            }
          }
        }
      }
    }
    return zc::none;
  }

  // Legacy string-keyed lookup.
  auto key = makeKey(var);
  auto found = impl->stringBindings.find(key);
  if (found == zc::none) { return zc::none; }
  ZC_IF_SOME(binding, found) {
    if (binding) return *binding;
  }
  return zc::none;
}

const Type& TypeEnv::resolve(const Type& ty) const {
  const Type* current = &ty;

  while (isTypeVar(*current)) {
    auto& var = static_cast<const TypeVar&>(*current);
    uint64_t varId = var.getId();

    const Type* bound = nullptr;

    if (varId != 0) {
      // Use find() to get proper union-find resolution with path compression.
      const Type& found = find(var);
      if (&found != &var && !isTypeVar(found)) {
        current = &found;
        continue;
      }
      // If find() returned another TypeVar, check for concrete binding.
      auto foundBinding = impl->idBindings.find(varId);
      if (foundBinding != zc::none) {
        ZC_IF_SOME(b, foundBinding) { bound = b; }
      }
      // Also check the root's binding.
      if (!bound) {
        uint64_t rootId = findRoot(varId);
        if (rootId != varId) {
          auto rootBinding = impl->idBindings.find(rootId);
          if (rootBinding != zc::none) {
            ZC_IF_SOME(b, rootBinding) {
              bound = b;
              // Path compression: bind varId directly to root's binding.
              impl->idBindings.upsert(varId, bound);
            }
          }
        }
      }
    } else {
      auto key = makeKey(var);
      auto found = impl->stringBindings.find(key);
      if (found != zc::none) {
        ZC_IF_SOME(b, found) { bound = b; }
      }
    }

    if (!bound) { break; }
    current = bound;
  }

  return *current;
}

Type& TypeEnv::resolve(Type& ty) const {
  const Type& resolved = resolve(const_cast<const Type&>(ty));
  return const_cast<Type&>(resolved);
}

bool TypeEnv::occursIn(const TypeVar& var, const Type& type) const {
  const Type& resolved = resolve(type);

  // If the resolved type is the same variable, it occurs trivially.
  if (isTypeVar(resolved)) {
    auto& resolvedVar = static_cast<const TypeVar&>(resolved);
    if (resolvedVar.equals(var)) { return true; }
  }

  // Recursively check sub-components.
  switch (resolved.getKind()) {
    case TypeKind::TypeVar: {
      return false;  // Already checked above.
    }
    case TypeKind::Function: {
      auto& fn = static_cast<const FunctionType&>(resolved);
      for (size_t i = 0; i < fn.getParamCount(); ++i) {
        if (occursIn(var, fn.getParamType(i))) { return true; }
      }
      if (occursIn(var, fn.getReturnType())) { return true; }
      auto raises = fn.getRaisesType();
      ZC_IF_SOME(r, raises) {
        if (occursIn(var, r)) { return true; }
      }
      return false;
    }
    case TypeKind::Tuple: {
      auto& tuple = static_cast<const TupleType&>(resolved);
      for (size_t i = 0; i < tuple.getElementCount(); ++i) {
        if (occursIn(var, tuple.getElementType(i))) { return true; }
      }
      return false;
    }
    case TypeKind::Object: {
      auto& obj = static_cast<const ObjectType&>(resolved);
      auto members = obj.getMembers();
      for (size_t i = 0; i < members.size(); ++i) {
        ZC_IF_SOME(memberType, members[i].type) {
          if (occursIn(var, memberType)) { return true; }
        }
      }
      return false;
    }
    case TypeKind::Array: {
      auto& arr = static_cast<const ArrayType&>(resolved);
      return occursIn(var, arr.getElementType());
    }
    case TypeKind::Named: {
      auto& named = static_cast<const NamedType&>(resolved);
      for (size_t i = 0; i < named.getTypeArgCount(); ++i) {
        if (occursIn(var, named.getTypeArg(i))) { return true; }
      }
      return false;
    }
    case TypeKind::Union: {
      auto& uni = static_cast<const UnionType&>(resolved);
      for (size_t i = 0; i < uni.getAlternativeCount(); ++i) {
        if (occursIn(var, uni.getAlternative(i))) { return true; }
      }
      return false;
    }
    case TypeKind::Reference: {
      auto& ref = static_cast<const ReferenceType&>(resolved);
      return occursIn(var, ref.getPointeeType());
    }
    case TypeKind::RawPointer: {
      auto& ptr = static_cast<const RawPointerType&>(resolved);
      return occursIn(var, ptr.getPointeeType());
    }
    default:
      // Primitive, Error, Interface, Intersection, Existential, Associated
      // are leaf types.
      return false;
  }
}

uint64_t TypeEnv::freshId() { return impl->nextTypeVarId++; }

bool TypeEnv::isBound(const TypeVar& var) const { return lookup(var) != zc::none; }

// ===========================================================================
// Impl table
// ===========================================================================

void TypeEnv::registerImpl(zc::StringPtr ifaceName, const Type& forType, ast::NodeId implNode) {
  zc::String typeKey = forType.toString();

  auto entry = TypeEnv::Impl::ImplEntry{zc::mv(typeKey), implNode};

  auto existing = impl->implTable.find(ifaceName);
  if (existing != zc::none) {
    ZC_IF_SOME(vec, existing) {
      // Update if already present for this type.
      for (size_t i = 0; i < vec.size(); ++i) {
        if (vec[i].typeKey == entry.typeKey) {
          vec[i].implNode = implNode;
          return;
        }
      }
      vec.add(zc::mv(entry));
      return;
    }
  }

  zc::Vector<TypeEnv::Impl::ImplEntry> newVec;
  newVec.add(zc::mv(entry));
  impl->implTable.insert(ifaceName, zc::mv(newVec));
}

zc::Maybe<ast::NodeId> TypeEnv::lookupImpl(zc::StringPtr ifaceName, const Type& forType) const {
  auto found = impl->implTable.find(ifaceName);
  if (found == zc::none) { return zc::none; }

  zc::String typeKey = forType.toString();

  ZC_IF_SOME(vec, found) {
    for (size_t i = 0; i < vec.size(); ++i) {
      if (vec[i].typeKey == typeKey) { return vec[i].implNode; }
    }
  }

  return zc::none;
}

bool TypeEnv::implements(const Type& ty, zc::StringPtr ifaceName) const {
  return lookupImpl(ifaceName, ty) != zc::none;
}

// ===========================================================================
// Error type
// ===========================================================================

const ErrorType& TypeEnv::errorType() const { return *impl->errorTypeInstance; }

// ===========================================================================
// Utility
// ===========================================================================

void TypeEnv::clear() {
  impl->nodeTypes.clear();
  impl->nodeTypeIds.clear();
  impl->nodeCoercions.clear();
  impl->typeVars.clear();
  impl->nextTypeVarId = 1;
  impl->unionParent.clear();
  impl->unionRank.clear();
  impl->idBindings.clear();
  impl->stringBindings.clear();
  impl->ownedBindings.clear();
  impl->implTable.clear();
}

size_t TypeEnv::size() const { return impl->idBindings.size() + impl->stringBindings.size(); }

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
