// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/source-location.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-info.h"
#include "zomlang/compiler/diagnostics/core/diagnostic.h"
#include "zomlang/compiler/diagnostics/consumer/in-flight-diagnostic.h"

namespace zomlang::compiler::diagnostics {

/// \brief Common RAII diagnostic emission surface with stable caller provenance.
class DiagnosticEmitter {
public:
  virtual ~DiagnosticEmitter() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(DiagnosticEmitter);

  virtual void emitDiagnostic(const Diagnostic& diagnostic, zc::SourceLocation emitter) = 0;

  template <DiagID ID>
  InFlightDiagnostic diagnose(source::SourceLoc loc, zc::SourceLocation emitter = {}) {
    static_assert(DiagnosticTraits<ID>::argCount == 0, "Incorrect number of diagnostic arguments");
    return InFlightDiagnostic(*this, Diagnostic(ID, loc), emitter);
  }

  template <DiagID ID, typename Arg0>
  InFlightDiagnostic diagnose(source::SourceLoc loc, Arg0&& arg0,
                              zc::SourceLocation emitter = {}) {
    static_assert(DiagnosticTraits<ID>::argCount == 1, "Incorrect number of diagnostic arguments");
    return InFlightDiagnostic(*this, Diagnostic(ID, loc, zc::fwd<Arg0>(arg0)), emitter);
  }

  template <DiagID ID, typename Arg0, typename Arg1>
  InFlightDiagnostic diagnose(source::SourceLoc loc, Arg0&& arg0, Arg1&& arg1,
                              zc::SourceLocation emitter = {}) {
    static_assert(DiagnosticTraits<ID>::argCount == 2, "Incorrect number of diagnostic arguments");
    return InFlightDiagnostic(
        *this, Diagnostic(ID, loc, zc::fwd<Arg0>(arg0), zc::fwd<Arg1>(arg1)), emitter);
  }

  template <DiagID ID, typename Arg0, typename Arg1, typename Arg2>
  InFlightDiagnostic diagnose(source::SourceLoc loc, Arg0&& arg0, Arg1&& arg1, Arg2&& arg2,
                              zc::SourceLocation emitter = {}) {
    static_assert(DiagnosticTraits<ID>::argCount == 3, "Incorrect number of diagnostic arguments");
    return InFlightDiagnostic(*this,
                              Diagnostic(ID, loc, zc::fwd<Arg0>(arg0), zc::fwd<Arg1>(arg1),
                                         zc::fwd<Arg2>(arg2)),
                              emitter);
  }

protected:
  DiagnosticEmitter() = default;
};

}  // namespace zomlang::compiler::diagnostics
