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

#include "zomlang/compiler/driver/package/source-inventory.h"

#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

zc::Array<uint8_t> encodePath(const identity::CanonicalRelativePath& path) {
  identity::CanonicalEncoder encoder;
  path.encode(encoder);
  return encoder.finish();
}

}  // namespace

PackageSourceInventory::PackageSourceInventory(
    zc::Vector<identity::CanonicalRelativePath>&& regularFiles) noexcept
    : regularFileValues(zc::mv(regularFiles)) {}

zc::Maybe<PackageSourceInventory> PackageSourceInventory::from(
    zc::Vector<identity::CanonicalRelativePath>&& regularFiles) {
  for (size_t index = 1; index < regularFiles.size(); ++index) {
    auto current = zc::mv(regularFiles[index]);
    size_t insertion = index;
    while (insertion > 0 &&
           encodePath(current).asPtr() < encodePath(regularFiles[insertion - 1]).asPtr()) {
      regularFiles[insertion] = zc::mv(regularFiles[insertion - 1]);
      --insertion;
    }
    regularFiles[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < regularFiles.size(); ++index) {
    if (encodePath(regularFiles[index - 1]).asPtr() == encodePath(regularFiles[index]).asPtr()) {
      return zc::none;
    }
  }
  return PackageSourceInventory(zc::mv(regularFiles));
}

PackageSourceInventory PackageSourceInventory::clone() const {
  zc::Vector<identity::CanonicalRelativePath> result(regularFileValues.size());
  for (const auto& file : regularFileValues) { result.add(file.clone()); }
  return PackageSourceInventory(zc::mv(result));
}

zc::ArrayPtr<const identity::CanonicalRelativePath> PackageSourceInventory::regularFiles()
    const noexcept {
  return regularFileValues.asPtr();
}

bool PackageSourceInventory::containsRegularFile(
    const identity::CanonicalRelativePath& path) const {
  const auto encoded = encodePath(path);
  for (const auto& file : regularFileValues) {
    if (encodePath(file).asPtr() == encoded.asPtr()) { return true; }
  }
  return false;
}

}  // namespace zomlang::compiler::driver::package
