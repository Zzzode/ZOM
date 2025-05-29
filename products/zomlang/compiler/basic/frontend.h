// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "zc/core/memory.h"

namespace zomlang {
namespace compiler {

namespace source {
class SourceManager;
class BufferId;
}  // namespace source

namespace diagnostics {
class DiagnosticEngine;
}

namespace ast {
class Node;
}

namespace basic {

struct LangOptions;

/// \brief Perform lexing and parsing of a given source file.
/// \param sm The source manager.
/// \param diagnosticEngine The diagnostic engine.
/// \param langOpts The language options.
/// \param bufferId The buffer id of the source file.
/// \return The ASTNode if successful, zc::Nothing otherwise.
zc::Maybe<zc::Own<ast::Node>> performParse(const source::SourceManager& sm,
                                           diagnostics::DiagnosticEngine& diagnosticEngine,
                                           const LangOptions& langOpts,
                                           const source::BufferId& bufferId);

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang
