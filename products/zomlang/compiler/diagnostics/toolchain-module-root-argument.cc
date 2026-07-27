// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/diagnostics/toolchain-module-root-argument.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::diagnostics {
namespace {

constexpr zc::StringPtr kToolchainModuleRoot = "core"_zc;

bool isToolchainModuleRoot(zc::ArrayPtr<const identity::ModulePathSegment> path) {
  return path.size() == 1 && path[0].text() == kToolchainModuleRoot;
}

}  // namespace

ToolchainModuleRootArgument::ToolchainModuleRootArgument(
    zc::Vector<identity::ModulePathSegment>&& path) noexcept
    : pathValue(zc::mv(path)) {}

zc::Maybe<ToolchainModuleRootArgument> ToolchainModuleRootArgument::fromCanonicalPath(
    zc::Vector<identity::ModulePathSegment>&& path) {
  if (!isToolchainModuleRoot(path.asPtr())) { return zc::none; }
  return ToolchainModuleRootArgument(zc::mv(path));
}

zc::Maybe<ToolchainModuleRootArgument> ToolchainModuleRootArgument::decodeCanonical(
    identity::CanonicalDecoder& decoder) {
  auto count = decoder.decodeSequenceSize(1);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) != 1) { return zc::none; }
  auto segment = identity::ModulePathSegment::decodeCanonical(decoder);
  if (segment == zc::none) { return zc::none; }
  zc::Vector<identity::ModulePathSegment> path;
  path.add(zc::mv(ZC_ASSERT_NONNULL(segment)));
  return fromCanonicalPath(zc::mv(path));
}

ToolchainModuleRootArgument ToolchainModuleRootArgument::clone() const {
  zc::Vector<identity::ModulePathSegment> path(pathValue.size());
  for (const auto& segment : pathValue) { path.add(segment.clone()); }
  return ToolchainModuleRootArgument(zc::mv(path));
}

zc::ArrayPtr<const identity::ModulePathSegment> ToolchainModuleRootArgument::path() const noexcept {
  return pathValue.asPtr();
}

void ToolchainModuleRootArgument::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeSequenceSize(pathValue.size());
  for (const auto& segment : pathValue) { segment.encode(encoder); }
}

bool ToolchainModuleRootArgument::operator==(
    const ToolchainModuleRootArgument& other) const noexcept {
  return pathValue == other.pathValue;
}

}  // namespace zomlang::compiler::diagnostics
