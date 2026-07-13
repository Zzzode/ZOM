// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/binding-diagnostic-adapter.h"

#include "zomlang/compiler/diagnostics/diagnostic-engine.h"

namespace zomlang::compiler::binder {
namespace {

template <diagnostics::DiagID Primary>
void emitTypedRedeclaration(diagnostics::DiagnosticEngine& diagnostics, source::SourceLoc primary,
                            source::SourceLoc previous, VerifiedIdentifierArgument&& identifier) {
  auto diagnostic = diagnostics.diagnose<Primary>(primary, zc::mv(identifier).take());
  diagnostic.addChild(
      zc::heap<diagnostics::Diagnostic>(diagnostics::DiagID::PreviousDeclarationHere, previous));
  diagnostic.emit();
}

}  // namespace

VerifiedIdentifierArgument::VerifiedIdentifierArgument(zc::String&& value) noexcept
    : value(zc::mv(value)) {}

VerifiedIdentifierArgument VerifiedIdentifierArgument::from(
    const identity::SemanticIdentifier& identifier) {
  return VerifiedIdentifierArgument(zc::str(identifier.text()));
}

zc::String VerifiedIdentifierArgument::take() && { return zc::mv(value); }

bool BindingDiagnosticAdapter::emitControlTransferFailure(
    diagnostics::DiagnosticEngine& diagnostics, BinderDiagnosticCode code,
    source::SourceLoc primary) {
  using diagnostics::DiagID;
  switch (code) {
    case BinderDiagnosticCode::BreakTargetNotFound:
      diagnostics.diagnose<DiagID::BreakTargetNotFound>(primary).emit();
      return true;
    case BinderDiagnosticCode::ContinueTargetNotFound:
      diagnostics.diagnose<DiagID::ContinueTargetNotFound>(primary).emit();
      return true;
    case BinderDiagnosticCode::UndefinedIdentifier:
    case BinderDiagnosticCode::SymbolNamespaceMismatch:
    case BinderDiagnosticCode::RedeclareVariable:
    case BinderDiagnosticCode::RedeclareParameter:
    case BinderDiagnosticCode::RedeclareFunction:
    case BinderDiagnosticCode::RedeclareClass:
    case BinderDiagnosticCode::RedeclareInterface:
    case BinderDiagnosticCode::RedeclareEnum:
    case BinderDiagnosticCode::RedeclareTypeAlias:
    case BinderDiagnosticCode::DuplicateIdentifier:
    case BinderDiagnosticCode::PreviousDeclarationHere:
      return false;
  }
  ZC_UNREACHABLE;
}

bool BindingDiagnosticAdapter::emitLookupFailure(diagnostics::DiagnosticEngine& diagnostics,
                                                 BinderDiagnosticCode code,
                                                 source::SourceLoc primary,
                                                 VerifiedIdentifierArgument&& identifier,
                                                 Namespace expectedNamespace) {
  using diagnostics::DiagID;
  switch (code) {
    case BinderDiagnosticCode::UndefinedIdentifier:
      diagnostics.diagnose<DiagID::UndefinedIdentifier>(primary, zc::mv(identifier).take()).emit();
      return true;
    case BinderDiagnosticCode::SymbolNamespaceMismatch: {
      const zc::StringPtr expected = expectedNamespace == Namespace::Value ? "value"_zc : "type"_zc;
      diagnostics
          .diagnose<DiagID::SymbolNamespaceMismatch>(primary, zc::mv(identifier).take(), expected)
          .emit();
      return true;
    }
    case BinderDiagnosticCode::RedeclareVariable:
    case BinderDiagnosticCode::RedeclareParameter:
    case BinderDiagnosticCode::RedeclareFunction:
    case BinderDiagnosticCode::RedeclareClass:
    case BinderDiagnosticCode::RedeclareInterface:
    case BinderDiagnosticCode::RedeclareEnum:
    case BinderDiagnosticCode::RedeclareTypeAlias:
    case BinderDiagnosticCode::DuplicateIdentifier:
    case BinderDiagnosticCode::PreviousDeclarationHere:
    case BinderDiagnosticCode::BreakTargetNotFound:
    case BinderDiagnosticCode::ContinueTargetNotFound:
      return false;
  }
  ZC_UNREACHABLE;
}

bool BindingDiagnosticAdapter::emitRedeclaration(diagnostics::DiagnosticEngine& diagnostics,
                                                 BinderDiagnosticCode code,
                                                 source::SourceLoc primary,
                                                 source::SourceLoc previous,
                                                 VerifiedIdentifierArgument&& identifier) {
  using diagnostics::DiagID;
  switch (code) {
    case BinderDiagnosticCode::RedeclareVariable:
      emitTypedRedeclaration<DiagID::RedeclareVariable>(diagnostics, primary, previous,
                                                        zc::mv(identifier));
      return true;
    case BinderDiagnosticCode::RedeclareParameter:
      emitTypedRedeclaration<DiagID::RedeclareParameter>(diagnostics, primary, previous,
                                                         zc::mv(identifier));
      return true;
    case BinderDiagnosticCode::RedeclareFunction:
      emitTypedRedeclaration<DiagID::RedeclareFunction>(diagnostics, primary, previous,
                                                        zc::mv(identifier));
      return true;
    case BinderDiagnosticCode::RedeclareClass:
      emitTypedRedeclaration<DiagID::RedeclareClass>(diagnostics, primary, previous,
                                                     zc::mv(identifier));
      return true;
    case BinderDiagnosticCode::RedeclareInterface:
      emitTypedRedeclaration<DiagID::RedeclareInterface>(diagnostics, primary, previous,
                                                         zc::mv(identifier));
      return true;
    case BinderDiagnosticCode::RedeclareEnum:
      emitTypedRedeclaration<DiagID::RedeclareEnum>(diagnostics, primary, previous,
                                                    zc::mv(identifier));
      return true;
    case BinderDiagnosticCode::RedeclareTypeAlias:
      emitTypedRedeclaration<DiagID::RedeclareTypeAlias>(diagnostics, primary, previous,
                                                         zc::mv(identifier));
      return true;
    case BinderDiagnosticCode::DuplicateIdentifier:
      emitTypedRedeclaration<DiagID::DuplicateIdentifier>(diagnostics, primary, previous,
                                                          zc::mv(identifier));
      return true;
    case BinderDiagnosticCode::UndefinedIdentifier:
    case BinderDiagnosticCode::SymbolNamespaceMismatch:
    case BinderDiagnosticCode::PreviousDeclarationHere:
    case BinderDiagnosticCode::BreakTargetNotFound:
    case BinderDiagnosticCode::ContinueTargetNotFound:
      return false;
  }
  ZC_UNREACHABLE;
}

}  // namespace zomlang::compiler::binder
