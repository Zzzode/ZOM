// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/diagnostics/diagnostic-materializer.h"

#include <climits>

#include "zc/core/debug.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::diagnostics {
namespace {

source::SourceLoc sourceLoc(source::SourceManager& sources, const source::BufferId& buffer,
                            uint64_t offset) {
  ZC_IREQUIRE(offset <= UINT_MAX, "diagnostic source offset exceeds SourceManager capacity");
  return sources.getLocForOffset(buffer, static_cast<unsigned>(offset));
}

source::CharSourceRange sourceRange(source::SourceManager& sources, const source::BufferId& buffer,
                                    const DiagnosticFactRange& range) {
  return source::CharSourceRange(sourceLoc(sources, buffer, range.byteStart),
                                 sourceLoc(sources, buffer, range.byteEnd), range.isTokenRange);
}

zc::Vector<DiagnosticArgument> materializeArguments(zc::ArrayPtr<const zc::String> arguments) {
  zc::Vector<DiagnosticArgument> result(arguments.size());
  for (const auto& argument : arguments) { result.add(zc::str(argument)); }
  return result;
}

zc::Own<Diagnostic> materializeSecondary(const SecondaryDiagnosticFact& fact,
                                         source::SourceManager& sources,
                                         const source::BufferId& buffer) {
  auto diagnostic =
      zc::heap<Diagnostic>(fact.code, sourceLoc(sources, buffer, fact.primaryByteOffset),
                           materializeArguments(fact.arguments.asPtr()));
  for (const auto& range : fact.ranges) {
    diagnostic->addRange(sourceRange(sources, buffer, range));
  }
  return diagnostic;
}

}  // namespace

void materializeDiagnosticFacts(zc::ArrayPtr<const DiagnosticFact> facts,
                                source::SourceManager& sources, const source::BufferId& buffer,
                                DiagnosticEngine& engine) {
  ZC_IREQUIRE(&engine.getSourceManager() == &sources,
              "diagnostic materialization requires the engine source manager");
  const uint64_t sourceByteLength ZC_UNUSED = sources.getEntireTextForBuffer(buffer).size();
  for (const auto& fact : facts) {
    ZC_IREQUIRE(fact.primaryByteOffset <= sourceByteLength,
                "diagnostic primary location is outside the source buffer");
    Diagnostic diagnostic(fact.code, sourceLoc(sources, buffer, fact.primaryByteOffset),
                          materializeArguments(fact.arguments.asPtr()));
    for (const auto& range : fact.ranges) {
      ZC_IREQUIRE(range.byteEnd <= sourceByteLength,
                  "diagnostic range is outside the source buffer");
      diagnostic.addRange(sourceRange(sources, buffer, range));
    }
    for (const auto& fixIt : fact.fixIts) {
      ZC_IREQUIRE(fixIt.range.byteEnd <= sourceByteLength,
                  "diagnostic fix-it is outside the source buffer");
      diagnostic.addFixIt(zc::heap<FixIt>(
          FixIt{sourceRange(sources, buffer, fixIt.range), zc::str(fixIt.replacementText)}));
    }
    for (const auto& child : fact.secondary) {
      ZC_IREQUIRE(child.primaryByteOffset <= sourceByteLength,
                  "secondary diagnostic location is outside the source buffer");
      diagnostic.addChildDiagnostic(materializeSecondary(child, sources, buffer));
    }
    engine.emit(diagnostic);
  }
}

}  // namespace zomlang::compiler::diagnostics
