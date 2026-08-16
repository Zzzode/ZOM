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

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/identity/key/package-key.h"

namespace zomlang::compiler::identity {

class CanonicalDecoder;
class CanonicalEncoder;

enum class ToolchainComponent : uint8_t { Core = 0x01 };

/// \brief Stable identity of one compiler-provided compilation unit.
class ToolchainUnitKey final {
public:
  ZC_NODISCARD static ToolchainUnitKey core() noexcept;
  /// \brief Decodes one exact domain-separated toolchain-unit key.
  ZC_NODISCARD static zc::Maybe<ToolchainUnitKey> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Maybe<ToolchainUnitKey> decodeCanonical(CanonicalDecoder& decoder);
  ZC_NODISCARD ToolchainComponent component() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  /// \brief Encodes the exact domain-separated toolchain-unit key.
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit ToolchainUnitKey(ToolchainComponent component) noexcept;

  ToolchainComponent componentValue;
};

struct UserPackageCompilationUnit final {
  PackageKey package;
};

struct ToolchainCompilationUnit final {
  ToolchainUnitKey toolchain;
};

enum class CompilationUnitKind : uint8_t { UserPackage = 0x01, Toolchain = 0x02 };

/// \brief Exhaustive canonical identity of a user or compiler-provided compilation unit.
class CompilationUnitIdentity final {
public:
  CompilationUnitIdentity(CompilationUnitIdentity&&) noexcept = default;
  CompilationUnitIdentity& operator=(CompilationUnitIdentity&&) noexcept = default;
  ZC_DISALLOW_COPY(CompilationUnitIdentity);

  ZC_NODISCARD static CompilationUnitIdentity userPackage(PackageKey&& package);
  ZC_NODISCARD static CompilationUnitIdentity toolchain(ToolchainUnitKey toolchain);
  ZC_NODISCARD static zc::Maybe<CompilationUnitIdentity> decodeCanonical(CanonicalDecoder& decoder);
  ZC_NODISCARD CompilationUnitIdentity clone() const;
  ZC_NODISCARD CompilationUnitKind kind() const noexcept;
  /// \pre `kind() == CompilationUnitKind::UserPackage`.
  ZC_NODISCARD const PackageKey& userPackage() const;
  /// \pre `kind() == CompilationUnitKind::Toolchain`.
  ZC_NODISCARD const ToolchainUnitKey& toolchain() const;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit CompilationUnitIdentity(UserPackageCompilationUnit&& unit) noexcept;
  explicit CompilationUnitIdentity(ToolchainCompilationUnit&& unit) noexcept;

  zc::OneOf<UserPackageCompilationUnit, ToolchainCompilationUnit> value;
};

}  // namespace zomlang::compiler::identity
