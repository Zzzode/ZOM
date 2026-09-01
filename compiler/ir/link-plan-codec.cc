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

#include "compiler/ir/link-plan-codec.h"

#include "compiler/identity/identity-invariant.h"
#include "compiler/identity/semantic/context-fingerprint.h"
#include "compiler/identity/source-snapshot.h"

namespace zomlang::compiler::ir {
namespace {

// ---------------------------------------------------------------------------
// Canonical framing helpers (shared discipline with the LIR algebra codec):
// Frame = big-endian uint64 byte length followed by the exact bytes.
// ---------------------------------------------------------------------------

void appendUint8(zc::Vector<uint8_t>& output, uint8_t value) { output.add(value); }

void appendUint32(zc::Vector<uint8_t>& output, uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    output.add(static_cast<uint8_t>(value >> static_cast<uint32_t>(shift)));
  }
}

void appendUint64(zc::Vector<uint8_t>& output, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    output.add(static_cast<uint8_t>(value >> static_cast<uint32_t>(shift)));
  }
}

void appendBytes(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> value) {
  output.addAll(value);
}

void appendFramed(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> value) {
  appendUint64(output, value.size());
  appendBytes(output, value);
}

void appendFramed(zc::Vector<uint8_t>& output, zc::StringPtr value) {
  appendUint64(output, value.size());
  for (const auto byte : value) { output.add(static_cast<uint8_t>(byte)); }
}

// A LinkInputRecord frame: Frame(path) uint8(role) Frame(digest) uint64(byteCount).
void appendInputRecord(zc::Vector<uint8_t>& output, const LinkInputRecord& record) {
  appendFramed(output, record.path());
  appendUint8(output, static_cast<uint8_t>(record.role()));
  appendFramed(output, record.digest().bytes());
  appendUint64(output, record.byteCount());
}

void appendInputSequence(zc::Vector<uint8_t>& output, zc::ArrayPtr<const LinkInputRecord> records) {
  appendUint64(output, records.size());
  for (const auto& record : records) { appendInputRecord(output, record); }
}

identity::Sha256Digest requireDigest(zc::ArrayPtr<const uint8_t> bytes) {
  auto digest = identity::sha256(bytes);
  ZC_IF_SOME(value, digest) { return value; }
  ZC_UNREACHABLE
}

// A normalized path in this slice is a non-empty absolute POSIX path with no
// unnormalized "." or ".." segment and no empty interior segment. This is a
// pure-value normalization check; RFC 0043's live path service performs the
// filesystem-bound normalization in a later slice.
bool isNormalizedAbsolutePath(zc::StringPtr path) {
  if (path.size() == 0 || path[0] != '/') { return false; }
  size_t segmentStart = 1;
  for (size_t index = 1; index <= path.size(); ++index) {
    const bool atEnd = index == path.size();
    if (!atEnd && path[index] != '/') { continue; }
    const size_t length = index - segmentStart;
    // Reject an empty segment (a trailing slash or "//"), "." and "..".
    if (length == 0) { return false; }
    if (length == 1 && path[segmentStart] == '.') { return false; }
    if (length == 2 && path[segmentStart] == '.' && path[segmentStart + 1] == '.') { return false; }
    segmentStart = index + 1;
  }
  return true;
}

// True when `path` equals `root` or is a descendant of `root` (a normalized
// same-tree containment check on already-normalized absolute paths).
bool isInsideRoot(zc::StringPtr root, zc::StringPtr path) {
  if (root.size() == 0 || path.size() < root.size()) { return false; }
  for (size_t index = 0; index < root.size(); ++index) {
    if (path[index] != root[index]) { return false; }
  }
  if (path.size() == root.size()) { return true; }
  // A descendant must continue with a separator, unless root already ends in one.
  const bool rootEndsWithSlash = root[root.size() - 1] == '/';
  return rootEndsWithSlash || path[root.size()] == '/';
}

// The session context bound to a link-plan-construction rejection. The link
// phases are session-owned (RFC 0043 failure table), so the fact carries the
// session fingerprint and never needs module/definition identity expansion.
identity::ContextFingerprint linkPlanSessionContext() {
  auto digest = requireDigest("zom.link-plan"_zc.asBytes());
  return identity::ContextFingerprint::fromCanonicalDigest(digest);
}

// A resolver that expands nothing: a session-owned fact is admitted without any
// module/definition/instance expansion, so this is never invoked.
class UnusedIdentityResolver final : public IrFailureIdentityResolver {
public:
  ExpandedIrIdentityResult expand(identity::ModuleId) const override {
    return rejected(identity::IdentityAllocationPhase::Module);
  }
  ExpandedIrIdentityResult expand(identity::DefId) const override {
    return rejected(identity::IdentityAllocationPhase::Definition);
  }
  ExpandedIrIdentityResult expand(InstanceId) const override {
    return rejected(identity::IdentityAllocationPhase::Definition);
  }

private:
  static ExpandedIrIdentityResult rejected(identity::IdentityAllocationPhase phase) {
    zc::Maybe<zc::Array<uint8_t>> noKey;
    zc::Maybe<identity::UnbrandedSourceRange> noRange;
    auto invariant = identity::IdentityInvariant::from(
        identity::IdentityInvariantKind::InvalidHandle, phase, zc::mv(noKey), zc::mv(noRange),
        identity::IdentityApiSite::HandleLookup, 0);
    ZC_IF_SOME(value, invariant) { return RejectedIrIdentityValue{zc::mv(value)}; }
    ZC_UNREACHABLE
  }
};

// Builds a single session-owned LinkPlanConstruction rejection with `kind`.
// `kind` must be legal for the LinkPlanConstruction phase; the shared failure
// factory validates the shape and admits the fact.
IrOperationResult<VerifiedLinkPlan> rejectLinkPlan(IrFailureKind kind, uint32_t ordinal) {
  UnusedIdentityResolver resolver;
  auto fallback =
      IrFailureFallbackContext::from(IrFailurePhase::LinkPlanConstruction,
                                     IrFailureOwner::session(linkPlanSessionContext().clone()));
  ZC_IREQUIRE(fallback != zc::none, "Link plan construction failure fallback must be legal");
  const bool capability = kind == IrFailureKind::OutputCreationFailed;
  const auto branch =
      capability ? IrRejectedBranch::CapabilityRejected : IrRejectedBranch::IrInvariantRejected;
  zc::Maybe<IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  auto descriptor = IrFailureDescriptor::decoded(
      branch, IrFailurePhase::LinkPlanConstruction, kind,
      IrFailureOwner::session(linkPlanSessionContext().clone()), zc::mv(noSite),
      IrFailureDetail::none(), zc::mv(noSpan), zc::mv(noPath), ordinal);
  ZC_IF_SOME(context, fallback) {
    auto admitted = IrFailureFactory::admit(zc::mv(descriptor), context, resolver);
    ZC_IREQUIRE(admitted.is<AcceptedIrFailureDescriptor>(),
                "Session-owned link plan rejection must admit without identity expansion");
    if (capability) {
      zc::Vector<IrFailureFact> facts;
      facts.add(zc::mv(admitted).get<AcceptedIrFailureDescriptor>().fact);
      auto sorted = SortedCapabilityFailureFacts::from(zc::mv(facts));
      ZC_IF_SOME(values, sorted) {
        return IrOperationResult<VerifiedLinkPlan>::capabilityRejected(zc::mv(values));
      }
      ZC_UNREACHABLE
    }
    zc::Vector<IrFailureFact> facts;
    facts.add(zc::mv(admitted).get<AcceptedIrFailureDescriptor>().fact);
    auto sorted = SortedIrInvariantFailureFacts::from(zc::mv(facts));
    ZC_IF_SOME(values, sorted) {
      return IrOperationResult<VerifiedLinkPlan>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

// Orders and checks a record sequence: every record's path must be normalized
// and inside `root`, and the sorted sequence must contain no duplicate canonical
// key. On success `records` is left sorted. Returns the failure kind, or none.
zc::Maybe<IrFailureKind> normalizeInputSequence(zc::Array<LinkInputRecord>& records,
                                                zc::StringPtr root) {
  for (const auto& record : records) {
    if (!isNormalizedAbsolutePath(record.path())) { return IrFailureKind::InvalidFact; }
    if (!isInsideRoot(root, record.path())) { return IrFailureKind::InvalidFact; }
  }
  // Insertion sort by the complete canonical key; the sequences are small.
  for (size_t i = 1; i < records.size(); ++i) {
    for (size_t j = i; j > 0 && records[j].compareCanonical(records[j - 1]) < 0; --j) {
      auto tmp = zc::mv(records[j]);
      records[j] = zc::mv(records[j - 1]);
      records[j - 1] = zc::mv(tmp);
    }
  }
  for (size_t i = 1; i < records.size(); ++i) {
    if (records[i].compareCanonical(records[i - 1]) == 0) { return IrFailureKind::AdditionalFact; }
  }
  return zc::none;
}

bool validRuntimeSymbol(zc::StringPtr symbol) {
  if (symbol.size() == 0) { return false; }
  for (const char byte : symbol) {
    const auto value = static_cast<uint8_t>(byte);
    if (value == 0 || value > 0x7f) { return false; }
  }
  return true;
}

bool validSymbolBytes(zc::ArrayPtr<const uint8_t> symbol) {
  if (symbol.size() == 0) { return false; }
  for (const uint8_t byte : symbol) {
    if (byte == 0 || byte > 0x7f) { return false; }
  }
  return true;
}

int compareSymbols(zc::StringPtr left, zc::StringPtr right) {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    const auto leftByte = static_cast<uint8_t>(left[index]);
    const auto rightByte = static_cast<uint8_t>(right[index]);
    if (leftByte != rightByte) { return leftByte < rightByte ? -1 : 1; }
  }
  if (left.size() == right.size()) { return 0; }
  return left.size() < right.size() ? -1 : 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// ExecutableInspectionProfile
// ---------------------------------------------------------------------------

zc::Maybe<ExecutableInspectionProfile> ExecutableInspectionProfile::make(
    ObjectFormat objectFormat, ExecutableMachine machine, uint32_t pointerWidthBits,
    zc::Array<zc::String>&& requiredRuntimeSymbols, zc::String&& runtimeReferenceDomain) {
  if ((objectFormat != ObjectFormat::Elf && objectFormat != ObjectFormat::MachO) ||
      pointerWidthBits != 64) {
    return zc::none;
  }
  switch (machine) {
    case ExecutableMachine::X86_64:
    case ExecutableMachine::AArch64:
      break;
    default:
      return zc::none;
  }
  for (size_t index = 0; index < requiredRuntimeSymbols.size(); ++index) {
    if (!validRuntimeSymbol(requiredRuntimeSymbols[index])) { return zc::none; }
    if (index > 0 &&
        compareSymbols(requiredRuntimeSymbols[index - 1], requiredRuntimeSymbols[index]) >= 0) {
      return zc::none;
    }
  }
  // The runtime-reference domain is the target's canonical raw runtime-ABI
  // symbol prefix, derived strictly from the object format: ELF spells the C
  // runtime imports `__zom_*`, and Mach-O's nlist adds a leading underscore, so
  // the same imports are `___zom_*`. The caller records the prefix for identity
  // folding, but it may not choose an arbitrary one - an off-canonical value
  // would silently disable or misdirect the unresolved-runtime-symbol check.
  const zc::StringPtr canonicalDomain =
      objectFormat == ObjectFormat::Elf ? "__zom_"_zc : "___zom_"_zc;
  if (runtimeReferenceDomain != canonicalDomain) { return zc::none; }
  return ExecutableInspectionProfile(objectFormat, machine, pointerWidthBits,
                                     zc::mv(requiredRuntimeSymbols),
                                     zc::mv(runtimeReferenceDomain));
}

// ---------------------------------------------------------------------------
// LinkInputRecord
// ---------------------------------------------------------------------------

zc::Maybe<LinkInputRecord> LinkInputRecord::make(zc::StringPtr normalizedPath, LinkInputRole role,
                                                 const identity::Sha256Digest& digest,
                                                 uint64_t byteCount) {
  if (!isNormalizedAbsolutePath(normalizedPath) || byteCount == 0) { return zc::none; }
  return LinkInputRecord(zc::str(normalizedPath), role, digest, byteCount);
}

int LinkInputRecord::compareCanonical(const LinkInputRecord& other) const noexcept {
  if (roleValue != other.roleValue) {
    return static_cast<uint8_t>(roleValue) < static_cast<uint8_t>(other.roleValue) ? -1 : 1;
  }
  const auto left = pathValue.asBytes();
  const auto right = other.pathValue.asBytes();
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index] ? -1 : 1; }
  }
  if (left.size() != right.size()) { return left.size() < right.size() ? -1 : 1; }
  if (digestValue != other.digestValue) { return digestValue < other.digestValue ? -1 : 1; }
  return 0;
}

// ---------------------------------------------------------------------------
// ToolchainClosureRecord
// ---------------------------------------------------------------------------

zc::Maybe<ToolchainClosureRecord> ToolchainClosureRecord::make(
    zc::ArrayPtr<const uint8_t> targetSpecificationIdentity, zc::StringPtr sysroot,
    LinkerDriverKind linkerKind, zc::StringPtr linkerPath,
    const identity::Sha256Digest& linkerDigest, uint64_t linkerByteCount,
    zc::Array<LinkInputRecord>&& crtObjects, zc::Array<LinkInputRecord>&& defaultLibraries) {
  if (targetSpecificationIdentity.size() == 0) { return zc::none; }
  if (!isNormalizedAbsolutePath(sysroot)) { return zc::none; }
  if (!isNormalizedAbsolutePath(linkerPath) || linkerByteCount == 0) { return zc::none; }
  for (const auto& record : crtObjects) {
    if (record.role() != LinkInputRole::CrtObject) { return zc::none; }
  }
  for (const auto& record : defaultLibraries) {
    if (record.role() != LinkInputRole::DefaultLibrary) { return zc::none; }
  }
  // RFC 0043 "Toolchain Discovery Record": every closure path must be normalized,
  // absolute, and contained inside `sysroot`, and each role sequence must be
  // canonically sorted and duplicate-free. `make` is the sole construction entry
  // for this immutable record, so it is the single guard for these invariants;
  // `normalizeInputSequence` sorts each sequence in place and fails closed on an
  // out-of-root path or a duplicate canonical key.
  if (normalizeInputSequence(crtObjects, sysroot) != zc::none) { return zc::none; }
  if (normalizeInputSequence(defaultLibraries, sysroot) != zc::none) { return zc::none; }
  return ToolchainClosureRecord(zc::heapArray<uint8_t>(targetSpecificationIdentity),
                                zc::str(sysroot), linkerKind, zc::str(linkerPath), linkerDigest,
                                linkerByteCount, zc::mv(crtObjects), zc::mv(defaultLibraries));
}

// ---------------------------------------------------------------------------
// LinkPlanCodec
// ---------------------------------------------------------------------------

zc::Array<uint8_t> LinkPlanCodec::encode(const VerifiedLinkPlan& plan) {
  const auto& closure = plan.toolchainClosure();
  zc::Vector<uint8_t> preimage;
  for (const auto byte : "zom.link-plan"_zc) { preimage.add(static_cast<uint8_t>(byte)); }
  preimage.add(0);
  appendFramed(preimage, closure.targetSpecificationIdentity());
  appendUint8(preimage, static_cast<uint8_t>(closure.linkerKind()));
  appendFramed(preimage, closure.sysroot());
  appendFramed(preimage, closure.linkerPath());
  appendFramed(preimage, closure.linkerDigest().bytes());
  appendUint64(preimage, closure.linkerByteCount());
  appendInputSequence(preimage, closure.crtObjects());
  appendInputSequence(preimage, closure.defaultLibraries());
  const auto& inspection = plan.inspectionProfile();
  appendUint8(preimage, static_cast<uint8_t>(inspection.objectFormat()));
  appendUint8(preimage, static_cast<uint8_t>(inspection.machine()));
  appendUint32(preimage, inspection.pointerWidthBits());
  appendUint64(preimage, inspection.requiredRuntimeSymbols().size());
  for (const auto& symbol : inspection.requiredRuntimeSymbols()) { appendFramed(preimage, symbol); }
  appendFramed(preimage, inspection.runtimeReferenceDomain());
  appendFramed(preimage, plan.entrySymbol());
  appendInputSequence(preimage, plan.objectRecords());
  appendInputSequence(preimage, plan.runtimeRecords());
  appendFramed(preimage, plan.outputPath());
  return preimage.releaseAsArray();
}

LinkPlanId LinkPlanCodec::computeId(const VerifiedLinkPlan& plan) {
  auto bytes = encode(plan);
  return LinkPlanId::fromDigest(requireDigest(bytes.asPtr()));
}

// ---------------------------------------------------------------------------
// LinkPlanVerifier
// ---------------------------------------------------------------------------

IrOperationResult<VerifiedLinkPlan> LinkPlanVerifier::verify(ExecutableLinkRequest&& request) {
  // Invariant (6): the output path must be normalized and inside the output root.
  if (!isNormalizedAbsolutePath(request.outputRoot) ||
      !isNormalizedAbsolutePath(request.outputPath)) {
    return rejectLinkPlan(IrFailureKind::OutputCreationFailed, 0);
  }
  if (!isInsideRoot(request.outputRoot, request.outputPath)) {
    return rejectLinkPlan(IrFailureKind::OutputCreationFailed, 1);
  }

  // Invariant (3): the plan must name exactly one non-empty entry symbol.
  if (!validSymbolBytes(request.entrySymbol.asPtr())) {
    return rejectLinkPlan(IrFailureKind::MissingRequiredFact, 2);
  }

  const auto profileFormat = request.inspectionProfile.objectFormat();
  const bool matchingDriver = (profileFormat == ObjectFormat::Elf &&
                               request.closure.linkerKind() == LinkerDriverKind::ElfDriver) ||
                              (profileFormat == ObjectFormat::MachO &&
                               request.closure.linkerKind() == LinkerDriverKind::MachODriver);
  if (!matchingDriver) { return rejectLinkPlan(IrFailureKind::InvalidAbi, 8); }

  // Invariant (1): at least one object artifact must be linked.
  if (request.objectRecords.size() == 0) {
    return rejectLinkPlan(IrFailureKind::MissingRequiredFact, 3);
  }
  for (const auto& record : request.objectRecords) {
    if (record.role() != LinkInputRole::ObjectArtifact) {
      return rejectLinkPlan(IrFailureKind::InvalidFact, 4);
    }
  }
  for (const auto& record : request.runtimeRecords) {
    if (record.role() != LinkInputRole::RuntimeObject) {
      return rejectLinkPlan(IrFailureKind::InvalidFact, 5);
    }
  }

  // Invariant (4)+(5): records are normalized, inside the closure sysroot or the
  // output root, sorted by their complete canonical key, and free of duplicates.
  const auto sysroot = request.closure.sysroot();
  ZC_IF_SOME(kind, normalizeInputSequence(request.objectRecords, request.outputRoot)) {
    return rejectLinkPlan(kind, 6);
  }
  ZC_IF_SOME(kind, normalizeInputSequence(request.runtimeRecords, sysroot)) {
    return rejectLinkPlan(kind, 7);
  }

  auto plan =
      VerifiedLinkPlan(zc::mv(request.closure), zc::mv(request.inspectionProfile),
                       zc::mv(request.entrySymbol), zc::mv(request.objectRecords),
                       zc::mv(request.runtimeRecords), zc::mv(request.outputPath), LinkPlanId());
  plan.idValue = LinkPlanCodec::computeId(plan);
  return IrOperationResult<VerifiedLinkPlan>::verified(zc::mv(plan));
}

}  // namespace zomlang::compiler::ir
