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

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/identity/identity-diagnostic-adapter.h"
#include "zomlang/compiler/identity/semantic-identity-registry-set.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::identity {

/// \brief Validated bridge from canonical source identities to live source buffers.
class SourceManagerIdentityResolver final : public IdentityDiagnosticLocationResolver {
public:
  SourceManagerIdentityResolver(SourceManagerIdentityResolver&&) noexcept;
  SourceManagerIdentityResolver& operator=(SourceManagerIdentityResolver&&) noexcept;
  ~SourceManagerIdentityResolver() noexcept(false) override;
  ZC_DISALLOW_COPY(SourceManagerIdentityResolver);

  ZC_NODISCARD static zc::Maybe<SourceManagerIdentityResolver> create(
      const SemanticIdentityRegistrySet& registries, source::SourceManager& sourceManager);

  /// \brief Binds one source handle only when the live buffer bytes equal its snapshot.
  ZC_NODISCARD bool bind(SourceFileId source, source::BufferId buffer);

  ZC_NODISCARD zc::Maybe<source::SourceLoc> resolve(
      const UnbrandedSourceRange& range) const override;

private:
  struct Impl;
  explicit SourceManagerIdentityResolver(zc::Own<Impl>&& impl) noexcept;

  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::identity
