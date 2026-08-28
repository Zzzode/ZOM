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

#include "compiler/ir/invoke-linker.h"

#include "compiler/identity/crypto/sha256.h"
#include "compiler/identity/identity-invariant.h"
#include "compiler/identity/semantic/context-fingerprint.h"
#include "compiler/ir/linker-invocation.h"
#include "zc/core/debug.h"
#include "zc/core/subprocess.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ir {

namespace {

// The session context bound to a linker-invocation rejection. Like the other
// RFC 0043 link phases, LinkerInvocation is session-owned, so the fact carries
// the session fingerprint and never needs module/definition identity expansion.
identity::ContextFingerprint linkerInvocationSessionContext() {
  auto digest = identity::sha256("zom.linker-invocation"_zc.asBytes());
  ZC_IREQUIRE(digest != zc::none, "linker-invocation session digest must compute");
  return identity::ContextFingerprint::fromCanonicalDigest(ZC_REQUIRE_NONNULL(digest));
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

// Builds a single session-owned LinkerInvocation rejection with `kind`. `kind`
// must be legal for the LinkerInvocation phase (InputRevisionMismatch,
// InvalidFact, or CanonicalCodecMismatch); the shared failure factory validates
// the shape and admits the fact.
IrOperationResult<VerifiedLinkedExecutable> rejectLinkerInvocation(IrFailureKind kind,
                                                                   uint32_t ordinal) {
  UnusedIdentityResolver resolver;
  auto fallback = IrFailureFallbackContext::from(
      IrFailurePhase::LinkerInvocation,
      IrFailureOwner::session(linkerInvocationSessionContext().clone()));
  ZC_IREQUIRE(fallback != zc::none, "Linker invocation failure fallback must be legal");
  zc::Maybe<IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  auto descriptor = IrFailureDescriptor::decoded(
      IrRejectedBranch::IrInvariantRejected, IrFailurePhase::LinkerInvocation, kind,
      IrFailureOwner::session(linkerInvocationSessionContext().clone()), zc::mv(noSite),
      IrFailureDetail::none(), zc::mv(noSpan), zc::mv(noPath), ordinal);
  ZC_IF_SOME(context, fallback) {
    auto admitted = IrFailureFactory::admit(zc::mv(descriptor), context, resolver);
    ZC_IREQUIRE(admitted.is<AcceptedIrFailureDescriptor>(),
                "Session-owned linker invocation rejection must admit without expansion");
    zc::Vector<IrFailureFact> facts;
    facts.add(zc::mv(admitted).get<AcceptedIrFailureDescriptor>().fact);
    auto sorted = SortedIrInvariantFailureFacts::from(zc::mv(facts));
    ZC_IF_SOME(values, sorted) {
      return IrOperationResult<VerifiedLinkedExecutable>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

// Opens an absolute path under `root`, returning its bytes, or none if absent.
zc::Maybe<zc::Array<zc::byte>> tryReadAbsolute(const zc::ReadableDirectory& root,
                                               zc::StringPtr absolutePath) {
  if (absolutePath.size() < 2 || absolutePath[0] != '/') { return zc::none; }
  ZC_IF_SOME(file, root.tryOpenFile(zc::Path::parse(absolutePath.slice(1)))) {
    return file->readAllBytes();
  }
  return zc::none;
}

}  // namespace

IrOperationResult<VerifiedLinkedExecutable> linkExecutable(const VerifiedLinkPlan& plan,
                                                           const zc::Directory& filesystemRoot) {
  const ToolchainClosureRecord& closure = plan.toolchainClosure();

  // Step 1: re-verify the driver on disk still matches the closure's recorded
  // digest and byte count. A file replaced after discovery (a swap window) is
  // rejected as an input-revision mismatch before any process is spawned.
  zc::Maybe<zc::Array<zc::byte>> driverBytes =
      tryReadAbsolute(filesystemRoot, closure.linkerPath());
  if (driverBytes == zc::none) { return rejectLinkerInvocation(IrFailureKind::InvalidFact, 0); }
  {
    zc::Array<zc::byte> content = ZC_REQUIRE_NONNULL(zc::mv(driverBytes));
    if (content.size() != closure.linkerByteCount()) {
      return rejectLinkerInvocation(IrFailureKind::InputRevisionMismatch, 1);
    }
    zc::Maybe<identity::Sha256Digest> digest = identity::sha256(content.asBytes());
    if (digest == zc::none) { return rejectLinkerInvocation(IrFailureKind::InvalidFact, 2); }
    if (ZC_REQUIRE_NONNULL(digest) != closure.linkerDigest()) {
      return rejectLinkerInvocation(IrFailureKind::InputRevisionMismatch, 3);
    }
  }

  // Step 2: reject a pre-existing file at the plan's output path so a stale
  // artifact can never be mistaken for this link's result.
  zc::StringPtr outputPath = plan.outputPath();
  if (outputPath.size() < 2 || outputPath[0] != '/') {
    return rejectLinkerInvocation(IrFailureKind::InvalidFact, 4);
  }
  zc::Path outputRelative = zc::Path::parse(outputPath.slice(1));
  if (filesystemRoot.exists(outputRelative)) {
    return rejectLinkerInvocation(IrFailureKind::InvalidFact, 5);
  }

  // Step 3: expand the plan to the canonical shell-free invocation and spawn.
  zc::Maybe<LinkerInvocation> invocation = expandLinkPlanToInvocation(plan);
  if (invocation == zc::none) { return rejectLinkerInvocation(IrFailureKind::InvalidFact, 6); }
  const LinkerInvocation& value = ZC_REQUIRE_NONNULL(invocation);

  zc::SubprocessCommand command(value.program());
  command.envPolicy(zc::SubprocessEnvPolicy::Empty);
  command.cwd(value.workingDirectory());
  zc::ArrayPtr<const zc::String> argv = value.argv();
  if (argv.size() >= 1) { command.argv0(argv[0]); }
  for (size_t index = 1; index < argv.size(); ++index) { command.arg(argv[index]); }
  zc::ArrayPtr<const zc::String> environment = value.environment();
  for (size_t index = 0; index + 1 < environment.size(); index += 2) {
    command.env(environment[index], environment[index + 1]);
  }

  zc::SubprocessResult spawnResult = command.run();

  // Step 4: classify the outcome; on any failure, remove partial output.
  auto rejectWithCleanup = [&](IrFailureKind kind,
                               uint32_t ordinal) -> IrOperationResult<VerifiedLinkedExecutable> {
    filesystemRoot.tryRemove(outputRelative);
    return rejectLinkerInvocation(kind, ordinal);
  };

  if (!spawnResult.spawned()) { return rejectWithCleanup(IrFailureKind::InvalidFact, 7); }
  const zc::SubprocessOutput& output = spawnResult.output();
  if (output.terminationKind == zc::SubprocessTerminationKind::Signaled) {
    return rejectWithCleanup(IrFailureKind::InvalidFact, 8);
  }
  if (output.code != 0) { return rejectWithCleanup(IrFailureKind::InvalidFact, 9); }

  // Step 5: the driver exited cleanly; the planned output must now exist. Read it
  // back as the verified linked executable.
  zc::Maybe<zc::Array<zc::byte>> producedBytes = tryReadAbsolute(filesystemRoot, outputPath);
  if (producedBytes == zc::none) { return rejectWithCleanup(IrFailureKind::InvalidFact, 10); }
  zc::Array<zc::byte> produced = ZC_REQUIRE_NONNULL(zc::mv(producedBytes));
  auto owned = zc::heapArray<uint8_t>(produced.size());
  for (size_t index = 0; index < produced.size(); ++index) { owned[index] = produced[index]; }
  return IrOperationResult<VerifiedLinkedExecutable>::verified(
      VerifiedLinkedExecutable(zc::mv(owned)));
}

}  // namespace zomlang::compiler::ir
