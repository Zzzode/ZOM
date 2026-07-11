// Copyright (c) 2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/type/type-env.h"

#include "zc/core/io.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/type/associated-type.h"
#include "zomlang/compiler/type/coercion.h"
#include "zomlang/compiler/type/error-type.h"
#include "zomlang/compiler/type/existential-type.h"
#include "zomlang/compiler/type/function-type.h"
#include "zomlang/compiler/type/intersection-type.h"
#include "zomlang/compiler/type/named-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/type-scheme.h"
#include "zomlang/compiler/type/type-var.h"

namespace zomlang {
namespace compiler {
namespace type {

// ============================================================================
// TypeEnv basic operations
// ============================================================================

ZC_TEST("TypeEnv.InitiallyEmpty") {
  TypeEnv env;
  ZC_EXPECT(env.size() == 0);
  ZC_EXPECT(env.nodeTypeCount() == 0);
}

ZC_TEST("TypeEnv.SetAndGetType") {
  TypeEnv env;
  ast::NodeId node(1);
  env.setType(node, PrimitiveType::createI32());
  ZC_EXPECT(env.hasType(node));
  ZC_EXPECT(env.nodeTypeCount() == 1);
  auto& ty = env.getType(node);
  ZC_EXPECT(isPrimitive(ty));
}

ZC_TEST("TypeEnv.SetTypeAlsoAssignsCanonicalTypeId") {
  TypeEnv env;
  ast::NodeId first(1);
  ast::NodeId second(2);

  env.setType(first, PrimitiveType::createI32());
  env.setType(second, PrimitiveType::createI32());

  ZC_EXPECT(env.hasTypeId(first));
  ZC_EXPECT(env.hasTypeId(second));
  ZC_EXPECT(env.getTypeId(first).isValid());
  ZC_EXPECT(env.getTypeId(first) == env.getTypeId(second));
}

ZC_TEST("TypeEnv.OverwriteTypeUpdatesCanonicalTypeId") {
  TypeEnv env;
  ast::NodeId node(1);
  ast::NodeId strNode(2);

  env.setType(node, PrimitiveType::createI32());
  auto oldId = env.getTypeId(node);

  env.setType(node, PrimitiveType::createStr());
  env.setType(strNode, PrimitiveType::createStr());

  ZC_EXPECT(env.getTypeId(node) != oldId);
  ZC_EXPECT(env.getTypeId(node) == env.getTypeId(strNode));
}

ZC_TEST("TypeEnv.HasTypeFalseForUnset") {
  TypeEnv env;
  ast::NodeId node(1);
  ZC_EXPECT(!env.hasType(node));
  ZC_EXPECT(!env.hasTypeId(node));
}

ZC_TEST("TypeEnv.MultipleNodeTypes") {
  TypeEnv env;
  ast::NodeId n1(1);
  ast::NodeId n2(2);
  ast::NodeId n3(3);

  env.setType(n1, PrimitiveType::createI32());
  env.setType(n2, PrimitiveType::createStr());
  env.setType(n3, PrimitiveType::createBool());

  ZC_EXPECT(env.hasType(n1));
  ZC_EXPECT(env.hasType(n2));
  ZC_EXPECT(env.hasType(n3));
}

ZC_TEST("TypeEnv.OverwriteType") {
  TypeEnv env;
  ast::NodeId node(1);
  env.setType(node, PrimitiveType::createI32());
  env.setType(node, PrimitiveType::createStr());

  auto& ty = env.getType(node);
  ZC_EXPECT(ty.getKind() == TypeKind::Primitive);
}

// ============================================================================
// TypeEnv coercion records
// ============================================================================

ZC_TEST("TypeEnv.SetAndGetCoercion") {
  TypeEnv env;
  ast::NodeId node(42);

  env.setCoercion(node, CoercionKind::UnionInjection);

  ZC_EXPECT(env.hasCoercion(node));
  ZC_EXPECT(env.getCoercion(node) == CoercionKind::UnionInjection);
}

ZC_TEST("TypeEnv.ClearRemovesCoercions") {
  TypeEnv env;
  ast::NodeId node(42);

  env.setCoercion(node, CoercionKind::MutRefToSharedRef);
  ZC_EXPECT(env.hasCoercion(node));

  env.clear();
  ZC_EXPECT(!env.hasCoercion(node));
}

ZC_TEST("TypeEnv.ClearRemovesTypeIds") {
  TypeEnv env;
  ast::NodeId node(42);

  env.setType(node, PrimitiveType::createI32());
  ZC_EXPECT(env.hasTypeId(node));

  env.clear();
  ZC_EXPECT(!env.hasTypeId(node));
}

ZC_TEST("TypeEnv.SetAndGetDispatch") {
  TypeEnv env;
  ast::NodeId node(42);

  CallDispatchRecord record;
  record.targetKind = CallTargetKind::QualifiedInterfaceMethod;
  record.receiverMode = ReceiverMode::ExplicitFirstArgument;
  record.interfaceName = zc::str("Drawable"_zc);
  record.methodName = zc::str("draw"_zc);
  record.targetSymbol = symbol::SymbolId::create(99);
  record.implNode = ast::NodeId(7);
  record.vtableSlot = 11;
  record.argumentTypes.add(TypeId(1));
  record.argumentTypes.add(TypeId(2));
  record.resultType = TypeId(3);
  env.setDispatch(node, zc::mv(record));

  ZC_EXPECT(env.hasDispatch(node));
  auto& stored = env.getDispatch(node);
  ZC_EXPECT(stored.targetKind == CallTargetKind::QualifiedInterfaceMethod);
  ZC_EXPECT(stored.receiverMode == ReceiverMode::ExplicitFirstArgument);
  ZC_EXPECT(stored.interfaceName.asPtr() == "Drawable"_zc);
  ZC_EXPECT(stored.methodName.asPtr() == "draw"_zc);
  ZC_EXPECT(stored.targetSymbol == symbol::SymbolId::create(99));
  ZC_EXPECT(stored.implNode == ast::NodeId(7));
  ZC_EXPECT(stored.vtableSlot == 11);
  ZC_EXPECT(stored.argumentTypes.size() == 2);
  ZC_EXPECT(stored.argumentTypes[0] == TypeId(1));
  ZC_EXPECT(stored.argumentTypes[1] == TypeId(2));
  ZC_EXPECT(stored.resultType == TypeId(3));
}

ZC_TEST("TypeEnv.ClearRemovesDispatch") {
  TypeEnv env;
  ast::NodeId node(42);
  CallDispatchRecord record;
  record.targetKind = CallTargetKind::IndexMethod;
  env.setDispatch(node, zc::mv(record));
  ZC_EXPECT(env.hasDispatch(node));

  env.clear();
  ZC_EXPECT(!env.hasDispatch(node));
}

ZC_TEST("TypeEnv.DispatchFreezeStateClearsWithEnvironment") {
  TypeEnv env;

  ZC_EXPECT(!env.isDispatchFrozen());
  env.freezeDispatch();
  ZC_EXPECT(env.isDispatchFrozen());

  env.clear();
  ZC_EXPECT(!env.isDispatchFrozen());
}

ZC_TEST("TypeEnv.DumpDispatchEmptyTable") {
  TypeEnv env;
  zc::VectorOutputStream output;

  env.dumpDispatch(output);

  ZC_EXPECT(zc::str(output.getArray().asChars()) == "zom.dispatch.v0\n"_zc);
}

ZC_TEST("TypeEnv.DumpDispatchIsSortedAndComplete") {
  TypeEnv env;

  CallDispatchRecord later;
  later.targetKind = CallTargetKind::DynVTable;
  later.receiverMode = ReceiverMode::ImplicitSelf;
  later.interfaceName = zc::str("Drawable"_zc);
  later.methodName = zc::str("draw"_zc);
  later.vtableSlot = 3;
  later.argumentTypes.add(TypeId(1));
  later.argumentTypes.add(TypeId(2));
  later.resultType = TypeId(4);
  env.setDispatch(ast::NodeId(9), zc::mv(later));

  CallDispatchRecord earlier;
  earlier.targetKind = CallTargetKind::FreeFunction;
  earlier.receiverMode = ReceiverMode::None;
  earlier.targetSymbol = symbol::SymbolId::create(42);
  earlier.resultType = TypeId(7);
  env.setDispatch(ast::NodeId(2), zc::mv(earlier));

  zc::VectorOutputStream output;
  env.dumpDispatch(output);

  ZC_EXPECT(zc::str(output.getArray().asChars()) ==
            "zom.dispatch.v0\n"
            "node=2 target=FreeFunction receiver=None symbol=42 args=[] result=7\n"
            "node=9 target=DynVTable receiver=ImplicitSelf interface=Drawable method=draw "
            "slot=3 args=[1,2] result=4\n"_zc);
}

// ============================================================================
// TypeEnv fresh type variables
// ============================================================================

ZC_TEST("TypeEnv.FreshTypeVar") {
  TypeEnv env;
  auto& tv = env.freshTypeVar();
  ZC_EXPECT(isTypeVar(tv));
}

ZC_TEST("TypeEnv.FreshTypeVarsHaveUniqueIds") {
  TypeEnv env;
  auto& tv1 = env.freshTypeVar();
  auto& tv2 = env.freshTypeVar();
  auto& tv3 = env.freshTypeVar();
  ZC_EXPECT(tv1.getId() != tv2.getId());
  ZC_EXPECT(tv2.getId() != tv3.getId());
  ZC_EXPECT(tv1.getId() != tv3.getId());
}

ZC_TEST("TypeEnv.FreshTypeVarWithName") {
  TypeEnv env;
  auto& tv = env.freshTypeVar("T"_zc);
  ZC_EXPECT(tv.getName() == "T"_zc);
}

ZC_TEST("TypeEnv.InstantiateFunctionHandlesRepeatedTypeVar") {
  TypeEnv env;
  auto& tv = env.freshTypeVar("T"_zc);

  zc::Vector<zc::Own<Type>> params;
  params.add(zc::heap<TypeVar>(tv.getName(), tv.getId()));
  auto ret = zc::heap<TypeVar>(tv.getName(), tv.getId());
  FunctionType fn(zc::mv(params), zc::mv(ret));
  fn.addGenericParam(zc::heap<GenericParam>("T"_zc));

  auto instantiated = env.instantiateFunction(fn);
  ZC_EXPECT(static_cast<bool>(instantiated));
  ZC_EXPECT(isFunction(*instantiated));
}

ZC_TEST("TypeEnv.InstantiatePreservesMonomorphicExistential") {
  TypeEnv env;
  zc::Vector<zc::Own<GenericParam>> params;
  zc::Vector<zc::StringPtr> markers;
  markers.add("Sendable"_zc);
  auto body = zc::heap<ExistentialType>(zc::heap<NamedType>("Drawable"_zc), markers.asPtr());
  TypeScheme scheme(zc::mv(params), zc::mv(body));

  auto instantiated = env.instantiate(scheme);

  ZC_EXPECT(static_cast<bool>(instantiated));
  ZC_EXPECT(isExistential(*instantiated));
  if (isExistential(*instantiated)) {
    const auto& existential = static_cast<const ExistentialType&>(*instantiated);
    ZC_EXPECT(existential.getMarkerCount() == 1);
    ZC_EXPECT(existential.getMarkerName(0) == "Sendable"_zc);
  }
}

ZC_TEST("TypeEnv.InstantiatePreservesMonomorphicAssociatedType") {
  TypeEnv env;
  zc::Vector<zc::Own<GenericParam>> params;
  auto body = zc::heap<AssociatedType>(zc::heap<NamedType>("Iterator"_zc), "Item"_zc);
  TypeScheme scheme(zc::mv(params), zc::mv(body));

  auto instantiated = env.instantiate(scheme);

  ZC_EXPECT(static_cast<bool>(instantiated));
  ZC_EXPECT(isAssociated(*instantiated));
  if (isAssociated(*instantiated)) {
    const auto& associated = static_cast<const AssociatedType&>(*instantiated);
    ZC_EXPECT(associated.getName() == "Item"_zc);
    ZC_EXPECT(isNamed(associated.getParentType()));
  }
}

ZC_TEST("TypeEnv.InstantiatePreservesMonomorphicIntersection") {
  TypeEnv env;
  zc::Vector<zc::Own<GenericParam>> params;
  zc::Vector<zc::Own<Type>> conjuncts;
  conjuncts.add(zc::heap<NamedType>("Drawable"_zc));
  conjuncts.add(zc::heap<NamedType>("Clickable"_zc));
  auto body = zc::heap<IntersectionType>(zc::mv(conjuncts));
  TypeScheme scheme(zc::mv(params), zc::mv(body));

  auto instantiated = env.instantiate(scheme);

  ZC_EXPECT(static_cast<bool>(instantiated));
  ZC_EXPECT(isIntersection(*instantiated));
  if (isIntersection(*instantiated)) {
    const auto& intersection = static_cast<const IntersectionType&>(*instantiated);
    ZC_EXPECT(intersection.getConjunctCount() == 2);
    ZC_EXPECT(isNamed(intersection.getConjunct(0)));
    ZC_EXPECT(isNamed(intersection.getConjunct(1)));
  }
}

ZC_TEST("TypeEnv.GeneralizeCollectsExistentialInterfaceTypeVars") {
  TypeEnv env;
  auto& tv = env.freshTypeVar("T"_zc);
  auto iface = zc::heap<NamedType>("Box"_zc);
  iface->addTypeArg(zc::heap<TypeVar>(tv.getName(), tv.getId()));
  zc::Vector<zc::StringPtr> markers;
  markers.add("Sendable"_zc);
  auto dynTy = zc::heap<ExistentialType>(zc::mv(iface), markers.asPtr());

  auto scheme = env.generalize(*dynTy);

  ZC_EXPECT(scheme->getParamCount() == 1);
  if (scheme->getParamCount() == 1) { ZC_EXPECT(scheme->getParam(0).name == "T"_zc); }
}

ZC_TEST("TypeEnv.GeneralizeCollectsAssociatedParentTypeVars") {
  TypeEnv env;
  auto& tv = env.freshTypeVar("T"_zc);
  auto parent = zc::heap<NamedType>("Iterator"_zc);
  parent->addTypeArg(zc::heap<TypeVar>(tv.getName(), tv.getId()));
  auto associated = zc::heap<AssociatedType>(zc::mv(parent), "Item"_zc);

  auto scheme = env.generalize(*associated);

  ZC_EXPECT(scheme->getParamCount() == 1);
  if (scheme->getParamCount() == 1) { ZC_EXPECT(scheme->getParam(0).name == "T"_zc); }
}

ZC_TEST("TypeEnv.GeneralizeCollectsIntersectionConjunctTypeVars") {
  TypeEnv env;
  auto& tv = env.freshTypeVar("T"_zc);
  auto drawable = zc::heap<NamedType>("Drawable"_zc);
  drawable->addTypeArg(zc::heap<TypeVar>(tv.getName(), tv.getId()));
  zc::Vector<zc::Own<Type>> conjuncts;
  conjuncts.add(zc::mv(drawable));
  conjuncts.add(zc::heap<NamedType>("Clickable"_zc));
  auto intersection = zc::heap<IntersectionType>(zc::mv(conjuncts));

  auto scheme = env.generalize(*intersection);

  ZC_EXPECT(scheme->getParamCount() == 1);
  if (scheme->getParamCount() == 1) { ZC_EXPECT(scheme->getParam(0).name == "T"_zc); }
}

// ============================================================================
// TypeEnv type variable binding and resolution
// ============================================================================

ZC_TEST("TypeEnv.BindTypeVar") {
  TypeEnv env;
  auto& tv = env.freshTypeVar();
  auto i32 = PrimitiveType::createI32();
  env.bind(tv, *i32);

  ZC_EXPECT(env.isBound(tv));
  auto bound = env.lookup(tv);
  ZC_EXPECT(bound != zc::none);
}

ZC_TEST("TypeEnv.UnboundTypeVar") {
  TypeEnv env;
  auto& tv = env.freshTypeVar();
  ZC_EXPECT(!env.isBound(tv));
  ZC_EXPECT(env.lookup(tv) == zc::none);
}

ZC_TEST("TypeEnv.ResolveBoundTypeVar") {
  TypeEnv env;
  auto& tv = env.freshTypeVar();
  auto i32 = PrimitiveType::createI32();
  env.bind(tv, *i32);

  auto& resolved = env.resolve(tv);
  ZC_EXPECT(isPrimitive(resolved));
}

ZC_TEST("TypeEnv.ResolveUnboundTypeVarReturnsSelf") {
  TypeEnv env;
  auto& tv = env.freshTypeVar();
  auto& resolved = env.resolve(tv);
  ZC_EXPECT(isTypeVar(resolved));
}

ZC_TEST("TypeEnv.ResolveChainOfBindings") {
  TypeEnv env;
  auto& tv1 = env.freshTypeVar();
  auto& tv2 = env.freshTypeVar();
  auto i32 = PrimitiveType::createI32();

  env.bind(tv1, tv2);
  env.bind(tv2, *i32);

  auto& resolved = env.resolve(tv1);
  ZC_EXPECT(isPrimitive(resolved));
}

ZC_TEST("TypeEnv.OwnsBoundType") {
  TypeEnv env;
  auto& tv = env.freshTypeVar();
  auto i32 = PrimitiveType::createI32();
  env.bind(tv, zc::Own<Type>(zc::mv(i32)));

  ZC_EXPECT(env.isBound(tv));
  auto& resolved = env.resolve(tv);
  ZC_EXPECT(isPrimitive(resolved));
}

ZC_TEST("TypeEnv.CopiesBorrowedBindingBeforeSourceDies") {
  TypeEnv env;
  auto& tv = env.freshTypeVar();
  {
    auto i32 = PrimitiveType::createI32();
    env.bind(tv, *i32);
  }

  const auto& resolved = env.resolve(tv);
  ZC_EXPECT(isPrimitive(resolved));
  if (isPrimitive(resolved)) {
    ZC_EXPECT(static_cast<const PrimitiveType&>(resolved).getPrimitiveKind() == PrimitiveKind::I32);
  }
}

ZC_TEST("TypeEnv.OwnsConcreteUnificationBinding") {
  TypeEnv env;
  auto& tv = env.freshTypeVar();
  {
    auto i32 = PrimitiveType::createI32();
    env.unite(tv, *i32);
  }

  const auto& resolved = env.resolve(tv);
  ZC_EXPECT(isPrimitive(resolved));
  if (isPrimitive(resolved)) {
    ZC_EXPECT(static_cast<const PrimitiveType&>(resolved).getPrimitiveKind() == PrimitiveKind::I32);
  }
}

// ============================================================================
// TypeEnv occurs check
// ============================================================================

ZC_TEST("TypeEnv.OccursInSimple") {
  TypeEnv env;
  auto& tv = env.freshTypeVar();
  auto i32 = PrimitiveType::createI32();

  // tv does not occur in i32
  ZC_EXPECT(!env.occursIn(tv, *i32));
}

ZC_TEST("TypeEnv.OccursInSelf") {
  TypeEnv env;
  auto& tv = env.freshTypeVar();

  // tv occurs in itself
  ZC_EXPECT(env.occursIn(tv, tv));
}

ZC_TEST("TypeEnv.OccursInThroughBinding") {
  TypeEnv env;
  auto& tv1 = env.freshTypeVar();
  auto& tv2 = env.freshTypeVar();

  env.bind(tv2, tv1);
  // tv1 occurs in tv2 (through the binding)
  ZC_EXPECT(env.occursIn(tv1, tv2));
}

// ============================================================================
// TypeEnv union-find operations
// ============================================================================

ZC_TEST("TypeEnv.FindUnboundVarReturnsSelf") {
  TypeEnv env;
  auto& tv = env.freshTypeVar();
  auto& found = env.find(tv);
  ZC_EXPECT(isTypeVar(found));
}

ZC_TEST("TypeEnv.FindBoundVarReturnsBinding") {
  TypeEnv env;
  auto& tv = env.freshTypeVar();
  auto i32 = PrimitiveType::createI32();
  env.bind(tv, *i32);

  auto& found = env.find(tv);
  ZC_EXPECT(isPrimitive(found));
}

ZC_TEST("TypeEnv.UniteTwoVars") {
  TypeEnv env;
  auto& tv1 = env.freshTypeVar();
  auto& tv2 = env.freshTypeVar();

  env.unite(tv1, tv2);

  // After unite, find should return the same representative
  auto& f1 = env.find(tv1);
  auto& f2 = env.find(tv2);
  ZC_EXPECT(&f1 == &f2);
}

// ============================================================================
// TypeEnv impl table
// ============================================================================

ZC_TEST("TypeEnv.RegisterAndLookupImpl") {
  TypeEnv env;
  auto i32 = PrimitiveType::createI32();
  ast::NodeId implNode(10);

  env.registerImpl("Display"_zc, *i32, implNode);
  ZC_EXPECT(env.implements(*i32, "Display"_zc));

  auto result = env.lookupImpl("Display"_zc, *i32);
  ZC_EXPECT(result != zc::none);
  ZC_IF_SOME(node, result) { ZC_EXPECT(node == implNode); }
}

ZC_TEST("TypeEnv.LookupMissingImpl") {
  TypeEnv env;
  auto i32 = PrimitiveType::createI32();
  ZC_EXPECT(!env.implements(*i32, "NonExistent"_zc));
}

ZC_TEST("TypeEnv.MultipleImpls") {
  TypeEnv env;
  auto i32 = PrimitiveType::createI32();

  env.registerImpl("Display"_zc, *i32, ast::NodeId(1));
  env.registerImpl("Debug"_zc, *i32, ast::NodeId(2));
  env.registerImpl("Hash"_zc, *i32, ast::NodeId(3));

  ZC_EXPECT(env.implements(*i32, "Display"_zc));
  ZC_EXPECT(env.implements(*i32, "Debug"_zc));
  ZC_EXPECT(env.implements(*i32, "Hash"_zc));
}

// ============================================================================
// TypeEnv error type
// ============================================================================

ZC_TEST("TypeEnv.ErrorTypeSingleton") {
  TypeEnv env;
  auto& err1 = env.errorType();
  auto& err2 = env.errorType();
  ZC_EXPECT(&err1 == &err2);
  ZC_EXPECT(isError(err1));
}

// ============================================================================
// TypeEnv clear
// ============================================================================

ZC_TEST("TypeEnv.ClearRemovesAll") {
  TypeEnv env;
  ast::NodeId node(1);
  env.setType(node, PrimitiveType::createI32());
  ZC_EXPECT(env.hasType(node));

  env.clear();
  ZC_EXPECT(!env.hasType(node));
  ZC_EXPECT(env.size() == 0);
}

// ============================================================================
// TypeEnv size tracking
// ============================================================================

ZC_TEST("TypeEnv.SizeIncreasesWithBindings") {
  TypeEnv env;
  ZC_EXPECT(env.size() == 0);

  auto& tv1 = env.freshTypeVar();
  auto i32 = PrimitiveType::createI32();
  env.bind(tv1, *i32);
  ZC_EXPECT(env.size() >= 1);
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
