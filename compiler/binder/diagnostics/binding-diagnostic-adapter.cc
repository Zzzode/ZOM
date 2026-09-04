// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/binder/diagnostics/binding-diagnostic-adapter.h"

#include "compiler/diagnostics/core/diagnostic-engine.h"

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

VerifiedIdentifierArgument VerifiedIdentifierArgument::from(
    const identity::DeclaredDefinitionName& identifier) {
  return VerifiedIdentifierArgument(zc::str(identifier.text()));
}

zc::String VerifiedIdentifierArgument::take() && { return zc::mv(value); }

bool BindingDiagnosticAdapter::emitControlTransferFailure(
    diagnostics::DiagnosticEngine& diagnostics, BinderErrorId code, source::SourceLoc primary) {
  using diagnostics::DiagID;
  switch (code.diagnosticId()) {
    case DiagID::BreakTargetNotFound:
      diagnostics.diagnose<DiagID::BreakTargetNotFound>(primary).emit();
      return true;
    case DiagID::ContinueTargetNotFound:
      diagnostics.diagnose<DiagID::ContinueTargetNotFound>(primary).emit();
      return true;
    case DiagID::ContinueTargetNotLoop:
      diagnostics.diagnose<DiagID::ContinueTargetNotLoop>(primary).emit();
      return true;
    default:
      return false;
  }
  ZC_UNREACHABLE;
}

bool BindingDiagnosticAdapter::emitLabelLookupFailure(diagnostics::DiagnosticEngine& diagnostics,
                                                      BinderErrorId code, source::SourceLoc primary,
                                                      VerifiedIdentifierArgument&& identifier) {
  using diagnostics::DiagID;
  switch (code.diagnosticId()) {
    case DiagID::UndefinedIdentifier:
      diagnostics.diagnose<DiagID::UndefinedIdentifier>(primary, zc::mv(identifier).take()).emit();
      return true;
    default:
      return false;
  }
  ZC_UNREACHABLE;
}

bool BindingDiagnosticAdapter::emitLookupFailure(diagnostics::DiagnosticEngine& diagnostics,
                                                 BinderErrorId code, source::SourceLoc primary,
                                                 VerifiedIdentifierArgument&& identifier,
                                                 Namespace expectedNamespace) {
  using diagnostics::DiagID;
  switch (code.diagnosticId()) {
    case DiagID::UndefinedIdentifier:
      diagnostics.diagnose<DiagID::UndefinedIdentifier>(primary, zc::mv(identifier).take()).emit();
      return true;
    case DiagID::SymbolNamespaceMismatch: {
      const zc::StringPtr expected = expectedNamespace == Namespace::Value ? "value"_zc : "type"_zc;
      diagnostics
          .diagnose<DiagID::SymbolNamespaceMismatch>(primary, zc::mv(identifier).take(), expected)
          .emit();
      return true;
    }
    case DiagID::ContextualSelfOutsideType:
      diagnostics.diagnose<DiagID::ContextualSelfOutsideType>(primary).emit();
      return true;
    default:
      return false;
  }
  ZC_UNREACHABLE;
}

bool BindingDiagnosticAdapter::emitRedeclaration(diagnostics::DiagnosticEngine& diagnostics,
                                                 BinderErrorId code, source::SourceLoc primary,
                                                 source::SourceLoc previous,
                                                 VerifiedIdentifierArgument&& identifier) {
  using diagnostics::DiagID;
  switch (code.diagnosticId()) {
    case DiagID::RedeclareVariable:
      emitTypedRedeclaration<DiagID::RedeclareVariable>(diagnostics, primary, previous,
                                                        zc::mv(identifier));
      return true;
    case DiagID::RedeclareParameter:
      emitTypedRedeclaration<DiagID::RedeclareParameter>(diagnostics, primary, previous,
                                                         zc::mv(identifier));
      return true;
    case DiagID::RedeclareFunction:
      emitTypedRedeclaration<DiagID::RedeclareFunction>(diagnostics, primary, previous,
                                                        zc::mv(identifier));
      return true;
    case DiagID::RedeclareClass:
      emitTypedRedeclaration<DiagID::RedeclareClass>(diagnostics, primary, previous,
                                                     zc::mv(identifier));
      return true;
    case DiagID::RedeclareInterface:
      emitTypedRedeclaration<DiagID::RedeclareInterface>(diagnostics, primary, previous,
                                                         zc::mv(identifier));
      return true;
    case DiagID::RedeclareEnum:
      emitTypedRedeclaration<DiagID::RedeclareEnum>(diagnostics, primary, previous,
                                                    zc::mv(identifier));
      return true;
    case DiagID::RedeclareTypeAlias:
      emitTypedRedeclaration<DiagID::RedeclareTypeAlias>(diagnostics, primary, previous,
                                                         zc::mv(identifier));
      return true;
    case DiagID::DuplicateIdentifier:
      emitTypedRedeclaration<DiagID::DuplicateIdentifier>(diagnostics, primary, previous,
                                                          zc::mv(identifier));
      return true;
    default:
      return false;
  }
  ZC_UNREACHABLE;
}

}  // namespace zomlang::compiler::binder
