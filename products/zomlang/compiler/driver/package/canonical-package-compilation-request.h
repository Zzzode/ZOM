// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/driver/package/package-compilation-request.h"

namespace zomlang::compiler::driver::package {

// Canonical wire foundation and compilation root record.

class CanonicalCompilationRootRecord final {
public:
  ~CanonicalCompilationRootRecord() noexcept(false);
  CanonicalCompilationRootRecord(CanonicalCompilationRootRecord&&) noexcept;
  CanonicalCompilationRootRecord& operator=(CanonicalCompilationRootRecord&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalCompilationRootRecord);

  ZC_NODISCARD static zc::Maybe<CanonicalCompilationRootRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD CanonicalCompilationRootRecord clone() const;
  ZC_NODISCARD const identity::PackageKey& package() const noexcept;
  ZC_NODISCARD identity::CrateTargetKind targetKind() const noexcept;
  ZC_NODISCARD zc::StringPtr targetName() const noexcept;
  ZC_NODISCARD uint32_t editionYear() const noexcept;
  ZC_NODISCARD bool requiresBuildScript() const noexcept;
  ZC_NODISCARD const identity::CanonicalRelativePath& sourcePath() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const CanonicalCompilationRootRecord& other) const;

private:
  struct Impl;
  explicit CanonicalCompilationRootRecord(zc::Own<Impl>&& impl) noexcept;
  ZC_NODISCARD static zc::Maybe<CanonicalCompilationRootRecord> project(
      const VerifiedCompilationRoot& root);
  zc::Own<Impl> impl;
  friend class CanonicalPackageCompilationRequest;
};

// Canonical target selection and language option records.

class CanonicalTargetSelectionRecord final {
public:
  ~CanonicalTargetSelectionRecord() noexcept(false);
  CanonicalTargetSelectionRecord(CanonicalTargetSelectionRecord&&) noexcept;
  CanonicalTargetSelectionRecord& operator=(CanonicalTargetSelectionRecord&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalTargetSelectionRecord);

  ZC_NODISCARD static zc::Maybe<CanonicalTargetSelectionRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD CanonicalTargetSelectionRecord clone() const;
  ZC_NODISCARD const identity::Sha256Digest& registryRevision() const noexcept;
  ZC_NODISCARD zc::StringPtr profile() const noexcept;
  ZC_NODISCARD const identity::CanonicalTargetSpecificationKey& semanticProjection() const noexcept;
  ZC_NODISCARD PackagePanicStrategy panicStrategy() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const CanonicalTargetSelectionRecord& other) const;

private:
  struct Impl;
  explicit CanonicalTargetSelectionRecord(zc::Own<Impl>&& impl) noexcept;
  ZC_NODISCARD static zc::Maybe<CanonicalTargetSelectionRecord> project(
      const RegisteredTargetSelection& selection);
  zc::Own<Impl> impl;
  friend class CanonicalPackageCompilationRequest;
};

class CanonicalLanguageOptionsRecord final {
public:
  ~CanonicalLanguageOptionsRecord() noexcept(false);
  CanonicalLanguageOptionsRecord(CanonicalLanguageOptionsRecord&&) noexcept;
  CanonicalLanguageOptionsRecord& operator=(CanonicalLanguageOptionsRecord&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalLanguageOptionsRecord);

  ZC_NODISCARD static CanonicalLanguageOptionsRecord project(
      const SelectedLanguageOptions& options);
  ZC_NODISCARD static zc::Maybe<CanonicalLanguageOptionsRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD CanonicalLanguageOptionsRecord clone() const;
  ZC_NODISCARD bool useUnicode() const noexcept;
  ZC_NODISCARD bool allowDollarIdentifiers() const noexcept;
  ZC_NODISCARD bool supportRegexLiterals() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const CanonicalLanguageOptionsRecord& other) const noexcept;

private:
  struct Impl;
  explicit CanonicalLanguageOptionsRecord(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

class CanonicalPackageCompilationRequest final {
public:
  ~CanonicalPackageCompilationRequest() noexcept(false);
  CanonicalPackageCompilationRequest(CanonicalPackageCompilationRequest&&) noexcept;
  CanonicalPackageCompilationRequest& operator=(CanonicalPackageCompilationRequest&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalPackageCompilationRequest);

  ZC_NODISCARD static zc::Maybe<CanonicalPackageCompilationRequest> fromVerified(
      const VerifiedPackageCompilationRequest& request);
  ZC_NODISCARD static zc::Maybe<CanonicalPackageCompilationRequest> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD CanonicalPackageCompilationRequest clone() const;
  ZC_NODISCARD zc::ArrayPtr<const CanonicalCompilationRootRecord> roots() const noexcept;
  ZC_NODISCARD const CanonicalTargetSelectionRecord& hostTarget() const noexcept;
  ZC_NODISCARD const CanonicalTargetSelectionRecord& target() const noexcept;
  ZC_NODISCARD const CanonicalLanguageOptionsRecord& languageOptions() const noexcept;
  ZC_NODISCARD PackageLockMode lockMode() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const CanonicalPackageCompilationRequest& other) const;

private:
  struct Impl;
  explicit CanonicalPackageCompilationRequest(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

class CanonicalPackageCompilationRequestProjectionVerifier final {
public:
  ZC_NODISCARD static bool verify(const CanonicalPackageCompilationRequest& candidate,
                                  const VerifiedPackageCompilationRequest& request);
};

}  // namespace zomlang::compiler::driver::package
