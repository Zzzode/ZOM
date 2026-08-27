// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/checker/module-interface-contract.h"

#include "zc/core/vector.h"

namespace zomlang::compiler::module_interface {
namespace {

constexpr char kModuleInterfaceRevisionDomain[] = "zom.module-interface-revision";

void append(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> bytes) {
  output.addAll(bytes);
}

void appendDomain(zc::Vector<uint8_t>& output) {
  for (size_t index = 0; index + 1 < sizeof(kModuleInterfaceRevisionDomain); ++index) {
    output.add(static_cast<uint8_t>(kModuleInterfaceRevisionDomain[index]));
  }
  output.add(0);
}

void appendUint64(zc::Vector<uint8_t>& output, uint64_t value) {
  for (uint32_t index = 0; index < 8; ++index) {
    output.add(static_cast<uint8_t>((value >> (56U - index * 8U)) & 0xffU));
  }
}

bool appendSortedRecords(zc::Vector<uint8_t>& output,
                         zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> records,
                         bool frameElements) {
  appendUint64(output, records.size());
  zc::ArrayPtr<const uint8_t> previous;
  for (size_t index = 0; index < records.size(); ++index) {
    const auto record = records[index];
    if (record.size() == 0) { return false; }
    if (index != 0) {
      const size_t common = previous.size() < record.size() ? previous.size() : record.size();
      size_t offset = 0;
      while (offset < common && previous[offset] == record[offset]) { ++offset; }
      if (offset == common) {
        if (previous.size() >= record.size()) { return false; }
      } else if (previous[offset] > record[offset]) {
        return false;
      }
    }
    if (frameElements) { appendUint64(output, record.size()); }
    append(output, record);
    previous = record;
  }
  return true;
}

}  // namespace

ModuleInterfaceRevision::ModuleInterfaceRevision(const identity::Sha256Digest& value) noexcept
    : value(value) {}

const identity::Sha256Digest& ModuleInterfaceRevision::digest() const noexcept { return value; }

zc::Maybe<ModuleInterfaceRevision> ModuleInterfaceRevision::computeFramed(
    const identity::Sha256Digest& contextFingerprint,
    zc::ArrayPtr<const uint8_t> expandedOwningModule,
    const identity::Sha256Digest& sourceContentDigest,
    const identity::Sha256Digest& bindingSurfaceRevision,
    const identity::Sha256Digest& signatureFactsRevision,
    const identity::Sha256Digest& markerPolicyRegistryRevision,
    const identity::Sha256Digest& importedSignatureViewRevision,
    const identity::Sha256Digest& borrowInterfaceRevision,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> signatureRootRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> signatureDefinitionRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> supportDefinitionRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> visibleBindingRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> exportedBindingRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> implHeadRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> markerFactRecords) {
  if (expandedOwningModule.size() == 0) { return zc::none; }

  zc::Vector<uint8_t> preimage;
  appendDomain(preimage);
  append(preimage, contextFingerprint.bytes());
  append(preimage, expandedOwningModule);
  append(preimage, sourceContentDigest.bytes());
  append(preimage, bindingSurfaceRevision.bytes());
  append(preimage, signatureFactsRevision.bytes());
  append(preimage, markerPolicyRegistryRevision.bytes());
  append(preimage, importedSignatureViewRevision.bytes());
  append(preimage, borrowInterfaceRevision.bytes());
  if (!appendSortedRecords(preimage, signatureRootRecords, false) ||
      !appendSortedRecords(preimage, signatureDefinitionRecords, false) ||
      !appendSortedRecords(preimage, supportDefinitionRecords, false) ||
      !appendSortedRecords(preimage, visibleBindingRecords, false) ||
      !appendSortedRecords(preimage, exportedBindingRecords, false) ||
      !appendSortedRecords(preimage, implHeadRecords, true) ||
      !appendSortedRecords(preimage, markerFactRecords, true)) {
    return zc::none;
  }
  ZC_IF_SOME(digest, identity::sha256(preimage.asPtr())) { return ModuleInterfaceRevision(digest); }
  return zc::none;
}

SignatureAuthorizationOrigin SignatureAuthorizationOrigin::clone() const {
  if (value.is<LocalSignatureAuthorization>()) {
    return SignatureAuthorizationOrigin(LocalSignatureAuthorization{});
  }
  return SignatureAuthorizationOrigin(ImportedSignatureAuthorization{
      value.get<ImportedSignatureAuthorization>().interfaceRevision.clone()});
}

ImportedInterfaceRevision ImportedInterfaceRevision::clone() const {
  if (value.is<UserImportedInterfaceRevision>()) {
    return ImportedInterfaceRevision(
        UserImportedInterfaceRevision{value.get<UserImportedInterfaceRevision>().value});
  }
  return ImportedInterfaceRevision(ToolchainCoreImportedInterfaceRevision{
      value.get<ToolchainCoreImportedInterfaceRevision>().value.clone()});
}

ImportedBindingSurfaceRevision ImportedBindingSurfaceRevision::clone() const {
  if (value.is<UserImportedBindingSurfaceRevision>()) {
    return ImportedBindingSurfaceRevision(
        UserImportedBindingSurfaceRevision{value.get<UserImportedBindingSurfaceRevision>().value});
  }
  return ImportedBindingSurfaceRevision(ToolchainCoreImportedBindingSurfaceRevision{
      value.get<ToolchainCoreImportedBindingSurfaceRevision>().value.clone()});
}

bool isSignatureRootBinding(const binder::BindingTarget& binding) noexcept {
  const auto& value = binding.value();
  return value.is<binder::DefinitionBindingTarget>() ||
         value.is<binder::SemanticImportBindingTarget>();
}

bool sameSignatureRootBinding(const binder::BindingTarget& left,
                              const binder::BindingTarget& right) noexcept {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<binder::DefinitionBindingTarget>()) {
    return rightValue.is<binder::DefinitionBindingTarget>() &&
           leftValue.get<binder::DefinitionBindingTarget>().definition ==
               rightValue.get<binder::DefinitionBindingTarget>().definition;
  }
  if (leftValue.is<binder::SemanticImportBindingTarget>()) {
    return rightValue.is<binder::SemanticImportBindingTarget>() &&
           leftValue.get<binder::SemanticImportBindingTarget>().binding ==
               rightValue.get<binder::SemanticImportBindingTarget>().binding;
  }
  return false;
}

SignatureRootAuthorization SignatureRootAuthorization::clone() const {
  return SignatureRootAuthorization{binding.clone(),
                                    canonicalDefinition,
                                    visibility.clone(),
                                    sourceModule,
                                    bindingSurfaceRevision.clone(),
                                    origin.clone()};
}

AuthorizedSignatureBundle AuthorizedSignatureBundle::clone() const {
  zc::Vector<SignatureRootAuthorization> rootValues(roots.size());
  for (const auto& root : roots) { rootValues.add(root.clone()); }
  zc::Vector<checker::signature::SemanticSignature> definitionValues(definitions.size());
  for (const auto& definition : definitions) { definitionValues.add(definition.clone()); }
  zc::Vector<checker::signature::SemanticSignature> supportValues(supportDefinitions.size());
  for (const auto& definition : supportDefinitions) { supportValues.add(definition.clone()); }
  return AuthorizedSignatureBundle{zc::mv(rootValues), zc::mv(definitionValues),
                                   zc::mv(supportValues)};
}

}  // namespace zomlang::compiler::module_interface
