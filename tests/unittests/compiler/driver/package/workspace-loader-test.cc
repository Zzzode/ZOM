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

// Cover the reusable workspace loader extracted from the zomc driver:
// discoverManifestPath's manifest resolution and loadWorkspace's typed failure
// channel. The success and failure sets match the prior CLI behavior; the typed
// WorkspaceLoadFailure only adds precision. A minimal in-memory Filesystem backs
// the tests so no disk state is touched.

#include "compiler/driver/package/workspace-loader.h"

#include "zc/core/filesystem.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

// A minimal Filesystem over one in-memory directory. The current path is empty
// (the root), which is all discoverManifestPath's upward walk and loadWorkspace's
// root openSubdir need.
class MemoryFilesystem final : public zc::Filesystem {
public:
  explicit MemoryFilesystem(zc::Own<const zc::Directory> root)
      : rootDir(zc::mv(root)), currentPath(zc::Path::parse(""_zc)) {}
  const zc::Directory& getRoot() const override { return *rootDir; }
  const zc::Directory& getCurrent() const override { return *rootDir; }
  zc::PathPtr getCurrentPath() const override { return currentPath; }

private:
  zc::Own<const zc::Directory> rootDir;
  zc::Path currentPath;
};

// Writes `content` to `path` (slash-separated) under `dir`.
void writeFile(const zc::Directory& dir, zc::StringPtr path, zc::StringPtr content) {
  zc::Vector<zc::String> parts;
  size_t start = 0;
  for (size_t i = 0; i <= path.size(); ++i) {
    if (i == path.size() || path[i] == '/') {
      parts.add(zc::heapString(path.slice(start, i)));
      start = i + 1;
    }
  }
  dir.openFile(zc::Path(parts.releaseAsArray()),
               zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(content);
}

constexpr zc::StringPtr kValidManifest =
    "[package]\nname = \"app\"\nversion = \"1.0.0\"\nedition = \"2026\"\n"_zc;

ZC_TEST("discoverManifestPath finds a Zom.toml by walking up from the current directory") {
  auto root = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*root, "Zom.toml"_zc, kValidManifest);
  MemoryFilesystem filesystem(zc::mv(root));

  zc::Vector<zc::String> noManifestFlags;
  auto result = discoverManifestPath(filesystem, noManifestFlags.asPtr());
  ZC_REQUIRE(result.is<zc::Path>());
  ZC_EXPECT(result.get<zc::Path>().basename()[0] == "Zom.toml"_zc);
}

ZC_TEST("discoverManifestPath reports ManifestNotFound when no Zom.toml exists") {
  auto root = zc::newInMemoryDirectory(zc::nullClock());
  MemoryFilesystem filesystem(zc::mv(root));

  zc::Vector<zc::String> noManifestFlags;
  auto result = discoverManifestPath(filesystem, noManifestFlags.asPtr());
  ZC_REQUIRE(result.is<InvocationIssue>());
  ZC_EXPECT(result.get<InvocationIssue>() == InvocationIssue::ManifestNotFound);
}

ZC_TEST("discoverManifestPath rejects an explicit path that is not a Zom.toml file") {
  auto root = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*root, "other.toml"_zc, kValidManifest);
  MemoryFilesystem filesystem(zc::mv(root));

  zc::Vector<zc::String> flags;
  flags.add(zc::heapString("other.toml"));
  auto result = discoverManifestPath(filesystem, flags.asPtr());
  ZC_REQUIRE(result.is<InvocationIssue>());
  ZC_EXPECT(result.get<InvocationIssue>() == InvocationIssue::InvalidManifestPath);
}

ZC_TEST("loadWorkspace loads a single-package workspace") {
  auto root = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*root, "Zom.toml"_zc, kValidManifest);
  writeFile(*root, "src/main.zom"_zc, "fn main() {}\n"_zc);
  MemoryFilesystem filesystem(zc::mv(root));

  auto result = loadWorkspace(filesystem, zc::Path("Zom.toml"_zc));
  ZC_REQUIRE(result.is<LoadedWorkspace>());
  auto& loaded = result.get<LoadedWorkspace>();
  // The root manifest diagnostic document is retained.
  ZC_EXPECT(loaded.diagnosticDocuments.size() == 1);
}

ZC_TEST("loadWorkspace reports a typed ManifestParseFailed for a malformed manifest") {
  auto root = zc::newInMemoryDirectory(zc::nullClock());
  // A syntactically broken manifest: parsing fails, so the loader reports a typed
  // parse failure that still carries the collected diagnostic document.
  writeFile(*root, "Zom.toml"_zc, "[package\nname = broken\n"_zc);
  MemoryFilesystem filesystem(zc::mv(root));

  auto result = loadWorkspace(filesystem, zc::Path("Zom.toml"_zc));
  ZC_REQUIRE(result.is<WorkspaceLoadFailure>());
  auto& failure = result.get<WorkspaceLoadFailure>();
  ZC_REQUIRE(failure.is<ManifestParseFailed>());
  // The document collected before the parse failure is carried for diagnostics.
  ZC_EXPECT(failure.get<ManifestParseFailed>().diagnosticDocuments.size() == 1);
}

}  // namespace
}  // namespace zomlang::compiler::driver::package
