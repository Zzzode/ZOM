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

#include "zomlang/compiler/type/constraint-set.h"

#include "zc/ztest/test.h"

namespace zomlang {
namespace compiler {
namespace type {

ZC_TEST("ConstraintSet.InitiallyEmpty") {
  ConstraintSet constraints;

  ZC_EXPECT(constraints.empty());
  ZC_EXPECT(constraints.size() == 0);
}

ZC_TEST("ConstraintSet.AddEq") {
  ConstraintSet constraints;
  TypeId left(1);
  TypeId right(2);

  constraints.addEq(left, right, zc::str("binary operands"));

  ZC_EXPECT(!constraints.empty());
  ZC_EXPECT(constraints.size() == 1);
  auto& constraint = constraints.get(0);
  ZC_EXPECT(constraint.kind == ConstraintKind::Eq);
  ZC_EXPECT(constraint.first == left);
  ZC_EXPECT(constraint.second == right);
  ZC_EXPECT(constraint.reason == "binary operands"_zc);
}

ZC_TEST("ConstraintSet.AddSub") {
  ConstraintSet constraints;
  TypeId source(3);
  TypeId target(4);

  constraints.addSub(source, target, zc::str("call argument"));

  auto& constraint = constraints.get(0);
  ZC_EXPECT(constraint.kind == ConstraintKind::Sub);
  ZC_EXPECT(constraint.first == source);
  ZC_EXPECT(constraint.second == target);
}

ZC_TEST("ConstraintSet.AddObligation") {
  ConstraintSet constraints;
  TypeId type(5);
  TypeId iface(6);

  constraints.addObligation(type, iface, zc::str("where clause"));

  auto& constraint = constraints.get(0);
  ZC_EXPECT(constraint.kind == ConstraintKind::Obligation);
  ZC_EXPECT(constraint.first == type);
  ZC_EXPECT(constraint.second == iface);
}

ZC_TEST("ConstraintSet.AddProjectionEq") {
  ConstraintSet constraints;
  TypeId projection(7);
  TypeId value(8);

  constraints.addProjectionEq(projection, value, zc::str("associated type"));

  auto& constraint = constraints.get(0);
  ZC_EXPECT(constraint.kind == ConstraintKind::ProjectionEq);
  ZC_EXPECT(constraint.first == projection);
  ZC_EXPECT(constraint.second == value);
}

ZC_TEST("ConstraintSet.Clear") {
  ConstraintSet constraints;
  constraints.addEq(TypeId(1), TypeId(2), zc::str("test"));
  ZC_EXPECT(!constraints.empty());

  constraints.clear();
  ZC_EXPECT(constraints.empty());
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
