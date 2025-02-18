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

namespace zomlang {
namespace compiler {

namespace source {
class SourceManager;
}

namespace diagnostics {

class Diagnostic;

class DiagnosticConsumer {
public:
  virtual ~DiagnosticConsumer() noexcept(false);

  virtual void handleDiagnostic(const source::SourceManager& sm, const Diagnostic& diagnostic) = 0;
};

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang
