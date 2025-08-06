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

#include "zomlang/compiler/ast/type.h"

#include "zc/core/memory.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/factory.h"

namespace zomlang {
namespace compiler {
namespace ast {

ZC_TEST("TypeTest.TypeReference") {
  auto id = factory::createIdentifier(zc::str("Int"));
  auto type = factory::createTypeReference(zc::mv(id), zc::none);

  ZC_EXPECT(type->getKind() == SyntaxKind::kTypeReference);
  ZC_EXPECT(type->getName() == "Int");
}

ZC_TEST("TypeTest.ArrayType") {
  auto elemType = factory::createPredefinedType(zc::str("i32"));
  auto type = factory::createArrayType(zc::mv(elemType));

  ZC_EXPECT(type->getKind() == SyntaxKind::kArrayType);
  auto arrayType = static_cast<ArrayType*>(type.get());
  ZC_EXPECT(arrayType->getElementType() != nullptr);
  ZC_EXPECT(arrayType->getElementType()->getKind() == SyntaxKind::kPredefinedType);
}

ZC_TEST("TypeTest.UnionType") {
  zc::Vector<zc::Own<Type>> types;
  types.add(factory::createPredefinedType(zc::str("i32")));
  types.add(factory::createPredefinedType(zc::str("str")));
  auto type = factory::createUnionType(zc::mv(types));

  ZC_EXPECT(type->getKind() == SyntaxKind::kUnionType);
  auto unionType = static_cast<UnionType*>(type.get());
  ZC_EXPECT(unionType->getTypes().size() == 2);
}

ZC_TEST("TypeTest.FunctionType") {
  zc::Vector<zc::Own<TypeParameter>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;
  auto returnType =
      factory::createReturnType(factory::createPredefinedType(zc::str("str")), zc::none);
  auto type = factory::createFunctionType(zc::mv(typeParams), zc::mv(params), zc::mv(returnType));

  ZC_EXPECT(type->getKind() == SyntaxKind::kFunctionType);
}

ZC_TEST("TypeTest.OptionalType") {
  auto baseType = factory::createPredefinedType(zc::str("i32"));
  auto type = factory::createOptionalType(zc::mv(baseType));

  ZC_EXPECT(type->getKind() == SyntaxKind::kOptionalType);
  auto optionalType = static_cast<OptionalType*>(type.get());
  ZC_EXPECT(optionalType->getType() != nullptr);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang