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

#include "zomlang/compiler/diagnostics/diagnostic-engine.h"

#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-state.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

struct DiagnosticEngine::Impl {
  explicit Impl(source::SourceManager& sm) : sourceManager(sm) {}

  source::SourceManager& sourceManager;
  zc::Vector<zc::Own<DiagnosticConsumer>> consumers;
  DiagnosticState state;
};

DiagnosticEngine::DiagnosticEngine(source::SourceManager& sourceManager)
    : impl(zc::heap<Impl>(sourceManager)) {}
DiagnosticEngine::~DiagnosticEngine() = default;

void DiagnosticEngine::addConsumer(zc::Own<DiagnosticConsumer> consumer) {
  impl->consumers.add(zc::mv(consumer));
}

void DiagnosticEngine::emit(const Diagnostic& diagnostic) {
  for (auto& consumer : impl->consumers) { consumer->handleDiagnostic(diagnostic); }
}

bool DiagnosticEngine::hasErrors() const { return impl->state.getHadAnyError(); }

source::SourceManager& DiagnosticEngine::getSourceManager() const { return impl->sourceManager; }

DiagnosticState& DiagnosticEngine::getState() { return impl->state; }

const DiagnosticState& DiagnosticEngine::getState() const { return impl->state; }

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang