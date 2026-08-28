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

// RFC 0043 O5/KR5.3 slice (Tier 1.3): prove a VerifiedLinkPlan expands to a
// deterministic, shell-free driver invocation - the exact argument vector,
// working directory, and (empty) environment a child-process spawn consumes -
// in the RFC 0043 canonical order. The expansion performs no I/O and never
// interposes a shell.

#include "compiler/ir/linker-invocation.h"

#include "compiler/identity/crypto/sha256.h"
#include "compiler/ir/link-plan-codec.h"
#include "zc/core/array.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ir {
namespace {

identity::Sha256Digest digestOf(zc::StringPtr seed) {
  auto digest = identity::sha256(seed.asBytes());
  ZC_REQUIRE(digest != zc::none);
  return ZC_REQUIRE_NONNULL(digest);
}

LinkInputRecord input(zc::StringPtr path, LinkInputRole role, zc::StringPtr digestSeed,
                      uint64_t byteCount) {
  auto record = LinkInputRecord::make(path, role, digestOf(digestSeed), byteCount);
  ZC_REQUIRE(record != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(record));
}

zc::Array<LinkInputRecord> oneInput(LinkInputRecord&& record) {
  auto builder = zc::heapArrayBuilder<LinkInputRecord>(1);
  builder.add(zc::mv(record));
  return builder.finish();
}

ToolchainClosureRecord minimalClosure() {
  const uint8_t targetIdentity[] = {0x74, 0x67, 0x74};  // "tgt"
  auto crtObjects = oneInput(input("/sysroot/lib/crt1.o", LinkInputRole::CrtObject, "crt1", 1024));
  auto defaultLibraries =
      oneInput(input("/sysroot/lib/libc.so", LinkInputRole::DefaultLibrary, "libc", 2048));
  auto closure = ToolchainClosureRecord::make(
      zc::arrayPtr(targetIdentity, 3), "/sysroot"_zc, LinkerDriverKind::ElfDriver,
      "/sysroot/bin/cc"_zc, digestOf("cc"), 4096, zc::mv(crtObjects), zc::mv(defaultLibraries));
  ZC_REQUIRE(closure != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(closure));
}

// The plan carries one target-owned argument record (link mode).
zc::Array<LinkerArgumentRecord> targetArguments() {
  auto record = LinkerArgumentRecord::make("-static"_zc);
  ZC_REQUIRE(record != zc::none);
  auto builder = zc::heapArrayBuilder<LinkerArgumentRecord>(1);
  builder.add(ZC_REQUIRE_NONNULL(zc::mv(record)));
  return builder.finish();
}

// Builds a verified plan with the given entry-symbol bytes, object path, output
// path, and output root. The object path must live inside the output root.
VerifiedLinkPlan planWith(zc::Array<uint8_t>&& entrySymbol, zc::StringPtr objectPath,
                          zc::StringPtr outputPath, zc::StringPtr outputRoot) {
  ExecutableLinkRequest request{
      minimalClosure(),
      zc::mv(entrySymbol),
      oneInput(input(objectPath, LinkInputRole::ObjectArtifact, "obj", 512)),
      oneInput(input("/sysroot/lib/zomrt.o", LinkInputRole::RuntimeObject, "rt", 256)),
      targetArguments(),
      zc::str(outputRoot),
      zc::str(outputPath)};
  auto result = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isVerified());
  return zc::mv(result).takeVerified();
}

VerifiedLinkPlan minimalPlan() {
  // "zom" entry symbol.
  return planWith(zc::heapArray<uint8_t>({0x7a, 0x6f, 0x6d}), "/out/app.o"_zc, "/out/app"_zc,
                  "/out"_zc);
}

ZC_TEST("Link plan expands to the canonical driver argument vector") {
  auto plan = minimalPlan();
  auto invocation = expandLinkPlanToInvocation(plan);
  ZC_ASSERT(invocation != zc::none);
  const LinkerInvocation& value = ZC_REQUIRE_NONNULL(invocation);

  ZC_EXPECT(value.program() == "/sysroot/bin/cc"_zc);

  // argv order: driver, -o <out>, -e <entry>, target args, objects, runtime.
  zc::ArrayPtr<const zc::String> argv = value.argv();
  ZC_ASSERT(argv.size() == 8u);
  ZC_EXPECT(argv[0] == "/sysroot/bin/cc"_zc);
  ZC_EXPECT(argv[1] == "-o"_zc);
  ZC_EXPECT(argv[2] == "/out/app"_zc);
  ZC_EXPECT(argv[3] == "-e"_zc);
  ZC_EXPECT(argv[4] == "zom"_zc);
  ZC_EXPECT(argv[5] == "-static"_zc);
  ZC_EXPECT(argv[6] == "/out/app.o"_zc);
  ZC_EXPECT(argv[7] == "/sysroot/lib/zomrt.o"_zc);
}

ZC_TEST("Link plan invocation runs in the output directory with an empty environment") {
  auto plan = minimalPlan();
  auto invocation = expandLinkPlanToInvocation(plan);
  ZC_ASSERT(invocation != zc::none);
  const LinkerInvocation& value = ZC_REQUIRE_NONNULL(invocation);

  ZC_EXPECT(value.workingDirectory() == "/out"_zc);
  ZC_EXPECT(value.environment().size() == 0u);
}

ZC_TEST("Link plan expansion rejects an entry symbol carrying an interior NUL") {
  // A NUL byte cannot survive an argv token; the expansion fails closed rather
  // than truncating the entry symbol.
  auto plan = planWith(zc::heapArray<uint8_t>({0x7a, 0x00, 0x6d}), "/out/app.o"_zc, "/out/app"_zc,
                       "/out"_zc);
  auto invocation = expandLinkPlanToInvocation(plan);
  ZC_EXPECT(invocation == zc::none);
}

ZC_TEST("Link plan expansion derives a nested output directory") {
  auto plan = planWith(zc::heapArray<uint8_t>({0x7a, 0x6f, 0x6d}), "/out/nested/app.o"_zc,
                       "/out/nested/app"_zc, "/out/nested"_zc);
  auto invocation = expandLinkPlanToInvocation(plan);
  ZC_ASSERT(invocation != zc::none);
  const LinkerInvocation& value = ZC_REQUIRE_NONNULL(invocation);
  ZC_EXPECT(value.workingDirectory() == "/out/nested"_zc);
}

}  // namespace
}  // namespace zomlang::compiler::ir
