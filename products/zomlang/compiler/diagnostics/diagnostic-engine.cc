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

#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/diagnostics/diagnostic-state.h"
#include "zomlang/compiler/source/location.h"

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
  // Check if this is an error-level diagnostic and update state
  const DiagnosticInfo& info = getDiagnosticInfo(diagnostic.getId());
  if (info.severity >= DiagSeverity::Error) { impl->state.setHadAnyError(); }

  for (auto& consumer : impl->consumers) {
    consumer->handleDiagnostic(impl->sourceManager, diagnostic);
  }
}

bool DiagnosticEngine::hasErrors() const { return impl->state.getHadAnyError(); }

source::SourceManager& DiagnosticEngine::getSourceManager() const { return impl->sourceManager; }

DiagnosticState& DiagnosticEngine::getState() { return impl->state; }

const DiagnosticState& DiagnosticEngine::getState() const { return impl->state; }

// static
void DiagnosticEngine::formatDiagnosticMessage(const source::SourceManager& sm,
                                               zc::OutputStream& out, zc::StringPtr format,
                                               zc::ArrayPtr<const DiagnosticArgument> args) {
  struct ParamPos {
    size_t start;
    size_t end;
    size_t index;
  };
  zc::Vector<ParamPos> params;

  // Preprocessing finds all parameter locations
  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] == '{' && i + 1 < format.size()) {
      size_t j = i + 1;
      while (j < format.size() && format[j] != '}') { ++j; }

      if (j < format.size() && j > i + 1) {
        size_t argIndex = 0;
        for (size_t k = i + 1; k < j; ++k) {
          ZC_IREQUIRE(format[k] >= '0' && format[k] <= '9', "Invalid parameter index format");
          argIndex = argIndex * 10 + (format[k] - '0');
        }

        ParamPos param{i, j, argIndex};
        params.add(zc::mv(param));
        i = j;  // Skip the current '}'
      }
    }
  }

  // Check the number and order of parameters
  ZC_IREQUIRE(params.size() == args.size(), "Parameter count mismatch");
  for (size_t i = 0; i < params.size(); ++i) {
    ZC_IREQUIRE(params[i].index == i, "Parameter indices must be consecutive starting from 0");
  }

  // Unified output
  size_t lastPos = 0;
  for (const auto& param : params) {
    // Output the text before the argument
    if (param.start > lastPos) { out.write(format.slice(lastPos, param.start).asBytes()); }

    // Output parameters
    const auto& arg = args[param.index];
    ZC_SWITCH_ONEOF(arg) {
      ZC_CASE_ONEOF(strPtr, zc::StringPtr) { out.write(strPtr.asBytes()); }
      ZC_CASE_ONEOF(str, zc::String) { out.write(str.asBytes()); }
      ZC_CASE_ONEOF(token, lexer::Token) {
        auto tokenText = token.getText(sm);
        out.write(tokenText.asBytes());
      }
    }
    lastPos = param.end + 1;
  }

  // Output the remaining text
  if (lastPos < format.size()) { out.write(format.slice(lastPos, format.size()).asBytes()); }
}

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang
