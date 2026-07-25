// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/identity/semantic-import-binding-key.h"

#include "zc/core/vector.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

bool isValid(SemanticImportOperation operation) {
  switch (operation) {
    case SemanticImportOperation::Import:
    case SemanticImportOperation::ForeignReexport:
      return true;
  }
  return false;
}

bool isValid(DefinitionNamespace nameSpace) {
  switch (nameSpace) {
    case DefinitionNamespace::Value:
    case DefinitionNamespace::Type:
    case DefinitionNamespace::Module:
      return true;
  }
  return false;
}

bool sameModule(const ModuleKey& left, const ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool operationMatchesResolution(SemanticImportOperation operation,
                                ModuleDependencyKind dependencyKind) {
  switch (operation) {
    case SemanticImportOperation::Import:
      return dependencyKind == ModuleDependencyKind::Import;
    case SemanticImportOperation::ForeignReexport:
      return dependencyKind == ModuleDependencyKind::ForeignReexport;
  }
  return false;
}

zc::Array<uint8_t> domainSeparated(zc::StringPtr domain, zc::ArrayPtr<const uint8_t> record) {
  zc::Vector<uint8_t> encoded(domain.size() + 1 + record.size());
  encoded.addAll(domain.asBytes());
  encoded.add(0x00);
  encoded.addAll(record);
  return encoded.releaseAsArray();
}

}  // namespace

SemanticImportBindingKey::SemanticImportBindingKey(
    ModuleKey&& requester, ModuleResolutionKey&& resolution, SemanticImportOperation operation,
    DefinitionNamespace sourceNamespace, DeclaredDefinitionName&& sourceName,
    DefinitionNamespace localNamespace, DeclaredDefinitionName&& localName) noexcept
    : requesterValue(zc::mv(requester)),
      resolutionValue(zc::mv(resolution)),
      operationValue(operation),
      sourceNamespaceValue(sourceNamespace),
      sourceNameValue(zc::mv(sourceName)),
      localNamespaceValue(localNamespace),
      localNameValue(zc::mv(localName)) {}

zc::Maybe<SemanticImportBindingKey> SemanticImportBindingKey::from(
    ModuleKey&& requester, ModuleResolutionKey&& resolution, SemanticImportOperation operation,
    DefinitionNamespace sourceNamespace, DeclaredDefinitionName&& sourceName,
    DefinitionNamespace localNamespace, DeclaredDefinitionName&& localName) {
  if (!isValid(operation) || !isValid(sourceNamespace) || !isValid(localNamespace) ||
      !sameModule(requester, resolution.requester()) ||
      !operationMatchesResolution(operation, resolution.dependencyKind())) {
    return zc::none;
  }
  return SemanticImportBindingKey(zc::mv(requester), zc::mv(resolution), operation, sourceNamespace,
                                  zc::mv(sourceName), localNamespace, zc::mv(localName));
}

SemanticImportBindingKey SemanticImportBindingKey::clone() const {
  return SemanticImportBindingKey(requesterValue.clone(), resolutionValue.clone(), operationValue,
                                  sourceNamespaceValue, sourceNameValue.clone(),
                                  localNamespaceValue, localNameValue.clone());
}

const ModuleKey& SemanticImportBindingKey::requester() const noexcept { return requesterValue; }

const ModuleResolutionKey& SemanticImportBindingKey::resolution() const noexcept {
  return resolutionValue;
}

SemanticImportOperation SemanticImportBindingKey::operation() const noexcept {
  return operationValue;
}

DefinitionNamespace SemanticImportBindingKey::sourceNamespace() const noexcept {
  return sourceNamespaceValue;
}

const DeclaredDefinitionName& SemanticImportBindingKey::sourceName() const noexcept {
  return sourceNameValue;
}

DefinitionNamespace SemanticImportBindingKey::localNamespace() const noexcept {
  return localNamespaceValue;
}

const DeclaredDefinitionName& SemanticImportBindingKey::localName() const noexcept {
  return localNameValue;
}

zc::Array<uint8_t> SemanticImportBindingKey::encode() const {
  CanonicalEncoder record;
  requesterValue.encode(record);
  const auto resolutionBytes = resolutionValue.encode();
  record.encodeByteString(resolutionBytes.asPtr());
  record.encodeUint8(static_cast<uint8_t>(operationValue));
  record.encodeUint8(static_cast<uint8_t>(sourceNamespaceValue));
  sourceNameValue.encode(record);
  record.encodeUint8(static_cast<uint8_t>(localNamespaceValue));
  localNameValue.encode(record);

  constexpr auto domain = "zom.semantic-import-binding"_zc;
  const auto recordBytes = record.finish();
  return domainSeparated(domain, recordBytes.asPtr());
}

bool SemanticImportBindingKey::operator==(const SemanticImportBindingKey& other) const {
  return encode().asPtr() == other.encode().asPtr();
}

}  // namespace zomlang::compiler::identity
