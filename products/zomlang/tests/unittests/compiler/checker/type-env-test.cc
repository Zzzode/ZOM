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

#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/type/coercion.h"
#include "zomlang/compiler/type/error-type.h"
#include "zomlang/compiler/type/existential-type.h"
#include "zomlang/compiler/type/function-type.h"
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
  record.targetKind = CallTargetKind::OperatorMethod;
  record.receiverMode = ReceiverMode::OperatorLeftHandSide;
  record.interfaceName = "Add"_zc;
  record.methodName = "add"_zc;
  record.targetSymbol = symbol::SymbolId::create(99);
  record.implNode = ast::NodeId(7);
  record.argumentTypes.add(TypeId(1));
  record.argumentTypes.add(TypeId(2));
  record.resultType = TypeId(3);
  env.setDispatch(node, zc::mv(record));

  ZC_EXPECT(env.hasDispatch(node));
  auto& stored = env.getDispatch(node);
  ZC_EXPECT(stored.targetKind == CallTargetKind::OperatorMethod);
  ZC_EXPECT(stored.receiverMode == ReceiverMode::OperatorLeftHandSide);
  ZC_EXPECT(stored.interfaceName == "Add"_zc);
  ZC_EXPECT(stored.methodName == "add"_zc);
  ZC_EXPECT(stored.targetSymbol == symbol::SymbolId::create(99));
  ZC_EXPECT(stored.implNode == ast::NodeId(7));
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
