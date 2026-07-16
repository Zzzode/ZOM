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

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/type/semantic-type-data.h"

namespace zomlang::compiler::type::semantic {

/// \brief Read-only expansion boundary required by the semantic type key encoder.
class SemanticTypeKeyResolver {
public:
  virtual ~SemanticTypeKeyResolver() noexcept(false) = default;
  ZC_DISALLOW_COPY_AND_MOVE(SemanticTypeKeyResolver);

  /// \brief Resolves one semantic type handle to immutable closed type data.
  ZC_NODISCARD virtual zc::Maybe<const TypeData&> resolve(identity::SemanticTypeId id) const = 0;

  /// \brief Writes the canonical expanded definition key for one definition handle.
  virtual bool encodeDefinition(identity::CanonicalEncoder& encoder,
                                identity::DefId definition) const = 0;

protected:
  SemanticTypeKeyResolver() = default;
};

/// \brief Move-only canonical semantic type v1 key bytes.
class SemanticTypeKey final {
public:
  SemanticTypeKey(SemanticTypeKey&&) noexcept = default;
  SemanticTypeKey& operator=(SemanticTypeKey&&) noexcept = default;
  ZC_DISALLOW_COPY(SemanticTypeKey);

  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const noexcept ZC_LIFETIMEBOUND {
    return value.asPtr();
  }

private:
  explicit SemanticTypeKey(zc::Array<uint8_t>&& bytes) noexcept : value(zc::mv(bytes)) {}

  zc::Array<uint8_t> value;

  friend zc::Maybe<SemanticTypeKey> encodeSemanticTypeKeyV1(
      const TypeData& data, const SemanticTypeKeyResolver& resolver);
};

/// \brief Validates and encodes one closed semantic type using the v1 key domain.
ZC_NODISCARD zc::Maybe<SemanticTypeKey> encodeSemanticTypeKeyV1(
    const TypeData& data, const SemanticTypeKeyResolver& resolver);

}  // namespace zomlang::compiler::type::semantic
