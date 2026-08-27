// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "compiler/identity/key/definition-key.h"
#include "compiler/identity/key/module-resolution-key.h"

namespace zomlang::compiler::identity {

/// \brief Closed semantic operations that create stable selected-import slots.
enum class SemanticImportOperation : uint8_t {
  Import = 0x01,
  ForeignReexport = 0x02,
  ModuleAlias = 0x03,
};

/// \brief Stable semantic identity of one selected import or re-export namespace slot.
class ImportBindingKey final {
public:
  ImportBindingKey(ImportBindingKey&&) noexcept = default;
  ImportBindingKey& operator=(ImportBindingKey&&) noexcept = default;
  ZC_DISALLOW_COPY(ImportBindingKey);

  /// \brief Admits exactly one semantic namespace-and-name import slot.
  ZC_NODISCARD static zc::Maybe<ImportBindingKey> from(
      ModuleKey&& requester, ModuleResolutionKey&& resolution, SemanticImportOperation operation,
      DefinitionNamespace sourceNamespace, DeclaredDefinitionName&& sourceName,
      DefinitionNamespace localNamespace, DeclaredDefinitionName&& localName);

  ZC_NODISCARD ImportBindingKey clone() const;
  ZC_NODISCARD const ModuleKey& requester() const noexcept;
  ZC_NODISCARD const ModuleResolutionKey& resolution() const noexcept;
  ZC_NODISCARD SemanticImportOperation operation() const noexcept;
  ZC_NODISCARD DefinitionNamespace sourceNamespace() const noexcept;
  ZC_NODISCARD const DeclaredDefinitionName& sourceName() const noexcept;
  ZC_NODISCARD DefinitionNamespace localNamespace() const noexcept;
  ZC_NODISCARD const DeclaredDefinitionName& localName() const noexcept;

  /// \brief Encodes the exact domain-separated RFC 0017 semantic key bytes.
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

  bool operator==(const ImportBindingKey& other) const;
  bool operator!=(const ImportBindingKey& other) const { return !(*this == other); }

private:
  ImportBindingKey(ModuleKey&& requester, ModuleResolutionKey&& resolution,
                           SemanticImportOperation operation, DefinitionNamespace sourceNamespace,
                           DeclaredDefinitionName&& sourceName, DefinitionNamespace localNamespace,
                           DeclaredDefinitionName&& localName) noexcept;

  ModuleKey requesterValue;
  ModuleResolutionKey resolutionValue;
  SemanticImportOperation operationValue;
  DefinitionNamespace sourceNamespaceValue;
  DeclaredDefinitionName sourceNameValue;
  DefinitionNamespace localNamespaceValue;
  DeclaredDefinitionName localNameValue;
};

}  // namespace zomlang::compiler::identity
