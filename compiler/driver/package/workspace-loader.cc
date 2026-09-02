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

#include "compiler/driver/package/workspace-loader.h"

#include "compiler/driver/package/manifest-model.h"
#include "compiler/driver/package/source-inventory.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/identity/key/package-key.h"

namespace zomlang::compiler::driver::package {
namespace {

// Builds a canonical workspace-relative path from a filesystem path. Every
// component of a manifest-relative path is canonical by construction, so a
// non-canonical component is unreachable.
identity::CanonicalWorkspaceRelativePath workspacePath(zc::PathPtr path) {
  zc::Vector<identity::CanonicalPathSegment> segments(path.size());
  for (const auto& component : path) {
    auto admitted = identity::CanonicalPathSegment::fromSource(component);
    ZC_IF_SOME(value, admitted) {
      segments.add(zc::mv(value));
    } else {
      ZC_UNREACHABLE;
    }
  }
  return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
}

// Builds a manifest diagnostic document for `source` at `path`, or none when the
// digest or document key cannot be formed.
zc::Maybe<PackageDiagnosticDocument> packageDiagnosticDocument(
    identity::CanonicalWorkspaceRelativePath&& path, zc::ArrayPtr<const zc::byte> source) {
  auto digest = identity::sha256(source);
  if (digest == zc::none) { return zc::none; }
  ZC_IF_SOME(digestValue, digest) {
    auto key = InputDocumentKey::from(InputDocumentKind::Manifest,
                                      DiagnosticDocumentPath::workspace(zc::mv(path)), digestValue);
    ZC_IF_SOME(keyValue, key) { return PackageDiagnosticDocument::from(zc::mv(keyValue), source); }
  }
  return zc::none;
}

// Renders a canonical workspace-relative path back to a filesystem path.
zc::Path filesystemPath(const identity::CanonicalWorkspaceRelativePath& path) {
  zc::Path result(nullptr);
  for (uint32_t index = 0; index < path.leadingParents(); ++index) {
    result = zc::mv(result).eval(".."_zc);
  }
  for (const auto& segment : path.segments()) { result = zc::mv(result).append(segment.text()); }
  return result;
}

}  // namespace

zc::OneOf<zc::Path, InvocationIssue> discoverManifestPath(
    const zc::Filesystem& filesystem, zc::ArrayPtr<const zc::String> manifestPaths) {
  try {
    if (manifestPaths.size() == 1) {
      auto path = filesystem.getCurrentPath().eval(manifestPaths[0]);
      if (path.basename().size() != 1 || path.basename()[0] != "Zom.toml"_zc) {
        return InvocationIssue::InvalidManifestPath;
      }
      auto metadata = filesystem.getRoot().tryLstat(path);
      if (metadata == zc::none) { return InvocationIssue::InvalidManifestPath; }
      ZC_IF_SOME(value, metadata) {
        if (value.type != zc::FsNode::Type::FILE) { return InvocationIssue::InvalidManifestPath; }
      }
      return zc::mv(path);
    }
    auto directory = filesystem.getCurrentPath().clone();
    for (;;) {
      auto candidate = directory.clone().append("Zom.toml"_zc);
      auto metadata = filesystem.getRoot().tryLstat(candidate);
      ZC_IF_SOME(value, metadata) {
        if (value.type == zc::FsNode::Type::FILE) { return zc::mv(candidate); }
      }
      if (directory.size() == 0) { break; }
      directory = zc::mv(directory).parent();
    }
  } catch (const zc::Exception&) { return InvocationIssue::InvalidManifestPath; }
  return InvocationIssue::ManifestNotFound;
}

WorkspaceLoadResult loadWorkspace(const zc::Filesystem& filesystem, zc::Path&& manifestPath) {
  try {
    auto rootPath = manifestPath.parent().clone();
    auto rootDirectory = filesystem.getRoot().openSubdir(rootPath);
    auto rootSource = rootDirectory->openFile(zc::Path("Zom.toml"_zc))->readAllText();
    auto rootInventory = PackageSourceInventory::walk(*rootDirectory);
    if (rootInventory == zc::none) { return WorkspaceLoadFailure(InventoryRejected{}); }
    ZC_IF_SOME(rootInventoryValue, rootInventory) {
      zc::Vector<PackageDiagnosticDocument> diagnosticDocuments;
      auto rootDiagnosticDocument =
          packageDiagnosticDocument(workspacePath(zc::Path("Zom.toml"_zc)), rootSource.asBytes());
      if (rootDiagnosticDocument == zc::none) {
        return WorkspaceLoadFailure(DiagnosticDocumentRejected{});
      }
      ZC_IF_SOME(document, rootDiagnosticDocument) { diagnosticDocuments.add(zc::mv(document)); }
      ManifestParser parser;
      auto parsed = parser.parseWorkspaceManifest(workspacePath(zc::Path("Zom.toml"_zc)),
                                                  rootSource, rootInventoryValue);
      if (parsed.is<ManifestFailure>()) {
        return WorkspaceLoadFailure(ManifestParseFailed{zc::mv(parsed.get<ManifestFailure>()),
                                                        zc::mv(diagnosticDocuments)});
      }
      const auto& rootManifest = parsed.get<NormalizedManifest>();
      zc::Vector<WorkspaceMemberInput> members;
      if (rootManifest.hasWorkspace()) {
        for (const auto& memberPath : rootManifest.workspaceMembers()) {
          auto relative = filesystemPath(memberPath);
          auto memberDirectory = rootDirectory->openSubdir(relative);
          auto memberSource = memberDirectory->openFile(zc::Path("Zom.toml"_zc))->readAllText();
          auto memberInventory = PackageSourceInventory::walk(*memberDirectory);
          if (memberInventory == zc::none) { return WorkspaceLoadFailure(InventoryRejected{}); }
          ZC_IF_SOME(memberInventoryValue, memberInventory) {
            auto memberManifestPath = relative.clone().append("Zom.toml"_zc);
            auto memberDiagnosticDocument = packageDiagnosticDocument(
                workspacePath(memberManifestPath), memberSource.asBytes());
            if (memberDiagnosticDocument == zc::none) {
              return WorkspaceLoadFailure(DiagnosticDocumentRejected{});
            }
            ZC_IF_SOME(document, memberDiagnosticDocument) {
              diagnosticDocuments.add(zc::mv(document));
            }
            members.add(WorkspaceMemberInput::from(memberPath.clone(), zc::mv(memberSource),
                                                   zc::mv(memberInventoryValue)));
          }
        }
      }
      auto normalized = normalizeWorkspace(rootSource, rootInventoryValue, zc::mv(members));
      if (normalized.is<NormalizedWorkspace>()) {
        return LoadedWorkspace{zc::mv(normalized.get<NormalizedWorkspace>()), zc::mv(rootPath),
                               zc::mv(diagnosticDocuments)};
      }
      return WorkspaceLoadFailure(WorkspaceNormalizeFailed{
          zc::mv(normalized.get<ManifestFailure>()), zc::mv(diagnosticDocuments)});
    }
  } catch (const zc::Exception&) { return WorkspaceLoadFailure(ManifestReadFailed{}); }
  ZC_UNREACHABLE;
}

}  // namespace zomlang::compiler::driver::package
