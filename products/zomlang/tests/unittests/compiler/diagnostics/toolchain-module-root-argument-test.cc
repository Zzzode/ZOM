// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/diagnostics/toolchain-module-root-argument.h"

#include "zc/core/array.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::diagnostics {
namespace {

identity::ModulePathSegment requireSegment(zc::StringPtr text) {
  auto segment = identity::ModulePathSegment::fromCanonical(text);
  ZC_IF_SOME(value, segment) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid canonical module-path segment test input");
}

zc::Vector<identity::ModulePathSegment> path(zc::StringPtr first, zc::StringPtr second = nullptr) {
  zc::Vector<identity::ModulePathSegment> result;
  result.add(requireSegment(first));
  if (second != nullptr) { result.add(requireSegment(second)); }
  return result;
}

}  // namespace

ZC_TEST("ToolchainModuleRootArgument admits only the exact canonical core root") {
  auto admitted = ToolchainModuleRootArgument::fromCanonicalPath(path("core"_zc));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(value, admitted) {
    ZC_EXPECT(value.path().size() == 1);
    ZC_EXPECT(value.path()[0].text() == "core"_zc);
    ZC_EXPECT(value.clone() == value);
  }

  ZC_EXPECT(ToolchainModuleRootArgument::fromCanonicalPath(path("app"_zc)) == zc::none);
  ZC_EXPECT(ToolchainModuleRootArgument::fromCanonicalPath(path("core"_zc, "marker"_zc)) ==
            zc::none);
  zc::Vector<identity::ModulePathSegment> empty;
  ZC_EXPECT(ToolchainModuleRootArgument::fromCanonicalPath(zc::mv(empty)) == zc::none);
}

ZC_TEST("ToolchainModuleRootArgument codec is strict and canonically framed") {
  auto admitted = ToolchainModuleRootArgument::fromCanonicalPath(path("core"_zc));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(value, admitted) {
    identity::CanonicalEncoder encoder;
    value.encode(encoder);
    auto bytes = encoder.finish();
    const uint8_t expected[] = {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 4, 'c', 'o', 'r', 'e'};
    ZC_EXPECT(bytes.asPtr() == zc::arrayPtr(expected));

    identity::CanonicalDecoder decoder(bytes);
    auto decoded = ToolchainModuleRootArgument::decodeCanonical(decoder);
    ZC_REQUIRE(decoded != zc::none);
    ZC_IF_SOME(decodedValue, decoded) { ZC_EXPECT(decodedValue == value); }
    ZC_EXPECT(decoder.finished());
  }

  identity::CanonicalEncoder wrongRootEncoder;
  wrongRootEncoder.encodeSequenceSize(1);
  requireSegment("app"_zc).encode(wrongRootEncoder);
  auto wrongRoot = wrongRootEncoder.finish();
  identity::CanonicalDecoder wrongRootDecoder(wrongRoot);
  ZC_EXPECT(ToolchainModuleRootArgument::decodeCanonical(wrongRootDecoder) == zc::none);

  identity::CanonicalEncoder emptyEncoder;
  emptyEncoder.encodeSequenceSize(0);
  auto emptyBytes = emptyEncoder.finish();
  identity::CanonicalDecoder emptyDecoder(emptyBytes);
  ZC_EXPECT(ToolchainModuleRootArgument::decodeCanonical(emptyDecoder) == zc::none);

  identity::CanonicalEncoder oversizedEncoder;
  oversizedEncoder.encodeSequenceSize(2);
  auto oversizedBytes = oversizedEncoder.finish();
  identity::CanonicalDecoder oversizedDecoder(oversizedBytes);
  ZC_EXPECT(ToolchainModuleRootArgument::decodeCanonical(oversizedDecoder) == zc::none);
}

}  // namespace zomlang::compiler::diagnostics
