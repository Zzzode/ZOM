// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/diagnostics/core/diagnostic-info.h"
#include "compiler/source/location.h"
#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::diagnostics {

/// \brief Stable handle for adding structured data to one retained source draft.
class SourceDiagnosticDraftHandle final {
public:
  constexpr SourceDiagnosticDraftHandle() noexcept = default;
  ZC_NODISCARD constexpr bool isValid() const noexcept { return token != 0; }

private:
  constexpr explicit SourceDiagnosticDraftHandle(uint64_t token) noexcept : token(token) {}
  uint64_t token = 0;

  friend class SourceDiagnosticSink;
};

/// \brief Typed source-diagnostic construction boundary for lexer and parser producers.
class SourceDiagnosticSink {
public:
  virtual ~SourceDiagnosticSink() noexcept(false) = default;
  ZC_DISALLOW_COPY_AND_MOVE(SourceDiagnosticSink);

  template <DiagID ID, typename... Args>
  SourceDiagnosticDraftHandle report(source::SourceLoc primary, Args&&... arguments) {
    static_assert(DiagnosticTraits<ID>::argCount == sizeof...(Args),
                  "incorrect number of diagnostic arguments");
    zc::Vector<zc::String> retained(sizeof...(Args));
    (retained.add(zc::str(zc::fwd<Args>(arguments))), ...);
    return append(ID, primary, zc::mv(retained));
  }

  virtual void addHighlight(SourceDiagnosticDraftHandle draft,
                            const source::CharSourceRange& range) = 0;

  template <DiagID ID, typename... Args>
  void addNote(SourceDiagnosticDraftHandle draft, source::SourceLoc primary, Args&&... arguments) {
    static_assert(DiagnosticTraits<ID>::argCount == sizeof...(Args),
                  "incorrect number of diagnostic arguments");
    zc::Vector<zc::String> retained(sizeof...(Args));
    (retained.add(zc::str(zc::fwd<Args>(arguments))), ...);
    appendNote(draft, ID, primary, zc::mv(retained));
  }

protected:
  SourceDiagnosticSink() = default;
  ZC_NODISCARD static constexpr SourceDiagnosticDraftHandle makeHandle(uint64_t token) noexcept {
    return SourceDiagnosticDraftHandle(token);
  }
  ZC_NODISCARD static constexpr uint64_t handleToken(SourceDiagnosticDraftHandle handle) noexcept {
    return handle.token;
  }

  ZC_NODISCARD virtual SourceDiagnosticDraftHandle append(DiagID code, source::SourceLoc primary,
                                                          zc::Vector<zc::String>&& arguments) = 0;
  virtual void appendNote(SourceDiagnosticDraftHandle draft, DiagID code, source::SourceLoc primary,
                          zc::Vector<zc::String>&& arguments) = 0;
};

}  // namespace zomlang::compiler::diagnostics
