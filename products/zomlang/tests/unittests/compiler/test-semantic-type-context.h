// Copyright (c) 2026 Zode.Z. All rights reserved
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

#pragma once

#include "zc/core/memory.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/semantic-type-store.h"
#include "zomlang/compiler/type/type-env.h"

namespace zomlang::compiler::tests {

/// \brief Owns one complete semantic context and its sole type store for a test scope.
class TestSemanticTypeContext {
public:
  TestSemanticTypeContext() {
    auto issuedContext = factory.issue();
    ZC_REQUIRE(issuedContext != zc::none);
    ZC_IF_SOME(value, issuedContext) { context = value; }
    auto issuedToken = factory.issueSemanticTypeStoreConstructionToken(context);
    ZC_REQUIRE(issuedToken != zc::none);
    ZC_IF_SOME(token, issuedToken) {
      semanticTypeStore = zc::heap<type::SemanticTypeStore>(zc::mv(token));
    }
  }

  ZC_DISALLOW_COPY(TestSemanticTypeContext);
  TestSemanticTypeContext(TestSemanticTypeContext&&) noexcept = default;
  TestSemanticTypeContext& operator=(TestSemanticTypeContext&&) noexcept = default;

  /// \brief Returns the sole semantic type store for this test context.
  type::SemanticTypeStore& semanticTypes() { return *semanticTypeStore; }
  const type::SemanticTypeStore& semanticTypes() const { return *semanticTypeStore; }

  /// \brief Returns the context brand that owns the test store.
  identity::SemanticContextBrand brand() const noexcept { return context; }

private:
  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  zc::Own<type::SemanticTypeStore> semanticTypeStore;
};

/// \brief Type environment whose semantic store is owned by an earlier base subobject.
class TestTypeEnv final : private TestSemanticTypeContext, public type::TypeEnv {
public:
  TestTypeEnv() : type::TypeEnv(TestSemanticTypeContext::semanticTypes()) {}

  ZC_DISALLOW_COPY(TestTypeEnv);
  TestTypeEnv(TestTypeEnv&&) noexcept = default;
  TestTypeEnv& operator=(TestTypeEnv&&) noexcept = default;

  identity::SemanticContextBrand semanticContextBrand() const noexcept {
    return TestSemanticTypeContext::brand();
  }
};

/// \brief Issues one real semantic type identity for tests that only retain the handle value.
inline type::SemanticTypeId testSemanticType(uint32_t ordinal = 0) {
  TestSemanticTypeContext context;
  type::SemanticTypeId result;
  constexpr uint32_t primitiveCount = static_cast<uint32_t>(type::PrimitiveKind::Any) + 1;
  for (uint32_t index = 0; index <= ordinal; ++index) {
    type::PrimitiveType primitive(static_cast<type::PrimitiveKind>(index % primitiveCount));
    result = context.semanticTypes().intern(primitive);
  }
  return result;
}

}  // namespace zomlang::compiler::tests
