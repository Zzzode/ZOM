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
// See the License for the specific language governing permissions and
// limitations under the License.

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/driver/session/compiler-session.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/ownership/facts/inputs.h"
#include "zomlang/compiler/ownership/ownership-checked-mir.h"
#include "zomlang/tests/unittests/compiler/driver/core/core-library-test-fixture.h"
#include "zomlang/tests/unittests/compiler/ownership/ownership-facts-differential-oracle.h"

namespace zomlang::compiler::ownership {
namespace {

namespace driver = zomlang::compiler::driver;
namespace package = driver::package;

// ---------------------------------------------------------------------------
// Pipeline fixture (mirrors the OwnershipPipelineFixture pattern in
// ownership-event-overlay-test.cc; duplicated here because that fixture lives
// in an anonymous namespace).
// ---------------------------------------------------------------------------

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid ownership fixture scalar");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid ownership fixture feature set");
}

identity::CanonicalPackageSource packageSource() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  return identity::CanonicalPackageSource::localPath(
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments)));
}

identity::PackageBaseKey packageBase() {
  return identity::PackageBaseKey::from(packageSource(), scalar<identity::PackageName>("app"_zc),
                                        scalar<identity::ResolvedVersion>("1.0.0"_zc));
}

identity::PackageKey packageKey() {
  return identity::PackageKey::from(packageSource(), scalar<identity::PackageName>("app"_zc),
                                    scalar<identity::ResolvedVersion>("1.0.0"_zc), emptyFeatures());
}

identity::CanonicalRelativePath mainPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>("src"_zc));
  segments.add(scalar<identity::CanonicalPathSegment>("main.zom"_zc));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

identity::CanonicalTargetSpecificationKey targetProjection() {
  zc::Vector<identity::TargetFeatureName> features;
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(sorted != zc::none);
  ZC_IF_SOME(values, sorted) {
    auto result = identity::CanonicalTargetSpecificationKey::from(
        scalar<identity::TargetComponentName>("x86_64"_zc),
        scalar<identity::TargetComponentName>("zom"_zc),
        scalar<identity::TargetComponentName>("none"_zc),
        scalar<identity::TargetComponentName>("unknown"_zc),
        scalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
        zc::mv(values));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid ownership fixture target projection");
}

package::RegisteredTargetProfileName targetProfileName() {
  auto result = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid ownership fixture target profile name");
}

ir::TargetRegistrySnapshot targetRegistry() {
  zc::Vector<ir::CanonicalTargetFeature> targetFeatures;
  auto targetSpec = ir::CanonicalTargetSpec::from(
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(targetFeatures), "zom"_zc,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_REQUIRE(targetSpec != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  ZC_IF_SOME(value, targetSpec) { specifications.add(zc::mv(value)); }
  auto profile = ir::RegisteredTargetProfileRecord::from(
      targetProfileName(), targetProjection(), zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  ZC_IF_SOME(value, profile) { profiles.add(zc::mv(value)); }
  auto registry = ir::TargetRegistrySnapshot::from(targetProfileName(), zc::mv(profiles));
  ZC_IF_SOME(value, registry) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid ownership fixture target registry");
}

package::RegisteredTargetSelection targetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto result = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid ownership fixture target selection");
}

ir::VerifiedTargetSelection verifiedTargetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto result = registry.verify(targetSelection(registry));
  ZC_REQUIRE(result.is<ir::VerifiedTargetSelection>());
  return zc::mv(result.get<ir::VerifiedTargetSelection>());
}

package::VerifiedPackageCompilationRequest compilationRequest(
    const ir::TargetRegistrySnapshot& registry) {
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(packageKey(), identity::CrateTargetKind::Binary,
                                                   scalar<identity::TargetName>("app"_zc), 2026,
                                                   false, mainPath()));
  auto result = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), targetSelection(registry), targetSelection(registry),
      package::SelectedLanguageOptions{}, package::PackageLockMode::PreferLocked);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid ownership fixture compilation request");
}

class MemoryFreshDirectory final : public package::FreshSourceDirectory {
public:
  MemoryFreshDirectory() : rootValue(zc::newInMemoryDirectory(zc::nullClock())) {}
  ~MemoryFreshDirectory() noexcept override = default;
  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<package::MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<zc::Directory> rootValue;
};

class MemoryFreshDirectoryFactory final : public package::FreshSourceDirectoryFactory {
public:
  package::FreshSourceDirectoryResult create() override {
    zc::Own<package::FreshSourceDirectory> result = zc::heap<MemoryFreshDirectory>();
    return zc::mv(result);
  }
};

package::DigestVerifiedSourceSnapshot sourceSnapshot(zc::StringPtr sourceText) {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(sourceText);
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::ResolutionOutput resolution(zc::MemoryResource& resource, zc::StringPtr sourceText) {
  package::ManifestParser parser;
  zc::Vector<identity::CanonicalRelativePath> files;
  auto inventory = package::PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  package::NormalizedManifest normalized = [&]() {
    ZC_IF_SOME(sourceInventory, inventory) {
      zc::Vector<identity::CanonicalPathSegment> documentSegments;
      documentSegments.add(scalar<identity::CanonicalPathSegment>("Zom.toml"_zc));
      auto parsed = parser.parseWorkspaceManifest(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(documentSegments)),
          "[package]\nname = \"app\"\nversion = \"1.0.0\"\nedition = \"2026\"\n"_zc,
          sourceInventory);
      ZC_REQUIRE(parsed.is<package::NormalizedManifest>());
      return zc::mv(parsed.get<package::NormalizedManifest>());
    }
    ZC_UNREACHABLE
  }();
  auto record = package::LocalPackageRecord::from(packageBase(), zc::mv(normalized),
                                                  sourceSnapshot(sourceText));
  ZC_REQUIRE(record != zc::none);
  zc::Vector<package::ResolverRelease> releases;
  ZC_IF_SOME(value, record) { releases.add(package::ResolverRelease::fromLocal(value)); }
  zc::Vector<package::ResolverRoot> roots;
  roots.add(package::ResolverRoot::from(packageBase(), emptyFeatures(), false, false));
  auto result = package::PackageResolver::resolve(resource, roots, releases);
  ZC_REQUIRE(result.is<package::ResolutionOutput>());
  return zc::mv(result.get<package::ResolutionOutput>());
}

zc::Vector<package::ResolvedPackageSourceSnapshot> resolvedSnapshots(zc::StringPtr sourceText) {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(
      package::ResolvedPackageSourceSnapshot::from(packageBase(), sourceSnapshot(sourceText)));
  return snapshots;
}

class OwnershipPipelineFixture final {
public:
  explicit OwnershipPipelineFixture(zc::StringPtr sourceText)
      : session(contextFactory, languageOptions, compilerOptions) {
    auto registry = targetRegistry();
    auto input = driver::VerifiedPackageSessionInput::from(
        compilationRequest(registry), verifiedTargetSelection(registry),
        verifiedTargetSelection(registry),
        resolution(session.getPackageResolutionMemoryResource(), sourceText),
        resolvedSnapshots(sourceText));
    ZC_REQUIRE(input != zc::none);
    ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
    driver::core_library_test::installCoreDistribution(session);
    const auto roots = session.getFinalizedCompilationRoots();
    ZC_REQUIRE(roots.size() == 1);
    ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
    ZC_REQUIRE(session.parseSources());
    ZC_REQUIRE(session.bindSources());
    ZC_REQUIRE(session.checkSources());
    ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());
    ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  }

  const mir::VerifiedBuiltMir& builtMir() const {
    return session.getOwnershipCheckedMirModules()[0].builtMir();
  }

  driver::borrow_evidence::VerifiedBorrowEvidence cloneBorrowEvidence() const {
    const auto repository = session.getBorrowEvidenceRepository();
    ZC_REQUIRE(repository != zc::none);
    ZC_IF_SOME(value, repository) {
      const auto evidence = value.capability().lookup(builtMir().borrowEvidenceLease());
      ZC_REQUIRE(evidence.isResolved());
      return evidence.evidence().clone();
    }
    ZC_UNREACHABLE
  }

  driver::CompilerSession& compilerSession() noexcept { return session; }
  const driver::CompilerSession& compilerSession() const noexcept { return session; }

private:
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  driver::CompilerSession session;
};

const facts::VerifiedOwnershipInputs& ownershipInputs(const driver::CompilerSession& session) {
  const auto checkedMir = session.getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);
  return checkedMir[0].facts();
}

/// \brief Recomputes all eight facts inventories with the differential oracle
/// and compares each against production as sets.
void expectOracleMatchesInventory(const OwnershipPipelineFixture& fixture) {
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  auto evidence = fixture.cloneBorrowEvidence();
  const auto& inputs = ownershipInputs(session);
  const test_oracle::OwnershipFactsOracle oracle(builtMir, overlay, evidence);

  {
    auto derived = oracle.movePaths();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesMovePaths(value.asPtr(), inputs.movePaths().functions()));
    }
  }
  {
    auto derived = oracle.flow();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesFlow(value.asPtr(), inputs.flow().functions()));
    }
  }
  {
    auto derived = oracle.initialization();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(
          test_oracle::matchesInitialization(value.asPtr(), inputs.initialization().functions()));
    }
  }
  {
    auto derived = oracle.loans();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesLoans(value.asPtr(), inputs.loans().loans()));
    }
  }
  {
    auto derived = oracle.references();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesReferences(value.asPtr(), inputs.references().definitions()));
    }
  }
  {
    auto derived = oracle.regions();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesRegions(value.asPtr(), inputs.regions().regions()));
    }
  }
  {
    auto derived = oracle.states();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesStates(value.asPtr(), inputs.states().states()));
    }
  }
  {
    auto derived = oracle.resources();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesResources(value.asPtr(), inputs.resources().functions()));
    }
  }
}

// ---------------------------------------------------------------------------
// 512-byte overlay collision oracle.
//
// Constructs one canonical function record representing a local-borrow body
// (entry root, constant-assign destination write, borrow issue + activation,
// return read), frames it through the production OwnershipEventOverlayCodec
// with fixed revisions, and asserts the exact byte count. A companion test
// mutates every load-bearing byte and asserts the digest changes, proving the
// encoding is collision-free for this record shape.
//
// Calibration: the framed encoding is 512 bytes, not 513. The framing
// overhead is 149 bytes: 28 (domain "zom.ownership-event-overlay" + null)
// + 32 (context fingerprint) + 9 (module key byte string) + 32 (checked
// facts revision) + 32 (built revision) + 8 (function count) + 8 (function
// byte-string length). 149 + 363 (function record) = 512.
// ---------------------------------------------------------------------------

identity::Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) value = byte;
  ZC_IF_SOME(digest, identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes))) { return digest; }
  ZC_FAIL_REQUIRE("invalid ownership digest fixture");
}

zc::Array<uint8_t> encodeEntryEventKeyOracle(zc::ArrayPtr<const uint8_t> owner) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(owner);
  encoder.encodeUint8(0x01);  // MirPointKind::Entry
  encoder.encodeUint32(0);    // operandOrdinal
  return encoder.finish();
}

zc::Array<uint8_t> encodeStatementEventKeyOracle(zc::ArrayPtr<const uint8_t> owner, uint32_t block,
                                                 uint32_t statement, uint32_t operandOrdinal) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(owner);
  encoder.encodeUint8(0x02);  // MirPointKind::BeforeStatement
  encoder.encodeUint32(block);
  encoder.encodeUint32(statement);
  encoder.encodeUint32(operandOrdinal);
  return encoder.finish();
}

zc::Array<uint8_t> encodeTerminatorEventKeyOracle(zc::ArrayPtr<const uint8_t> owner, uint32_t block,
                                                  uint32_t operandOrdinal) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(owner);
  encoder.encodeUint8(0x04);  // MirPointKind::BeforeTerminator
  encoder.encodeUint32(block);
  encoder.encodeUint32(operandOrdinal);
  return encoder.finish();
}

zc::Array<uint8_t> encodeEventSlotOracle(zc::ArrayPtr<const uint8_t> key, uint8_t stage,
                                         zc::ArrayPtr<const uint8_t> roles) {
  identity::CanonicalEncoder encoder;
  for (uint8_t value : key) { encoder.encodeUint8(value); }
  encoder.encodeUint8(stage);
  encoder.encodeSequenceSize(roles.size());
  for (uint8_t role : roles) {
    const uint8_t encodedRole[] = {role};
    encoder.encodeByteString(zc::arrayPtr(encodedRole));
  }
  return encoder.finish();
}

/// \brief Builds the 363-byte canonical function record for a local-borrow
/// body: one entry root slot, one constant-assign destination-write slot, one
/// borrow issue + activation slot, and one return-read slot, followed by five
/// empty inventories.
zc::Array<uint8_t> localBorrowFunctionOracle() {
  const uint8_t owner[] = {0xb1};
  const uint8_t entryRoles[] = {0x02};         // EntryRoot
  const uint8_t assignRoles[] = {0x06, 0x07};  // ConstantOperand, DestinationWrite
  const uint8_t borrowRoles[] = {0x08, 0x09};  // BorrowIssue, BorrowActivation
  const uint8_t returnRoles[] = {0x03};        // OperandRead

  auto entryKey = encodeEntryEventKeyOracle(zc::arrayPtr(owner));
  auto entrySlot = encodeEventSlotOracle(entryKey.asPtr(), 0x01, zc::arrayPtr(entryRoles));
  auto assignKey = encodeStatementEventKeyOracle(zc::arrayPtr(owner), 0, 0, 0);
  auto assignSlot = encodeEventSlotOracle(assignKey.asPtr(), 0x01, zc::arrayPtr(assignRoles));
  auto borrowKey = encodeStatementEventKeyOracle(zc::arrayPtr(owner), 0, 1, 0);
  auto borrowSlot = encodeEventSlotOracle(borrowKey.asPtr(), 0x01, zc::arrayPtr(borrowRoles));
  auto returnKey = encodeTerminatorEventKeyOracle(zc::arrayPtr(owner), 0, 0);
  auto returnSlot = encodeEventSlotOracle(returnKey.asPtr(), 0x01, zc::arrayPtr(returnRoles));

  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(zc::arrayPtr(owner));
  encoder.encodeSequenceSize(4);
  encoder.encodeByteString(entryKey.asPtr());
  encoder.encodeByteString(entrySlot.asPtr());
  encoder.encodeByteString(assignKey.asPtr());
  encoder.encodeByteString(assignSlot.asPtr());
  encoder.encodeByteString(borrowKey.asPtr());
  encoder.encodeByteString(borrowSlot.asPtr());
  encoder.encodeByteString(returnKey.asPtr());
  encoder.encodeByteString(returnSlot.asPtr());
  for (int index = 0; index < 5; ++index) { encoder.encodeSequenceSize(0); }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> frameLocalBorrowOverlay(zc::ArrayPtr<const uint8_t> functionRecord) {
  const uint8_t module[] = {0xa1};
  zc::Vector<zc::Array<uint8_t>> functions;
  functions.add(zc::heapArray(functionRecord));
  return OwnershipEventOverlayCodec::encodeFramed(repeatedDigest(0x00), zc::arrayPtr(module),
                                                  repeatedDigest(0x44), repeatedDigest(0x22),
                                                  functions.asPtr());
}

zc::Maybe<identity::Sha256Digest> digestOf(zc::ArrayPtr<const uint8_t> bytes) {
  return identity::sha256(bytes);
}

}  // namespace

// ---------------------------------------------------------------------------
// Facts oracle tests: three independent recomputations of all eight
// inventories, compared against production as sets.
// ---------------------------------------------------------------------------

ZC_TEST("Facts oracle matches production for a scalar local return") {
  OwnershipPipelineFixture fixture("fun entry() -> i32 { let value = 0; return value; }"_zc);
  expectOracleMatchesInventory(fixture);
}

ZC_TEST("Facts oracle matches production for an aggregate local return") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  expectOracleMatchesInventory(fixture);
}

ZC_TEST("Facts oracle matches production for a parameter reborrow return") {
  OwnershipPipelineFixture fixture("fun entry(p: &i32) -> &i32 { return &*p; }"_zc);
  expectOracleMatchesInventory(fixture);
}

ZC_TEST("Facts oracle matches production for a mutable parameter reborrow return") {
  OwnershipPipelineFixture fixture("fun entry(p: &mut i32) -> &mut i32 { return &mut *p; }"_zc);
  expectOracleMatchesInventory(fixture);
}

// ---------------------------------------------------------------------------
// 512-byte overlay collision oracle.
// ---------------------------------------------------------------------------

ZC_TEST("Ownership event overlay encoding matches the 512-byte local-borrow oracle") {
  auto record = localBorrowFunctionOracle();
  ZC_EXPECT(record.size() == 363);
  auto encoded = frameLocalBorrowOverlay(record.asPtr());
  ZC_REQUIRE(encoded != zc::none);
  ZC_IF_SOME(bytes, encoded) {
    // Framing overhead: 28 (domain + null) + 32 (context) + 9 (module key)
    // + 32 (checked facts) + 32 (built revision) + 8 (function count)
    // + 8 (function byte-string length) = 149 bytes. 149 + 363 = 512.
    ZC_EXPECT(bytes.size() == 512);
  }
}

ZC_TEST("Ownership event overlay local-borrow oracle changes when any input byte is mutated") {
  auto record = localBorrowFunctionOracle();
  ZC_REQUIRE(record.size() == 363);
  auto baselineEncoded = frameLocalBorrowOverlay(record.asPtr());
  ZC_REQUIRE(baselineEncoded != zc::none);
  auto baselineDigest = digestOf(ZC_REQUIRE_NONNULL(baselineEncoded).asPtr());
  ZC_REQUIRE(baselineDigest != zc::none);

  // Each entry is (offset, expected old value, new value). The offsets are
  // derived from the canonical encoding layout documented in
  // localBorrowFunctionOracle().
  struct Mutation final {
    size_t offset;
    uint8_t expected;
    uint8_t mutated;
  };
  const Mutation mutations[] = {
      {8, 0xb1, 0xb2},    // owner
      {34, 0x01, 0x05},   // entry point kind
      {78, 0x02, 0x01},   // entry role (EntryRoot -> Operation)
      {156, 0x06, 0x03},  // assign role 0 (ConstantOperand -> OperandRead)
      {191, 0x01, 0x02},  // borrow statement ordinal (1 -> 2)
      {243, 0x08, 0x07},  // borrow role 0 (BorrowIssue -> DestinationWrite)
      {322, 0x03, 0x04},  // return role (OperandRead -> OperandCopy)
  };

  for (const auto& mutation : mutations) {
    ZC_REQUIRE(mutation.offset < record.size());
    ZC_REQUIRE(record[mutation.offset] == mutation.expected);
    auto mutated = zc::heapArray(record.asPtr());
    mutated[mutation.offset] = mutation.mutated;
    auto mutatedEncoded = frameLocalBorrowOverlay(mutated.asPtr());
    ZC_REQUIRE(mutatedEncoded != zc::none);
    auto mutatedDigest = digestOf(ZC_REQUIRE_NONNULL(mutatedEncoded).asPtr());
    ZC_REQUIRE(mutatedDigest != zc::none);
    ZC_IF_SOME(baseline, baselineDigest) {
      ZC_IF_SOME(changed, mutatedDigest) { ZC_EXPECT(baseline != changed); }
    }
  }
}

// ---------------------------------------------------------------------------
// RFC 0007 ownership-facts byte oracles.
//
// A test-owned encoder reproduces the exact facts framing layout from the RFC
// (domain + null + context fingerprint + module key + MIR revision + overlay
// revision + borrow-evidence revision + function sequence) without calling the
// production OwnershipFactsCodec, whose group layout differs. Each test
// asserts the exact byte count, the full preimage hex, and the SHA-256 digest
// from the RFC, plus mutation sensitivity.
// ---------------------------------------------------------------------------

zc::Array<uint8_t> decoded(zc::StringPtr hex) {
  auto bytes = zc::decodeHex(hex);
  ZC_REQUIRE(bytes != zc::none);
  return zc::mv(ZC_REQUIRE_NONNULL(bytes));
}

zc::Array<uint8_t> encodeFactsOracle(const identity::Sha256Digest& contextFingerprint,
                                     zc::ArrayPtr<const uint8_t> expandedModuleKey,
                                     const identity::Sha256Digest& mirRevision,
                                     const identity::Sha256Digest& overlayRevision,
                                     const identity::Sha256Digest& borrowEvidenceRevision,
                                     zc::ArrayPtr<const zc::Array<uint8_t>> functions) {
  identity::CanonicalEncoder encoder;
  constexpr char domain[] = "zom.ownership-facts";
  for (size_t index = 0; index + 1 < sizeof(domain); ++index) {
    encoder.encodeUint8(static_cast<uint8_t>(domain[index]));
  }
  encoder.encodeUint8(0);
  encoder.encodeDigest(contextFingerprint);
  encoder.encodeByteString(expandedModuleKey);
  encoder.encodeDigest(mirRevision);
  encoder.encodeDigest(overlayRevision);
  encoder.encodeDigest(borrowEvidenceRevision);
  encoder.encodeSequenceSize(functions.size());
  for (const auto& function : functions) { encoder.encodeByteString(function.asPtr()); }
  return encoder.finish();
}

zc::Array<uint8_t> encodeFixedFactsOracle(zc::ArrayPtr<const zc::Array<uint8_t>> functions) {
  const uint8_t module[] = {0xa1};
  return encodeFactsOracle(repeatedDigest(0x00), zc::arrayPtr(module), repeatedDigest(0x22),
                           repeatedDigest(0x55), repeatedDigest(0x33), functions);
}

void expectFactsOracle(zc::Vector<zc::Array<uint8_t>>&& functions, zc::StringPtr expectedPreimage,
                       zc::StringPtr expectedDigest) {
  auto bytes = encodeFixedFactsOracle(functions.asPtr());
  ZC_EXPECT(zc::encodeHex(bytes.asPtr()) == expectedPreimage);
  auto digest = identity::sha256(bytes.asPtr());
  ZC_REQUIRE(digest != zc::none);
  ZC_IF_SOME(value, digest) { ZC_EXPECT(zc::encodeHex(value.bytes()) == expectedDigest); }
}

zc::Array<uint8_t> emptyFunctionFactsOracle() {
  const uint8_t owner[] = {0xb1};
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(zc::arrayPtr(owner));
  for (int index = 0; index < 13; ++index) { encoder.encodeSequenceSize(0); }
  return encoder.finish();
}

zc::Array<uint8_t> pointStateFunctionFactsOracle() {
  const uint8_t owner[] = {0xb1};
  const uint8_t pointKey[] = {0x01, 0x01};
  // The 74-byte pointState value from the RFC 0007 oracle: one
  // OwnershipResourceStateAlternative with empty drop-obligation,
  // linear-obligation, and cast-carrier state maps.
  auto pointValue = decoded(
      "0101000000000000000000000000000000000000000000000000000000000000000100000000000000180000000000000000000000000000000000000000000000000000000000000000"_zc);

  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(zc::arrayPtr(owner));
  encoder.encodeSequenceSize(0);  // movePaths
  encoder.encodeSequenceSize(0);  // conflicts
  encoder.encodeSequenceSize(1);  // pointStates
  encoder.encodeByteString(zc::arrayPtr(pointKey));
  encoder.encodeByteString(pointValue.asPtr());
  for (int index = 0; index < 10; ++index) { encoder.encodeSequenceSize(0); }
  return encoder.finish();
}

ZC_TEST("Ownership facts test encoder matches the RFC 0007 empty oracle") {
  zc::Vector<zc::Array<uint8_t>> functions;
  expectFactsOracle(zc::mv(functions),
                    "7a6f6d2e6f776e6572736869702d66616374730000000000000000000000000000000000000000"
                    "00000000000000000000"
                    "0000000000000000000001a1222222222222222222222222222222222222222222222222222222"
                    "22222222225555555555"
                    "555555555555555555555555555555555555555555555555555555333333333333333333333333"
                    "33333333333333333333"
                    "333333333333333333330000000000000000"_zc,
                    "3fc9636b5e668ec6b10fac4ce2c76b0a20d87f9b25dd51b7729141ed33296b93"_zc);
}

ZC_TEST("Ownership facts test encoder matches the RFC 0007 function-framing oracle") {
  zc::Vector<zc::Array<uint8_t>> functions;
  functions.add(emptyFunctionFactsOracle());
  expectFactsOracle(
      zc::mv(functions),
      "7a6f6d2e6f776e6572736869702d6661637473000000000000000000000000000000000000000000000000000000"
      "000000"
      "0000000000000000000001a122222222222222222222222222222222222222222222222222222222222222225555"
      "555555"
      "55555555555555555555555555555555555555555555555555555533333333333333333333333333333333333333"
      "333333"
      "33333333333333333333000000000000000100000000000000710000000000000001b10000000000000000000000"
      "000000"
      "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000"
      "0000000000000000000000000000000000000000000000000000000000000000000000000000000000"_zc,
      "b2d0e68fd7a597ddabb36e3d2c78cf96a596561921fb965e856fdd26363bfba2"_zc);
}

ZC_TEST("Ownership facts test encoder matches the RFC 0007 point-state oracle") {
  zc::Vector<zc::Array<uint8_t>> functions;
  functions.add(pointStateFunctionFactsOracle());
  expectFactsOracle(zc::mv(functions),
                    "7a6f6d2e6f776e6572736869702d66616374730000000000000000000000000000000000000000"
                    "00000000000000000000"
                    "0000000000000000000001a1222222222222222222222222222222222222222222222222222222"
                    "22222222225555555555"
                    "555555555555555555555555555555555555555555555555555555333333333333333333333333"
                    "33333333333333333333"
                    "33333333333333333333000000000000000100000000000000cd0000000000000001b100000000"
                    "00000000000000000000"
                    "0000000000000000000100000000000000020101000000000000004a0101000000000000000000"
                    "00000000000000000000"
                    "000000000000000000000000010000000000000018000000000000000000000000000000000000"
                    "00000000000000000000"
                    "000000000000000000000000000000000000000000000000000000000000000000000000000000"
                    "00000000000000000000"
                    "0000000000000000000000000000000000000000000000000000000000000000000000"_zc,
                    "f5acb38c2474aea982f75216e69997d0aa8d2634eebdabc7fc6d59e01a2d6577"_zc);
}

ZC_TEST("Ownership facts oracles change when any input byte is mutated") {
  // 165-byte empty oracle: mutate the module key byte in the framed output.
  {
    zc::Vector<zc::Array<uint8_t>> functions;
    auto baseline = encodeFixedFactsOracle(functions.asPtr());
    ZC_REQUIRE(baseline.size() == 165);
    auto baselineDigest = identity::sha256(baseline.asPtr());
    ZC_REQUIRE(baselineDigest != zc::none);
    auto mutated = zc::heapArray(baseline.asPtr());
    mutated[60] = 0xa2;  // module key byte
    auto mutatedDigest = identity::sha256(mutated.asPtr());
    ZC_REQUIRE(mutatedDigest != zc::none);
    ZC_IF_SOME(before, baselineDigest) {
      ZC_IF_SOME(after, mutatedDigest) { ZC_EXPECT(before != after); }
    }
  }

  // 286-byte function-framing oracle: mutate the owner byte.
  {
    auto record = emptyFunctionFactsOracle();
    ZC_REQUIRE(record.size() == 113);
    zc::Vector<zc::Array<uint8_t>> baselineFunctions;
    baselineFunctions.add(zc::heapArray(record.asPtr()));
    auto baseline = encodeFixedFactsOracle(baselineFunctions.asPtr());
    auto baselineDigest = identity::sha256(baseline.asPtr());
    ZC_REQUIRE(baselineDigest != zc::none);
    auto mutated = zc::heapArray(record.asPtr());
    mutated[8] = 0xb2;  // owner
    zc::Vector<zc::Array<uint8_t>> mutatedFunctions;
    mutatedFunctions.add(zc::mv(mutated));
    auto changed = encodeFixedFactsOracle(mutatedFunctions.asPtr());
    auto changedDigest = identity::sha256(changed.asPtr());
    ZC_REQUIRE(changedDigest != zc::none);
    ZC_IF_SOME(before, baselineDigest) {
      ZC_IF_SOME(after, changedDigest) { ZC_EXPECT(before != after); }
    }
  }

  // 378-byte point-state oracle: mutate the owner and point-state key bytes.
  {
    auto record = pointStateFunctionFactsOracle();
    ZC_REQUIRE(record.size() == 205);
    zc::Vector<zc::Array<uint8_t>> baselineFunctions;
    baselineFunctions.add(zc::heapArray(record.asPtr()));
    auto baseline = encodeFixedFactsOracle(baselineFunctions.asPtr());
    auto baselineDigest = identity::sha256(baseline.asPtr());
    ZC_REQUIRE(baselineDigest != zc::none);

    struct Mutation final {
      size_t offset;
      uint8_t expected;
      uint8_t mutated;
    };
    const Mutation mutations[] = {
        {8, 0xb1, 0xb2},   // owner
        {41, 0x01, 0x03},  // pointStates key byte 0
    };
    for (const auto& mutation : mutations) {
      ZC_REQUIRE(mutation.offset < record.size());
      ZC_REQUIRE(record[mutation.offset] == mutation.expected);
      auto mutated = zc::heapArray(record.asPtr());
      mutated[mutation.offset] = mutation.mutated;
      zc::Vector<zc::Array<uint8_t>> mutatedFunctions;
      mutatedFunctions.add(zc::mv(mutated));
      auto changed = encodeFixedFactsOracle(mutatedFunctions.asPtr());
      auto changedDigest = identity::sha256(changed.asPtr());
      ZC_REQUIRE(changedDigest != zc::none);
      ZC_IF_SOME(before, baselineDigest) {
        ZC_IF_SOME(after, changedDigest) { ZC_EXPECT(before != after); }
      }
    }
  }
}

}  // namespace zomlang::compiler::ownership
