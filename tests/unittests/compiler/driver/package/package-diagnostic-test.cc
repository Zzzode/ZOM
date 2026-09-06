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

#include "compiler/driver/package/package-diagnostic.h"

#include "compiler/diagnostics/fact/diagnostic-materializer.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

struct ManifestIssueCase final {
  ManifestIssue issue;
  zc::StringPtr display;
};

constexpr ManifestIssueCase ISSUE_CASES[] = {
    {ManifestIssue::ReadFailed, "read-failed"_zc},
    {ManifestIssue::InvalidUtf8, "invalid-utf8"_zc},
    {ManifestIssue::ByteOrderMarkPresent, "byte-order-mark-present"_zc},
    {ManifestIssue::TomlSyntax, "toml-syntax"_zc},
    {ManifestIssue::UnknownTable, "unknown-table"_zc},
    {ManifestIssue::UnknownKey, "unknown-key"_zc},
    {ManifestIssue::MissingRequiredKey, "missing-required-key"_zc},
    {ManifestIssue::WrongValueType, "wrong-value-type"_zc},
    {ManifestIssue::InvalidStrongScalar, "invalid-strong-scalar"_zc},
    {ManifestIssue::UnsupportedEdition, "unsupported-edition"_zc},
    {ManifestIssue::InvalidPath, "invalid-path"_zc},
    {ManifestIssue::PathOutsideRoot, "path-outside-root"_zc},
    {ManifestIssue::DuplicateCanonicalValue, "duplicate-canonical-value"_zc},
    {ManifestIssue::WorkspaceMemberMissing, "workspace-member-missing"_zc},
    {ManifestIssue::NestedWorkspace, "nested-workspace"_zc},
    {ManifestIssue::DuplicateWorkspacePackageName, "duplicate-workspace-package-name"_zc},
    {ManifestIssue::TargetCollision, "target-collision"_zc},
    {ManifestIssue::TargetPathCollision, "target-path-collision"_zc},
    {ManifestIssue::MissingTargetPath, "missing-target-path"_zc},
    {ManifestIssue::DependencySourceConflict, "dependency-source-conflict"_zc},
    {ManifestIssue::InvalidVersionConstraint, "invalid-version-constraint"_zc},
    {ManifestIssue::InvalidVcsSelector, "invalid-vcs-selector"_zc},
    {ManifestIssue::InvalidFeatureEdge, "invalid-feature-edge"_zc},
    {ManifestIssue::FeatureCycle, "feature-cycle"_zc},
};

identity::CanonicalWorkspaceRelativePath workspacePath(uint32_t parents, zc::StringPtr segment) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto value = identity::CanonicalPathSegment::fromCanonical(segment);
  ZC_IF_SOME(admitted, value) { segments.add(zc::mv(admitted)); }
  ZC_REQUIRE(segments.size() == 1);
  return identity::CanonicalWorkspaceRelativePath::from(parents, zc::mv(segments));
}

InputDocumentKey documentKey(zc::StringPtr path, zc::ArrayPtr<const zc::byte> source) {
  auto digest = identity::sha256(source);
  ZC_IF_SOME(digestValue, digest) {
    auto key = InputDocumentKey::from(InputDocumentKind::Manifest,
                                      DiagnosticDocumentPath::workspace(workspacePath(0, path)),
                                      digestValue);
    ZC_IF_SOME(admitted, key) { return zc::mv(admitted); }
  }
  ZC_FAIL_REQUIRE("invalid package diagnostic document fixture");
}

DiagnosticAnchor anchor(const PackageDiagnosticDocument& document, uint64_t start, uint64_t end) {
  auto span =
      ManifestSpan::from(document.key().clone(), document.sourceView().originalSize(), start, end);
  ZC_IF_SOME(admitted, span) { return DiagnosticAnchor::manifest(zc::mv(admitted)); }
  ZC_FAIL_REQUIRE("invalid package diagnostic span fixture");
}

DiagnosticAnchor forgedAnchor(const PackageDiagnosticDocument& document, uint64_t declaredLength,
                              uint64_t start, uint64_t end) {
  auto span = ManifestSpan::from(document.key().clone(), declaredLength, start, end);
  ZC_IF_SOME(admitted, span) { return DiagnosticAnchor::manifest(zc::mv(admitted)); }
  ZC_FAIL_REQUIRE("invalid forged package diagnostic span fixture");
}

ManifestFailure failure(const PackageDiagnosticDocument& document, ManifestIssue issue,
                        zc::Vector<DiagnosticAnchor>&& related = {}) {
  auto provenance = DiagnosticProvenance::from(anchor(document, 0, 1), zc::mv(related));
  ZC_IF_SOME(admitted, provenance) { return ManifestFailure::invalid(zc::mv(admitted), issue); }
  ZC_FAIL_REQUIRE("invalid package diagnostic provenance fixture");
}

PackageDiagnosticDocument document(zc::StringPtr path, zc::ArrayPtr<const zc::byte> source) {
  auto value = PackageDiagnosticDocument::from(documentKey(path, source), source);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("package diagnostic document admission failed");
}

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid scalar diagnostic fixture");
}

identity::CanonicalRelativePath sourcePath(zc::StringPtr first, zc::StringPtr second,
                                           zc::StringPtr third) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>(first));
  segments.add(scalar<identity::CanonicalPathSegment>(second));
  segments.add(scalar<identity::CanonicalPathSegment>(third));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

NormalizedManifest reservationManifest(zc::StringPtr source) {
  zc::Vector<identity::CanonicalRelativePath> files;
  files.add(sourcePath("src"_zc, "bin"_zc, "core.zom"_zc));
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  ZC_IF_SOME(value, inventory) {
    ManifestParser parser;
    auto parsed = parser.parseWorkspaceManifest(workspacePath(0, "Zom.toml"_zc), source, value);
    if (parsed.is<NormalizedManifest>()) { return zc::mv(parsed.get<NormalizedManifest>()); }
  }
  ZC_FAIL_REQUIRE("valid reservation diagnostic manifest was rejected");
}

identity::PackageKey reservationPackageKey() {
  zc::Vector<identity::CanonicalPathSegment> sourceSegments;
  zc::Vector<identity::FeatureName> features;
  auto sortedFeatures = identity::SortedFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(sortedFeatures != zc::none);
  ZC_IF_SOME(featureValue, sortedFeatures) {
    return identity::PackageKey::from(
        identity::CanonicalPackageSource::localPath(
            identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(sourceSegments))),
        scalar<identity::PackageName>("app"_zc), scalar<identity::ResolvedVersion>("1.0.0"_zc),
        zc::mv(featureValue));
  }
  ZC_UNREACHABLE;
}

diagnostics::CompilationDiagnosticFacts seal(PackageDiagnosticFactProjection&& projection) {
  diagnostics::CompilationDiagnosticCollector collector;
  for (const auto& authority : projection.documents) {
    ZC_REQUIRE(collector.addDocumentAuthority(authority.identity.asPtr(), authority.byteLength));
  }
  ZC_REQUIRE(collector.addRoot(projection.facts.asPtr(), projection.provenance.asPtr()));
  auto result = zc::mv(collector).seal();
  ZC_REQUIRE(result.is<diagnostics::CompilationDiagnosticFacts>());
  return zc::mv(result).get<diagnostics::CompilationDiagnosticFacts>();
}

}  // namespace

ZC_TEST("PackageDiagnosticTest.SanitizesSourceAndProjectsOriginalOffsets") {
  const zc::byte sourceBytes[] = {'A', '\\', '\n', 0xc3, 0xa9, 0xff};
  auto view = SanitizedSourceView::from(sourceBytes);

  ZC_EXPECT(view.escapedSource() == "A\\\\\\u{A}\\u{E9}\\xFF"_zc);
  ZC_EXPECT(view.originalSize() == 6);
  ZC_EXPECT(view.escapedOffset(0) == 0);
  ZC_EXPECT(view.escapedOffset(1) == 1);
  ZC_EXPECT(view.escapedOffset(2) == 3);
  ZC_EXPECT(view.escapedOffset(3) == 8);
  ZC_EXPECT(view.escapedOffset(4) == 8);
  ZC_EXPECT(view.escapedOffset(5) == 14);
  ZC_EXPECT(view.escapedOffset(6) == 18);
}

ZC_TEST("PackageDiagnosticTest.VerifiesDigestAndUsesHostPathFreeDisplayName") {
  const auto source = "[package]"_zc.asBytes();
  auto admitted = PackageDiagnosticDocument::from(documentKey("Zom.toml"_zc, source), source);
  ZC_IF_SOME(documentValue, admitted) {
    ZC_EXPECT(documentValue.displayName() == "Zom.toml"_zc);
  } else {
    ZC_FAIL_EXPECT("valid diagnostic document was rejected");
  }

  auto wrongKey = documentKey("Zom.toml"_zc, "different"_zc.asBytes());
  ZC_EXPECT(PackageDiagnosticDocument::from(zc::mv(wrongKey), source) == zc::none);
}

ZC_TEST("PackageDiagnosticTest.ProjectsEveryManifestIssueThroughClosedFact") {
  const auto source = "x"_zc.asBytes();
  zc::Vector<PackageDiagnosticDocument> documents;
  documents.add(document("Zom.toml"_zc, source));

  for (const auto& issueCase : ISSUE_CASES) {
    ZC_EXPECT(manifestIssueDisplay(issueCase.issue) == issueCase.display);
    auto manifestFailure = failure(documents[0], issueCase.issue);
    auto result = projectManifestFailure(documents.asPtr(), manifestFailure);
    ZC_REQUIRE(result.is<PackageDiagnosticFactProjection>());
    const auto& projection = result.get<PackageDiagnosticFactProjection>();
    ZC_REQUIRE(projection.facts.size() == 1);
    ZC_EXPECT(projection.facts[0].code() == diagnostics::DiagID::PackageManifestInvalid);
    ZC_REQUIRE(projection.facts[0].arguments().size() == 1);
    ZC_EXPECT(projection.facts[0].arguments()[0] == issueCase.display);
    ZC_REQUIRE(projection.facts[0].secondary().size() == 1);
    ZC_EXPECT(projection.facts[0].secondary()[0].role() ==
              diagnostics::DiagnosticSecondaryRole::Highlight);
    ZC_REQUIRE(projection.documents.size() == 1);
    ZC_EXPECT(projection.documents[0].byteLength == source.size());
  }
}

ZC_TEST("PackageDiagnosticTest.MaterializesCrossDocumentRelatedNote") {
  const auto source = "x"_zc.asBytes();
  zc::Vector<PackageDiagnosticDocument> documents;
  documents.add(document("first.toml"_zc, source));
  documents.add(document("second.toml"_zc, source));
  zc::Vector<DiagnosticAnchor> related;
  related.add(anchor(documents[0], 0, 1));
  auto manifestFailure =
      failure(documents[1], ManifestIssue::DuplicateWorkspacePackageName, zc::mv(related));
  auto projected = projectManifestFailure(documents.asPtr(), manifestFailure);
  ZC_REQUIRE(projected.is<PackageDiagnosticFactProjection>());
  auto sealed = seal(zc::mv(projected).get<PackageDiagnosticFactProjection>());
  auto resolver =
      ZC_REQUIRE_NONNULL(diagnostics::CompilationDiagnosticProvenanceResolver::from(sealed));
  auto materialized = diagnostics::materializeDiagnosticFacts(sealed, resolver);
  ZC_REQUIRE(materialized.is<diagnostics::ResolvedDiagnosticBatch>());
  const auto resolved = materialized.get<diagnostics::ResolvedDiagnosticBatch>().diagnostics();
  ZC_REQUIRE(resolved.size() == 1);
  ZC_EXPECT(resolved[0].code() == diagnostics::DiagID::PackageManifestInvalid);
  ZC_EXPECT(resolved[0].primary().origin() == diagnostics::DiagnosticFactOrigin::Document);
  ZC_EXPECT(resolved[0].primary().documentIdentityBytes() == documents[1].key().encode().asPtr());
  ZC_REQUIRE(resolved[0].related().size() == 2);
  ZC_EXPECT(resolved[0].related()[0].role == diagnostics::DiagnosticSecondaryRole::Highlight);
  const diagnostics::DiagnosticSourceRange expectedHighlight{0, 1, true};
  ZC_EXPECT(resolved[0].related()[0].location.range() == expectedHighlight);
  ZC_EXPECT(resolved[0].related()[1].code == diagnostics::DiagID::PreviousWorkspacePackageHere);
  ZC_EXPECT(resolved[0].related()[1].location.documentIdentityBytes() ==
            documents[0].key().encode().asPtr());
}

ZC_TEST("PackageDiagnosticTest.ProjectsTypedToolchainRootAndRejectsWrongDocuments") {
  const auto source = R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[[bin]]
name = "core"
path = "src/bin/core.zom"
)toml"_zc;
  auto manifest = reservationManifest(source);
  ZC_REQUIRE(manifest.binaries().size() == 1);
  auto failure = PackageToolchainModuleRootFailure::userTargetRoot(manifest.binaries()[0],
                                                                   reservationPackageKey());
  ZC_REQUIRE(failure != zc::none);

  zc::Vector<PackageDiagnosticDocument> documents;
  documents.add(document("Zom.toml"_zc, source.asBytes()));
  ZC_IF_SOME(value, failure) {
    auto projected = projectToolchainModuleRootFailure(documents.asPtr(), value);
    ZC_REQUIRE(projected.is<PackageDiagnosticFactProjection>());
    const auto& projection = projected.get<PackageDiagnosticFactProjection>();
    ZC_REQUIRE(projection.facts.size() == 1);
    ZC_EXPECT(projection.facts[0].code() == diagnostics::DiagID::ToolchainModuleRootReserved);
    ZC_REQUIRE(projection.facts[0].arguments().size() == 1);
    ZC_EXPECT(projection.facts[0].arguments()[0] == "core"_zc);
    ZC_REQUIRE(projection.facts[0].secondary().size() == 1);
    ZC_EXPECT(projection.facts[0].secondary()[0].role() ==
              diagnostics::DiagnosticSecondaryRole::Highlight);

    zc::Vector<PackageDiagnosticDocument> noDocuments;
    auto missing = projectToolchainModuleRootFailure(noDocuments.asPtr(), value);
    ZC_REQUIRE(missing.is<PackageDiagnosticProjectionFailure>());
    ZC_EXPECT(missing.get<PackageDiagnosticProjectionFailure>() ==
              PackageDiagnosticProjectionFailure::MissingDocument);
    zc::Vector<PackageDiagnosticDocument> wrongDocuments;
    wrongDocuments.add(document("other.toml"_zc, source.asBytes()));
    auto wrong = projectToolchainModuleRootFailure(wrongDocuments.asPtr(), value);
    ZC_REQUIRE(wrong.is<PackageDiagnosticProjectionFailure>());
    ZC_EXPECT(wrong.get<PackageDiagnosticProjectionFailure>() ==
              PackageDiagnosticProjectionFailure::MissingDocument);
  }
}

ZC_TEST("PackageDiagnosticTest.RejectsManifestSpanBeyondAdmittedDocument") {
  const auto source = "x"_zc.asBytes();
  zc::Vector<PackageDiagnosticDocument> documents;
  documents.add(document("Zom.toml"_zc, source));
  auto provenance = DiagnosticProvenance::from(forgedAnchor(documents[0], 2, 0, 2),
                                               zc::Vector<DiagnosticAnchor>());
  ZC_REQUIRE(provenance != zc::none);
  auto manifestFailure =
      ManifestFailure::invalid(zc::mv(ZC_ASSERT_NONNULL(provenance)), ManifestIssue::TomlSyntax);
  auto projected = projectManifestFailure(documents.asPtr(), manifestFailure);
  ZC_REQUIRE(projected.is<PackageDiagnosticProjectionFailure>());
  ZC_EXPECT(projected.get<PackageDiagnosticProjectionFailure>() ==
            PackageDiagnosticProjectionFailure::MissingDocument);
}

ZC_TEST("PackageDiagnosticTest.ClassifiesBuildScriptIssuesAsOperationalTokens") {
  for (uint8_t value = static_cast<uint8_t>(BuildScriptIssue::SandboxUnavailable);
       value <= static_cast<uint8_t>(BuildScriptIssue::BuildResultIntegrityViolation); ++value) {
    const auto issue = static_cast<BuildScriptIssue>(value);
    ZC_EXPECT(buildScriptIssueDisplay(issue).size() != 0);
  }
}

ZC_TEST("PackageDiagnosticTest.ClassifiesMaterializationIssuesAsOperationalTokens") {
  for (uint8_t value = static_cast<uint8_t>(MaterializationIssue::UnsupportedArchiveFormat);
       value <= static_cast<uint8_t>(MaterializationIssue::SnapshotCleanupFailed); ++value) {
    const auto issue = static_cast<MaterializationIssue>(value);
    ZC_EXPECT(materializationIssueDisplay(issue).size() != 0);
  }
}

}  // namespace zomlang::compiler::driver::package
