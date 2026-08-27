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

#include "compiler/driver/package/trusted-runtime-manifest.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

zc::Array<uint8_t> bytes(zc::StringPtr text) {
  zc::Vector<uint8_t> result(text.size());
  result.addAll(text.asBytes());
  return result.releaseAsArray();
}

identity::Sha256Digest digest(zc::StringPtr text) {
  auto result = identity::sha256(text.asBytes());
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("digest fixture failed");
}

zc::Array<uint32_t> sectionCounts() {
  zc::Vector<uint32_t> result;
  result.add(4);
  return result.releaseAsArray();
}

zc::Array<TrustedRuntimeSymbolId> requiredOperations() {
  zc::Vector<TrustedRuntimeSymbolId> result;
  result.add(TrustedRuntimeSymbolId{0, 1, 1});
  return result.releaseAsArray();
}

TrustedRuntimeSymbolRecord symbol(
    uint32_t index, zc::StringPtr name,
    TrustedRuntimeSymbolBindingTag binding = TrustedRuntimeSymbolBindingTag::Global) {
  return TrustedRuntimeSymbolRecord::named(TrustedRuntimeSymbolId{0, 1, index}, bytes(name),
                                           TrustedRuntimeSymbolKindTag::Function, 0, binding, 0,
                                           TrustedRuntimeSymbolVisibility::Default,
                                           TrustedRuntimeSymbolSectionTag::Section, 2, 16);
}

TrustedRuntimeManifestSet manifest(
    zc::StringPtr firstName = "allocate"_zc, bool extraSymbol = false,
    bool unexpectedInitializer = false, bool includeRelocation = true,
    TrustedRuntimeOperation operation = TrustedRuntimeOperation::Allocate) {
  zc::Vector<TrustedRuntimeSymbolRecord> symbols;
  symbols.add(symbol(1, firstName));
  if (extraSymbol) { symbols.add(symbol(2, "extra"_zc)); }
  zc::Vector<TrustedRuntimeRelocationRecord> relocations;
  if (includeRelocation) {
    relocations.add(TrustedRuntimeRelocationRecord{0, 3, 8, 1, {0, 1, 1}, 0});
  }
  zc::Vector<TrustedRuntimeOperationRecord> operations;
  operations.add(TrustedRuntimeOperationRecord{operation, {0, 1, 1}});
  auto result = TrustedRuntimeManifestSet::verify(sectionCounts(), zc::mv(symbols),
                                                  zc::mv(relocations), zc::mv(operations),
                                                  requiredOperations(), unexpectedInitializer);
  if (result.is<TrustedRuntimeManifestSet>()) {
    return zc::mv(result.get<TrustedRuntimeManifestSet>());
  }
  ZC_FAIL_REQUIRE("trusted runtime manifest fixture was rejected");
}

TrustedRuntimeVerificationEvidence evidence() {
  zc::Vector<identity::Sha256Digest> objects;
  objects.add(digest("object"_zc));
  auto declared = manifest();
  auto observed = manifest();
  auto result =
      TrustedRuntimeVerificationEvidence::verify(zc::mv(objects), zc::mv(declared), observed);
  if (result.is<TrustedRuntimeVerificationEvidence>()) {
    return zc::mv(result.get<TrustedRuntimeVerificationEvidence>());
  }
  ZC_FAIL_REQUIRE("trusted runtime evidence fixture was rejected");
}

}  // namespace

ZC_TEST("Trusted runtime manifest framing passes all three RFC fixed oracles") {
  zc::Vector<zc::Array<uint8_t>> records;
  uint8_t value[] = {0xa1};
  zc::Vector<uint8_t> record;
  record.addAll(zc::arrayPtr(value));
  records.add(record.releaseAsArray());
  ZC_EXPECT(
      zc::encodeHex(digestTrustedRuntimeManifestFrames(TrustedRuntimeManifestKind::Symbols, records)
                        .bytes()) ==
      "bfe1388a645337e27c7698f417ab1dee2b84085cf4d5ec43d3b28fc32a6a4cb5"_zc);
  ZC_EXPECT(zc::encodeHex(
                digestTrustedRuntimeManifestFrames(TrustedRuntimeManifestKind::Relocations, records)
                    .bytes()) ==
            "c97176c784c39a9851ea3182d74016fe013cb5768cd3b4ad8d2f33be9232a339"_zc);
  ZC_EXPECT(zc::encodeHex(
                digestTrustedRuntimeManifestFrames(TrustedRuntimeManifestKind::Operations, records)
                    .bytes()) ==
            "62ed17f52434b97d173457e33fb4484f87520882d62f1bd08eecd11d83b5df00"_zc);
}

ZC_TEST("Trusted runtime symbol name codec passes the independent RFC oracle") {
  auto named = encodeTrustedRuntimeSymbolName(TrustedRuntimeSymbolNameTag::Named, bytes("x"_zc));
  ZC_REQUIRE(named.is<zc::Array<uint8_t>>());
  ZC_EXPECT(zc::encodeHex(named.get<zc::Array<uint8_t>>()) == "02000000000000000178"_zc);

  auto namedDigest =
      digestTrustedRuntimeSymbolName(TrustedRuntimeSymbolNameTag::Named, bytes("x"_zc));
  ZC_REQUIRE(namedDigest.is<identity::Sha256Digest>());
  ZC_EXPECT(zc::encodeHex(namedDigest.get<identity::Sha256Digest>().bytes()) ==
            "8c5ba799f877152eaf4207d8606e986a59ccd38739305e1943f9426b2b3c201e"_zc);

  zc::Vector<uint8_t> empty;
  auto unnamed = encodeTrustedRuntimeSymbolName(TrustedRuntimeSymbolNameTag::Unnamed, empty);
  ZC_REQUIRE(unnamed.is<zc::Array<uint8_t>>());
  ZC_EXPECT(zc::encodeHex(unnamed.get<zc::Array<uint8_t>>()) == "01"_zc);

  auto none = encodeTrustedRuntimeSymbolName(static_cast<TrustedRuntimeSymbolNameTag>(0x00), empty);
  auto some =
      encodeTrustedRuntimeSymbolName(static_cast<TrustedRuntimeSymbolNameTag>(0x01), bytes("x"_zc));
  auto emptyNamed = encodeTrustedRuntimeSymbolName(TrustedRuntimeSymbolNameTag::Named, empty);
  auto control = encodeTrustedRuntimeSymbolName(TrustedRuntimeSymbolNameTag::Named, bytes("\n"_zc));
  ZC_EXPECT(none.is<TrustedRuntimeInvariantIssue>());
  ZC_EXPECT(some.is<TrustedRuntimeInvariantIssue>());
  ZC_EXPECT(emptyNamed.is<TrustedRuntimeInvariantIssue>());
  ZC_EXPECT(control.is<TrustedRuntimeInvariantIssue>());
}

ZC_TEST("Trusted runtime manifest rejects weak fallback invalid targets and initializers") {
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(symbol(1, "weak"_zc, TrustedRuntimeSymbolBindingTag::Weak));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::WeakFallback);
  }
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(symbol(1, "target"_zc));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    relocations.add(TrustedRuntimeRelocationRecord{0, 3, 0, 1, {0, 1, 99}, 0});
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::InvalidManifestRecord);
  }
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(symbol(1, "init"_zc));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required, true);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::UnexpectedInitializer);
  }
}

ZC_TEST("Trusted runtime manifest requires exact operation classification") {
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(symbol(1, "allocate"_zc));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    operations.add(TrustedRuntimeOperationRecord{TrustedRuntimeOperation::Allocate, {0, 1, 1}});
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::OperationManifestMismatch);
  }
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(symbol(1, "allocate"_zc));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    auto result =
        TrustedRuntimeManifestSet::verify(sectionCounts(), zc::mv(symbols), zc::mv(relocations),
                                          zc::mv(operations), requiredOperations());
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::OperationManifestMismatch);
  }
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(symbol(1, "allocate"_zc));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    operations.add(TrustedRuntimeOperationRecord{TrustedRuntimeOperation::Allocate, {0, 1, 1}});
    operations.add(TrustedRuntimeOperationRecord{TrustedRuntimeOperation::Deallocate, {0, 1, 1}});
    auto result =
        TrustedRuntimeManifestSet::verify(sectionCounts(), zc::mv(symbols), zc::mv(relocations),
                                          zc::mv(operations), requiredOperations());
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::OperationManifestMismatch);
  }
}

ZC_TEST("Trusted runtime manifest admits the complete positive symbol inventory") {
  zc::Vector<TrustedRuntimeSymbolRecord> symbols;
  symbols.add(TrustedRuntimeSymbolRecord::unnamed(
      {0, 1, 0}, TrustedRuntimeSymbolKindTag::NoType, 0, TrustedRuntimeSymbolBindingTag::Local, 0,
      TrustedRuntimeSymbolVisibility::Default, TrustedRuntimeSymbolSectionTag::Undefined, 0, 0));
  symbols.add(TrustedRuntimeSymbolRecord::unnamed(
      {0, 1, 1}, TrustedRuntimeSymbolKindTag::Section, 0, TrustedRuntimeSymbolBindingTag::Local, 0,
      TrustedRuntimeSymbolVisibility::Default, TrustedRuntimeSymbolSectionTag::Section, 2, 0));
  symbols.add(TrustedRuntimeSymbolRecord::named(
      {0, 1, 2}, bytes("external"_zc), TrustedRuntimeSymbolKindTag::NoType, 0,
      TrustedRuntimeSymbolBindingTag::Global, 0, TrustedRuntimeSymbolVisibility::Default,
      TrustedRuntimeSymbolSectionTag::Undefined, 0, 0));
  symbols.add(TrustedRuntimeSymbolRecord::named(
      {0, 1, 3}, bytes("absolute"_zc), TrustedRuntimeSymbolKindTag::Object, 0,
      TrustedRuntimeSymbolBindingTag::Local, 0, TrustedRuntimeSymbolVisibility::Hidden,
      TrustedRuntimeSymbolSectionTag::Absolute, 0, 8));
  symbols.add(TrustedRuntimeSymbolRecord::named(
      {0, 1, 4}, bytes("common"_zc), TrustedRuntimeSymbolKindTag::Common, 0,
      TrustedRuntimeSymbolBindingTag::Local, 0, TrustedRuntimeSymbolVisibility::Default,
      TrustedRuntimeSymbolSectionTag::Common, 0, 8));
  symbols.add(TrustedRuntimeSymbolRecord::named(
      {0, 1, 5}, bytes("duplicate"_zc), TrustedRuntimeSymbolKindTag::NoType, 0,
      TrustedRuntimeSymbolBindingTag::Local, 0, TrustedRuntimeSymbolVisibility::Internal,
      TrustedRuntimeSymbolSectionTag::Section, 2, 0));
  symbols.add(TrustedRuntimeSymbolRecord::named(
      {0, 1, 6}, bytes("duplicate"_zc), TrustedRuntimeSymbolKindTag::Tls, 0,
      TrustedRuntimeSymbolBindingTag::Local, 0, TrustedRuntimeSymbolVisibility::Protected,
      TrustedRuntimeSymbolSectionTag::Section, 3, 8));
  symbols.add(TrustedRuntimeSymbolRecord::named(
      {0, 1, 7}, bytes("os-kind"_zc), TrustedRuntimeSymbolKindTag::OsSpecific, 10,
      TrustedRuntimeSymbolBindingTag::Global, 0, TrustedRuntimeSymbolVisibility::Default,
      TrustedRuntimeSymbolSectionTag::Section, 2, 16));
  symbols.add(TrustedRuntimeSymbolRecord::named(
      {0, 1, 8}, bytes("processor-kind"_zc), TrustedRuntimeSymbolKindTag::ProcessorSpecific, 13,
      TrustedRuntimeSymbolBindingTag::Global, 0, TrustedRuntimeSymbolVisibility::Default,
      TrustedRuntimeSymbolSectionTag::Section, 2, 16));
  symbols.add(TrustedRuntimeSymbolRecord::named(
      {0, 1, 9}, bytes("runtime-file"_zc), TrustedRuntimeSymbolKindTag::File, 0,
      TrustedRuntimeSymbolBindingTag::Local, 0, TrustedRuntimeSymbolVisibility::Default,
      TrustedRuntimeSymbolSectionTag::Absolute, 0, 0));
  symbols.add(symbol(10, "allocate"_zc));

  zc::Vector<TrustedRuntimeRelocationRecord> relocations;
  relocations.add(TrustedRuntimeRelocationRecord{0, 4, 0, 1, {0, 1, 5}, 0});
  relocations.add(TrustedRuntimeRelocationRecord{0, 4, 8, 2, {0, 1, 1}, -4});
  zc::Vector<TrustedRuntimeOperationRecord> operations;
  operations.add(TrustedRuntimeOperationRecord{TrustedRuntimeOperation::Allocate, {0, 1, 10}});
  zc::Vector<TrustedRuntimeSymbolId> required;
  required.add(TrustedRuntimeSymbolId{0, 1, 10});

  zc::Vector<uint32_t> inventorySectionCounts;
  inventorySectionCounts.add(5);
  auto result = TrustedRuntimeManifestSet::verify(
      inventorySectionCounts, zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
  ZC_REQUIRE(result.is<TrustedRuntimeManifestSet>());
  ZC_EXPECT(result.get<TrustedRuntimeManifestSet>().symbols().size() == 11);
  ZC_EXPECT(result.get<TrustedRuntimeManifestSet>().relocations().size() == 2);
}

ZC_TEST("Trusted runtime manifest admits every operation tag exactly once") {
  constexpr zc::StringPtr names[] = {
      "allocate"_zc,
      "deallocate"_zc,
      "read-request-frame"_zc,
      "write-response-frame"_zc,
      "validate-contract-path"_zc,
      "read-input"_zc,
      "read-environment"_zc,
      "write-output"_zc,
      "export-environment"_zc,
      "open-input"_zc,
      "open-output"_zc,
      "read-file"_zc,
      "write-file"_zc,
      "close-file"_zc,
      "fail"_zc,
      "exit"_zc,
  };
  zc::Vector<TrustedRuntimeSymbolRecord> symbols;
  zc::Vector<TrustedRuntimeOperationRecord> operations;
  zc::Vector<TrustedRuntimeSymbolId> required;
  for (uint32_t index = 0; index < 16; ++index) {
    const TrustedRuntimeSymbolId id{0, 1, index + 1};
    symbols.add(symbol(index + 1, names[index]));
    operations.add(
        TrustedRuntimeOperationRecord{static_cast<TrustedRuntimeOperation>(index + 1), id});
    required.add(id);
  }
  zc::Vector<TrustedRuntimeRelocationRecord> relocations;
  auto result = TrustedRuntimeManifestSet::verify(
      sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
  ZC_REQUIRE(result.is<TrustedRuntimeManifestSet>());
  ZC_EXPECT(result.get<TrustedRuntimeManifestSet>().operations().size() == 16);
}

ZC_TEST("Trusted runtime manifest rejects every invalid structural reference class") {
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(TrustedRuntimeSymbolRecord::named(
        {0, 1, 1}, bytes("\n"_zc), TrustedRuntimeSymbolKindTag::Object, 0,
        TrustedRuntimeSymbolBindingTag::Local, 0, TrustedRuntimeSymbolVisibility::Default,
        TrustedRuntimeSymbolSectionTag::Section, 2, 1));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::InvalidManifestRecord);
  }
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(TrustedRuntimeSymbolRecord::unnamed(
        {1, 1, 0}, TrustedRuntimeSymbolKindTag::NoType, 0, TrustedRuntimeSymbolBindingTag::Local, 0,
        TrustedRuntimeSymbolVisibility::Default, TrustedRuntimeSymbolSectionTag::Undefined, 0, 0));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_EXPECT(result.is<TrustedRuntimeInvariantIssue>());
  }
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(TrustedRuntimeSymbolRecord::unnamed(
        {0, 4, 0}, TrustedRuntimeSymbolKindTag::NoType, 0, TrustedRuntimeSymbolBindingTag::Local, 0,
        TrustedRuntimeSymbolVisibility::Default, TrustedRuntimeSymbolSectionTag::Undefined, 0, 0));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_EXPECT(result.is<TrustedRuntimeInvariantIssue>());
  }
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(TrustedRuntimeSymbolRecord::named(
        {0, 1, 1}, bytes("bad-kind"_zc), TrustedRuntimeSymbolKindTag::OsSpecific, 9,
        TrustedRuntimeSymbolBindingTag::Local, 0, TrustedRuntimeSymbolVisibility::Default,
        TrustedRuntimeSymbolSectionTag::Section, 2, 1));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_EXPECT(result.is<TrustedRuntimeInvariantIssue>());
  }
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(TrustedRuntimeSymbolRecord::named(
        {0, 1, 1}, bytes("bad-visibility"_zc), TrustedRuntimeSymbolKindTag::Object, 0,
        TrustedRuntimeSymbolBindingTag::Local, 0, static_cast<TrustedRuntimeSymbolVisibility>(0),
        TrustedRuntimeSymbolSectionTag::Section, 2, 1));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_EXPECT(result.is<TrustedRuntimeInvariantIssue>());
  }
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(TrustedRuntimeSymbolRecord::named(
        {0, 1, 1}, bytes("bad-section"_zc), TrustedRuntimeSymbolKindTag::Object, 0,
        TrustedRuntimeSymbolBindingTag::Local, 0, TrustedRuntimeSymbolVisibility::Default,
        TrustedRuntimeSymbolSectionTag::Section, 4, 1));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_EXPECT(result.is<TrustedRuntimeInvariantIssue>());
  }
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(TrustedRuntimeSymbolRecord::named(
        {0, 1, 1}, bytes("undefined-size"_zc), TrustedRuntimeSymbolKindTag::Object, 0,
        TrustedRuntimeSymbolBindingTag::Local, 0, TrustedRuntimeSymbolVisibility::Default,
        TrustedRuntimeSymbolSectionTag::Undefined, 0, 1));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_EXPECT(result.is<TrustedRuntimeInvariantIssue>());
  }
}

ZC_TEST("Trusted runtime manifest rejects duplicate and fallback ambiguity") {
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(symbol(1, "first"_zc));
    symbols.add(symbol(1, "second"_zc));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::InvalidManifestRecord);
  }
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(symbol(1, "duplicate"_zc));
    symbols.add(symbol(2, "duplicate"_zc));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::InvalidManifestRecord);
  }
  constexpr TrustedRuntimeSymbolBindingTag fallbackBindings[] = {
      TrustedRuntimeSymbolBindingTag::OsSpecific,
      TrustedRuntimeSymbolBindingTag::ProcessorSpecific,
  };
  for (const auto binding : fallbackBindings) {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    const uint8_t native = binding == TrustedRuntimeSymbolBindingTag::OsSpecific ? 10 : 13;
    symbols.add(TrustedRuntimeSymbolRecord::named(
        {0, 1, 1}, bytes("fallback"_zc), TrustedRuntimeSymbolKindTag::Function, 0, binding, native,
        TrustedRuntimeSymbolVisibility::Default, TrustedRuntimeSymbolSectionTag::Section, 2, 16));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::WeakFallback);
  }
}

ZC_TEST("Trusted runtime manifest rejects relocation and operation corruption") {
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(symbol(1, "target"_zc));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    relocations.add(TrustedRuntimeRelocationRecord{0, 3, 8, 1, {0, 1, 1}, 0});
    relocations.add(TrustedRuntimeRelocationRecord{0, 3, 8, 2, {0, 1, 1}, 1});
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::InvalidManifestRecord);
  }
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(symbol(1, "target"_zc));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    operations.add(
        TrustedRuntimeOperationRecord{static_cast<TrustedRuntimeOperation>(0), {0, 1, 1}});
    auto result =
        TrustedRuntimeManifestSet::verify(sectionCounts(), zc::mv(symbols), zc::mv(relocations),
                                          zc::mv(operations), requiredOperations());
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::InvalidManifestRecord);
  }
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(TrustedRuntimeSymbolRecord::named(
        {0, 1, 1}, bytes("not-function"_zc), TrustedRuntimeSymbolKindTag::Object, 0,
        TrustedRuntimeSymbolBindingTag::Global, 0, TrustedRuntimeSymbolVisibility::Default,
        TrustedRuntimeSymbolSectionTag::Section, 2, 16));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    operations.add(TrustedRuntimeOperationRecord{TrustedRuntimeOperation::Allocate, {0, 1, 1}});
    auto result =
        TrustedRuntimeManifestSet::verify(sectionCounts(), zc::mv(symbols), zc::mv(relocations),
                                          zc::mv(operations), requiredOperations());
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::InvalidManifestRecord);
  }
  {
    zc::Vector<TrustedRuntimeSymbolRecord> symbols;
    symbols.add(symbol(1, "target"_zc));
    zc::Vector<TrustedRuntimeRelocationRecord> relocations;
    zc::Vector<TrustedRuntimeOperationRecord> operations;
    operations.add(TrustedRuntimeOperationRecord{TrustedRuntimeOperation::Allocate, {0, 1, 1}});
    zc::Vector<TrustedRuntimeSymbolId> required;
    required.add(TrustedRuntimeSymbolId{0, 1, 1});
    required.add(TrustedRuntimeSymbolId{0, 1, 1});
    auto result = TrustedRuntimeManifestSet::verify(
        sectionCounts(), zc::mv(symbols), zc::mv(relocations), zc::mv(operations), required);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::InvalidManifestRecord);
  }
}

ZC_TEST("Trusted runtime evidence distinguishes extra and changed manifest facts") {
  {
    zc::Vector<identity::Sha256Digest> objects;
    objects.add(digest("object"_zc));
    auto declared = manifest();
    auto observed = manifest("allocate"_zc, true);
    auto result =
        TrustedRuntimeVerificationEvidence::verify(zc::mv(objects), zc::mv(declared), observed);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::UnmanifestedSymbol);
  }
  {
    zc::Vector<identity::Sha256Digest> objects;
    objects.add(digest("object"_zc));
    auto declared = manifest();
    auto observed = manifest("changed"_zc);
    auto result =
        TrustedRuntimeVerificationEvidence::verify(zc::mv(objects), zc::mv(declared), observed);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::SymbolManifestMismatch);
  }
  {
    zc::Vector<identity::Sha256Digest> objects;
    objects.add(digest("object"_zc));
    auto declared = manifest("allocate"_zc, false, false, false);
    auto observed = manifest();
    auto result =
        TrustedRuntimeVerificationEvidence::verify(zc::mv(objects), zc::mv(declared), observed);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::UnmanifestedRelocation);
  }
  {
    zc::Vector<identity::Sha256Digest> objects;
    objects.add(digest("object"_zc));
    auto declared = manifest();
    auto observed = manifest("allocate"_zc, false, false, false);
    auto result =
        TrustedRuntimeVerificationEvidence::verify(zc::mv(objects), zc::mv(declared), observed);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::RelocationManifestMismatch);
  }
  {
    zc::Vector<identity::Sha256Digest> objects;
    objects.add(digest("object"_zc));
    auto declared = manifest();
    auto observed =
        manifest("allocate"_zc, false, false, true, TrustedRuntimeOperation::Deallocate);
    auto result =
        TrustedRuntimeVerificationEvidence::verify(zc::mv(objects), zc::mv(declared), observed);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::OperationManifestMismatch);
  }
  {
    zc::Vector<identity::Sha256Digest> objects;
    objects.add(digest("same"_zc));
    objects.add(digest("same"_zc));
    auto declared = manifest();
    auto observed = manifest();
    auto result =
        TrustedRuntimeVerificationEvidence::verify(zc::mv(objects), zc::mv(declared), observed);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::DuplicateObjectDigest);
  }
}

ZC_TEST("Trusted build runtime key requires exact declared object bytes") {
  {
    zc::Vector<zc::Array<uint8_t>> objects;
    objects.add(bytes("object"_zc));
    auto result =
        TrustedBuildRuntimeKey::verifyEvidence("zom"_zc, "zom"_zc, zc::mv(objects), evidence());
    ZC_EXPECT(result.is<TrustedBuildRuntimeKey>());
  }
  {
    zc::Vector<zc::Array<uint8_t>> objects;
    objects.add(bytes("changed"_zc));
    auto result =
        TrustedBuildRuntimeKey::verifyEvidence("zom"_zc, "zom"_zc, zc::mv(objects), evidence());
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::ObjectDigestMismatch);
  }
  {
    zc::Vector<zc::Array<uint8_t>> objects;
    objects.add(bytes("object"_zc));
    auto result = TrustedBuildRuntimeKey::verifyEvidence("zom"_zc, "zom-alternate"_zc,
                                                         zc::mv(objects), evidence());
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::RuntimeAbiMismatch);
  }
}

}  // namespace zomlang::compiler::driver::package
