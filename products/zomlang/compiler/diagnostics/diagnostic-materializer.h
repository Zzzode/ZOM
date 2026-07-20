// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zomlang/compiler/diagnostics/diagnostic-fact.h"

namespace zomlang::compiler {
namespace source {
class BufferId;
class SourceManager;
}  // namespace source

namespace diagnostics {

class DiagnosticEngine;

/// \brief Materializes detached facts exactly once into a session diagnostic engine.
void materializeDiagnosticFacts(zc::ArrayPtr<const DiagnosticFact> facts,
                                source::SourceManager& sources, const source::BufferId& buffer,
                                DiagnosticEngine& engine);

}  // namespace diagnostics
}  // namespace zomlang::compiler
