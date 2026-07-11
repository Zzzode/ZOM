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
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/source-key.h"

namespace zomlang::compiler::identity {

class CanonicalEncoder;

enum class DefinitionKind : uint8_t {
  ModuleAlias = 0x01,
  Function = 0x02,
  Method = 0x03,
  Constructor = 0x04,
  Destructor = 0x05,
  Class = 0x06,
  Struct = 0x07,
  Interface = 0x08,
  Enum = 0x09,
  Error = 0x0a,
  TypeAlias = 0x0b,
  AssociatedType = 0x0c,
  Field = 0x0d,
  EnumVariant = 0x0e,
  Parameter = 0x0f,
  TypeParameter = 0x10,
  Constant = 0x11,
  Static = 0x12,
  Local = 0x13,
  PatternBinding = 0x14,
  Closure = 0x15,
  ImportAlias = 0x16,
  ReexportAlias = 0x17
};

enum class AnonymousDefinitionRole : uint8_t { Lambda = 0x01, FunctionExpression = 0x02 };

struct DeclaredDefinitionNameKey final {
  DeclaredDefinitionName name;
};

struct AnonymousDefinitionNameKey final {
  AnonymousDefinitionRole role;
};

enum class DefinitionNameKind : uint8_t { Declared = 0x01, Anonymous = 0x02 };

/// \brief Closed declared-or-anonymous definition name key.
class DefinitionNameKey final {
public:
  DefinitionNameKey(DefinitionNameKey&&) noexcept = default;
  DefinitionNameKey& operator=(DefinitionNameKey&&) noexcept = default;
  ZC_DISALLOW_COPY(DefinitionNameKey);

  ZC_NODISCARD static DefinitionNameKey declared(DeclaredDefinitionName&& name);
  ZC_NODISCARD static zc::Maybe<DefinitionNameKey> anonymous(AnonymousDefinitionRole role);
  ZC_NODISCARD DefinitionNameKey clone() const;
  void encode(CanonicalEncoder& encoder) const;

private:
  explicit DefinitionNameKey(DeclaredDefinitionNameKey&& value) noexcept;
  explicit DefinitionNameKey(AnonymousDefinitionNameKey&& value) noexcept;

  zc::OneOf<DeclaredDefinitionNameKey, AnonymousDefinitionNameKey> value;
};

/// \brief One structural declared-definition path segment.
class DefinitionPathSegment final {
public:
  DefinitionPathSegment(DefinitionPathSegment&&) noexcept = default;
  DefinitionPathSegment& operator=(DefinitionPathSegment&&) noexcept = default;
  ZC_DISALLOW_COPY(DefinitionPathSegment);

  ZC_NODISCARD static zc::Maybe<DefinitionPathSegment> from(
      DefinitionKind kind, DefinitionNameKey&& name, SourceSpan&& sourceAnchor,
      uint32_t siblingOrdinal);
  ZC_NODISCARD DefinitionPathSegment clone() const;
  ZC_NODISCARD bool belongsTo(const ModuleKey& module) const;
  void encode(CanonicalEncoder& encoder) const;

private:
  DefinitionPathSegment(DefinitionKind kind, DefinitionNameKey&& name,
                        SourceSpan&& sourceAnchor, uint32_t siblingOrdinal) noexcept;

  DefinitionKind kindValue;
  DefinitionNameKey nameValue;
  SourceSpan sourceAnchorValue;
  uint32_t siblingOrdinalValue;
};

/// \brief Structural implementation-parent path segment.
class ImplPathSegment final {
public:
  ImplPathSegment(ImplPathSegment&&) noexcept = default;
  ImplPathSegment& operator=(ImplPathSegment&&) noexcept = default;
  ZC_DISALLOW_COPY(ImplPathSegment);

  ZC_NODISCARD static ImplPathSegment from(SourceSpan&& sourceAnchor,
                                           uint32_t siblingOrdinal);
  ZC_NODISCARD ImplPathSegment clone() const;
  ZC_NODISCARD bool belongsTo(const ModuleKey& module) const;
  void encode(CanonicalEncoder& encoder) const;

private:
  ImplPathSegment(SourceSpan&& sourceAnchor, uint32_t siblingOrdinal) noexcept;

  SourceSpan sourceAnchorValue;
  uint32_t siblingOrdinalValue;
};

struct DefinitionPathDefinitionComponent final {
  DefinitionPathSegment segment;
};

struct DefinitionPathImplComponent final {
  ImplPathSegment segment;
};

enum class DefinitionPathComponentKind : uint8_t { Definition = 0x01, Impl = 0x02 };

/// \brief Closed definition-or-impl structural path component.
class DefinitionPathComponent final {
public:
  DefinitionPathComponent(DefinitionPathComponent&&) noexcept = default;
  DefinitionPathComponent& operator=(DefinitionPathComponent&&) noexcept = default;
  ZC_DISALLOW_COPY(DefinitionPathComponent);

  ZC_NODISCARD static DefinitionPathComponent definition(DefinitionPathSegment&& segment);
  ZC_NODISCARD static DefinitionPathComponent impl(ImplPathSegment&& segment);
  ZC_NODISCARD DefinitionPathComponent clone() const;
  ZC_NODISCARD DefinitionPathComponentKind kind() const noexcept;
  ZC_NODISCARD bool belongsTo(const ModuleKey& module) const;
  void encode(CanonicalEncoder& encoder) const;

private:
  explicit DefinitionPathComponent(DefinitionPathDefinitionComponent&& value) noexcept;
  explicit DefinitionPathComponent(DefinitionPathImplComponent&& value) noexcept;

  zc::OneOf<DefinitionPathDefinitionComponent, DefinitionPathImplComponent> value;
};

/// \brief Complete canonical semantic definition key.
class DefinitionKey final {
public:
  DefinitionKey(DefinitionKey&&) noexcept = default;
  DefinitionKey& operator=(DefinitionKey&&) noexcept = default;
  ZC_DISALLOW_COPY(DefinitionKey);

  ZC_NODISCARD static zc::Maybe<DefinitionKey> from(
      ModuleKey&& module, zc::Vector<DefinitionPathComponent>&& path);
  ZC_NODISCARD DefinitionKey clone() const;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  DefinitionKey(ModuleKey&& module, zc::Vector<DefinitionPathComponent>&& path) noexcept;

  ModuleKey moduleValue;
  zc::Vector<DefinitionPathComponent> pathValue;
};

/// \brief Complete canonical implementation declaration key.
class ImplKey final {
public:
  ImplKey(ImplKey&&) noexcept = default;
  ImplKey& operator=(ImplKey&&) noexcept = default;
  ZC_DISALLOW_COPY(ImplKey);

  ZC_NODISCARD static zc::Maybe<ImplKey> from(
      ModuleKey&& module, zc::Vector<DefinitionPathSegment>&& parentPath, SourceSpan&& source,
      uint32_t siblingOrdinal);
  ZC_NODISCARD ImplKey clone() const;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  ImplKey(ModuleKey&& module, zc::Vector<DefinitionPathSegment>&& parentPath,
          SourceSpan&& source, uint32_t siblingOrdinal) noexcept;

  ModuleKey moduleValue;
  zc::Vector<DefinitionPathSegment> parentPathValue;
  SourceSpan sourceValue;
  uint32_t siblingOrdinalValue;
};

}  // namespace zomlang::compiler::identity
