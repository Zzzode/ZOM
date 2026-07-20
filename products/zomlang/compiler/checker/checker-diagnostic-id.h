// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"

namespace zomlang::compiler::checker::checked {

/// \brief Private-construction registry identity for Checker source errors.
class CheckerErrorId final {
public:
#define CHECKER_ERROR(Name)                                      \
  ZC_NODISCARD static constexpr CheckerErrorId Name() noexcept { \
    return CheckerErrorId(diagnostics::DiagID::Name);            \
  }
#define CHECKER_WARNING(Name)
#define CHECKER_NOTE(Name)
#include "zomlang/compiler/checker/checker-source-diagnostics.def"
#undef CHECKER_NOTE
#undef CHECKER_WARNING
#undef CHECKER_ERROR

  ZC_NODISCARD static zc::Maybe<CheckerErrorId> fromDiagnosticId(
      diagnostics::DiagID diagnostic) noexcept;
  ZC_NODISCARD diagnostics::DiagID diagnosticId() const noexcept;
  ZC_NODISCARD constexpr bool operator==(CheckerErrorId other) const noexcept {
    return value == other.value;
  }
  ZC_NODISCARD constexpr bool operator!=(CheckerErrorId other) const noexcept {
    return !(*this == other);
  }

private:
  explicit constexpr CheckerErrorId(diagnostics::DiagID value) noexcept : value(value) {}
  diagnostics::DiagID value;
};

/// \brief Private-construction registry identity for Checker source warnings.
class CheckerWarningId final {
public:
#define CHECKER_ERROR(Name)
#define CHECKER_WARNING(Name)                                      \
  ZC_NODISCARD static constexpr CheckerWarningId Name() noexcept { \
    return CheckerWarningId(diagnostics::DiagID::Name);            \
  }
#define CHECKER_NOTE(Name)
#include "zomlang/compiler/checker/checker-source-diagnostics.def"
#undef CHECKER_NOTE
#undef CHECKER_WARNING
#undef CHECKER_ERROR

  ZC_NODISCARD static zc::Maybe<CheckerWarningId> fromDiagnosticId(
      diagnostics::DiagID diagnostic) noexcept;
  ZC_NODISCARD diagnostics::DiagID diagnosticId() const noexcept;
  ZC_NODISCARD constexpr bool operator==(CheckerWarningId other) const noexcept {
    return value == other.value;
  }

private:
  explicit constexpr CheckerWarningId(diagnostics::DiagID value) noexcept : value(value) {}
  diagnostics::DiagID value;
};

/// \brief Private-construction registry identity for RFC 0005 attached notes.
class CheckerNoteId final {
public:
#define CHECKER_ERROR(Name)
#define CHECKER_WARNING(Name)
#define CHECKER_NOTE(Name)                                      \
  ZC_NODISCARD static constexpr CheckerNoteId Name() noexcept { \
    return CheckerNoteId(diagnostics::DiagID::Name);            \
  }
#include "zomlang/compiler/checker/checker-source-diagnostics.def"
#undef CHECKER_NOTE
#undef CHECKER_WARNING
#undef CHECKER_ERROR

  ZC_NODISCARD static zc::Maybe<CheckerNoteId> fromDiagnosticId(
      diagnostics::DiagID diagnostic) noexcept;
  ZC_NODISCARD diagnostics::DiagID diagnosticId() const noexcept;
  ZC_NODISCARD constexpr bool operator==(CheckerNoteId other) const noexcept {
    return value == other.value;
  }

private:
  explicit constexpr CheckerNoteId(diagnostics::DiagID value) noexcept : value(value) {}
  diagnostics::DiagID value;
};

}  // namespace zomlang::compiler::checker::checked
