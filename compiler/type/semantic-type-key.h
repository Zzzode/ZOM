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
#include "compiler/identity/brand.h"
#include "compiler/type/semantic-type-data.h"

namespace zomlang::compiler::type {

class SemanticTypeStore;

}

namespace zomlang::compiler::type::semantic {

class StoreBoundTypeEncoder;

/// \brief Move-only canonical semantic type key bytes.
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

  friend class StoreBoundTypeEncoder;
  friend class ::zomlang::compiler::type::SemanticTypeStore;
};

/// \brief Move-only closed semantic type payload paired with its canonical key.
class CanonicalTypeData final {
public:
  CanonicalTypeData(CanonicalTypeData&&) noexcept = default;
  CanonicalTypeData& operator=(CanonicalTypeData&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalTypeData);

private:
  CanonicalTypeData(identity::SemanticContextBrand admissionContext, TypeData&& data,
                    SemanticTypeKey&& key) noexcept
      : admissionContext(admissionContext), dataValue(zc::mv(data)), keyValue(zc::mv(key)) {}

  identity::SemanticContextBrand admissionContext;
  TypeData dataValue;
  SemanticTypeKey keyValue;

  friend class ::zomlang::compiler::type::SemanticTypeStore;
};

}  // namespace zomlang::compiler::type::semantic
