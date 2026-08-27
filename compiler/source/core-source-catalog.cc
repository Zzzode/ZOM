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

#include "compiler/source/core-source-catalog.h"

#include "zc/core/debug.h"
#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::source::core {
namespace {

zc::Array<uint8_t> encodeModule(zc::ArrayPtr<const identity::ModulePathSegment> module) {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(module.size());
  for (const auto& segment : module) { segment.encode(encoder); }
  return encoder.finish();
}

bool sameModule(zc::ArrayPtr<const identity::ModulePathSegment> left,
                zc::ArrayPtr<const identity::ModulePathSegment> right) {
  return encodeModule(left).asPtr() == encodeModule(right).asPtr();
}

bool samePath(const identity::CanonicalRelativePath& left,
              const identity::CanonicalRelativePath& right) {
  identity::CanonicalEncoder leftEncoder;
  left.encode(leftEncoder);
  identity::CanonicalEncoder rightEncoder;
  right.encode(rightEncoder);
  return leftEncoder.finish().asPtr() == rightEncoder.finish().asPtr();
}

bool nonzero(const identity::Sha256Digest& digest) {
  for (const auto byte : digest.bytes()) {
    if (byte != 0) { return true; }
  }
  return false;
}

bool isProjectedCoreCrate(const identity::CrateKey& crate,
                          const VerifiedCoreDistribution& distribution) {
  return crate.unit().kind() == identity::CompilationUnitKind::Toolchain &&
         crate.unit().toolchain().component() == identity::ToolchainComponent::Core &&
         crate.targetKind() == identity::CrateTargetKind::Library &&
         crate.targetName() == "core"_zc && !crate.compilation().hasBuildScriptProducer() &&
         crate.semanticOptions().editionYear() == distribution.record().editionYear();
}

zc::Maybe<zc::Vector<identity::ModulePathSegment>> moduleForPath(
    const identity::CanonicalRelativePath& path) {
  if (path.segments().size() == 0) { return zc::none; }
  const auto file = path.segments().back().text();
  if (!file.endsWith(".zom"_zc)) { return zc::none; }
  zc::Vector<identity::ModulePathSegment> module(path.segments().size());
  const bool moduleFile = file == "mod.zom"_zc;
  const size_t directoryCount = path.segments().size() - 1;
  for (size_t index = 0; index < directoryCount; ++index) {
    auto segment = identity::ModulePathSegment::fromCanonical(path.segments()[index].text());
    if (segment == zc::none) { return zc::none; }
    ZC_IF_SOME(value, segment) { module.add(zc::mv(value)); }
  }
  if (!moduleFile) {
    const auto stemText = zc::str(file.slice(0, file.size() - 4));
    auto stem = identity::ModulePathSegment::fromCanonical(stemText);
    if (stem == zc::none) { return zc::none; }
    ZC_IF_SOME(value, stem) { module.add(zc::mv(value)); }
  }
  if (module.empty()) { return zc::none; }
  return zc::mv(module);
}

zc::Maybe<const VerifiedCoreSourceSnapshot&> findSnapshot(
    const VerifiedCoreDistribution& distribution, const identity::CanonicalRelativePath& path) {
  for (const auto& snapshot : distribution.snapshots()) {
    if (samePath(snapshot.path(), path)) { return snapshot; }
  }
  return zc::none;
}

zc::Maybe<const CoreSourceFile&> findFile(const VerifiedCoreDistribution& distribution,
                                          const identity::CanonicalRelativePath& path) {
  for (const auto& file : distribution.record().files()) {
    if (samePath(file.path(), path)) { return file; }
  }
  return zc::none;
}

zc::Maybe<AdmittedCoreSourceCatalogEntry> makeEntry(const identity::CrateKey& crate,
                                                    const identity::CanonicalRelativePath& path,
                                                    const identity::Sha256Digest& digest) {
  auto module = moduleForPath(path);
  if (module == zc::none) { return zc::none; }
  auto source = identity::SourceFileKey::from(
      crate.clone(),
      identity::SourceOriginKey::coreFile(identity::ToolchainUnitKey::core(), path.clone()));
  return AdmittedCoreSourceCatalogEntry::from(zc::mv(ZC_ASSERT_NONNULL(module)), zc::mv(source),
                                              digest);
}

using EntryBuildResult =
    zc::OneOf<zc::Vector<AdmittedCoreSourceCatalogEntry>, CoreDistributionAdmissionFailure>;

void sortEntries(zc::Vector<AdmittedCoreSourceCatalogEntry>& entries) {
  for (size_t index = 1; index < entries.size(); ++index) {
    auto current = zc::mv(entries[index]);
    size_t insertion = index;
    while (insertion > 0 && encodeModule(current.module()).asPtr() <
                                encodeModule(entries[insertion - 1].module()).asPtr()) {
      entries[insertion] = zc::mv(entries[insertion - 1]);
      --insertion;
    }
    entries[insertion] = zc::mv(current);
  }
}

EntryBuildResult buildEntries(const VerifiedCoreDistribution& distribution,
                              const identity::CrateKey& crate) {
  zc::Vector<AdmittedCoreSourceCatalogEntry> entries(distribution.record().files().size());
  for (const auto& file : distribution.record().files()) {
    auto snapshot = findSnapshot(distribution, file.path());
    if (snapshot == zc::none) {
      return CoreDistributionAdmissionFailure::withoutCoordinate(
          CoreLibraryIssue::InputContextMismatch);
    }
    ZC_IF_SOME(value, snapshot) {
      if (value.contentDigest() != file.digest()) {
        return CoreDistributionAdmissionFailure::withoutCoordinate(
            CoreLibraryIssue::InputContextMismatch);
      }
    }
    auto entry = makeEntry(crate, file.path(), file.digest());
    if (entry == zc::none) {
      return CoreDistributionAdmissionFailure::withoutCoordinate(
          CoreLibraryIssue::InputContextMismatch);
    }
    ZC_IF_SOME(value, entry) { entries.add(zc::mv(value)); }
  }
  sortEntries(entries);
  return zc::mv(entries);
}

EntryBuildResult independentlyVerifyEntries(const VerifiedCoreDistribution& distribution,
                                            const identity::CrateKey& crate) {
  zc::Vector<AdmittedCoreSourceCatalogEntry> entries(distribution.snapshots().size());
  for (const auto& snapshot : distribution.snapshots()) {
    auto file = findFile(distribution, snapshot.path());
    if (file == zc::none) {
      return CoreDistributionAdmissionFailure::withoutCoordinate(
          CoreLibraryIssue::InputContextMismatch);
    }
    ZC_IF_SOME(value, file) {
      if (value.digest() != snapshot.contentDigest()) {
        return CoreDistributionAdmissionFailure::withoutCoordinate(
            CoreLibraryIssue::InputContextMismatch);
      }
    }
    auto entry = makeEntry(crate, snapshot.path(), snapshot.contentDigest());
    if (entry == zc::none) {
      return CoreDistributionAdmissionFailure::withoutCoordinate(
          CoreLibraryIssue::InputContextMismatch);
    }
    ZC_IF_SOME(value, entry) { entries.add(zc::mv(value)); }
  }
  sortEntries(entries);
  return zc::mv(entries);
}

bool sameEntries(zc::ArrayPtr<const AdmittedCoreSourceCatalogEntry> left,
                 zc::ArrayPtr<const AdmittedCoreSourceCatalogEntry> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (!sameModule(left[index].module(), right[index].module()) ||
        !left[index].source().sameAs(right[index].source()) ||
        left[index].contentDigest() != right[index].contentDigest()) {
      return false;
    }
  }
  return true;
}

bool isInitialCatalog(zc::ArrayPtr<const AdmittedCoreSourceCatalogEntry> entries) {
  if (entries.size() != 3) { return false; }
  const zc::StringPtr expected[][2] = {
      {"core"_zc, zc::StringPtr()},
      {"core"_zc, "marker"_zc},
      {"core"_zc, "prelude"_zc},
  };
  const size_t sizes[] = {1, 2, 2};
  for (size_t index = 0; index < 3; ++index) {
    if (entries[index].module().size() != sizes[index]) { return false; }
    for (size_t segment = 0; segment < sizes[index]; ++segment) {
      if (entries[index].module()[segment].text() != expected[index][segment]) { return false; }
    }
  }
  return true;
}

}  // namespace

struct AdmittedCoreSourceCatalogEntry::Impl final {
  Impl(zc::Vector<identity::ModulePathSegment>&& module, identity::SourceFileKey&& source,
       const identity::Sha256Digest& contentDigest)
      : module(zc::mv(module)), source(zc::mv(source)), contentDigest(contentDigest) {}
  zc::Vector<identity::ModulePathSegment> module;
  identity::SourceFileKey source;
  identity::Sha256Digest contentDigest;
};
AdmittedCoreSourceCatalogEntry::AdmittedCoreSourceCatalogEntry(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
AdmittedCoreSourceCatalogEntry::~AdmittedCoreSourceCatalogEntry() noexcept(false) = default;
AdmittedCoreSourceCatalogEntry::AdmittedCoreSourceCatalogEntry(
    AdmittedCoreSourceCatalogEntry&&) noexcept = default;
AdmittedCoreSourceCatalogEntry& AdmittedCoreSourceCatalogEntry::operator=(
    AdmittedCoreSourceCatalogEntry&&) noexcept = default;
zc::Maybe<AdmittedCoreSourceCatalogEntry> AdmittedCoreSourceCatalogEntry::from(
    zc::Vector<identity::ModulePathSegment>&& module, identity::SourceFileKey&& source,
    const identity::Sha256Digest& contentDigest) {
  const auto& crate = source.crate();
  if (module.empty() || crate.unit().kind() != identity::CompilationUnitKind::Toolchain ||
      crate.unit().toolchain().component() != identity::ToolchainComponent::Core ||
      crate.targetKind() != identity::CrateTargetKind::Library || crate.targetName() != "core"_zc ||
      crate.semanticOptions().editionYear() != 2026 ||
      crate.compilation().hasBuildScriptProducer() ||
      source.origin().kind() != identity::SourceOriginKind::CoreFile || !nonzero(contentDigest)) {
    return zc::none;
  }
  return AdmittedCoreSourceCatalogEntry(
      zc::heap<Impl>(zc::mv(module), zc::mv(source), contentDigest));
}
AdmittedCoreSourceCatalogEntry AdmittedCoreSourceCatalogEntry::clone() const {
  zc::Vector<identity::ModulePathSegment> moduleValue(impl->module.size());
  for (const auto& segment : impl->module) { moduleValue.add(segment.clone()); }
  auto result = from(zc::mv(moduleValue), impl->source.clone(), impl->contentDigest);
  return zc::mv(ZC_ASSERT_NONNULL(result));
}
zc::ArrayPtr<const identity::ModulePathSegment> AdmittedCoreSourceCatalogEntry::module()
    const noexcept {
  return impl->module.asPtr();
}
const identity::SourceFileKey& AdmittedCoreSourceCatalogEntry::source() const noexcept {
  return impl->source;
}
const identity::Sha256Digest& AdmittedCoreSourceCatalogEntry::contentDigest() const noexcept {
  return impl->contentDigest;
}

struct AdmittedCoreSourceCatalog::Impl final {
  Impl(identity::CrateKey&& crate, const identity::Sha256Digest& distribution,
       zc::Vector<AdmittedCoreSourceCatalogEntry>&& entries)
      : crate(zc::mv(crate)), distribution(distribution), entries(zc::mv(entries)) {}
  identity::CrateKey crate;
  identity::Sha256Digest distribution;
  zc::Vector<AdmittedCoreSourceCatalogEntry> entries;
};
AdmittedCoreSourceCatalog::AdmittedCoreSourceCatalog(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
AdmittedCoreSourceCatalog::~AdmittedCoreSourceCatalog() noexcept(false) = default;
AdmittedCoreSourceCatalog::AdmittedCoreSourceCatalog(AdmittedCoreSourceCatalog&&) noexcept =
    default;
AdmittedCoreSourceCatalog& AdmittedCoreSourceCatalog::operator=(
    AdmittedCoreSourceCatalog&&) noexcept = default;
const identity::CrateKey& AdmittedCoreSourceCatalog::crate() const noexcept { return impl->crate; }
const identity::Sha256Digest& AdmittedCoreSourceCatalog::distribution() const noexcept {
  return impl->distribution;
}
zc::ArrayPtr<const AdmittedCoreSourceCatalogEntry> AdmittedCoreSourceCatalog::entries()
    const noexcept {
  return impl->entries.asPtr();
}
zc::Maybe<const AdmittedCoreSourceCatalogEntry&> AdmittedCoreSourceCatalog::find(
    zc::ArrayPtr<const identity::ModulePathSegment> module) const noexcept {
  const auto expected = encodeModule(module);
  for (const auto& entry : impl->entries) {
    const auto current = encodeModule(entry.module());
    if (current.asPtr() == expected.asPtr()) { return entry; }
    if (expected.asPtr() < current.asPtr()) { return zc::none; }
  }
  return zc::none;
}

CoreSourceCatalogAdmissionResult CoreSourceCatalogAdmission::admit(
    const VerifiedCoreDistribution& distribution, const identity::CrateKey& crate) {
  if (!isProjectedCoreCrate(crate, distribution)) {
    return CoreDistributionAdmissionFailure::withoutCoordinate(
        CoreLibraryIssue::InputContextMismatch);
  }
  auto builder = buildEntries(distribution, crate);
  if (builder.is<CoreDistributionAdmissionFailure>()) {
    return zc::mv(builder.get<CoreDistributionAdmissionFailure>());
  }
  auto verifier = independentlyVerifyEntries(distribution, crate);
  if (verifier.is<CoreDistributionAdmissionFailure>()) {
    return zc::mv(verifier.get<CoreDistributionAdmissionFailure>());
  }
  auto entries = zc::mv(builder.get<zc::Vector<AdmittedCoreSourceCatalogEntry>>());
  auto verifiedEntries = zc::mv(verifier.get<zc::Vector<AdmittedCoreSourceCatalogEntry>>());
  if (!sameEntries(entries.asPtr(), verifiedEntries.asPtr()) ||
      !isInitialCatalog(entries.asPtr())) {
    return CoreDistributionAdmissionFailure::withoutCoordinate(
        CoreLibraryIssue::VerifierDisagreement);
  }
  return AdmittedCoreSourceCatalog(zc::heap<AdmittedCoreSourceCatalog::Impl>(
      crate.clone(), distribution.distributionDigest(), zc::mv(entries)));
}

}  // namespace zomlang::compiler::source::core
