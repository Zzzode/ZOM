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

#include "zomlang/compiler/lir/lir-algebra-codec.h"

namespace zomlang::compiler::lir {
namespace {

// The initial LIR-algebra registry binds the current MIR revision domain. The
// empty/initial registry reproduces the RFC 0021 56-byte oracle whose preimage
// framing is exactly this ASCII string.
constexpr char kDefaultSourceMirRevisionDomain[] = "zom.mir-revision";

bool isAscii(zc::StringPtr value) {
  if (value.size() == 0) { return false; }
  for (const auto byte : value) {
    if (static_cast<uint8_t>(byte) >= 0x80U || byte == '\0') { return false; }
  }
  return true;
}

void append(zc::Vector<uint8_t>& output, zc::StringPtr value) {
  for (const auto byte : value) { output.add(static_cast<uint8_t>(byte)); }
}

void append(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> value) {
  output.addAll(value);
}

void appendUint64(zc::Vector<uint8_t>& output, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    output.add(static_cast<uint8_t>(value >> static_cast<uint32_t>(shift)));
  }
}

// Frame: big-endian uint64 byte length followed by the exact bytes.
void appendFramed(zc::Vector<uint8_t>& output, zc::StringPtr value) {
  appendUint64(output, value.size());
  append(output, value);
}

void appendFramed(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> value) {
  appendUint64(output, value.size());
  append(output, value);
}

// EncodeFramedSequence: big-endian uint64 element count followed by one Frame
// per already-canonical element record.
void appendFramedSequence(zc::Vector<uint8_t>& output,
                          zc::ArrayPtr<const zc::Array<uint8_t>> records) {
  appendUint64(output, records.size());
  for (const auto& record : records) { appendFramed(output, record.asPtr()); }
}

identity::Sha256Digest requireDigest(zc::ArrayPtr<const uint8_t> bytes) {
  auto digest = identity::sha256(bytes);
  ZC_IF_SOME(value, digest) { return value; }
  ZC_UNREACHABLE;
}

}  // namespace

LirAlgebraRevision LirAlgebraRevision::fromDigest(const identity::Sha256Digest& digest) noexcept {
  return LirAlgebraRevision(digest);
}

LirAlgebraRegistry LirAlgebraRegistry::empty() {
  return LirAlgebraRegistry(zc::str(kDefaultSourceMirRevisionDomain));
}

zc::Maybe<LirAlgebraRegistry> LirAlgebraRegistry::withSourceDomain(zc::StringPtr domain) {
  if (!isAscii(domain)) { return zc::none; }
  return LirAlgebraRegistry(zc::str(domain));
}

void LirAlgebraRegistry::addRecipe(zc::ArrayPtr<const uint8_t> canonicalRecord) {
  recipeRecords.add(zc::heapArray<uint8_t>(canonicalRecord));
}

void LirAlgebraRegistry::addGeneratedRecipe(zc::ArrayPtr<const uint8_t> canonicalRecord) {
  generatedRecords.add(zc::heapArray<uint8_t>(canonicalRecord));
}

zc::Array<uint8_t> LirAlgebraCodec::encode(const LirAlgebraRegistry& registry) {
  zc::Vector<uint8_t> preimage;
  append(preimage, "zom.lir-algebra"_zc);
  preimage.add(0);
  appendFramed(preimage, registry.sourceMirRevisionDomain());
  appendFramedSequence(preimage, registry.recipes());
  appendFramedSequence(preimage, registry.generatedRecipes());
  return preimage.releaseAsArray();
}

LirAlgebraRevision LirAlgebraCodec::compute(const LirAlgebraRegistry& registry) {
  auto bytes = encode(registry);
  return LirAlgebraRevision::fromDigest(requireDigest(bytes.asPtr()));
}

}  // namespace zomlang::compiler::lir
