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

#include "zomlang/compiler/identity/handle.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::identity {
namespace {

class TestContextRegistry final {
public:
  static ContextHandle<TestContextRegistry> issue(SemanticContextBrand context, uint32_t slot) {
    return ContextHandle<TestContextRegistry>(context, slot);
  }
};

class TestStoreRegistry final {
public:
  static StoreHandle<TestStoreRegistry> issue(SemanticContextBrand context, RegistryBrand registry,
                                              uint32_t slot) {
    return StoreHandle<TestStoreRegistry>(context, registry, slot);
  }
};

SemanticContextBrand requireContext(SemanticContextFactory& factory) {
  auto issued = factory.issue();
  ZC_IF_SOME(context, issued) { return context; }
  ZC_FAIL_REQUIRE("semantic context brand space exhausted during identity test");
}

RegistryBrand requireRegistry(RegistryBrandIssuer& issuer) {
  auto issued = issuer.issue();
  ZC_IF_SOME(registry, issued) { return registry; }
  ZC_FAIL_REQUIRE("registry brand space exhausted during identity test");
}

RegistryBrandIssuer requireRegistryIssuer(SemanticContextFactory& factory,
                                          SemanticContextBrand context) {
  auto issued = factory.issueRegistryBrandIssuer(context);
  ZC_IF_SOME(issuer, issued) { return zc::mv(issuer); }
  ZC_FAIL_REQUIRE("semantic context already owns a registry issuer during identity test");
}

}  // namespace

ZC_TEST("ContextHandle rejects default and foreign-context identity") {
  SemanticContextFactory factory;
  const auto firstContext = requireContext(factory);
  const auto secondContext = requireContext(factory);
  const ContextHandle<TestContextRegistry> invalid;
  const auto first = TestContextRegistry::issue(firstContext, 0);
  const auto same = TestContextRegistry::issue(firstContext, 0);
  const auto foreign = TestContextRegistry::issue(secondContext, 0);

  ZC_EXPECT(!invalid.isValid());
  ZC_EXPECT(first.isValid());
  ZC_EXPECT(first.belongsTo(firstContext));
  ZC_EXPECT(!first.belongsTo(secondContext));
  ZC_EXPECT(first == same);
  ZC_EXPECT(first != foreign);
}

ZC_TEST("StoreHandle rejects default, foreign-context, and foreign-registry identity") {
  SemanticContextFactory factory;
  const auto firstContext = requireContext(factory);
  const auto secondContext = requireContext(factory);
  auto firstIssuer = requireRegistryIssuer(factory, firstContext);
  auto secondIssuer = requireRegistryIssuer(factory, secondContext);
  const auto firstRegistry = requireRegistry(firstIssuer);
  const auto secondRegistry = requireRegistry(firstIssuer);
  const auto foreignRegistry = requireRegistry(secondIssuer);
  const StoreHandle<TestStoreRegistry> invalid;
  const auto first = TestStoreRegistry::issue(firstContext, firstRegistry, 0);
  const auto same = TestStoreRegistry::issue(firstContext, firstRegistry, 0);
  const auto siblingStore = TestStoreRegistry::issue(firstContext, secondRegistry, 0);
  const auto foreignContext = TestStoreRegistry::issue(secondContext, foreignRegistry, 0);

  ZC_EXPECT(!invalid.isValid());
  ZC_EXPECT(first.isValid());
  ZC_EXPECT(first.belongsTo(firstContext));
  ZC_EXPECT(first.belongsTo(firstRegistry));
  ZC_EXPECT(!first.belongsTo(secondContext));
  ZC_EXPECT(!first.belongsTo(secondRegistry));
  ZC_EXPECT(first == same);
  ZC_EXPECT(first != siblingStore);
  ZC_EXPECT(first != foreignContext);
}

ZC_TEST("StoreHandle rejects an issuer that does not belong to its context") {
  SemanticContextFactory factory;
  const auto firstContext = requireContext(factory);
  const auto secondContext = requireContext(factory);
  auto secondIssuer = requireRegistryIssuer(factory, secondContext);
  const auto secondRegistry = requireRegistry(secondIssuer);
  const auto malformed = TestStoreRegistry::issue(firstContext, secondRegistry, 0);

  ZC_EXPECT(!malformed.isValid());
  ZC_EXPECT(!malformed.belongsTo(firstContext));
  ZC_EXPECT(!malformed.belongsTo(secondRegistry));
}

}  // namespace zomlang::compiler::identity
