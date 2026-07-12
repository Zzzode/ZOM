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

#include "zomlang/compiler/identity/brand.h"

#include <atomic>

#include "zc/core/map.h"
#include "zc/core/mutex.h"
#include "zc/core/refcount.h"

namespace zomlang::compiler::identity {
namespace {

constexpr uint64_t kFirstBrandToken = 1;
constexpr uint64_t kExhaustedBrandToken = ~uint64_t{0};

constinit std::atomic<uint64_t> gNextContextToken{kFirstBrandToken};
constinit std::atomic<uint64_t> gNextRegistryToken{kFirstBrandToken};

zc::Maybe<uint64_t> issueProcessToken(std::atomic<uint64_t>& nextToken) {
  uint64_t current = nextToken.load(std::memory_order_relaxed);
  while (current != kExhaustedBrandToken) {
    if (nextToken.compare_exchange_weak(current, current + 1, std::memory_order_relaxed,
                                        std::memory_order_relaxed)) {
      return current;
    }
  }
  return zc::none;
}

struct BrandStateData final {
  explicit BrandStateData(SemanticContextIssueBudget budget)
      : remainingContextIssues(budget.contextBrands),
        remainingRegistryIssues(budget.registryBrands) {}

  uint64_t remainingContextIssues;
  uint64_t remainingRegistryIssues;
  zc::HashSet<uint64_t> issuedContexts;
  zc::HashSet<uint64_t> claimedRegistryIssuers;
  zc::HashSet<uint64_t> claimedIdentityRegistrySets;
  zc::HashSet<uint64_t> claimedSemanticTypeStores;
};

class BrandState final : public zc::AtomicRefcounted {
public:
  explicit BrandState(SemanticContextIssueBudget budget) : data(budget) {}

  zc::Maybe<uint64_t> issueContextToken() const {
    auto locked = data.lockExclusive();
    if (locked->remainingContextIssues == 0) { return zc::none; }
    auto token = issueProcessToken(gNextContextToken);
    if (token == zc::none) { return zc::none; }
    --locked->remainingContextIssues;
    uint64_t issued = 0;
    ZC_IF_SOME(value, token) { issued = value; }
    locked->issuedContexts.insert(issued);
    return issued;
  }

  bool claimRegistryIssuer(uint64_t contextToken) const {
    auto locked = data.lockExclusive();
    if (!locked->issuedContexts.contains(contextToken) ||
        locked->claimedRegistryIssuers.contains(contextToken)) {
      return false;
    }
    locked->claimedRegistryIssuers.insert(contextToken);
    return true;
  }

  bool claimIdentityRegistrySet(uint64_t contextToken) const {
    auto locked = data.lockExclusive();
    if (!locked->issuedContexts.contains(contextToken) ||
        locked->claimedIdentityRegistrySets.contains(contextToken)) {
      return false;
    }
    locked->claimedIdentityRegistrySets.insert(contextToken);
    return true;
  }

  bool claimSemanticTypeStore(uint64_t contextToken) const {
    auto locked = data.lockExclusive();
    if (!locked->issuedContexts.contains(contextToken) ||
        locked->claimedSemanticTypeStores.contains(contextToken)) {
      return false;
    }
    locked->claimedSemanticTypeStores.insert(contextToken);
    return true;
  }

  zc::Maybe<uint64_t> issueRegistryToken() const {
    auto locked = data.lockExclusive();
    if (locked->remainingRegistryIssues == 0) { return zc::none; }
    auto token = issueProcessToken(gNextRegistryToken);
    if (token == zc::none) { return zc::none; }
    --locked->remainingRegistryIssues;
    return token;
  }

private:
  zc::MutexGuarded<BrandStateData> data;
};

}  // namespace

struct SemanticContextFactory::Impl final {
  explicit Impl(SemanticContextIssueBudget budget) : state(zc::arc<BrandState>(budget)) {}

  zc::Arc<BrandState> state;
};

SemanticContextFactory::SemanticContextFactory() noexcept
    : SemanticContextFactory(
          SemanticContextIssueBudget{kExhaustedBrandToken, kExhaustedBrandToken}) {}
SemanticContextFactory::SemanticContextFactory(SemanticContextIssueBudget budget) noexcept
    : impl(zc::heap<Impl>(budget)) {}
SemanticContextFactory::~SemanticContextFactory() noexcept(false) = default;
SemanticContextFactory::SemanticContextFactory(SemanticContextFactory&&) noexcept = default;
SemanticContextFactory& SemanticContextFactory::operator=(SemanticContextFactory&&) noexcept =
    default;

zc::Maybe<SemanticContextBrand> SemanticContextFactory::issue() const {
  ZC_IF_SOME(token, impl->state->issueContextToken()) { return SemanticContextBrand(token); }
  return zc::none;
}

struct RegistryBrandIssuer::Impl final {
  Impl(SemanticContextBrand owner, zc::Arc<BrandState>&& brandState)
      : context(owner), state(zc::mv(brandState)) {}

  SemanticContextBrand context;
  zc::Arc<BrandState> state;
};

RegistryBrandIssuer::RegistryBrandIssuer(zc::Own<Impl>&& issuerImpl) noexcept
    : impl(zc::mv(issuerImpl)) {}
RegistryBrandIssuer::~RegistryBrandIssuer() noexcept(false) = default;
RegistryBrandIssuer::RegistryBrandIssuer(RegistryBrandIssuer&&) noexcept = default;
RegistryBrandIssuer& RegistryBrandIssuer::operator=(RegistryBrandIssuer&&) noexcept = default;

zc::Maybe<RegistryBrand> RegistryBrandIssuer::issue() const {
  ZC_IF_SOME(token, impl->state->issueRegistryToken()) {
    return RegistryBrand(impl->context, token);
  }
  return zc::none;
}

zc::Maybe<RegistryBrandIssuer> SemanticContextFactory::issueRegistryBrandIssuer(
    SemanticContextBrand context) const {
  if (!context.isValid() || !impl->state->claimRegistryIssuer(context.token)) { return zc::none; }
  return RegistryBrandIssuer(zc::heap<RegistryBrandIssuer::Impl>(context, impl->state.addRef()));
}

bool SemanticContextFactory::claimIdentityRegistrySet(SemanticContextBrand context) const {
  return context.isValid() && impl->state->claimIdentityRegistrySet(context.token);
}

SemanticTypeStoreConstructionToken::SemanticTypeStoreConstructionToken(
    SemanticContextBrand owner) noexcept
    : context(owner) {}

SemanticTypeStoreConstructionToken::SemanticTypeStoreConstructionToken(
    SemanticTypeStoreConstructionToken&& other) noexcept
    : context(other.context) {
  other.context = SemanticContextBrand();
}

SemanticTypeStoreConstructionToken& SemanticTypeStoreConstructionToken::operator=(
    SemanticTypeStoreConstructionToken&& other) noexcept {
  if (this == &other) { return *this; }
  context = other.context;
  other.context = SemanticContextBrand();
  return *this;
}

bool SemanticTypeStoreConstructionToken::isValid() const noexcept { return context.isValid(); }

SemanticContextBrand SemanticTypeStoreConstructionToken::consume() noexcept {
  const auto owner = context;
  context = SemanticContextBrand();
  return owner;
}

zc::Maybe<SemanticTypeStoreConstructionToken>
SemanticContextFactory::issueSemanticTypeStoreConstructionToken(
    SemanticContextBrand context) const {
  if (!context.isValid() || !impl->state->claimSemanticTypeStore(context.token)) {
    return zc::none;
  }
  return SemanticTypeStoreConstructionToken(context);
}

}  // namespace zomlang::compiler::identity
