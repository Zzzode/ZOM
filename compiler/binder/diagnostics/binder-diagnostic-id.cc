// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "compiler/binder/diagnostics/binder-diagnostic-id.h"

namespace zomlang::compiler::binder {

zc::Maybe<BinderErrorId> BinderErrorId::fromDiagnosticId(
    diagnostics::DiagID diagnostic) noexcept {
  switch (diagnostic) {
#define BINDER_ERROR(Name)        \
  case diagnostics::DiagID::Name: \
    return BinderErrorId::Name();
#define BINDER_NOTE(Name)
#include "compiler/binder/binder-source-diagnostics.def"
#undef BINDER_NOTE
#undef BINDER_ERROR
    default:
      return zc::none;
  }
}

diagnostics::DiagID BinderErrorId::diagnosticId() const noexcept { return value; }

zc::Maybe<BinderNoteId> BinderNoteId::fromDiagnosticId(diagnostics::DiagID diagnostic) noexcept {
  switch (diagnostic) {
#define BINDER_ERROR(Name)
#define BINDER_NOTE(Name)         \
  case diagnostics::DiagID::Name: \
    return BinderNoteId::Name();
#include "compiler/binder/binder-source-diagnostics.def"
#undef BINDER_NOTE
#undef BINDER_ERROR
    default:
      return zc::none;
  }
}

diagnostics::DiagID BinderNoteId::diagnosticId() const noexcept { return value; }

}  // namespace zomlang::compiler::binder
