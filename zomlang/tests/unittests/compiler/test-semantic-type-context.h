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
#include "zomlang/compiler/type/semantic-type-store.h"

namespace zomlang::compiler::tests {

/// \brief Owns one complete semantic context and its sole type store for a test scope.
class TestSemanticTypeContext {
public:
  TestSemanticTypeContext() {
    auto issuedContext = factory.issue();
    ZC_REQUIRE(issuedContext != zc::none);
    ZC_IF_SOME(value, issuedContext) { context = value; }
    auto issuedIdentities = identity::IdentityInternerSet::create(factory, context);
    ZC_REQUIRE(issuedIdentities != zc::none);
    ZC_IF_SOME(value, issuedIdentities) {
      identities = zc::heap<identity::IdentityInternerSet>(zc::mv(value));
    }
    auto issuedToken = factory.issueSemanticTypeStoreConstructionToken(context);
    ZC_REQUIRE(issuedToken != zc::none);
    ZC_IF_SOME(token, issuedToken) {
      semanticTypeStore = zc::heap<type::SemanticTypeStore>(zc::mv(token), *identities);
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

  /// \brief Interns one closed primitive payload through the production admission boundary.
  identity::SemanticTypeId internPrimitive(type::semantic::PrimitiveKind kind) {
    auto canonical = semanticTypeStore->canonicalizeClosed(
        type::semantic::TypeData(type::semantic::PrimitiveTypeData{kind}));
    ZC_REQUIRE(canonical.is<type::semantic::CanonicalTypeData>());
    auto result =
        semanticTypeStore->intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
    ZC_REQUIRE(result.is<type::SemanticTypeInterned>());
    return result.get<type::SemanticTypeInterned>().id;
  }

private:
  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  zc::Own<identity::IdentityInternerSet> identities;
  zc::Own<type::SemanticTypeStore> semanticTypeStore;
};

/// \brief Issues one real semantic type identity for tests that only retain the handle value.
inline type::SemanticTypeId testSemanticType(uint32_t ordinal = 0) {
  TestSemanticTypeContext context;
  type::SemanticTypeId result;
  constexpr uint32_t firstPrimitive = static_cast<uint32_t>(type::semantic::PrimitiveKind::I8);
  constexpr uint32_t primitiveCount =
      static_cast<uint32_t>(type::semantic::PrimitiveKind::Null) - firstPrimitive + 1;
  for (uint32_t index = 0; index <= ordinal; ++index) {
    const auto kind =
        static_cast<type::semantic::PrimitiveKind>(firstPrimitive + index % primitiveCount);
    result = context.internPrimitive(kind);
  }
  return result;
}

}  // namespace zomlang::compiler::tests
