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

#include "compiler/identity/brand.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::identity {

ZC_TEST("SemanticContextFactory issues distinct valid brands") {
  SemanticContextFactory factory;
  auto first = factory.issue();
  auto second = factory.issue();

  bool compared = false;
  ZC_IF_SOME(firstBrand, first) {
    ZC_IF_SOME(secondBrand, second) {
      ZC_EXPECT(firstBrand.isValid());
      ZC_EXPECT(secondBrand.isValid());
      ZC_EXPECT(firstBrand != secondBrand);
      compared = true;
    }
  }
  ZC_EXPECT(compared);
}

ZC_TEST("RegistryBrandIssuer binds registry identity to its context") {
  SemanticContextFactory factory;
  auto firstContext = factory.issue();
  auto secondContext = factory.issue();

  bool compared = false;
  ZC_IF_SOME(firstContextBrand, firstContext) {
    ZC_IF_SOME(secondContextBrand, secondContext) {
      auto firstIssuer = factory.issueRegistryBrandIssuer(firstContextBrand);
      auto secondIssuer = factory.issueRegistryBrandIssuer(secondContextBrand);
      ZC_IF_SOME(firstRegistryIssuer, firstIssuer) {
        ZC_IF_SOME(secondRegistryIssuer, secondIssuer) {
          auto firstRegistry = firstRegistryIssuer.issue();
          auto secondRegistry = secondRegistryIssuer.issue();

          ZC_IF_SOME(firstRegistryBrand, firstRegistry) {
            ZC_IF_SOME(secondRegistryBrand, secondRegistry) {
              ZC_EXPECT(firstRegistryBrand.belongsTo(firstContextBrand));
              ZC_EXPECT(!firstRegistryBrand.belongsTo(secondContextBrand));
              ZC_EXPECT(secondRegistryBrand.belongsTo(secondContextBrand));
              ZC_EXPECT(firstRegistryBrand != secondRegistryBrand);
              compared = true;
            }
          }
        }
      }
    }
  }
  ZC_EXPECT(compared);
}

ZC_TEST("SemanticContextFactory rejects invalid and duplicate registry issuers") {
  SemanticContextFactory factory;
  auto context = factory.issue();

  ZC_EXPECT(factory.issueRegistryBrandIssuer(SemanticContextBrand()) == zc::none);
  ZC_IF_SOME(contextBrand, context) {
    ZC_EXPECT(factory.issueRegistryBrandIssuer(contextBrand) != zc::none);
    ZC_EXPECT(factory.issueRegistryBrandIssuer(contextBrand) == zc::none);
  }
}

ZC_TEST("Independent factories cannot issue colliding process brands") {
  SemanticContextFactory firstFactory;
  SemanticContextFactory secondFactory;
  auto firstContext = firstFactory.issue();
  auto secondContext = secondFactory.issue();
  ZC_IF_SOME(firstContextBrand, firstContext) {
    ZC_IF_SOME(secondContextBrand, secondContext) {
      ZC_EXPECT(firstContextBrand != secondContextBrand);
      auto firstIssuer = firstFactory.issueRegistryBrandIssuer(firstContextBrand);
      auto secondIssuer = secondFactory.issueRegistryBrandIssuer(secondContextBrand);
      ZC_IF_SOME(firstRegistryIssuer, firstIssuer) {
        ZC_IF_SOME(secondRegistryIssuer, secondIssuer) {
          auto firstRegistry = firstRegistryIssuer.issue();
          auto secondRegistry = secondRegistryIssuer.issue();
          ZC_IF_SOME(firstRegistryBrand, firstRegistry) {
            ZC_IF_SOME(secondRegistryBrand, secondRegistry) {
              ZC_EXPECT(firstRegistryBrand != secondRegistryBrand);
            }
          }
        }
      }
    }
  }
}

ZC_TEST("Brand issue budgets reject exhaustion before token reuse") {
  SemanticContextFactory factory(SemanticContextIssueBudget{1, 1});
  auto context = factory.issue();
  ZC_EXPECT(context != zc::none);
  ZC_EXPECT(factory.issue() == zc::none);
  ZC_IF_SOME(contextBrand, context) {
    auto issuer = factory.issueRegistryBrandIssuer(contextBrand);
    ZC_IF_SOME(registryIssuer, issuer) {
      ZC_EXPECT(registryIssuer.issue() != zc::none);
      ZC_EXPECT(registryIssuer.issue() == zc::none);
    }
  }
}

}  // namespace zomlang::compiler::identity
