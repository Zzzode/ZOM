// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/identity/source-manager-identity-resolver.h"

#include "zc/core/vector.h"

namespace zomlang::compiler::identity {
namespace {

bool sameBytes(zc::ArrayPtr<const uint8_t> expected,
               zc::ArrayPtr<const zc::byte> actual) noexcept {
  if (expected.size() != actual.size()) { return false; }
  for (size_t index = 0; index < expected.size(); ++index) {
    if (expected[index] != static_cast<uint8_t>(actual[index])) { return false; }
  }
  return true;
}

struct SourceBinding final {
  SourceBinding(SourceFileKey&& source, const Sha256Digest& digest, uint64_t byteSize,
                source::BufferId buffer)
      : sourceValue(zc::mv(source)),
        digestValue(digest),
        byteSizeValue(byteSize),
        bufferValue(zc::mv(buffer)) {}
  SourceBinding(SourceBinding&&) noexcept = default;
  SourceBinding& operator=(SourceBinding&&) noexcept = default;
  ZC_DISALLOW_COPY(SourceBinding);

  SourceFileKey sourceValue;
  Sha256Digest digestValue;
  uint64_t byteSizeValue;
  source::BufferId bufferValue;
};

}  // namespace

struct SourceManagerIdentityResolver::Impl final {
  Impl(const SemanticIdentityRegistrySet& identityRegistries,
       source::SourceManager& liveSourceManager) noexcept
      : registries(identityRegistries), sourceManager(liveSourceManager) {}

  const SemanticIdentityRegistrySet& registries;
  source::SourceManager& sourceManager;
  zc::Vector<SourceBinding> bindings;
};

SourceManagerIdentityResolver::SourceManagerIdentityResolver(zc::Own<Impl>&& resolverImpl) noexcept
    : impl(zc::mv(resolverImpl)) {}
SourceManagerIdentityResolver::SourceManagerIdentityResolver(
    SourceManagerIdentityResolver&&) noexcept = default;
SourceManagerIdentityResolver& SourceManagerIdentityResolver::operator=(
    SourceManagerIdentityResolver&&) noexcept = default;
SourceManagerIdentityResolver::~SourceManagerIdentityResolver() noexcept(false) = default;

zc::Maybe<SourceManagerIdentityResolver> SourceManagerIdentityResolver::create(
    const SemanticIdentityRegistrySet& registries, source::SourceManager& sourceManager) {
  if (!registries.sourceFiles().isFrozen()) { return zc::none; }
  return SourceManagerIdentityResolver(zc::heap<Impl>(registries, sourceManager));
}

bool SourceManagerIdentityResolver::bind(SourceFileId source, source::BufferId buffer) {
  if (!buffer.isValid()) { return false; }
  auto snapshot = impl->registries.sourceSnapshot(source);
  ZC_IF_SOME(value, snapshot) {
    for (const auto& binding : impl->bindings) {
      if (binding.sourceValue.sameAs(value.source()) || binding.bufferValue == buffer) {
        return false;
      }
    }
    const auto liveBytes = impl->sourceManager.getEntireTextForBuffer(buffer);
    if (!sameBytes(value.bytes(), liveBytes)) { return false; }
    impl->bindings.add(SourceBinding(value.source().clone(), value.contentDigest(),
                                     value.bytes().size(), zc::mv(buffer)));
    return true;
  }
  return false;
}

zc::Maybe<source::SourceLoc> SourceManagerIdentityResolver::resolve(
    const UnbrandedSourceRange& range) const {
  for (const auto& binding : impl->bindings) {
    if (!range.belongsTo(binding.sourceValue) || range.contentDigest() != binding.digestValue ||
        range.byteStart() > range.byteEnd() || range.byteEnd() > binding.byteSizeValue ||
        range.byteStart() > static_cast<uint64_t>(0xffffffffu)) {
      continue;
    }
    return impl->sourceManager.getLocForOffset(binding.bufferValue,
                                                static_cast<unsigned>(range.byteStart()));
  }
  return zc::none;
}

}  // namespace zomlang::compiler::identity
