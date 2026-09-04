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

#pragma once

#include "compiler/diagnostics/core/diagnostic-ids.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"

namespace zomlang::compiler::binder {

/// \brief Private-construction registry identity for binder source errors.
///
/// The constructor is private and the only factories are generated from
/// `binder-source-diagnostics.def`, so binder code cannot name a diagnostic
/// outside the closed set the binder is allowed to report. The wrapped value is
/// the registry `DiagID` itself: there is no second numbering to keep in sync.
class BinderErrorId final {
public:
#define BINDER_ERROR(Name)                                     \
  ZC_NODISCARD static constexpr BinderErrorId Name() noexcept { \
    return BinderErrorId(diagnostics::DiagID::Name);            \
  }
#define BINDER_NOTE(Name)
#include "compiler/binder/binder-source-diagnostics.def"
#undef BINDER_NOTE
#undef BINDER_ERROR

  ZC_NODISCARD static zc::Maybe<BinderErrorId> fromDiagnosticId(
      diagnostics::DiagID diagnostic) noexcept;
  ZC_NODISCARD diagnostics::DiagID diagnosticId() const noexcept;
  ZC_NODISCARD constexpr bool operator==(BinderErrorId other) const noexcept {
    return value == other.value;
  }
  ZC_NODISCARD constexpr bool operator!=(BinderErrorId other) const noexcept {
    return !(*this == other);
  }

private:
  explicit constexpr BinderErrorId(diagnostics::DiagID value) noexcept : value(value) {}
  diagnostics::DiagID value;
};

/// \brief Private-construction registry identity for binder source notes.
class BinderNoteId final {
public:
#define BINDER_ERROR(Name)
#define BINDER_NOTE(Name)                                     \
  ZC_NODISCARD static constexpr BinderNoteId Name() noexcept { \
    return BinderNoteId(diagnostics::DiagID::Name);            \
  }
#include "compiler/binder/binder-source-diagnostics.def"
#undef BINDER_NOTE
#undef BINDER_ERROR

  ZC_NODISCARD static zc::Maybe<BinderNoteId> fromDiagnosticId(
      diagnostics::DiagID diagnostic) noexcept;
  ZC_NODISCARD diagnostics::DiagID diagnosticId() const noexcept;
  ZC_NODISCARD constexpr bool operator==(BinderNoteId other) const noexcept {
    return value == other.value;
  }
  ZC_NODISCARD constexpr bool operator!=(BinderNoteId other) const noexcept {
    return !(*this == other);
  }

private:
  explicit constexpr BinderNoteId(diagnostics::DiagID value) noexcept : value(value) {}
  diagnostics::DiagID value;
};

}  // namespace zomlang::compiler::binder
