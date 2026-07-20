// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/binding-input-diagnostic-adapter.h"

#include <climits>

#include "zc/core/string.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang::compiler::binder {
namespace {

struct VerifiedDiagnosticRange final {
  source::SourceLoc start;
  source::SourceLoc end;
};

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
}

zc::Maybe<VerifiedDiagnosticRange> resolveFailureRange(const VerifiedParsedModule& parsedModule,
                                                       const BindingInputSourceFailure& failure) {
  const auto& tree = parsedModule.tree();
  const auto node = failure.syntax();
  const auto& span = failure.source();
  if (!tree.contains(node) || !span.belongsTo(parsedModule.rootSpan().source()) ||
      span.byteStart() > span.byteEnd() || span.byteEnd() > parsedModule.byteLength() ||
      span.byteEnd() > UINT_MAX) {
    return zc::none;
  }
  auto exactNodeSpan = parsedModule.spanFor(tree.node(node).range);
  if (exactNodeSpan == zc::none) { return zc::none; }
  ZC_IF_SOME(value, exactNodeSpan) {
    if (!sameSpan(value, span)) { return zc::none; }
  }
  auto start = parsedModule.sourceLocFor(span);
  if (start == zc::none) { return zc::none; }
  ZC_IF_SOME(startValue, start) {
    const auto length = static_cast<unsigned>(span.byteEnd() - span.byteStart());
    return VerifiedDiagnosticRange{startValue, startValue.getAdvancedLoc(length)};
  }
  ZC_UNREACHABLE;
}

bool hasSafeArguments(const BindingInputSourceFailure& failure) {
  switch (failure.diagnostic()) {
    case BindingInputDiagnostic::ImportMemberNotFound:
    case BindingInputDiagnostic::ReexportMemberNotFound:
      return failure.modulePath().size() != 0 && failure.memberName().text().size() != 0;
    case BindingInputDiagnostic::ImportTargetNotVisible:
    case BindingInputDiagnostic::ReexportTargetNotVisible:
      return true;
  }
  return false;
}

}  // namespace

bool canEmitBindingInputSourceFailure(const VerifiedParsedModule& parsedModule,
                                      const BindingInputSourceFailure& failure) {
  return hasSafeArguments(failure) && resolveFailureRange(parsedModule, failure) != zc::none;
}

bool emitBindingInputSourceFailure(diagnostics::DiagnosticEngine& diagnostics,
                                   const VerifiedParsedModule& parsedModule,
                                   const BindingInputSourceFailure& failure) {
  if (!canEmitBindingInputSourceFailure(parsedModule, failure)) { return false; }
  auto range = resolveFailureRange(parsedModule, failure);
  if (range == zc::none) { return false; }
  ZC_IF_SOME(value, range) {
    const auto sourceRange = source::CharSourceRange::getCharRange(value.start, value.end);
    using diagnostics::DiagID;
    switch (failure.diagnostic()) {
      case BindingInputDiagnostic::ImportMemberNotFound: {
        auto diagnostic = diagnostics.diagnose<DiagID::ImportMemberNotFound>(
            value.start, zc::str(failure.modulePath()), zc::str(failure.memberName().text()));
        diagnostic.addRange(sourceRange);
        diagnostic.emit();
        return true;
      }
      case BindingInputDiagnostic::ReexportMemberNotFound: {
        auto diagnostic = diagnostics.diagnose<DiagID::ReexportMemberNotFound>(
            value.start, zc::str(failure.modulePath()), zc::str(failure.memberName().text()));
        diagnostic.addRange(sourceRange);
        diagnostic.emit();
        return true;
      }
      case BindingInputDiagnostic::ImportTargetNotVisible: {
        auto diagnostic = diagnostics.diagnose<DiagID::ImportTargetNotVisible>(value.start);
        diagnostic.addRange(sourceRange);
        diagnostic.emit();
        return true;
      }
      case BindingInputDiagnostic::ReexportTargetNotVisible: {
        auto diagnostic = diagnostics.diagnose<DiagID::ReexportTargetNotVisible>(value.start);
        diagnostic.addRange(sourceRange);
        diagnostic.emit();
        return true;
      }
    }
  }
  ZC_UNREACHABLE;
}

}  // namespace zomlang::compiler::binder
