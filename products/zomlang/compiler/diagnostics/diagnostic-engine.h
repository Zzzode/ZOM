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

#include "zc/core/common.h"
#include "zc/core/io.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/diagnostics/in-flight-diagnostic.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {

namespace source {
class SourceManager;
class SourceLoc;
}  // namespace source

namespace diagnostics {

enum class DiagID : uint32_t;

class Diagnostic;
class DiagnosticConsumer;
class DiagnosticState;

class InFlightDiagnostic;

class DiagnosticEngine {
public:
  explicit DiagnosticEngine(source::SourceManager& sourceManager);
  ~DiagnosticEngine();

  ZC_DISALLOW_COPY_AND_MOVE(DiagnosticEngine);

  void addConsumer(zc::Own<DiagnosticConsumer> consumer);
  void emit(const Diagnostic& diagnostic);

  ZC_NODISCARD bool hasErrors() const;
  ZC_NODISCARD source::SourceManager& getSourceManager() const;
  ZC_NODISCARD const DiagnosticState& getState() const;

  DiagnosticState& getState();

  /// \brief Report a diagnostic at the given location.
  ///
  /// \param loc The location of the diagnostic.
  /// \param id The diagnostic ID.
  /// \param args The diagnostic arguments.
  /// \return The in-flight diagnostic.
  template <DiagID ID, typename... Args>
  InFlightDiagnostic diagnose(source::SourceLoc loc, Args&&... args) {
    static_assert(sizeof...(args) == DiagnosticTraits<ID>::argCount,
                  "Incorrect number of diagnostic arguments");
    return InFlightDiagnostic(*this, Diagnostic(ID, loc, zc::fwd<Args>(args)...));
  }

  static void formatDiagnosticMessage(const source::SourceManager& sm, zc::OutputStream& out,
                                      zc::StringPtr message,
                                      zc::ArrayPtr<const DiagnosticArgument> args);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang
