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

#include "zomlang/compiler/driver/package/source-snapshot.h"

#include "source-archive-test-data.h"
#include "zc/core/filesystem.h"
#include "zc/core/time.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

class MemoryZstdInput final : public ZstdInput {
public:
  explicit MemoryZstdInput(zc::ArrayPtr<const zc::byte> bytes) : bytes(bytes) {}

  ZstdInputResult read(zc::ArrayPtr<zc::byte> destination) override {
    if (position == bytes.size()) { return ZstdInputEnd{}; }
    const size_t count = zc::min(destination.size(), bytes.size() - position);
    for (size_t index = 0; index < count; ++index) { destination[index] = bytes[position + index]; }
    position += count;
    return ZstdInputData{count};
  }

private:
  zc::ArrayPtr<const zc::byte> bytes;
  size_t position = 0;
};

struct FreshDirectoryState final {
  size_t cleanupAttempts = 0;
  size_t cleanupFailures = 0;
  bool createFails = false;
  zc::Maybe<zc::Own<const zc::Directory>> inspectionRoot;
};

class MemoryFreshDirectory final : public FreshSourceDirectory {
public:
  MemoryFreshDirectory(zc::Own<zc::Directory>&& root, FreshDirectoryState& state)
      : rootValue(zc::mv(root)), state(state) {}
  ~MemoryFreshDirectory() noexcept override = default;

  const zc::Directory& root() const override { return *rootValue; }

  zc::Maybe<MaterializationIssue> finish() override {
    ++state.cleanupAttempts;
    if (state.cleanupAttempts <= state.cleanupFailures) {
      return MaterializationIssue::SnapshotCleanupFailed;
    }
    return zc::none;
  }

private:
  zc::Own<zc::Directory> rootValue;
  FreshDirectoryState& state;
};

class MemoryFreshDirectoryFactory final : public FreshSourceDirectoryFactory {
public:
  explicit MemoryFreshDirectoryFactory(FreshDirectoryState& state) : state(state) {}

  FreshSourceDirectoryResult create() override {
    if (state.createFails) { return MaterializationIssue::FreshDirectoryCreateFailed; }
    auto root = zc::newInMemoryDirectory(zc::nullClock());
    state.inspectionRoot = root->clone();
    return zc::Own<FreshSourceDirectory>(zc::heap<MemoryFreshDirectory>(zc::mv(root), state));
  }

private:
  FreshDirectoryState& state;
};

identity::CanonicalRelativePath libraryPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  for (auto text : {"src"_zc, "lib.zom"_zc}) {
    auto segment = identity::CanonicalPathSegment::fromCanonical(text);
    ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
  }
  ZC_REQUIRE(segments.size() == 2);
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

DigestVerifiedSourceSnapshot materialize(FreshDirectoryState& state) {
  MemoryZstdInput input(zc::arrayPtr(test::kCompressedUstar));
  MemoryFreshDirectoryFactory factory(state);
  SourceArchiveMaterializer materializer;
  auto result = materializer.materialize(input, factory);
  if (result.is<DigestVerifiedSourceSnapshot>()) {
    return zc::mv(result.get<DigestVerifiedSourceSnapshot>());
  }
  ZC_FAIL_REQUIRE("valid source snapshot fixture was rejected");
}

zc::Own<zc::Directory> localSource() {
  auto source = zc::newInMemoryDirectory(zc::nullClock());
  auto file = source->openFile(zc::Path({"src"_zc, "lib.zom"_zc}),
                               zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT);
  file->writeAll("library"_zc);
  return source;
}

class MutatingObserver final : public SourceMaterializationObserver {
public:
  explicit MutatingObserver(const zc::Directory& source) : source(source) {}

  zc::Maybe<MaterializationIssue> afterFirstInventory() override {
    auto file = source.openFile(zc::Path({"src"_zc, "lib.zom"_zc}), zc::WriteMode::MODIFY);
    file->writeAll("changed"_zc);
    return zc::none;
  }

private:
  const zc::Directory& source;
};

enum class DestinationFaultStage { Create, Write, Sync };

class DestinationFaultObserver final : public SourceMaterializationObserver {
public:
  explicit DestinationFaultObserver(DestinationFaultStage stage) : stage(stage) {}

  zc::Maybe<MaterializationIssue> beforeDestinationCreate() override {
    return stage == DestinationFaultStage::Create
               ? zc::Maybe<MaterializationIssue>(MaterializationIssue::DestinationCreateFailed)
               : zc::Maybe<MaterializationIssue>(zc::none);
  }
  zc::Maybe<MaterializationIssue> beforeDestinationWrite() override {
    return stage == DestinationFaultStage::Write
               ? zc::Maybe<MaterializationIssue>(MaterializationIssue::DestinationWriteFailed)
               : zc::Maybe<MaterializationIssue>(zc::none);
  }
  zc::Maybe<MaterializationIssue> beforeDestinationSync() override {
    return stage == DestinationFaultStage::Sync
               ? zc::Maybe<MaterializationIssue>(MaterializationIssue::DestinationSyncFailed)
               : zc::Maybe<MaterializationIssue>(zc::none);
  }

private:
  DestinationFaultStage stage;
};

}  // namespace

ZC_TEST("SourceSnapshotTest.MaterializesReadsAndFinishesIdempotently") {
  FreshDirectoryState state;
  auto snapshot = materialize(state);
  auto path = libraryPath();
  auto read = snapshot.readVerifiedFile(path);
  ZC_REQUIRE(read.is<zc::Array<zc::byte>>());
  ZC_EXPECT(zc::str(read.get<zc::Array<zc::byte>>().asChars()) == "library"_zc);
  ZC_EXPECT(snapshot.finish() == zc::none);
  ZC_EXPECT(snapshot.finish() == zc::none);
  ZC_EXPECT(state.cleanupAttempts == 1);
}

ZC_TEST("SourceSnapshotTest.MaterializesIndependentlyVerifiedCopy") {
  FreshDirectoryState sourceState;
  auto snapshot = materialize(sourceState);
  FreshDirectoryState copyState;
  MemoryFreshDirectoryFactory factory(copyState);
  auto copied = snapshot.materializeVerifiedCopy(factory);
  ZC_REQUIRE(copied.is<zc::Own<DigestVerifiedSourceSnapshot>>());
  auto copy = zc::mv(copied.get<zc::Own<DigestVerifiedSourceSnapshot>>());
  ZC_EXPECT(copy->record().digest() == snapshot.record().digest());
  auto read = copy->readVerifiedFile(libraryPath());
  ZC_REQUIRE(read.is<zc::Array<zc::byte>>());
  ZC_EXPECT(zc::str(read.get<zc::Array<zc::byte>>().asChars()) == "library"_zc);
}

ZC_TEST("SourceSnapshotTest.RejectsMutationAfterPublication") {
  FreshDirectoryState state;
  auto snapshot = materialize(state);
  ZC_IF_SOME(root, state.inspectionRoot) {
    auto file = root->openFile(zc::Path({"src"_zc, "lib.zom"_zc}), zc::WriteMode::MODIFY);
    file->writeAll("changed"_zc);
  }
  auto read = snapshot.readVerifiedFile(libraryPath());
  ZC_REQUIRE(read.is<MaterializationIssue>());
  ZC_EXPECT(read.get<MaterializationIssue>() == MaterializationIssue::SourceChangedDuringSnapshot);
}

ZC_TEST("SourceSnapshotTest.RetriesRemainingCleanupAfterReportedFailure") {
  FreshDirectoryState state;
  state.cleanupFailures = 1;
  {
    auto snapshot = materialize(state);
    ZC_EXPECT(snapshot.finish() == MaterializationIssue::SnapshotCleanupFailed);
    ZC_EXPECT(state.cleanupAttempts == 1);
  }
  ZC_EXPECT(state.cleanupAttempts == 2);
}

ZC_TEST("SourceSnapshotTest.ForwardsFreshDirectoryCreationFailure") {
  FreshDirectoryState state;
  state.createFails = true;
  MemoryZstdInput input(zc::arrayPtr(test::kCompressedUstar));
  MemoryFreshDirectoryFactory factory(state);
  SourceArchiveMaterializer materializer;
  auto result = materializer.materialize(input, factory);
  ZC_REQUIRE(result.is<MaterializationIssue>());
  ZC_EXPECT(result.get<MaterializationIssue>() == MaterializationIssue::FreshDirectoryCreateFailed);
}

ZC_TEST("SourceSnapshotTest.CleansPartialDirectoryAfterAdmissionFailure") {
  FreshDirectoryState state;
  MemoryZstdInput input(zc::arrayPtr(test::kCompressedUstar).first(20));
  MemoryFreshDirectoryFactory factory(state);
  SourceArchiveMaterializer materializer;
  auto result = materializer.materialize(input, factory);
  ZC_REQUIRE(result.is<MaterializationIssue>());
  ZC_EXPECT(state.cleanupAttempts == 1);
}

ZC_TEST("SourceSnapshotTest.CopiesLocalSourceThroughTwoInventories") {
  auto source = localSource();
  FreshDirectoryState state;
  MemoryFreshDirectoryFactory factory(state);
  SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*source, factory);
  ZC_REQUIRE(result.is<DigestVerifiedSourceSnapshot>());
  auto snapshot = zc::mv(result.get<DigestVerifiedSourceSnapshot>());
  auto read = snapshot.readVerifiedFile(libraryPath());
  ZC_REQUIRE(read.is<zc::Array<zc::byte>>());
  ZC_EXPECT(zc::str(read.get<zc::Array<zc::byte>>().asChars()) == "library"_zc);
}

ZC_TEST("SourceSnapshotTest.DetectsLocalMutationBetweenInventoryPasses") {
  auto source = localSource();
  FreshDirectoryState state;
  MemoryFreshDirectoryFactory factory(state);
  MutatingObserver observer(*source);
  SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*source, factory, observer);
  ZC_REQUIRE(result.is<MaterializationIssue>());
  ZC_EXPECT(result.get<MaterializationIssue>() ==
            MaterializationIssue::SourceChangedDuringSnapshot);
  ZC_EXPECT(state.cleanupAttempts == 1);
}

ZC_TEST("SourceSnapshotTest.RejectsCaseFoldedDirectoryCollision") {
  auto source = zc::newInMemoryDirectory(zc::nullClock());
  source
      ->openFile(zc::Path({"A"_zc, "one.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("one"_zc);
  source
      ->openFile(zc::Path({"a"_zc, "two.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("two"_zc);
  FreshDirectoryState state;
  MemoryFreshDirectoryFactory factory(state);
  SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*source, factory);
  ZC_REQUIRE(result.is<MaterializationIssue>());
  ZC_EXPECT(result.get<MaterializationIssue>() == MaterializationIssue::CaseFoldCollision);
}

ZC_TEST("SourceSnapshotTest.ForwardsDestinationFaultInjectionAndCleansUp") {
  for (const auto stage :
       {DestinationFaultStage::Create, DestinationFaultStage::Write, DestinationFaultStage::Sync}) {
    FreshDirectoryState state;
    MemoryZstdInput input(zc::arrayPtr(test::kCompressedUstar));
    MemoryFreshDirectoryFactory factory(state);
    DestinationFaultObserver observer(stage);
    SourceArchiveMaterializer materializer;
    auto result = materializer.materialize(input, factory, observer);
    ZC_REQUIRE(result.is<MaterializationIssue>());
    const auto expected =
        stage == DestinationFaultStage::Create  ? MaterializationIssue::DestinationCreateFailed
        : stage == DestinationFaultStage::Write ? MaterializationIssue::DestinationWriteFailed
                                                : MaterializationIssue::DestinationSyncFailed;
    ZC_EXPECT(result.get<MaterializationIssue>() == expected);
    ZC_EXPECT(state.cleanupAttempts == 1);
  }
}

}  // namespace zomlang::compiler::driver::package
