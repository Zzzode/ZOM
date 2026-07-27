// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/identity/identity-dump.h"

#include "zc/core/encoding.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::identity {
namespace {

void append(zc::Vector<char>& output, zc::StringPtr text) { output.addAll(text); }

template <typename Registry>
void appendRegistry(zc::Vector<char>& output, zc::StringPtr label, const Registry& registry) {
  for (size_t index = 0; index < registry.size(); ++index) {
    ZC_IF_SOME(key, registry.keyAt(index)) {
      append(output, label);
      append(output, " "_zc);
      const auto encoded = key.encode();
      append(output, zc::encodeHex(encoded.asPtr()));
      append(output, "\n"_zc);
    }
  }
}

}  // namespace

zc::Maybe<zc::String> dumpIdentityRegistries(const SemanticIdentityRegistrySet& registries) {
  if (!registries.compilationUnits().isFrozen() || !registries.crates().isFrozen() ||
      !registries.sourceFiles().isFrozen() || !registries.modules().isFrozen() ||
      !registries.definitions().isFrozen() || !registries.impls().isFrozen()) {
    return zc::none;
  }

  zc::Vector<char> output;
  append(output, "zom.identity\n[compilation-units]\n"_zc);
  appendRegistry(output, "compilation-unit"_zc, registries.compilationUnits());
  append(output, "[crates]\n"_zc);
  appendRegistry(output, "crate"_zc, registries.crates());
  append(output, "[sources]\n"_zc);
  for (size_t index = 0; index < registries.sourceFiles().size(); ++index) {
    auto key = registries.sourceFiles().keyAt(index);
    bool foundSnapshot = false;
    ZC_IF_SOME(sourceKey, key) {
      for (const auto& snapshot : registries.sourceSnapshots()) {
        if (!snapshot.source().sameAs(sourceKey)) { continue; }
        append(output, "source "_zc);
        const auto encoded = sourceKey.encode();
        append(output, zc::encodeHex(encoded.asPtr()));
        append(output, " content="_zc);
        append(output, zc::encodeHex(snapshot.contentDigest().bytes()));
        append(output, "\n"_zc);
        foundSnapshot = true;
        break;
      }
    }
    if (!foundSnapshot) { return zc::none; }
  }
  append(output, "[modules]\n"_zc);
  appendRegistry(output, "module"_zc, registries.modules());
  append(output, "[definitions]\n"_zc);
  appendRegistry(output, "definition"_zc, registries.definitions());
  append(output, "[impls]\n"_zc);
  appendRegistry(output, "impl"_zc, registries.impls());
  return zc::str(output.releaseAsArray());
}

}  // namespace zomlang::compiler::identity
