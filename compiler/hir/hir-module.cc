// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/hir-module.h"

#include <cstdint>

#include "compiler/hir/hir-candidate-impl.h"
#include "compiler/hir/hir-internal.h"
#include "compiler/hir/hir-shape.h"
#include "compiler/ownership/admission/surface-admission.h"
#include "zc/core/encoding.h"

namespace zomlang::compiler::hir {
using namespace detail;
namespace {

void append(zc::Vector<char>& output, zc::StringPtr text) { output.addAll(text); }

void appendDigest(zc::Vector<char>& output, const identity::Sha256Digest& digest) {
  append(output, zc::encodeHex(digest.bytes()));
}

void appendInterfaceRevision(zc::Vector<char>& output,
                             const module_interface::ImportedInterfaceRevision& revision) {
  const auto& value = revision.variant();
  if (value.is<module_interface::UserImportedInterfaceRevision>()) {
    append(output, "user:"_zc);
    appendDigest(output,
                 value.get<module_interface::UserImportedInterfaceRevision>().value.digest());
    return;
  }
  append(output, "core:"_zc);
  appendDigest(
      output, value.get<module_interface::ToolchainCoreImportedInterfaceRevision>().value.digest());
}

}  // namespace

HirVisibility HirVisibility::module(identity::ModuleId module) noexcept {
  return HirVisibility(ModuleHirVisibility{module});
}

HirVisibility HirVisibility::external() noexcept { return HirVisibility(ExternalHirVisibility{}); }

HirVisibility HirVisibility::clone() const noexcept {
  if (value.is<ModuleHirVisibility>()) { return module(value.get<ModuleHirVisibility>().module); }
  return external();
}

HirVisibilityKind HirVisibility::kind() const noexcept {
  return value.is<ModuleHirVisibility>() ? HirVisibilityKind::Module : HirVisibilityKind::External;
}

zc::Maybe<identity::ModuleId> HirVisibility::visibleModule() const noexcept {
  if (!value.is<ModuleHirVisibility>()) return zc::none;
  return value.get<ModuleHirVisibility>().module;
}

struct VerifiedHirModule::Impl final {
  Impl(VerifiedCheckedModule&& admittedCheckedModule, ownership::AdmittedBoundModule&& boundModule,
       checker::CheckerIdentityAuthority&& identities,
       const checker::checked::CheckedFactsRepository& checkedRepository,
       driver::borrow_evidence::BorrowEvidenceRepositoryCapability&& borrowEvidenceCapability,
       const type::SemanticTypeStore& semanticTypes, zc::Vector<HirValueDeclaration>&& declarations,
       zc::Vector<HirFunctionDeclaration>&& functions, zc::Vector<HirBlockStatement>&& blocks,
       zc::Vector<HirReturnStatement>&& returns, zc::Vector<HirBindingPattern>&& patterns,
       zc::Vector<HirScalarLiteralExpression>&& expressions,
       zc::Vector<HirNominalAggregateExpression>&& aggregates, zc::Vector<HirLocalBinding>&& locals,
       zc::Vector<HirLocalWriteStatement>&& localWrites,
       zc::Vector<HirLocalReferenceExpression>&& localReferences,
       zc::Vector<HirLocalFieldProjectionExpression>&& localFieldProjections,
       zc::Vector<HirParameterFieldProjectionExpression>&& parameterFieldProjections,
       zc::Vector<HirParameterFieldWriteStatement>&& parameterFieldWrites,
       zc::Vector<HirParameterReferenceExpression>&& parameterReferences,
       zc::Vector<HirParameterIndexExpression>&& parameterIndexes,
       zc::Vector<HirParameterReborrowExpression>&& parameterReborrows,
       zc::Vector<HirLocalBorrowExpression>&& localBorrows,
       zc::Vector<HirDirectCallExpression>&& calls,
       zc::Vector<HirReceiverCallExpression>&& receiverCalls,
       zc::Vector<HirUnsafeBlockExpression>&& unsafeBlocks,
       zc::Vector<HirPrimitiveBinaryExpression>&& primitiveBinaryOperations,
       zc::Vector<HirConditionalExpression>&& conditionals,
       zc::Vector<HirLoopStatement>&& loops) noexcept
      : admittedCheckedModule(zc::mv(admittedCheckedModule)),
        boundModule(zc::mv(boundModule)),
        identities(zc::mv(identities)),
        semanticContext(this->admittedCheckedModule.semanticContext()),
        contextFingerprint(this->admittedCheckedModule.contextFingerprint().clone()),
        compilationUnit(this->admittedCheckedModule.compilationUnit()),
        crate(this->admittedCheckedModule.crate()),
        module(this->admittedCheckedModule.module()),
        sourceContentDigest(this->admittedCheckedModule.sourceContentDigest()),
        parsedModuleReceipt(this->admittedCheckedModule.parsedModuleReceipt().digest()),
        checkedFactsRevision(this->admittedCheckedModule.checkedFactsRevision()),
        dispatchFactsRevision(this->admittedCheckedModule.dispatchFactsRevision()),
        borrowEvidenceRevision(this->admittedCheckedModule.borrowEvidenceRevision()),
        ownInterface(
            ModuleInterfaceLineage{this->admittedCheckedModule.ownInterface().module,
                                   this->admittedCheckedModule.ownInterface().revision.clone()}),
        checkedRepository(checkedRepository),
        borrowEvidenceCapability(zc::mv(borrowEvidenceCapability)),
        semanticTypes(semanticTypes),
        declarations(zc::mv(declarations)),
        functions(zc::mv(functions)),
        blocks(zc::mv(blocks)),
        returns(zc::mv(returns)),
        patterns(zc::mv(patterns)),
        expressions(zc::mv(expressions)),
        aggregates(zc::mv(aggregates)),
        locals(zc::mv(locals)),
        localWrites(zc::mv(localWrites)),
        localReferences(zc::mv(localReferences)),
        localFieldProjections(zc::mv(localFieldProjections)),
        parameterFieldProjections(zc::mv(parameterFieldProjections)),
        parameterFieldWrites(zc::mv(parameterFieldWrites)),
        parameterReferences(zc::mv(parameterReferences)),
        parameterIndexes(zc::mv(parameterIndexes)),
        parameterReborrows(zc::mv(parameterReborrows)),
        localBorrows(zc::mv(localBorrows)),
        calls(zc::mv(calls)),
        receiverCalls(zc::mv(receiverCalls)),
        unsafeBlocks(zc::mv(unsafeBlocks)),
        primitiveBinaryOperations(zc::mv(primitiveBinaryOperations)),
        conditionals(zc::mv(conditionals)),
        loops(zc::mv(loops)) {
    for (const auto& interface : this->admittedCheckedModule.visibleImportedInterfaces()) {
      visibleImportedInterfaces.add(
          ModuleInterfaceLineage{interface.module, interface.revision.clone()});
    }
  }

  VerifiedCheckedModule admittedCheckedModule;
  ownership::AdmittedBoundModule boundModule;
  checker::CheckerIdentityAuthority identities;
  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::CompilationUnitId compilationUnit;
  identity::CrateId crate;
  identity::ModuleId module;
  identity::Sha256Digest sourceContentDigest;
  identity::Sha256Digest parsedModuleReceipt;
  checker::checked::CheckedFactsRevision checkedFactsRevision;
  checker::dispatch::DispatchFactsRevision dispatchFactsRevision;
  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision;
  ModuleInterfaceLineage ownInterface;
  zc::Vector<ModuleInterfaceLineage> visibleImportedInterfaces;
  const checker::checked::CheckedFactsRepository& checkedRepository;
  driver::borrow_evidence::BorrowEvidenceRepositoryCapability borrowEvidenceCapability;
  const type::SemanticTypeStore& semanticTypes;
  zc::Vector<HirValueDeclaration> declarations;
  zc::Vector<HirFunctionDeclaration> functions;
  zc::Vector<HirBlockStatement> blocks;
  zc::Vector<HirReturnStatement> returns;
  zc::Vector<HirBindingPattern> patterns;
  zc::Vector<HirScalarLiteralExpression> expressions;
  zc::Vector<HirNominalAggregateExpression> aggregates;
  zc::Vector<HirLocalBinding> locals;
  zc::Vector<HirLocalWriteStatement> localWrites;
  zc::Vector<HirLocalReferenceExpression> localReferences;
  zc::Vector<HirLocalFieldProjectionExpression> localFieldProjections;
  zc::Vector<HirParameterFieldProjectionExpression> parameterFieldProjections;
  zc::Vector<HirParameterFieldWriteStatement> parameterFieldWrites;
  zc::Vector<HirParameterReferenceExpression> parameterReferences;
  zc::Vector<HirParameterIndexExpression> parameterIndexes;
  zc::Vector<HirParameterReborrowExpression> parameterReborrows;
  zc::Vector<HirLocalBorrowExpression> localBorrows;
  zc::Vector<HirDirectCallExpression> calls;
  zc::Vector<HirReceiverCallExpression> receiverCalls;
  zc::Vector<HirUnsafeBlockExpression> unsafeBlocks;
  zc::Vector<HirPrimitiveBinaryExpression> primitiveBinaryOperations;
  zc::Vector<HirConditionalExpression> conditionals;
  zc::Vector<HirLoopStatement> loops;
};

VerifiedHirModule::VerifiedHirModule(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedHirModule::~VerifiedHirModule() noexcept(false) = default;
VerifiedHirModule::VerifiedHirModule(VerifiedHirModule&&) noexcept = default;
VerifiedHirModule& VerifiedHirModule::operator=(VerifiedHirModule&&) noexcept = default;

identity::SemanticContextBrand VerifiedHirModule::semanticContext() const noexcept {
  return impl->semanticContext;
}

const identity::ContextFingerprint& VerifiedHirModule::contextFingerprint() const noexcept {
  return impl->contextFingerprint;
}

identity::CompilationUnitId VerifiedHirModule::compilationUnit() const noexcept {
  return impl->compilationUnit;
}
identity::CrateId VerifiedHirModule::crate() const noexcept { return impl->crate; }
identity::ModuleId VerifiedHirModule::module() const noexcept { return impl->module; }

const identity::Sha256Digest& VerifiedHirModule::sourceContentDigest() const noexcept {
  return impl->sourceContentDigest;
}

const identity::Sha256Digest& VerifiedHirModule::parsedModuleReceiptDigest() const noexcept {
  return impl->parsedModuleReceipt;
}

const checker::checked::CheckedFactsRevision& VerifiedHirModule::checkedFactsRevision()
    const noexcept {
  return impl->checkedFactsRevision;
}

const checker::dispatch::DispatchFactsRevision& VerifiedHirModule::dispatchFactsRevision()
    const noexcept {
  return impl->dispatchFactsRevision;
}

const driver::borrow_evidence::BorrowEvidenceRevision& VerifiedHirModule::borrowEvidenceRevision()
    const noexcept {
  return impl->borrowEvidenceRevision;
}

const ModuleInterfaceLineage& VerifiedHirModule::ownInterface() const noexcept {
  return impl->ownInterface;
}

zc::ArrayPtr<const ModuleInterfaceLineage> VerifiedHirModule::visibleImportedInterfaces()
    const noexcept {
  return impl->visibleImportedInterfaces.asPtr();
}

ownership::AdmittedBoundModule VerifiedHirModule::retainAdmittedBoundModule() const {
  return impl->boundModule.retain();
}

checker::CheckerIdentityAuthority VerifiedHirModule::retainIdentityAuthority() const {
  return impl->identities.clone();
}

const checker::checked::CheckedEvidenceLease& VerifiedHirModule::checkedEvidenceLease()
    const noexcept {
  return impl->admittedCheckedModule.checkedEvidenceLease();
}

const driver::borrow_evidence::VerifiedBorrowEvidenceLease& VerifiedHirModule::borrowEvidenceLease()
    const noexcept {
  return impl->admittedCheckedModule.borrowEvidenceLease();
}

const VerifiedCheckedModule& VerifiedHirModule::admittedCheckedModule() const noexcept {
  return impl->admittedCheckedModule;
}

driver::borrow_evidence::BorrowEvidenceRepositoryCapability
VerifiedHirModule::borrowEvidenceCapability() const noexcept {
  return impl->borrowEvidenceCapability.clone();
}

const type::SemanticTypeStore& VerifiedHirModule::semanticTypes() const noexcept {
  return impl->semanticTypes;
}

zc::ArrayPtr<const HirValueDeclaration> VerifiedHirModule::declarations() const noexcept {
  return impl->declarations.asPtr();
}

zc::ArrayPtr<const HirFunctionDeclaration> VerifiedHirModule::functions() const noexcept {
  return impl->functions.asPtr();
}

zc::ArrayPtr<const HirBlockStatement> VerifiedHirModule::blocks() const noexcept {
  return impl->blocks.asPtr();
}

zc::ArrayPtr<const HirReturnStatement> VerifiedHirModule::returns() const noexcept {
  return impl->returns.asPtr();
}

zc::ArrayPtr<const HirBindingPattern> VerifiedHirModule::patterns() const noexcept {
  return impl->patterns.asPtr();
}

zc::ArrayPtr<const HirScalarLiteralExpression> VerifiedHirModule::expressions() const noexcept {
  return impl->expressions.asPtr();
}

zc::ArrayPtr<const HirNominalAggregateExpression> VerifiedHirModule::aggregates() const noexcept {
  return impl->aggregates.asPtr();
}

zc::ArrayPtr<const HirLocalBinding> VerifiedHirModule::locals() const noexcept {
  return impl->locals.asPtr();
}

zc::ArrayPtr<const HirLocalWriteStatement> VerifiedHirModule::localWrites() const noexcept {
  return impl->localWrites.asPtr();
}

zc::ArrayPtr<const HirLocalReferenceExpression> VerifiedHirModule::localReferences()
    const noexcept {
  return impl->localReferences.asPtr();
}

zc::ArrayPtr<const HirLocalFieldProjectionExpression> VerifiedHirModule::localFieldProjections()
    const noexcept {
  return impl->localFieldProjections.asPtr();
}

zc::ArrayPtr<const HirParameterFieldProjectionExpression>
VerifiedHirModule::parameterFieldProjections() const noexcept {
  return impl->parameterFieldProjections.asPtr();
}

zc::ArrayPtr<const HirParameterFieldWriteStatement> VerifiedHirModule::parameterFieldWrites()
    const noexcept {
  return impl->parameterFieldWrites.asPtr();
}

zc::ArrayPtr<const HirParameterReferenceExpression> VerifiedHirModule::parameterReferences()
    const noexcept {
  return impl->parameterReferences.asPtr();
}

zc::ArrayPtr<const HirParameterIndexExpression> VerifiedHirModule::parameterIndexes()
    const noexcept {
  return impl->parameterIndexes.asPtr();
}

zc::ArrayPtr<const HirParameterReborrowExpression> VerifiedHirModule::parameterReborrows()
    const noexcept {
  return impl->parameterReborrows.asPtr();
}

zc::ArrayPtr<const HirLocalBorrowExpression> VerifiedHirModule::localBorrows() const noexcept {
  return impl->localBorrows.asPtr();
}

zc::ArrayPtr<const HirDirectCallExpression> VerifiedHirModule::calls() const noexcept {
  return impl->calls.asPtr();
}

zc::ArrayPtr<const HirReceiverCallExpression> VerifiedHirModule::receiverCalls() const noexcept {
  return impl->receiverCalls.asPtr();
}

zc::ArrayPtr<const HirUnsafeBlockExpression> VerifiedHirModule::unsafeBlocks() const noexcept {
  return impl->unsafeBlocks.asPtr();
}

zc::ArrayPtr<const HirConditionalExpression> VerifiedHirModule::conditionals() const noexcept {
  return impl->conditionals.asPtr();
}

zc::ArrayPtr<const HirPrimitiveBinaryExpression> VerifiedHirModule::primitiveBinaryOperations()
    const noexcept {
  return impl->primitiveBinaryOperations.asPtr();
}

zc::ArrayPtr<const HirLoopStatement> VerifiedHirModule::loops() const noexcept {
  return impl->loops.asPtr();
}

zc::Maybe<zc::String> VerifiedHirModule::dump() const {
  auto moduleKey = impl->identities.module(impl->module);
  const auto& checkedLease = impl->admittedCheckedModule.checkedEvidenceLease();
  const auto& borrowLease = impl->admittedCheckedModule.borrowEvidenceLease();
  const auto borrowCapability = borrowEvidenceCapability();
  const auto borrowEvidence = borrowCapability.lookup(borrowLease);
  if (moduleKey == zc::none || impl->checkedRepository.lookup(checkedLease) == zc::none ||
      !borrowEvidence.isResolved() ||
      borrowEvidence.evidence().revision().digest() != impl->borrowEvidenceRevision.digest() ||
      borrowLease.key().revision.digest() != impl->borrowEvidenceRevision.digest()) {
    return zc::none;
  }
  zc::Vector<char> output;
  append(output, "zom.hir\nmodule "_zc);
  ZC_IF_SOME(key, moduleKey) { append(output, zc::encodeHex(key.key().encode().asPtr())); }
  append(output, "\ncontext "_zc);
  appendDigest(output, impl->contextFingerprint.digest());
  append(output, "\nchecked "_zc);
  appendDigest(output, impl->checkedFactsRevision.digest());
  append(output, "\nsource "_zc);
  appendDigest(output, impl->sourceContentDigest);
  append(output, "\nparsed "_zc);
  appendDigest(output, impl->parsedModuleReceipt);
  append(output, "\ndispatch "_zc);
  appendDigest(output, impl->dispatchFactsRevision.digest());
  append(output, "\nborrow-evidence "_zc);
  appendDigest(output, impl->borrowEvidenceRevision.digest());
  append(output, "\ninterface "_zc);
  appendInterfaceRevision(output, impl->ownInterface.revision);
  append(output, "\n"_zc);
  for (const auto& imported : impl->visibleImportedInterfaces) {
    auto importedModule = impl->identities.module(imported.module);
    if (importedModule == zc::none) { return zc::none; }
    append(output, "import-interface "_zc);
    ZC_IF_SOME(key, importedModule) { append(output, zc::encodeHex(key.key().encode().asPtr())); }
    append(output, " "_zc);
    appendInterfaceRevision(output, imported.revision);
    append(output, "\n"_zc);
  }

  for (const auto& declaration : impl->declarations) {
    auto definition = impl->identities.definition(declaration.definition);
    auto semanticType = impl->semanticTypes.get(declaration.inferredType);
    if (definition == zc::none || !semanticType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "decl h"_zc);
    append(output, zc::str(declaration.node.ordinal()));
    append(output, " def="_zc);
    ZC_IF_SOME(key, definition) { append(output, zc::encodeHex(key.key().encode().asPtr())); }
    append(output, " type="_zc);
    append(output, zc::encodeHex(semanticType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, " pattern=h"_zc);
    append(output, zc::str(declaration.pattern.ordinal()));
    append(output, " initializer=h"_zc);
    append(output, zc::str(declaration.initializer.ordinal()));
    append(output, "\n"_zc);
  }
  for (const auto& pattern : impl->patterns) {
    append(output, "pattern h"_zc);
    append(output, zc::str(pattern.node.ordinal()));
    append(output, " binding="_zc);
    auto definition = impl->identities.definition(pattern.binding);
    if (definition == zc::none) return zc::none;
    ZC_IF_SOME(key, definition) { append(output, zc::encodeHex(key.key().encode().asPtr())); }
    append(output, "\n"_zc);
  }
  for (const auto& function : impl->functions) {
    auto definition = impl->identities.definition(function.definition);
    auto resultType = impl->semanticTypes.get(function.resultType);
    if (definition == zc::none || !resultType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "function h"_zc);
    append(output, zc::str(function.node.ordinal()));
    append(output, " def="_zc);
    ZC_IF_SOME(key, definition) { append(output, zc::encodeHex(key.key().encode().asPtr())); }
    append(output, " result="_zc);
    append(output, zc::encodeHex(resultType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, " body=h"_zc);
    append(output, zc::str(function.body.ordinal()));
    append(output, "\n"_zc);
  }
  for (const auto& block : impl->blocks) {
    append(output, "block h"_zc);
    append(output, zc::str(block.node.ordinal()));
    for (const auto statement : block.statements) {
      append(output, " statement=h"_zc);
      append(output, zc::str(statement.ordinal()));
    }
    append(output, "\n"_zc);
  }
  for (const auto& statement : impl->returns) {
    append(output, "return h"_zc);
    append(output, zc::str(statement.node.ordinal()));
    append(output, " value=h"_zc);
    append(output, zc::str(statement.value.ordinal()));
    append(output, "\n"_zc);
  }
  for (const auto& expression : impl->expressions) {
    append(output, "literal h"_zc);
    append(output, zc::str(expression.node.ordinal()));
    append(output, " value="_zc);
    auto encoded =
        checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValueFromAuthority(
            expression.value, impl->module, impl->identities, impl->semanticTypes);
    if (encoded == zc::none) return zc::none;
    ZC_IF_SOME(bytes, encoded) { append(output, zc::encodeHex(bytes.asPtr())); }
    append(output, "\n"_zc);
  }
  for (const auto& local : impl->locals) {
    auto semanticType = impl->semanticTypes.get(local.type);
    if (!semanticType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "local h"_zc);
    append(output, zc::str(local.node.ordinal()));
    append(output, " l"_zc);
    append(output, zc::str(local.local.ordinal()));
    append(output, " type="_zc);
    append(output, zc::encodeHex(semanticType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, " initializer="_zc);
    ZC_IF_SOME(initializer, local.initializer) {
      append(output, "h"_zc);
      append(output, zc::str(initializer.ordinal()));
    } else {
      append(output, "none"_zc);
    }
    append(output, "\n"_zc);
  }
  for (const auto& reference : impl->localReferences) {
    auto semanticType = impl->semanticTypes.get(reference.type);
    if (!semanticType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "local-ref h"_zc);
    append(output, zc::str(reference.node.ordinal()));
    append(output, " l"_zc);
    append(output, zc::str(reference.local.ordinal()));
    append(output, " type="_zc);
    append(output, zc::encodeHex(semanticType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, "\n"_zc);
  }
  for (const auto& call : impl->calls) {
    auto callee = impl->identities.definition(call.callee);
    auto calleeType = impl->semanticTypes.get(call.calleeType);
    auto resultType = impl->semanticTypes.get(call.resultType);
    if (callee == zc::none || !calleeType.is<type::SemanticTypeLookup>() ||
        !resultType.is<type::SemanticTypeLookup>()) {
      return zc::none;
    }
    append(output, "call h"_zc);
    append(output, zc::str(call.node.ordinal()));
    append(output, " callee="_zc);
    ZC_IF_SOME(key, callee) { append(output, zc::encodeHex(key.key().encode().asPtr())); }
    append(output, " callee-type="_zc);
    append(output, zc::encodeHex(calleeType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, " result="_zc);
    append(output, zc::encodeHex(resultType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, " nargs="_zc);
    append(output, zc::str(call.arguments.size()));
    for (size_t index = 0; index < call.arguments.size(); ++index) {
      const auto& argument = call.arguments[index];
      auto argumentType = impl->semanticTypes.get(argument.type);
      if (!argumentType.is<type::SemanticTypeLookup>()) return zc::none;
      append(output, " arg"_zc);
      append(output, zc::str(index));
      append(output, "-type="_zc);
      append(output, zc::encodeHex(argumentType.get<type::SemanticTypeLookup>().key().bytes()));
      // An argument is a constant or a parameter reference; encode a discriminating
      // tag so the two shapes never collide in the canonical text.
      ZC_IF_SOME(value, argument.value) {
        append(output, " arg"_zc);
        append(output, zc::str(index));
        append(output, "-const="_zc);
        auto encoded = checker::signature::SignatureFactsCanonicalCodec::
            encodeCanonicalConstValueFromAuthority(value, impl->module, impl->identities,
                                                   impl->semanticTypes);
        if (encoded == zc::none) return zc::none;
        ZC_IF_SOME(bytes, encoded) { append(output, zc::encodeHex(bytes.asPtr())); }
      }
      ZC_IF_SOME(parameter, argument.parameter) {
        append(output, " arg"_zc);
        append(output, zc::str(index));
        append(output, "-param="_zc);
        append(output, zc::encodeHex(parameter.encode().asPtr()));
      }
    }
    append(output, "\n"_zc);
  }
  for (const auto& unsafeBlock : impl->unsafeBlocks) {
    auto resultType = impl->semanticTypes.get(unsafeBlock.type);
    if (!resultType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "unsafe-block h"_zc);
    append(output, zc::str(unsafeBlock.node.ordinal()));
    append(output, " body=h"_zc);
    append(output, zc::str(unsafeBlock.body.ordinal()));
    append(output, " type="_zc);
    append(output, zc::encodeHex(resultType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, "\n"_zc);
  }
  for (const auto& equality : impl->primitiveBinaryOperations) {
    auto resultType = impl->semanticTypes.get(equality.type);
    if (!resultType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "equality h"_zc);
    append(output, zc::str(equality.node.ordinal()));
    append(output, " left=h"_zc);
    append(output, zc::str(equality.left.ordinal()));
    append(output, " right=h"_zc);
    append(output, zc::str(equality.right.ordinal()));
    append(output, " type="_zc);
    append(output, zc::encodeHex(resultType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, "\n"_zc);
  }
  for (const auto& conditional : impl->conditionals) {
    auto resultType = impl->semanticTypes.get(conditional.type);
    if (!resultType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "conditional h"_zc);
    append(output, zc::str(conditional.node.ordinal()));
    append(output, " condition=h"_zc);
    append(output, zc::str(conditional.condition.ordinal()));
    append(output, " then=h"_zc);
    append(output, zc::str(conditional.thenReturnValue.ordinal()));
    append(output, " else=h"_zc);
    append(output, zc::str(conditional.elseReturnValue.ordinal()));
    append(output, " type="_zc);
    append(output, zc::encodeHex(resultType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, "\n"_zc);
  }
  for (const auto& loop : impl->loops) {
    auto resultType = impl->semanticTypes.get(loop.type);
    if (!resultType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "loop h"_zc);
    append(output, zc::str(loop.node.ordinal()));
    append(output, " condition=h"_zc);
    append(output, zc::str(loop.condition.ordinal()));
    append(output, " type="_zc);
    append(output, zc::encodeHex(resultType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, "\n"_zc);
  }
  return zc::str(output.releaseAsArray());
}

ir::IrOperationResult<VerifiedHirModule> HirVerifier::verify(HirModuleCandidate&& candidate) {
  const auto module = candidate.impl->checkedModule.module();
  const auto registries = candidate.impl->checkedModule.retainIdentityAuthority();
  const auto& semanticTypes = candidate.impl->checkedModule.semanticTypes();
  const auto& facts = candidate.impl->checkedModule.checkedFacts();
  const auto bound = candidate.impl->checkedModule.retainAdmittedBoundModule();
  const auto& definitions = bound.definitions();
  // Dead-erase slice: the admitted dead-erase initializer nodes, derived from
  // the same checked facts the builder drained. Both the builder and the
  // verifier restrict their dead-binding filter to this erasure cone, so the
  // two derive one identical filtered shape.
  const auto deadEraseInitializers = deadEraseInitializerNodes(bound.tree(), definitions, facts);
  const auto& signatures = candidate.impl->checkedModule.ownModuleInterface().signatures();
  const auto declarationCount = candidate.impl->declarations.size();
  const auto functionCount = candidate.impl->functions.size();
  const auto directCallCount = candidate.impl->calls.size();
  const auto receiverCallCount = candidate.impl->receiverCalls.size();
  size_t directCallArgumentCount = 0;
  size_t directCallLiteralArgumentCount = 0;
  // A by-value aggregate call (`let p: P = P { .. }; return f(p);`) passes the
  // aggregate-initialized owner local inline as the sole call argument. The
  // local is consumed by the call instead of a local-reference expression, so
  // these calls correct the local-reference, literal, and expression count
  // equations exactly once per function.
  size_t directAggregateCallCount = 0;
  // A scalar-local call (`let a: i32 = <literal>; return f(a);`) has the same
  // inline owner-local argument as the aggregate call but its F+3 initializer is
  // a scalar-literal expression rather than an aggregate. It corrects the same
  // count equations; the two carriers are distinguished by which pool owns the
  // initializer node.
  size_t directScalarLocalCallCount = 0;
  size_t receiverCallArgumentCount = 0;
  // A field-projection argument (`cell.echo(cell.value)`) produces a node-type,
  // member, and place fact but no literal fact, unlike a constant argument.
  // Tracked to correct the literals, members, and places equations.
  size_t receiverCallFieldArgumentCount = 0;
  // A field-comparison argument (`cell.compare(cell.value > 0)`) produces a
  // node-type, member, place, and literal fact. The literal fact means it is
  // not subtracted from the literals equation like a field-projection
  // argument, but the member and place facts still need counting.
  size_t receiverCallComparisonArgumentCount = 0;
  // A self-call pool record forwards the implicit header receiver: its
  // receiver node slot is unset and it carries zero explicit arguments. Such a
  // record still contributes the call, member, dispatch, and two node-type
  // facts, but owns no expression/literal record, so the per-function baseline
  // must be corrected exactly like a direct call.
  size_t receiverSelfCallCount = 0;
  size_t localReturnCount = candidate.impl->locals.size();
  const auto localWriteCount = candidate.impl->localWrites.size();
  const auto parameterReferenceCount = candidate.impl->parameterReferences.size();
  const auto parameterIndexCount = candidate.impl->parameterIndexes.size();
  const auto parameterReborrowCount = candidate.impl->parameterReborrows.size();
  const auto localBorrowCount = candidate.impl->localBorrows.size();
  size_t aggregateCount = candidate.impl->aggregates.size();
  const auto localFieldProjectionCount = candidate.impl->localFieldProjections.size();
  const auto parameterFieldProjectionCount = candidate.impl->parameterFieldProjections.size();
  const auto parameterFieldWriteCount = candidate.impl->parameterFieldWrites.size();
  size_t localFieldWriteCount = 0;
  size_t localAliasReborrowCount = 0;
  size_t aggregateElementCount = 0;
  const auto unsafeBlockCount = candidate.impl->unsafeBlocks.size();
  const auto conditionalCount = candidate.impl->conditionals.size();
  // Match-return shapes reuse the conditional-return HIR nodes but carry two
  // extra AST nodes (the MatchStmt and its scrutinee) and two extra pattern
  // literals (true/false) that the checker produces node-type and literal
  // facts for. Each match-return shape produces one exhaustiveness fact, so
  // the fact count gives the match-return count.
  const auto matchReturnCount = facts.exhaustiveness().size();
  // A match-return with a default (wildcard) arm has one pattern literal
  // instead of two, so both the nodeTypes and literals equations subtract one
  // per default arm. Derive the count from the same exhaustiveness facts that
  // give matchReturnCount, inspecting each match's arms in the AST.
  size_t matchDefaultArmCount = 0;
  {
    const auto& tree = bound.tree();
    for (const auto& entry : facts.exhaustiveness().entries()) {
      if (!tree.contains(entry.value.node)) continue;
      const auto& matchNode = tree.node(entry.value.node);
      const ast::NodeList arms{matchNode.payload.words[ast::kMatchStmtArmsFirstWord],
                               matchNode.payload.words[ast::kMatchStmtArmsSizeWord]};
      if (!tree.contains(arms)) continue;
      for (size_t index = 0; index < arms.size; ++index) {
        const ast::NodeId armId = tree.list(arms)[index];
        if (!tree.contains(armId)) continue;
        const auto& arm = tree.node(armId);
        if (arm.kind != ast::SyntaxKind::MatchArmStmt) continue;
        const ast::NodeId pattern(arm.payload.words[ast::kMatchArmStmtPatternWord]);
        if (tree.contains(pattern) && tree.node(pattern).kind == ast::SyntaxKind::WildcardPattern) {
          ++matchDefaultArmCount;
          break;
        }
      }
    }
  }
  // equalityConditionalCount is derived below, after the sequential-binary tally,
  // because the primitiveBinaryOperations vector pools conditional/comparison
  // binaries with sequential-local binary initializers.
  const auto loopCount = candidate.impl->loops.size();
  for (const auto& write : candidate.impl->localWrites) {
    if (write.field != zc::none) ++localFieldWriteCount;
  }
  for (const auto& reborrow : candidate.impl->parameterReborrows) {
    if (reborrow.sourceAlias != zc::none) ++localAliasReborrowCount;
  }
  for (const auto& aggregate : candidate.impl->aggregates) {
    aggregateElementCount += aggregate.elements.size();
  }
  for (const auto& call : candidate.impl->calls) {
    directCallArgumentCount += call.arguments.size();
    for (const auto& argument : call.arguments) {
      if (argument.value != zc::none) ++directCallLiteralArgumentCount;
    }
    if (call.arguments.size() == 1 && call.arguments[0].value == zc::none &&
        call.arguments[0].parameter == zc::none && call.arguments[0].local != zc::none) {
      // Both admitted local-argument calls share the six-node stride: the
      // initializer is node F+3, two before the F+5 call node. An expression
      // record there marks the scalar-local carrier; anything else is the
      // aggregate carrier.
      const uint32_t initializerOrdinal = call.node.ordinal() - 2;
      bool initializerIsExpression = false;
      for (const auto& expression : candidate.impl->expressions) {
        if (expression.node.ordinal() == initializerOrdinal) {
          initializerIsExpression = true;
          break;
        }
      }
      if (initializerIsExpression) {
        ++directScalarLocalCallCount;
      } else {
        ++directAggregateCallCount;
      }
    }
  }
  for (const auto& call : candidate.impl->receiverCalls) {
    receiverCallArgumentCount += call.arguments.size();
    for (const auto& argument : call.arguments) {
      if (argument.local != zc::none && argument.field != zc::none) {
        if (argument.comparisonOperation != zc::none) {
          ++receiverCallComparisonArgumentCount;
        } else {
          ++receiverCallFieldArgumentCount;
        }
      }
    }
    if (call.receiver == HirNodeId()) ++receiverSelfCallCount;
  }
  size_t uninitializedLocalReturnCount = 0;
  for (const auto& local : candidate.impl->locals) {
    if (local.initializer == zc::none) ++uninitializedLocalReturnCount;
  }
  // Sequential N-local corrections. The shared count equations assume one value
  // node and one local reference per function local; a sequential body instead
  // produces one value node per literal/aggregate/parameter-reference
  // initializer plus a parameter return, and one local reference per
  // local-reference initializer plus a local return. These tallies re-derive the
  // true contributions from the candidate HIR so the localReferences,
  // expressions, and literals equations balance for any N. All corrections are
  // zero for the former two-local literal or aggregate source.
  size_t sequentialLiteralInitializers = 0;
  size_t sequentialAggregateInitializers = 0;
  size_t sequentialParameterInitializers = 0;
  size_t sequentialLocalInitializers = 0;
  size_t sequentialCastInitializers = 0;
  size_t sequentialLocalCount = 0;
  size_t sequentialFunctionCount = 0;
  size_t sequentialParameterReturns = 0;
  size_t sequentialLocalReturns = 0;
  // Sequential primitive-binary bindings and, of their operands, the counts that
  // are scalar literals, parameter references, and earlier-local references.
  size_t sequentialBinaryCount = 0;
  size_t sequentialBinaryLiteralOperands = 0;
  size_t sequentialBinaryParameterOperands = 0;
  size_t sequentialBinaryLocalOperands = 0;
  // Ternary bindings and, of those, the count whose condition names an earlier
  // local (rather than a parameter).
  size_t sequentialTernaryCount = 0;
  size_t sequentialTernaryLocalConditions = 0;
  // Ternary bindings whose condition is a parameter reference. These are
  // counted in parameterReferenceCount (subtracted by the expressions and
  // literals equations) but are conditions, not initializers, so the
  // sequentialLiteralCorrection does not compensate for them. Tracked to add
  // them back.
  size_t sequentialTernaryParameterConditions = 0;
  // Dead-erase slice: bindings skipped by the builder's dead-binding filter.
  // The checker produces facts for every binding, so the HIR-node-based counts
  // (localReturnCount, aggregateCount, aggregateElementCount) must add the dead
  // bindings back to match the fact totals. The pre-pass corrections below
  // already count all bindings, so only the HIR-node-based counts need this.
  size_t deadLocalReturnCount = 0;
  size_t deadAggregateCount = 0;
  // Dead bindings' fact contributions beyond the per-binding local node type.
  // These are added to the expected counts because the checker produces facts
  // for every binding but the HIR only has nodes for live bindings.
  size_t deadNodeTypesExtra = 0;
  size_t deadLiterals = 0;
  size_t deadCalls = 0;
  size_t deadCasts = 0;
  {
    const auto& tree = bound.tree();
    for (const auto& functionDeclaration : candidate.impl->functions) {
      auto sourceDefinitionIndex = definitionIndex(definitions, functionDeclaration.definition);
      if (sourceDefinitionIndex == zc::none) continue;
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      if (!tree.contains(sourceDefinition.node)) continue;
      auto shape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      bool sequential = false;
      ast::NodeId sourceBody;
      ZC_IF_SOME(value, shape) {
        sequential = value.isSequentialLocalReturn;
        sourceBody = value.body;
      }
      if (!sequential) continue;
      auto sequentialShape = sequentialLocalShape(tree, sourceBody);
      ZC_IF_SOME(value, sequentialShape) {
        // Dead-erase slice: identify the erasure-cone removed bindings. The
        // corrections below count only kept bindings (the HIR has no nodes for
        // removed ones); the removed bindings' fact contributions are tracked
        // separately and added to the expected counts in the equations below.
        const auto dead = deadSequentialBindings(tree, value);
        const auto removed = erasureRelatedDeadBindings(tree, value, dead, deadEraseInitializers);
        ++sequentialFunctionCount;
        for (size_t bindingIndex = 0; bindingIndex < value.bindings.size(); ++bindingIndex) {
          const auto& binding = value.bindings[bindingIndex];
          if (removed[bindingIndex]) {
            // Track the removed binding's fact contributions. The checker
            // produces facts for every binding; the HIR only has nodes for
            // kept ones, so the expected counts must add the removed facts.
            ++deadLocalReturnCount;
            switch (binding.initializerKind) {
              case SequentialInitializerKind::Literal:
              case SequentialInitializerKind::EnumVariant:
                ++deadLiterals;
                break;
              case SequentialInitializerKind::Aggregate: {
                ++deadAggregateCount;
                auto aggregateIndex = factIndex(facts.aggregates(), binding.initializer);
                if (aggregateIndex != zc::none) {
                  size_t aggregateSlot = 0;
                  ZC_IF_SOME(index, aggregateIndex) { aggregateSlot = index; }
                  const auto elementCount =
                      facts.aggregates().entries()[aggregateSlot].value.elements.size();
                  deadNodeTypesExtra += elementCount;
                  deadLiterals += elementCount;
                }
                break;
              }
              case SequentialInitializerKind::LocalReference:
              case SequentialInitializerKind::ParameterReference:
                break;
              case SequentialInitializerKind::PrimitiveUnary:
                // Desugars to a binary: 1 extra node-type (the binary) plus
                // 1 call fact. The synthetic literal operand has no checker
                // fact (subtracted by unaryReturnCount for live bindings).
                ++deadNodeTypesExtra;
                ++deadCalls;
                break;
              case SequentialInitializerKind::PrimitiveBinary:
                ++deadNodeTypesExtra;
                ++deadNodeTypesExtra;
                ++deadCalls;
                for (const auto* operand : {&binding.leftOperand, &binding.rightOperand}) {
                  ZC_IF_SOME(operandValue, *operand) {
                    if (operandValue.kind == SequentialBinaryOperandKind::Literal) {
                      ++deadLiterals;
                    }
                  }
                }
                break;
              case SequentialInitializerKind::Cast:
                ++deadNodeTypesExtra;
                ++deadLiterals;
                ++deadCasts;
                break;
              case SequentialInitializerKind::Ternary:
                deadNodeTypesExtra += 3;
                ++deadLiterals;
                ++deadLiterals;
                if (binding.ternaryConditionIsLiteral) { ++deadLiterals; }
                break;
            }
            continue;
          }
          ++sequentialLocalCount;
          switch (binding.initializerKind) {
            case SequentialInitializerKind::Literal:
            case SequentialInitializerKind::EnumVariant:
              ++sequentialLiteralInitializers;
              break;
            case SequentialInitializerKind::Aggregate:
              ++sequentialAggregateInitializers;
              break;
            case SequentialInitializerKind::ParameterReference:
              ++sequentialParameterInitializers;
              break;
            case SequentialInitializerKind::LocalReference:
              ++sequentialLocalInitializers;
              break;
            case SequentialInitializerKind::PrimitiveUnary:
              // A unary binding desugars to one binary with a synthetic literal
              // operand. The binary counts as a sequential binary; the synthetic
              // literal counts as a literal-bearing slot (offset by the
              // unaryReturnCount subtraction in the digest equations, since it
              // has no checker fact). The real operand is tallied as a parameter
              // or local reference.
              ++sequentialBinaryCount;
              ++sequentialBinaryLiteralOperands;
              ZC_IF_SOME(operand, binding.unaryOperand) {
                if (operand.kind == SequentialBinaryOperandKind::ParameterReference) {
                  ++sequentialBinaryParameterOperands;
                } else if (operand.kind == SequentialBinaryOperandKind::LocalReference) {
                  ++sequentialBinaryLocalOperands;
                }
              }
              break;
            case SequentialInitializerKind::Cast:
              // An integer `as` cast over a scalar literal inner expression. The
              // cast is a no-op for widening or identity conversions; the inner
              // literal counts as a literal-bearing slot.
              ++sequentialCastInitializers;
              ++sequentialLiteralInitializers;
              break;
            case SequentialInitializerKind::PrimitiveBinary:
              ++sequentialBinaryCount;
              for (const auto* operand : {&binding.leftOperand, &binding.rightOperand}) {
                ZC_IF_SOME(value, *operand) {
                  switch (value.kind) {
                    case SequentialBinaryOperandKind::Literal:
                      ++sequentialBinaryLiteralOperands;
                      break;
                    case SequentialBinaryOperandKind::ParameterReference:
                      ++sequentialBinaryParameterOperands;
                      break;
                    case SequentialBinaryOperandKind::LocalReference:
                      ++sequentialBinaryLocalOperands;
                      break;
                    case SequentialBinaryOperandKind::NestedBinary:
                      // A nested operand is a second primitive binary carrying its
                      // own call/dispatch fact and two leaf operands; count it as
                      // an additional binary and tally its leaves the same way.
                      ++sequentialBinaryCount;
                      for (const auto* leaf : {&value.nestedLeft, &value.nestedRight}) {
                        ZC_IF_SOME(leafValue, *leaf) {
                          switch (leafValue.kind) {
                            case SequentialBinaryOperandKind::Literal:
                              ++sequentialBinaryLiteralOperands;
                              break;
                            case SequentialBinaryOperandKind::ParameterReference:
                              ++sequentialBinaryParameterOperands;
                              break;
                            case SequentialBinaryOperandKind::LocalReference:
                              ++sequentialBinaryLocalOperands;
                              break;
                            case SequentialBinaryOperandKind::NestedBinary:
                              break;
                          }
                        }
                      }
                      break;
                  }
                }
              }
              break;
            case SequentialInitializerKind::Ternary:
              // A ternary conditional expression. The condition is a bool
              // reference or a bool literal; both branches are scalar
              // literals. Each branch counts as a literal-bearing slot. The
              // condition may name an earlier local or a parameter; track the
              // local case for the localReferences correction. A bool literal
              // condition contributes one additional literal.
              ++sequentialTernaryCount;
              ++sequentialLiteralInitializers;
              ++sequentialLiteralInitializers;
              if (binding.ternaryConditionIsLiteral) { ++sequentialLiteralInitializers; }
              if (binding.ternaryConditionIsLocal) { ++sequentialTernaryLocalConditions; }
              if (!binding.ternaryConditionIsLiteral && !binding.ternaryConditionIsLocal) {
                ++sequentialTernaryParameterConditions;
              }
              break;
          }
        }
        if (value.returnsLocal == zc::none) {
          ++sequentialParameterReturns;
        } else {
          ++sequentialLocalReturns;
        }
      }
    }
  }
  // Dead-erase slice: the checker produces facts for every binding, but the
  // builder only emits HIR nodes for live bindings. The corrections above
  // count only live bindings; the dead bindings' fact contributions are
  // tracked separately (deadLocalReturnCount, deadNodeTypesExtra, deadLiterals,
  // deadCalls, deadCasts, deadAggregateCount) and added to the expected counts
  // in the equations below.
  // Ternary bindings add their HirConditionalExpression to the conditionals
  // vector, but the builder's conditionalCount excludes them (the conditional
  // is the binding initializer counted by localReturnCount, and its condition
  // and arm literals are counted by sequentialTernaryCount). Subtract to match
  // the builder's tally.
  const size_t effectiveConditionalCount = conditionalCount - sequentialTernaryCount;
  // Leading-local conditional corrections. Each such function contributes K
  // locals but only R_init (local-reference initializers) plus lc_local
  // (comparison operands reading a leading local) actual local references. Its
  // expressions/literals baseline grants one per-function value plus the
  // conditional/equality arm and operand credits, so the same signed correction
  // C = K - R_init - lc_local balances the localReferences, expressions, and
  // literals equations.
  int64_t leadingLocalConditionalCorrection = 0;
  // Each unary condition (`!x`) desugars to `x == false` in HIR. The synthetic
  // false operand has no AST node and therefore no checker-produced node-type
  // or literal fact, so the nodeTypes and literals equations subtract one per
  // unary leading-local conditional.
  int64_t leadingLocalConditionalUnaryCount = 0;
  // Arithmetic bindings synthesized by the HIR builder for nested-arithmetic
  // comparison operands (`a + 1 < 5` -> `let t = a + 1; if (t < 5)`). Each
  // adds one pooled primitive binary and one synthesized local whose
  // local-reference operand has no AST node and therefore no checker-produced
  // node-type fact, so the nodeTypes equation subtracts one per binding. The
  // arithmetic's two leaf operands are real AST nodes with checker-produced
  // facts, so the nodeTypes equation adds two per binding and the
  // expressions/literals equations add the literal-leaf count.
  int64_t leadingLocalConditionalArithmeticCount = 0;
  int64_t leadingLocalConditionalArithmeticLiteralCount = 0;
  int64_t leadingLocalConditionalArithmeticParameterCount = 0;
  int64_t leadingLocalConditionalArithmeticLocalCount = 0;
  {
    const auto& tree = bound.tree();
    // Classify one arithmetic leaf operand: literal, parameter reference, or
    // local reference. The expressions/literals equations subtract every
    // pooled parameter reference, so arithmetic leaf parameter references must
    // be added back; local leaf references need no correction (they are not
    // subtracted) but the per-function expression baseline is freed when the
    // comparison left operand becomes a synthesized local, hence the
    // leadingLocalConditionalArithmeticCount term in those equations.
    auto classifyArithmeticLeaf = [&](ast::NodeId leafNode, bool isLiteral) {
      if (isLiteral) {
        ++leadingLocalConditionalArithmeticLiteralCount;
        return;
      }
      if (resolvedCallableParameter(bound.bindings(), leafNode) != zc::none) {
        ++leadingLocalConditionalArithmeticParameterCount;
        return;
      }
      if (resolvedOwnerLocal(bound.bindings(), leafNode) != zc::none) {
        ++leadingLocalConditionalArithmeticLocalCount;
      }
    };
    for (const auto& functionDeclaration : candidate.impl->functions) {
      auto sourceDefinitionIndex = definitionIndex(definitions, functionDeclaration.definition);
      if (sourceDefinitionIndex == zc::none) continue;
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      if (!tree.contains(sourceDefinition.node)) continue;
      auto shape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (shape == zc::none || !ZC_ASSERT_NONNULL(shape).isLeadingLocalConditional) continue;
      if (ZC_ASSERT_NONNULL(shape).conditionIsUnary) ++leadingLocalConditionalUnaryCount;
      auto leading = leadingLocalConditionalShape(tree, ZC_ASSERT_NONNULL(shape).body);
      if (leading == zc::none) {
        // The HIR builder synthesizes an arithmetic binding for nested-arithmetic
        // comparison operands. The source shape has no leading let binding, so
        // leadingLocalConditionalShape returns none, but the synthesized binding
        // still needs digest corrections.
        if (ZC_ASSERT_NONNULL(shape).conditionLeftIsNestedArithmetic) {
          ++leadingLocalConditionalArithmeticCount;
          classifyArithmeticLeaf(ZC_ASSERT_NONNULL(shape).nestedArithmeticLeft,
                                 ZC_ASSERT_NONNULL(shape).nestedArithmeticLeftIsLiteral);
          classifyArithmeticLeaf(ZC_ASSERT_NONNULL(shape).nestedArithmeticRight,
                                 ZC_ASSERT_NONNULL(shape).nestedArithmeticRightIsLiteral);
        }
        if (ZC_ASSERT_NONNULL(shape).conditionRightIsNestedArithmetic) {
          ++leadingLocalConditionalArithmeticCount;
          classifyArithmeticLeaf(ZC_ASSERT_NONNULL(shape).nestedArithmeticLeft,
                                 ZC_ASSERT_NONNULL(shape).nestedArithmeticLeftIsLiteral);
          classifyArithmeticLeaf(ZC_ASSERT_NONNULL(shape).nestedArithmeticRight,
                                 ZC_ASSERT_NONNULL(shape).nestedArithmeticRightIsLiteral);
        }
        continue;
      }
      size_t localReferencesUsed = 0;
      ZC_IF_SOME(value, leading) {
        for (const auto& binding : value.bindings) {
          if (binding.initializerKind == SequentialInitializerKind::LocalReference) {
            ++localReferencesUsed;
          }
        }
        auto namesBinding = [&](ast::NodeId operand) {
          if (!tree.contains(operand) || tree.node(operand).kind != ast::SyntaxKind::IdentExpr) {
            return false;
          }
          for (const auto& binding : value.bindings) {
            if (tree.node(binding.pattern).payload.words[ast::kIdentifierPatternNameWord] ==
                tree.node(operand).payload.words[ast::kIdentExprNameWord]) {
              return true;
            }
          }
          return false;
        };
        if (namesBinding(ZC_ASSERT_NONNULL(shape).conditionLeft)) ++localReferencesUsed;
        if (namesBinding(ZC_ASSERT_NONNULL(shape).conditionRight)) ++localReferencesUsed;
        leadingLocalConditionalCorrection +=
            static_cast<int64_t>(value.bindings.size()) - static_cast<int64_t>(localReferencesUsed);
      }
      // The leading-bindings shape can also carry a nested-arithmetic comparison
      // operand; the HIR builder synthesizes an arithmetic binding just like the
      // no-leading-bindings case.
      if (ZC_ASSERT_NONNULL(shape).conditionLeftIsNestedArithmetic) {
        ++leadingLocalConditionalArithmeticCount;
        classifyArithmeticLeaf(ZC_ASSERT_NONNULL(shape).nestedArithmeticLeft,
                               ZC_ASSERT_NONNULL(shape).nestedArithmeticLeftIsLiteral);
        classifyArithmeticLeaf(ZC_ASSERT_NONNULL(shape).nestedArithmeticRight,
                               ZC_ASSERT_NONNULL(shape).nestedArithmeticRightIsLiteral);
      }
      if (ZC_ASSERT_NONNULL(shape).conditionRightIsNestedArithmetic) {
        ++leadingLocalConditionalArithmeticCount;
        classifyArithmeticLeaf(ZC_ASSERT_NONNULL(shape).nestedArithmeticLeft,
                               ZC_ASSERT_NONNULL(shape).nestedArithmeticLeftIsLiteral);
        classifyArithmeticLeaf(ZC_ASSERT_NONNULL(shape).nestedArithmeticRight,
                               ZC_ASSERT_NONNULL(shape).nestedArithmeticRightIsLiteral);
      }
    }
  }
  // Receiver field-arithmetic methods (`return this.<field> OP
  // <literal>;`) derived from the source shapes. Each materializes one pooled
  // primitive binary plus a parameter-field projection, so like the sequential
  // binary tally this count is subtracted back when deriving the pooled
  // comparison/conditional count and balanced by explicit count terms.
  size_t receiverFieldArithmeticCount = 0;
  {
    const auto& tree = bound.tree();
    for (const auto& functionDeclaration : candidate.impl->functions) {
      auto sourceDefinitionIndex = definitionIndex(definitions, functionDeclaration.definition);
      if (sourceDefinitionIndex == zc::none) continue;
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      if (!tree.contains(sourceDefinition.node)) continue;
      auto shape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      ZC_IF_SOME(value, shape) {
        if (value.returnsReceiverFieldArithmetic) ++receiverFieldArithmeticCount;
      }
    }
  }
  // Void mutating methods (`this.<field> = <parameter>;`, Unit result) derived
  // from the source shapes. Each owns a function and a body block but no return
  // record or return value node, so the returns identity and the per-function
  // baselines of the expressions, literals, and nodeTypes equations subtract
  // this count.
  size_t voidFunctionCount = 0;
  // Discarded statement-position receiver calls. Unlike a returned receiver
  // call, their call node is not covered by the per-function baseline, so each
  // contributes one extra node-type fact.
  size_t discardedStatementCallCount = 0;
  // For-loop accumulator functions with N > 1 accumulators: the count equations
  // were derived for the N=1 shape. Each additional accumulator contributes one
  // extra local reference (body-write left), two extra literal expressions
  // (init + body-write right), and five extra node-type facts, while the
  // baseline terms only absorb one of each. Sum the (N-1) surplus across all
  // accumulator functions so the localReferences, expressions, literals, and
  // nodeTypes equations balance.
  int64_t forLoopAccumulatorCorrection = 0;
  {
    const auto& tree = bound.tree();
    for (const auto& functionDeclaration : candidate.impl->functions) {
      auto sourceDefinitionIndex = definitionIndex(definitions, functionDeclaration.definition);
      if (sourceDefinitionIndex == zc::none) continue;
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      if (!tree.contains(sourceDefinition.node)) continue;
      auto shape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      ZC_IF_SOME(value, shape) {
        if (value.isVoidBody) ++voidFunctionCount;
        if (value.hasDiscardedReceiverCallStatement) ++discardedStatementCallCount;
        if (value.isForLoopAccumulator && value.forLoopAccumulatorPatterns.size() > 1) {
          forLoopAccumulatorCorrection +=
              static_cast<int64_t>(value.forLoopAccumulatorPatterns.size()) - 1;
        }
      }
    }
  }
  // Mutable-local writes whose value node is a materialized primitive binary.
  // Each pools one entry into primitiveBinaryOperations, carries one call and one
  // dispatch fact, and its two operands are each a scalar literal (one literal
  // fact + one expression) or a parameter reference. Tally them from the
  // candidate HIR so the same corrections the builder applied balance here.
  size_t binaryWriteCount = 0;
  size_t binaryWriteParameterOperands = 0;
  size_t binaryWriteLocalOperands = 0;
  for (const auto& write : candidate.impl->localWrites) {
    zc::Maybe<const HirPrimitiveBinaryExpression&> writeBinary;
    for (const auto& operation : candidate.impl->primitiveBinaryOperations) {
      if (operation.node != write.value) continue;
      writeBinary = operation;
    }
    ZC_IF_SOME(binary, writeBinary) {
      ++binaryWriteCount;
      for (const auto operandNode : {binary.left, binary.right}) {
        for (const auto& reference : candidate.impl->parameterReferences) {
          if (reference.node == operandNode) ++binaryWriteParameterOperands;
        }
        for (const auto& reference : candidate.impl->localReferences) {
          if (reference.node == operandNode) ++binaryWriteLocalOperands;
        }
      }
    }
  }
  // The materialized primitive-binary operations pool three sources: conditional
  // conditions, comparison-return values, and sequential-local binary
  // initializers. Restore equalityConditionalCount to only the first two so the
  // pooled comparison/conditional equation terms stay exact; sequential binaries
  // and binary-write values are balanced by explicit count terms below.
  const auto equalityConditionalCount =
      candidate.impl->primitiveBinaryOperations.size() - sequentialBinaryCount - binaryWriteCount -
      receiverFieldArithmeticCount - leadingLocalConditionalArithmeticCount;
  // Unary desugars (`-x` -> `0 - x`, etc.) pool into primitiveBinaryOperations
  // like comparison returns, but their synthetic operand has no checker-produced
  // node-type or literal fact. Subtract one per unary desugar from both
  // equations so the verifier matches the builder's corrected counts.
  size_t unaryReturnCount = 0;
  for (const auto& operation : candidate.impl->primitiveBinaryOperations) {
    if (operation.isUnaryDesugar) ++unaryReturnCount;
  }
  // Postfix increment/decrement writes (`x++` -> `x = x + 1`) pool into
  // primitiveBinaryOperations like binary writes, but the PostfixExpression
  // source node replaces the assignment plus binary nodes (two node-type facts
  // instead of five) and the synthetic literal 1 has no checked literal fact.
  // Subtract three per postfix write from nodeTypes and one from literals.
  size_t postfixIncrementWriteCount = 0;
  for (const auto& operation : candidate.impl->primitiveBinaryOperations) {
    if (operation.isPostfixDesugar) ++postfixIncrementWriteCount;
  }
  // Compound assignment writes (`x += 1` -> `x = x + 1`) pool into
  // primitiveBinaryOperations like binary writes, but the AssignmentExpr source
  // node carries only three node-type facts (assignment, target, value) instead
  // of five and the binary operation has no checked call fact. Subtract two per
  // compound write from nodeTypes and one from calls and dispatch facts.
  size_t compoundAssignmentWriteCount = 0;
  for (const auto& operation : candidate.impl->primitiveBinaryOperations) {
    if (operation.isCompoundAssignmentDesugar) ++compoundAssignmentWriteCount;
  }
  // localReferences: sequential locals contribute N to the localReturnCount
  // baseline but only L_loc + R_loc + binary-local-operand + ternary-local-
  // condition actual local references. Signed because binary local operands can
  // exceed the shortfall.
  const int64_t sequentialLocalReferenceCorrection =
      static_cast<int64_t>(sequentialLocalCount) -
      static_cast<int64_t>(sequentialLocalInitializers + sequentialLocalReturns +
                           sequentialBinaryLocalOperands + sequentialTernaryLocalConditions);
  // expressions and literals: each sequential function contributes one baseline
  // value node minus its aggregate and parameter-reference credits, but the true
  // scalar-literal count is L_lit plus any binary literal operands. Both
  // equations need the same correction. Signed because an all-reference binary
  // body carries fewer literals than the per-function baseline grants. A
  // ternary binding's two arm literals are counted by
  // sequentialLiteralInitializers, so only the per-function baseline is
  // subtracted here.
  const int64_t sequentialLiteralCorrection =
      static_cast<int64_t>(sequentialLiteralInitializers + sequentialAggregateInitializers +
                           sequentialParameterInitializers + sequentialParameterReturns +
                           sequentialBinaryLiteralOperands + sequentialBinaryParameterOperands) -
      static_cast<int64_t>(sequentialFunctionCount);
  const auto executableDefinitions = executableDefinitionCount(definitions);
  const auto borrowCapability = candidate.impl->checkedModule.borrowEvidenceCapability();
  const auto borrowEvidence =
      borrowCapability.lookup(candidate.impl->checkedModule.borrowEvidenceLease());
  if (registries.semanticContext() != candidate.impl->checkedModule.semanticContext() ||
      registries.fingerprint().digest() !=
          candidate.impl->checkedModule.contextFingerprint().digest() ||
      registries.boundModule(module) == zc::none ||
      candidate.impl->checkedModule.checkedRepository().lookup(
          candidate.impl->checkedModule.checkedEvidenceLease()) == zc::none ||
      !borrowEvidence.isResolved() ||
      borrowEvidence.evidence().revision().digest() !=
          candidate.impl->checkedModule.borrowEvidenceRevision().digest() ||
      candidate.impl->checkedModule.borrowEvidenceLease().key().revision.digest() !=
          candidate.impl->checkedModule.borrowEvidenceRevision().digest() ||
      candidate.impl->checkedModule.dispatchFacts().facts().size() !=
          directCallCount + receiverCallCount + equalityConditionalCount + sequentialBinaryCount +
              receiverFieldArithmeticCount + binaryWriteCount - compoundAssignmentWriteCount +
              leadingLocalConditionalArithmeticCount + receiverCallComparisonArgumentCount) {
    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        registries, 0);
  }
  if (!unsupportedNonErasureFacts(facts)) {
    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        registries, 0);
  }
  if (candidate.impl->patterns.size() != declarationCount) {
    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        registries, 0);
  }
  if (static_cast<int64_t>(candidate.impl->localReferences.size() + localFieldProjectionCount +
                           localAliasReborrowCount + localBorrowCount) +
          sequentialLocalReferenceCorrection + leadingLocalConditionalCorrection -
          leadingLocalConditionalArithmeticLocalCount + forLoopAccumulatorCorrection !=
      static_cast<int64_t>(localReturnCount) + discardedStatementCallCount -
          directAggregateCallCount - directScalarLocalCallCount + binaryWriteLocalOperands) {
    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        registries, 0);
  }
  if (parameterReferenceCount + parameterIndexCount + parameterReborrowCount >
      functionCount + localAliasReborrowCount + effectiveConditionalCount * 2 +
          equalityConditionalCount * 2 + loopCount + sequentialParameterInitializers +
          sequentialParameterReturns + sequentialBinaryParameterOperands +
          binaryWriteParameterOperands) {
    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        registries, 0);
  }
  if (candidate.impl->blocks.size() != functionCount ||
      candidate.impl->returns.size() != functionCount - voidFunctionCount) {
    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        registries, 0);
  }
  if (static_cast<int64_t>(candidate.impl->expressions.size()) !=
      static_cast<int64_t>(
          declarationCount + functionCount - voidFunctionCount - directCallCount - aggregateCount -
          receiverSelfCallCount - uninitializedLocalReturnCount - parameterReferenceCount -
          parameterReborrowCount - parameterFieldProjectionCount + localAliasReborrowCount +
          localWriteCount + effectiveConditionalCount * 2 + equalityConditionalCount + loopCount +
          binaryWriteCount + parameterFieldWriteCount + receiverFieldArithmeticCount +
          directAggregateCallCount + directScalarLocalCallCount) +
          sequentialLiteralCorrection + sequentialTernaryParameterConditions +
          leadingLocalConditionalCorrection + leadingLocalConditionalArithmeticParameterCount +
          leadingLocalConditionalArithmeticLiteralCount - leadingLocalConditionalArithmeticCount -
          binaryWriteLocalOperands + forLoopAccumulatorCorrection) {
    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        registries, 0);
  }
  if (executableDefinitions != declarationCount + functionCount ||
      facts.definitionTypes().size() != declarationCount) {
    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        registries, 0);
  }
  if (facts.nodeTypes().size() !=
          declarationCount + functionCount - voidFunctionCount + directCallCount +
              receiverCallCount * 2 + localReturnCount + deadLocalReturnCount -
              uninitializedLocalReturnCount + localWriteCount * 3 + aggregateElementCount +
              deadNodeTypesExtra + localFieldProjectionCount + localFieldWriteCount +
              parameterIndexCount * 2 + parameterReborrowCount * 2 + directCallArgumentCount +
              receiverCallArgumentCount + receiverCallFieldArgumentCount +
              receiverCallComparisonArgumentCount * 3 + localBorrowCount + unsafeBlockCount +
              effectiveConditionalCount * 2 + equalityConditionalCount * 2 - unaryReturnCount +
              matchReturnCount * 2 - matchDefaultArmCount + loopCount + sequentialBinaryCount * 2 +
              binaryWriteCount * 2 + parameterFieldProjectionCount +
              receiverFieldArithmeticCount * 2 + parameterFieldWriteCount * 4 +
              discardedStatementCallCount + sequentialCastInitializers +
              sequentialTernaryCount * 3 - leadingLocalConditionalUnaryCount -
              leadingLocalConditionalArithmeticCount + leadingLocalConditionalArithmeticCount * 2 -
              postfixIncrementWriteCount * 3 - compoundAssignmentWriteCount * 2 ||
      static_cast<int64_t>(facts.literals().size()) !=
          static_cast<int64_t>(
              declarationCount + functionCount - voidFunctionCount - directCallCount -
              aggregateCount - receiverSelfCallCount - uninitializedLocalReturnCount -
              parameterReferenceCount - parameterReborrowCount - parameterFieldProjectionCount +
              localAliasReborrowCount + localWriteCount + aggregateElementCount +
              directCallLiteralArgumentCount + receiverCallArgumentCount -
              receiverCallFieldArgumentCount + effectiveConditionalCount * 2 +
              matchReturnCount * 2 - matchDefaultArmCount + equalityConditionalCount -
              unaryReturnCount + loopCount + binaryWriteCount + parameterFieldWriteCount +
              receiverFieldArithmeticCount + directAggregateCallCount +
              directScalarLocalCallCount) +
              sequentialLiteralCorrection + sequentialTernaryParameterConditions +
              leadingLocalConditionalCorrection - leadingLocalConditionalUnaryCount +
              leadingLocalConditionalArithmeticParameterCount +
              leadingLocalConditionalArithmeticLiteralCount -
              leadingLocalConditionalArithmeticCount - binaryWriteLocalOperands -
              postfixIncrementWriteCount + deadLiterals + forLoopAccumulatorCorrection ||
      facts.calls().size() !=
          directCallCount + receiverCallCount + parameterIndexCount + equalityConditionalCount +
              sequentialBinaryCount + receiverFieldArithmeticCount + binaryWriteCount -
              compoundAssignmentWriteCount + leadingLocalConditionalArithmeticCount +
              receiverCallComparisonArgumentCount + deadCalls ||
      facts.casts().size() != sequentialCastInitializers + deadCasts ||
      facts.patterns().size() != declarationCount ||
      facts.aggregates().size() != aggregateCount + deadAggregateCount ||
      facts.members().size() != localFieldProjectionCount + localFieldWriteCount +
                                    receiverCallCount + receiverCallFieldArgumentCount +
                                    receiverCallComparisonArgumentCount +
                                    parameterFieldProjectionCount + parameterFieldWriteCount ||
      facts.places().size() != localFieldProjectionCount + localFieldWriteCount +
                                   receiverCallFieldArgumentCount +
                                   receiverCallComparisonArgumentCount + parameterIndexCount +
                                   parameterFieldProjectionCount + parameterFieldWriteCount ||
      facts.indexes().size() != parameterIndexCount ||
      facts.markerObligations().size() != parameterIndexCount) {
    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        registries, 0);
  }

  size_t expectedConstantCount = 0;
  for (const auto& declaration : candidate.impl->declarations) {
    if (declaration.definitionKind == identity::DefinitionKind::Constant) {
      ++expectedConstantCount;
    }
  }
  if (facts.constants().size() != expectedConstantCount) {
    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::AdditionalFact, module, registries, 0);
  }

  for (size_t sourceIndex = 0; sourceIndex < declarationCount; ++sourceIndex) {
    const auto index = static_cast<uint32_t>(sourceIndex);
    const auto& declaration = candidate.impl->declarations[index];
    const auto& pattern = candidate.impl->patterns[index];
    const auto& expression = candidate.impl->expressions[index];
    const uint32_t expectedDeclaration = index * 3 + 1;
    if (declaration.node.ordinal() != expectedDeclaration ||
        pattern.node.ordinal() != expectedDeclaration + 1 ||
        expression.node.ordinal() != expectedDeclaration + 2 ||
        declaration.pattern != pattern.node || declaration.initializer != expression.node ||
        pattern.binding != declaration.definition || declaration.inferredType != pattern.type ||
        pattern.type != pattern.scrutineeType || declaration.inferredType != expression.type ||
        expression.category != HirValueCategory::Value || !pattern.reachable ||
        !typeExists(declaration.declaredType, semanticTypes) ||
        !typeExists(declaration.inferredType, semanticTypes) ||
        (index != 0 && declaration.sourceSpan.byteStart() <
                           candidate.impl->declarations[index - 1].sourceSpan.byteStart())) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }

    auto sourceDefinitionIndex = definitionIndex(definitions, declaration.definition);
    if (sourceDefinitionIndex == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::AdditionalFact, module, registries,
                                          index + 1);
    }
    size_t definitionSlot = 0;
    ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
    const auto& sourceDefinition = definitions.definitions()[definitionSlot];
    if (!hasExecutableBody(sourceDefinition, definitions) ||
        !definitionBelongsToModule(sourceDefinition, definitions) ||
        sourceDefinition.record.kind() != declaration.definitionKind) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& tree = bound.tree();
    const auto definitionInventory = binder::DefinitionInventory::collect(tree);
    auto patternSite = patternBindingSite(definitionInventory, sourceDefinition);
    if (patternSite == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& bindingSite = ZC_ASSERT_NONNULL(patternSite);
    if (bindingSite.patternPath.size() != 0 || !tree.contains(bindingSite.introducer) ||
        tree.node(bindingSite.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& declarator = tree.node(bindingSite.introducer);
    const ast::NodeId patternNode(declarator.payload.words[ast::kVariableDeclaratorPatternWord]);
    const ast::NodeId initializer(declarator.payload.words[ast::kVariableDeclaratorInitWord]);
    if (!tree.contains(patternNode) ||
        tree.node(patternNode).kind != ast::SyntaxKind::IdentifierPattern ||
        !tree.contains(initializer) || !isScalarLiteral(tree.node(initializer).kind)) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    auto declarationSourceSpan = bound.parsedModule().spanFor(declarator.range);
    auto initializerSourceSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
    auto definitionTypeIndex = factIndex(facts.definitionTypes(), declaration.definition);
    auto patternFactIndex = factIndex(facts.patterns(), patternNode);
    auto nodeTypeIndex = factIndex(facts.nodeTypes(), initializer);
    auto literalIndex = factIndex(facts.literals(), initializer);
    auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), declaration.definition);
    auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), declaration.definition);
    if (definitionTypeIndex == zc::none || patternFactIndex == zc::none ||
        nodeTypeIndex == zc::none || literalIndex == zc::none || signaturePosition == zc::none ||
        rootPosition == zc::none || declarationSourceSpan == zc::none ||
        initializerSourceSpan == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    size_t definitionTypeSlot = 0;
    size_t patternFactSlot = 0;
    size_t nodeTypeSlot = 0;
    size_t literalSlot = 0;
    size_t signatureSlot = 0;
    size_t rootSlot = 0;
    ZC_IF_SOME(value, definitionTypeIndex) { definitionTypeSlot = value; }
    ZC_IF_SOME(value, patternFactIndex) { patternFactSlot = value; }
    ZC_IF_SOME(value, nodeTypeIndex) { nodeTypeSlot = value; }
    ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
    ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
    ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
    const auto& definitionType = facts.definitionTypes().entries()[definitionTypeSlot].value;
    const auto& patternFact = facts.patterns().entries()[patternFactSlot].value;
    const auto& nodeType = facts.nodeTypes().entries()[nodeTypeSlot].value;
    const auto& literalFact = facts.literals().entries()[literalSlot].value;
    const auto& signature = signatures.definitions[signatureSlot];
    const auto& root = signatures.roots[rootSlot];
    if (!signature.payload.variant().is<checker::signature::ValueSignature>() ||
        !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& valueSignature =
        signature.payload.variant().get<checker::signature::ValueSignature>();
    auto expectedVisibility = visibility(root.visibility);
    auto expectedLinkage = linkage(valueSignature);
    bool visibilityMatches = false;
    bool linkageMatches = false;
    bool declarationSpanMatches = false;
    bool initializerSpanMatches = false;
    ZC_IF_SOME(value, expectedVisibility) {
      visibilityMatches = sameVisibility(declaration.visibility, value);
    }
    ZC_IF_SOME(value, expectedLinkage) { linkageMatches = declaration.linkage == value; }
    ZC_IF_SOME(value, declarationSourceSpan) {
      declarationSpanMatches = sameSpan(declaration.sourceSpan, value);
    }
    ZC_IF_SOME(value, initializerSourceSpan) {
      initializerSpanMatches = sameSpan(expression.sourceSpan, value);
    }
    if (declaration.inferredType != definitionType || expression.type != nodeType ||
        pattern.type != patternFact.scrutineeType || patternFact.bindings.size() != 1 ||
        patternFact.bindings[0].binding != declaration.definition ||
        patternFact.bindings[0].type != pattern.type ||
        !sameConstant(expression.value, literalFact.literal, module, registries, semanticTypes) ||
        !sameSpan(expression.sourceSpan, literalFact.sourceSpan) ||
        !sameSpan(pattern.sourceSpan, sourceDefinition.source) || !declarationSpanMatches ||
        !initializerSpanMatches || !visibilityMatches || !linkageMatches ||
        signature.definition != declaration.definition ||
        signature.definitionKind != declaration.definitionKind ||
        !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
        root.canonicalDefinition != declaration.definition || root.sourceModule != module ||
        valueSignature.type != declaration.declaredType ||
        declaration.declaredType != declaration.inferredType ||
        valueSignature.mutability != declaration.mutability || !valueSignature.hasInitializer ||
        patternFact.refinements.size() != 0 ||
        !patternFact.constructor.variant().is<checker::checked::WildcardPattern>() ||
        !patternFact.reachable || patternFact.guardMayRaise != zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }

    auto constantIndex = factIndex(facts.constants(), declaration.definition);
    if (declaration.definitionKind == identity::DefinitionKind::Constant) {
      if (constantIndex == zc::none || declaration.constantValue == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t constantSlot = 0;
      ZC_IF_SOME(value, constantIndex) { constantSlot = value; }
      bool same = false;
      ZC_IF_SOME(value, declaration.constantValue) {
        same = sameConstant(value, facts.constants().entries()[constantSlot].value.value, module,
                            registries, semanticTypes);
      }
      bool signatureConstantMatches = false;
      ZC_IF_SOME(value, valueSignature.constantValue) {
        signatureConstantMatches =
            sameConstant(value, facts.constants().entries()[constantSlot].value.value, module,
                         registries, semanticTypes);
      }
      const auto& constantFact = facts.constants().entries()[constantSlot].value;
      if (!same || !signatureConstantMatches || constantFact.expression != initializer ||
          constantFact.type != declaration.inferredType || constantFact.dependencies.size() != 0) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
    } else if (constantIndex != zc::none || declaration.constantValue != zc::none ||
               valueSignature.constantValue != zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::AdditionalFact, module, registries,
                                          index + 1);
    }
  }

  uint32_t nextFunction = static_cast<uint32_t>(declarationCount * 3 + 1);
  for (size_t sourceIndex = 0; sourceIndex < functionCount; ++sourceIndex) {
    const auto index = static_cast<uint32_t>(sourceIndex);
    const auto& function = candidate.impl->functions[index];
    const auto& block = candidate.impl->blocks[index];
    const uint32_t expectedFunction = nextFunction;
    // Recompute the source shape once for this function. A void method has no
    // return record, so the historical parallel returns[index] binding is
    // replaced by a scan for the return whose node is the block's trailing
    // statement id.
    zc::Maybe<FunctionReturnShape> sourceShapeMaybe;
    {
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex != zc::none) {
        size_t definitionSlot = 0;
        ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
        const auto& sourceDefinition = definitions.definitions()[definitionSlot];
        const auto& tree = bound.tree();
        if (tree.contains(sourceDefinition.node)) {
          sourceShapeMaybe = functionReturnShape(tree, tree.node(sourceDefinition.node));
        }
      }
    }
    bool isVoidShape = false;
    ZC_IF_SOME(value, sourceShapeMaybe) { isVoidShape = value.isVoidBody; }
    bool isSequentialShape = false;
    ZC_IF_SOME(value, sourceShapeMaybe) { isSequentialShape = value.isSequentialLocalReturn; }
    if (isVoidShape) {
      const FunctionReturnShape& voidSource = ZC_ASSERT_NONNULL(sourceShapeMaybe);
      // Void mutating-method branch: function F, body F+1, write F+2, write
      // value parameter reference F+3; no return record exists.
      if (block.statements.size() != 1) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      zc::Maybe<const HirParameterFieldWriteStatement&> voidWriteRecord;
      for (const auto& write : candidate.impl->parameterFieldWrites) {
        if (write.node != block.statements[0]) continue;
        if (voidWriteRecord != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        voidWriteRecord = write;
      }
      if (function.receiver == zc::none || voidWriteRecord == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const auto& voidWrite = ZC_ASSERT_NONNULL(voidWriteRecord);
      const auto& voidReceiver = ZC_ASSERT_NONNULL(function.receiver);
      const auto voidValueNodeId = hirId(expectedFunction + 3);
      if (function.node != hirId(expectedFunction) || block.node != hirId(expectedFunction + 1) ||
          function.body != block.node || block.statements[0] != hirId(expectedFunction + 2) ||
          voidWrite.node != hirId(expectedFunction + 2) || voidWrite.value != voidValueNodeId ||
          voidWrite.parameter != voidReceiver.key) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      zc::Maybe<const HirParameterReferenceExpression&> voidValueRecord;
      for (const auto& reference : candidate.impl->parameterReferences) {
        if (reference.node != voidValueNodeId) continue;
        if (voidValueRecord != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        voidValueRecord = reference;
      }
      if (voidValueRecord == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      const auto& voidValue = ZC_ASSERT_NONNULL(voidValueRecord);
      bool voidParameterMatches = false;
      for (const auto& parameter : function.parameters) {
        if (parameter.key == voidValue.parameter && parameter.type == voidWrite.type) {
          voidParameterMatches = true;
        }
      }
      if (!voidParameterMatches || voidValue.type != voidWrite.type ||
          voidValue.category != HirValueCategory::Place) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      // No return record may lie in this function's id range.
      for (const auto& candidateReturn : candidate.impl->returns) {
        if (candidateReturn.node.ordinal() >= expectedFunction &&
            candidateReturn.node.ordinal() < expectedFunction + 4) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
      }
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t sourceDefinitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { sourceDefinitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[sourceDefinitionSlot];
      const auto& tree = bound.tree();
      if (sourceDefinition.record.kind() != identity::DefinitionKind::Method ||
          !hasExecutableBody(sourceDefinition, definitions) ||
          !definitionBelongsToModule(sourceDefinition, definitions) ||
          !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
          !tree.contains(sourceDefinition.node) ||
          tree.node(sourceDefinition.node).kind != ast::SyntaxKind::MethodDecl ||
          !voidSource.writesReceiverField || !voidSource.voidWriteValueIsParameter) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const ast::NodeId voidAssignment = voidSource.receiverWriteAssignment;
      const ast::NodeId voidTargetNode(
          tree.node(voidAssignment).payload.words[ast::kAssignmentExprLhsWord]);
      const ast::NodeId voidThisNode(
          tree.node(voidTargetNode).payload.words[ast::kMemberExpressionObjectWord]);
      auto bodySpan = bound.parsedModule().spanFor(tree.node(voidSource.body).range);
      auto sourceWriteSpan =
          bound.parsedModule().spanFor(tree.node(voidSource.receiverWriteStatement).range);
      auto sourceWriteValueSpan =
          bound.parsedModule().spanFor(tree.node(voidSource.receiverWriteValue).range);
      auto voidValueSpan =
          bound.parsedModule().spanFor(tree.node(voidSource.receiverWriteValue).range);
      if (bodySpan == zc::none || sourceWriteSpan == zc::none || sourceWriteValueSpan == zc::none ||
          voidValueSpan == zc::none || !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) ||
          !sameSpan(voidWrite.sourceSpan, ZC_ASSERT_NONNULL(sourceWriteSpan)) ||
          !sameSpan(voidWrite.valueSpan, ZC_ASSERT_NONNULL(sourceWriteValueSpan)) ||
          !sameSpan(voidValue.sourceSpan, ZC_ASSERT_NONNULL(voidValueSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto voidReceiverLookup = semanticTypes.get(voidReceiver.type);
      if (!voidReceiverLookup.is<type::SemanticTypeLookup>() ||
          !voidReceiverLookup.get<type::SemanticTypeLookup>()
               .data()
               .is<type::semantic::ReferenceTypeData>() ||
          voidReceiverLookup.get<type::SemanticTypeLookup>()
                  .data()
                  .get<type::semantic::ReferenceTypeData>()
                  .mutability != type::semantic::Mutability::Mutable ||
          voidReceiverLookup.get<type::SemanticTypeLookup>()
                  .data()
                  .get<type::semantic::ReferenceTypeData>()
                  .referent != voidWrite.receiverType) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto voidThisTypeIndex = factIndex(facts.nodeTypes(), voidThisNode);
      auto voidMemberIndex = factIndex(facts.members(), voidTargetNode);
      auto voidPlaceIndex = factIndex(facts.places(), voidTargetNode);
      auto voidRhsTypeIndex = factIndex(facts.nodeTypes(), voidSource.receiverWriteValue);
      auto voidRhsLiteralIndex = factIndex(facts.literals(), voidSource.receiverWriteValue);
      if (voidThisTypeIndex == zc::none || voidMemberIndex == zc::none ||
          voidPlaceIndex == zc::none || voidRhsTypeIndex == zc::none ||
          voidRhsLiteralIndex != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      size_t voidThisTypeSlot = 0;
      size_t voidMemberSlot = 0;
      size_t voidPlaceSlot = 0;
      size_t voidRhsTypeSlot = 0;
      ZC_IF_SOME(slot, voidThisTypeIndex) { voidThisTypeSlot = slot; }
      ZC_IF_SOME(slot, voidMemberIndex) { voidMemberSlot = slot; }
      ZC_IF_SOME(slot, voidPlaceIndex) { voidPlaceSlot = slot; }
      ZC_IF_SOME(slot, voidRhsTypeIndex) { voidRhsTypeSlot = slot; }
      const auto& voidMemberFact = facts.members().entries()[voidMemberSlot].value;
      const auto& voidPlaceFact = facts.places().entries()[voidPlaceSlot].value;
      const auto& voidPlaceRoot = voidPlaceFact.root.variant();
      if (facts.nodeTypes().entries()[voidThisTypeSlot].value != voidReceiver.type ||
          facts.nodeTypes().entries()[voidRhsTypeSlot].value != voidWrite.type ||
          voidMemberFact.node != voidTargetNode || voidMemberFact.member != voidWrite.field ||
          voidMemberFact.memberType != voidWrite.type ||
          voidMemberFact.receiverType != voidWrite.receiverType ||
          voidMemberFact.adjustment != zc::none || voidPlaceFact.node != voidTargetNode ||
          voidPlaceFact.type != voidWrite.type ||
          !voidPlaceRoot.is<checker::checked::CallableParameterPlaceRoot>() ||
          voidPlaceFact.projections.size() != 1 ||
          !voidPlaceFact.projections[0].variant().is<checker::checked::FieldProjection>() ||
          voidPlaceFact.projections[0].variant().get<checker::checked::FieldProjection>().field !=
              voidWrite.field) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto voidRootAuthority = registries.callableParameter(
          voidPlaceRoot.get<checker::checked::CallableParameterPlaceRoot>().parameter);
      if (voidRootAuthority == zc::none ||
          ZC_ASSERT_NONNULL(voidRootAuthority).key() != voidReceiver.key) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), function.definition);
      if (signaturePosition == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t signatureSlot = 0;
      ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
      const auto& signature = signatures.definitions[signatureSlot];
      if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
          !signature.scope.variant().is<checker::signature::MemberSignatureScope>() ||
          signature.definition != function.definition ||
          signature.definitionKind != identity::DefinitionKind::Method) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const auto& memberScope =
          signature.scope.variant().get<checker::signature::MemberSignatureScope>();
      const auto& callable =
          signature.payload.variant().get<checker::signature::CallableSignature>();
      const auto expectedVisibility = memberVisibility(memberScope.visibility, module);
      const auto expectedLinkage = linkage(callable);
      auto voidResultLookup = semanticTypes.get(callable.success);
      const bool callableSuccessIsUnit =
          voidResultLookup.is<type::SemanticTypeLookup>() &&
          voidResultLookup.get<type::SemanticTypeLookup>().data().primitiveKind() != zc::none &&
          ZC_ASSERT_NONNULL(
              voidResultLookup.get<type::SemanticTypeLookup>().data().primitiveKind()) ==
              type::semantic::PrimitiveKind::Unit;
      if (memberScope.owner == function.definition || callable.raises != zc::none ||
          callable.receiver == zc::none ||
          ZC_ASSERT_NONNULL(callable.receiver).mode != checker::signature::ReceiverMode::Mutable ||
          ZC_ASSERT_NONNULL(callable.receiver).parameter != voidReceiver.key ||
          callable.parameters.size() != function.parameters.size() ||
          function.parameters.size() != 1 || !callableSuccessIsUnit ||
          function.resultType != callable.success || expectedLinkage == zc::none ||
          !sameVisibility(function.visibility, expectedVisibility) ||
          function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
          !sameSpan(function.sourceSpan, sourceDefinition.source) ||
          !sameSpan(signature.declarationSpan, sourceDefinition.source)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += 4;
      continue;
    }
    // Every non-void body block ends with its return statement id. Resolve the
    // return record by that trailing id rather than by parallel index.
    const HirReturnStatement* returnStatementPtr = nullptr;
    if (block.statements.empty()) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    {
      const HirNodeId trailingNode = block.statements[block.statements.size() - 1];
      for (const auto& candidateReturn : candidate.impl->returns) {
        if (candidateReturn.node != trailingNode) continue;
        if (returnStatementPtr != nullptr) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        returnStatementPtr = &candidateReturn;
      }
    }
    if (returnStatementPtr == nullptr) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    const auto& returnStatement = *returnStatementPtr;
    // Sequential N-local shape: recompute the source classification and verify
    // the materialized bindings against it and against checked facts. Each
    // binding consumes its localNode then its initializerNode, plus two operand
    // nodes when the initializer is a primitive binary; after the last binding
    // come returnNode, returnValueNode, and an optional unsafe block. The
    // materializer and this verifier derive the same per-binding widths, so the
    // ordinals agree.
    if (isSequentialShape) {
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          !definitionBelongsToModule(sourceDefinition, definitions) ||
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
          !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
          !tree.contains(sourceDefinition.node)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sequentialShapeMaybe = zc::Maybe<SequentialLocalShape>(zc::none);
      // Recompute the classified source shape directly from the function body.
      auto functionShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (functionShape == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ast::NodeId sourceBody;
      ZC_IF_SOME(value, functionShape) { sourceBody = value.body; }
      sequentialShapeMaybe = sequentialLocalShape(tree, sourceBody);
      if (sequentialShapeMaybe == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      SequentialLocalShape source{};
      ZC_IF_SOME(value, sequentialShapeMaybe) { source = zc::mv(value); }
      // Dead-erase slice: apply the same erasure-cone filter the builder
      // uses, so the verifier validates the HIR against the filtered shape
      // (the one the builder actually lowered). Without this filter the
      // verifier would expect HIR nodes for bindings the builder skipped.
      {
        const auto dead = deadSequentialBindings(tree, source);
        const auto removed = erasureRelatedDeadBindings(tree, source, dead, deadEraseInitializers);
        bool anyRemoved = false;
        for (const bool isRemoved : removed) {
          if (isRemoved) anyRemoved = true;
        }
        if (anyRemoved) { source = filterDeadSequentialBindings(source, removed); }
      }
      const size_t bindingCount = source.bindings.size();
      // Signature and function metadata.
      auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), function.definition);
      auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
      auto bodySpan = bound.parsedModule().spanFor(tree.node(sourceBody).range);
      if (signaturePosition == zc::none || rootPosition == zc::none || bodySpan == zc::none ||
          tree.node(sourceDefinition.node).kind != ast::SyntaxKind::FunctionDecl) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t signatureSlot = 0;
      size_t rootSlot = 0;
      ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
      ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
      const auto& signature = signatures.definitions[signatureSlot];
      const auto& root = signatures.roots[rootSlot];
      auto expectedVisibility = visibility(root.visibility);
      if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
          !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>() ||
          expectedVisibility == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const auto& callable =
          signature.payload.variant().get<checker::signature::CallableSignature>();
      auto expectedLinkage = linkage(callable);
      auto returnTypeIndex = factIndex(facts.nodeTypes(), source.returnValue);
      if (returnTypeIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t returnTypeSlot = 0;
      ZC_IF_SOME(value, returnTypeIndex) { returnTypeSlot = value; }
      const auto sequentialType = facts.nodeTypes().entries()[returnTypeSlot].value;
      if (expectedLinkage == zc::none || signature.definition != function.definition ||
          signature.definitionKind != identity::DefinitionKind::Function ||
          root.canonicalDefinition != function.definition || root.sourceModule != module ||
          callable.receiver != zc::none || callable.raises != zc::none ||
          callable.success != function.resultType || function.resultType != sequentialType ||
          !typeExists(sequentialType, semanticTypes) ||
          !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
          !sameSpan(function.sourceSpan, sourceDefinition.source) ||
          !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
          function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
          !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) ||
          function.node != hirId(expectedFunction) || block.node != hirId(expectedFunction + 1) ||
          function.body != block.node || function.parameters.size() != callable.parameters.size() ||
          block.statements.size() != bindingCount + 1) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      for (size_t parameterIndex = 0; parameterIndex < function.parameters.size();
           ++parameterIndex) {
        const auto& parameter = function.parameters[parameterIndex];
        const auto& sourceParameter = callable.parameters[parameterIndex];
        if (parameter.key != sourceParameter.parameter || parameter.type != sourceParameter.type ||
            sourceParameter.hasDefault || !typeExists(parameter.type, semanticTypes)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      // Per-binding node width: 2 (local + initializer) plus 2 operand nodes for
      // a primitive-binary initializer, plus 2 more inner leaf-operand nodes for
      // each operand that is itself a nested one-level primitive binary. The
      // return statement follows the last binding's nodes. This mirrors the
      // materializer's id allocation exactly.
      auto bindingWidth = [&](const SequentialLocalBinding& binding) -> uint32_t {
        if (binding.initializerKind == SequentialInitializerKind::Ternary) {
          // local + conditional + condition + then-branch + else-branch
          return 5u;
        }
        if (binding.initializerKind != SequentialInitializerKind::PrimitiveBinary &&
            binding.initializerKind != SequentialInitializerKind::PrimitiveUnary)
          return 2u;
        uint32_t width = 4u;
        for (const auto* operand : {&binding.leftOperand, &binding.rightOperand}) {
          ZC_IF_SOME(value, *operand) {
            if (value.kind == SequentialBinaryOperandKind::NestedBinary) width += 2u;
          }
        }
        return width;
      };
      uint32_t bindingNodeSpan = 0;
      for (const auto& binding : source.bindings) bindingNodeSpan += bindingWidth(binding);
      const uint32_t returnNodeOrdinal = expectedFunction + 2 + bindingNodeSpan;
      // Verify the return statement structure.
      auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
      auto returnValueSpan = bound.parsedModule().spanFor(tree.node(source.returnValue).range);
      if (returnStatement.node != hirId(returnNodeOrdinal) ||
          returnStatement.value != hirId(returnNodeOrdinal + 1) ||
          returnStatement.resultType != sequentialType || returnSpan == zc::none ||
          returnValueSpan == zc::none ||
          !sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      // Verify each binding: its local node, initializer node, type, spans, and
      // the checked fact for its initializer.
      bool bindingsValid = true;
      zc::Vector<binder::OwnerLocalBindingId> localBindingIds;
      uint32_t bindingOrdinal = expectedFunction + 2;
      for (size_t bindingIndex = 0; bindingIndex < bindingCount && bindingsValid; ++bindingIndex) {
        const auto& binding = source.bindings[bindingIndex];
        const uint32_t localNodeOrdinal = bindingOrdinal;
        const uint32_t initializerNodeOrdinal = localNodeOrdinal + 1;
        bindingOrdinal += bindingWidth(binding);
        zc::Maybe<const HirLocalBinding&> localBinding;
        for (const auto& local : candidate.impl->locals) {
          if (local.node != hirId(localNodeOrdinal)) continue;
          if (localBinding != zc::none) { bindingsValid = false; }
          localBinding = local;
        }
        if (localBinding == zc::none || block.statements[bindingIndex] != hirId(localNodeOrdinal)) {
          bindingsValid = false;
          break;
        }
        const auto& localValue = ZC_ASSERT_NONNULL(localBinding);
        auto patternSpan = bound.parsedModule().spanFor(tree.node(binding.pattern).range);
        auto initializerSpan = bound.parsedModule().spanFor(tree.node(binding.initializer).range);
        auto ownerBinding = ownerLocalBindingForPattern(definitions, binding.pattern, tree);
        if (patternSpan == zc::none || initializerSpan == zc::none || ownerBinding == zc::none ||
            localValue.local != hirLocalId(static_cast<uint32_t>(bindingIndex + 1)) ||
            localValue.initializer != hirId(initializerNodeOrdinal) ||
            !sameSpan(localValue.sourceSpan, ZC_ASSERT_NONNULL(patternSpan)) ||
            localValue.initializerSpan == zc::none ||
            !sameSpan(ZC_ASSERT_NONNULL(localValue.initializerSpan),
                      ZC_ASSERT_NONNULL(initializerSpan)) ||
            !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(ownerBinding), binding.pattern,
                               tree)) {
          bindingsValid = false;
          break;
        }
        for (const auto existing : localBindingIds) {
          if (existing == ZC_ASSERT_NONNULL(ownerBinding)) bindingsValid = false;
        }
        if (!bindingsValid) break;
        localBindingIds.add(ZC_ASSERT_NONNULL(ownerBinding));
        // Each binding initializer carries the local's own declared type (the
        // checker verifies it matches the binding pattern); only the returned
        // local shares the function result type, checked separately above.
        auto initializerTypeIndex = factIndex(facts.nodeTypes(), binding.initializer);
        if (initializerTypeIndex == zc::none) {
          bindingsValid = false;
          break;
        }
        size_t initializerTypeSlot = 0;
        ZC_IF_SOME(value, initializerTypeIndex) { initializerTypeSlot = value; }
        const identity::SemanticTypeId bindingType =
            facts.nodeTypes().entries()[initializerTypeSlot].value;
        if (!typeExists(bindingType, semanticTypes) || localValue.type != bindingType) {
          bindingsValid = false;
          break;
        }
        if (binding.initializerKind == SequentialInitializerKind::Literal ||
            binding.initializerKind == SequentialInitializerKind::EnumVariant) {
          zc::Maybe<const HirScalarLiteralExpression&> literal;
          for (const auto& expression : candidate.impl->expressions) {
            if (expression.node != hirId(initializerNodeOrdinal)) continue;
            if (literal != zc::none) bindingsValid = false;
            literal = expression;
          }
          auto literalIndex = factIndex(facts.literals(), binding.initializer);
          if (!bindingsValid || literal == zc::none || literalIndex == zc::none) {
            bindingsValid = false;
            break;
          }
          size_t literalSlot = 0;
          ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          const auto& literalValue = ZC_ASSERT_NONNULL(literal);
          if (literalValue.type != bindingType ||
              literalValue.category != HirValueCategory::Value || literalFact.type != bindingType ||
              !sameConstant(literalValue.value, literalFact.literal, module, registries,
                            semanticTypes) ||
              !sameSpan(literalValue.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
            bindingsValid = false;
            break;
          }
        } else if (binding.initializerKind == SequentialInitializerKind::Aggregate) {
          zc::Maybe<const HirNominalAggregateExpression&> aggregate;
          for (const auto& expression : candidate.impl->aggregates) {
            if (expression.node != hirId(initializerNodeOrdinal)) continue;
            if (aggregate != zc::none) bindingsValid = false;
            aggregate = expression;
          }
          auto aggregateIndex = factIndex(facts.aggregates(), binding.initializer);
          if (!bindingsValid || aggregate == zc::none || aggregateIndex == zc::none) {
            bindingsValid = false;
            break;
          }
          size_t aggregateSlot = 0;
          ZC_IF_SOME(value, aggregateIndex) { aggregateSlot = value; }
          const auto& checkedAggregate = facts.aggregates().entries()[aggregateSlot].value;
          const auto& aggregateValue = ZC_ASSERT_NONNULL(aggregate);
          if (aggregateValue.type != bindingType ||
              aggregateValue.category != HirValueCategory::Value ||
              checkedAggregate.node != binding.initializer ||
              !checkedAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
              checkedAggregate.kind.variant()
                      .get<checker::checked::NominalAggregate>()
                      .definition != aggregateValue.definition ||
              checkedAggregate.resultType != aggregateValue.type ||
              !sameSpan(checkedAggregate.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan)) ||
              checkedAggregate.elements.size() != aggregateValue.elements.size()) {
            bindingsValid = false;
            break;
          }
          for (size_t elementIndex = 0; elementIndex < aggregateValue.elements.size();
               ++elementIndex) {
            const auto& checkedElement = checkedAggregate.elements[elementIndex];
            const auto& element = aggregateValue.elements[elementIndex];
            auto elementLiteral = factIndex(facts.literals(), checkedElement.sourceNode);
            if (checkedElement.field == zc::none || checkedElement.index != elementIndex ||
                checkedElement.sourceType != checkedElement.destinationType ||
                checkedElement.adjustment != zc::none || elementLiteral == zc::none ||
                ZC_ASSERT_NONNULL(checkedElement.field) != element.field ||
                checkedElement.destinationType != element.type) {
              bindingsValid = false;
              break;
            }
            size_t elementSlot = 0;
            ZC_IF_SOME(item, elementLiteral) { elementSlot = item; }
            const auto& checkedLiteral = facts.literals().entries()[elementSlot].value;
            if (checkedLiteral.type != element.type ||
                !sameConstant(checkedLiteral.literal, element.value, module, registries,
                              semanticTypes) ||
                !sameSpan(checkedLiteral.sourceSpan, element.sourceSpan)) {
              bindingsValid = false;
              break;
            }
          }
        } else if (binding.initializerKind == SequentialInitializerKind::LocalReference) {
          zc::Maybe<const HirLocalReferenceExpression&> reference;
          for (const auto& localReference : candidate.impl->localReferences) {
            if (localReference.node != hirId(initializerNodeOrdinal)) continue;
            if (reference != zc::none) bindingsValid = false;
            reference = localReference;
          }
          auto referenceBinding = resolvedOwnerLocal(bound.bindings(), binding.initializer);
          if (!bindingsValid || reference == zc::none || referenceBinding == zc::none ||
              binding.referencedLocal >= localBindingIds.size() ||
              ZC_ASSERT_NONNULL(referenceBinding) != localBindingIds[binding.referencedLocal] ||
              ZC_ASSERT_NONNULL(reference).local !=
                  hirLocalId(static_cast<uint32_t>(binding.referencedLocal + 1)) ||
              ZC_ASSERT_NONNULL(reference).type != bindingType ||
              ZC_ASSERT_NONNULL(reference).category != HirValueCategory::Place ||
              !sameSpan(ZC_ASSERT_NONNULL(reference).sourceSpan,
                        ZC_ASSERT_NONNULL(initializerSpan))) {
            bindingsValid = false;
            break;
          }
        } else if (binding.initializerKind == SequentialInitializerKind::ParameterReference) {
          zc::Maybe<const HirParameterReferenceExpression&> reference;
          for (const auto& parameterReference : candidate.impl->parameterReferences) {
            if (parameterReference.node != hirId(initializerNodeOrdinal)) continue;
            if (reference != zc::none) bindingsValid = false;
            reference = parameterReference;
          }
          auto parameterHandle = resolvedCallableParameter(bound.bindings(), binding.initializer);
          if (!bindingsValid || reference == zc::none || parameterHandle == zc::none) {
            bindingsValid = false;
            break;
          }
          bool parameterMatches = false;
          ZC_IF_SOME(handle, parameterHandle) {
            auto authority = registries.callableParameter(handle);
            ZC_IF_SOME(entry, authority) {
              parameterMatches = ZC_ASSERT_NONNULL(reference).parameter == entry.key();
            }
          }
          if (!parameterMatches || ZC_ASSERT_NONNULL(reference).type != bindingType ||
              ZC_ASSERT_NONNULL(reference).category != HirValueCategory::Place ||
              !sameSpan(ZC_ASSERT_NONNULL(reference).sourceSpan,
                        ZC_ASSERT_NONNULL(initializerSpan))) {
            bindingsValid = false;
            break;
          }
        } else if (binding.initializerKind == SequentialInitializerKind::PrimitiveUnary) {
          // PrimitiveUnary: the initializer node is a HirPrimitiveBinaryExpression
          // flagged isUnaryDesugar, referencing two operand nodes (left at +1,
          // right at +2). One operand is the real unary operand (parameter or
          // earlier local); the other is a synthetic constant with no source
          // fact. The checked call fact keys on the unary expression node and
          // carries one argument.
          if (binding.unaryOperation == zc::none || binding.unaryOperand == zc::none) {
            bindingsValid = false;
            break;
          }
          const uint32_t leftOperandOrdinal = initializerNodeOrdinal + 1;
          const uint32_t rightOperandOrdinal = initializerNodeOrdinal + 2;
          zc::Maybe<const HirPrimitiveBinaryExpression&> binary;
          for (const auto& operation : candidate.impl->primitiveBinaryOperations) {
            if (operation.node != hirId(initializerNodeOrdinal)) continue;
            if (binary != zc::none) bindingsValid = false;
            binary = operation;
          }
          auto callIndex = factIndex(facts.calls(), binding.initializer);
          if (!bindingsValid || binary == zc::none || callIndex == zc::none) {
            bindingsValid = false;
            break;
          }
          const auto& binaryValue = ZC_ASSERT_NONNULL(binary);
          size_t callSlot = 0;
          ZC_IF_SOME(value, callIndex) { callSlot = value; }
          const auto& callFact = facts.calls().entries()[callSlot].value;
          const auto& call = callFact.invocation;
          const auto& selected = call.selected.variant();
          if (!selected.is<checker::checked::PrimitiveCallable>()) {
            bindingsValid = false;
            break;
          }
          const auto unaryOperation = selected.get<checker::checked::PrimitiveCallable>().operation;
          const auto unaryOperandType =
              call.arguments.size() == 1 ? call.arguments[0].sourceType : bindingType;
          const ast::NodeId unaryOperandNode(
              tree.node(binding.initializer).payload.words[ast::kUnaryExpressionOperandWord]);
          // Neg desugars to 0 - x (synthetic on the left); the other three
          // place the synthetic on the right.
          const bool syntheticOnLeft =
              ZC_ASSERT_NONNULL(binding.unaryOperation) == checker::PrimitiveOperation::Neg;
          if (!binaryValue.isUnaryDesugar || !isScalarUnaryOperation(unaryOperation) ||
              unaryOperation != ZC_ASSERT_NONNULL(binding.unaryOperation) ||
              binaryValue.node != hirId(initializerNodeOrdinal) ||
              binaryValue.left != hirId(leftOperandOrdinal) ||
              binaryValue.right != hirId(rightOperandOrdinal) || binaryValue.type != bindingType ||
              binaryValue.category != HirValueCategory::Value ||
              !sameSpan(binaryValue.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan)) ||
              callFact.node != binding.initializer || call.calleeType != unaryOperandType ||
              call.receiver != zc::none || call.receiverMode != zc::none ||
              call.receiverAdjustment != zc::none || call.arguments.size() != 1 ||
              call.arguments[0].sourceNode != unaryOperandNode ||
              call.arguments[0].sourceType != unaryOperandType || call.successType != bindingType ||
              call.resultType != bindingType || call.substitutions != zc::none ||
              call.witnesses != zc::none || call.raises != zc::none) {
            bindingsValid = false;
            break;
          }
          // Verify the real operand at its ordinal.
          const auto& classified = ZC_ASSERT_NONNULL(binding.unaryOperand);
          const uint32_t realOrdinal = syntheticOnLeft ? rightOperandOrdinal : leftOperandOrdinal;
          auto realSpan = bound.parsedModule().spanFor(tree.node(classified.node).range);
          if (realSpan == zc::none) {
            bindingsValid = false;
            break;
          }
          bool realValid = false;
          if (classified.kind == SequentialBinaryOperandKind::LocalReference) {
            zc::Maybe<const HirLocalReferenceExpression&> reference;
            for (const auto& localReference : candidate.impl->localReferences) {
              if (localReference.node != hirId(realOrdinal)) continue;
              if (reference != zc::none) {
                bindingsValid = false;
                break;
              }
              reference = localReference;
            }
            if (reference == zc::none) {
              bindingsValid = false;
              break;
            }
            auto referenceBinding = resolvedOwnerLocal(bound.bindings(), classified.node);
            const auto& referenceValue = ZC_ASSERT_NONNULL(reference);
            realValid = referenceBinding != zc::none &&
                        classified.referencedLocal < localBindingIds.size() &&
                        ZC_ASSERT_NONNULL(referenceBinding) ==
                            localBindingIds[classified.referencedLocal] &&
                        referenceValue.local ==
                            hirLocalId(static_cast<uint32_t>(classified.referencedLocal + 1)) &&
                        referenceValue.type == unaryOperandType &&
                        referenceValue.category == HirValueCategory::Place &&
                        sameSpan(referenceValue.sourceSpan, ZC_ASSERT_NONNULL(realSpan));
          } else if (classified.kind == SequentialBinaryOperandKind::ParameterReference) {
            zc::Maybe<const HirParameterReferenceExpression&> reference;
            for (const auto& parameterReference : candidate.impl->parameterReferences) {
              if (parameterReference.node != hirId(realOrdinal)) continue;
              if (reference != zc::none) {
                bindingsValid = false;
                break;
              }
              reference = parameterReference;
            }
            if (reference == zc::none) {
              bindingsValid = false;
              break;
            }
            auto parameterHandle = resolvedCallableParameter(bound.bindings(), classified.node);
            bool parameterMatches = false;
            ZC_IF_SOME(handle, parameterHandle) {
              auto authority = registries.callableParameter(handle);
              ZC_IF_SOME(entry, authority) {
                parameterMatches = ZC_ASSERT_NONNULL(reference).parameter == entry.key();
              }
            }
            const auto& referenceValue = ZC_ASSERT_NONNULL(reference);
            realValid = parameterMatches && referenceValue.type == unaryOperandType &&
                        referenceValue.category == HirValueCategory::Place &&
                        sameSpan(referenceValue.sourceSpan, ZC_ASSERT_NONNULL(realSpan));
          }
          if (!realValid) {
            bindingsValid = false;
            break;
          }
          // Verify the synthetic constant operand at the other ordinal. It is
          // a scalar literal with no checker-produced literal fact.
          const uint32_t syntheticOrdinal =
              syntheticOnLeft ? leftOperandOrdinal : rightOperandOrdinal;
          zc::Maybe<const HirScalarLiteralExpression&> syntheticLiteral;
          for (const auto& expression : candidate.impl->expressions) {
            if (expression.node != hirId(syntheticOrdinal)) continue;
            if (syntheticLiteral != zc::none) {
              bindingsValid = false;
              break;
            }
            syntheticLiteral = expression;
          }
          if (syntheticLiteral == zc::none) {
            bindingsValid = false;
            break;
          }
          const auto& syntheticValue = ZC_ASSERT_NONNULL(syntheticLiteral);
          if (syntheticValue.type != unaryOperandType ||
              syntheticValue.category != HirValueCategory::Value) {
            bindingsValid = false;
            break;
          }
        } else if (binding.initializerKind == SequentialInitializerKind::Cast) {
          // Cast: the initializer node is a HirScalarLiteralExpression carrying
          // the inner literal with the cast result type. The cast is a no-op
          // for widening or identity conversions.
          zc::Maybe<const HirScalarLiteralExpression&> literal;
          for (const auto& expression : candidate.impl->expressions) {
            if (expression.node != hirId(initializerNodeOrdinal)) continue;
            if (literal != zc::none) bindingsValid = false;
            literal = expression;
          }
          auto literalIndex = factIndex(facts.literals(), binding.castInnerNode);
          if (!bindingsValid || literal == zc::none || literalIndex == zc::none) {
            bindingsValid = false;
            break;
          }
          size_t literalSlot = 0;
          ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          const auto& literalValue = ZC_ASSERT_NONNULL(literal);
          if (literalValue.type != bindingType ||
              literalValue.category != HirValueCategory::Value || literalFact.type != bindingType ||
              !sameConstant(literalValue.value, literalFact.literal, module, registries,
                            semanticTypes) ||
              !sameSpan(literalValue.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
            bindingsValid = false;
            break;
          }
        } else if (binding.initializerKind == SequentialInitializerKind::Ternary) {
          // Ternary: the initializer node is a HirConditionalExpression that
          // selects between two scalar-literal branch values based on a bool
          // condition. The condition, then-branch, and else-branch are separate
          // expression nodes at +1, +2, +3.
          const uint32_t conditionOrdinal = initializerNodeOrdinal + 1;
          const uint32_t thenOrdinal = initializerNodeOrdinal + 2;
          const uint32_t elseOrdinal = initializerNodeOrdinal + 3;
          zc::Maybe<const HirConditionalExpression&> conditional;
          for (const auto& cond : candidate.impl->conditionals) {
            if (cond.node != hirId(initializerNodeOrdinal)) continue;
            if (conditional != zc::none) bindingsValid = false;
            conditional = cond;
          }
          if (!bindingsValid || conditional == zc::none) {
            bindingsValid = false;
            break;
          }
          const auto& condValue = ZC_ASSERT_NONNULL(conditional);
          if (condValue.condition != hirId(conditionOrdinal) ||
              condValue.thenReturnValue != hirId(thenOrdinal) ||
              condValue.elseReturnValue != hirId(elseOrdinal) || condValue.type != bindingType ||
              condValue.category != HirValueCategory::Value ||
              !sameSpan(condValue.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
            bindingsValid = false;
            break;
          }
          // Verify the condition node: a bool local or parameter reference.
          auto condSpan = bound.parsedModule().spanFor(tree.node(binding.ternaryCondNode).range);
          auto condTypeIndex = factIndex(facts.nodeTypes(), binding.ternaryCondNode);
          if (condSpan == zc::none || condTypeIndex == zc::none) {
            bindingsValid = false;
            break;
          }
          size_t condTypeSlot = 0;
          ZC_IF_SOME(value, condTypeIndex) { condTypeSlot = value; }
          const identity::SemanticTypeId condType = facts.nodeTypes().entries()[condTypeSlot].value;
          // Resolve the condition to an earlier local, a parameter, or a bool
          // literal.
          bool conditionIsLocal = false;
          size_t conditionLocal = 0;
          for (size_t earlier = 0; earlier < bindingIndex; ++earlier) {
            if (matchesLocalReference(tree, source.bindings[earlier].pattern,
                                      binding.ternaryCondNode)) {
              conditionIsLocal = true;
              conditionLocal = earlier;
              break;
            }
          }
          if (binding.ternaryConditionIsLiteral) {
            // The condition is a bool literal; verify a scalar-literal
            // expression at the condition ordinal.
            zc::Maybe<const HirScalarLiteralExpression&> condLiteral;
            for (const auto& expression : candidate.impl->expressions) {
              if (expression.node != hirId(conditionOrdinal)) continue;
              if (condLiteral != zc::none) bindingsValid = false;
              condLiteral = expression;
            }
            if (!bindingsValid || condLiteral == zc::none) {
              bindingsValid = false;
              break;
            }
            auto condLiteralIndex = factIndex(facts.literals(), binding.ternaryCondNode);
            if (condLiteralIndex == zc::none) {
              bindingsValid = false;
              break;
            }
            size_t condLiteralSlot = 0;
            ZC_IF_SOME(value, condLiteralIndex) { condLiteralSlot = value; }
            const auto& condLiteralFact = facts.literals().entries()[condLiteralSlot].value;
            const auto& condLiteralValue = ZC_ASSERT_NONNULL(condLiteral);
            if (condLiteralValue.type != condType ||
                condLiteralValue.category != HirValueCategory::Value ||
                condLiteralFact.type != condType ||
                !sameConstant(condLiteralValue.value, condLiteralFact.literal, module, registries,
                              semanticTypes) ||
                !sameSpan(condLiteralValue.sourceSpan, ZC_ASSERT_NONNULL(condSpan))) {
              bindingsValid = false;
              break;
            }
          } else if (conditionIsLocal) {
            zc::Maybe<const HirLocalReferenceExpression&> condRef;
            for (const auto& reference : candidate.impl->localReferences) {
              if (reference.node != hirId(conditionOrdinal)) continue;
              if (condRef != zc::none) bindingsValid = false;
              condRef = reference;
            }
            if (!bindingsValid || condRef == zc::none) {
              bindingsValid = false;
              break;
            }
            const auto& refValue = ZC_ASSERT_NONNULL(condRef);
            if (refValue.local != hirLocalId(static_cast<uint32_t>(conditionLocal + 1)) ||
                refValue.type != condType || refValue.category != HirValueCategory::Place ||
                !sameSpan(refValue.sourceSpan, ZC_ASSERT_NONNULL(condSpan))) {
              bindingsValid = false;
              break;
            }
          } else {
            auto parameterHandle =
                resolvedCallableParameter(bound.bindings(), binding.ternaryCondNode);
            zc::Maybe<const HirParameterReferenceExpression&> condRef;
            for (const auto& reference : candidate.impl->parameterReferences) {
              if (reference.node != hirId(conditionOrdinal)) continue;
              if (condRef != zc::none) bindingsValid = false;
              condRef = reference;
            }
            if (!bindingsValid || condRef == zc::none || parameterHandle == zc::none) {
              bindingsValid = false;
              break;
            }
            const auto& refValue = ZC_ASSERT_NONNULL(condRef);
            bool parameterMatches = false;
            identity::CallableParameterId handle;
            ZC_IF_SOME(value, parameterHandle) { handle = value; }
            auto authority = registries.callableParameter(handle);
            ZC_IF_SOME(entry, authority) {
              parameterMatches = refValue.parameter == entry.key() && refValue.type == condType &&
                                 refValue.category == HirValueCategory::Place &&
                                 sameSpan(refValue.sourceSpan, ZC_ASSERT_NONNULL(condSpan));
            }
            if (!parameterMatches) {
              bindingsValid = false;
              break;
            }
          }
          // Verify the then-branch and else-branch literal nodes.
          for (const auto* branchNode : {&binding.ternaryThenNode, &binding.ternaryElseNode}) {
            const uint32_t branchOrdinal =
                branchNode == &binding.ternaryThenNode ? thenOrdinal : elseOrdinal;
            zc::Maybe<const HirScalarLiteralExpression&> branchLiteral;
            for (const auto& expression : candidate.impl->expressions) {
              if (expression.node != hirId(branchOrdinal)) continue;
              if (branchLiteral != zc::none) bindingsValid = false;
              branchLiteral = expression;
            }
            if (!bindingsValid || branchLiteral == zc::none) {
              bindingsValid = false;
              break;
            }
            auto literalIndex = factIndex(facts.literals(), *branchNode);
            if (literalIndex == zc::none) {
              bindingsValid = false;
              break;
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            const auto& literalValue = ZC_ASSERT_NONNULL(branchLiteral);
            auto branchSpan = bound.parsedModule().spanFor(tree.node(*branchNode).range);
            if (literalValue.type != bindingType ||
                literalValue.category != HirValueCategory::Value ||
                literalFact.type != bindingType ||
                !sameConstant(literalValue.value, literalFact.literal, module, registries,
                              semanticTypes) ||
                branchSpan == zc::none ||
                !sameSpan(literalValue.sourceSpan, ZC_ASSERT_NONNULL(branchSpan))) {
              bindingsValid = false;
              break;
            }
          }
          if (!bindingsValid) break;
        } else {
          // PrimitiveBinary: the initializer node is a HirPrimitiveBinaryExpression
          // referencing its two operand nodes (left at +1, right at +2). Each
          // operand materializes as a literal, parameter reference, or reference
          // to an earlier local, and its checked call fact keys on the initializer
          // node. This mirrors the return-position comparison verification.
          const uint32_t leftOperandOrdinal = initializerNodeOrdinal + 1;
          const uint32_t rightOperandOrdinal = initializerNodeOrdinal + 2;
          zc::Maybe<const HirPrimitiveBinaryExpression&> binary;
          for (const auto& operation : candidate.impl->primitiveBinaryOperations) {
            if (operation.node != hirId(initializerNodeOrdinal)) continue;
            if (binary != zc::none) bindingsValid = false;
            binary = operation;
          }
          auto callIndex = factIndex(facts.calls(), binding.initializer);
          if (!bindingsValid || binary == zc::none || callIndex == zc::none ||
              binding.leftOperand == zc::none || binding.rightOperand == zc::none) {
            bindingsValid = false;
            break;
          }
          const auto& binaryValue = ZC_ASSERT_NONNULL(binary);
          size_t callSlot = 0;
          ZC_IF_SOME(value, callIndex) { callSlot = value; }
          const auto& callFact = facts.calls().entries()[callSlot].value;
          const auto& call = callFact.invocation;
          const auto& selected = call.selected.variant();
          if (!selected.is<checker::checked::PrimitiveCallable>()) {
            bindingsValid = false;
            break;
          }
          const auto operation = selected.get<checker::checked::PrimitiveCallable>().operation;
          const bool comparison = isScalarComparisonOperation(operation);
          const bool arithmetic = isScalarArithmeticOperation(operation);
          const auto binaryOperandType = binaryValue.operandType;
          if ((!comparison && !arithmetic) || binaryValue.node != hirId(initializerNodeOrdinal) ||
              binaryValue.left != hirId(leftOperandOrdinal) ||
              binaryValue.right != hirId(rightOperandOrdinal) || binaryValue.type != bindingType ||
              binaryValue.category != HirValueCategory::Value ||
              binaryValue.operation != operation ||
              !sameSpan(binaryValue.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan)) ||
              (arithmetic && bindingType != binaryOperandType) ||
              callFact.node != binding.initializer || call.calleeType != binaryOperandType ||
              call.receiver != zc::none || call.receiverMode != zc::none ||
              call.receiverAdjustment != zc::none || call.arguments.size() != 2 ||
              call.arguments[0].sourceType != binaryOperandType ||
              call.arguments[1].sourceType != binaryOperandType ||
              call.successType != bindingType || call.resultType != bindingType ||
              call.substitutions != zc::none || call.witnesses != zc::none ||
              call.raises != zc::none) {
            bindingsValid = false;
            break;
          }
          // Verify one leaf operand node (literal, parameter, or earlier local)
          // at a fixed ordinal against its expected operand type.
          auto verifyLeaf = [&](uint32_t leafOrdinal, const SequentialBinaryLeafOperand& leaf,
                                identity::SemanticTypeId leafType) -> bool {
            auto leafSpan = bound.parsedModule().spanFor(tree.node(leaf.node).range);
            if (leafSpan == zc::none) return false;
            if (leaf.kind == SequentialBinaryOperandKind::Literal) {
              zc::Maybe<const HirScalarLiteralExpression&> literal;
              for (const auto& expression : candidate.impl->expressions) {
                if (expression.node != hirId(leafOrdinal)) continue;
                if (literal != zc::none) return false;
                literal = expression;
              }
              auto operandLiteral = factIndex(facts.literals(), leaf.node);
              if (literal == zc::none || operandLiteral == zc::none) return false;
              size_t literalSlot = 0;
              ZC_IF_SOME(value, operandLiteral) { literalSlot = value; }
              const auto& literalFact = facts.literals().entries()[literalSlot].value;
              const auto& literalValue = ZC_ASSERT_NONNULL(literal);
              return literalValue.type == leafType &&
                     literalValue.category == HirValueCategory::Value &&
                     literalFact.type == leafType &&
                     sameConstant(literalValue.value, literalFact.literal, module, registries,
                                  semanticTypes) &&
                     sameSpan(literalValue.sourceSpan, ZC_ASSERT_NONNULL(leafSpan));
            }
            if (leaf.kind == SequentialBinaryOperandKind::LocalReference) {
              zc::Maybe<const HirLocalReferenceExpression&> reference;
              for (const auto& localReference : candidate.impl->localReferences) {
                if (localReference.node != hirId(leafOrdinal)) continue;
                if (reference != zc::none) return false;
                reference = localReference;
              }
              auto referenceBinding = resolvedOwnerLocal(bound.bindings(), leaf.node);
              if (reference == zc::none || referenceBinding == zc::none ||
                  leaf.referencedLocal >= localBindingIds.size() ||
                  ZC_ASSERT_NONNULL(referenceBinding) != localBindingIds[leaf.referencedLocal]) {
                return false;
              }
              const auto& referenceValue = ZC_ASSERT_NONNULL(reference);
              return referenceValue.local ==
                         hirLocalId(static_cast<uint32_t>(leaf.referencedLocal + 1)) &&
                     referenceValue.type == leafType &&
                     referenceValue.category == HirValueCategory::Place &&
                     sameSpan(referenceValue.sourceSpan, ZC_ASSERT_NONNULL(leafSpan));
            }
            zc::Maybe<const HirParameterReferenceExpression&> reference;
            for (const auto& parameterReference : candidate.impl->parameterReferences) {
              if (parameterReference.node != hirId(leafOrdinal)) continue;
              if (reference != zc::none) return false;
              reference = parameterReference;
            }
            auto parameterHandle = resolvedCallableParameter(bound.bindings(), leaf.node);
            if (reference == zc::none || parameterHandle == zc::none) return false;
            bool parameterMatches = false;
            ZC_IF_SOME(handle, parameterHandle) {
              auto authority = registries.callableParameter(handle);
              ZC_IF_SOME(entry, authority) {
                parameterMatches = ZC_ASSERT_NONNULL(reference).parameter == entry.key();
              }
            }
            const auto& referenceValue = ZC_ASSERT_NONNULL(reference);
            return parameterMatches && referenceValue.type == leafType &&
                   referenceValue.category == HirValueCategory::Place &&
                   sameSpan(referenceValue.sourceSpan, ZC_ASSERT_NONNULL(leafSpan));
          };
          // Verify each operand node against its classification. `leafOrdinal` is
          // the first of the two node ids reserved for a nested operand's inner
          // leaves; it is only consumed for a nested binary.
          auto verifyOperand = [&](uint32_t operandOrdinal, uint32_t leafOrdinal,
                                   const SequentialBinaryOperand& operand) -> bool {
            auto operandSpan = bound.parsedModule().spanFor(tree.node(operand.node).range);
            if (operandSpan == zc::none) return false;
            if (operand.kind == SequentialBinaryOperandKind::NestedBinary) {
              // The nested operand node is its own HirPrimitiveBinaryExpression
              // whose inner leaves are at leafOrdinal / leafOrdinal + 1, with its
              // own checked call fact keyed on the operand node.
              if (operand.nestedLeft == zc::none || operand.nestedRight == zc::none ||
                  operand.nestedOperation == zc::none) {
                return false;
              }
              zc::Maybe<const HirPrimitiveBinaryExpression&> nested;
              for (const auto& operation : candidate.impl->primitiveBinaryOperations) {
                if (operation.node != hirId(operandOrdinal)) continue;
                if (nested != zc::none) return false;
                nested = operation;
              }
              auto nestedCallIndex = factIndex(facts.calls(), operand.node);
              if (nested == zc::none || nestedCallIndex == zc::none) return false;
              size_t nestedCallSlot = 0;
              ZC_IF_SOME(value, nestedCallIndex) { nestedCallSlot = value; }
              const auto& nestedFact = facts.calls().entries()[nestedCallSlot].value;
              const auto& nestedCall = nestedFact.invocation;
              const auto& nestedSelected = nestedCall.selected.variant();
              if (!nestedSelected.is<checker::checked::PrimitiveCallable>()) return false;
              const auto nestedOp =
                  nestedSelected.get<checker::checked::PrimitiveCallable>().operation;
              const bool nestedComparison = isScalarComparisonOperation(nestedOp);
              const bool nestedArithmetic = isScalarArithmeticOperation(nestedOp);
              const auto& nestedValue = ZC_ASSERT_NONNULL(nested);
              const auto nestedOperandType = nestedValue.operandType;
              const ast::NodeId nestedLeftNode(
                  tree.node(operand.node).payload.words[ast::kBinaryExprLhsWord]);
              const ast::NodeId nestedRightNode(
                  tree.node(operand.node).payload.words[ast::kBinaryExprRhsWord]);
              if ((!nestedComparison && !nestedArithmetic) ||
                  nestedValue.node != hirId(operandOrdinal) ||
                  nestedValue.left != hirId(leafOrdinal) ||
                  nestedValue.right != hirId(leafOrdinal + 1) ||
                  nestedValue.type != binaryOperandType ||
                  nestedValue.category != HirValueCategory::Value ||
                  nestedValue.operation != nestedOp ||
                  nestedOp != ZC_ASSERT_NONNULL(operand.nestedOperation) ||
                  !sameSpan(nestedValue.sourceSpan, ZC_ASSERT_NONNULL(operandSpan)) ||
                  (nestedArithmetic && binaryOperandType != nestedOperandType) ||
                  nestedFact.node != operand.node || nestedCall.calleeType != nestedOperandType ||
                  nestedCall.receiver != zc::none || nestedCall.receiverMode != zc::none ||
                  nestedCall.receiverAdjustment != zc::none || nestedCall.arguments.size() != 2 ||
                  nestedCall.arguments[0].sourceNode != nestedLeftNode ||
                  nestedCall.arguments[0].sourceType != nestedOperandType ||
                  nestedCall.arguments[1].sourceNode != nestedRightNode ||
                  nestedCall.arguments[1].sourceType != nestedOperandType ||
                  nestedCall.resultType != binaryOperandType ||
                  nestedCall.substitutions != zc::none || nestedCall.witnesses != zc::none ||
                  nestedCall.raises != zc::none) {
                return false;
              }
              bool leavesValid = false;
              ZC_IF_SOME(leftLeaf, operand.nestedLeft) {
                ZC_IF_SOME(rightLeaf, operand.nestedRight) {
                  leavesValid = verifyLeaf(leafOrdinal, leftLeaf, nestedOperandType) &&
                                verifyLeaf(leafOrdinal + 1, rightLeaf, nestedOperandType);
                }
              }
              return leavesValid;
            }
            SequentialBinaryLeafOperand leaf{operand.node, operand.kind, operand.referencedLocal};
            return verifyLeaf(operandOrdinal, leaf, binaryOperandType);
          };
          // The nested operand's inner leaves occupy the two node ids after both
          // operand ids; only one operand may be nested, so the leaf window is
          // shared.
          const uint32_t nestedLeafOrdinal = rightOperandOrdinal + 1;
          bool operandsValid = false;
          ZC_IF_SOME(left, binding.leftOperand) {
            ZC_IF_SOME(right, binding.rightOperand) {
              operandsValid = verifyOperand(leftOperandOrdinal, nestedLeafOrdinal, left) &&
                              verifyOperand(rightOperandOrdinal, nestedLeafOrdinal, right) &&
                              call.arguments[0].sourceNode == left.node &&
                              call.arguments[1].sourceNode == right.node;
            }
          }
          if (!operandsValid) {
            bindingsValid = false;
            break;
          }
        }
      }
      if (!bindingsValid) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      // Verify the returned place: a parameter or one of the declared locals.
      if (source.returnsLocal != zc::none) {
        size_t returnLocal = 0;
        ZC_IF_SOME(value, source.returnsLocal) { returnLocal = value; }
        zc::Maybe<const HirLocalReferenceExpression&> returnReference;
        for (const auto& localReference : candidate.impl->localReferences) {
          if (localReference.node != hirId(returnNodeOrdinal + 1)) continue;
          if (returnReference != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          returnReference = localReference;
        }
        if (returnReference == zc::none ||
            ZC_ASSERT_NONNULL(returnReference).local !=
                hirLocalId(static_cast<uint32_t>(returnLocal + 1)) ||
            ZC_ASSERT_NONNULL(returnReference).type != sequentialType ||
            ZC_ASSERT_NONNULL(returnReference).category != HirValueCategory::Place ||
            !sameSpan(ZC_ASSERT_NONNULL(returnReference).sourceSpan,
                      ZC_ASSERT_NONNULL(returnValueSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      } else {
        zc::Maybe<const HirParameterReferenceExpression&> returnReference;
        for (const auto& parameterReference : candidate.impl->parameterReferences) {
          if (parameterReference.node != hirId(returnNodeOrdinal + 1)) continue;
          if (returnReference != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          returnReference = parameterReference;
        }
        auto parameterHandle = resolvedCallableParameter(bound.bindings(), source.returnValue);
        bool parameterMatches = false;
        if (returnReference != zc::none) {
          ZC_IF_SOME(handle, parameterHandle) {
            auto authority = registries.callableParameter(handle);
            ZC_IF_SOME(entry, authority) {
              parameterMatches = ZC_ASSERT_NONNULL(returnReference).parameter == entry.key();
            }
          }
        }
        if (returnReference == zc::none || parameterHandle == zc::none || !parameterMatches ||
            ZC_ASSERT_NONNULL(returnReference).type != sequentialType ||
            ZC_ASSERT_NONNULL(returnReference).category != HirValueCategory::Place ||
            !sameSpan(ZC_ASSERT_NONNULL(returnReference).sourceSpan,
                      ZC_ASSERT_NONNULL(returnValueSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      // Unsafe-block boundary sits immediately after the return value node.
      FunctionReturnShape returnShape = zc::mv(functionShape).orDefault(FunctionReturnShape{});
      if ((returnShape.unsafeBlock != zc::none) != (function.unsafeBlock != zc::none)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      if (returnShape.unsafeBlock != zc::none) {
        const auto expectedUnsafeBlock = hirId(returnNodeOrdinal + 2);
        if (function.unsafeBlock != expectedUnsafeBlock) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        zc::Maybe<const HirUnsafeBlockExpression&> unsafeBlock;
        for (const auto& candidateBlock : candidate.impl->unsafeBlocks) {
          if (candidateBlock.node != expectedUnsafeBlock) continue;
          if (unsafeBlock != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          unsafeBlock = candidateBlock;
        }
        if (unsafeBlock == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        ZC_IF_SOME(unsafe, unsafeBlock) {
          auto sourceUnsafeSpan = bound.parsedModule().spanFor(
              tree.node(ZC_ASSERT_NONNULL(returnShape.unsafeBlock)).range);
          if (unsafe.body != returnStatement.value || unsafe.type != function.resultType ||
              sourceUnsafeSpan == zc::none ||
              !sameSpan(unsafe.sourceSpan, ZC_ASSERT_NONNULL(sourceUnsafeSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        nextFunction += bindingNodeSpan + 5;
      } else {
        nextFunction += bindingNodeSpan + 4;
      }
      continue;
    }
    // Receiver-field write-read method shape: a mutating-receiver inherent
    // method whose body is `this.<field> = <scalar>;` followed by
    // `return this.<field>;`. Six node ids: function, body, write, write value
    // literal, return, parameter field projection.
    {
      zc::Maybe<const HirParameterFieldWriteStatement&> writeRecord;
      zc::Maybe<const HirParameterFieldProjectionExpression&> writeProjectionRecord;
      for (const auto& write : candidate.impl->parameterFieldWrites) {
        if (write.node != block.statements[0]) continue;
        if (writeRecord != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        writeRecord = write;
      }
      for (const auto& projection : candidate.impl->parameterFieldProjections) {
        if (projection.node != returnStatement.value) continue;
        if (writeProjectionRecord != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        writeProjectionRecord = projection;
      }
      if (function.receiver != zc::none && writeRecord != zc::none &&
          writeProjectionRecord != zc::none && block.statements.size() == 2) {
        const auto& write = ZC_ASSERT_NONNULL(writeRecord);
        const auto& projection = ZC_ASSERT_NONNULL(writeProjectionRecord);
        const auto& receiver = ZC_ASSERT_NONNULL(function.receiver);
        const auto writeNodeId = hirId(expectedFunction + 2);
        const auto writeValueNodeId = hirId(expectedFunction + 3);
        const auto returnNodeId = hirId(expectedFunction + 4);
        const auto valueNodeId = hirId(expectedFunction + 5);
        if (function.node != hirId(expectedFunction) || block.node != hirId(expectedFunction + 1) ||
            block.statements[1] != returnStatement.node || returnStatement.node != returnNodeId ||
            returnStatement.value != valueNodeId || write.node != writeNodeId ||
            write.value != writeValueNodeId || projection.node != valueNodeId ||
            function.body != block.node || block.statements[0] != write.node ||
            returnStatement.resultType != function.resultType || write.parameter != receiver.key ||
            write.type != function.resultType || projection.type != function.resultType ||
            projection.parameter != receiver.key || write.field != projection.field ||
            write.receiverType != projection.receiverType ||
            projection.category != HirValueCategory::Place) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
        if (sourceDefinitionIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t sourceDefinitionSlot = 0;
        ZC_IF_SOME(value, sourceDefinitionIndex) { sourceDefinitionSlot = value; }
        const auto& sourceDefinition = definitions.definitions()[sourceDefinitionSlot];
        const auto& tree = bound.tree();
        if (sourceDefinition.record.kind() != identity::DefinitionKind::Method ||
            !hasExecutableBody(sourceDefinition, definitions) ||
            !definitionBelongsToModule(sourceDefinition, definitions) ||
            !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
            !tree.contains(sourceDefinition.node) ||
            tree.node(sourceDefinition.node).kind != ast::SyntaxKind::MethodDecl) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
        if (sourceShape == zc::none || !ZC_ASSERT_NONNULL(sourceShape).returnsReceiverField ||
            !ZC_ASSERT_NONNULL(sourceShape).writesReceiverField) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& source = ZC_ASSERT_NONNULL(sourceShape);
        const ast::NodeId sourceThisNode(
            tree.node(source.value).payload.words[ast::kMemberExpressionObjectWord]);
        auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
        auto thisSpan = bound.parsedModule().spanFor(tree.node(sourceThisNode).range);
        auto projectionSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
        auto sourceWriteSpan =
            bound.parsedModule().spanFor(tree.node(source.receiverWriteStatement).range);
        auto sourceWriteValueSpan =
            bound.parsedModule().spanFor(tree.node(source.receiverWriteValue).range);
        if (bodySpan == zc::none || returnSpan == zc::none || thisSpan == zc::none ||
            projectionSpan == zc::none || sourceWriteSpan == zc::none ||
            sourceWriteValueSpan == zc::none ||
            !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) ||
            !sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) ||
            !sameSpan(projection.sourceSpan, ZC_ASSERT_NONNULL(projectionSpan)) ||
            !sameSpan(write.sourceSpan, ZC_ASSERT_NONNULL(sourceWriteSpan)) ||
            !sameSpan(write.valueSpan, ZC_ASSERT_NONNULL(sourceWriteValueSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto receiverLookup = semanticTypes.get(receiver.type);
        if (!receiverLookup.is<type::SemanticTypeLookup>() ||
            !receiverLookup.get<type::SemanticTypeLookup>()
                 .data()
                 .is<type::semantic::ReferenceTypeData>() ||
            receiverLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .mutability != type::semantic::Mutability::Mutable) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& receiverReference = receiverLookup.get<type::SemanticTypeLookup>()
                                            .data()
                                            .get<type::semantic::ReferenceTypeData>();
        if (receiverReference.referent != projection.receiverType) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto thisTypeIndex = factIndex(facts.nodeTypes(), sourceThisNode);
        auto memberTypeIndex = factIndex(facts.nodeTypes(), source.value);
        auto memberIndex = factIndex(facts.members(), source.value);
        auto placeIndex = factIndex(facts.places(), source.value);
        auto writeValueTypeIndex = factIndex(facts.nodeTypes(), source.receiverWriteValue);
        auto writeLiteralIndex = factIndex(facts.literals(), source.receiverWriteValue);
        if (thisTypeIndex == zc::none || memberTypeIndex == zc::none || memberIndex == zc::none ||
            placeIndex == zc::none || writeValueTypeIndex == zc::none ||
            writeLiteralIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t thisTypeSlot = 0;
        size_t memberTypeSlot = 0;
        size_t memberSlot = 0;
        size_t placeSlot = 0;
        size_t writeTypeSlot = 0;
        size_t writeLiteralSlot = 0;
        ZC_IF_SOME(slot, thisTypeIndex) { thisTypeSlot = slot; }
        ZC_IF_SOME(slot, memberTypeIndex) { memberTypeSlot = slot; }
        ZC_IF_SOME(slot, memberIndex) { memberSlot = slot; }
        ZC_IF_SOME(slot, placeIndex) { placeSlot = slot; }
        ZC_IF_SOME(slot, writeValueTypeIndex) { writeTypeSlot = slot; }
        ZC_IF_SOME(slot, writeLiteralIndex) { writeLiteralSlot = slot; }
        const auto& memberFact = facts.members().entries()[memberSlot].value;
        const auto& placeFact = facts.places().entries()[placeSlot].value;
        const auto& writeLiteral = facts.literals().entries()[writeLiteralSlot].value;
        if (facts.nodeTypes().entries()[thisTypeSlot].value != receiver.type ||
            facts.nodeTypes().entries()[memberTypeSlot].value != projection.type ||
            facts.nodeTypes().entries()[writeTypeSlot].value != write.type ||
            memberFact.node != source.value || memberFact.receiverType != projection.receiverType ||
            memberFact.member != projection.field || memberFact.memberType != projection.type ||
            memberFact.adjustment != zc::none || placeFact.node != source.value ||
            placeFact.type != projection.type || !placeFact.mutablePlace || !placeFact.movable ||
            !placeFact.root.variant().is<checker::checked::CallableParameterPlaceRoot>() ||
            placeFact.projections.size() != 1 ||
            !placeFact.projections[0].variant().is<checker::checked::FieldProjection>() ||
            placeFact.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                projection.field ||
            writeLiteral.type != write.type) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto rootAuthority = registries.callableParameter(
            placeFact.root.variant().get<checker::checked::CallableParameterPlaceRoot>().parameter);
        if (rootAuthority == zc::none || ZC_ASSERT_NONNULL(rootAuthority).key() != receiver.key) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        if (signaturePosition == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t signatureSlot = 0;
        ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
        const auto& signature = signatures.definitions[signatureSlot];
        if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
            !signature.scope.variant().is<checker::signature::MemberSignatureScope>() ||
            signature.definition != function.definition ||
            signature.definitionKind != identity::DefinitionKind::Method) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& memberScope =
            signature.scope.variant().get<checker::signature::MemberSignatureScope>();
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        const auto expectedVisibility = memberVisibility(memberScope.visibility, module);
        const auto expectedLinkage = linkage(callable);
        if (memberScope.owner == function.definition || callable.raises != zc::none ||
            callable.success != function.resultType || callable.parameters.size() != 0 ||
            function.parameters.size() != 0 || callable.receiver == zc::none ||
            ZC_ASSERT_NONNULL(callable.receiver).mode !=
                checker::signature::ReceiverMode::Mutable ||
            ZC_ASSERT_NONNULL(callable.receiver).parameter != receiver.key ||
            expectedLinkage == zc::none ||
            !sameVisibility(function.visibility, expectedVisibility) ||
            function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
            !sameSpan(function.sourceSpan, sourceDefinition.source) ||
            !sameSpan(signature.declarationSpan, sourceDefinition.source)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const ast::NodeId parameterListNode(
            tree.node(sourceDefinition.node).payload.words[ast::kMethodDeclParamsIdWord]);
        if (!tree.contains(parameterListNode) ||
            tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const ast::NodeList astParameters{
            tree.node(parameterListNode).payload.words[ast::kFunctionParameterListParamsFirstWord],
            tree.node(parameterListNode).payload.words[ast::kFunctionParameterListParamsSizeWord]};
        if (!tree.contains(astParameters) || astParameters.size != 1) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto receiverNode = tree.list(astParameters)[0];
        if (!tree.contains(receiverNode) ||
            tree.node(receiverNode).kind != ast::SyntaxKind::FunctionParameterDecl) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto receiverSpan = bound.parsedModule().spanFor(tree.node(receiverNode).range);
        if (receiverSpan == zc::none ||
            !sameSpan(receiver.sourceSpan, ZC_ASSERT_NONNULL(receiverSpan)) ||
            !typeExists(receiver.type, semanticTypes) ||
            !typeExists(projection.type, semanticTypes)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        nextFunction += 6;
        continue;
      }
    }
    // Receiver-field method shape: a shared- or mutating-receiver inherent
    // method whose body is the single statement `return this.<field>;`. Four
    // node ids: function, body, return, parameter field projection. The branch
    // is fully self-contained: it validates the source shape, the
    // receiver-keyed member and place facts, and the method header before
    // advancing past four nodes.
    {
      zc::Maybe<const HirParameterFieldProjectionExpression&> projectionRecord;
      for (const auto& projection : candidate.impl->parameterFieldProjections) {
        if (projection.node != returnStatement.value) continue;
        if (projectionRecord != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        projectionRecord = projection;
      }
      if (function.receiver != zc::none && projectionRecord != zc::none) {
        const auto& projection = ZC_ASSERT_NONNULL(projectionRecord);
        const auto valueNodeId = hirId(expectedFunction + 3);
        if (function.node != hirId(expectedFunction) || block.node != hirId(expectedFunction + 1) ||
            returnStatement.node != hirId(expectedFunction + 2) ||
            returnStatement.value != valueNodeId || projection.node != valueNodeId ||
            function.body != block.node || block.statements.size() != 1 ||
            block.statements[0] != returnStatement.node ||
            returnStatement.resultType != function.resultType ||
            projection.type != function.resultType ||
            projection.category != HirValueCategory::Place) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& receiver = ZC_ASSERT_NONNULL(function.receiver);
        if (projection.parameter != receiver.key) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
        if (sourceDefinitionIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t sourceDefinitionSlot = 0;
        ZC_IF_SOME(value, sourceDefinitionIndex) { sourceDefinitionSlot = value; }
        const auto& sourceDefinition = definitions.definitions()[sourceDefinitionSlot];
        const auto& tree = bound.tree();
        if (sourceDefinition.record.kind() != identity::DefinitionKind::Method ||
            !hasExecutableBody(sourceDefinition, definitions) ||
            !definitionBelongsToModule(sourceDefinition, definitions) ||
            !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
            !tree.contains(sourceDefinition.node) ||
            tree.node(sourceDefinition.node).kind != ast::SyntaxKind::MethodDecl) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
        if (sourceShape == zc::none || !ZC_ASSERT_NONNULL(sourceShape).returnsReceiverField) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& source = ZC_ASSERT_NONNULL(sourceShape);
        if (!tree.contains(source.value) ||
            tree.node(source.value).kind != ast::SyntaxKind::MemberExpression ||
            static_cast<ast::MemberAccessKind>(
                tree.node(source.value).payload.words[ast::kMemberExpressionAccessWord]) !=
                ast::MemberAccessKind::Dot) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const ast::NodeId thisNode(
            tree.node(source.value).payload.words[ast::kMemberExpressionObjectWord]);
        if (!tree.contains(thisNode) || tree.node(thisNode).kind != ast::SyntaxKind::ThisExpr) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
        auto thisSpan = bound.parsedModule().spanFor(tree.node(thisNode).range);
        auto projectionSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
        if (bodySpan == zc::none || returnSpan == zc::none || thisSpan == zc::none ||
            projectionSpan == zc::none ||
            !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) ||
            !sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) ||
            !sameSpan(projection.sourceSpan, ZC_ASSERT_NONNULL(projectionSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // The receiver parameter carries a reference to the owner, either shared
        // or mutable; its referent must equal the projection's receiver type.
        auto receiverLookup = semanticTypes.get(receiver.type);
        if (!receiverLookup.is<type::SemanticTypeLookup>() ||
            !receiverLookup.get<type::SemanticTypeLookup>()
                 .data()
                 .is<type::semantic::ReferenceTypeData>()) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& receiverReference = receiverLookup.get<type::SemanticTypeLookup>()
                                            .data()
                                            .get<type::semantic::ReferenceTypeData>();
        if (receiverReference.referent != projection.receiverType) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // A field READ copies the projected value out and is sound through both
        // shared and mutable receivers; no mutability gate applies here. The
        // write-read branch retains its mutable-receiver requirement separately.
        // Checked facts: the bare `this` carries the receiver reference type,
        // the member node carries the field type, and the member/place facts
        // root at the receiver parameter with one field projection.
        auto thisTypeIndex = factIndex(facts.nodeTypes(), thisNode);
        auto memberTypeIndex = factIndex(facts.nodeTypes(), source.value);
        auto memberIndex = factIndex(facts.members(), source.value);
        auto placeIndex = factIndex(facts.places(), source.value);
        if (thisTypeIndex == zc::none || memberTypeIndex == zc::none || memberIndex == zc::none ||
            placeIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t thisTypeSlot = 0;
        size_t memberTypeSlot = 0;
        size_t memberSlot = 0;
        size_t placeSlot = 0;
        ZC_IF_SOME(value, thisTypeIndex) { thisTypeSlot = value; }
        ZC_IF_SOME(value, memberTypeIndex) { memberTypeSlot = value; }
        ZC_IF_SOME(value, memberIndex) { memberSlot = value; }
        ZC_IF_SOME(value, placeIndex) { placeSlot = value; }
        const auto& memberFact = facts.members().entries()[memberSlot].value;
        const auto& placeFact = facts.places().entries()[placeSlot].value;
        if (facts.nodeTypes().entries()[thisTypeSlot].value != receiver.type ||
            facts.nodeTypes().entries()[memberTypeSlot].value != projection.type ||
            memberFact.node != source.value || memberFact.receiverType != projection.receiverType ||
            memberFact.member != projection.field || memberFact.memberType != projection.type ||
            memberFact.adjustment != zc::none || placeFact.node != source.value ||
            placeFact.type != projection.type || !placeFact.movable ||
            !placeFact.root.variant().is<checker::checked::CallableParameterPlaceRoot>() ||
            placeFact.projections.size() != 1 ||
            !placeFact.projections[0].variant().is<checker::checked::FieldProjection>() ||
            placeFact.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                projection.field) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto rootAuthority = registries.callableParameter(
            placeFact.root.variant().get<checker::checked::CallableParameterPlaceRoot>().parameter);
        if (rootAuthority == zc::none || ZC_ASSERT_NONNULL(rootAuthority).key() != receiver.key) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Method header: member-signature scope, a shared-or-mutating receiver
        // matching the header, zero ordinary parameters, and matching
        // linkage/visibility. A field read is sound through either receiver mode.
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        if (signaturePosition == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t signatureSlot = 0;
        ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
        const auto& signature = signatures.definitions[signatureSlot];
        if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
            !signature.scope.variant().is<checker::signature::MemberSignatureScope>() ||
            signature.definition != function.definition ||
            signature.definitionKind != identity::DefinitionKind::Method) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& memberScope =
            signature.scope.variant().get<checker::signature::MemberSignatureScope>();
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        const auto expectedVisibility = memberVisibility(memberScope.visibility, module);
        const auto expectedLinkage = linkage(callable);
        if (memberScope.owner == function.definition || callable.raises != zc::none ||
            callable.success != function.resultType || callable.parameters.size() != 0 ||
            function.parameters.size() != 0 || callable.receiver == zc::none ||
            (ZC_ASSERT_NONNULL(callable.receiver).mode !=
                 checker::signature::ReceiverMode::Shared &&
             ZC_ASSERT_NONNULL(callable.receiver).mode !=
                 checker::signature::ReceiverMode::Mutable) ||
            ZC_ASSERT_NONNULL(callable.receiver).parameter != receiver.key ||
            expectedLinkage == zc::none ||
            !sameVisibility(function.visibility, expectedVisibility) ||
            function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
            !sameSpan(function.sourceSpan, sourceDefinition.source) ||
            !sameSpan(signature.declarationSpan, sourceDefinition.source)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const ast::NodeId parameterListNode(
            tree.node(sourceDefinition.node).payload.words[ast::kMethodDeclParamsIdWord]);
        if (!tree.contains(parameterListNode) ||
            tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const ast::NodeList astParameters{
            tree.node(parameterListNode).payload.words[ast::kFunctionParameterListParamsFirstWord],
            tree.node(parameterListNode).payload.words[ast::kFunctionParameterListParamsSizeWord]};
        if (!tree.contains(astParameters) || astParameters.size != 1) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto receiverNode = tree.list(astParameters)[0];
        if (!tree.contains(receiverNode) ||
            tree.node(receiverNode).kind != ast::SyntaxKind::FunctionParameterDecl) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto receiverSpan = bound.parsedModule().spanFor(tree.node(receiverNode).range);
        if (receiverSpan == zc::none ||
            !sameSpan(receiver.sourceSpan, ZC_ASSERT_NONNULL(receiverSpan)) ||
            !typeExists(receiver.type, semanticTypes) ||
            !typeExists(projection.type, semanticTypes)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        nextFunction += 4;
        continue;
      }
    }
    // By-value struct parameter field-return shape: a free function whose body
    // is the single statement `return <ordinary-parameter>.<field>;`. Four node
    // ids: function, body, return, parameter field projection. The projection
    // roots at the ordinary by-value parameter (a nominal place, no
    // dereference), unlike the receiver shape.
    {
      zc::Maybe<const HirParameterFieldProjectionExpression&> projectionRecord;
      for (const auto& projection : candidate.impl->parameterFieldProjections) {
        if (projection.node != returnStatement.value) continue;
        if (projectionRecord != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        projectionRecord = projection;
      }
      if (function.receiver == zc::none && projectionRecord != zc::none) {
        const auto& projection = ZC_ASSERT_NONNULL(projectionRecord);
        const auto valueNodeId = hirId(expectedFunction + 3);
        if (function.node != hirId(expectedFunction) || block.node != hirId(expectedFunction + 1) ||
            returnStatement.node != hirId(expectedFunction + 2) ||
            returnStatement.value != valueNodeId || projection.node != valueNodeId ||
            function.body != block.node || block.statements.size() != 1 ||
            block.statements[0] != returnStatement.node ||
            returnStatement.resultType != function.resultType ||
            projection.type != function.resultType ||
            projection.category != HirValueCategory::Place || function.parameters.size() != 1 ||
            function.parameters[0].type != projection.receiverType ||
            function.parameters[0].key != projection.parameter) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
        if (sourceDefinitionIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t sourceDefinitionSlot = 0;
        ZC_IF_SOME(value, sourceDefinitionIndex) { sourceDefinitionSlot = value; }
        const auto& sourceDefinition = definitions.definitions()[sourceDefinitionSlot];
        const auto& sourceTree = bound.tree();
        if (sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
            !hasExecutableBody(sourceDefinition, definitions) ||
            !definitionBelongsToModule(sourceDefinition, definitions) ||
            !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
            !sourceTree.contains(sourceDefinition.node) ||
            sourceTree.node(sourceDefinition.node).kind != ast::SyntaxKind::FunctionDecl) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto sourceShape = functionReturnShape(sourceTree, sourceTree.node(sourceDefinition.node));
        if (sourceShape == zc::none || !ZC_ASSERT_NONNULL(sourceShape).returnsParameterField) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& source = ZC_ASSERT_NONNULL(sourceShape);
        if (!sourceTree.contains(source.value) ||
            sourceTree.node(source.value).kind != ast::SyntaxKind::MemberExpression ||
            static_cast<ast::MemberAccessKind>(
                sourceTree.node(source.value).payload.words[ast::kMemberExpressionAccessWord]) !=
                ast::MemberAccessKind::Dot) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const ast::NodeId objectNode(
            sourceTree.node(source.value).payload.words[ast::kMemberExpressionObjectWord]);
        if (!sourceTree.contains(objectNode) ||
            sourceTree.node(objectNode).kind != ast::SyntaxKind::IdentExpr) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto bodySpan = bound.parsedModule().spanFor(sourceTree.node(source.body).range);
        auto returnSpan =
            bound.parsedModule().spanFor(sourceTree.node(source.returnStatement).range);
        auto projectionSpan = bound.parsedModule().spanFor(sourceTree.node(source.value).range);
        if (bodySpan == zc::none || returnSpan == zc::none || projectionSpan == zc::none ||
            !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) ||
            !sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) ||
            !sameSpan(projection.sourceSpan, ZC_ASSERT_NONNULL(projectionSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto objectTypeIndex = factIndex(facts.nodeTypes(), objectNode);
        auto memberTypeIndex = factIndex(facts.nodeTypes(), source.value);
        auto memberIndex = factIndex(facts.members(), source.value);
        auto placeIndex = factIndex(facts.places(), source.value);
        if (objectTypeIndex == zc::none || memberTypeIndex == zc::none || memberIndex == zc::none ||
            placeIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t objectTypeSlot = 0;
        size_t memberTypeSlot = 0;
        size_t memberSlot = 0;
        size_t placeSlot = 0;
        ZC_IF_SOME(slot, objectTypeIndex) { objectTypeSlot = slot; }
        ZC_IF_SOME(slot, memberTypeIndex) { memberTypeSlot = slot; }
        ZC_IF_SOME(slot, memberIndex) { memberSlot = slot; }
        ZC_IF_SOME(slot, placeIndex) { placeSlot = slot; }
        const auto& memberFact = facts.members().entries()[memberSlot].value;
        const auto& placeFact = facts.places().entries()[placeSlot].value;
        if (facts.nodeTypes().entries()[objectTypeSlot].value != projection.receiverType ||
            facts.nodeTypes().entries()[memberTypeSlot].value != projection.type ||
            memberFact.node != source.value || memberFact.receiverType != projection.receiverType ||
            memberFact.member != projection.field || memberFact.memberType != projection.type ||
            memberFact.adjustment != zc::none || placeFact.node != source.value ||
            placeFact.type != projection.type || placeFact.mutablePlace || !placeFact.movable ||
            !placeFact.root.variant().is<checker::checked::CallableParameterPlaceRoot>() ||
            placeFact.projections.size() != 1 ||
            !placeFact.projections[0].variant().is<checker::checked::FieldProjection>() ||
            placeFact.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                projection.field) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto rootAuthority = registries.callableParameter(
            placeFact.root.variant().get<checker::checked::CallableParameterPlaceRoot>().parameter);
        if (rootAuthority == zc::none ||
            ZC_ASSERT_NONNULL(rootAuthority).key() != projection.parameter) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
        if (signaturePosition == zc::none || rootPosition == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t signatureSlot = 0;
        size_t rootSlot = 0;
        ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
        ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
        const auto& signature = signatures.definitions[signatureSlot];
        const auto& root = signatures.roots[rootSlot];
        if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
            !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>() ||
            signature.definition != function.definition ||
            signature.definitionKind != identity::DefinitionKind::Function ||
            root.canonicalDefinition != function.definition || root.sourceModule != module) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        const auto expectedVisibility = visibility(root.visibility);
        const auto expectedLinkage = linkage(callable);
        if (expectedVisibility == zc::none || expectedLinkage == zc::none ||
            callable.receiver != zc::none || callable.raises != zc::none ||
            callable.success != function.resultType || callable.parameters.size() != 1 ||
            callable.parameters[0].parameter != projection.parameter ||
            callable.parameters[0].type != projection.receiverType ||
            callable.parameters[0].hasDefault ||
            !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
            function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
            !sameSpan(function.sourceSpan, sourceDefinition.source) ||
            !sameSpan(signature.declarationSpan, sourceDefinition.source)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        if (!typeExists(projection.receiverType, semanticTypes) ||
            !typeExists(projection.type, semanticTypes)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        nextFunction += 4;
        continue;
      }
    }
    // Receiver self-call method shape: a shared-receiver inherent method whose
    // body is the single statement `return this.<method>();`. Four node ids:
    // function, body, return, self call. The implicit receiver is a header
    // parameter, so the self-call record's receiver slot is unset and MIR
    // forwards the receiver parameter local directly.
    {
      zc::Maybe<const HirReceiverCallExpression&> selfCallRecord;
      for (const auto& call : candidate.impl->receiverCalls) {
        if (call.node != returnStatement.value) continue;
        if (selfCallRecord != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        selfCallRecord = call;
      }
      if (function.receiver != zc::none && selfCallRecord != zc::none &&
          ZC_ASSERT_NONNULL(selfCallRecord).receiver == HirNodeId()) {
        const auto& selfCall = ZC_ASSERT_NONNULL(selfCallRecord);
        const auto callNodeId = hirId(expectedFunction + 3);
        if (function.node != hirId(expectedFunction) || block.node != hirId(expectedFunction + 1) ||
            returnStatement.node != hirId(expectedFunction + 2) ||
            returnStatement.value != callNodeId || selfCall.node != callNodeId ||
            function.body != block.node || block.statements.size() != 1 ||
            block.statements[0] != returnStatement.node ||
            returnStatement.resultType != function.resultType ||
            selfCall.resultType != function.resultType ||
            selfCall.receiverMode != checker::checked::ReceiverMode::Shared ||
            selfCall.receiverAdjustments.size() != 1 ||
            selfCall.receiverAdjustments[0] !=
                checker::checked::ReceiverAdjustmentStep::ReborrowShared ||
            selfCall.arguments.size() != 0 ||
            selfCall.receiverSourceType != selfCall.receiverType) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& receiver = ZC_ASSERT_NONNULL(function.receiver);
        auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
        if (sourceDefinitionIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t sourceDefinitionSlot = 0;
        ZC_IF_SOME(value, sourceDefinitionIndex) { sourceDefinitionSlot = value; }
        const auto& sourceDefinition = definitions.definitions()[sourceDefinitionSlot];
        const auto& tree = bound.tree();
        if (sourceDefinition.record.kind() != identity::DefinitionKind::Method ||
            !hasExecutableBody(sourceDefinition, definitions) ||
            !definitionBelongsToModule(sourceDefinition, definitions) ||
            !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
            !tree.contains(sourceDefinition.node) ||
            tree.node(sourceDefinition.node).kind != ast::SyntaxKind::MethodDecl) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
        if (sourceShape == zc::none || !ZC_ASSERT_NONNULL(sourceShape).returnsReceiverSelfCall) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& source = ZC_ASSERT_NONNULL(sourceShape);
        if (!tree.contains(source.value) ||
            tree.node(source.value).kind != ast::SyntaxKind::CallExpression) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& sourceCall = tree.node(source.value);
        const ast::NodeId calleeNode(sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
        const ast::NodeList typeArguments{
            sourceCall.payload.words[ast::kCallExpressionTypeArgsFirstWord],
            sourceCall.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
        const ast::NodeList arguments{sourceCall.payload.words[ast::kCallExpressionArgsFirstWord],
                                      sourceCall.payload.words[ast::kCallExpressionArgsSizeWord]};
        if (!tree.contains(calleeNode) ||
            tree.node(calleeNode).kind != ast::SyntaxKind::MemberExpression ||
            static_cast<ast::MemberAccessKind>(
                tree.node(calleeNode).payload.words[ast::kMemberExpressionAccessWord]) !=
                ast::MemberAccessKind::Dot ||
            !tree.contains(typeArguments) || !typeArguments.empty() || !tree.contains(arguments) ||
            !arguments.empty()) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const ast::NodeId thisNode(
            tree.node(calleeNode).payload.words[ast::kMemberExpressionObjectWord]);
        if (!tree.contains(thisNode) || tree.node(thisNode).kind != ast::SyntaxKind::ThisExpr) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
        auto callSpan = bound.parsedModule().spanFor(sourceCall.range);
        if (bodySpan == zc::none || returnSpan == zc::none || callSpan == zc::none ||
            !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) ||
            !sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) ||
            !sameSpan(selfCall.sourceSpan, ZC_ASSERT_NONNULL(callSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // The header receiver parameter is the shared reference `&Owner`; the
        // self call forwards it unchanged, so both source and destination are
        // that reference type.
        auto receiverLookup = semanticTypes.get(receiver.type);
        if (!receiverLookup.is<type::SemanticTypeLookup>() ||
            !receiverLookup.get<type::SemanticTypeLookup>()
                 .data()
                 .is<type::semantic::ReferenceTypeData>() ||
            receiverLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .mutability != type::semantic::Mutability::Const) {
          return rejectHirCapability<VerifiedHirModule>(
              function.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
              sourceDefinition.source.clone());
        }
        auto thisTypeIndex = factIndex(facts.nodeTypes(), thisNode);
        auto calleeTypeIndex = factIndex(facts.nodeTypes(), calleeNode);
        auto memberIndex = factIndex(facts.members(), calleeNode);
        auto checkedCallIndex = factIndex(facts.calls(), source.value);
        auto callKey = checkedNodeKey(tree, bound.parsedModule(), source.value);
        if (thisTypeIndex == zc::none || calleeTypeIndex == zc::none || memberIndex == zc::none ||
            checkedCallIndex == zc::none || callKey == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        auto dispatchIndex = dispatchFactIndex(
            candidate.impl->checkedModule.dispatchFacts().facts(), ZC_ASSERT_NONNULL(callKey));
        if (dispatchIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t thisTypeSlot = 0;
        size_t calleeTypeSlot = 0;
        size_t memberSlot = 0;
        size_t checkedCallSlot = 0;
        size_t dispatchSlot = 0;
        ZC_IF_SOME(value, thisTypeIndex) { thisTypeSlot = value; }
        ZC_IF_SOME(value, calleeTypeIndex) { calleeTypeSlot = value; }
        ZC_IF_SOME(value, memberIndex) { memberSlot = value; }
        ZC_IF_SOME(value, checkedCallIndex) { checkedCallSlot = value; }
        ZC_IF_SOME(value, dispatchIndex) { dispatchSlot = value; }
        const auto& memberFact = facts.members().entries()[memberSlot].value;
        const auto& invocation = facts.calls().entries()[checkedCallSlot].value.invocation;
        const auto& selected = invocation.selected.variant();
        const auto& dispatch = candidate.impl->checkedModule.dispatchFacts().facts()[dispatchSlot];
        const auto& target = dispatch.fact.target.variant();
        const auto& transform = dispatch.fact.resultTransform.variant();
        bool dispatchOwnerMatches = false;
        ZC_IF_SOME(owner, dispatch.owner) { dispatchOwnerMatches = owner == function.definition; }
        // The member fact records the receiver *source* type, which is the
        // shared reference `&Owner` for a self-call (the checker types `this`
        // with the reference, not the owner nominal).
        if (facts.nodeTypes().entries()[thisTypeSlot].value != receiver.type ||
            memberFact.node != calleeNode || memberFact.receiverType != receiver.type ||
            memberFact.member != selfCall.callee ||
            memberFact.memberType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
            memberFact.adjustment != zc::none || selfCall.calleeType != memberFact.memberType ||
            !selected.is<checker::checked::ConcreteMethodCallable>() ||
            selected.get<checker::checked::ConcreteMethodCallable>().method != selfCall.callee ||
            !target.is<checker::dispatch::ConcreteMethodTarget>() ||
            target.get<checker::dispatch::ConcreteMethodTarget>().method != selfCall.callee ||
            !transform.is<checker::dispatch::IdentityResultTransform>() ||
            invocation.calleeType != selfCall.calleeType ||
            invocation.successType != function.resultType ||
            invocation.resultType != function.resultType || invocation.receiver == zc::none ||
            invocation.receiverMode == zc::none || invocation.receiverAdjustment == zc::none ||
            invocation.arguments.size() != 0 || invocation.substitutions != zc::none ||
            invocation.witnesses != zc::none || invocation.raises != zc::none ||
            !dispatchOwnerMatches || dispatch.fact.receiver == zc::none ||
            dispatch.fact.arguments.size() != 0 ||
            dispatch.fact.successType != function.resultType ||
            dispatch.fact.resultType != function.resultType ||
            dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
            dispatch.fact.raises != zc::none ||
            !sameSpan(facts.calls().entries()[checkedCallSlot].value.sourceSpan,
                      ZC_ASSERT_NONNULL(callSpan)) ||
            !sameSpan(dispatch.fact.sourceSpan, ZC_ASSERT_NONNULL(callSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        ZC_IF_SOME(callReceiver, invocation.receiver) {
          ZC_IF_SOME(mode, invocation.receiverMode) {
            ZC_IF_SOME(adjustment, invocation.receiverAdjustment) {
              if (callReceiver.sourceNode != thisNode || callReceiver.sourceType != receiver.type ||
                  callReceiver.parameterType != receiver.type ||
                  callReceiver.adjustment != zc::none ||
                  mode != checker::checked::ReceiverMode::Shared ||
                  adjustment.source != receiver.type || adjustment.destination != receiver.type ||
                  adjustment.steps.size() != 1 ||
                  adjustment.steps[0] != checker::checked::ReceiverAdjustmentStep::ReborrowShared) {
                return rejectHirCapability<VerifiedHirModule>(
                    function.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                    sourceDefinition.source.clone());
              }
            }
          }
        }
        // Method header: member-signature scope, shared receiver matching the
        // header key, zero ordinary parameters, and matching linkage.
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        if (signaturePosition == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t signatureSlot = 0;
        ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
        const auto& signature = signatures.definitions[signatureSlot];
        if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
            !signature.scope.variant().is<checker::signature::MemberSignatureScope>() ||
            signature.definition != function.definition ||
            signature.definitionKind != identity::DefinitionKind::Method) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& memberScope =
            signature.scope.variant().get<checker::signature::MemberSignatureScope>();
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        const auto expectedVisibility = memberVisibility(memberScope.visibility, module);
        const auto expectedLinkage = linkage(callable);
        if (memberScope.owner == function.definition || callable.raises != zc::none ||
            callable.success != function.resultType || callable.parameters.size() != 0 ||
            function.parameters.size() != 0 || callable.receiver == zc::none ||
            ZC_ASSERT_NONNULL(callable.receiver).mode != checker::signature::ReceiverMode::Shared ||
            ZC_ASSERT_NONNULL(callable.receiver).parameter != receiver.key ||
            expectedLinkage == zc::none ||
            !sameVisibility(function.visibility, expectedVisibility) ||
            function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
            !sameSpan(function.sourceSpan, sourceDefinition.source) ||
            !sameSpan(signature.declarationSpan, sourceDefinition.source)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const ast::NodeId parameterListNode(
            tree.node(sourceDefinition.node).payload.words[ast::kMethodDeclParamsIdWord]);
        if (!tree.contains(parameterListNode) ||
            tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const ast::NodeList astParameters{
            tree.node(parameterListNode).payload.words[ast::kFunctionParameterListParamsFirstWord],
            tree.node(parameterListNode).payload.words[ast::kFunctionParameterListParamsSizeWord]};
        if (!tree.contains(astParameters) || astParameters.size != 1) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto receiverNode = tree.list(astParameters)[0];
        if (!tree.contains(receiverNode) ||
            tree.node(receiverNode).kind != ast::SyntaxKind::FunctionParameterDecl) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto receiverSpan = bound.parsedModule().spanFor(tree.node(receiverNode).range);
        if (receiverSpan == zc::none ||
            !sameSpan(receiver.sourceSpan, ZC_ASSERT_NONNULL(receiverSpan)) ||
            !typeExists(receiver.type, semanticTypes)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        nextFunction += 4;
        continue;
      }
    }
    // Conditional shape: if/else with scalar returns in both branches.
    {
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          !definitionBelongsToModule(sourceDefinition, definitions) ||
          (sourceDefinition.record.kind() != identity::DefinitionKind::Function &&
           sourceDefinition.record.kind() != identity::DefinitionKind::Method) ||
          !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
          !tree.contains(sourceDefinition.node)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (sourceShape == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      FunctionReturnShape source = zc::mv(sourceShape).orDefault(FunctionReturnShape{});
      if (source.isConditional) {
        if (source.isLeadingLocalConditional) {
          // K leading scalar-local bindings followed by one comparison
          // conditional with two literal arms. Fixed-id layout relative to the
          // function id: function, body, per binding (local, initializer), then
          // left operand, right operand, comparison, then arm, else arm,
          // conditional, return: 9 + 2K nodes.
          const bool isArithmetic =
              source.conditionLeftIsNestedArithmetic || source.conditionRightIsNestedArithmetic;
          LeadingLocalConditionalShape leading{};
          size_t bindingCount = 0;
          if (isArithmetic) {
            // The HIR builder synthesizes one arithmetic binding for a nested-
            // arithmetic comparison operand. Collect any real leading bindings
            // first, then append the synthesized arithmetic binding so the
            // fixed-id layout accounts for both.
            auto leadingMaybe = leadingLocalConditionalShape(tree, source.body);
            if (leadingMaybe != zc::none) {
              ZC_IF_SOME(value, leadingMaybe) { leading = zc::mv(value); }
            }
            const ast::NodeId arithmeticNode = source.conditionLeftIsNestedArithmetic
                                                   ? source.conditionLeft
                                                   : source.conditionRight;
            SequentialLocalBinding synth{};
            synth.declarator = arithmeticNode;
            synth.pattern = arithmeticNode;
            synth.initializer = arithmeticNode;
            synth.initializerKind = SequentialInitializerKind::PrimitiveBinary;
            leading.bindings.add(zc::mv(synth));
            leading.body = source.body;
            leading.ifStatement = source.returnStatement;
            bindingCount = leading.bindings.size();
          } else {
            auto leadingMaybe = leadingLocalConditionalShape(tree, source.body);
            if (leadingMaybe == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
            ZC_IF_SOME(value, leadingMaybe) { leading = zc::mv(value); }
            bindingCount = leading.bindings.size();
          }
          // Shared signature/header validation (free-function variant).
          auto signaturePosition =
              signatureIndex(signatures.definitions.asPtr(), function.definition);
          auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
          if (signaturePosition == zc::none || rootPosition == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          size_t signatureSlot = 0;
          size_t rootSlot = 0;
          ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
          ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
          const auto& signature = signatures.definitions[signatureSlot];
          const auto& root = signatures.roots[rootSlot];
          if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
              !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const auto& callable =
              signature.payload.variant().get<checker::signature::CallableSignature>();
          auto expectedVisibility = visibility(root.visibility);
          auto expectedLinkage = linkage(callable);
          if (expectedVisibility == zc::none || expectedLinkage == zc::none ||
              signature.definition != function.definition ||
              signature.definitionKind != identity::DefinitionKind::Function ||
              signatures.roots[rootSlot].sourceModule != module || callable.receiver != zc::none ||
              function.receiver != zc::none || callable.raises != zc::none ||
              callable.success != function.resultType ||
              !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
              !sameSpan(function.sourceSpan, sourceDefinition.source) ||
              !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
              function.parameters.size() != callable.parameters.size()) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          if (function.linkage != ZC_ASSERT_NONNULL(expectedLinkage)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          for (size_t parameterIndex = 0; parameterIndex < function.parameters.size();
               ++parameterIndex) {
            const auto& parameter = function.parameters[parameterIndex];
            const auto& sourceParameter = callable.parameters[parameterIndex];
            if (parameter.key != sourceParameter.parameter ||
                parameter.type != sourceParameter.type || sourceParameter.hasDefault ||
                !typeExists(parameter.type, semanticTypes)) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
          }
          // Spans and checked facts.
          auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
          auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
          auto conditionSpan = bound.parsedModule().spanFor(tree.node(source.condition).range);
          auto thenSpan = bound.parsedModule().spanFor(tree.node(source.thenReturnValue).range);
          auto elseSpan = bound.parsedModule().spanFor(tree.node(source.elseReturnValue).range);
          if (bodySpan == zc::none || returnSpan == zc::none || conditionSpan == zc::none ||
              thenSpan == zc::none || elseSpan == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          auto conditionTypeIndex = factIndex(facts.nodeTypes(), source.condition);
          auto thenTypeIndex = factIndex(facts.nodeTypes(), source.thenReturnValue);
          auto elseTypeIndex = factIndex(facts.nodeTypes(), source.elseReturnValue);
          auto thenLiteralIndex = factIndex(facts.literals(), source.thenReturnValue);
          auto elseLiteralIndex = factIndex(facts.literals(), source.elseReturnValue);
          if (conditionTypeIndex == zc::none || thenTypeIndex == zc::none ||
              elseTypeIndex == zc::none || thenLiteralIndex == zc::none ||
              elseLiteralIndex == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          size_t conditionTypeSlot = 0;
          size_t thenTypeSlot = 0;
          size_t elseTypeSlot = 0;
          ZC_IF_SOME(value, conditionTypeIndex) { conditionTypeSlot = value; }
          ZC_IF_SOME(value, thenTypeIndex) { thenTypeSlot = value; }
          ZC_IF_SOME(value, elseTypeIndex) { elseTypeSlot = value; }
          const auto conditionType = facts.nodeTypes().entries()[conditionTypeSlot].value;
          if (facts.nodeTypes().entries()[thenTypeSlot].value != function.resultType ||
              facts.nodeTypes().entries()[elseTypeSlot].value != function.resultType) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          // Fixed-id layout: bindings first, then the seven tail nodes. Each
          // arithmetic binding allocates two extra leaf-operand nodes.
          uint32_t arithmeticBindingCount = 0;
          for (size_t i = 0; i < bindingCount; ++i) {
            if (leading.bindings[i].initializerKind == SequentialInitializerKind::PrimitiveBinary) {
              ++arithmeticBindingCount;
            }
          }
          const uint32_t tailBase =
              2u + static_cast<uint32_t>(bindingCount) * 2u + arithmeticBindingCount * 2u;
          const HirNodeId leftId = hirId(expectedFunction + tailBase);
          const HirNodeId rightId = hirId(expectedFunction + tailBase + 1);
          const HirNodeId equalityId = hirId(expectedFunction + tailBase + 2);
          const HirNodeId thenId = hirId(expectedFunction + tailBase + 3);
          const HirNodeId elseId = hirId(expectedFunction + tailBase + 4);
          const HirNodeId conditionalId = hirId(expectedFunction + tailBase + 5);
          const HirNodeId returnId = hirId(expectedFunction + tailBase + 6);
          bool structureOk = function.node == hirId(expectedFunction) &&
                             block.node == hirId(expectedFunction + 1) &&
                             function.body == block.node &&
                             block.statements.size() == bindingCount + 1;
          if (structureOk) {
            for (size_t bindingIndex = 0; bindingIndex < bindingCount; ++bindingIndex) {
              if (block.statements[bindingIndex] !=
                  hirId(expectedFunction + 2u + static_cast<uint32_t>(bindingIndex) * 2u)) {
                structureOk = false;
              }
            }
            if (block.statements[bindingCount] != returnId) structureOk = false;
          }
          structureOk = structureOk && sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan));
          if (!structureOk) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          // Verify the bindings against their owner locals and checked facts.
          zc::Vector<binder::OwnerLocalBindingId> localBindingIds;
          for (size_t bindingIndex = 0; bindingIndex < bindingCount; ++bindingIndex) {
            const auto& binding = leading.bindings[bindingIndex];
            const HirNodeId localNodeId =
                hirId(expectedFunction + 2u + static_cast<uint32_t>(bindingIndex) * 2u);
            const HirNodeId initializerNodeId =
                hirId(expectedFunction + 3u + static_cast<uint32_t>(bindingIndex) * 2u);
            zc::Maybe<const HirLocalBinding&> localRecord;
            for (const auto& local : candidate.impl->locals) {
              if (local.node != localNodeId) continue;
              if (localRecord != zc::none) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::AdditionalFact, module,
                                                    registries, index + 1);
              }
              localRecord = local;
            }
            if (localRecord == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
            auto patternSpan = bound.parsedModule().spanFor(tree.node(binding.pattern).range);
            auto initializerSpan =
                bound.parsedModule().spanFor(tree.node(binding.initializer).range);
            // A synthesized arithmetic binding has no source VariableDeclarator,
            // so there is no owner-local binding to match against.
            const bool isArithmeticBinding =
                binding.initializerKind == SequentialInitializerKind::PrimitiveBinary;
            zc::Maybe<binder::OwnerLocalBindingId> ownerBinding;
            if (!isArithmeticBinding) {
              ownerBinding = ownerLocalBindingForPattern(definitions, binding.pattern, tree);
            }
            if (patternSpan == zc::none || initializerSpan == zc::none ||
                (!isArithmeticBinding && ownerBinding == zc::none)) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
            if (!isArithmeticBinding) {
              for (const auto existing : localBindingIds) {
                if (existing == ZC_ASSERT_NONNULL(ownerBinding)) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::AdditionalFact, module,
                                                      registries, index + 1);
                }
              }
            }
            auto initializerTypeIndex = factIndex(facts.nodeTypes(), binding.initializer);
            if (initializerTypeIndex == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
            size_t initializerTypeSlot = 0;
            ZC_IF_SOME(value, initializerTypeIndex) { initializerTypeSlot = value; }
            const auto bindingType = facts.nodeTypes().entries()[initializerTypeSlot].value;
            const auto& localValue = ZC_ASSERT_NONNULL(localRecord);
            if (!typeExists(bindingType, semanticTypes) ||
                localValue.local != hirLocalId(static_cast<uint32_t>(bindingIndex + 1)) ||
                localValue.initializer != initializerNodeId || localValue.type != bindingType ||
                !sameSpan(localValue.sourceSpan, ZC_ASSERT_NONNULL(patternSpan)) ||
                localValue.initializerSpan == zc::none ||
                !sameSpan(ZC_ASSERT_NONNULL(localValue.initializerSpan),
                          ZC_ASSERT_NONNULL(initializerSpan)) ||
                (!isArithmeticBinding &&
                 !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(ownerBinding), binding.pattern,
                                    tree))) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            bool initializerRecordOk = false;
            if (binding.initializerKind == SequentialInitializerKind::PrimitiveBinary) {
              // A synthesized arithmetic binding (`let t = a + 1`). The pooled
              // primitive binary operation record must match the initializer
              // node, result type, and source span.
              for (const auto& binary : candidate.impl->primitiveBinaryOperations) {
                if (binary.node != initializerNodeId) continue;
                if (initializerRecordOk) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::AdditionalFact, module,
                                                      registries, index + 1);
                }
                initializerRecordOk =
                    binary.type == bindingType && binary.category == HirValueCategory::Value &&
                    sameSpan(binary.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan));
              }
            } else if (binding.initializerKind == SequentialInitializerKind::Literal ||
                       binding.initializerKind == SequentialInitializerKind::EnumVariant) {
              for (const auto& expression : candidate.impl->expressions) {
                if (expression.node != initializerNodeId) continue;
                if (initializerRecordOk) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::AdditionalFact, module,
                                                      registries, index + 1);
                }
                auto literalIndex = factIndex(facts.literals(), binding.initializer);
                if (literalIndex != zc::none) {
                  size_t literalSlot = 0;
                  ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
                  const auto& literalFact = facts.literals().entries()[literalSlot].value;
                  initializerRecordOk =
                      expression.type == bindingType &&
                      expression.category == HirValueCategory::Value &&
                      literalFact.type == bindingType &&
                      sameConstant(expression.value, literalFact.literal, module, registries,
                                   semanticTypes) &&
                      sameSpan(expression.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan));
                }
              }
            } else if (binding.initializerKind == SequentialInitializerKind::Cast) {
              // An integer `as` cast over a scalar literal inner expression. The
              // cast is a no-op for widening or identity conversions; validate
              // the cast fact and the inner literal expression record.
              auto castIndex = factIndex(facts.casts(), binding.initializer);
              if (castIndex != zc::none) {
                size_t castSlot = 0;
                ZC_IF_SOME(value, castIndex) { castSlot = value; }
                const auto& castFact = facts.casts().entries()[castSlot].value;
                if (castFact.node == binding.initializer && castFact.result == bindingType &&
                    castFact.target == bindingType && castFact.source == bindingType) {
                  auto literalIndex = factIndex(facts.literals(), binding.castInnerNode);
                  if (literalIndex != zc::none) {
                    size_t literalSlot = 0;
                    ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
                    const auto& literalFact = facts.literals().entries()[literalSlot].value;
                    if (literalFact.type == bindingType) {
                      for (const auto& expression : candidate.impl->expressions) {
                        if (expression.node != initializerNodeId) continue;
                        if (initializerRecordOk) {
                          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                              ir::IrFailureKind::AdditionalFact,
                                                              module, registries, index + 1);
                        }
                        initializerRecordOk =
                            expression.type == bindingType &&
                            expression.category == HirValueCategory::Value &&
                            sameConstant(expression.value, literalFact.literal, module, registries,
                                         semanticTypes) &&
                            sameSpan(expression.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan));
                      }
                    }
                  }
                }
              }
            } else if (binding.initializerKind == SequentialInitializerKind::Ternary) {
              // Ternary bindings are not admitted in the leading-local
              // conditional shape; leadingLocalConditionalShape rejects any
              // initializer that is not a scalar literal or identifier. This
              // arm is a fail-closed defense in depth.
              initializerRecordOk = false;
            } else if (binding.initializerKind == SequentialInitializerKind::LocalReference) {
              auto referenceBinding = resolvedOwnerLocal(bound.bindings(), binding.initializer);
              for (const auto& reference : candidate.impl->localReferences) {
                if (reference.node != initializerNodeId) continue;
                if (initializerRecordOk) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::AdditionalFact, module,
                                                      registries, index + 1);
                }
                initializerRecordOk =
                    referenceBinding != zc::none &&
                    binding.referencedLocal < localBindingIds.size() &&
                    ZC_ASSERT_NONNULL(referenceBinding) ==
                        localBindingIds[binding.referencedLocal] &&
                    reference.local ==
                        hirLocalId(static_cast<uint32_t>(binding.referencedLocal + 1)) &&
                    reference.type == bindingType &&
                    reference.category == HirValueCategory::Place &&
                    sameSpan(reference.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan));
              }
            } else {
              auto parameterHandle =
                  resolvedCallableParameter(bound.bindings(), binding.initializer);
              for (const auto& reference : candidate.impl->parameterReferences) {
                if (reference.node != initializerNodeId) continue;
                if (initializerRecordOk) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::AdditionalFact, module,
                                                      registries, index + 1);
                }
                bool parameterMatches = false;
                if (parameterHandle != zc::none) {
                  identity::CallableParameterId handle;
                  ZC_IF_SOME(value, parameterHandle) { handle = value; }
                  auto authority = registries.callableParameter(handle);
                  ZC_IF_SOME(entry, authority) {
                    parameterMatches =
                        reference.parameter == entry.key() && reference.type == bindingType &&
                        reference.category == HirValueCategory::Place &&
                        sameSpan(reference.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan));
                  }
                }
                initializerRecordOk = parameterMatches;
              }
            }
            if (!initializerRecordOk) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            if (!isArithmeticBinding) { localBindingIds.add(ZC_ASSERT_NONNULL(ownerBinding)); }
          }
          // Condition call fact: equality comparison or unary logical-not. The
          // unary `!x` desugars to `x == false` in HIR, so the tail verification
          // below shares the equality-record shape; only the checker call fact
          // and operand coverage differ.
          identity::SemanticTypeId operandType;
          checker::PrimitiveOperation hirOperation;
          zc::Maybe<identity::SourceSpan> leftSpan;
          zc::Maybe<identity::SourceSpan> rightSpan;
          if (source.conditionIsUnary) {
            // The unary `!x` condition desugars to `x == false`. Validate the
            // unary call fact and the operand node type; the synthetic false
            // operand has no AST node and carries no checker-produced fact.
            auto operandTypeIndex = factIndex(facts.nodeTypes(), source.conditionUnaryOperand);
            auto unaryCallIndex = factIndex(facts.calls(), source.condition);
            leftSpan = bound.parsedModule().spanFor(tree.node(source.conditionUnaryOperand).range);
            if (operandTypeIndex == zc::none || unaryCallIndex == zc::none ||
                leftSpan == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
            size_t operandTypeSlot = 0;
            size_t unaryCallSlot = 0;
            ZC_IF_SOME(value, operandTypeIndex) { operandTypeSlot = value; }
            ZC_IF_SOME(value, unaryCallIndex) { unaryCallSlot = value; }
            operandType = facts.nodeTypes().entries()[operandTypeSlot].value;
            const auto& unaryCallFact = facts.calls().entries()[unaryCallSlot].value;
            const auto& unaryCall = unaryCallFact.invocation;
            const auto& unarySelected = unaryCall.selected.variant();
            if (unaryCallFact.node != source.condition ||
                !unarySelected.is<checker::checked::PrimitiveCallable>() ||
                unarySelected.get<checker::checked::PrimitiveCallable>().operation !=
                    checker::PrimitiveOperation::LogicalNot ||
                unaryCall.calleeType != operandType || unaryCall.receiver != zc::none ||
                unaryCall.receiverMode != zc::none || unaryCall.receiverAdjustment != zc::none ||
                unaryCall.arguments.size() != 1 ||
                unaryCall.arguments[0].sourceNode != source.conditionUnaryOperand ||
                unaryCall.arguments[0].sourceType != operandType ||
                unaryCall.successType != conditionType || unaryCall.resultType != conditionType ||
                unaryCall.substitutions != zc::none || unaryCall.witnesses != zc::none ||
                unaryCall.raises != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            // The HIR materialization desugars `!x` to `x == false`.
            hirOperation = checker::PrimitiveOperation::Eq;
          } else {
            // Equality comparison call fact.
            auto leftTypeIndex = factIndex(facts.nodeTypes(), source.conditionLeft);
            auto rightTypeIndex = factIndex(facts.nodeTypes(), source.conditionRight);
            auto callIndex = factIndex(facts.calls(), source.condition);
            leftSpan = bound.parsedModule().spanFor(tree.node(source.conditionLeft).range);
            rightSpan = bound.parsedModule().spanFor(tree.node(source.conditionRight).range);
            if (leftTypeIndex == zc::none || rightTypeIndex == zc::none || callIndex == zc::none ||
                leftSpan == zc::none || rightSpan == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
            size_t leftTypeSlot = 0;
            size_t rightTypeSlot = 0;
            size_t callSlot = 0;
            ZC_IF_SOME(value, leftTypeIndex) { leftTypeSlot = value; }
            ZC_IF_SOME(value, rightTypeIndex) { rightTypeSlot = value; }
            ZC_IF_SOME(value, callIndex) { callSlot = value; }
            operandType = facts.nodeTypes().entries()[leftTypeSlot].value;
            const auto rightType = facts.nodeTypes().entries()[rightTypeSlot].value;
            const auto& callFact = facts.calls().entries()[callSlot].value;
            const auto& call = callFact.invocation;
            const auto& selected = call.selected.variant();
            if (operandType != rightType || callFact.node != source.condition ||
                !selected.is<checker::checked::PrimitiveCallable>() ||
                !(isScalarComparisonOperation(
                      selected.get<checker::checked::PrimitiveCallable>().operation) ||
                  selected.get<checker::checked::PrimitiveCallable>().operation ==
                      checker::PrimitiveOperation::LogicalAnd ||
                  selected.get<checker::checked::PrimitiveCallable>().operation ==
                      checker::PrimitiveOperation::LogicalOr) ||
                call.calleeType != operandType || call.receiver != zc::none ||
                call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
                call.arguments.size() != 2 ||
                call.arguments[0].sourceNode != source.conditionLeft ||
                call.arguments[0].sourceType != operandType ||
                call.arguments[1].sourceNode != source.conditionRight ||
                call.arguments[1].sourceType != operandType || call.successType != conditionType ||
                call.resultType != conditionType || call.substitutions != zc::none ||
                call.witnesses != zc::none || call.raises != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            hirOperation = selected.get<checker::checked::PrimitiveCallable>().operation;
          }
          // Verify one comparison operand node: literal, parameter reference, or
          // leading-local reference.
          auto verifyConditionOperand = [&](bool isLiteral, ast::NodeId operandSourceNode,
                                            HirNodeId operandId,
                                            const identity::SourceSpan& operandSpan) -> bool {
            if (isLiteral) {
              auto literalIndex = factIndex(facts.literals(), operandSourceNode);
              if (literalIndex == zc::none) return false;
              size_t literalSlot = 0;
              ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
              const auto& literalFact = facts.literals().entries()[literalSlot].value;
              for (const auto& expression : candidate.impl->expressions) {
                if (expression.node != operandId) continue;
                return expression.type == operandType &&
                       expression.category == HirValueCategory::Value &&
                       sameConstant(expression.value, literalFact.literal, module, registries,
                                    semanticTypes) &&
                       sameSpan(expression.sourceSpan, operandSpan);
              }
              return false;
            }
            // A synthesized arithmetic binding has no source VariableDeclarator,
            // so resolvedOwnerLocal cannot find it. Match the operand against the
            // arithmetic bindings' initializer nodes directly.
            for (size_t arithmeticIndex = 0; arithmeticIndex < bindingCount; ++arithmeticIndex) {
              if (leading.bindings[arithmeticIndex].initializerKind !=
                  SequentialInitializerKind::PrimitiveBinary) {
                continue;
              }
              if (leading.bindings[arithmeticIndex].initializer != operandSourceNode) continue;
              for (const auto& reference : candidate.impl->localReferences) {
                if (reference.node != operandId) continue;
                return reference.local == hirLocalId(static_cast<uint32_t>(arithmeticIndex + 1)) &&
                       reference.type == operandType &&
                       reference.category == HirValueCategory::Place &&
                       sameSpan(reference.sourceSpan, operandSpan);
              }
              return false;
            }
            auto ownerBinding = resolvedOwnerLocal(bound.bindings(), operandSourceNode);
            if (ownerBinding != zc::none) {
              for (size_t bindingIndex = 0; bindingIndex < localBindingIds.size(); ++bindingIndex) {
                if (ZC_ASSERT_NONNULL(ownerBinding) != localBindingIds[bindingIndex]) continue;
                for (const auto& reference : candidate.impl->localReferences) {
                  if (reference.node != operandId) continue;
                  return reference.local == hirLocalId(static_cast<uint32_t>(bindingIndex + 1)) &&
                         reference.type == operandType &&
                         reference.category == HirValueCategory::Place &&
                         sameSpan(reference.sourceSpan, operandSpan);
                }
                return false;
              }
              return false;
            }
            auto parameter = resolvedCallableParameter(bound.bindings(), operandSourceNode);
            if (parameter == zc::none) return false;
            identity::CallableParameterId handle;
            ZC_IF_SOME(value, parameter) { handle = value; }
            auto authority = registries.callableParameter(handle);
            if (authority == zc::none) return false;
            for (const auto& reference : candidate.impl->parameterReferences) {
              if (reference.node != operandId) continue;
              ZC_IF_SOME(entry, authority) {
                return reference.parameter == entry.key() && reference.type == operandType &&
                       reference.category == HirValueCategory::Place &&
                       sameSpan(reference.sourceSpan, operandSpan);
              }
            }
            return false;
          };
          if (source.conditionIsUnary) {
            // The unary condition has one real operand (the `x` in `!x`); the
            // synthetic false right operand has no AST node and no HIR record
            // to cross-check.
            if (!verifyConditionOperand(source.conditionUnaryOperandIsLiteral,
                                        source.conditionUnaryOperand, leftId,
                                        ZC_ASSERT_NONNULL(leftSpan))) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
          } else if (!verifyConditionOperand(source.conditionLeftIsLiteral, source.conditionLeft,
                                             leftId, ZC_ASSERT_NONNULL(leftSpan)) ||
                     !verifyConditionOperand(source.conditionRightIsLiteral, source.conditionRight,
                                             rightId, ZC_ASSERT_NONNULL(rightSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          // Equality, arms, conditional, and return records.
          zc::Maybe<const HirPrimitiveBinaryExpression&> equalityRecord;
          for (const auto& equality : candidate.impl->primitiveBinaryOperations) {
            if (equality.node != equalityId) continue;
            if (equalityRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            equalityRecord = equality;
          }
          if (equalityRecord == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          const auto armLiteralOk = [&](HirNodeId armId, ast::NodeId armSourceNode,
                                        const identity::SourceSpan& armSpan) -> bool {
            auto literalIndex = factIndex(facts.literals(), armSourceNode);
            if (literalIndex == zc::none) return false;
            size_t literalSlot = 0;
            ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            for (const auto& expression : candidate.impl->expressions) {
              if (expression.node != armId) continue;
              return expression.type == function.resultType &&
                     expression.category == HirValueCategory::Value &&
                     sameConstant(expression.value, literalFact.literal, module, registries,
                                  semanticTypes) &&
                     sameSpan(expression.sourceSpan, armSpan);
            }
            return false;
          };
          zc::Maybe<const HirConditionalExpression&> conditionalRecord;
          for (const auto& candidateConditional : candidate.impl->conditionals) {
            if (candidateConditional.node != conditionalId) continue;
            if (conditionalRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            conditionalRecord = candidateConditional;
          }
          const auto& equality = ZC_ASSERT_NONNULL(equalityRecord);
          bool tailOk = equality.left == leftId && equality.right == rightId &&
                        equality.operandType == operandType && equality.type == conditionType &&
                        equality.category == HirValueCategory::Value &&
                        equality.operation == hirOperation &&
                        sameSpan(equality.sourceSpan, ZC_ASSERT_NONNULL(conditionSpan)) &&
                        armLiteralOk(thenId, source.thenReturnValue, ZC_ASSERT_NONNULL(thenSpan)) &&
                        armLiteralOk(elseId, source.elseReturnValue, ZC_ASSERT_NONNULL(elseSpan));
          if (conditionalRecord != zc::none) {
            const auto& conditional = ZC_ASSERT_NONNULL(conditionalRecord);
            tailOk = tailOk && conditional.condition == equalityId &&
                     conditional.thenReturnValue == thenId &&
                     conditional.elseReturnValue == elseId &&
                     conditional.type == function.resultType &&
                     conditional.category == HirValueCategory::Value &&
                     sameSpan(conditional.sourceSpan, ZC_ASSERT_NONNULL(returnSpan));
          } else {
            tailOk = false;
          }
          zc::Maybe<const HirReturnStatement&> returnRecord;
          for (const auto& candidateReturn : candidate.impl->returns) {
            if (candidateReturn.node != returnId) continue;
            if (returnRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            returnRecord = candidateReturn;
          }
          if (returnRecord != zc::none) {
            const auto& returnValue = ZC_ASSERT_NONNULL(returnRecord);
            tailOk = tailOk && returnValue.value == conditionalId &&
                     returnValue.resultType == function.resultType &&
                     sameSpan(returnValue.sourceSpan, ZC_ASSERT_NONNULL(returnSpan));
          } else {
            tailOk = false;
          }
          if (!tailOk || !typeExists(operandType, semanticTypes) ||
              !typeExists(conditionType, semanticTypes)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          nextFunction +=
              9u + static_cast<uint32_t>(bindingCount) * 2u + arithmeticBindingCount * 2u;
          continue;
        }
        const bool conditionalIsMethod = function.receiver != zc::none;
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
        if (signaturePosition == zc::none || (!conditionalIsMethod && rootPosition == zc::none)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t signatureSlot = 0;
        ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
        size_t rootSlot = 0;
        ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
        const auto& signature = signatures.definitions[signatureSlot];
        checker::signature::MemberSignatureScope conditionalMethodScope;
        zc::Maybe<HirVisibility> expectedVisibility;
        if (conditionalIsMethod) {
          if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
              !signature.scope.variant().is<checker::signature::MemberSignatureScope>()) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          conditionalMethodScope =
              signature.scope.variant().get<checker::signature::MemberSignatureScope>();
          expectedVisibility = memberVisibility(conditionalMethodScope.visibility, module);
        } else {
          const auto& root = signatures.roots[rootSlot];
          expectedVisibility = visibility(root.visibility);
          if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
              !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        auto expectedLinkage = linkage(callable);
        bool headerValid =
            expectedVisibility != zc::none && expectedLinkage != zc::none &&
            signature.definition == function.definition &&
            signature.definitionKind == (conditionalIsMethod
                                             ? identity::DefinitionKind::Method
                                             : identity::DefinitionKind::Function) &&
            (conditionalIsMethod || signatures.roots[rootSlot].sourceModule == module) &&
            (conditionalIsMethod ? callable.receiver != zc::none : callable.receiver == zc::none) &&
            callable.raises == zc::none && callable.success == function.resultType &&
            sameSpan(signature.declarationSpan, sourceDefinition.source) &&
            sameSpan(function.sourceSpan, sourceDefinition.source) &&
            sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) &&
            function.linkage == ZC_ASSERT_NONNULL(expectedLinkage) &&
            function.parameters.size() == callable.parameters.size();
        if (!headerValid) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        if (conditionalIsMethod) {
          if (conditionalMethodScope.owner == function.definition) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const auto& receiver = ZC_ASSERT_NONNULL(function.receiver);
          if (ZC_ASSERT_NONNULL(callable.receiver).mode !=
              checker::signature::ReceiverMode::Shared) {
            // A conditional method body is admitted only through a shared
            // receiver; a mutable receiver is well-formed source the current
            // lowering does not emit. Drain the owning definition with the
            // capability code rather than an invariant.
            return rejectHirCapability<VerifiedHirModule>(
                function.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                sourceDefinition.source.clone());
          }
          if (ZC_ASSERT_NONNULL(callable.receiver).parameter != receiver.key ||
              !typeExists(receiver.type, semanticTypes)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const ast::NodeId methodParameterListNode(
              tree.node(sourceDefinition.node).payload.words[ast::kMethodDeclParamsIdWord]);
          if (!tree.contains(methodParameterListNode) ||
              tree.node(methodParameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const ast::NodeList methodAstParameters{
              tree.node(methodParameterListNode)
                  .payload.words[ast::kFunctionParameterListParamsFirstWord],
              tree.node(methodParameterListNode)
                  .payload.words[ast::kFunctionParameterListParamsSizeWord]};
          if (!tree.contains(methodAstParameters) ||
              methodAstParameters.size != callable.parameters.size() + 1) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        for (size_t parameterIndex = 0; parameterIndex < function.parameters.size();
             ++parameterIndex) {
          const auto& parameter = function.parameters[parameterIndex];
          const auto& sourceParameter = callable.parameters[parameterIndex];
          if (parameter.key != sourceParameter.parameter ||
              parameter.type != sourceParameter.type || sourceParameter.hasDefault ||
              !typeExists(parameter.type, semanticTypes)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
        auto conditionSpan = bound.parsedModule().spanFor(tree.node(source.condition).range);
        auto thenSpan = bound.parsedModule().spanFor(tree.node(source.thenReturnValue).range);
        auto elseSpan = bound.parsedModule().spanFor(tree.node(source.elseReturnValue).range);
        if (bodySpan == zc::none || returnSpan == zc::none || conditionSpan == zc::none ||
            thenSpan == zc::none || elseSpan == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        auto conditionTypeIndex = factIndex(facts.nodeTypes(), source.condition);
        auto thenTypeIndex = factIndex(facts.nodeTypes(), source.thenReturnValue);
        auto elseTypeIndex = factIndex(facts.nodeTypes(), source.elseReturnValue);
        const bool thenIsParameter =
            tree.node(source.thenReturnValue).kind == ast::SyntaxKind::IdentExpr;
        const bool elseIsParameter =
            tree.node(source.elseReturnValue).kind == ast::SyntaxKind::IdentExpr;
        auto thenLiteralIndex =
            thenIsParameter ? zc::none : factIndex(facts.literals(), source.thenReturnValue);
        auto elseLiteralIndex =
            elseIsParameter ? zc::none : factIndex(facts.literals(), source.elseReturnValue);
        if (conditionTypeIndex == zc::none || thenTypeIndex == zc::none ||
            elseTypeIndex == zc::none || (!thenIsParameter && thenLiteralIndex == zc::none) ||
            (!elseIsParameter && elseLiteralIndex == zc::none)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t conditionTypeSlot = 0;
        size_t thenTypeSlot = 0;
        size_t elseTypeSlot = 0;
        ZC_IF_SOME(value, conditionTypeIndex) { conditionTypeSlot = value; }
        ZC_IF_SOME(value, thenTypeIndex) { thenTypeSlot = value; }
        ZC_IF_SOME(value, elseTypeIndex) { elseTypeSlot = value; }
        const auto conditionType = facts.nodeTypes().entries()[conditionTypeSlot].value;
        const auto thenType = facts.nodeTypes().entries()[thenTypeSlot].value;
        const auto elseType = facts.nodeTypes().entries()[elseTypeSlot].value;
        if (thenType != function.resultType || elseType != function.resultType) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // The condition is either a bare bool parameter reference, an `a == b`
        // equality comparison of two same-typed scalar parameters, or a unary
        // `!x` desugared to `x == false`. The comparison and unary forms
        // materialize the same 9-node stride; the bare-parameter form uses 7.
        const bool conditionIsEquality = source.conditionIsEquality;
        const bool conditionIsUnary = source.conditionIsUnary;
        const bool conditionHasEqualityStride = conditionIsEquality || conditionIsUnary;
        const uint32_t thenOffset = conditionHasEqualityStride ? 5 : 3;
        const uint32_t elseOffset = conditionHasEqualityStride ? 6 : 4;
        const uint32_t conditionalOffset = conditionHasEqualityStride ? 7 : 5;
        const uint32_t returnOffset = conditionHasEqualityStride ? 8 : 6;
        const uint32_t functionNodeCount = conditionHasEqualityStride ? 9 : 7;
        // Common arm verification: each arm value node resolves to a
        // scalar-literal expression or a parameter reference depending on its AST
        // kind. Locate the matching materialized node and cross-check it.
        auto verifyArm = [&](bool isParameter, ast::NodeId armSourceNode, HirNodeId armId,
                             identity::SemanticTypeId armType, zc::Maybe<size_t> literalSlotIndex,
                             const identity::SourceSpan& armSpan) -> bool {
          if (isParameter) {
            auto parameter = resolvedCallableParameter(bound.bindings(), armSourceNode);
            if (parameter == zc::none) return false;
            identity::CallableParameterId handle;
            ZC_IF_SOME(value, parameter) { handle = value; }
            auto authority = registries.callableParameter(handle);
            if (authority == zc::none) return false;
            zc::Maybe<const HirParameterReferenceExpression&> reference;
            for (const auto& candidateReference : candidate.impl->parameterReferences) {
              if (candidateReference.node != armId) continue;
              if (reference != zc::none) return false;
              reference = candidateReference;
            }
            bool ok = false;
            ZC_IF_SOME(referenceValue, reference) {
              ZC_IF_SOME(authorityValue, authority) {
                ok = referenceValue.parameter == authorityValue.key() &&
                     referenceValue.type == armType &&
                     referenceValue.category == HirValueCategory::Place &&
                     sameSpan(referenceValue.sourceSpan, armSpan);
              }
            }
            return ok;
          }
          if (literalSlotIndex == zc::none) return false;
          size_t literalSlot = 0;
          ZC_IF_SOME(value, literalSlotIndex) { literalSlot = value; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          zc::Maybe<const HirScalarLiteralExpression&> expressionValue;
          for (const auto& expression : candidate.impl->expressions) {
            if (expression.node != armId) continue;
            if (expressionValue != zc::none) return false;
            expressionValue = expression;
          }
          bool ok = false;
          ZC_IF_SOME(expression, expressionValue) {
            ok = expression.type == armType && expression.category == HirValueCategory::Value &&
                 sameConstant(expression.value, literalFact.literal, module, registries,
                              semanticTypes) &&
                 sameSpan(expression.sourceSpan, armSpan);
          }
          return ok;
        };
        zc::Maybe<const HirConditionalExpression&> conditionalValue;
        for (const auto& candidate : candidate.impl->conditionals) {
          if (candidate.node != hirId(expectedFunction + conditionalOffset)) continue;
          if (conditionalValue != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          conditionalValue = candidate;
        }
        if (conditionalValue == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        if (!verifyArm(thenIsParameter, source.thenReturnValue,
                       hirId(expectedFunction + thenOffset), thenType, thenLiteralIndex,
                       ZC_ASSERT_NONNULL(thenSpan)) ||
            !verifyArm(elseIsParameter, source.elseReturnValue,
                       hirId(expectedFunction + elseOffset), elseType, elseLiteralIndex,
                       ZC_ASSERT_NONNULL(elseSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Verify the shared function/block/return skeleton and arm node ids.
        bool skeletonOk = false;
        ZC_IF_SOME(conditional, conditionalValue) {
          skeletonOk = function.node == hirId(expectedFunction) &&
                       block.node == hirId(expectedFunction + 1) && function.body == block.node &&
                       block.statements.size() == 1 &&
                       block.statements[0] == returnStatement.node &&
                       returnStatement.node == hirId(expectedFunction + returnOffset) &&
                       returnStatement.value == conditional.node &&
                       returnStatement.resultType == function.resultType &&
                       conditional.thenReturnValue == hirId(expectedFunction + thenOffset) &&
                       conditional.elseReturnValue == hirId(expectedFunction + elseOffset) &&
                       conditional.type == function.resultType &&
                       conditional.category == HirValueCategory::Value &&
                       sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) &&
                       sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) &&
                       sameSpan(conditional.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) &&
                       typeExists(function.resultType, semanticTypes) &&
                       typeExists(conditionType, semanticTypes);
        }
        if (!skeletonOk) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        if (conditionIsEquality) {
          // Cross-check the two operand nodes and the comparison node against the
          // checked relational call fact. Each operand is a parameter reference
          // or a scalar literal; the shared operand type comes from the fact.
          auto leftTypeIndex = factIndex(facts.nodeTypes(), source.conditionLeft);
          auto rightTypeIndex = factIndex(facts.nodeTypes(), source.conditionRight);
          auto callIndex = factIndex(facts.calls(), source.condition);
          auto leftSpan = bound.parsedModule().spanFor(tree.node(source.conditionLeft).range);
          auto rightSpan = bound.parsedModule().spanFor(tree.node(source.conditionRight).range);
          if (leftTypeIndex == zc::none || rightTypeIndex == zc::none || callIndex == zc::none ||
              leftSpan == zc::none || rightSpan == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          size_t leftTypeSlot = 0;
          size_t rightTypeSlot = 0;
          size_t callSlot = 0;
          ZC_IF_SOME(value, leftTypeIndex) { leftTypeSlot = value; }
          ZC_IF_SOME(value, rightTypeIndex) { rightTypeSlot = value; }
          ZC_IF_SOME(value, callIndex) { callSlot = value; }
          const auto operandType = facts.nodeTypes().entries()[leftTypeSlot].value;
          const auto rightType = facts.nodeTypes().entries()[rightTypeSlot].value;
          const auto& callFact = facts.calls().entries()[callSlot].value;
          const auto& call = callFact.invocation;
          const auto& selected = call.selected.variant();
          if (operandType != rightType || callFact.node != source.condition ||
              !selected.is<checker::checked::PrimitiveCallable>() ||
              !(isScalarComparisonOperation(
                    selected.get<checker::checked::PrimitiveCallable>().operation) ||
                selected.get<checker::checked::PrimitiveCallable>().operation ==
                    checker::PrimitiveOperation::LogicalAnd ||
                selected.get<checker::checked::PrimitiveCallable>().operation ==
                    checker::PrimitiveOperation::LogicalOr) ||
              call.calleeType != operandType || call.receiver != zc::none ||
              call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
              call.arguments.size() != 2 || call.arguments[0].sourceNode != source.conditionLeft ||
              call.arguments[0].sourceType != operandType ||
              call.arguments[1].sourceNode != source.conditionRight ||
              call.arguments[1].sourceType != operandType || call.successType != conditionType ||
              call.resultType != conditionType || call.substitutions != zc::none ||
              call.witnesses != zc::none || call.raises != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          // Verify one comparison operand: a parameter operand resolves to a
          // parameter reference at the fixed node id; a literal operand resolves
          // to a scalar-literal expression matching its checked literal fact.
          auto verifyOperand = [&](bool isLiteral, ast::NodeId operandSourceNode,
                                   HirNodeId operandId,
                                   const identity::SourceSpan& operandSpan) -> bool {
            if (!isLiteral) {
              auto parameter = resolvedCallableParameter(bound.bindings(), operandSourceNode);
              if (parameter == zc::none) return false;
              identity::CallableParameterId handle;
              ZC_IF_SOME(value, parameter) { handle = value; }
              auto authority = registries.callableParameter(handle);
              if (authority == zc::none) return false;
              zc::Maybe<const HirParameterReferenceExpression&> reference;
              for (const auto& candidateReference : candidate.impl->parameterReferences) {
                if (candidateReference.node != operandId) continue;
                if (reference != zc::none) return false;
                reference = candidateReference;
              }
              bool ok = false;
              ZC_IF_SOME(referenceValue, reference) {
                ZC_IF_SOME(authorityValue, authority) {
                  ok = referenceValue.parameter == authorityValue.key() &&
                       referenceValue.type == operandType &&
                       referenceValue.category == HirValueCategory::Place &&
                       sameSpan(referenceValue.sourceSpan, operandSpan);
                }
              }
              return ok;
            }
            auto literalIndex = factIndex(facts.literals(), operandSourceNode);
            if (literalIndex == zc::none) return false;
            size_t literalSlot = 0;
            ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            zc::Maybe<const HirScalarLiteralExpression&> expressionValue;
            for (const auto& expression : candidate.impl->expressions) {
              if (expression.node != operandId) continue;
              if (expressionValue != zc::none) return false;
              expressionValue = expression;
            }
            bool ok = false;
            ZC_IF_SOME(expression, expressionValue) {
              ok = expression.type == operandType &&
                   expression.category == HirValueCategory::Value &&
                   sameConstant(expression.value, literalFact.literal, module, registries,
                                semanticTypes) &&
                   sameSpan(expression.sourceSpan, operandSpan);
            }
            return ok;
          };
          if (!verifyOperand(source.conditionLeftIsLiteral, source.conditionLeft,
                             hirId(expectedFunction + 2), ZC_ASSERT_NONNULL(leftSpan)) ||
              !verifyOperand(source.conditionRightIsLiteral, source.conditionRight,
                             hirId(expectedFunction + 3), ZC_ASSERT_NONNULL(rightSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          zc::Maybe<const HirPrimitiveBinaryExpression&> equalityValue;
          for (const auto& equality : candidate.impl->primitiveBinaryOperations) {
            if (equality.node != hirId(expectedFunction + 4)) continue;
            if (equalityValue != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            equalityValue = equality;
          }
          if (equalityValue == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          bool equalityOk = false;
          ZC_IF_SOME(equality, equalityValue) {
            ZC_IF_SOME(conditional, conditionalValue) {
              equalityOk = equality.left == hirId(expectedFunction + 2) &&
                           equality.right == hirId(expectedFunction + 3) &&
                           equality.operandType == operandType && equality.type == conditionType &&
                           equality.category == HirValueCategory::Value &&
                           equality.operation ==
                               selected.get<checker::checked::PrimitiveCallable>().operation &&
                           sameSpan(equality.sourceSpan, ZC_ASSERT_NONNULL(conditionSpan)) &&
                           conditional.condition == equality.node &&
                           typeExists(operandType, semanticTypes);
            }
          }
          if (!equalityOk) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        } else if (conditionIsUnary) {
          // Cross-check the unary `!x` condition against the checked LogicalNot
          // call fact. The HIR builder desugars it to `x == false`; the
          // desugared comparison reuses the equality node layout.
          auto operandTypeIndex = factIndex(facts.nodeTypes(), source.conditionUnaryOperand);
          auto callIndex = factIndex(facts.calls(), source.condition);
          auto operandSpan =
              bound.parsedModule().spanFor(tree.node(source.conditionUnaryOperand).range);
          if (operandTypeIndex == zc::none || callIndex == zc::none || operandSpan == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          size_t operandTypeSlot = 0;
          size_t callSlot = 0;
          ZC_IF_SOME(index, operandTypeIndex) { operandTypeSlot = index; }
          ZC_IF_SOME(index, callIndex) { callSlot = index; }
          const auto operandType = facts.nodeTypes().entries()[operandTypeSlot].value;
          const auto& callFact = facts.calls().entries()[callSlot].value;
          const auto& call = callFact.invocation;
          const auto& selected = call.selected.variant();
          if (callFact.node != source.condition ||
              !selected.is<checker::checked::PrimitiveCallable>() ||
              selected.get<checker::checked::PrimitiveCallable>().operation !=
                  checker::PrimitiveOperation::LogicalNot ||
              call.calleeType != operandType || call.receiver != zc::none ||
              call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
              call.arguments.size() != 1 ||
              call.arguments[0].sourceNode != source.conditionUnaryOperand ||
              call.arguments[0].sourceType != operandType || call.successType != conditionType ||
              call.resultType != conditionType || call.substitutions != zc::none ||
              call.witnesses != zc::none || call.raises != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          // Verify the real operand: a parameter reference or a scalar literal
          // at the fixed node id.
          auto verifyOperand = [&](bool isLiteral, ast::NodeId operandSourceNode,
                                   HirNodeId operandId,
                                   const identity::SourceSpan& operandSpan) -> bool {
            if (!isLiteral) {
              auto parameter = resolvedCallableParameter(bound.bindings(), operandSourceNode);
              if (parameter == zc::none) return false;
              identity::CallableParameterId handle;
              ZC_IF_SOME(value, parameter) { handle = value; }
              auto authority = registries.callableParameter(handle);
              if (authority == zc::none) return false;
              zc::Maybe<const HirParameterReferenceExpression&> reference;
              for (const auto& candidateReference : candidate.impl->parameterReferences) {
                if (candidateReference.node != operandId) continue;
                if (reference != zc::none) return false;
                reference = candidateReference;
              }
              bool ok = false;
              ZC_IF_SOME(referenceValue, reference) {
                ZC_IF_SOME(authorityValue, authority) {
                  ok = referenceValue.parameter == authorityValue.key() &&
                       referenceValue.type == operandType &&
                       referenceValue.category == HirValueCategory::Place &&
                       sameSpan(referenceValue.sourceSpan, operandSpan);
                }
              }
              return ok;
            }
            auto literalIndex = factIndex(facts.literals(), operandSourceNode);
            if (literalIndex == zc::none) return false;
            size_t literalSlot = 0;
            ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            zc::Maybe<const HirScalarLiteralExpression&> expressionValue;
            for (const auto& expression : candidate.impl->expressions) {
              if (expression.node != operandId) continue;
              if (expressionValue != zc::none) return false;
              expressionValue = expression;
            }
            bool ok = false;
            ZC_IF_SOME(expression, expressionValue) {
              ok = expression.type == operandType &&
                   expression.category == HirValueCategory::Value &&
                   sameConstant(expression.value, literalFact.literal, module, registries,
                                semanticTypes) &&
                   sameSpan(expression.sourceSpan, operandSpan);
            }
            return ok;
          };
          if (!verifyOperand(source.conditionUnaryOperandIsLiteral, source.conditionUnaryOperand,
                             hirId(expectedFunction + 2), ZC_ASSERT_NONNULL(operandSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          // The synthetic false operand has no AST node; it materializes as a
          // scalar-literal expression at the fixed node id with no checked
          // literal fact to cross-check. Verify its presence and type only.
          zc::Maybe<const HirScalarLiteralExpression&> syntheticValue;
          for (const auto& expression : candidate.impl->expressions) {
            if (expression.node != hirId(expectedFunction + 3)) continue;
            if (syntheticValue != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            syntheticValue = expression;
          }
          if (syntheticValue == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          bool syntheticOk = false;
          ZC_IF_SOME(synthetic, syntheticValue) {
            auto boolValue = synthetic.value.booleanValue();
            syntheticOk = synthetic.type == operandType &&
                          synthetic.category == HirValueCategory::Value && boolValue != zc::none &&
                          !ZC_ASSERT_NONNULL(boolValue) &&
                          sameSpan(synthetic.sourceSpan, ZC_ASSERT_NONNULL(conditionSpan));
          }
          if (!syntheticOk) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          // Verify the desugared comparison: Eq operation with isUnaryDesugar.
          zc::Maybe<const HirPrimitiveBinaryExpression&> equalityValue;
          for (const auto& equality : candidate.impl->primitiveBinaryOperations) {
            if (equality.node != hirId(expectedFunction + 4)) continue;
            if (equalityValue != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            equalityValue = equality;
          }
          if (equalityValue == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          bool equalityOk = false;
          ZC_IF_SOME(equality, equalityValue) {
            ZC_IF_SOME(conditional, conditionalValue) {
              equalityOk = equality.left == hirId(expectedFunction + 2) &&
                           equality.right == hirId(expectedFunction + 3) &&
                           equality.operandType == operandType && equality.type == conditionType &&
                           equality.category == HirValueCategory::Value &&
                           equality.operation == checker::PrimitiveOperation::Eq &&
                           equality.isUnaryDesugar &&
                           sameSpan(equality.sourceSpan, ZC_ASSERT_NONNULL(conditionSpan)) &&
                           conditional.condition == equality.node &&
                           typeExists(operandType, semanticTypes);
            }
          }
          if (!equalityOk) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        } else {
          auto conditionParameter = resolvedCallableParameter(bound.bindings(), source.condition);
          if (conditionParameter == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          identity::CallableParameterId conditionHandle;
          ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
          auto conditionAuthority = registries.callableParameter(conditionHandle);
          if (conditionAuthority == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          zc::Maybe<const HirParameterReferenceExpression&> conditionReference;
          for (const auto& reference : candidate.impl->parameterReferences) {
            if (reference.node != hirId(expectedFunction + 2)) continue;
            if (conditionReference != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            conditionReference = reference;
          }
          if (conditionReference == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          bool conditionOk = false;
          ZC_IF_SOME(conditional, conditionalValue) {
            ZC_IF_SOME(conditionRef, conditionReference) {
              ZC_IF_SOME(authority, conditionAuthority) {
                conditionOk = conditional.condition == conditionRef.node &&
                              conditionRef.parameter == authority.key() &&
                              conditionRef.type == conditionType &&
                              conditionRef.category == HirValueCategory::Place &&
                              sameSpan(conditionRef.sourceSpan, ZC_ASSERT_NONNULL(conditionSpan));
              }
            }
          }
          if (!conditionOk) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        nextFunction += functionNodeCount;
        continue;
      }
    }
    // Comparison-return shape: `return <a CMP b>`. Fixed-id layout, relative to
    // the function id (6 nodes): +0 function, +1 body block, +2 left operand,
    // +3 right operand, +4 comparison, +5 return. The materializer emits the
    // same six nodes, so this verifier's offsets and nextFunction increment must
    // agree with it exactly.
    {
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          !definitionBelongsToModule(sourceDefinition, definitions) ||
          (sourceDefinition.record.kind() != identity::DefinitionKind::Function &&
           sourceDefinition.record.kind() != identity::DefinitionKind::Method) ||
          !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
          !tree.contains(sourceDefinition.node)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (sourceShape == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      FunctionReturnShape source = zc::mv(sourceShape).orDefault(FunctionReturnShape{});
      if (source.returnsComparison) {
        const bool comparisonIsMethod = function.receiver != zc::none;
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
        if (signaturePosition == zc::none || (!comparisonIsMethod && rootPosition == zc::none)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t signatureSlot = 0;
        size_t rootSlot = 0;
        ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
        ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
        const auto& signature = signatures.definitions[signatureSlot];
        checker::signature::MemberSignatureScope comparisonMethodScope;
        zc::Maybe<HirVisibility> expectedVisibility;
        if (comparisonIsMethod) {
          if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
              !signature.scope.variant().is<checker::signature::MemberSignatureScope>()) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          comparisonMethodScope =
              signature.scope.variant().get<checker::signature::MemberSignatureScope>();
          expectedVisibility = memberVisibility(comparisonMethodScope.visibility, module);
        } else {
          const auto& root = signatures.roots[rootSlot];
          expectedVisibility = visibility(root.visibility);
          if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
              !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        auto expectedLinkage = linkage(callable);
        if (expectedVisibility == zc::none || expectedLinkage == zc::none ||
            signature.definition != function.definition ||
            signature.definitionKind != (comparisonIsMethod ? identity::DefinitionKind::Method
                                                            : identity::DefinitionKind::Function) ||
            (!comparisonIsMethod &&
             signatures.roots[rootSlot].canonicalDefinition != function.definition) ||
            (!comparisonIsMethod && signatures.roots[rootSlot].sourceModule != module) ||
            (comparisonIsMethod ? callable.receiver == zc::none : callable.receiver != zc::none) ||
            callable.raises != zc::none || callable.success != function.resultType ||
            !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
            !sameSpan(function.sourceSpan, sourceDefinition.source) ||
            !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
            function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
            function.parameters.size() != callable.parameters.size()) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        if (comparisonIsMethod) {
          if (comparisonMethodScope.owner == function.definition) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const auto& receiver = ZC_ASSERT_NONNULL(function.receiver);
          if (ZC_ASSERT_NONNULL(callable.receiver).mode !=
              checker::signature::ReceiverMode::Shared) {
            // A parameter-binary method body is admitted only through a shared
            // receiver; a mutable receiver is well-formed source the current
            // lowering does not emit. Drain the owning definition with the
            // capability code rather than an invariant.
            return rejectHirCapability<VerifiedHirModule>(
                function.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                sourceDefinition.source.clone());
          }
          if (ZC_ASSERT_NONNULL(callable.receiver).parameter != receiver.key ||
              !typeExists(receiver.type, semanticTypes)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const ast::NodeId methodParameterListNode(
              tree.node(sourceDefinition.node).payload.words[ast::kMethodDeclParamsIdWord]);
          if (!tree.contains(methodParameterListNode) ||
              tree.node(methodParameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const ast::NodeList methodAstParameters{
              tree.node(methodParameterListNode)
                  .payload.words[ast::kFunctionParameterListParamsFirstWord],
              tree.node(methodParameterListNode)
                  .payload.words[ast::kFunctionParameterListParamsSizeWord]};
          if (!tree.contains(methodAstParameters) ||
              methodAstParameters.size != callable.parameters.size() + 1) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        for (size_t parameterIndex = 0; parameterIndex < function.parameters.size();
             ++parameterIndex) {
          const auto& parameter = function.parameters[parameterIndex];
          const auto& sourceParameter = callable.parameters[parameterIndex];
          if (parameter.key != sourceParameter.parameter ||
              parameter.type != sourceParameter.type || sourceParameter.hasDefault ||
              !typeExists(parameter.type, semanticTypes)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
        auto leftSpan = bound.parsedModule().spanFor(tree.node(source.comparisonLeft).range);
        auto rightSpan = bound.parsedModule().spanFor(tree.node(source.comparisonRight).range);
        auto nodeTypeIndex = factIndex(facts.nodeTypes(), source.value);
        auto leftTypeIndex = factIndex(facts.nodeTypes(), source.comparisonLeft);
        auto rightTypeIndex = factIndex(facts.nodeTypes(), source.comparisonRight);
        auto callIndex = factIndex(facts.calls(), source.value);
        if (bodySpan == zc::none || returnSpan == zc::none || valueSpan == zc::none ||
            leftSpan == zc::none || rightSpan == zc::none || nodeTypeIndex == zc::none ||
            leftTypeIndex == zc::none || rightTypeIndex == zc::none || callIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t nodeTypeSlot = 0;
        size_t leftTypeSlot = 0;
        size_t rightTypeSlot = 0;
        size_t callSlot = 0;
        ZC_IF_SOME(value, nodeTypeIndex) { nodeTypeSlot = value; }
        ZC_IF_SOME(value, leftTypeIndex) { leftTypeSlot = value; }
        ZC_IF_SOME(value, rightTypeIndex) { rightTypeSlot = value; }
        ZC_IF_SOME(value, callIndex) { callSlot = value; }
        const auto resultType = facts.nodeTypes().entries()[nodeTypeSlot].value;
        const auto operandType = facts.nodeTypes().entries()[leftTypeSlot].value;
        const auto rightType = facts.nodeTypes().entries()[rightTypeSlot].value;
        const auto& callFact = facts.calls().entries()[callSlot].value;
        const auto& call = callFact.invocation;
        const auto& selected = call.selected.variant();
        // A comparison return produces a bool result; an arithmetic/bitwise
        // return produces the operand type (resultType == operandType). Accept
        // either family for the supported operators.
        const bool operationSupported =
            selected.is<checker::checked::PrimitiveCallable>() &&
            (isScalarComparisonOperation(
                 selected.get<checker::checked::PrimitiveCallable>().operation) ||
             (isScalarArithmeticOperation(
                  selected.get<checker::checked::PrimitiveCallable>().operation) &&
              resultType == operandType));
        if (resultType != function.resultType || operandType != rightType ||
            callFact.node != source.value || !operationSupported ||
            call.calleeType != operandType || call.receiver != zc::none ||
            call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
            call.arguments.size() != 2 || call.arguments[0].sourceNode != source.comparisonLeft ||
            call.arguments[0].sourceType != operandType ||
            call.arguments[1].sourceNode != source.comparisonRight ||
            call.arguments[1].sourceType != operandType || call.successType != resultType ||
            call.resultType != resultType || call.substitutions != zc::none ||
            call.witnesses != zc::none || call.raises != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Verify one comparison operand at its fixed node id: a parameter operand
        // resolves to a parameter reference; a literal operand resolves to a
        // scalar-literal expression matching its checked literal fact.
        auto verifyOperand = [&](bool isLiteral, ast::NodeId operandSourceNode, HirNodeId operandId,
                                 const identity::SourceSpan& operandSpan) -> bool {
          if (!isLiteral) {
            auto parameter = resolvedCallableParameter(bound.bindings(), operandSourceNode);
            if (parameter == zc::none) return false;
            identity::CallableParameterId handle;
            ZC_IF_SOME(value, parameter) { handle = value; }
            auto authority = registries.callableParameter(handle);
            if (authority == zc::none) return false;
            zc::Maybe<const HirParameterReferenceExpression&> reference;
            for (const auto& candidateReference : candidate.impl->parameterReferences) {
              if (candidateReference.node != operandId) continue;
              if (reference != zc::none) return false;
              reference = candidateReference;
            }
            bool ok = false;
            ZC_IF_SOME(referenceValue, reference) {
              ZC_IF_SOME(authorityValue, authority) {
                ok = referenceValue.parameter == authorityValue.key() &&
                     referenceValue.type == operandType &&
                     referenceValue.category == HirValueCategory::Place &&
                     sameSpan(referenceValue.sourceSpan, operandSpan);
              }
            }
            return ok;
          }
          auto literalIndex = factIndex(facts.literals(), operandSourceNode);
          if (literalIndex == zc::none) return false;
          size_t literalSlot = 0;
          ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          zc::Maybe<const HirScalarLiteralExpression&> expressionValue;
          for (const auto& expression : candidate.impl->expressions) {
            if (expression.node != operandId) continue;
            if (expressionValue != zc::none) return false;
            expressionValue = expression;
          }
          bool ok = false;
          ZC_IF_SOME(expression, expressionValue) {
            ok = expression.type == operandType && expression.category == HirValueCategory::Value &&
                 sameConstant(expression.value, literalFact.literal, module, registries,
                              semanticTypes) &&
                 sameSpan(expression.sourceSpan, operandSpan);
          }
          return ok;
        };
        if (!verifyOperand(source.comparisonLeftIsLiteral, source.comparisonLeft,
                           hirId(expectedFunction + 2), ZC_ASSERT_NONNULL(leftSpan)) ||
            !verifyOperand(source.comparisonRightIsLiteral, source.comparisonRight,
                           hirId(expectedFunction + 3), ZC_ASSERT_NONNULL(rightSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        zc::Maybe<const HirPrimitiveBinaryExpression&> comparisonValue;
        for (const auto& comparison : candidate.impl->primitiveBinaryOperations) {
          if (comparison.node != hirId(expectedFunction + 4)) continue;
          if (comparisonValue != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          comparisonValue = comparison;
        }
        if (comparisonValue == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        bool skeletonOk = false;
        ZC_IF_SOME(comparison, comparisonValue) {
          skeletonOk = function.node == hirId(expectedFunction) &&
                       block.node == hirId(expectedFunction + 1) && function.body == block.node &&
                       block.statements.size() == 1 &&
                       block.statements[0] == returnStatement.node &&
                       returnStatement.node == hirId(expectedFunction + 5) &&
                       returnStatement.value == comparison.node &&
                       returnStatement.resultType == function.resultType &&
                       comparison.left == hirId(expectedFunction + 2) &&
                       comparison.right == hirId(expectedFunction + 3) &&
                       comparison.operandType == operandType && comparison.type == resultType &&
                       comparison.category == HirValueCategory::Value &&
                       comparison.operation ==
                           selected.get<checker::checked::PrimitiveCallable>().operation &&
                       sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) &&
                       sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) &&
                       sameSpan(comparison.sourceSpan, ZC_ASSERT_NONNULL(valueSpan)) &&
                       typeExists(function.resultType, semanticTypes) &&
                       typeExists(operandType, semanticTypes);
        }
        if (!skeletonOk) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        nextFunction += 6;
        continue;
      }
    }
    // Unary-return shape: `return <op x>`. The HIR builder desugars the unary
    // operation to an equivalent binary operation with a synthetic constant
    // operand, reusing the comparison-return materialization path. Fixed-id
    // layout, relative to the function id (6 nodes): +0 function, +1 body
    // block, +2 left operand, +3 right operand, +4 binary, +5 return. The
    // synthetic operand has no AST node and no checker-produced fact; the
    // verifier checks its constant value against the desugaring mapping.
    {
      bool isUnaryShape = false;
      ZC_IF_SOME(shape, sourceShapeMaybe) { isUnaryShape = shape.returnsUnary; }
      if (isUnaryShape) {
        const FunctionReturnShape& source = ZC_ASSERT_NONNULL(sourceShapeMaybe);
        auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
        if (sourceDefinitionIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t definitionSlot = 0;
        ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
        const auto& sourceDefinition = definitions.definitions()[definitionSlot];
        const auto& tree = bound.tree();
        if (!hasExecutableBody(sourceDefinition, definitions) ||
            !definitionBelongsToModule(sourceDefinition, definitions) ||
            (sourceDefinition.record.kind() != identity::DefinitionKind::Function &&
             sourceDefinition.record.kind() != identity::DefinitionKind::Method) ||
            !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
            !tree.contains(sourceDefinition.node)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const bool unaryIsMethod = function.receiver != zc::none;
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
        if (signaturePosition == zc::none || (!unaryIsMethod && rootPosition == zc::none)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t signatureSlot = 0;
        size_t rootSlot = 0;
        ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
        ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
        const auto& signature = signatures.definitions[signatureSlot];
        checker::signature::MemberSignatureScope unaryMethodScope;
        zc::Maybe<HirVisibility> expectedVisibility;
        if (unaryIsMethod) {
          if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
              !signature.scope.variant().is<checker::signature::MemberSignatureScope>()) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          unaryMethodScope =
              signature.scope.variant().get<checker::signature::MemberSignatureScope>();
          expectedVisibility = memberVisibility(unaryMethodScope.visibility, module);
        } else {
          const auto& root = signatures.roots[rootSlot];
          expectedVisibility = visibility(root.visibility);
          if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
              !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        auto expectedLinkage = linkage(callable);
        if (expectedVisibility == zc::none || expectedLinkage == zc::none ||
            signature.definition != function.definition ||
            signature.definitionKind != (unaryIsMethod ? identity::DefinitionKind::Method
                                                       : identity::DefinitionKind::Function) ||
            (!unaryIsMethod &&
             signatures.roots[rootSlot].canonicalDefinition != function.definition) ||
            (!unaryIsMethod && signatures.roots[rootSlot].sourceModule != module) ||
            (unaryIsMethod ? callable.receiver == zc::none : callable.receiver != zc::none) ||
            callable.raises != zc::none || callable.success != function.resultType ||
            !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
            !sameSpan(function.sourceSpan, sourceDefinition.source) ||
            !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
            function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
            function.parameters.size() != callable.parameters.size()) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        if (unaryIsMethod) {
          if (unaryMethodScope.owner == function.definition) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const auto& receiver = ZC_ASSERT_NONNULL(function.receiver);
          if (ZC_ASSERT_NONNULL(callable.receiver).mode !=
              checker::signature::ReceiverMode::Shared) {
            return rejectHirCapability<VerifiedHirModule>(
                function.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                sourceDefinition.source.clone());
          }
          if (ZC_ASSERT_NONNULL(callable.receiver).parameter != receiver.key ||
              !typeExists(receiver.type, semanticTypes)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const ast::NodeId methodParameterListNode(
              tree.node(sourceDefinition.node).payload.words[ast::kMethodDeclParamsIdWord]);
          if (!tree.contains(methodParameterListNode) ||
              tree.node(methodParameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const ast::NodeList methodAstParameters{
              tree.node(methodParameterListNode)
                  .payload.words[ast::kFunctionParameterListParamsFirstWord],
              tree.node(methodParameterListNode)
                  .payload.words[ast::kFunctionParameterListParamsSizeWord]};
          if (!tree.contains(methodAstParameters) ||
              methodAstParameters.size != callable.parameters.size() + 1) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        for (size_t parameterIndex = 0; parameterIndex < function.parameters.size();
             ++parameterIndex) {
          const auto& parameter = function.parameters[parameterIndex];
          const auto& sourceParameter = callable.parameters[parameterIndex];
          if (parameter.key != sourceParameter.parameter ||
              parameter.type != sourceParameter.type || sourceParameter.hasDefault ||
              !typeExists(parameter.type, semanticTypes)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        // Unary body validation. The call fact records the unary operation;
        // the HIR carries the desugared binary operation with one synthetic
        // constant operand that has no AST node.
        auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
        auto operandSpan = bound.parsedModule().spanFor(tree.node(source.unaryOperand).range);
        auto nodeTypeIndex = factIndex(facts.nodeTypes(), source.value);
        auto operandTypeIndex = factIndex(facts.nodeTypes(), source.unaryOperand);
        auto callIndex = factIndex(facts.calls(), source.value);
        if (bodySpan == zc::none || returnSpan == zc::none || valueSpan == zc::none ||
            operandSpan == zc::none || nodeTypeIndex == zc::none || operandTypeIndex == zc::none ||
            callIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t nodeTypeSlot = 0;
        size_t operandTypeSlot = 0;
        size_t callSlot = 0;
        ZC_IF_SOME(value, nodeTypeIndex) { nodeTypeSlot = value; }
        ZC_IF_SOME(value, operandTypeIndex) { operandTypeSlot = value; }
        ZC_IF_SOME(value, callIndex) { callSlot = value; }
        const auto resultType = facts.nodeTypes().entries()[nodeTypeSlot].value;
        const auto operandType = facts.nodeTypes().entries()[operandTypeSlot].value;
        const auto& callFact = facts.calls().entries()[callSlot].value;
        const auto& call = callFact.invocation;
        const auto& selected = call.selected.variant();
        const auto selectedOperation =
            selected.is<checker::checked::PrimitiveCallable>()
                ? zc::Maybe<checker::PrimitiveOperation>(
                      selected.get<checker::checked::PrimitiveCallable>().operation)
                : zc::Maybe<checker::PrimitiveOperation>(zc::none);
        bool operationSupported = false;
        ZC_IF_SOME(op, selectedOperation) {
          operationSupported = isScalarUnaryOperation(op) && callable.success == resultType;
        }
        if (resultType != function.resultType || callFact.node != source.value ||
            !operationSupported || call.calleeType != operandType || call.receiver != zc::none ||
            call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
            call.arguments.size() != 1 || call.arguments[0].sourceNode != source.unaryOperand ||
            call.arguments[0].sourceType != operandType || call.successType != resultType ||
            call.resultType != resultType || call.substitutions != zc::none ||
            call.witnesses != zc::none || call.raises != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Derive the expected binary operation from the unary operation:
        //   Neg        -> Sub(0, x)     synthetic on the left
        //   UnaryPlus  -> Add(x, 0)     synthetic on the right
        //   BitNot     -> BitXor(x, -1) synthetic on the right
        //   LogicalNot -> Eq(x, false)  synthetic on the right
        const auto unaryOperation = ZC_ASSERT_NONNULL(selectedOperation);
        checker::PrimitiveOperation expectedBinaryOperation;
        bool syntheticOnLeft = false;
        switch (unaryOperation) {
          case checker::PrimitiveOperation::Neg:
            expectedBinaryOperation = checker::PrimitiveOperation::Sub;
            syntheticOnLeft = true;
            break;
          case checker::PrimitiveOperation::UnaryPlus:
            expectedBinaryOperation = checker::PrimitiveOperation::Add;
            break;
          case checker::PrimitiveOperation::BitNot:
            expectedBinaryOperation = checker::PrimitiveOperation::BitXor;
            break;
          case checker::PrimitiveOperation::LogicalNot:
            expectedBinaryOperation = checker::PrimitiveOperation::Eq;
            break;
          default:
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
        }
        const HirNodeId syntheticNodeId =
            syntheticOnLeft ? hirId(expectedFunction + 2) : hirId(expectedFunction + 3);
        const HirNodeId realNodeId =
            syntheticOnLeft ? hirId(expectedFunction + 3) : hirId(expectedFunction + 2);
        // Find the HIR binary expression at the expected node id.
        zc::Maybe<const HirPrimitiveBinaryExpression&> comparisonValue;
        for (const auto& comparison : candidate.impl->primitiveBinaryOperations) {
          if (comparison.node != hirId(expectedFunction + 4)) continue;
          if (comparisonValue != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          comparisonValue = comparison;
        }
        if (comparisonValue == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        // Find the synthetic literal expression at its expected node id.
        zc::Maybe<const HirScalarLiteralExpression&> syntheticLiteral;
        for (const auto& expression : candidate.impl->expressions) {
          if (expression.node != syntheticNodeId) continue;
          if (syntheticLiteral != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          syntheticLiteral = expression;
        }
        if (syntheticLiteral == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        // Find the real parameter reference at its expected node id. The
        // builder rejects literal operands, so the real operand is always a
        // parameter reference.
        zc::Maybe<const HirParameterReferenceExpression&> realReference;
        for (const auto& reference : candidate.impl->parameterReferences) {
          if (reference.node != realNodeId) continue;
          if (realReference != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          realReference = reference;
        }
        if (realReference == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        // Resolve the real operand to its parameter authority.
        auto parameter = resolvedCallableParameter(bound.bindings(), source.unaryOperand);
        if (parameter == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        identity::CallableParameterId handle;
        ZC_IF_SOME(value, parameter) { handle = value; }
        auto authority = registries.callableParameter(handle);
        if (authority == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        // Verify the synthetic literal value matches the desugaring mapping.
        bool syntheticValueOk = false;
        ZC_IF_SOME(literal, syntheticLiteral) {
          if (literal.type == operandType && literal.category == HirValueCategory::Value &&
              sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(valueSpan))) {
            if (unaryOperation == checker::PrimitiveOperation::LogicalNot) {
              ZC_IF_SOME(boolValue, literal.value.booleanValue()) { syntheticValueOk = !boolValue; }
            } else if (unaryOperation == checker::PrimitiveOperation::BitNot) {
              ZC_IF_SOME(intValue, literal.value.integerValue()) {
                syntheticValueOk = intValue.sign == checker::signature::IntegerSign::Negative &&
                                   intValue.magnitude.size() == 1 && intValue.magnitude[0] == 1;
              }
            } else {
              // Neg or UnaryPlus: zero of the operand type.
              ZC_IF_SOME(intValue, literal.value.integerValue()) {
                syntheticValueOk = intValue.sign == checker::signature::IntegerSign::NonNegative &&
                                   intValue.magnitude.size() == 0;
              }
              if (!syntheticValueOk) {
                ZC_IF_SOME(floatValue, literal.value.floatValue()) {
                  syntheticValueOk = floatValue.bits == 0;
                }
              }
            }
          }
        }
        // Skeleton check: six nodes, correct structure and facts.
        bool skeletonOk = false;
        ZC_IF_SOME(comparison, comparisonValue) {
          ZC_IF_SOME(reference, realReference) {
            ZC_IF_SOME(authorityValue, authority) {
              skeletonOk =
                  function.node == hirId(expectedFunction) &&
                  block.node == hirId(expectedFunction + 1) && function.body == block.node &&
                  block.statements.size() == 1 && block.statements[0] == returnStatement.node &&
                  returnStatement.node == hirId(expectedFunction + 5) &&
                  returnStatement.value == comparison.node &&
                  returnStatement.resultType == function.resultType &&
                  comparison.left == hirId(expectedFunction + 2) &&
                  comparison.right == hirId(expectedFunction + 3) &&
                  comparison.operandType == operandType && comparison.type == resultType &&
                  comparison.category == HirValueCategory::Value &&
                  comparison.operation == expectedBinaryOperation && comparison.isUnaryDesugar &&
                  sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) &&
                  sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) &&
                  sameSpan(comparison.sourceSpan, ZC_ASSERT_NONNULL(valueSpan)) &&
                  typeExists(function.resultType, semanticTypes) &&
                  typeExists(operandType, semanticTypes) &&
                  reference.parameter == authorityValue.key() && reference.type == operandType &&
                  reference.category == HirValueCategory::Place &&
                  sameSpan(reference.sourceSpan, ZC_ASSERT_NONNULL(operandSpan)) &&
                  syntheticValueOk;
            }
          }
        }
        if (!skeletonOk) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        nextFunction += 6;
        continue;
      }
    }
    // Receiver-field arithmetic method shape: a shared or mutable receiver
    // inherent method with no ordinary parameters whose body is the single
    // statement `return this.<field> OP <literal>;`. Six node ids: function,
    // body, field projection, literal, primitive binary, return. The branch is
    // self-contained: it validates the source shape, the receiver-keyed member
    // and place facts, the literal and arithmetic call facts, and the method
    // header before advancing past six nodes. The field projection is a place
    // read; the place mutability follows the receiver mode and is not
    // constrained because the body never writes through it.
    {
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex != zc::none) {
        size_t definitionSlot = 0;
        ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
        const auto& sourceDefinition = definitions.definitions()[definitionSlot];
        const auto& tree = bound.tree();
        if (tree.contains(sourceDefinition.node) &&
            tree.node(sourceDefinition.node).kind == ast::SyntaxKind::MethodDecl) {
          auto sourceShapeMaybe = functionReturnShape(tree, tree.node(sourceDefinition.node));
          if (sourceShapeMaybe != zc::none &&
              ZC_ASSERT_NONNULL(sourceShapeMaybe).returnsReceiverFieldArithmetic) {
            FunctionReturnShape source = zc::mv(sourceShapeMaybe).orDefault(FunctionReturnShape{});
            const bool fieldIsLeft = !source.comparisonLeftIsLiteral;
            if (source.comparisonLeftIsLiteral == source.comparisonRightIsLiteral) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            const ast::NodeId fieldSourceNode =
                fieldIsLeft ? source.comparisonLeft : source.comparisonRight;
            const ast::NodeId literalSourceNode =
                fieldIsLeft ? source.comparisonRight : source.comparisonLeft;
            const HirNodeId fieldNodeId =
                fieldIsLeft ? hirId(expectedFunction + 2) : hirId(expectedFunction + 3);
            const HirNodeId literalNodeId =
                fieldIsLeft ? hirId(expectedFunction + 3) : hirId(expectedFunction + 2);
            const auto binaryNodeId = hirId(expectedFunction + 4);
            const auto returnNodeId = hirId(expectedFunction + 5);
            if (function.node != hirId(expectedFunction) ||
                block.node != hirId(expectedFunction + 1) || function.body != block.node ||
                block.statements.size() != 1 || block.statements[0] != returnStatement.node ||
                returnStatement.node != returnNodeId || returnStatement.value != binaryNodeId ||
                returnStatement.resultType != function.resultType ||
                function.receiver == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            const auto& receiver = ZC_ASSERT_NONNULL(function.receiver);
            zc::Maybe<const HirParameterFieldProjectionExpression&> projectionRecord;
            for (const auto& projection : candidate.impl->parameterFieldProjections) {
              if (projection.node != fieldNodeId) continue;
              if (projectionRecord != zc::none) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::AdditionalFact, module,
                                                    registries, index + 1);
              }
              projectionRecord = projection;
            }
            zc::Maybe<const HirPrimitiveBinaryExpression&> binaryRecord;
            for (const auto& operation : candidate.impl->primitiveBinaryOperations) {
              if (operation.node != binaryNodeId) continue;
              if (binaryRecord != zc::none) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::AdditionalFact, module,
                                                    registries, index + 1);
              }
              binaryRecord = operation;
            }
            zc::Maybe<const HirScalarLiteralExpression&> literalRecord;
            for (const auto& expression : candidate.impl->expressions) {
              if (expression.node != literalNodeId) continue;
              if (literalRecord != zc::none) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::AdditionalFact, module,
                                                    registries, index + 1);
              }
              literalRecord = expression;
            }
            if (projectionRecord == zc::none || binaryRecord == zc::none ||
                literalRecord == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
            const auto& projection = ZC_ASSERT_NONNULL(projectionRecord);
            const auto& binary = ZC_ASSERT_NONNULL(binaryRecord);
            const auto& literalExpression = ZC_ASSERT_NONNULL(literalRecord);
            const ast::NodeId thisNode(
                tree.node(fieldSourceNode).payload.words[ast::kMemberExpressionObjectWord]);
            if (!tree.contains(thisNode) || tree.node(thisNode).kind != ast::SyntaxKind::ThisExpr ||
                projection.parameter != receiver.key || projection.node != fieldNodeId ||
                projection.type != function.resultType ||
                projection.category != HirValueCategory::Place ||
                binary.left != hirId(expectedFunction + 2) ||
                binary.right != hirId(expectedFunction + 3) ||
                binary.operandType != function.resultType || binary.type != function.resultType ||
                binary.category != HirValueCategory::Value ||
                literalExpression.type != function.resultType ||
                literalExpression.category != HirValueCategory::Value) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            if (!hasExecutableBody(sourceDefinition, definitions) ||
                !definitionBelongsToModule(sourceDefinition, definitions) ||
                sourceDefinition.record.kind() != identity::DefinitionKind::Method ||
                !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>()) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            // Method header: member-signature scope, shared or mutable
            // receiver matching the header, zero ordinary parameters, and
            // matching linkage/visibility.
            auto signaturePosition =
                signatureIndex(signatures.definitions.asPtr(), function.definition);
            if (signaturePosition == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
            size_t signatureSlot = 0;
            ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
            const auto& signature = signatures.definitions[signatureSlot];
            if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
                !signature.scope.variant().is<checker::signature::MemberSignatureScope>() ||
                signature.definition != function.definition ||
                signature.definitionKind != identity::DefinitionKind::Method) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            const auto& memberScope =
                signature.scope.variant().get<checker::signature::MemberSignatureScope>();
            const auto& callable =
                signature.payload.variant().get<checker::signature::CallableSignature>();
            auto expectedVisibility = memberVisibility(memberScope.visibility, module);
            auto expectedLinkage = linkage(callable);
            if (memberScope.owner == function.definition || callable.raises != zc::none ||
                callable.success != function.resultType || callable.parameters.size() != 0 ||
                function.parameters.size() != 0 || callable.receiver == zc::none ||
                (ZC_ASSERT_NONNULL(callable.receiver).mode !=
                     checker::signature::ReceiverMode::Shared &&
                 ZC_ASSERT_NONNULL(callable.receiver).mode !=
                     checker::signature::ReceiverMode::Mutable) ||
                ZC_ASSERT_NONNULL(callable.receiver).parameter != receiver.key ||
                expectedLinkage == zc::none ||
                !sameVisibility(function.visibility, expectedVisibility) ||
                function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
                !sameSpan(function.sourceSpan, sourceDefinition.source) ||
                !sameSpan(signature.declarationSpan, sourceDefinition.source)) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            const ast::NodeId parameterListNode(
                tree.node(sourceDefinition.node).payload.words[ast::kMethodDeclParamsIdWord]);
            if (!tree.contains(parameterListNode) ||
                tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            const ast::NodeList astParameters{
                tree.node(parameterListNode)
                    .payload.words[ast::kFunctionParameterListParamsFirstWord],
                tree.node(parameterListNode)
                    .payload.words[ast::kFunctionParameterListParamsSizeWord]};
            if (!tree.contains(astParameters) || astParameters.size != 1) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            // The receiver parameter carries a reference `&Owner` or
            // `&mut Owner`; its referent must equal the projection's receiver
            // type.
            auto receiverLookup = semanticTypes.get(receiver.type);
            if (!receiverLookup.is<type::SemanticTypeLookup>() ||
                !receiverLookup.get<type::SemanticTypeLookup>()
                     .data()
                     .is<type::semantic::ReferenceTypeData>()) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            const auto& receiverReference = receiverLookup.get<type::SemanticTypeLookup>()
                                                .data()
                                                .get<type::semantic::ReferenceTypeData>();
            if (receiverReference.referent != projection.receiverType) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            // Both shared and mutable receivers are admitted: the body only
            // reads the field, so the receiver mutability does not affect the
            // arithmetic lowering.
            auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
            auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
            auto fieldSpan = bound.parsedModule().spanFor(tree.node(fieldSourceNode).range);
            auto literalSpan = bound.parsedModule().spanFor(tree.node(literalSourceNode).range);
            auto binarySpan = bound.parsedModule().spanFor(tree.node(source.value).range);
            auto thisTypeIndex = factIndex(facts.nodeTypes(), thisNode);
            auto fieldTypeIndex = factIndex(facts.nodeTypes(), fieldSourceNode);
            auto literalTypeIndex = factIndex(facts.nodeTypes(), literalSourceNode);
            auto memberIndex = factIndex(facts.members(), fieldSourceNode);
            auto placeIndex = factIndex(facts.places(), fieldSourceNode);
            auto literalIndex = factIndex(facts.literals(), literalSourceNode);
            auto callIndex = factIndex(facts.calls(), source.value);
            if (bodySpan == zc::none || returnSpan == zc::none || fieldSpan == zc::none ||
                literalSpan == zc::none || binarySpan == zc::none || thisTypeIndex == zc::none ||
                fieldTypeIndex == zc::none || literalTypeIndex == zc::none ||
                memberIndex == zc::none || placeIndex == zc::none || literalIndex == zc::none ||
                callIndex == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
            size_t thisTypeSlot = 0;
            size_t fieldTypeSlot = 0;
            size_t literalTypeSlot = 0;
            size_t memberSlot = 0;
            size_t placeSlot = 0;
            size_t literalSlot = 0;
            size_t callSlot = 0;
            ZC_IF_SOME(value, thisTypeIndex) { thisTypeSlot = value; }
            ZC_IF_SOME(value, fieldTypeIndex) { fieldTypeSlot = value; }
            ZC_IF_SOME(value, literalTypeIndex) { literalTypeSlot = value; }
            ZC_IF_SOME(value, memberIndex) { memberSlot = value; }
            ZC_IF_SOME(value, placeIndex) { placeSlot = value; }
            ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
            ZC_IF_SOME(value, callIndex) { callSlot = value; }
            const auto& memberFact = facts.members().entries()[memberSlot].value;
            const auto& placeFact = facts.places().entries()[placeSlot].value;
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            const auto& callFact = facts.calls().entries()[callSlot].value;
            const auto& invocation = callFact.invocation;
            const auto& selected = invocation.selected.variant();
            const bool arithmeticSupported =
                selected.is<checker::checked::PrimitiveCallable>() &&
                isScalarArithmeticOperation(
                    selected.get<checker::checked::PrimitiveCallable>().operation);
            if (facts.nodeTypes().entries()[thisTypeSlot].value != receiver.type ||
                facts.nodeTypes().entries()[fieldTypeSlot].value != projection.type ||
                facts.nodeTypes().entries()[literalTypeSlot].value != projection.type ||
                memberFact.node != fieldSourceNode ||
                memberFact.receiverType != projection.receiverType ||
                memberFact.member != projection.field || memberFact.memberType != projection.type ||
                memberFact.adjustment != zc::none || placeFact.node != fieldSourceNode ||
                placeFact.type != projection.type || !placeFact.movable ||
                !placeFact.root.variant().is<checker::checked::CallableParameterPlaceRoot>() ||
                placeFact.projections.size() != 1 ||
                !placeFact.projections[0].variant().is<checker::checked::FieldProjection>() ||
                placeFact.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                    projection.field ||
                literalFact.node != literalSourceNode || literalFact.type != projection.type ||
                !sameConstant(literalExpression.value, literalFact.literal, module, registries,
                              semanticTypes) ||
                callFact.node != source.value || !arithmeticSupported ||
                invocation.calleeType != projection.type || invocation.receiver != zc::none ||
                invocation.receiverMode != zc::none || invocation.receiverAdjustment != zc::none ||
                invocation.arguments.size() != 2 ||
                invocation.arguments[0].sourceNode != source.comparisonLeft ||
                invocation.arguments[0].sourceType != projection.type ||
                invocation.arguments[1].sourceNode != source.comparisonRight ||
                invocation.arguments[1].sourceType != projection.type ||
                invocation.successType != function.resultType ||
                invocation.resultType != function.resultType ||
                invocation.substitutions != zc::none || invocation.witnesses != zc::none ||
                invocation.raises != zc::none ||
                binary.operation != selected.get<checker::checked::PrimitiveCallable>().operation) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            auto rootAuthority = registries.callableParameter(
                placeFact.root.variant()
                    .get<checker::checked::CallableParameterPlaceRoot>()
                    .parameter);
            if (rootAuthority == zc::none ||
                ZC_ASSERT_NONNULL(rootAuthority).key() != receiver.key) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            if (!sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) ||
                !sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) ||
                !sameSpan(projection.sourceSpan, ZC_ASSERT_NONNULL(fieldSpan)) ||
                !sameSpan(literalExpression.sourceSpan, ZC_ASSERT_NONNULL(literalSpan)) ||
                !sameSpan(binary.sourceSpan, ZC_ASSERT_NONNULL(binarySpan)) ||
                !typeExists(receiver.type, semanticTypes) ||
                !typeExists(projection.type, semanticTypes)) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            nextFunction += 6;
            continue;
          }
        }
      }
    }
    // Loop shape: a `while` with a bool parameter condition and empty body,
    // followed by a scalar return.
    {
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          !definitionBelongsToModule(sourceDefinition, definitions) ||
          (sourceDefinition.record.kind() != identity::DefinitionKind::Function &&
           sourceDefinition.record.kind() != identity::DefinitionKind::Method) ||
          !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
          !tree.contains(sourceDefinition.node)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (sourceShape == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      FunctionReturnShape source = zc::mv(sourceShape).orDefault(FunctionReturnShape{});
      if (source.isLoop) {
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
        if (signaturePosition == zc::none || rootPosition == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t signatureSlot = 0;
        size_t rootSlot = 0;
        ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
        ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
        const auto& signature = signatures.definitions[signatureSlot];
        const auto& root = signatures.roots[rootSlot];
        auto expectedVisibility = visibility(root.visibility);
        if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
            !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>() ||
            expectedVisibility == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        auto expectedLinkage = linkage(callable);
        if (expectedLinkage == zc::none || signature.definition != function.definition ||
            signature.definitionKind != identity::DefinitionKind::Function ||
            root.canonicalDefinition != function.definition || root.sourceModule != module ||
            callable.receiver != zc::none || callable.raises != zc::none ||
            callable.success != function.resultType ||
            !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
            !sameSpan(function.sourceSpan, sourceDefinition.source) ||
            !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
            function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
            function.parameters.size() != callable.parameters.size()) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        for (size_t parameterIndex = 0; parameterIndex < function.parameters.size();
             ++parameterIndex) {
          const auto& parameter = function.parameters[parameterIndex];
          const auto& sourceParameter = callable.parameters[parameterIndex];
          if (parameter.key != sourceParameter.parameter ||
              parameter.type != sourceParameter.type || sourceParameter.hasDefault ||
              !typeExists(parameter.type, semanticTypes)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
        auto conditionSpan = bound.parsedModule().spanFor(tree.node(source.loopCondition).range);
        auto loopSpan = bound.parsedModule().spanFor(tree.node(source.loopStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
        if (bodySpan == zc::none || returnSpan == zc::none || conditionSpan == zc::none ||
            loopSpan == zc::none || valueSpan == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        auto conditionTypeIndex = factIndex(facts.nodeTypes(), source.loopCondition);
        auto returnTypeIndex = factIndex(facts.nodeTypes(), source.value);
        auto returnLiteralIndex = factIndex(facts.literals(), source.value);
        if (conditionTypeIndex == zc::none || returnTypeIndex == zc::none ||
            returnLiteralIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t conditionTypeSlot = 0;
        size_t returnTypeSlot = 0;
        size_t returnLiteralSlot = 0;
        ZC_IF_SOME(value, conditionTypeIndex) { conditionTypeSlot = value; }
        ZC_IF_SOME(value, returnTypeIndex) { returnTypeSlot = value; }
        ZC_IF_SOME(value, returnLiteralIndex) { returnLiteralSlot = value; }
        const auto conditionType = facts.nodeTypes().entries()[conditionTypeSlot].value;
        const auto returnType = facts.nodeTypes().entries()[returnTypeSlot].value;
        const auto& returnLiteral = facts.literals().entries()[returnLiteralSlot].value;
        if (returnType != function.resultType) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto conditionParameter = resolvedCallableParameter(bound.bindings(), source.loopCondition);
        if (conditionParameter == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        identity::CallableParameterId conditionHandle;
        ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
        auto conditionAuthority = registries.callableParameter(conditionHandle);
        if (conditionAuthority == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        zc::Maybe<const HirLoopStatement&> loopValue;
        for (const auto& candidateLoop : candidate.impl->loops) {
          if (candidateLoop.node != hirId(expectedFunction + 4)) continue;
          if (loopValue != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          loopValue = candidateLoop;
        }
        zc::Maybe<const HirParameterReferenceExpression&> conditionReference;
        for (const auto& reference : candidate.impl->parameterReferences) {
          if (reference.node != hirId(expectedFunction + 2)) continue;
          if (conditionReference != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          conditionReference = reference;
        }
        zc::Maybe<const HirScalarLiteralExpression&> returnValue;
        for (const auto& expression : candidate.impl->expressions) {
          if (expression.node != hirId(expectedFunction + 3)) continue;
          if (returnValue != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          returnValue = expression;
        }
        if (loopValue == zc::none || conditionReference == zc::none || returnValue == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        ZC_IF_SOME(loop, loopValue) {
          ZC_IF_SOME(conditionRef, conditionReference) {
            ZC_IF_SOME(returnLit, returnValue) {
              ZC_IF_SOME(authority, conditionAuthority) {
                if (function.node != hirId(expectedFunction) ||
                    block.node != hirId(expectedFunction + 1) || function.body != block.node ||
                    block.statements.size() != 2 || block.statements[0] != loop.node ||
                    block.statements[1] != returnStatement.node ||
                    returnStatement.node != hirId(expectedFunction + 5) ||
                    returnStatement.value != returnLit.node ||
                    returnStatement.resultType != function.resultType ||
                    loop.condition != conditionRef.node || loop.type != conditionType ||
                    loop.category != HirValueCategory::Place ||
                    conditionRef.parameter != authority.key() ||
                    conditionRef.type != conditionType ||
                    conditionRef.category != HirValueCategory::Place ||
                    returnLit.type != returnType || returnLit.category != HirValueCategory::Value ||
                    !sameConstant(returnLit.value, returnLiteral.literal, module, registries,
                                  semanticTypes) ||
                    !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) ||
                    !sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) ||
                    !sameSpan(loop.sourceSpan, ZC_ASSERT_NONNULL(loopSpan)) ||
                    !sameSpan(conditionRef.sourceSpan, ZC_ASSERT_NONNULL(conditionSpan)) ||
                    !sameSpan(returnLit.sourceSpan, ZC_ASSERT_NONNULL(valueSpan)) ||
                    !typeExists(function.resultType, semanticTypes) ||
                    !typeExists(conditionType, semanticTypes)) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
              }
            }
          }
        }
        nextFunction += 6;
        continue;
      }
    }
    // Loop-body composite shape: a leading `mut` local, an admitted `while` whose
    // body writes that local, and a trailing local return. The body block is
    // `[local, loop, return]`; the loop statement carries the write node ids, and
    // each write reuses the mut-local write validation. Fixed-id layout relative
    // to the function id F:
    //   F+0 function, F+1 body block, F+2 local, F+3 initializer literal,
    //   then per write i: F+(4 + i*2) write, F+(5 + i*2) write value; then
    //   returnNode, returnValueNode (the local reference), and finally the two
    //   loop nodes F+(condition), F+(loop). Any binary write's two operand nodes
    //   trail after the loop nodes, matching the materializer's allocation order.
    {
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      bool isLoopBodyShape = false;
      if (sourceDefinitionIndex != zc::none) {
        size_t definitionSlot = 0;
        ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
        const auto& sourceDefinition = definitions.definitions()[definitionSlot];
        const auto& tree = bound.tree();
        if (tree.contains(sourceDefinition.node)) {
          auto probe = functionReturnShape(tree, tree.node(sourceDefinition.node));
          ZC_IF_SOME(value, probe) { isLoopBodyShape = value.isLoopBody; }
        }
      }
      if (isLoopBodyShape) {
        size_t definitionSlot = 0;
        ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
        const auto& sourceDefinition = definitions.definitions()[definitionSlot];
        const auto& tree = bound.tree();
        if (!hasExecutableBody(sourceDefinition, definitions) ||
            !definitionBelongsToModule(sourceDefinition, definitions) ||
            sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
            !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
            !tree.contains(sourceDefinition.node)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto sourceShapeMaybe = functionReturnShape(tree, tree.node(sourceDefinition.node));
        if (sourceShapeMaybe == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        FunctionReturnShape source = zc::mv(sourceShapeMaybe).orDefault(FunctionReturnShape{});
        const size_t writeCount = source.localWrites.size;
        if (writeCount == 0) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Signature and parameter validation, identical to the empty-body loop
        // and mut-local shapes.
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
        if (signaturePosition == zc::none || rootPosition == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t signatureSlot = 0;
        size_t rootSlot = 0;
        ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
        ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
        const auto& signature = signatures.definitions[signatureSlot];
        const auto& root = signatures.roots[rootSlot];
        auto expectedVisibility = visibility(root.visibility);
        if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
            !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>() ||
            expectedVisibility == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        auto expectedLinkage = linkage(callable);
        if (expectedLinkage == zc::none || signature.definition != function.definition ||
            signature.definitionKind != identity::DefinitionKind::Function ||
            root.canonicalDefinition != function.definition || root.sourceModule != module ||
            callable.receiver != zc::none || callable.raises != zc::none ||
            callable.success != function.resultType ||
            !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
            !sameSpan(function.sourceSpan, sourceDefinition.source) ||
            !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
            function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
            function.parameters.size() != callable.parameters.size()) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        for (size_t parameterIndex = 0; parameterIndex < function.parameters.size();
             ++parameterIndex) {
          const auto& parameter = function.parameters[parameterIndex];
          const auto& sourceParameter = callable.parameters[parameterIndex];
          if (parameter.key != sourceParameter.parameter ||
              parameter.type != sourceParameter.type || sourceParameter.hasDefault ||
              !typeExists(parameter.type, semanticTypes)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        // Locate the materialized nodes at their fixed ids.
        const auto localNode = hirId(expectedFunction + 2);
        const auto initializerNode = hirId(expectedFunction + 3);
        const auto returnNode = hirId(expectedFunction + 4 + static_cast<uint32_t>(writeCount) * 2);
        const auto valueNode = hirId(returnNode.ordinal() + 1);
        const auto conditionNode = hirId(valueNode.ordinal() + 1);
        const auto loopNode = hirId(conditionNode.ordinal() + 1);
        zc::Maybe<const HirLocalBinding&> localBinding;
        for (const auto& local : candidate.impl->locals) {
          if (local.node != localNode) continue;
          if (localBinding != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          localBinding = local;
        }
        zc::Maybe<const HirScalarLiteralExpression&> initializerLiteral;
        for (const auto& expression : candidate.impl->expressions) {
          if (expression.node != initializerNode) continue;
          if (initializerLiteral != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          initializerLiteral = expression;
        }
        zc::Maybe<const HirLoopStatement&> loopValue;
        for (const auto& candidateLoop : candidate.impl->loops) {
          if (candidateLoop.node != loopNode) continue;
          if (loopValue != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          loopValue = candidateLoop;
        }
        zc::Maybe<const HirParameterReferenceExpression&> conditionReference;
        for (const auto& reference : candidate.impl->parameterReferences) {
          if (reference.node != conditionNode) continue;
          if (conditionReference != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          conditionReference = reference;
        }
        zc::Maybe<const HirLocalReferenceExpression&> returnReference;
        for (const auto& reference : candidate.impl->localReferences) {
          if (reference.node != valueNode) continue;
          if (returnReference != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          returnReference = reference;
        }
        if (localBinding == zc::none || initializerLiteral == zc::none || loopValue == zc::none ||
            conditionReference == zc::none || returnReference == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        // Loop condition parameter authority.
        auto conditionParameter = resolvedCallableParameter(bound.bindings(), source.loopCondition);
        auto conditionTypeIndex = factIndex(facts.nodeTypes(), source.loopCondition);
        auto conditionSpan = bound.parsedModule().spanFor(tree.node(source.loopCondition).range);
        auto loopSpan = bound.parsedModule().spanFor(tree.node(source.loopStatement).range);
        auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
        auto returnValueSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
        zc::Maybe<identity::SourceSpan> sourceBreakSpan;
        zc::Maybe<identity::SourceSpan> sourceContinueSpan;
        if (source.loopBodyBreak) {
          sourceBreakSpan = bound.parsedModule().spanFor(tree.node(source.loopBodyBreak).range);
        }
        if (source.loopBodyContinue) {
          sourceContinueSpan =
              bound.parsedModule().spanFor(tree.node(source.loopBodyContinue).range);
        }
        if (conditionParameter == zc::none || conditionTypeIndex == zc::none ||
            conditionSpan == zc::none || loopSpan == zc::none || bodySpan == zc::none ||
            returnSpan == zc::none || returnValueSpan == zc::none ||
            (source.loopBodyBreak && sourceBreakSpan == zc::none) ||
            (source.loopBodyContinue && sourceContinueSpan == zc::none)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        identity::CallableParameterId conditionHandle;
        ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
        auto conditionAuthority = registries.callableParameter(conditionHandle);
        if (conditionAuthority == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t conditionTypeSlot = 0;
        ZC_IF_SOME(value, conditionTypeIndex) { conditionTypeSlot = value; }
        const auto conditionType = facts.nodeTypes().entries()[conditionTypeSlot].value;
        // Owner-local binding for the declared local, matched to the return.
        auto ownerBinding = resolvedOwnerLocal(bound.bindings(), source.localReference);
        auto returnBinding = resolvedOwnerLocal(bound.bindings(), source.value);
        if (ownerBinding == zc::none || returnBinding == zc::none ||
            ownerBinding != returnBinding ||
            !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(ownerBinding), source.localPattern,
                               tree)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        // Initializer literal fact.
        // Structural checks: block topology, loop node, condition, return.
        bool structureOk =
            function.node == hirId(expectedFunction) && block.node == hirId(expectedFunction + 1) &&
            function.body == block.node && block.statements.size() == 3 &&
            block.statements[0] == localNode && block.statements[1] == loopNode &&
            block.statements[2] == returnStatement.node && returnStatement.node == returnNode &&
            returnStatement.value == valueNode &&
            returnStatement.resultType == function.resultType &&
            ZC_ASSERT_NONNULL(localBinding).node == localNode &&
            ZC_ASSERT_NONNULL(localBinding).initializer == initializerNode &&
            ZC_ASSERT_NONNULL(localBinding).type == function.resultType &&
            ZC_ASSERT_NONNULL(initializerLiteral).node == initializerNode &&
            ZC_ASSERT_NONNULL(initializerLiteral).type == function.resultType &&
            ZC_ASSERT_NONNULL(initializerLiteral).category == HirValueCategory::Value &&
            ZC_ASSERT_NONNULL(loopValue).node == loopNode &&
            ZC_ASSERT_NONNULL(loopValue).condition == conditionNode &&
            ZC_ASSERT_NONNULL(loopValue).type == conditionType &&
            ZC_ASSERT_NONNULL(loopValue).category == HirValueCategory::Place &&
            ZC_ASSERT_NONNULL(loopValue).body.size() == writeCount &&
            (ZC_ASSERT_NONNULL(loopValue).breakSpan != zc::none) ==
                static_cast<bool>(source.loopBodyBreak) &&
            (ZC_ASSERT_NONNULL(loopValue).continueSpan != zc::none) ==
                static_cast<bool>(source.loopBodyContinue) &&
            ZC_ASSERT_NONNULL(conditionReference).type == conditionType &&
            ZC_ASSERT_NONNULL(conditionReference).category == HirValueCategory::Place &&
            ZC_ASSERT_NONNULL(returnReference).local == ZC_ASSERT_NONNULL(localBinding).local &&
            ZC_ASSERT_NONNULL(returnReference).type == function.resultType &&
            ZC_ASSERT_NONNULL(returnReference).category == HirValueCategory::Place &&
            typeExists(function.resultType, semanticTypes) &&
            typeExists(conditionType, semanticTypes);
        ZC_IF_SOME(entry, conditionAuthority) {
          structureOk =
              structureOk && ZC_ASSERT_NONNULL(conditionReference).parameter == entry.key();
        }
        bool spansOk =
            sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) &&
            sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) &&
            sameSpan(ZC_ASSERT_NONNULL(loopValue).sourceSpan, ZC_ASSERT_NONNULL(loopSpan)) &&
            sameSpan(ZC_ASSERT_NONNULL(conditionReference).sourceSpan,
                     ZC_ASSERT_NONNULL(conditionSpan)) &&
            sameSpan(ZC_ASSERT_NONNULL(returnReference).sourceSpan,
                     ZC_ASSERT_NONNULL(returnValueSpan));
        // Break and continue spans are optional; verify them only when present.
        ZC_IF_SOME(breakSpan, ZC_ASSERT_NONNULL(loopValue).breakSpan) {
          spansOk = spansOk && sameSpan(breakSpan, ZC_ASSERT_NONNULL(sourceBreakSpan));
        }
        ZC_IF_SOME(continueSpan, ZC_ASSERT_NONNULL(loopValue).continueSpan) {
          spansOk = spansOk && sameSpan(continueSpan, ZC_ASSERT_NONNULL(sourceContinueSpan));
        }
        if (!structureOk || !spansOk) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Each loop-body write and its value, validated against checked facts. The
        // write is an overwrite of the initialized local; its value is a scalar
        // literal, a parameter reference, or a primitive binary of the same shape
        // as the flat mut-local write path.
        for (size_t writeIndex = 0; writeIndex < writeCount; ++writeIndex) {
          const auto expectedWrite =
              hirId(expectedFunction + 4 + static_cast<uint32_t>(writeIndex) * 2);
          const auto expectedValue = hirId(expectedWrite.ordinal() + 1);
          if (ZC_ASSERT_NONNULL(loopValue).body[writeIndex] != expectedWrite) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          auto sourceStatement = statementItem(tree, tree.list(source.localWrites)[writeIndex]);
          if (sourceStatement == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          ast::NodeId sourceStatementNode;
          ZC_IF_SOME(value, sourceStatement) { sourceStatementNode = value; }
          const ast::NodeId sourceWrite(
              tree.node(sourceStatementNode)
                  .payload.words[ast::kExpressionStatementExpressionWord]);
          const ast::NodeId sourceTarget(
              tree.node(sourceWrite).payload.words[ast::kAssignmentExprLhsWord]);
          const ast::NodeId sourceWriteValue(
              tree.node(sourceWrite).payload.words[ast::kAssignmentExprRhsWord]);
          auto sourceWriteSpan = bound.parsedModule().spanFor(tree.node(sourceWrite).range);
          auto sourceValueSpan = bound.parsedModule().spanFor(tree.node(sourceWriteValue).range);
          auto assignmentType = factIndex(facts.nodeTypes(), sourceWrite);
          auto targetType = factIndex(facts.nodeTypes(), sourceTarget);
          auto valueType = factIndex(facts.nodeTypes(), sourceWriteValue);
          const bool sourceReferenceValue =
              tree.contains(sourceWriteValue) &&
              tree.node(sourceWriteValue).kind == ast::SyntaxKind::IdentExpr;
          const bool sourceBinaryValue =
              tree.contains(sourceWriteValue) &&
              tree.node(sourceWriteValue).kind == ast::SyntaxKind::BinaryExpr;
          auto valueLiteral = (sourceReferenceValue || sourceBinaryValue)
                                  ? zc::none
                                  : factIndex(facts.literals(), sourceWriteValue);
          auto targetBinding = resolvedOwnerLocal(bound.bindings(), sourceTarget);
          if (sourceWriteSpan == zc::none || sourceValueSpan == zc::none ||
              assignmentType == zc::none || targetType == zc::none || valueType == zc::none ||
              (!sourceReferenceValue && !sourceBinaryValue && valueLiteral == zc::none) ||
              targetBinding == zc::none || targetBinding != ZC_ASSERT_NONNULL(ownerBinding)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          size_t assignmentSlot = 0;
          size_t targetSlot = 0;
          size_t valueSlot = 0;
          size_t literalSlot = 0;
          ZC_IF_SOME(value, assignmentType) { assignmentSlot = value; }
          ZC_IF_SOME(value, targetType) { targetSlot = value; }
          ZC_IF_SOME(value, valueType) { valueSlot = value; }
          ZC_IF_SOME(value, valueLiteral) { literalSlot = value; }
          zc::Maybe<const HirLocalWriteStatement&> write;
          zc::Maybe<const HirScalarLiteralExpression&> literal;
          zc::Maybe<const HirParameterReferenceExpression&> parameter;
          zc::Maybe<const HirPrimitiveBinaryExpression&> binary;
          for (const auto& candidateWrite : candidate.impl->localWrites) {
            if (candidateWrite.node != expectedWrite) continue;
            if (write != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            write = candidateWrite;
          }
          for (const auto& expression : candidate.impl->expressions) {
            if (expression.node != expectedValue) continue;
            if (literal != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            literal = expression;
          }
          for (const auto& reference : candidate.impl->parameterReferences) {
            if (reference.node != expectedValue) continue;
            if (parameter != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            parameter = reference;
          }
          for (const auto& operation : candidate.impl->primitiveBinaryOperations) {
            if (operation.node != expectedValue) continue;
            if (binary != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            binary = operation;
          }
          const bool binaryOk = sourceBinaryValue ? (binary != zc::none && literal == zc::none &&
                                                     parameter == zc::none)
                                                  : binary == zc::none;
          if (write == zc::none || !binaryOk ||
              (sourceReferenceValue ? (parameter == zc::none || literal != zc::none)
               : sourceBinaryValue  ? false
                                    : (literal == zc::none || parameter != zc::none))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          ZC_IF_SOME(writeValue, write) {
            const auto& assignmentFact = facts.nodeTypes().entries()[assignmentSlot].value;
            const auto& targetFact = facts.nodeTypes().entries()[targetSlot].value;
            const auto& valueFact = facts.nodeTypes().entries()[valueSlot].value;
            if (writeValue.local != ZC_ASSERT_NONNULL(localBinding).local ||
                writeValue.field != zc::none || writeValue.type != function.resultType ||
                writeValue.value != expectedValue || assignmentFact != writeValue.type ||
                targetFact != writeValue.type || valueFact != writeValue.type ||
                writeValue.kind != HirLocalWriteKind::Overwrite ||
                !sameSpan(writeValue.sourceSpan, ZC_ASSERT_NONNULL(sourceWriteSpan)) ||
                !sameSpan(writeValue.valueSpan, ZC_ASSERT_NONNULL(sourceValueSpan))) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            if (sourceReferenceValue) {
              ZC_IF_SOME(parameterValue, parameter) {
                if (parameterValue.type != writeValue.type ||
                    parameterValue.category != HirValueCategory::Place ||
                    !sameSpan(parameterValue.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan))) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
              }
            } else if (sourceBinaryValue) {
              const ast::NodeId binaryLeftNode(
                  tree.node(sourceWriteValue).payload.words[ast::kBinaryExprLhsWord]);
              const ast::NodeId binaryRightNode(
                  tree.node(sourceWriteValue).payload.words[ast::kBinaryExprRhsWord]);
              // The two operand nodes trail after the loop nodes, then each earlier
              // binary write consumes two ids, matching the materializer.
              uint32_t trailingBase = loopNode.ordinal() + 1;
              uint32_t priorBinaryWrites = 0;
              for (size_t earlier = 0; earlier < writeIndex; ++earlier) {
                auto earlierStatement = statementItem(tree, tree.list(source.localWrites)[earlier]);
                ZC_IF_SOME(earlierValue, earlierStatement) {
                  const ast::NodeId earlierWrite(
                      tree.node(earlierValue)
                          .payload.words[ast::kExpressionStatementExpressionWord]);
                  const ast::NodeId earlierRhs(
                      tree.node(earlierWrite).payload.words[ast::kAssignmentExprRhsWord]);
                  if (tree.contains(earlierRhs) &&
                      tree.node(earlierRhs).kind == ast::SyntaxKind::BinaryExpr) {
                    ++priorBinaryWrites;
                  }
                }
              }
              const auto leftOperandId = hirId(trailingBase + priorBinaryWrites * 2);
              const auto rightOperandId = hirId(trailingBase + priorBinaryWrites * 2 + 1);
              auto callIndex = factIndex(facts.calls(), sourceWriteValue);
              if (callIndex == zc::none) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::MissingRequiredFact, module,
                                                    registries, index + 1);
              }
              size_t callSlot = 0;
              ZC_IF_SOME(value, callIndex) { callSlot = value; }
              const auto& callFact = facts.calls().entries()[callSlot].value;
              const auto& call = callFact.invocation;
              const auto& selected = call.selected.variant();
              if (!selected.is<checker::checked::PrimitiveCallable>()) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::InvalidFact, module,
                                                    registries, index + 1);
              }
              const auto operation = selected.get<checker::checked::PrimitiveCallable>().operation;
              const bool comparison = isScalarComparisonOperation(operation);
              const bool arithmetic = isScalarArithmeticOperation(operation);
              const auto binaryOperandType =
                  call.arguments.size() == 2 ? call.arguments[0].sourceType : writeValue.type;
              const bool operationSupported =
                  comparison || (arithmetic && writeValue.type == binaryOperandType);
              ZC_IF_SOME(binaryValue, binary) {
                if (!operationSupported || binaryValue.node != expectedValue ||
                    binaryValue.left != leftOperandId || binaryValue.right != rightOperandId ||
                    binaryValue.type != writeValue.type ||
                    binaryValue.operandType != binaryOperandType ||
                    binaryValue.category != HirValueCategory::Value ||
                    binaryValue.operation != operation ||
                    !sameSpan(binaryValue.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan)) ||
                    callFact.node != sourceWriteValue || call.calleeType != binaryOperandType ||
                    call.receiver != zc::none || call.receiverMode != zc::none ||
                    call.receiverAdjustment != zc::none || call.arguments.size() != 2 ||
                    call.arguments[0].sourceNode != binaryLeftNode ||
                    call.arguments[0].sourceType != binaryOperandType ||
                    call.arguments[1].sourceNode != binaryRightNode ||
                    call.arguments[1].sourceType != binaryOperandType ||
                    call.successType != writeValue.type || call.resultType != writeValue.type ||
                    call.substitutions != zc::none || call.witnesses != zc::none ||
                    call.raises != zc::none) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
                auto verifyWriteOperand = [&](HirNodeId operandId,
                                              ast::NodeId operandNode) -> bool {
                  auto operandSpan = bound.parsedModule().spanFor(tree.node(operandNode).range);
                  if (operandSpan == zc::none) return false;
                  if (isScalarLiteral(tree.node(operandNode).kind)) {
                    zc::Maybe<const HirScalarLiteralExpression&> operandLiteral;
                    for (const auto& expression : candidate.impl->expressions) {
                      if (expression.node != operandId) continue;
                      if (operandLiteral != zc::none) return false;
                      operandLiteral = expression;
                    }
                    auto operandLiteralIndex = factIndex(facts.literals(), operandNode);
                    if (operandLiteral == zc::none || operandLiteralIndex == zc::none) return false;
                    size_t operandLiteralSlot = 0;
                    ZC_IF_SOME(value, operandLiteralIndex) { operandLiteralSlot = value; }
                    const auto& operandLiteralFact =
                        facts.literals().entries()[operandLiteralSlot].value;
                    const auto& literalValue = ZC_ASSERT_NONNULL(operandLiteral);
                    return literalValue.type == binaryOperandType &&
                           literalValue.category == HirValueCategory::Value &&
                           operandLiteralFact.type == binaryOperandType &&
                           sameConstant(literalValue.value, operandLiteralFact.literal, module,
                                        registries, semanticTypes) &&
                           sameSpan(literalValue.sourceSpan, ZC_ASSERT_NONNULL(operandSpan));
                  }
                  // A non-literal operand that names the written user local
                  // (`x = x + 1`) materializes a localReference, not a
                  // parameterReference.
                  auto ownerBinding = resolvedOwnerLocal(bound.bindings(), operandNode);
                  if (ownerBinding != zc::none) {
                    zc::Maybe<const HirLocalReferenceExpression&> operandLocal;
                    for (const auto& reference : candidate.impl->localReferences) {
                      if (reference.node != operandId) continue;
                      if (operandLocal != zc::none) return false;
                      operandLocal = reference;
                    }
                    if (operandLocal == zc::none) return false;
                    const auto& localValue = ZC_ASSERT_NONNULL(operandLocal);
                    return localValue.local == ZC_ASSERT_NONNULL(localBinding).local &&
                           localValue.type == binaryOperandType &&
                           localValue.category == HirValueCategory::Place &&
                           sameSpan(localValue.sourceSpan, ZC_ASSERT_NONNULL(operandSpan));
                  }
                  zc::Maybe<const HirParameterReferenceExpression&> operandReference;
                  for (const auto& reference : candidate.impl->parameterReferences) {
                    if (reference.node != operandId) continue;
                    if (operandReference != zc::none) return false;
                    operandReference = reference;
                  }
                  auto parameterHandle = resolvedCallableParameter(bound.bindings(), operandNode);
                  if (operandReference == zc::none || parameterHandle == zc::none) return false;
                  bool parameterMatches = false;
                  ZC_IF_SOME(handle, parameterHandle) {
                    auto authority = registries.callableParameter(handle);
                    ZC_IF_SOME(entry, authority) {
                      parameterMatches =
                          ZC_ASSERT_NONNULL(operandReference).parameter == entry.key();
                    }
                  }
                  const auto& referenceValue = ZC_ASSERT_NONNULL(operandReference);
                  return parameterMatches && referenceValue.type == binaryOperandType &&
                         referenceValue.category == HirValueCategory::Place &&
                         sameSpan(referenceValue.sourceSpan, ZC_ASSERT_NONNULL(operandSpan));
                };
                if (!verifyWriteOperand(leftOperandId, binaryLeftNode) ||
                    !verifyWriteOperand(rightOperandId, binaryRightNode)) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
              }
            } else {
              const auto& literalFact = facts.literals().entries()[literalSlot].value;
              ZC_IF_SOME(literalValue, literal) {
                if (literalValue.type != writeValue.type ||
                    literalValue.category != HirValueCategory::Value ||
                    literalFact.type != writeValue.type ||
                    !sameConstant(literalValue.value, literalFact.literal, module, registries,
                                  semanticTypes) ||
                    !sameSpan(literalValue.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan)) ||
                    !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan))) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
              }
            }
          }
        }
        // Total node span: F+0..F+3 (function, block, local, initializer),
        // writeCount * 2 write nodes, return + value + condition + loop (4), then
        // each binary write's two operand nodes. A compound assignment
        // (`x += 1`) desugars to a binary write, so its two operand nodes are
        // counted here even though the source RHS is not a BinaryExpr.
        uint32_t binaryWriteNodes = 0;
        for (size_t writeIndex = 0; writeIndex < writeCount; ++writeIndex) {
          auto sourceStatement = statementItem(tree, tree.list(source.localWrites)[writeIndex]);
          ZC_IF_SOME(value, sourceStatement) {
            const ast::NodeId writeNode(
                tree.node(value).payload.words[ast::kExpressionStatementExpressionWord]);
            if (tree.node(writeNode).kind == ast::SyntaxKind::PostfixExpression) {
              binaryWriteNodes += 2;
              continue;
            }
            if (tree.node(writeNode).kind == ast::SyntaxKind::AssignmentExpr &&
                isCompoundAssignment(static_cast<ast::AssignmentOperatorKind>(
                    tree.node(writeNode).payload.words[ast::kAssignmentExprOpWord]))) {
              binaryWriteNodes += 2;
              continue;
            }
            const ast::NodeId rhs(tree.node(writeNode).payload.words[ast::kAssignmentExprRhsWord]);
            if (tree.contains(rhs) && tree.node(rhs).kind == ast::SyntaxKind::BinaryExpr) {
              binaryWriteNodes += 2;
            }
          }
        }
        nextFunction += 4 + static_cast<uint32_t>(writeCount) * 2 + 4 + binaryWriteNodes;
        continue;
      }
    }
    // C-style for-loop return: `for (let id = <lit>; <ident> <cmp> <lit>;
    // <ident> = <binary>) {}` followed by a scalar return. Fixed-id layout
    // relative to the function id F:
    //   F+0 function, F+1 body block, F+2 local, F+3 init literal,
    //   F+4 condition left ref, F+5 condition right literal,
    //   F+6 condition binary, F+7 update left ref, F+8 update right literal,
    //   F+9 update binary, F+10 update write, F+11 loop, F+12 return,
    //   F+13 return value literal.
    {
      bool isForLoopShape = false;
      ZC_IF_SOME(source, sourceShapeMaybe) { isForLoopShape = source.isForLoop; }
      if (isForLoopShape) {
        const FunctionReturnShape& source = ZC_ASSERT_NONNULL(sourceShapeMaybe);
        auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
        if (sourceDefinitionIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t definitionSlot = 0;
        ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
        const auto& sourceDefinition = definitions.definitions()[definitionSlot];
        const auto& tree = bound.tree();
        if (!hasExecutableBody(sourceDefinition, definitions) ||
            !definitionBelongsToModule(sourceDefinition, definitions) ||
            sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
            !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
            !tree.contains(sourceDefinition.node)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
        if (signaturePosition == zc::none || rootPosition == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t signatureSlot = 0;
        size_t rootSlot = 0;
        ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
        ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
        const auto& signature = signatures.definitions[signatureSlot];
        const auto& root = signatures.roots[rootSlot];
        auto expectedVisibility = visibility(root.visibility);
        if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
            !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>() ||
            expectedVisibility == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        auto expectedLinkage = linkage(callable);
        if (expectedLinkage == zc::none || signature.definition != function.definition ||
            signature.definitionKind != identity::DefinitionKind::Function ||
            root.canonicalDefinition != function.definition || root.sourceModule != module ||
            callable.receiver != zc::none || callable.raises != zc::none ||
            callable.success != function.resultType ||
            !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
            !sameSpan(function.sourceSpan, sourceDefinition.source) ||
            !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
            function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
            function.parameters.size() != callable.parameters.size()) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        for (size_t parameterIndex = 0; parameterIndex < function.parameters.size();
             ++parameterIndex) {
          const auto& parameter = function.parameters[parameterIndex];
          const auto& sourceParameter = callable.parameters[parameterIndex];
          if (parameter.key != sourceParameter.parameter ||
              parameter.type != sourceParameter.type || sourceParameter.hasDefault ||
              !typeExists(parameter.type, semanticTypes)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        // Resolve the init let declarator, pattern, and initializer.
        const auto& letNode = tree.node(source.forLoopInit);
        const ast::NodeId declarations(letNode.payload.words[ast::kLetStmtDeclarationsWord]);
        const ast::NodeList declarators{
            tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
            tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
        if (!tree.contains(declarations) ||
            tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList ||
            !tree.contains(declarators) || declarators.size != 1) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto declarator = tree.list(declarators)[0];
        const ast::NodeId pattern(
            tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
        const ast::NodeId initializer(
            tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
        if (!tree.contains(pattern) ||
            tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
            !tree.contains(initializer) || !isScalarLiteral(tree.node(initializer).kind)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Resolve the condition binary operands.
        const auto& condNode = tree.node(source.forLoopCond);
        const ast::NodeId condLhs(condNode.payload.words[ast::kBinaryExprLhsWord]);
        const ast::NodeId condRhs(condNode.payload.words[ast::kBinaryExprRhsWord]);
        if (!tree.contains(condLhs) || tree.node(condLhs).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(condRhs) || !isScalarLiteral(tree.node(condRhs).kind)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Resolve the update assignment and its binary value operands.
        const auto& updateNode = tree.node(source.forLoopUpdate);
        const ast::NodeId updateTarget(updateNode.payload.words[ast::kAssignmentExprLhsWord]);
        const ast::NodeId updateValueNode(updateNode.payload.words[ast::kAssignmentExprRhsWord]);
        if (!tree.contains(updateTarget) ||
            tree.node(updateTarget).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(updateValueNode) ||
            tree.node(updateValueNode).kind != ast::SyntaxKind::BinaryExpr) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& updateValueBin = tree.node(updateValueNode);
        const ast::NodeId updateLhs(updateValueBin.payload.words[ast::kBinaryExprLhsWord]);
        const ast::NodeId updateRhs(updateValueBin.payload.words[ast::kBinaryExprRhsWord]);
        if (!tree.contains(updateLhs) || tree.node(updateLhs).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(updateRhs) || !isScalarLiteral(tree.node(updateRhs).kind)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Spans for every source node that materializes a HIR node.
        auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
        auto patternSpan = bound.parsedModule().spanFor(tree.node(pattern).range);
        auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
        auto condSpan = bound.parsedModule().spanFor(condNode.range);
        auto condLhsSpan = bound.parsedModule().spanFor(tree.node(condLhs).range);
        auto condRhsSpan = bound.parsedModule().spanFor(tree.node(condRhs).range);
        auto updateSpan = bound.parsedModule().spanFor(updateNode.range);
        auto updateValueSpan = bound.parsedModule().spanFor(updateValueBin.range);
        auto updateLhsSpan = bound.parsedModule().spanFor(tree.node(updateLhs).range);
        auto updateRhsSpan = bound.parsedModule().spanFor(tree.node(updateRhs).range);
        auto loopSpan = bound.parsedModule().spanFor(tree.node(source.forLoopStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
        zc::Maybe<identity::SourceSpan> sourceBreakSpan;
        zc::Maybe<identity::SourceSpan> sourceContinueSpan;
        if (source.forLoopBodyBreak) {
          sourceBreakSpan = bound.parsedModule().spanFor(tree.node(source.forLoopBodyBreak).range);
        }
        if (source.forLoopBodyContinue) {
          sourceContinueSpan =
              bound.parsedModule().spanFor(tree.node(source.forLoopBodyContinue).range);
        }
        if (bodySpan == zc::none || returnSpan == zc::none || patternSpan == zc::none ||
            initializerSpan == zc::none || condSpan == zc::none || condLhsSpan == zc::none ||
            condRhsSpan == zc::none || updateSpan == zc::none || updateValueSpan == zc::none ||
            updateLhsSpan == zc::none || updateRhsSpan == zc::none || loopSpan == zc::none ||
            valueSpan == zc::none || (source.forLoopBodyBreak && sourceBreakSpan == zc::none) ||
            (source.forLoopBodyContinue && sourceContinueSpan == zc::none)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        // Node-type and literal facts for every typed source node.
        auto initTypeIndex = factIndex(facts.nodeTypes(), initializer);
        auto condResultTypeIndex = factIndex(facts.nodeTypes(), source.forLoopCond);
        auto condLhsTypeIndex = factIndex(facts.nodeTypes(), condLhs);
        auto condRhsTypeIndex = factIndex(facts.nodeTypes(), condRhs);
        auto updateValueTypeIndex = factIndex(facts.nodeTypes(), updateValueNode);
        auto updateLhsTypeIndex = factIndex(facts.nodeTypes(), updateLhs);
        auto updateRhsTypeIndex = factIndex(facts.nodeTypes(), updateRhs);
        auto returnTypeIndex = factIndex(facts.nodeTypes(), source.value);
        auto initLiteralIndex = factIndex(facts.literals(), initializer);
        auto condRhsLiteralIndex = factIndex(facts.literals(), condRhs);
        auto updateRhsLiteralIndex = factIndex(facts.literals(), updateRhs);
        auto returnLiteralIndex = factIndex(facts.literals(), source.value);
        auto condCallIndex = factIndex(facts.calls(), source.forLoopCond);
        auto updateCallIndex = factIndex(facts.calls(), updateValueNode);
        if (initTypeIndex == zc::none || condResultTypeIndex == zc::none ||
            condLhsTypeIndex == zc::none || condRhsTypeIndex == zc::none ||
            updateValueTypeIndex == zc::none || updateLhsTypeIndex == zc::none ||
            updateRhsTypeIndex == zc::none || returnTypeIndex == zc::none ||
            initLiteralIndex == zc::none || condRhsLiteralIndex == zc::none ||
            updateRhsLiteralIndex == zc::none || returnLiteralIndex == zc::none ||
            condCallIndex == zc::none || updateCallIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t initTypeSlot = 0;
        size_t condResultTypeSlot = 0;
        size_t condLhsTypeSlot = 0;
        size_t condRhsTypeSlot = 0;
        size_t updateValueTypeSlot = 0;
        size_t updateLhsTypeSlot = 0;
        size_t updateRhsTypeSlot = 0;
        size_t returnTypeSlot = 0;
        size_t initLiteralSlot = 0;
        size_t condRhsLiteralSlot = 0;
        size_t updateRhsLiteralSlot = 0;
        size_t returnLiteralSlot = 0;
        size_t condCallSlot = 0;
        size_t updateCallSlot = 0;
        ZC_IF_SOME(value, initTypeIndex) { initTypeSlot = value; }
        ZC_IF_SOME(value, condResultTypeIndex) { condResultTypeSlot = value; }
        ZC_IF_SOME(value, condLhsTypeIndex) { condLhsTypeSlot = value; }
        ZC_IF_SOME(value, condRhsTypeIndex) { condRhsTypeSlot = value; }
        ZC_IF_SOME(value, updateValueTypeIndex) { updateValueTypeSlot = value; }
        ZC_IF_SOME(value, updateLhsTypeIndex) { updateLhsTypeSlot = value; }
        ZC_IF_SOME(value, updateRhsTypeIndex) { updateRhsTypeSlot = value; }
        ZC_IF_SOME(value, returnTypeIndex) { returnTypeSlot = value; }
        ZC_IF_SOME(value, initLiteralIndex) { initLiteralSlot = value; }
        ZC_IF_SOME(value, condRhsLiteralIndex) { condRhsLiteralSlot = value; }
        ZC_IF_SOME(value, updateRhsLiteralIndex) { updateRhsLiteralSlot = value; }
        ZC_IF_SOME(value, returnLiteralIndex) { returnLiteralSlot = value; }
        ZC_IF_SOME(value, condCallIndex) { condCallSlot = value; }
        ZC_IF_SOME(value, updateCallIndex) { updateCallSlot = value; }
        const auto initType = facts.nodeTypes().entries()[initTypeSlot].value;
        const auto condResultType = facts.nodeTypes().entries()[condResultTypeSlot].value;
        const auto condLhsType = facts.nodeTypes().entries()[condLhsTypeSlot].value;
        const auto condRhsType = facts.nodeTypes().entries()[condRhsTypeSlot].value;
        const auto updateValueType = facts.nodeTypes().entries()[updateValueTypeSlot].value;
        const auto updateLhsType = facts.nodeTypes().entries()[updateLhsTypeSlot].value;
        const auto updateRhsType = facts.nodeTypes().entries()[updateRhsTypeSlot].value;
        const auto returnType = facts.nodeTypes().entries()[returnTypeSlot].value;
        const auto& initLiteralFact = facts.literals().entries()[initLiteralSlot].value;
        const auto& condRhsLiteralFact = facts.literals().entries()[condRhsLiteralSlot].value;
        const auto& updateRhsLiteralFact = facts.literals().entries()[updateRhsLiteralSlot].value;
        const auto& returnLiteralFact = facts.literals().entries()[returnLiteralSlot].value;
        const auto& condCallFact = facts.calls().entries()[condCallSlot].value;
        const auto& updateCallFact = facts.calls().entries()[updateCallSlot].value;
        const auto& condSelected = condCallFact.invocation.selected.variant();
        const auto& updateSelected = updateCallFact.invocation.selected.variant();
        if (!condSelected.is<checker::checked::PrimitiveCallable>() ||
            !updateSelected.is<checker::checked::PrimitiveCallable>() ||
            !isBoolSemanticType(semanticTypes, condResultType) || condLhsType != condRhsType ||
            condLhsType != initType || updateLhsType != updateRhsType ||
            updateLhsType != initType || updateValueType != updateLhsType ||
            returnType != function.resultType || initLiteralFact.type != initType ||
            condRhsLiteralFact.type != condRhsType || updateRhsLiteralFact.type != updateRhsType ||
            returnLiteralFact.type != returnType) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto condOperation =
            condSelected.get<checker::checked::PrimitiveCallable>().operation;
        const auto updateOperation =
            updateSelected.get<checker::checked::PrimitiveCallable>().operation;
        if (!isScalarComparisonOperation(condOperation) ||
            !isScalarArithmeticOperation(updateOperation)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Resolve the init local binding through the definition inventory.
        auto initBinding = ownerLocalBindingForPattern(bound.definitions(), pattern, tree);
        if (initBinding == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        // Look up every HIR node by its expected fixed id.
        zc::Maybe<const HirLocalBinding&> localRecord;
        zc::Maybe<const HirScalarLiteralExpression&> initLiteralRecord;
        zc::Maybe<const HirLocalReferenceExpression&> condLhsRecord;
        zc::Maybe<const HirScalarLiteralExpression&> condRhsRecord;
        zc::Maybe<const HirPrimitiveBinaryExpression&> condBinaryRecord;
        zc::Maybe<const HirLocalReferenceExpression&> updateLhsRecord;
        zc::Maybe<const HirScalarLiteralExpression&> updateRhsRecord;
        zc::Maybe<const HirPrimitiveBinaryExpression&> updateBinaryRecord;
        zc::Maybe<const HirLocalWriteStatement&> updateWriteRecord;
        zc::Maybe<const HirLoopStatement&> loopRecord;
        zc::Maybe<const HirScalarLiteralExpression&> returnValueRecord;
        for (const auto& local : candidate.impl->locals) {
          if (local.node != hirId(expectedFunction + 2)) continue;
          if (localRecord != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          localRecord = local;
        }
        for (const auto& expression : candidate.impl->expressions) {
          if (expression.node == hirId(expectedFunction + 3)) {
            if (initLiteralRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            initLiteralRecord = expression;
          }
          if (expression.node == hirId(expectedFunction + 5)) {
            if (condRhsRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            condRhsRecord = expression;
          }
          if (expression.node == hirId(expectedFunction + 8)) {
            if (updateRhsRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            updateRhsRecord = expression;
          }
          if (expression.node == hirId(expectedFunction + 13)) {
            if (returnValueRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            returnValueRecord = expression;
          }
        }
        for (const auto& reference : candidate.impl->localReferences) {
          if (reference.node == hirId(expectedFunction + 4)) {
            if (condLhsRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            condLhsRecord = reference;
          }
          if (reference.node == hirId(expectedFunction + 7)) {
            if (updateLhsRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            updateLhsRecord = reference;
          }
        }
        for (const auto& binary : candidate.impl->primitiveBinaryOperations) {
          if (binary.node == hirId(expectedFunction + 6)) {
            if (condBinaryRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            condBinaryRecord = binary;
          }
          if (binary.node == hirId(expectedFunction + 9)) {
            if (updateBinaryRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            updateBinaryRecord = binary;
          }
        }
        for (const auto& write : candidate.impl->localWrites) {
          if (write.node != hirId(expectedFunction + 10)) continue;
          if (updateWriteRecord != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          updateWriteRecord = write;
        }
        for (const auto& candidateLoop : candidate.impl->loops) {
          if (candidateLoop.node != hirId(expectedFunction + 11)) continue;
          if (loopRecord != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          loopRecord = candidateLoop;
        }
        if (localRecord == zc::none || initLiteralRecord == zc::none || condLhsRecord == zc::none ||
            condRhsRecord == zc::none || condBinaryRecord == zc::none ||
            updateLhsRecord == zc::none || updateRhsRecord == zc::none ||
            updateBinaryRecord == zc::none || updateWriteRecord == zc::none ||
            loopRecord == zc::none || returnValueRecord == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        ZC_IF_SOME(local, localRecord) {
          ZC_IF_SOME(initLit, initLiteralRecord) {
            ZC_IF_SOME(condLhs, condLhsRecord) {
              ZC_IF_SOME(condRhs, condRhsRecord) {
                ZC_IF_SOME(condBin, condBinaryRecord) {
                  ZC_IF_SOME(updLhs, updateLhsRecord) {
                    ZC_IF_SOME(updRhs, updateRhsRecord) {
                      ZC_IF_SOME(updBin, updateBinaryRecord) {
                        ZC_IF_SOME(updWrite, updateWriteRecord) {
                          ZC_IF_SOME(loop, loopRecord) {
                            ZC_IF_SOME(retVal, returnValueRecord) {
                              if (function.node != hirId(expectedFunction) ||
                                  block.node != hirId(expectedFunction + 1) ||
                                  function.body != block.node || block.statements.size() != 3 ||
                                  block.statements[0] != local.node ||
                                  block.statements[1] != loop.node ||
                                  block.statements[2] != returnStatement.node ||
                                  returnStatement.node != hirId(expectedFunction + 12) ||
                                  returnStatement.value != retVal.node ||
                                  returnStatement.resultType != function.resultType ||
                                  local.local != hirLocalId(1) || local.type != initType ||
                                  local.initializer != initLit.node || initLit.type != initType ||
                                  initLit.category != HirValueCategory::Value ||
                                  !sameConstant(initLit.value, initLiteralFact.literal, module,
                                                registries, semanticTypes) ||
                                  !sameSpan(initLit.sourceSpan,
                                            ZC_ASSERT_NONNULL(initializerSpan)) ||
                                  !sameSpan(local.sourceSpan, ZC_ASSERT_NONNULL(patternSpan)) ||
                                  condLhs.local != hirLocalId(1) || condLhs.type != condLhsType ||
                                  condLhs.category != HirValueCategory::Place ||
                                  !sameSpan(condLhs.sourceSpan, ZC_ASSERT_NONNULL(condLhsSpan)) ||
                                  condRhs.type != condRhsType ||
                                  condRhs.category != HirValueCategory::Value ||
                                  !sameConstant(condRhs.value, condRhsLiteralFact.literal, module,
                                                registries, semanticTypes) ||
                                  !sameSpan(condRhs.sourceSpan, ZC_ASSERT_NONNULL(condRhsSpan)) ||
                                  condBin.left != condLhs.node || condBin.right != condRhs.node ||
                                  condBin.operandType != condLhsType ||
                                  condBin.type != condResultType ||
                                  condBin.category != HirValueCategory::Value ||
                                  condBin.operation != condOperation ||
                                  !sameSpan(condBin.sourceSpan, ZC_ASSERT_NONNULL(condSpan)) ||
                                  updLhs.local != hirLocalId(1) || updLhs.type != updateLhsType ||
                                  updLhs.category != HirValueCategory::Place ||
                                  !sameSpan(updLhs.sourceSpan, ZC_ASSERT_NONNULL(updateLhsSpan)) ||
                                  updRhs.type != updateRhsType ||
                                  updRhs.category != HirValueCategory::Value ||
                                  !sameConstant(updRhs.value, updateRhsLiteralFact.literal, module,
                                                registries, semanticTypes) ||
                                  !sameSpan(updRhs.sourceSpan, ZC_ASSERT_NONNULL(updateRhsSpan)) ||
                                  updBin.left != updLhs.node || updBin.right != updRhs.node ||
                                  updBin.operandType != updateLhsType ||
                                  updBin.type != updateValueType ||
                                  updBin.category != HirValueCategory::Value ||
                                  updBin.operation != updateOperation ||
                                  !sameSpan(updBin.sourceSpan,
                                            ZC_ASSERT_NONNULL(updateValueSpan)) ||
                                  updWrite.local != hirLocalId(1) || updWrite.field != zc::none ||
                                  updWrite.type != updateValueType ||
                                  updWrite.value != updBin.node ||
                                  updWrite.kind != HirLocalWriteKind::Overwrite ||
                                  !sameSpan(updWrite.sourceSpan, ZC_ASSERT_NONNULL(updateSpan)) ||
                                  !sameSpan(updWrite.valueSpan,
                                            ZC_ASSERT_NONNULL(updateValueSpan)) ||
                                  loop.condition != condBin.node || loop.body.size() != 1 ||
                                  loop.body[0] != updWrite.node || loop.type != condResultType ||
                                  loop.category != HirValueCategory::Place ||
                                  !sameSpan(loop.sourceSpan, ZC_ASSERT_NONNULL(loopSpan)) ||
                                  (loop.breakSpan != zc::none) !=
                                      static_cast<bool>(source.forLoopBodyBreak) ||
                                  (loop.continueSpan != zc::none) !=
                                      static_cast<bool>(source.forLoopBodyContinue) ||
                                  retVal.type != returnType ||
                                  retVal.category != HirValueCategory::Value ||
                                  !sameConstant(retVal.value, returnLiteralFact.literal, module,
                                                registries, semanticTypes) ||
                                  !sameSpan(retVal.sourceSpan, ZC_ASSERT_NONNULL(valueSpan)) ||
                                  !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) ||
                                  !sameSpan(returnStatement.sourceSpan,
                                            ZC_ASSERT_NONNULL(returnSpan)) ||
                                  !typeExists(function.resultType, semanticTypes) ||
                                  !typeExists(initType, semanticTypes) ||
                                  !typeExists(condResultType, semanticTypes) ||
                                  !ownerLocalMatches(bound.definitions(),
                                                     ZC_ASSERT_NONNULL(initBinding), pattern,
                                                     tree)) {
                                return rejectHir<VerifiedHirModule>(
                                    ir::IrFailurePhase::HirVerification,
                                    ir::IrFailureKind::InvalidFact, module, registries, index + 1);
                              }
                              // Break and continue spans are optional; verify
                              // them only when present.
                              ZC_IF_SOME(breakSpan, loop.breakSpan) {
                                if (!sameSpan(breakSpan, ZC_ASSERT_NONNULL(sourceBreakSpan))) {
                                  return rejectHir<VerifiedHirModule>(
                                      ir::IrFailurePhase::HirVerification,
                                      ir::IrFailureKind::InvalidFact, module, registries,
                                      index + 1);
                                }
                              }
                              ZC_IF_SOME(continueSpan, loop.continueSpan) {
                                if (!sameSpan(continueSpan,
                                              ZC_ASSERT_NONNULL(sourceContinueSpan))) {
                                  return rejectHir<VerifiedHirModule>(
                                      ir::IrFailurePhase::HirVerification,
                                      ir::IrFailureKind::InvalidFact, module, registries,
                                      index + 1);
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
        nextFunction += 14;
        continue;
      }
    }
    // C-style for-loop accumulator return: N leading scalar `mut` accumulator
    // locals, a `for (let id = <lit>; <ident> <cmp> <lit>; <ident> = <binary>)
    // { <accumulator> = <accumulator> <bin> <ident|lit>; ... }` loop, and a
    // trailing `return <accumulator-local>;`. Fixed-id layout relative to the
    // function id F:
    //   F+0 function, F+1 body block,
    //   per accumulator k: F+2+2k local, F+3+2k init literal,
    //   F+2+2N loop-init local, F+3+2N loop-init literal,
    //   F+4+2N cond left ref, F+5+2N cond right literal, F+6+2N cond binary,
    //   per accumulator k: F+7+2N+4k bw left, F+8+2N+4k bw right,
    //     F+9+2N+4k bw binary, F+10+2N+4k bw write,
    //   F+7+2N+4N update left, F+8+2N+4N update right,
    //   F+9+2N+4N update binary, F+10+2N+4N update write,
    //   F+11+2N+4N loop, F+12+2N+4N return, F+13+2N+4N return value ref.
    // Total: 14 + 6N nodes.
    {
      bool isForLoopAccumulatorShape = false;
      ZC_IF_SOME(source, sourceShapeMaybe) {
        isForLoopAccumulatorShape = source.isForLoopAccumulator;
      }
      if (isForLoopAccumulatorShape) {
        const FunctionReturnShape& source = ZC_ASSERT_NONNULL(sourceShapeMaybe);
        const size_t accumulatorCount = source.forLoopAccumulatorPatterns.size();
        auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
        if (sourceDefinitionIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t definitionSlot = 0;
        ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
        const auto& sourceDefinition = definitions.definitions()[definitionSlot];
        const auto& tree = bound.tree();
        if (!hasExecutableBody(sourceDefinition, definitions) ||
            !definitionBelongsToModule(sourceDefinition, definitions) ||
            sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
            !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
            !tree.contains(sourceDefinition.node)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
        if (signaturePosition == zc::none || rootPosition == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t signatureSlot = 0;
        size_t rootSlot = 0;
        ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
        ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
        const auto& signature = signatures.definitions[signatureSlot];
        const auto& root = signatures.roots[rootSlot];
        auto expectedVisibility = visibility(root.visibility);
        if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
            !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>() ||
            expectedVisibility == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        auto expectedLinkage = linkage(callable);
        if (expectedLinkage == zc::none || signature.definition != function.definition ||
            signature.definitionKind != identity::DefinitionKind::Function ||
            root.canonicalDefinition != function.definition || root.sourceModule != module ||
            callable.receiver != zc::none || callable.raises != zc::none ||
            callable.success != function.resultType ||
            !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
            !sameSpan(function.sourceSpan, sourceDefinition.source) ||
            !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
            function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
            function.parameters.size() != callable.parameters.size()) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        for (size_t parameterIndex = 0; parameterIndex < function.parameters.size();
             ++parameterIndex) {
          const auto& parameter = function.parameters[parameterIndex];
          const auto& sourceParameter = callable.parameters[parameterIndex];
          if (parameter.key != sourceParameter.parameter ||
              parameter.type != sourceParameter.type || sourceParameter.hasDefault ||
              !typeExists(parameter.type, semanticTypes)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        // Resolve the N accumulator let declarator patterns and initializers.
        zc::Vector<ast::NodeId> accPatterns;
        zc::Vector<ast::NodeId> accInitializers;
        for (size_t k = 0; k < accumulatorCount; ++k) {
          const ast::NodeId accPattern = source.forLoopAccumulatorPatterns[k];
          const ast::NodeId accInitializer = source.forLoopAccumulatorInitializers[k];
          if (!tree.contains(accPattern) ||
              tree.node(accPattern).kind != ast::SyntaxKind::IdentifierPattern ||
              !tree.contains(accInitializer) || !isScalarLiteral(tree.node(accInitializer).kind)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          accPatterns.add(accPattern);
          accInitializers.add(accInitializer);
        }
        // Resolve the for-loop init let declarator, pattern, and initializer.
        const auto& loopInitLetNode = tree.node(source.forLoopInit);
        const ast::NodeId loopInitDeclarations(
            loopInitLetNode.payload.words[ast::kLetStmtDeclarationsWord]);
        const ast::NodeList loopInitDeclarators{
            tree.node(loopInitDeclarations)
                .payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
            tree.node(loopInitDeclarations)
                .payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
        if (!tree.contains(loopInitDeclarations) ||
            tree.node(loopInitDeclarations).kind != ast::SyntaxKind::VariableDeclaratorList ||
            !tree.contains(loopInitDeclarators) || loopInitDeclarators.size != 1) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto loopInitDeclarator = tree.list(loopInitDeclarators)[0];
        const ast::NodeId loopInitPattern(
            tree.node(loopInitDeclarator).payload.words[ast::kVariableDeclaratorPatternWord]);
        const ast::NodeId loopInitInitializer(
            tree.node(loopInitDeclarator).payload.words[ast::kVariableDeclaratorInitWord]);
        if (!tree.contains(loopInitPattern) ||
            tree.node(loopInitPattern).kind != ast::SyntaxKind::IdentifierPattern ||
            !tree.contains(loopInitInitializer) ||
            !isScalarLiteral(tree.node(loopInitInitializer).kind)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Resolve the condition binary operands.
        const auto& condNode = tree.node(source.forLoopCond);
        const ast::NodeId condLhs(condNode.payload.words[ast::kBinaryExprLhsWord]);
        const ast::NodeId condRhs(condNode.payload.words[ast::kBinaryExprRhsWord]);
        if (!tree.contains(condLhs) || tree.node(condLhs).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(condRhs) || !isScalarLiteral(tree.node(condRhs).kind)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Resolve the update assignment and its binary value operands.
        const auto& updateNode = tree.node(source.forLoopUpdate);
        const ast::NodeId updateTarget(updateNode.payload.words[ast::kAssignmentExprLhsWord]);
        const ast::NodeId updateValueNode(updateNode.payload.words[ast::kAssignmentExprRhsWord]);
        if (!tree.contains(updateTarget) ||
            tree.node(updateTarget).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(updateValueNode) ||
            tree.node(updateValueNode).kind != ast::SyntaxKind::BinaryExpr) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto& updateValueBin = tree.node(updateValueNode);
        const ast::NodeId updateLhs(updateValueBin.payload.words[ast::kBinaryExprLhsWord]);
        const ast::NodeId updateRhs(updateValueBin.payload.words[ast::kBinaryExprRhsWord]);
        if (!tree.contains(updateLhs) || tree.node(updateLhs).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(updateRhs) || !isScalarLiteral(tree.node(updateRhs).kind)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Resolve the N body-write assignments and their binary value operands.
        // Each body write is `<accumulator> = <accumulator> <bin> <ident|lit>;`.
        zc::Vector<ast::NodeId> bodyWriteAssigns;
        zc::Vector<ast::NodeId> bodyWriteValueNodes;
        zc::Vector<ast::NodeId> bodyWriteLhss;
        zc::Vector<ast::NodeId> bodyWriteRhss;
        zc::Vector<bool> bodyWriteRhsIsLiteral;
        for (size_t k = 0; k < accumulatorCount; ++k) {
          auto bodyWriteItem = statementItem(tree, source.forLoopBodyWrites[k]);
          if (bodyWriteItem == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          ast::NodeId bodyWriteStmt;
          ZC_IF_SOME(value, bodyWriteItem) { bodyWriteStmt = value; }
          if (!tree.contains(bodyWriteStmt) ||
              tree.node(bodyWriteStmt).kind != ast::SyntaxKind::ExpressionStatement) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const ast::NodeId bodyWriteAssign(
              tree.node(bodyWriteStmt).payload.words[ast::kExpressionStatementExpressionWord]);
          if (!tree.contains(bodyWriteAssign) ||
              tree.node(bodyWriteAssign).kind != ast::SyntaxKind::AssignmentExpr) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const ast::NodeId bodyWriteTarget(
              tree.node(bodyWriteAssign).payload.words[ast::kAssignmentExprLhsWord]);
          const ast::NodeId bodyWriteValueNode(
              tree.node(bodyWriteAssign).payload.words[ast::kAssignmentExprRhsWord]);
          if (!tree.contains(bodyWriteTarget) ||
              tree.node(bodyWriteTarget).kind != ast::SyntaxKind::IdentExpr ||
              !tree.contains(bodyWriteValueNode) ||
              tree.node(bodyWriteValueNode).kind != ast::SyntaxKind::BinaryExpr) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const auto& bodyWriteValueBin = tree.node(bodyWriteValueNode);
          const ast::NodeId bodyWriteLhs(bodyWriteValueBin.payload.words[ast::kBinaryExprLhsWord]);
          const ast::NodeId bodyWriteRhs(bodyWriteValueBin.payload.words[ast::kBinaryExprRhsWord]);
          const bool rhsIsLiteral = isScalarLiteral(tree.node(bodyWriteRhs).kind);
          if (!tree.contains(bodyWriteLhs) ||
              tree.node(bodyWriteLhs).kind != ast::SyntaxKind::IdentExpr ||
              !tree.contains(bodyWriteRhs) ||
              (tree.node(bodyWriteRhs).kind != ast::SyntaxKind::IdentExpr && !rhsIsLiteral)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          bodyWriteAssigns.add(bodyWriteAssign);
          bodyWriteValueNodes.add(bodyWriteValueNode);
          bodyWriteLhss.add(bodyWriteLhs);
          bodyWriteRhss.add(bodyWriteRhs);
          bodyWriteRhsIsLiteral.add(rhsIsLiteral);
        }
        // Spans for every source node that materializes a HIR node.
        auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
        zc::Vector<identity::SourceSpan> accPatternSpans;
        zc::Vector<identity::SourceSpan> accInitializerSpans;
        zc::Vector<identity::SourceSpan> bodyWriteSpans;
        zc::Vector<identity::SourceSpan> bodyWriteValueSpans;
        zc::Vector<identity::SourceSpan> bodyWriteLhsSpans;
        zc::Vector<identity::SourceSpan> bodyWriteRhsSpans;
        for (size_t k = 0; k < accumulatorCount; ++k) {
          auto ps = bound.parsedModule().spanFor(tree.node(accPatterns[k]).range);
          auto is_ = bound.parsedModule().spanFor(tree.node(accInitializers[k]).range);
          auto bws = bound.parsedModule().spanFor(tree.node(bodyWriteAssigns[k]).range);
          auto bwvs = bound.parsedModule().spanFor(tree.node(bodyWriteValueNodes[k]).range);
          auto bwls = bound.parsedModule().spanFor(tree.node(bodyWriteLhss[k]).range);
          auto bwrs = bound.parsedModule().spanFor(tree.node(bodyWriteRhss[k]).range);
          if (ps == zc::none || is_ == zc::none || bws == zc::none || bwvs == zc::none ||
              bwls == zc::none || bwrs == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          accPatternSpans.add(zc::mv(ZC_ASSERT_NONNULL(ps)));
          accInitializerSpans.add(zc::mv(ZC_ASSERT_NONNULL(is_)));
          bodyWriteSpans.add(zc::mv(ZC_ASSERT_NONNULL(bws)));
          bodyWriteValueSpans.add(zc::mv(ZC_ASSERT_NONNULL(bwvs)));
          bodyWriteLhsSpans.add(zc::mv(ZC_ASSERT_NONNULL(bwls)));
          bodyWriteRhsSpans.add(zc::mv(ZC_ASSERT_NONNULL(bwrs)));
        }
        auto loopInitPatternSpan = bound.parsedModule().spanFor(tree.node(loopInitPattern).range);
        auto loopInitInitializerSpan =
            bound.parsedModule().spanFor(tree.node(loopInitInitializer).range);
        auto condSpan = bound.parsedModule().spanFor(condNode.range);
        auto condLhsSpan = bound.parsedModule().spanFor(tree.node(condLhs).range);
        auto condRhsSpan = bound.parsedModule().spanFor(tree.node(condRhs).range);
        auto updateSpan = bound.parsedModule().spanFor(updateNode.range);
        auto updateValueSpan = bound.parsedModule().spanFor(updateValueBin.range);
        auto updateLhsSpan = bound.parsedModule().spanFor(tree.node(updateLhs).range);
        auto updateRhsSpan = bound.parsedModule().spanFor(tree.node(updateRhs).range);
        auto loopSpan = bound.parsedModule().spanFor(tree.node(source.forLoopStatement).range);
        auto returnValueSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
        zc::Maybe<identity::SourceSpan> sourceBreakSpan;
        zc::Maybe<identity::SourceSpan> sourceContinueSpan;
        if (source.forLoopBodyBreak) {
          sourceBreakSpan = bound.parsedModule().spanFor(tree.node(source.forLoopBodyBreak).range);
        }
        if (source.forLoopBodyContinue) {
          sourceContinueSpan =
              bound.parsedModule().spanFor(tree.node(source.forLoopBodyContinue).range);
        }
        if (bodySpan == zc::none || returnSpan == zc::none || loopInitPatternSpan == zc::none ||
            loopInitInitializerSpan == zc::none || condSpan == zc::none ||
            condLhsSpan == zc::none || condRhsSpan == zc::none || updateSpan == zc::none ||
            updateValueSpan == zc::none || updateLhsSpan == zc::none || updateRhsSpan == zc::none ||
            loopSpan == zc::none || returnValueSpan == zc::none ||
            (source.forLoopBodyBreak && sourceBreakSpan == zc::none) ||
            (source.forLoopBodyContinue && sourceContinueSpan == zc::none)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        // Node-type and literal facts for every typed source node.
        zc::Vector<identity::SemanticTypeId> accInitTypes;
        zc::Vector<checker::checked::CanonicalConstValue> accInitLiterals;
        zc::Vector<identity::SemanticTypeId> bodyWriteValueTypes;
        zc::Vector<identity::SemanticTypeId> bodyWriteLhsTypes;
        zc::Vector<identity::SemanticTypeId> bodyWriteRhsTypes;
        zc::Vector<checker::PrimitiveOperation> bodyWriteOperations;
        zc::Vector<zc::Maybe<checker::checked::CanonicalConstValue>> bodyWriteRhsLiterals;
        for (size_t k = 0; k < accumulatorCount; ++k) {
          auto accInitTypeIndex = factIndex(facts.nodeTypes(), accInitializers[k]);
          auto accInitLiteralIndex = factIndex(facts.literals(), accInitializers[k]);
          auto bodyWriteValueTypeIndex = factIndex(facts.nodeTypes(), bodyWriteValueNodes[k]);
          auto bodyWriteLhsTypeIndex = factIndex(facts.nodeTypes(), bodyWriteLhss[k]);
          auto bodyWriteRhsTypeIndex = factIndex(facts.nodeTypes(), bodyWriteRhss[k]);
          auto bodyWriteCallIndex = factIndex(facts.calls(), bodyWriteValueNodes[k]);
          if (accInitTypeIndex == zc::none || accInitLiteralIndex == zc::none ||
              bodyWriteValueTypeIndex == zc::none || bodyWriteLhsTypeIndex == zc::none ||
              bodyWriteRhsTypeIndex == zc::none || bodyWriteCallIndex == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          size_t accInitTypeSlot = 0;
          size_t accInitLiteralSlot = 0;
          size_t bodyWriteValueTypeSlot = 0;
          size_t bodyWriteLhsTypeSlot = 0;
          size_t bodyWriteRhsTypeSlot = 0;
          size_t bodyWriteCallSlot = 0;
          ZC_IF_SOME(v, accInitTypeIndex) { accInitTypeSlot = v; }
          ZC_IF_SOME(v, accInitLiteralIndex) { accInitLiteralSlot = v; }
          ZC_IF_SOME(v, bodyWriteValueTypeIndex) { bodyWriteValueTypeSlot = v; }
          ZC_IF_SOME(v, bodyWriteLhsTypeIndex) { bodyWriteLhsTypeSlot = v; }
          ZC_IF_SOME(v, bodyWriteRhsTypeIndex) { bodyWriteRhsTypeSlot = v; }
          ZC_IF_SOME(v, bodyWriteCallIndex) { bodyWriteCallSlot = v; }
          const auto accInitType = facts.nodeTypes().entries()[accInitTypeSlot].value;
          const auto& accInitLiteralFact = facts.literals().entries()[accInitLiteralSlot].value;
          const auto bodyWriteValueType = facts.nodeTypes().entries()[bodyWriteValueTypeSlot].value;
          const auto bodyWriteLhsType = facts.nodeTypes().entries()[bodyWriteLhsTypeSlot].value;
          const auto bodyWriteRhsType = facts.nodeTypes().entries()[bodyWriteRhsTypeSlot].value;
          const auto& bodyWriteCallFact = facts.calls().entries()[bodyWriteCallSlot].value;
          const auto& bodyWriteSelected = bodyWriteCallFact.invocation.selected.variant();
          if (!bodyWriteSelected.is<checker::checked::PrimitiveCallable>()) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const auto bodyWriteOperation =
              bodyWriteSelected.get<checker::checked::PrimitiveCallable>().operation;
          if (!isScalarArithmeticOperation(bodyWriteOperation)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          if (accInitLiteralFact.type != accInitType) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          zc::Maybe<checker::checked::CanonicalConstValue> bwRhsLiteral;
          if (bodyWriteRhsIsLiteral[k]) {
            auto bwRhsLiteralIndex = factIndex(facts.literals(), bodyWriteRhss[k]);
            if (bwRhsLiteralIndex == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
            size_t bwRhsLiteralSlot = 0;
            ZC_IF_SOME(v, bwRhsLiteralIndex) { bwRhsLiteralSlot = v; }
            const auto& bwRhsLiteralFact = facts.literals().entries()[bwRhsLiteralSlot].value;
            if (bwRhsLiteralFact.type != bodyWriteRhsType) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
            bwRhsLiteral = bwRhsLiteralFact.literal.clone();
          }
          accInitTypes.add(accInitType);
          accInitLiterals.add(accInitLiteralFact.literal.clone());
          bodyWriteValueTypes.add(bodyWriteValueType);
          bodyWriteLhsTypes.add(bodyWriteLhsType);
          bodyWriteRhsTypes.add(bodyWriteRhsType);
          bodyWriteOperations.add(bodyWriteOperation);
          bodyWriteRhsLiterals.add(zc::mv(bwRhsLiteral));
        }
        auto loopInitTypeIndex = factIndex(facts.nodeTypes(), loopInitInitializer);
        auto condResultTypeIndex = factIndex(facts.nodeTypes(), source.forLoopCond);
        auto condLhsTypeIndex = factIndex(facts.nodeTypes(), condLhs);
        auto condRhsTypeIndex = factIndex(facts.nodeTypes(), condRhs);
        auto updateValueTypeIndex = factIndex(facts.nodeTypes(), updateValueNode);
        auto updateLhsTypeIndex = factIndex(facts.nodeTypes(), updateLhs);
        auto updateRhsTypeIndex = factIndex(facts.nodeTypes(), updateRhs);
        auto returnTypeIndex = factIndex(facts.nodeTypes(), source.value);
        auto loopInitLiteralIndex = factIndex(facts.literals(), loopInitInitializer);
        auto condRhsLiteralIndex = factIndex(facts.literals(), condRhs);
        auto updateRhsLiteralIndex = factIndex(facts.literals(), updateRhs);
        auto condCallIndex = factIndex(facts.calls(), source.forLoopCond);
        auto updateCallIndex = factIndex(facts.calls(), updateValueNode);
        if (loopInitTypeIndex == zc::none || condResultTypeIndex == zc::none ||
            condLhsTypeIndex == zc::none || condRhsTypeIndex == zc::none ||
            updateValueTypeIndex == zc::none || updateLhsTypeIndex == zc::none ||
            updateRhsTypeIndex == zc::none || returnTypeIndex == zc::none ||
            loopInitLiteralIndex == zc::none || condRhsLiteralIndex == zc::none ||
            updateRhsLiteralIndex == zc::none || condCallIndex == zc::none ||
            updateCallIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t loopInitTypeSlot = 0;
        size_t condResultTypeSlot = 0;
        size_t condLhsTypeSlot = 0;
        size_t condRhsTypeSlot = 0;
        size_t updateValueTypeSlot = 0;
        size_t updateLhsTypeSlot = 0;
        size_t updateRhsTypeSlot = 0;
        size_t returnTypeSlot = 0;
        size_t loopInitLiteralSlot = 0;
        size_t condRhsLiteralSlot = 0;
        size_t updateRhsLiteralSlot = 0;
        size_t condCallSlot = 0;
        size_t updateCallSlot = 0;
        ZC_IF_SOME(v, loopInitTypeIndex) { loopInitTypeSlot = v; }
        ZC_IF_SOME(v, condResultTypeIndex) { condResultTypeSlot = v; }
        ZC_IF_SOME(v, condLhsTypeIndex) { condLhsTypeSlot = v; }
        ZC_IF_SOME(v, condRhsTypeIndex) { condRhsTypeSlot = v; }
        ZC_IF_SOME(v, updateValueTypeIndex) { updateValueTypeSlot = v; }
        ZC_IF_SOME(v, updateLhsTypeIndex) { updateLhsTypeSlot = v; }
        ZC_IF_SOME(v, updateRhsTypeIndex) { updateRhsTypeSlot = v; }
        ZC_IF_SOME(v, returnTypeIndex) { returnTypeSlot = v; }
        ZC_IF_SOME(v, loopInitLiteralIndex) { loopInitLiteralSlot = v; }
        ZC_IF_SOME(v, condRhsLiteralIndex) { condRhsLiteralSlot = v; }
        ZC_IF_SOME(v, updateRhsLiteralIndex) { updateRhsLiteralSlot = v; }
        ZC_IF_SOME(v, condCallIndex) { condCallSlot = v; }
        ZC_IF_SOME(v, updateCallIndex) { updateCallSlot = v; }
        const auto loopInitType = facts.nodeTypes().entries()[loopInitTypeSlot].value;
        const auto condResultType = facts.nodeTypes().entries()[condResultTypeSlot].value;
        const auto condLhsType = facts.nodeTypes().entries()[condLhsTypeSlot].value;
        const auto condRhsType = facts.nodeTypes().entries()[condRhsTypeSlot].value;
        const auto updateValueType = facts.nodeTypes().entries()[updateValueTypeSlot].value;
        const auto updateLhsType = facts.nodeTypes().entries()[updateLhsTypeSlot].value;
        const auto updateRhsType = facts.nodeTypes().entries()[updateRhsTypeSlot].value;
        const auto returnType = facts.nodeTypes().entries()[returnTypeSlot].value;
        const auto& loopInitLiteralFact = facts.literals().entries()[loopInitLiteralSlot].value;
        const auto& condRhsLiteralFact = facts.literals().entries()[condRhsLiteralSlot].value;
        const auto& updateRhsLiteralFact = facts.literals().entries()[updateRhsLiteralSlot].value;
        const auto& condCallFact = facts.calls().entries()[condCallSlot].value;
        const auto& updateCallFact = facts.calls().entries()[updateCallSlot].value;
        const auto& condSelected = condCallFact.invocation.selected.variant();
        const auto& updateSelected = updateCallFact.invocation.selected.variant();
        if (!condSelected.is<checker::checked::PrimitiveCallable>() ||
            !updateSelected.is<checker::checked::PrimitiveCallable>() ||
            !isBoolSemanticType(semanticTypes, condResultType) ||
            accInitTypes[0] != function.resultType || returnType != function.resultType ||
            loopInitType != condLhsType || loopInitType != condRhsType ||
            updateLhsType != updateRhsType || updateLhsType != loopInitType ||
            updateValueType != loopInitType || loopInitLiteralFact.type != loopInitType ||
            condRhsLiteralFact.type != condRhsType || updateRhsLiteralFact.type != updateRhsType) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto condOperation =
            condSelected.get<checker::checked::PrimitiveCallable>().operation;
        const auto updateOperation =
            updateSelected.get<checker::checked::PrimitiveCallable>().operation;
        if (!isScalarComparisonOperation(condOperation) ||
            !isScalarArithmeticOperation(updateOperation)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Per-accumulator type validation: each accumulator's init type matches
        // the loop init type, the body write lhs/rhs/result types match, and
        // the init literal type matches the accumulator type.
        for (size_t k = 0; k < accumulatorCount; ++k) {
          if (accInitTypes[k] != loopInitType || bodyWriteLhsTypes[k] != accInitTypes[k] ||
              bodyWriteRhsTypes[k] != accInitTypes[k] ||
              bodyWriteValueTypes[k] != accInitTypes[k]) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        // Resolve owner-local bindings through the definition inventory.
        zc::Vector<binder::OwnerLocalBindingId> accBindings;
        for (size_t k = 0; k < accumulatorCount; ++k) {
          auto accBinding = ownerLocalBindingForPattern(bound.definitions(), accPatterns[k], tree);
          if (accBinding == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          accBindings.add(ZC_ASSERT_NONNULL(accBinding));
        }
        auto loopInitBinding =
            ownerLocalBindingForPattern(bound.definitions(), loopInitPattern, tree);
        if (loopInitBinding == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        // Look up every HIR node by its expected fixed id. The layout is
        // computed from N: accumulator k occupies F+2+2k (local) and F+3+2k
        // (init literal); the loop init occupies F+2+2N and F+3+2N; the
        // comparison occupies F+4+2N through F+6+2N; body write k occupies
        // F+7+2N+4k through F+10+2N+4k; the update occupies F+7+2N+4N through
        // F+10+2N+4N; the loop, return, and return value occupy F+11+2N+4N,
        // F+12+2N+4N, and F+13+2N+4N.
        const uint32_t accLocalBase = 2;
        const uint32_t loopInitLocalOff = 2 + 2 * static_cast<uint32_t>(accumulatorCount);
        const uint32_t condLeftOff = 4 + 2 * static_cast<uint32_t>(accumulatorCount);
        const uint32_t bodyWriteBase = 7 + 2 * static_cast<uint32_t>(accumulatorCount);
        const uint32_t updateOff = 7 + 6 * static_cast<uint32_t>(accumulatorCount);
        const uint32_t loopOff = 11 + 6 * static_cast<uint32_t>(accumulatorCount);
        const uint32_t returnOff = 12 + 6 * static_cast<uint32_t>(accumulatorCount);
        const uint32_t returnValueOff = 13 + 6 * static_cast<uint32_t>(accumulatorCount);

        // Collect HIR nodes by scanning the candidate's collections.
        zc::Vector<zc::Maybe<const HirLocalBinding&>> accLocalRecords;
        zc::Vector<zc::Maybe<const HirScalarLiteralExpression&>> accInitLiteralRecords;
        accLocalRecords.resize(accumulatorCount);
        accInitLiteralRecords.resize(accumulatorCount);
        zc::Maybe<const HirLocalBinding&> loopInitLocalRecord;
        zc::Maybe<const HirScalarLiteralExpression&> loopInitLiteralRecord;
        zc::Maybe<const HirLocalReferenceExpression&> condLhsRecord;
        zc::Maybe<const HirScalarLiteralExpression&> condRhsRecord;
        zc::Maybe<const HirPrimitiveBinaryExpression&> condBinaryRecord;
        zc::Vector<zc::Maybe<const HirLocalReferenceExpression&>> bodyWriteLhsRecords;
        zc::Vector<zc::Maybe<const HirLocalReferenceExpression&>> bodyWriteRhsRefRecords;
        zc::Vector<zc::Maybe<const HirScalarLiteralExpression&>> bodyWriteRhsLitRecords;
        zc::Vector<zc::Maybe<const HirPrimitiveBinaryExpression&>> bodyWriteBinaryRecords;
        zc::Vector<zc::Maybe<const HirLocalWriteStatement&>> bodyWriteRecords;
        bodyWriteLhsRecords.resize(accumulatorCount);
        bodyWriteRhsRefRecords.resize(accumulatorCount);
        bodyWriteRhsLitRecords.resize(accumulatorCount);
        bodyWriteBinaryRecords.resize(accumulatorCount);
        bodyWriteRecords.resize(accumulatorCount);
        zc::Maybe<const HirLocalReferenceExpression&> updateLhsRecord;
        zc::Maybe<const HirScalarLiteralExpression&> updateRhsRecord;
        zc::Maybe<const HirPrimitiveBinaryExpression&> updateBinaryRecord;
        zc::Maybe<const HirLocalWriteStatement&> updateWriteRecord;
        zc::Maybe<const HirLoopStatement&> loopRecord;
        zc::Maybe<const HirLocalReferenceExpression&> returnValueRecord;

        for (const auto& local : candidate.impl->locals) {
          for (size_t k = 0; k < accumulatorCount; ++k) {
            if (local.node ==
                hirId(expectedFunction + accLocalBase + 2 * static_cast<uint32_t>(k))) {
              if (accLocalRecords[k] != zc::none) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::AdditionalFact, module,
                                                    registries, index + 1);
              }
              accLocalRecords[k] = local;
            }
          }
          if (local.node == hirId(expectedFunction + loopInitLocalOff)) {
            if (loopInitLocalRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            loopInitLocalRecord = local;
          }
        }
        for (const auto& expression : candidate.impl->expressions) {
          for (size_t k = 0; k < accumulatorCount; ++k) {
            if (expression.node ==
                hirId(expectedFunction + accLocalBase + 1 + 2 * static_cast<uint32_t>(k))) {
              if (accInitLiteralRecords[k] != zc::none) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::AdditionalFact, module,
                                                    registries, index + 1);
              }
              accInitLiteralRecords[k] = expression;
            }
          }
          if (expression.node == hirId(expectedFunction + loopInitLocalOff + 1)) {
            if (loopInitLiteralRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            loopInitLiteralRecord = expression;
          }
          if (expression.node == hirId(expectedFunction + condLeftOff + 1)) {
            if (condRhsRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            condRhsRecord = expression;
          }
          if (expression.node == hirId(expectedFunction + updateOff + 1)) {
            if (updateRhsRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            updateRhsRecord = expression;
          }
          for (size_t k = 0; k < accumulatorCount; ++k) {
            if (bodyWriteRhsIsLiteral[k] &&
                expression.node ==
                    hirId(expectedFunction + bodyWriteBase + 1 + 4 * static_cast<uint32_t>(k))) {
              if (bodyWriteRhsLitRecords[k] != zc::none) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::AdditionalFact, module,
                                                    registries, index + 1);
              }
              bodyWriteRhsLitRecords[k] = expression;
            }
          }
        }
        for (const auto& reference : candidate.impl->localReferences) {
          if (reference.node == hirId(expectedFunction + condLeftOff)) {
            if (condLhsRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            condLhsRecord = reference;
          }
          if (reference.node == hirId(expectedFunction + updateOff)) {
            if (updateLhsRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            updateLhsRecord = reference;
          }
          if (reference.node == hirId(expectedFunction + returnValueOff)) {
            if (returnValueRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            returnValueRecord = reference;
          }
          for (size_t k = 0; k < accumulatorCount; ++k) {
            if (reference.node ==
                hirId(expectedFunction + bodyWriteBase + 4 * static_cast<uint32_t>(k))) {
              if (bodyWriteLhsRecords[k] != zc::none) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::AdditionalFact, module,
                                                    registries, index + 1);
              }
              bodyWriteLhsRecords[k] = reference;
            }
            if (!bodyWriteRhsIsLiteral[k] &&
                reference.node ==
                    hirId(expectedFunction + bodyWriteBase + 1 + 4 * static_cast<uint32_t>(k))) {
              if (bodyWriteRhsRefRecords[k] != zc::none) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::AdditionalFact, module,
                                                    registries, index + 1);
              }
              bodyWriteRhsRefRecords[k] = reference;
            }
          }
        }
        for (const auto& binary : candidate.impl->primitiveBinaryOperations) {
          if (binary.node == hirId(expectedFunction + condLeftOff + 2)) {
            if (condBinaryRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            condBinaryRecord = binary;
          }
          if (binary.node == hirId(expectedFunction + updateOff + 2)) {
            if (updateBinaryRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            updateBinaryRecord = binary;
          }
          for (size_t k = 0; k < accumulatorCount; ++k) {
            if (binary.node ==
                hirId(expectedFunction + bodyWriteBase + 2 + 4 * static_cast<uint32_t>(k))) {
              if (bodyWriteBinaryRecords[k] != zc::none) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::AdditionalFact, module,
                                                    registries, index + 1);
              }
              bodyWriteBinaryRecords[k] = binary;
            }
          }
        }
        for (const auto& write : candidate.impl->localWrites) {
          if (write.node == hirId(expectedFunction + updateOff + 3)) {
            if (updateWriteRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            updateWriteRecord = write;
          }
          for (size_t k = 0; k < accumulatorCount; ++k) {
            if (write.node ==
                hirId(expectedFunction + bodyWriteBase + 3 + 4 * static_cast<uint32_t>(k))) {
              if (bodyWriteRecords[k] != zc::none) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::AdditionalFact, module,
                                                    registries, index + 1);
              }
              bodyWriteRecords[k] = write;
            }
          }
        }
        for (const auto& candidateLoop : candidate.impl->loops) {
          if (candidateLoop.node != hirId(expectedFunction + loopOff)) continue;
          if (loopRecord != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          loopRecord = candidateLoop;
        }
        // Verify all required records were found.
        if (loopInitLocalRecord == zc::none || loopInitLiteralRecord == zc::none ||
            condLhsRecord == zc::none || condRhsRecord == zc::none ||
            condBinaryRecord == zc::none || updateLhsRecord == zc::none ||
            updateRhsRecord == zc::none || updateBinaryRecord == zc::none ||
            updateWriteRecord == zc::none || loopRecord == zc::none ||
            returnValueRecord == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        for (size_t k = 0; k < accumulatorCount; ++k) {
          if (accLocalRecords[k] == zc::none || accInitLiteralRecords[k] == zc::none ||
              bodyWriteLhsRecords[k] == zc::none || bodyWriteBinaryRecords[k] == zc::none ||
              bodyWriteRecords[k] == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          if (bodyWriteRhsIsLiteral[k]) {
            if (bodyWriteRhsLitRecords[k] == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
          } else {
            if (bodyWriteRhsRefRecords[k] == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
          }
        }
        // Verify the function, body block, return, and common HIR nodes.
        ZC_IF_SOME(loopInitLocal, loopInitLocalRecord) {
          ZC_IF_SOME(loopInitLit, loopInitLiteralRecord) {
            ZC_IF_SOME(condLhs, condLhsRecord) {
              ZC_IF_SOME(condRhs, condRhsRecord) {
                ZC_IF_SOME(condBin, condBinaryRecord) {
                  ZC_IF_SOME(updLhs, updateLhsRecord) {
                    ZC_IF_SOME(updRhs, updateRhsRecord) {
                      ZC_IF_SOME(updBin, updateBinaryRecord) {
                        ZC_IF_SOME(updWrite, updateWriteRecord) {
                          ZC_IF_SOME(loop, loopRecord) {
                            ZC_IF_SOME(retVal, returnValueRecord) {
                              if (function.node != hirId(expectedFunction) ||
                                  block.node != hirId(expectedFunction + 1) ||
                                  function.body != block.node ||
                                  block.statements.size() != accumulatorCount + 3 ||
                                  returnStatement.node != hirId(expectedFunction + returnOff) ||
                                  returnStatement.value != retVal.node ||
                                  returnStatement.resultType != function.resultType ||
                                  loopInitLocal.local !=
                                      hirLocalId(static_cast<uint32_t>(accumulatorCount + 1)) ||
                                  loopInitLocal.type != loopInitType ||
                                  loopInitLocal.initializer != loopInitLit.node ||
                                  loopInitLit.type != loopInitType ||
                                  loopInitLit.category != HirValueCategory::Value ||
                                  !sameConstant(loopInitLit.value, loopInitLiteralFact.literal,
                                                module, registries, semanticTypes) ||
                                  !sameSpan(loopInitLit.sourceSpan,
                                            ZC_ASSERT_NONNULL(loopInitInitializerSpan)) ||
                                  !sameSpan(loopInitLocal.sourceSpan,
                                            ZC_ASSERT_NONNULL(loopInitPatternSpan)) ||
                                  condLhs.local !=
                                      hirLocalId(static_cast<uint32_t>(accumulatorCount + 1)) ||
                                  condLhs.type != condLhsType ||
                                  condLhs.category != HirValueCategory::Place ||
                                  !sameSpan(condLhs.sourceSpan, ZC_ASSERT_NONNULL(condLhsSpan)) ||
                                  condRhs.type != condRhsType ||
                                  condRhs.category != HirValueCategory::Value ||
                                  !sameConstant(condRhs.value, condRhsLiteralFact.literal, module,
                                                registries, semanticTypes) ||
                                  !sameSpan(condRhs.sourceSpan, ZC_ASSERT_NONNULL(condRhsSpan)) ||
                                  condBin.left != condLhs.node || condBin.right != condRhs.node ||
                                  condBin.operandType != condLhsType ||
                                  condBin.type != condResultType ||
                                  condBin.category != HirValueCategory::Value ||
                                  condBin.operation != condOperation ||
                                  !sameSpan(condBin.sourceSpan, ZC_ASSERT_NONNULL(condSpan)) ||
                                  updLhs.local !=
                                      hirLocalId(static_cast<uint32_t>(accumulatorCount + 1)) ||
                                  updLhs.type != updateLhsType ||
                                  updLhs.category != HirValueCategory::Place ||
                                  !sameSpan(updLhs.sourceSpan, ZC_ASSERT_NONNULL(updateLhsSpan)) ||
                                  updRhs.type != updateRhsType ||
                                  updRhs.category != HirValueCategory::Value ||
                                  !sameConstant(updRhs.value, updateRhsLiteralFact.literal, module,
                                                registries, semanticTypes) ||
                                  !sameSpan(updRhs.sourceSpan, ZC_ASSERT_NONNULL(updateRhsSpan)) ||
                                  updBin.left != updLhs.node || updBin.right != updRhs.node ||
                                  updBin.operandType != updateLhsType ||
                                  updBin.type != updateValueType ||
                                  updBin.category != HirValueCategory::Value ||
                                  updBin.operation != updateOperation ||
                                  !sameSpan(updBin.sourceSpan,
                                            ZC_ASSERT_NONNULL(updateValueSpan)) ||
                                  updWrite.local !=
                                      hirLocalId(static_cast<uint32_t>(accumulatorCount + 1)) ||
                                  updWrite.field != zc::none || updWrite.type != updateValueType ||
                                  updWrite.value != updBin.node ||
                                  updWrite.kind != HirLocalWriteKind::Overwrite ||
                                  !sameSpan(updWrite.sourceSpan, ZC_ASSERT_NONNULL(updateSpan)) ||
                                  !sameSpan(updWrite.valueSpan,
                                            ZC_ASSERT_NONNULL(updateValueSpan)) ||
                                  loop.condition != condBin.node ||
                                  loop.body.size() != accumulatorCount + 1 ||
                                  loop.type != condResultType ||
                                  loop.category != HirValueCategory::Place ||
                                  !sameSpan(loop.sourceSpan, ZC_ASSERT_NONNULL(loopSpan)) ||
                                  (loop.breakSpan != zc::none) !=
                                      static_cast<bool>(source.forLoopBodyBreak) ||
                                  (loop.continueSpan != zc::none) !=
                                      static_cast<bool>(source.forLoopBodyContinue) ||
                                  retVal.local != hirLocalId(1) || retVal.type != returnType ||
                                  retVal.category != HirValueCategory::Place ||
                                  !sameSpan(retVal.sourceSpan,
                                            ZC_ASSERT_NONNULL(returnValueSpan)) ||
                                  !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) ||
                                  !sameSpan(returnStatement.sourceSpan,
                                            ZC_ASSERT_NONNULL(returnSpan)) ||
                                  !typeExists(function.resultType, semanticTypes) ||
                                  !typeExists(loopInitType, semanticTypes) ||
                                  !typeExists(condResultType, semanticTypes) ||
                                  !ownerLocalMatches(bound.definitions(),
                                                     ZC_ASSERT_NONNULL(loopInitBinding),
                                                     loopInitPattern, tree)) {
                                return rejectHir<VerifiedHirModule>(
                                    ir::IrFailurePhase::HirVerification,
                                    ir::IrFailureKind::InvalidFact, module, registries, index + 1);
                              }
                              // Verify body block statement order: accumulator
                              // locals, loop-init local, loop, return.
                              for (size_t k = 0; k < accumulatorCount; ++k) {
                                ZC_IF_SOME(accLocal, accLocalRecords[k]) {
                                  if (block.statements[k] != accLocal.node) {
                                    return rejectHir<VerifiedHirModule>(
                                        ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::InvalidFact, module, registries,
                                        index + 1);
                                  }
                                }
                              }
                              if (block.statements[accumulatorCount] != loopInitLocal.node ||
                                  block.statements[accumulatorCount + 1] != loop.node ||
                                  block.statements[accumulatorCount + 2] != returnStatement.node) {
                                return rejectHir<VerifiedHirModule>(
                                    ir::IrFailurePhase::HirVerification,
                                    ir::IrFailureKind::InvalidFact, module, registries, index + 1);
                              }
                              // Verify loop body statement order: body writes,
                              // update write.
                              for (size_t k = 0; k < accumulatorCount; ++k) {
                                ZC_IF_SOME(bw, bodyWriteRecords[k]) {
                                  if (loop.body[k] != bw.node) {
                                    return rejectHir<VerifiedHirModule>(
                                        ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::InvalidFact, module, registries,
                                        index + 1);
                                  }
                                }
                              }
                              if (loop.body[accumulatorCount] != updWrite.node) {
                                return rejectHir<VerifiedHirModule>(
                                    ir::IrFailurePhase::HirVerification,
                                    ir::IrFailureKind::InvalidFact, module, registries, index + 1);
                              }
                              // Verify per-accumulator HIR nodes.
                              for (size_t k = 0; k < accumulatorCount; ++k) {
                                ZC_IF_SOME(accLocal, accLocalRecords[k]) {
                                  ZC_IF_SOME(accInitLit, accInitLiteralRecords[k]) {
                                    ZC_IF_SOME(bwLhs, bodyWriteLhsRecords[k]) {
                                      ZC_IF_SOME(bwBin, bodyWriteBinaryRecords[k]) {
                                        ZC_IF_SOME(bw, bodyWriteRecords[k]) {
                                          if (accLocal.local !=
                                                  hirLocalId(static_cast<uint32_t>(k + 1)) ||
                                              accLocal.type != accInitTypes[k] ||
                                              accLocal.initializer != accInitLit.node ||
                                              accInitLit.type != accInitTypes[k] ||
                                              accInitLit.category != HirValueCategory::Value ||
                                              !sameConstant(accInitLit.value, accInitLiterals[k],
                                                            module, registries, semanticTypes) ||
                                              !sameSpan(accInitLit.sourceSpan,
                                                        accInitializerSpans[k]) ||
                                              !sameSpan(accLocal.sourceSpan, accPatternSpans[k]) ||
                                              bwLhs.local !=
                                                  hirLocalId(static_cast<uint32_t>(k + 1)) ||
                                              bwLhs.type != bodyWriteLhsTypes[k] ||
                                              bwLhs.category != HirValueCategory::Place ||
                                              !sameSpan(bwLhs.sourceSpan, bodyWriteLhsSpans[k]) ||
                                              bwBin.left != bwLhs.node ||
                                              bwBin.operandType != bodyWriteLhsTypes[k] ||
                                              bwBin.type != bodyWriteValueTypes[k] ||
                                              bwBin.category != HirValueCategory::Value ||
                                              bwBin.operation != bodyWriteOperations[k] ||
                                              !sameSpan(bwBin.sourceSpan, bodyWriteValueSpans[k]) ||
                                              bw.local !=
                                                  hirLocalId(static_cast<uint32_t>(k + 1)) ||
                                              bw.field != zc::none ||
                                              bw.type != bodyWriteValueTypes[k] ||
                                              bw.value != bwBin.node ||
                                              bw.kind != HirLocalWriteKind::Overwrite ||
                                              !sameSpan(bw.sourceSpan, bodyWriteSpans[k]) ||
                                              !sameSpan(bw.valueSpan, bodyWriteValueSpans[k]) ||
                                              !ownerLocalMatches(bound.definitions(),
                                                                 accBindings[k], accPatterns[k],
                                                                 tree)) {
                                            return rejectHir<VerifiedHirModule>(
                                                ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
                                          }
                                          // Verify the body write right operand:
                                          // a local reference to the init local
                                          // or a scalar literal.
                                          if (bodyWriteRhsIsLiteral[k]) {
                                            ZC_IF_SOME(bwRhsLit, bodyWriteRhsLitRecords[k]) {
                                              if (bwRhsLit.type != bodyWriteRhsTypes[k] ||
                                                  bwRhsLit.category != HirValueCategory::Value ||
                                                  !sameConstant(
                                                      bwRhsLit.value,
                                                      ZC_ASSERT_NONNULL(bodyWriteRhsLiterals[k]),
                                                      module, registries, semanticTypes) ||
                                                  !sameSpan(bwRhsLit.sourceSpan,
                                                            bodyWriteRhsSpans[k]) ||
                                                  bwBin.right != bwRhsLit.node) {
                                                return rejectHir<VerifiedHirModule>(
                                                    ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::InvalidFact, module,
                                                    registries, index + 1);
                                              }
                                            }
                                          } else {
                                            ZC_IF_SOME(bwRhsRef, bodyWriteRhsRefRecords[k]) {
                                              if (bwRhsRef.local !=
                                                      hirLocalId(static_cast<uint32_t>(
                                                          accumulatorCount + 1)) ||
                                                  bwRhsRef.type != bodyWriteRhsTypes[k] ||
                                                  bwRhsRef.category != HirValueCategory::Place ||
                                                  !sameSpan(bwRhsRef.sourceSpan,
                                                            bodyWriteRhsSpans[k]) ||
                                                  bwBin.right != bwRhsRef.node) {
                                                return rejectHir<VerifiedHirModule>(
                                                    ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::InvalidFact, module,
                                                    registries, index + 1);
                                              }
                                            }
                                          }
                                        }
                                      }
                                    }
                                  }
                                }
                              }
                              // Break and continue spans are optional; verify
                              // them only when present.
                              ZC_IF_SOME(breakSpan, loop.breakSpan) {
                                if (!sameSpan(breakSpan, ZC_ASSERT_NONNULL(sourceBreakSpan))) {
                                  return rejectHir<VerifiedHirModule>(
                                      ir::IrFailurePhase::HirVerification,
                                      ir::IrFailureKind::InvalidFact, module, registries,
                                      index + 1);
                                }
                              }
                              ZC_IF_SOME(continueSpan, loop.continueSpan) {
                                if (!sameSpan(continueSpan,
                                              ZC_ASSERT_NONNULL(sourceContinueSpan))) {
                                  return rejectHir<VerifiedHirModule>(
                                      ir::IrFailurePhase::HirVerification,
                                      ir::IrFailureKind::InvalidFact, module, registries,
                                      index + 1);
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
        nextFunction += 14 + 6 * static_cast<uint32_t>(accumulatorCount);
        continue;
      }
    }
    // Discarded receiver-call statement followed by a trailing receiver-call
    // return. Nine fixed node ids; this branch must run before the generic
    // local machinery, which assumes a two-statement block and would misread
    // the statement-position call.
    {
      bool isDiscardedShape = false;
      ZC_IF_SOME(source, sourceShapeMaybe) {
        isDiscardedShape = source.hasDiscardedReceiverCallStatement;
      }
      if (isDiscardedShape) {
        const FunctionReturnShape& source = ZC_ASSERT_NONNULL(sourceShapeMaybe);
        const auto localNodeId = hirId(expectedFunction + 2);
        const auto aggregateNodeId = hirId(expectedFunction + 3);
        const auto setReceiverNodeId = hirId(expectedFunction + 4);
        const auto setCallNodeId = hirId(expectedFunction + 5);
        const auto getReceiverNodeId = hirId(expectedFunction + 6);
        const auto getCallNodeId = hirId(expectedFunction + 7);
        const auto returnNodeId = hirId(expectedFunction + 8);
        zc::Maybe<const HirLocalBinding&> localRecord;
        zc::Maybe<const HirNominalAggregateExpression&> aggregateRecord;
        zc::Maybe<const HirLocalReferenceExpression&> setReceiverRecord;
        zc::Maybe<const HirLocalReferenceExpression&> getReceiverRecord;
        zc::Maybe<const HirReceiverCallExpression&> setCallRecord;
        zc::Maybe<const HirReceiverCallExpression&> getCallRecord;
        zc::Maybe<const HirReturnStatement&> discardedReturnRecord;
        for (const auto& local : candidate.impl->locals) {
          if (local.node != localNodeId) continue;
          if (localRecord != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          localRecord = local;
        }
        for (const auto& aggregate : candidate.impl->aggregates) {
          if (aggregate.node != aggregateNodeId) continue;
          if (aggregateRecord != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          aggregateRecord = aggregate;
        }
        for (const auto& reference : candidate.impl->localReferences) {
          if (reference.node == setReceiverNodeId) {
            if (setReceiverRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            setReceiverRecord = reference;
          } else if (reference.node == getReceiverNodeId) {
            if (getReceiverRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            getReceiverRecord = reference;
          }
        }
        for (const auto& call : candidate.impl->receiverCalls) {
          if (call.node == setCallNodeId) {
            if (setCallRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            setCallRecord = call;
          } else if (call.node == getCallNodeId) {
            if (getCallRecord != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            getCallRecord = call;
          }
        }
        for (const auto& candidateReturn : candidate.impl->returns) {
          if (candidateReturn.node != returnNodeId) continue;
          if (discardedReturnRecord != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          discardedReturnRecord = candidateReturn;
        }
        auto rejectDiscarded = [&]() {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        };
        if (function.node != hirId(expectedFunction) || block.node != hirId(expectedFunction + 1) ||
            function.body != block.node || block.statements.size() != 3 ||
            block.statements[0] != localNodeId || block.statements[1] != setCallNodeId ||
            block.statements[2] != returnNodeId || localRecord == zc::none ||
            aggregateRecord == zc::none || setReceiverRecord == zc::none ||
            getReceiverRecord == zc::none || setCallRecord == zc::none ||
            getCallRecord == zc::none || discardedReturnRecord == zc::none) {
          return rejectDiscarded();
        }
        const auto& localBinding = ZC_ASSERT_NONNULL(localRecord);
        const auto& receiverAggregate = ZC_ASSERT_NONNULL(aggregateRecord);
        const auto& setReceiver = ZC_ASSERT_NONNULL(setReceiverRecord);
        const auto& getReceiver = ZC_ASSERT_NONNULL(getReceiverRecord);
        const auto& setCall = ZC_ASSERT_NONNULL(setCallRecord);
        const auto& getCall = ZC_ASSERT_NONNULL(getCallRecord);
        const auto& discardedReturn = ZC_ASSERT_NONNULL(discardedReturnRecord);
        const auto ownerType = localBinding.type;
        if (localBinding.local != hirLocalId(1) || localBinding.initializer != aggregateNodeId ||
            localBinding.type != ownerType || receiverAggregate.node != aggregateNodeId ||
            receiverAggregate.type != ownerType ||
            receiverAggregate.category != HirValueCategory::Value ||
            setReceiver.local != hirLocalId(1) || setReceiver.type != ownerType ||
            setReceiver.category != HirValueCategory::Place || getReceiver.local != hirLocalId(1) ||
            getReceiver.type != ownerType || getReceiver.category != HirValueCategory::Place ||
            setCall.receiver != setReceiverNodeId || getCall.receiver != getReceiverNodeId ||
            discardedReturn.value != getCallNodeId ||
            discardedReturn.resultType != function.resultType ||
            setCall.receiverMode != checker::checked::ReceiverMode::Mutable ||
            setCall.receiverAdjustments.size() != 1 ||
            setCall.receiverAdjustments[0] !=
                checker::checked::ReceiverAdjustmentStep::BorrowMutable ||
            setCall.arguments.size() != 1 ||
            getCall.receiverMode != checker::checked::ReceiverMode::Shared ||
            getCall.receiverAdjustments.size() != 1 ||
            getCall.receiverAdjustments[0] !=
                checker::checked::ReceiverAdjustmentStep::BorrowShared ||
            getCall.arguments.size() != 0) {
          return rejectDiscarded();
        }
        const auto setResultLookup = semanticTypes.get(setCall.resultType);
        if (!setResultLookup.is<type::SemanticTypeLookup>() ||
            setResultLookup.get<type::SemanticTypeLookup>().data().primitiveKind() == zc::none ||
            ZC_ASSERT_NONNULL(
                setResultLookup.get<type::SemanticTypeLookup>().data().primitiveKind()) !=
                type::semantic::PrimitiveKind::Unit ||
            getCall.resultType != function.resultType) {
          return rejectDiscarded();
        }
        auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
        if (sourceDefinitionIndex == zc::none || signaturePosition == zc::none ||
            rootPosition == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t definitionSlot = 0;
        size_t signatureSlot = 0;
        size_t rootSlot = 0;
        ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
        ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
        ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
        const auto& sourceDefinition = definitions.definitions()[definitionSlot];
        const auto& signature = signatures.definitions[signatureSlot];
        const auto& root = signatures.roots[rootSlot];
        const auto& tree = bound.tree();
        if (!hasExecutableBody(sourceDefinition, definitions) ||
            !definitionBelongsToModule(sourceDefinition, definitions) ||
            sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
            !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
            !tree.contains(sourceDefinition.node) ||
            tree.node(sourceDefinition.node).kind != ast::SyntaxKind::FunctionDecl ||
            !signature.payload.variant().is<checker::signature::CallableSignature>() ||
            !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
          return rejectDiscarded();
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        auto expectedVisibility = visibility(root.visibility);
        auto expectedLinkage = linkage(callable);
        if (expectedVisibility == zc::none || expectedLinkage == zc::none ||
            signature.definition != function.definition ||
            signature.definitionKind != identity::DefinitionKind::Function ||
            root.canonicalDefinition != function.definition || root.sourceModule != module ||
            callable.receiver != zc::none || callable.raises != zc::none ||
            callable.success != function.resultType ||
            !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
            !sameSpan(function.sourceSpan, sourceDefinition.source) ||
            !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
            function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
            function.parameters.size() != callable.parameters.size()) {
          return rejectDiscarded();
        }
        const ast::NodeId statementNode = source.discardedCallStatement;
        const ast::NodeId setSourceCall(
            tree.node(statementNode).payload.words[ast::kExpressionStatementExpressionWord]);
        const ast::NodeId getSourceCall = source.value;
        ast::NodeId initializer;
        ZC_IF_SOME(value, source.localInitializer) { initializer = value; }
        if (!tree.contains(setSourceCall) ||
            tree.node(setSourceCall).kind != ast::SyntaxKind::CallExpression ||
            !tree.contains(getSourceCall) ||
            tree.node(getSourceCall).kind != ast::SyntaxKind::CallExpression ||
            !tree.contains(initializer) ||
            tree.node(initializer).kind != ast::SyntaxKind::StructLiteralExpr) {
          return rejectDiscarded();
        }
        // Validates one receiver call's member, invocation, dispatch, receiver
        // adjustment, and literal arguments against the checked facts.
        auto validateCallFact =
            [&](ast::NodeId sourceCallNode, const HirReceiverCallExpression& hirCall,
                checker::checked::ReceiverMode expectedMode,
                checker::checked::ReceiverAdjustmentStep expectedStep,
                type::semantic::Mutability expectedMutability,
                identity::SemanticTypeId expectedResultType, bool expectedUnitResult) -> bool {
          const auto& sourceCallNodeRef = tree.node(sourceCallNode);
          const ast::NodeId calleeNode(
              sourceCallNodeRef.payload.words[ast::kCallExpressionCalleeWord]);
          const ast::NodeList arguments{
              sourceCallNodeRef.payload.words[ast::kCallExpressionArgsFirstWord],
              sourceCallNodeRef.payload.words[ast::kCallExpressionArgsSizeWord]};
          const ast::NodeId receiverNode(
              tree.node(calleeNode).payload.words[ast::kMemberExpressionObjectWord]);
          auto receiverTypeIndex = factIndex(facts.nodeTypes(), receiverNode);
          auto calleeTypeIndex = factIndex(facts.nodeTypes(), calleeNode);
          auto memberIndex = factIndex(facts.members(), calleeNode);
          auto checkedCallIndex = factIndex(facts.calls(), sourceCallNode);
          auto callKey = checkedNodeKey(tree, bound.parsedModule(), sourceCallNode);
          auto callSpan = bound.parsedModule().spanFor(sourceCallNodeRef.range);
          auto receiverSpan = bound.parsedModule().spanFor(tree.node(receiverNode).range);
          auto dispatchIndex =
              callKey == zc::none
                  ? zc::none
                  : dispatchFactIndex(candidate.impl->checkedModule.dispatchFacts().facts(),
                                      ZC_ASSERT_NONNULL(callKey));
          auto factReceiverBinding = resolvedOwnerLocal(bound.bindings(), receiverNode);
          auto factOwnerBinding =
              ownerLocalBindingForPattern(definitions, source.localPattern, tree);
          if (!tree.contains(calleeNode) ||
              tree.node(calleeNode).kind != ast::SyntaxKind::MemberExpression ||
              static_cast<ast::MemberAccessKind>(
                  tree.node(calleeNode).payload.words[ast::kMemberExpressionAccessWord]) !=
                  ast::MemberAccessKind::Dot ||
              !tree.contains(receiverNode) ||
              tree.node(receiverNode).kind != ast::SyntaxKind::IdentExpr ||
              factReceiverBinding == zc::none || factOwnerBinding == zc::none ||
              ZC_ASSERT_NONNULL(factReceiverBinding) != ZC_ASSERT_NONNULL(factOwnerBinding) ||
              receiverTypeIndex == zc::none || calleeTypeIndex == zc::none ||
              memberIndex == zc::none || checkedCallIndex == zc::none || callKey == zc::none ||
              dispatchIndex == zc::none || callSpan == zc::none || receiverSpan == zc::none) {
            return false;
          }
          size_t receiverTypeSlot = 0;
          size_t calleeTypeSlot = 0;
          size_t memberSlot = 0;
          size_t checkedCallSlot = 0;
          size_t dispatchSlot = 0;
          ZC_IF_SOME(index, receiverTypeIndex) { receiverTypeSlot = index; }
          ZC_IF_SOME(index, calleeTypeIndex) { calleeTypeSlot = index; }
          ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
          ZC_IF_SOME(index, checkedCallIndex) { checkedCallSlot = index; }
          ZC_IF_SOME(index, dispatchIndex) { dispatchSlot = index; }
          const auto& member = facts.members().entries()[memberSlot].value;
          const auto& invocation = facts.calls().entries()[checkedCallSlot].value.invocation;
          const auto& selected = invocation.selected.variant();
          const auto& dispatch =
              candidate.impl->checkedModule.dispatchFacts().facts()[dispatchSlot];
          const auto& target = dispatch.fact.target.variant();
          const auto& transform = dispatch.fact.resultTransform.variant();
          bool dispatchOwnerMatches = false;
          ZC_IF_SOME(owner, dispatch.owner) { dispatchOwnerMatches = owner == function.definition; }
          const auto expectedResultIsUnit = [&](identity::SemanticTypeId id) {
            auto lookup = semanticTypes.get(id);
            return lookup.is<type::SemanticTypeLookup>() &&
                   lookup.get<type::SemanticTypeLookup>().data().primitiveKind() != zc::none &&
                   ZC_ASSERT_NONNULL(
                       lookup.get<type::SemanticTypeLookup>().data().primitiveKind()) ==
                       type::semantic::PrimitiveKind::Unit;
          };
          if (!selected.is<checker::checked::ConcreteMethodCallable>() ||
              !target.is<checker::dispatch::ConcreteMethodTarget>() ||
              !transform.is<checker::dispatch::IdentityResultTransform>() ||
              selected.get<checker::checked::ConcreteMethodCallable>().method != member.member ||
              target.get<checker::dispatch::ConcreteMethodTarget>().method != member.member ||
              member.node != calleeNode ||
              member.receiverType != facts.nodeTypes().entries()[receiverTypeSlot].value ||
              member.memberType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
              member.adjustment != zc::none || hirCall.callee != member.member ||
              hirCall.calleeType != member.memberType ||
              invocation.calleeType != hirCall.calleeType ||
              invocation.arguments.size() != arguments.size ||
              invocation.arguments.size() != hirCall.arguments.size() ||
              invocation.substitutions != zc::none || invocation.witnesses != zc::none ||
              invocation.raises != zc::none || invocation.receiver == zc::none ||
              invocation.receiverMode == zc::none || invocation.receiverAdjustment == zc::none ||
              !dispatchOwnerMatches || dispatch.fact.receiver == zc::none ||
              dispatch.fact.arguments.size() != arguments.size ||
              dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
              dispatch.fact.raises != zc::none ||
              !sameSpan(facts.calls().entries()[checkedCallSlot].value.sourceSpan,
                        ZC_ASSERT_NONNULL(callSpan)) ||
              !sameSpan(dispatch.fact.sourceSpan, ZC_ASSERT_NONNULL(callSpan)) ||
              !sameSpan(hirCall.sourceSpan, ZC_ASSERT_NONNULL(callSpan))) {
            return false;
          }
          const bool resultMatches = expectedUnitResult
                                         ? (expectedResultIsUnit(invocation.successType) &&
                                            expectedResultIsUnit(invocation.resultType) &&
                                            expectedResultIsUnit(dispatch.fact.successType) &&
                                            expectedResultIsUnit(dispatch.fact.resultType))
                                         : (invocation.successType == expectedResultType &&
                                            invocation.resultType == expectedResultType &&
                                            dispatch.fact.successType == expectedResultType &&
                                            dispatch.fact.resultType == expectedResultType);
          if (!resultMatches) return false;
          const auto& checkedReceiver = ZC_ASSERT_NONNULL(invocation.receiver);
          if (checkedReceiver.sourceNode != receiverNode ||
              checkedReceiver.sourceType != facts.nodeTypes().entries()[receiverTypeSlot].value ||
              hirCall.receiverSourceType != checkedReceiver.sourceType ||
              invocation.receiverMode == zc::none ||
              ZC_ASSERT_NONNULL(invocation.receiverMode) != expectedMode ||
              invocation.receiverAdjustment == zc::none ||
              ZC_ASSERT_NONNULL(invocation.receiverAdjustment).source !=
                  checkedReceiver.sourceType ||
              ZC_ASSERT_NONNULL(invocation.receiverAdjustment).destination !=
                  checkedReceiver.parameterType ||
              ZC_ASSERT_NONNULL(invocation.receiverAdjustment).steps.size() != 1 ||
              ZC_ASSERT_NONNULL(invocation.receiverAdjustment).steps[0] != expectedStep) {
            return false;
          }
          auto receiverParameter = semanticTypes.get(checkedReceiver.parameterType);
          if (!receiverParameter.is<type::SemanticTypeLookup>() ||
              !receiverParameter.get<type::SemanticTypeLookup>()
                   .data()
                   .is<type::semantic::ReferenceTypeData>() ||
              receiverParameter.get<type::SemanticTypeLookup>()
                      .data()
                      .get<type::semantic::ReferenceTypeData>()
                      .mutability != expectedMutability ||
              receiverParameter.get<type::SemanticTypeLookup>()
                      .data()
                      .get<type::semantic::ReferenceTypeData>()
                      .referent != checkedReceiver.sourceType ||
              hirCall.receiverType != checkedReceiver.parameterType) {
            return false;
          }
          const auto argumentNodes = tree.list(arguments);
          for (size_t argumentIndex = 0; argumentIndex < argumentNodes.size(); ++argumentIndex) {
            const auto argument = argumentNodes[argumentIndex];
            auto argumentTypeIndex = factIndex(facts.nodeTypes(), argument);
            auto literalIndex = factIndex(facts.literals(), argument);
            auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
            if (!tree.contains(argument) || !isScalarLiteral(tree.node(argument).kind) ||
                argumentTypeIndex == zc::none || literalIndex == zc::none ||
                argumentSpan == zc::none) {
              return false;
            }
            size_t argumentTypeSlot = 0;
            size_t literalSlot = 0;
            ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
            ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
            const auto& checkedArgument = invocation.arguments[argumentIndex];
            const auto& hirArgument = hirCall.arguments[argumentIndex];
            const auto& literal = facts.literals().entries()[literalSlot].value;
            const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
            bool constantMatches = false;
            ZC_IF_SOME(value, hirArgument.value) {
              constantMatches =
                  sameConstant(value, literal.literal, module, registries, semanticTypes);
            }
            if (checkedArgument.sourceNode != argument ||
                checkedArgument.sourceType != argumentType ||
                checkedArgument.parameterType != argumentType ||
                checkedArgument.adjustment != zc::none || literal.node != argument ||
                literal.type != argumentType || hirArgument.type != argumentType ||
                hirArgument.value == zc::none || hirArgument.parameter != zc::none ||
                !constantMatches ||
                !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan)) ||
                !sameSpan(hirArgument.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
              return false;
            }
          }
          return true;
        };
        if (!validateCallFact(setSourceCall, setCall, checker::checked::ReceiverMode::Mutable,
                              checker::checked::ReceiverAdjustmentStep::BorrowMutable,
                              type::semantic::Mutability::Mutable, setCall.resultType, true) ||
            !validateCallFact(getSourceCall, getCall, checker::checked::ReceiverMode::Shared,
                              checker::checked::ReceiverAdjustmentStep::BorrowShared,
                              type::semantic::Mutability::Const, function.resultType, false)) {
          return rejectDiscarded();
        }
        const ast::NodeId setCalleeSource(
            tree.node(setSourceCall).payload.words[ast::kCallExpressionCalleeWord]);
        const ast::NodeId getCalleeSource(
            tree.node(getSourceCall).payload.words[ast::kCallExpressionCalleeWord]);
        const ast::NodeId setReceiverSource(
            tree.node(setCalleeSource).payload.words[ast::kMemberExpressionObjectWord]);
        const ast::NodeId getReceiverSource(
            tree.node(getCalleeSource).payload.words[ast::kMemberExpressionObjectWord]);
        auto setReceiverSpan = bound.parsedModule().spanFor(tree.node(setReceiverSource).range);
        auto getReceiverSpan = bound.parsedModule().spanFor(tree.node(getReceiverSource).range);
        if (setReceiverSpan == zc::none || getReceiverSpan == zc::none ||
            !sameSpan(setReceiver.sourceSpan, ZC_ASSERT_NONNULL(setReceiverSpan)) ||
            !sameSpan(getReceiver.sourceSpan, ZC_ASSERT_NONNULL(getReceiverSpan))) {
          return rejectDiscarded();
        }
        nextFunction += 9;
        continue;
      }
    }
    // Scalar-local call: `let a: i32 = <literal>; return f(a);`. Fixed six-node
    // preorder: function F, body F+1, local F+2, scalar-literal initializer F+3,
    // return F+4, direct call F+5. The call's sole argument is the owner i32
    // local copied by value and allocates no extra node.
    if (sourceShapeMaybe != zc::none &&
        ZC_ASSERT_NONNULL(sourceShapeMaybe).returnsDirectScalarLocalCall) {
      const FunctionReturnShape& source = ZC_ASSERT_NONNULL(sourceShapeMaybe);
      const auto rejectScalarCall = [&]() {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      };
      const auto rejectScalarCallMissing = [&]() {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      };
      zc::Maybe<const HirLocalBinding&> scalarLocal;
      for (const auto& candidateLocal : candidate.impl->locals) {
        if (candidateLocal.node != hirId(expectedFunction + 2)) continue;
        if (scalarLocal != zc::none) return rejectScalarCall();
        scalarLocal = candidateLocal;
      }
      zc::Maybe<const HirScalarLiteralExpression&> initializerExpression;
      for (const auto& candidateExpression : candidate.impl->expressions) {
        if (candidateExpression.node != hirId(expectedFunction + 3)) continue;
        if (initializerExpression != zc::none) return rejectScalarCall();
        initializerExpression = candidateExpression;
      }
      zc::Maybe<const HirDirectCallExpression&> scalarCall;
      for (const auto& candidateCall : candidate.impl->calls) {
        if (candidateCall.node != hirId(expectedFunction + 5)) continue;
        if (scalarCall != zc::none) return rejectScalarCall();
        scalarCall = candidateCall;
      }
      zc::Maybe<const HirReturnStatement&> scalarReturn;
      for (const auto& candidateReturn : candidate.impl->returns) {
        if (candidateReturn.node != hirId(expectedFunction + 4)) continue;
        if (scalarReturn != zc::none) return rejectScalarCall();
        scalarReturn = candidateReturn;
      }
      if (scalarLocal == zc::none || initializerExpression == zc::none || scalarCall == zc::none ||
          scalarReturn == zc::none) {
        return rejectScalarCallMissing();
      }
      const auto& localRecord = ZC_ASSERT_NONNULL(scalarLocal);
      const auto& expression = ZC_ASSERT_NONNULL(initializerExpression);
      const auto& call = ZC_ASSERT_NONNULL(scalarCall);
      const auto& returnStatementRecord = ZC_ASSERT_NONNULL(scalarReturn);
      if (function.node != hirId(expectedFunction) || block.node != hirId(expectedFunction + 1) ||
          function.body != block.node || block.statements.size() != 2 ||
          block.statements[0] != localRecord.node ||
          block.statements[1] != returnStatementRecord.node ||
          localRecord.node != hirId(expectedFunction + 2) || localRecord.local != hirLocalId(1) ||
          localRecord.initializer != hirId(expectedFunction + 3) ||
          localRecord.initializerSpan == zc::none ||
          expression.node != hirId(expectedFunction + 3) || expression.type != localRecord.type ||
          expression.category != HirValueCategory::Value ||
          returnStatementRecord.node != hirId(expectedFunction + 4) ||
          returnStatementRecord.value != call.node ||
          returnStatementRecord.resultType != function.resultType ||
          call.node != hirId(expectedFunction + 5) || call.resultType != function.resultType ||
          function.receiver != zc::none || !typeExists(localRecord.type, semanticTypes) ||
          !typeExists(function.resultType, semanticTypes)) {
        return rejectScalarCall();
      }

      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), function.definition);
      auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
      if (sourceDefinitionIndex == zc::none || signaturePosition == zc::none ||
          rootPosition == zc::none) {
        return rejectScalarCallMissing();
      }
      size_t definitionSlot = 0;
      size_t signatureSlot = 0;
      size_t rootSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
      ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& signature = signatures.definitions[signatureSlot];
      const auto& root = signatures.roots[rootSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          !definitionBelongsToModule(sourceDefinition, definitions) ||
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
          !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
          !tree.contains(sourceDefinition.node) ||
          tree.node(sourceDefinition.node).kind != ast::SyntaxKind::FunctionDecl ||
          !signature.payload.variant().is<checker::signature::CallableSignature>() ||
          !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
        return rejectScalarCall();
      }
      const auto& callable =
          signature.payload.variant().get<checker::signature::CallableSignature>();
      auto expectedVisibility = visibility(root.visibility);
      auto expectedLinkage = linkage(callable);
      ast::NodeId initializer;
      ZC_IF_SOME(value, source.localInitializer) { initializer = value; }
      if (expectedVisibility == zc::none || expectedLinkage == zc::none || !source.returnsLocal ||
          source.localInitializer == zc::none || source.localWrites.size != 0 ||
          !tree.contains(source.value) ||
          tree.node(source.value).kind != ast::SyntaxKind::CallExpression ||
          !tree.contains(initializer) || !isScalarLiteral(tree.node(initializer).kind) ||
          signature.definition != function.definition ||
          signature.definitionKind != identity::DefinitionKind::Function ||
          root.canonicalDefinition != function.definition || root.sourceModule != module ||
          callable.receiver != zc::none || callable.raises != zc::none ||
          callable.success != function.resultType || callable.parameters.size() != 0 ||
          function.parameters.size() != 0 ||
          !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
          !sameSpan(function.sourceSpan, sourceDefinition.source) ||
          !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
          function.linkage != ZC_ASSERT_NONNULL(expectedLinkage)) {
        return rejectScalarCall();
      }
      const auto& sourceCall = tree.node(source.value);
      const ast::NodeId calleeNode(sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
      const ast::NodeList typeArguments{
          sourceCall.payload.words[ast::kCallExpressionTypeArgsFirstWord],
          sourceCall.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
      const ast::NodeList arguments{sourceCall.payload.words[ast::kCallExpressionArgsFirstWord],
                                    sourceCall.payload.words[ast::kCallExpressionArgsSizeWord]};
      if (!tree.contains(calleeNode) || tree.node(calleeNode).kind != ast::SyntaxKind::IdentExpr ||
          !tree.contains(typeArguments) || !typeArguments.empty() || !tree.contains(arguments) ||
          tree.list(arguments).size() != 1) {
        return rejectScalarCall();
      }
      const ast::NodeId argumentNode = tree.list(arguments)[0];
      if (!tree.contains(argumentNode) ||
          tree.node(argumentNode).kind != ast::SyntaxKind::IdentExpr) {
        return rejectScalarCall();
      }
      auto ownerBinding = ownerLocalBindingForPattern(definitions, source.localPattern, tree);
      auto argumentBinding = resolvedOwnerLocal(bound.bindings(), argumentNode);
      auto calleeTypeIndex = factIndex(facts.nodeTypes(), calleeNode);
      auto argumentTypeIndex = factIndex(facts.nodeTypes(), argumentNode);
      auto initializerTypeIndex = factIndex(facts.nodeTypes(), initializer);
      auto initializerLiteralIndex = factIndex(facts.literals(), initializer);
      auto checkedCallIndex = factIndex(facts.calls(), source.value);
      auto callKey = checkedNodeKey(tree, bound.parsedModule(), source.value);
      auto callSpan = bound.parsedModule().spanFor(sourceCall.range);
      auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
      auto argumentSpan = bound.parsedModule().spanFor(tree.node(argumentNode).range);
      if (ownerBinding == zc::none || argumentBinding == zc::none ||
          ownerBinding != argumentBinding ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(ownerBinding), source.localPattern,
                             tree) ||
          calleeTypeIndex == zc::none || argumentTypeIndex == zc::none ||
          initializerTypeIndex == zc::none || initializerLiteralIndex == zc::none ||
          checkedCallIndex == zc::none || callKey == zc::none || callSpan == zc::none ||
          initializerSpan == zc::none || argumentSpan == zc::none) {
        return rejectScalarCallMissing();
      }
      auto dispatchIndex = dispatchFactIndex(candidate.impl->checkedModule.dispatchFacts().facts(),
                                             ZC_ASSERT_NONNULL(callKey));
      if (dispatchIndex == zc::none) { return rejectScalarCallMissing(); }
      auto callee = resolvedDefinition(bound.bindings(), calleeNode);
      if (callee == zc::none) { return rejectScalarCallMissing(); }
      size_t calleeTypeSlot = 0;
      size_t argumentTypeSlot = 0;
      size_t initializerTypeSlot = 0;
      size_t initializerLiteralSlot = 0;
      size_t checkedCallSlot = 0;
      size_t dispatchSlot = 0;
      ZC_IF_SOME(value, calleeTypeIndex) { calleeTypeSlot = value; }
      ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
      ZC_IF_SOME(value, initializerTypeIndex) { initializerTypeSlot = value; }
      ZC_IF_SOME(value, initializerLiteralIndex) { initializerLiteralSlot = value; }
      ZC_IF_SOME(value, checkedCallIndex) { checkedCallSlot = value; }
      ZC_IF_SOME(value, dispatchIndex) { dispatchSlot = value; }
      const auto& checkedInitializer = facts.literals().entries()[initializerLiteralSlot].value;
      const auto& invocation = facts.calls().entries()[checkedCallSlot].value.invocation;
      const auto& selected = invocation.selected.variant();
      const auto& dispatch = candidate.impl->checkedModule.dispatchFacts().facts()[dispatchSlot];
      const auto& target = dispatch.fact.target.variant();
      const auto& transform = dispatch.fact.resultTransform.variant();
      const auto& argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
      bool dispatchOwnerMatches = false;
      ZC_IF_SOME(owner, dispatch.owner) { dispatchOwnerMatches = owner == function.definition; }
      bool callSpanMatches = false;
      bool dispatchSpanMatches = false;
      bool hirSpanMatches = false;
      ZC_IF_SOME(value, callSpan) {
        callSpanMatches =
            sameSpan(facts.calls().entries()[checkedCallSlot].value.sourceSpan, value);
        dispatchSpanMatches = sameSpan(dispatch.fact.sourceSpan, value);
        hirSpanMatches = sameSpan(call.sourceSpan, value);
      }
      if (expression.type != facts.nodeTypes().entries()[initializerTypeSlot].value ||
          checkedInitializer.node != initializer || checkedInitializer.type != expression.type ||
          !sameConstant(expression.value, checkedInitializer.literal, module, registries,
                        semanticTypes) ||
          !sameSpan(checkedInitializer.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan)) ||
          !sameSpan(localRecord.sourceSpan, ZC_ASSERT_NONNULL(bound.parsedModule().spanFor(
                                                tree.node(source.localPattern).range))) ||
          !sameSpan(ZC_ASSERT_NONNULL(localRecord.initializerSpan),
                    ZC_ASSERT_NONNULL(initializerSpan)) ||
          call.callee != ZC_ASSERT_NONNULL(callee) ||
          call.calleeType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
          call.arguments.size() != 1 || !selected.is<checker::checked::DirectCallable>() ||
          selected.get<checker::checked::DirectCallable>().callee != call.callee ||
          !target.is<checker::dispatch::DirectTarget>() ||
          target.get<checker::dispatch::DirectTarget>().callee != call.callee ||
          !transform.is<checker::dispatch::IdentityResultTransform>() ||
          invocation.calleeType != call.calleeType ||
          invocation.successType != function.resultType ||
          invocation.resultType != function.resultType || invocation.receiver != zc::none ||
          invocation.receiverMode != zc::none || invocation.receiverAdjustment != zc::none ||
          invocation.arguments.size() != 1 || invocation.substitutions != zc::none ||
          invocation.witnesses != zc::none || invocation.raises != zc::none ||
          !dispatchOwnerMatches || dispatch.fact.receiver != zc::none ||
          dispatch.fact.arguments.size() != 1 || dispatch.fact.successType != function.resultType ||
          dispatch.fact.resultType != function.resultType ||
          dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
          dispatch.fact.raises != zc::none || !callSpanMatches || !dispatchSpanMatches ||
          !hirSpanMatches) {
        return rejectScalarCall();
      }
      const auto& checkedArgument = invocation.arguments[0];
      const auto& dispatchArgument = dispatch.fact.arguments[0];
      const auto& hirArgument = call.arguments[0];
      if (checkedArgument.sourceNode != argumentNode ||
          checkedArgument.sourceType != argumentType ||
          checkedArgument.parameterType != argumentType || checkedArgument.adjustment != zc::none ||
          !sameNodeKey(
              dispatchArgument.sourceNode,
              ZC_ASSERT_NONNULL(checkedNodeKey(tree, bound.parsedModule(), argumentNode))) ||
          dispatchArgument.sourceType != argumentType ||
          dispatchArgument.parameterType != argumentType ||
          dispatchArgument.adjustment != zc::none || hirArgument.type != argumentType ||
          argumentType != expression.type || argumentType != localRecord.type ||
          hirArgument.value != zc::none || hirArgument.parameter != zc::none ||
          hirArgument.local != hirLocalId(1) ||
          !sameSpan(hirArgument.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
        return rejectScalarCall();
      }
      nextFunction += 6;
      continue;
    }
    // By-value aggregate call: `let p: P = P { .. }; return f(p);`. Fixed
    // six-node preorder: function F, body F+1, local F+2, aggregate
    // initializer F+3, return F+4, direct call F+5. The call's sole argument is
    // the owner local copied by value and allocates no extra node.
    if (sourceShapeMaybe != zc::none &&
        ZC_ASSERT_NONNULL(sourceShapeMaybe).returnsDirectAggregateCall) {
      const FunctionReturnShape& source = ZC_ASSERT_NONNULL(sourceShapeMaybe);
      const auto rejectAggregateCall = [&]() {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      };
      const auto rejectAggregateCallMissing = [&]() {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      };
      zc::Maybe<const HirLocalBinding&> aggregateLocal;
      for (const auto& candidateLocal : candidate.impl->locals) {
        if (candidateLocal.node != hirId(expectedFunction + 2)) continue;
        if (aggregateLocal != zc::none) return rejectAggregateCall();
        aggregateLocal = candidateLocal;
      }
      zc::Maybe<const HirNominalAggregateExpression&> aggregateRecord;
      for (const auto& candidateAggregate : candidate.impl->aggregates) {
        if (candidateAggregate.node != hirId(expectedFunction + 3)) continue;
        if (aggregateRecord != zc::none) return rejectAggregateCall();
        aggregateRecord = candidateAggregate;
      }
      zc::Maybe<const HirDirectCallExpression&> aggregateCall;
      for (const auto& candidateCall : candidate.impl->calls) {
        if (candidateCall.node != hirId(expectedFunction + 5)) continue;
        if (aggregateCall != zc::none) return rejectAggregateCall();
        aggregateCall = candidateCall;
      }
      zc::Maybe<const HirReturnStatement&> aggregateReturn;
      for (const auto& candidateReturn : candidate.impl->returns) {
        if (candidateReturn.node != hirId(expectedFunction + 4)) continue;
        if (aggregateReturn != zc::none) return rejectAggregateCall();
        aggregateReturn = candidateReturn;
      }
      if (aggregateLocal == zc::none || aggregateRecord == zc::none || aggregateCall == zc::none ||
          aggregateReturn == zc::none) {
        return rejectAggregateCallMissing();
      }
      const auto& localRecord = ZC_ASSERT_NONNULL(aggregateLocal);
      const auto& aggregate = ZC_ASSERT_NONNULL(aggregateRecord);
      const auto& call = ZC_ASSERT_NONNULL(aggregateCall);
      const auto& returnStatementRecord = ZC_ASSERT_NONNULL(aggregateReturn);
      if (function.node != hirId(expectedFunction) || block.node != hirId(expectedFunction + 1) ||
          function.body != block.node || block.statements.size() != 2 ||
          block.statements[0] != localRecord.node ||
          block.statements[1] != returnStatementRecord.node ||
          localRecord.node != hirId(expectedFunction + 2) || localRecord.local != hirLocalId(1) ||
          localRecord.initializer != hirId(expectedFunction + 3) ||
          localRecord.initializerSpan == zc::none ||
          aggregate.node != hirId(expectedFunction + 3) || aggregate.type != localRecord.type ||
          aggregate.category != HirValueCategory::Value ||
          returnStatementRecord.node != hirId(expectedFunction + 4) ||
          returnStatementRecord.value != call.node ||
          returnStatementRecord.resultType != function.resultType ||
          call.node != hirId(expectedFunction + 5) || call.resultType != function.resultType ||
          function.receiver != zc::none || !typeExists(localRecord.type, semanticTypes) ||
          !typeExists(function.resultType, semanticTypes)) {
        return rejectAggregateCall();
      }

      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), function.definition);
      auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
      if (sourceDefinitionIndex == zc::none || signaturePosition == zc::none ||
          rootPosition == zc::none) {
        return rejectAggregateCallMissing();
      }
      size_t definitionSlot = 0;
      size_t signatureSlot = 0;
      size_t rootSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
      ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& signature = signatures.definitions[signatureSlot];
      const auto& root = signatures.roots[rootSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          !definitionBelongsToModule(sourceDefinition, definitions) ||
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
          !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
          !tree.contains(sourceDefinition.node) ||
          tree.node(sourceDefinition.node).kind != ast::SyntaxKind::FunctionDecl ||
          !signature.payload.variant().is<checker::signature::CallableSignature>() ||
          !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
        return rejectAggregateCall();
      }
      const auto& callable =
          signature.payload.variant().get<checker::signature::CallableSignature>();
      auto expectedVisibility = visibility(root.visibility);
      auto expectedLinkage = linkage(callable);
      ast::NodeId initializer;
      ZC_IF_SOME(value, source.localInitializer) { initializer = value; }
      if (expectedVisibility == zc::none || expectedLinkage == zc::none || !source.returnsLocal ||
          source.localInitializer == zc::none || source.localWrites.size != 0 ||
          !tree.contains(source.value) ||
          tree.node(source.value).kind != ast::SyntaxKind::CallExpression ||
          !tree.contains(initializer) ||
          tree.node(initializer).kind != ast::SyntaxKind::StructLiteralExpr ||
          signature.definition != function.definition ||
          signature.definitionKind != identity::DefinitionKind::Function ||
          root.canonicalDefinition != function.definition || root.sourceModule != module ||
          callable.receiver != zc::none || callable.raises != zc::none ||
          callable.success != function.resultType || callable.parameters.size() != 0 ||
          function.parameters.size() != 0 ||
          !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
          !sameSpan(function.sourceSpan, sourceDefinition.source) ||
          !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
          function.linkage != ZC_ASSERT_NONNULL(expectedLinkage)) {
        return rejectAggregateCall();
      }
      const auto& sourceCall = tree.node(source.value);
      const ast::NodeId calleeNode(sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
      const ast::NodeList typeArguments{
          sourceCall.payload.words[ast::kCallExpressionTypeArgsFirstWord],
          sourceCall.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
      const ast::NodeList arguments{sourceCall.payload.words[ast::kCallExpressionArgsFirstWord],
                                    sourceCall.payload.words[ast::kCallExpressionArgsSizeWord]};
      if (!tree.contains(calleeNode) || tree.node(calleeNode).kind != ast::SyntaxKind::IdentExpr ||
          !tree.contains(typeArguments) || !typeArguments.empty() || !tree.contains(arguments) ||
          tree.list(arguments).size() != 1) {
        return rejectAggregateCall();
      }
      const ast::NodeId argumentNode = tree.list(arguments)[0];
      if (!tree.contains(argumentNode) ||
          tree.node(argumentNode).kind != ast::SyntaxKind::IdentExpr) {
        return rejectAggregateCall();
      }
      auto ownerBinding = ownerLocalBindingForPattern(definitions, source.localPattern, tree);
      auto argumentBinding = resolvedOwnerLocal(bound.bindings(), argumentNode);
      auto calleeTypeIndex = factIndex(facts.nodeTypes(), calleeNode);
      auto argumentTypeIndex = factIndex(facts.nodeTypes(), argumentNode);
      auto initializerTypeIndex = factIndex(facts.nodeTypes(), initializer);
      auto initializerAggregateIndex = factIndex(facts.aggregates(), initializer);
      auto checkedCallIndex = factIndex(facts.calls(), source.value);
      auto callKey = checkedNodeKey(tree, bound.parsedModule(), source.value);
      auto callSpan = bound.parsedModule().spanFor(sourceCall.range);
      auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
      auto argumentSpan = bound.parsedModule().spanFor(tree.node(argumentNode).range);
      if (ownerBinding == zc::none || argumentBinding == zc::none ||
          ownerBinding != argumentBinding ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(ownerBinding), source.localPattern,
                             tree) ||
          calleeTypeIndex == zc::none || argumentTypeIndex == zc::none ||
          initializerTypeIndex == zc::none || initializerAggregateIndex == zc::none ||
          checkedCallIndex == zc::none || callKey == zc::none || callSpan == zc::none ||
          initializerSpan == zc::none || argumentSpan == zc::none) {
        return rejectAggregateCallMissing();
      }
      auto dispatchIndex = dispatchFactIndex(candidate.impl->checkedModule.dispatchFacts().facts(),
                                             ZC_ASSERT_NONNULL(callKey));
      if (dispatchIndex == zc::none) { return rejectAggregateCallMissing(); }
      auto callee = resolvedDefinition(bound.bindings(), calleeNode);
      if (callee == zc::none) { return rejectAggregateCallMissing(); }
      size_t calleeTypeSlot = 0;
      size_t argumentTypeSlot = 0;
      size_t initializerTypeSlot = 0;
      size_t initializerAggregateSlot = 0;
      size_t checkedCallSlot = 0;
      size_t dispatchSlot = 0;
      ZC_IF_SOME(value, calleeTypeIndex) { calleeTypeSlot = value; }
      ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
      ZC_IF_SOME(value, initializerTypeIndex) { initializerTypeSlot = value; }
      ZC_IF_SOME(value, initializerAggregateIndex) { initializerAggregateSlot = value; }
      ZC_IF_SOME(value, checkedCallIndex) { checkedCallSlot = value; }
      ZC_IF_SOME(value, dispatchIndex) { dispatchSlot = value; }
      const auto& checkedInitializer = facts.aggregates().entries()[initializerAggregateSlot].value;
      const auto& invocation = facts.calls().entries()[checkedCallSlot].value.invocation;
      const auto& selected = invocation.selected.variant();
      const auto& dispatch = candidate.impl->checkedModule.dispatchFacts().facts()[dispatchSlot];
      const auto& target = dispatch.fact.target.variant();
      const auto& transform = dispatch.fact.resultTransform.variant();
      const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
      bool dispatchOwnerMatches = false;
      ZC_IF_SOME(owner, dispatch.owner) { dispatchOwnerMatches = owner == function.definition; }
      bool callSpanMatches = false;
      bool dispatchSpanMatches = false;
      bool hirSpanMatches = false;
      ZC_IF_SOME(value, callSpan) {
        callSpanMatches =
            sameSpan(facts.calls().entries()[checkedCallSlot].value.sourceSpan, value);
        dispatchSpanMatches = sameSpan(dispatch.fact.sourceSpan, value);
        hirSpanMatches = sameSpan(call.sourceSpan, value);
      }
      if (aggregate.type != facts.nodeTypes().entries()[initializerTypeSlot].value ||
          checkedInitializer.node != initializer ||
          checkedInitializer.resultType != aggregate.type ||
          !sameSpan(checkedInitializer.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan)) ||
          !sameSpan(localRecord.sourceSpan, ZC_ASSERT_NONNULL(bound.parsedModule().spanFor(
                                                tree.node(source.localPattern).range))) ||
          !sameSpan(ZC_ASSERT_NONNULL(localRecord.initializerSpan),
                    ZC_ASSERT_NONNULL(initializerSpan)) ||
          call.callee != ZC_ASSERT_NONNULL(callee) ||
          call.calleeType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
          call.arguments.size() != 1 || !selected.is<checker::checked::DirectCallable>() ||
          selected.get<checker::checked::DirectCallable>().callee != call.callee ||
          !target.is<checker::dispatch::DirectTarget>() ||
          target.get<checker::dispatch::DirectTarget>().callee != call.callee ||
          !transform.is<checker::dispatch::IdentityResultTransform>() ||
          invocation.calleeType != call.calleeType ||
          invocation.successType != function.resultType ||
          invocation.resultType != function.resultType || invocation.receiver != zc::none ||
          invocation.receiverMode != zc::none || invocation.receiverAdjustment != zc::none ||
          invocation.arguments.size() != 1 || invocation.substitutions != zc::none ||
          invocation.witnesses != zc::none || invocation.raises != zc::none ||
          !dispatchOwnerMatches || dispatch.fact.receiver != zc::none ||
          dispatch.fact.arguments.size() != 1 || dispatch.fact.successType != function.resultType ||
          dispatch.fact.resultType != function.resultType ||
          dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
          dispatch.fact.raises != zc::none || !callSpanMatches || !dispatchSpanMatches ||
          !hirSpanMatches) {
        return rejectAggregateCall();
      }
      const auto& checkedArgument = invocation.arguments[0];
      const auto& dispatchArgument = dispatch.fact.arguments[0];
      const auto& hirArgument = call.arguments[0];
      if (checkedArgument.sourceNode != argumentNode ||
          checkedArgument.sourceType != argumentType ||
          checkedArgument.parameterType != argumentType || checkedArgument.adjustment != zc::none ||
          !sameNodeKey(
              dispatchArgument.sourceNode,
              ZC_ASSERT_NONNULL(checkedNodeKey(tree, bound.parsedModule(), argumentNode))) ||
          dispatchArgument.sourceType != argumentType ||
          dispatchArgument.parameterType != argumentType ||
          dispatchArgument.adjustment != zc::none || hirArgument.type != argumentType ||
          argumentType != aggregate.type || argumentType != localRecord.type ||
          hirArgument.value != zc::none || hirArgument.parameter != zc::none ||
          hirArgument.local != hirLocalId(1) ||
          !sameSpan(hirArgument.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
        return rejectAggregateCall();
      }
      nextFunction += 6;
      continue;
    }
    // number of extra HIR nodes the unsafe block contributes (0 when absent, 1
    // when present and valid), or zc::none when present but invalid.
    auto verifyUnsafeBlock = [&](const FunctionReturnShape& source, uint32_t baseIncrement,
                                 HirNodeId bodyNode) -> zc::Maybe<uint32_t> {
      if (source.unsafeBlock == zc::none) return zc::Maybe<uint32_t>(0u);
      const auto expectedUnsafeBlock = hirId(expectedFunction + baseIncrement);
      if (function.unsafeBlock != expectedUnsafeBlock) return zc::none;
      zc::Maybe<const HirUnsafeBlockExpression&> unsafeBlock;
      for (const auto& candidateBlock : candidate.impl->unsafeBlocks) {
        if (candidateBlock.node != expectedUnsafeBlock) continue;
        if (unsafeBlock != zc::none) return zc::none;
        unsafeBlock = candidateBlock;
      }
      if (unsafeBlock == zc::none) return zc::none;
      ZC_IF_SOME(block, unsafeBlock) {
        auto sourceUnsafeSpan = bound.parsedModule().spanFor(
            bound.tree().node(ZC_ASSERT_NONNULL(source.unsafeBlock)).range);
        if (block.body != bodyNode || block.type != function.resultType ||
            sourceUnsafeSpan == zc::none ||
            !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(sourceUnsafeSpan))) {
          return zc::none;
        }
      }
      return zc::Maybe<uint32_t>(1u);
    };
    zc::Maybe<const HirLocalBinding&> localBinding;
    for (const auto& local : candidate.impl->locals) {
      if (local.node != hirId(expectedFunction + 2)) continue;
      if (localBinding != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      localBinding = local;
    }
    const bool returnsLocal = localBinding != zc::none;
    bool localHasInitializer = false;
    HirNodeId materializedNode;
    ZC_IF_SOME(local, localBinding) {
      ZC_IF_SOME(initializer, local.initializer) {
        materializedNode = initializer;
        localHasInitializer = true;
      }
    }
    size_t functionLocalWriteCount = 0;
    if (returnsLocal && block.statements.size() >= 2) {
      functionLocalWriteCount = block.statements.size() - 2;
    }
    zc::Maybe<const HirLocalWriteStatement&> localWrite;
    if (returnsLocal) {
      const auto expectedWrite = hirId(expectedFunction + (localHasInitializer ? 4 : 3));
      for (const auto& write : candidate.impl->localWrites) {
        if (write.node != expectedWrite) continue;
        if (localWrite != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        localWrite = write;
      }
    }
    const bool hasLocalWrite = functionLocalWriteCount != 0;
    const auto returnNode =
        hirId(expectedFunction +
              (returnsLocal ? (localHasInitializer ? 4 : 3) + functionLocalWriteCount * 2 : 2));
    const auto valueNode =
        hirId(expectedFunction +
              (returnsLocal ? (localHasInitializer ? 5 : 4) + functionLocalWriteCount * 2 : 3));
    if (!localHasInitializer) {
      materializedNode = hasLocalWrite ? hirId(expectedFunction + 4) : valueNode;
    }
    zc::Maybe<const HirScalarLiteralExpression&> literalExpression;
    zc::Maybe<const HirDirectCallExpression&> directCall;
    for (const auto& expression : candidate.impl->expressions) {
      if (expression.node != materializedNode) continue;
      if (literalExpression != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      literalExpression = expression;
    }
    for (const auto& call : candidate.impl->calls) {
      if (call.node != materializedNode) continue;
      if (directCall != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      directCall = call;
    }
    zc::Maybe<const HirScalarLiteralExpression&> writeLiteral;
    zc::Maybe<const HirParameterReferenceExpression&> writeParameter;
    zc::Maybe<const HirPrimitiveBinaryExpression&> writeBinary;
    if (hasLocalWrite) {
      const auto expectedValue = hirId(expectedFunction + (localHasInitializer ? 5 : 4));
      for (const auto& expression : candidate.impl->expressions) {
        if (expression.node != expectedValue) continue;
        if (writeLiteral != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        writeLiteral = expression;
      }
      for (const auto& operation : candidate.impl->primitiveBinaryOperations) {
        if (operation.node != expectedValue) continue;
        if (writeBinary != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        writeBinary = operation;
      }
      for (const auto& reference : candidate.impl->parameterReferences) {
        if (reference.node != expectedValue) continue;
        if (writeParameter != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        writeParameter = reference;
      }
    }
    // The first write's value is a scalar literal, a parameter reference, or a
    // primitive binary, never more than one. Downstream branches consume
    // whichever is populated.
    const bool hasFirstWriteValue =
        writeLiteral != zc::none || writeParameter != zc::none || writeBinary != zc::none;
    bool writesMatchBlock = true;
    if (hasLocalWrite) {
      for (size_t writeIndex = 0; writeIndex < functionLocalWriteCount; ++writeIndex) {
        const auto expectedWrite =
            hirId(expectedFunction + (localHasInitializer ? 4 : 3) + writeIndex * 2);
        bool found = false;
        for (const auto& write : candidate.impl->localWrites) {
          if (write.node != expectedWrite) continue;
          if (found) writesMatchBlock = false;
          found = true;
        }
        if (!found || block.statements[writeIndex + 1] != expectedWrite) {
          writesMatchBlock = false;
        }
      }
    }
    zc::Maybe<const HirLocalReferenceExpression&> localReference;
    for (const auto& reference : candidate.impl->localReferences) {
      if (reference.node != valueNode) continue;
      if (localReference != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      localReference = reference;
    }
    zc::Maybe<const HirLocalBorrowExpression&> localBorrow;
    for (const auto& borrow : candidate.impl->localBorrows) {
      if (borrow.node != valueNode) continue;
      if (localBorrow != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      localBorrow = borrow;
    }
    zc::Maybe<const HirReceiverCallExpression&> receiverCall;
    const auto receiverReferenceNode = hirId(valueNode.ordinal());
    const auto receiverCallNode = hirId(valueNode.ordinal() + 1);
    for (const auto& call : candidate.impl->receiverCalls) {
      if (call.node != receiverCallNode) continue;
      if (receiverCall != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      receiverCall = call;
    }
    if (receiverCall != zc::none) {
      zc::Maybe<const HirNominalAggregateExpression&> receiverAggregate;
      for (const auto& aggregate : candidate.impl->aggregates) {
        if (aggregate.node != materializedNode) continue;
        if (receiverAggregate != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        receiverAggregate = aggregate;
      }
      const bool sharedReceiverCall =
          ZC_ASSERT_NONNULL(receiverCall).receiverMode == checker::checked::ReceiverMode::Shared;
      const auto expectedReceiverMode = sharedReceiverCall
                                            ? checker::checked::ReceiverMode::Shared
                                            : checker::checked::ReceiverMode::Mutable;
      const auto expectedReceiverStep =
          sharedReceiverCall ? checker::checked::ReceiverAdjustmentStep::BorrowShared
                             : checker::checked::ReceiverAdjustmentStep::BorrowMutable;
      const auto expectedReceiverMutability = sharedReceiverCall
                                                  ? type::semantic::Mutability::Const
                                                  : type::semantic::Mutability::Mutable;
      if (!returnsLocal || !localHasInitializer || hasLocalWrite || directCall != zc::none ||
          localBinding == zc::none || localReference == zc::none || literalExpression != zc::none ||
          receiverAggregate == zc::none || function.node != hirId(expectedFunction) ||
          block.node != hirId(expectedFunction + 1) ||
          ZC_ASSERT_NONNULL(localBinding).node != hirId(expectedFunction + 2) ||
          ZC_ASSERT_NONNULL(localBinding).initializer != hirId(expectedFunction + 3) ||
          ZC_ASSERT_NONNULL(localBinding).initializerSpan == zc::none ||
          ZC_ASSERT_NONNULL(receiverAggregate).node != hirId(expectedFunction + 3) ||
          ZC_ASSERT_NONNULL(receiverAggregate).type != ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(receiverAggregate).category != HirValueCategory::Value ||
          returnStatement.node != hirId(expectedFunction + 4) ||
          ZC_ASSERT_NONNULL(localReference).node != receiverReferenceNode ||
          returnStatement.value != ZC_ASSERT_NONNULL(receiverCall).node ||
          ZC_ASSERT_NONNULL(receiverCall).receiver != receiverReferenceNode ||
          function.body != block.node || block.statements.size() != 2 ||
          block.statements[0] != ZC_ASSERT_NONNULL(localBinding).node ||
          block.statements[1] != returnStatement.node ||
          ZC_ASSERT_NONNULL(localReference).local != ZC_ASSERT_NONNULL(localBinding).local ||
          ZC_ASSERT_NONNULL(localReference).type != ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(localReference).category != HirValueCategory::Place ||
          ZC_ASSERT_NONNULL(receiverCall).receiverSourceType !=
              ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(receiverCall).resultType != function.resultType ||
          ZC_ASSERT_NONNULL(receiverCall).receiverMode != expectedReceiverMode ||
          ZC_ASSERT_NONNULL(receiverCall).receiverAdjustments.size() != 1 ||
          ZC_ASSERT_NONNULL(receiverCall).receiverAdjustments[0] != expectedReceiverStep ||
          !typeExists(ZC_ASSERT_NONNULL(localBinding).type, semanticTypes) ||
          !typeExists(function.resultType, semanticTypes)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto receiverParameter = semanticTypes.get(ZC_ASSERT_NONNULL(receiverCall).receiverType);
      if (!receiverParameter.is<type::SemanticTypeLookup>() ||
          !receiverParameter.get<type::SemanticTypeLookup>()
               .data()
               .is<type::semantic::ReferenceTypeData>() ||
          receiverParameter.get<type::SemanticTypeLookup>()
                  .data()
                  .get<type::semantic::ReferenceTypeData>()
                  .mutability != expectedReceiverMutability ||
          receiverParameter.get<type::SemanticTypeLookup>()
                  .data()
                  .get<type::semantic::ReferenceTypeData>()
                  .referent != ZC_ASSERT_NONNULL(receiverCall).receiverSourceType) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }

      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), function.definition);
      auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
      if (sourceDefinitionIndex == zc::none || signaturePosition == zc::none ||
          rootPosition == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t definitionSlot = 0;
      size_t signatureSlot = 0;
      size_t rootSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
      ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& signature = signatures.definitions[signatureSlot];
      const auto& root = signatures.roots[rootSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          !definitionBelongsToModule(sourceDefinition, definitions) ||
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
          !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
          !tree.contains(sourceDefinition.node) ||
          tree.node(sourceDefinition.node).kind != ast::SyntaxKind::FunctionDecl ||
          !signature.payload.variant().is<checker::signature::CallableSignature>() ||
          !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const auto& callable =
          signature.payload.variant().get<checker::signature::CallableSignature>();
      auto expectedVisibility = visibility(root.visibility);
      auto expectedLinkage = linkage(callable);
      auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (expectedVisibility == zc::none || expectedLinkage == zc::none ||
          sourceShape == zc::none || signature.definition != function.definition ||
          signature.definitionKind != identity::DefinitionKind::Function ||
          root.canonicalDefinition != function.definition || root.sourceModule != module ||
          callable.receiver != zc::none || callable.raises != zc::none ||
          callable.success != function.resultType ||
          !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
          !sameSpan(function.sourceSpan, sourceDefinition.source) ||
          !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
          function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
          function.parameters.size() != callable.parameters.size()) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      FunctionReturnShape source = zc::mv(sourceShape).orDefault(FunctionReturnShape{});
      if (!source.returnsLocal || !source.returnsReceiverCall ||
          source.localInitializer == zc::none || source.localWrites.size != 0 ||
          !tree.contains(source.value) ||
          tree.node(source.value).kind != ast::SyntaxKind::CallExpression) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ast::NodeId initializer;
      ZC_IF_SOME(value, source.localInitializer) { initializer = value; }
      const auto& sourceCall = tree.node(source.value);
      const ast::NodeId calleeNode(sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
      const ast::NodeList arguments{sourceCall.payload.words[ast::kCallExpressionArgsFirstWord],
                                    sourceCall.payload.words[ast::kCallExpressionArgsSizeWord]};
      if (!tree.contains(initializer) ||
          tree.node(initializer).kind != ast::SyntaxKind::StructLiteralExpr ||
          !tree.contains(calleeNode) ||
          tree.node(calleeNode).kind != ast::SyntaxKind::MemberExpression ||
          static_cast<ast::MemberAccessKind>(
              tree.node(calleeNode).payload.words[ast::kMemberExpressionAccessWord]) !=
              ast::MemberAccessKind::Dot ||
          !tree.contains(arguments)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const ast::NodeId receiverNode(
          tree.node(calleeNode).payload.words[ast::kMemberExpressionObjectWord]);
      auto ownerBinding = ownerLocalBindingForPattern(definitions, source.localPattern, tree);
      auto receiverBinding = resolvedOwnerLocal(bound.bindings(), receiverNode);
      auto initializerTypeIndex = factIndex(facts.nodeTypes(), initializer);
      auto initializerAggregateIndex = factIndex(facts.aggregates(), initializer);
      auto receiverTypeIndex = factIndex(facts.nodeTypes(), receiverNode);
      auto calleeTypeIndex = factIndex(facts.nodeTypes(), calleeNode);
      auto memberIndex = factIndex(facts.members(), calleeNode);
      auto checkedCallIndex = factIndex(facts.calls(), source.value);
      auto callKey = checkedNodeKey(tree, bound.parsedModule(), source.value);
      auto callSpan = bound.parsedModule().spanFor(sourceCall.range);
      auto receiverSpan = bound.parsedModule().spanFor(tree.node(receiverNode).range);
      auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
      if (!tree.contains(receiverNode) ||
          tree.node(receiverNode).kind != ast::SyntaxKind::IdentExpr || ownerBinding == zc::none ||
          receiverBinding == zc::none || ownerBinding != receiverBinding ||
          initializerTypeIndex == zc::none || initializerAggregateIndex == zc::none ||
          receiverTypeIndex == zc::none || calleeTypeIndex == zc::none || memberIndex == zc::none ||
          checkedCallIndex == zc::none || callKey == zc::none || callSpan == zc::none ||
          receiverSpan == zc::none || initializerSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(ownerBinding), source.localPattern,
                             tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      auto dispatchIndex = dispatchFactIndex(candidate.impl->checkedModule.dispatchFacts().facts(),
                                             ZC_ASSERT_NONNULL(callKey));
      if (dispatchIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t initializerTypeSlot = 0;
      size_t initializerAggregateSlot = 0;
      size_t receiverTypeSlot = 0;
      size_t calleeTypeSlot = 0;
      size_t memberSlot = 0;
      size_t checkedCallSlot = 0;
      size_t dispatchSlot = 0;
      ZC_IF_SOME(value, initializerTypeIndex) { initializerTypeSlot = value; }
      ZC_IF_SOME(value, initializerAggregateIndex) { initializerAggregateSlot = value; }
      ZC_IF_SOME(value, receiverTypeIndex) { receiverTypeSlot = value; }
      ZC_IF_SOME(value, calleeTypeIndex) { calleeTypeSlot = value; }
      ZC_IF_SOME(value, memberIndex) { memberSlot = value; }
      ZC_IF_SOME(value, checkedCallIndex) { checkedCallSlot = value; }
      ZC_IF_SOME(value, dispatchIndex) { dispatchSlot = value; }
      const auto& checkedInitializer = facts.aggregates().entries()[initializerAggregateSlot].value;
      const auto& member = facts.members().entries()[memberSlot].value;
      const auto& invocation = facts.calls().entries()[checkedCallSlot].value.invocation;
      const auto& selected = invocation.selected.variant();
      const auto& dispatch = candidate.impl->checkedModule.dispatchFacts().facts()[dispatchSlot];
      const auto& target = dispatch.fact.target.variant();
      const auto& transform = dispatch.fact.resultTransform.variant();
      bool dispatchOwnerMatches = false;
      ZC_IF_SOME(owner, dispatch.owner) { dispatchOwnerMatches = owner == function.definition; }
      if (ZC_ASSERT_NONNULL(receiverAggregate).type !=
              facts.nodeTypes().entries()[initializerTypeSlot].value ||
          checkedInitializer.node != initializer ||
          checkedInitializer.resultType != ZC_ASSERT_NONNULL(localBinding).type ||
          !sameSpan(checkedInitializer.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan)) ||
          !sameSpan(ZC_ASSERT_NONNULL(localBinding).sourceSpan,
                    ZC_ASSERT_NONNULL(
                        bound.parsedModule().spanFor(tree.node(source.localPattern).range))) ||
          !sameSpan(ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(localBinding).initializerSpan),
                    ZC_ASSERT_NONNULL(initializerSpan)) ||
          !sameSpan(ZC_ASSERT_NONNULL(localReference).sourceSpan,
                    ZC_ASSERT_NONNULL(receiverSpan)) ||
          !selected.is<checker::checked::ConcreteMethodCallable>() ||
          !target.is<checker::dispatch::ConcreteMethodTarget>() ||
          !transform.is<checker::dispatch::IdentityResultTransform>() ||
          selected.get<checker::checked::ConcreteMethodCallable>().method != member.member ||
          target.get<checker::dispatch::ConcreteMethodTarget>().method != member.member ||
          member.node != calleeNode ||
          member.receiverType != facts.nodeTypes().entries()[receiverTypeSlot].value ||
          member.memberType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
          member.adjustment != zc::none ||
          ZC_ASSERT_NONNULL(receiverCall).callee != member.member ||
          ZC_ASSERT_NONNULL(receiverCall).calleeType != member.memberType ||
          invocation.calleeType != ZC_ASSERT_NONNULL(receiverCall).calleeType ||
          invocation.successType != function.resultType ||
          invocation.resultType != function.resultType || invocation.receiver == zc::none ||
          invocation.receiverMode == zc::none || invocation.receiverAdjustment == zc::none ||
          invocation.arguments.size() != arguments.size || invocation.substitutions != zc::none ||
          invocation.witnesses != zc::none || invocation.raises != zc::none ||
          !dispatchOwnerMatches || dispatch.fact.receiver == zc::none ||
          dispatch.fact.arguments.size() != arguments.size ||
          dispatch.fact.successType != function.resultType ||
          dispatch.fact.resultType != function.resultType ||
          dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
          dispatch.fact.raises != zc::none ||
          !sameSpan(facts.calls().entries()[checkedCallSlot].value.sourceSpan,
                    ZC_ASSERT_NONNULL(callSpan)) ||
          !sameSpan(dispatch.fact.sourceSpan, ZC_ASSERT_NONNULL(callSpan)) ||
          !sameSpan(ZC_ASSERT_NONNULL(receiverCall).sourceSpan, ZC_ASSERT_NONNULL(callSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      ZC_IF_SOME(receiver, invocation.receiver) {
        ZC_IF_SOME(mode, invocation.receiverMode) {
          ZC_IF_SOME(adjustment, invocation.receiverAdjustment) {
            if (receiver.sourceNode != receiverNode ||
                receiver.sourceType != ZC_ASSERT_NONNULL(receiverCall).receiverSourceType ||
                receiver.parameterType != ZC_ASSERT_NONNULL(receiverCall).receiverType ||
                receiver.adjustment != zc::none ||
                mode != ZC_ASSERT_NONNULL(receiverCall).receiverMode ||
                adjustment.source != ZC_ASSERT_NONNULL(receiverCall).receiverSourceType ||
                adjustment.destination != ZC_ASSERT_NONNULL(receiverCall).receiverType ||
                adjustment.steps.size() != 1 || adjustment.steps[0] != expectedReceiverStep) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
          }
        }
      }
      const auto argumentNodes = tree.list(arguments);
      for (size_t argumentIndex = 0; argumentIndex < argumentNodes.size(); ++argumentIndex) {
        const auto argument = argumentNodes[argumentIndex];
        auto argumentTypeIndex = factIndex(facts.nodeTypes(), argument);
        auto literalIndex = factIndex(facts.literals(), argument);
        auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
        // A field-projection argument on the receiver local
        // (`cell.echo(cell.value)`) is admitted structurally: its node-type
        // fact is produced at a later production stage, so the field type
        // is resolved from the member fact rather than the literal fact.
        // The two `cell` identifiers are distinct AST nodes, so the match
        // is by owner-local binding, not by NodeId.
        const bool isReceiverFieldArgument =
            tree.contains(argument) &&
            tree.node(argument).kind == ast::SyntaxKind::MemberExpression &&
            static_cast<ast::MemberAccessKind>(
                tree.node(argument).payload.words[ast::kMemberExpressionAccessWord]) ==
                ast::MemberAccessKind::Dot &&
            resolvedOwnerLocal(
                bound.bindings(),
                ast::NodeId(tree.node(argument).payload.words[ast::kMemberExpressionObjectWord])) ==
                receiverBinding;
        if (isReceiverFieldArgument) {
          auto argumentMemberIndex = factIndex(facts.members(), argument);
          if (argumentTypeIndex == zc::none || argumentMemberIndex == zc::none ||
              argumentSpan == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          size_t argumentTypeSlot = 0;
          size_t argumentMemberSlot = 0;
          ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
          ZC_IF_SOME(value, argumentMemberIndex) { argumentMemberSlot = value; }
          const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
          const auto& argumentMember = facts.members().entries()[argumentMemberSlot].value;
          const auto& checkedArgument = invocation.arguments[argumentIndex];
          const auto& hirArgument = ZC_ASSERT_NONNULL(receiverCall).arguments[argumentIndex];
          if (checkedArgument.sourceNode != argument ||
              checkedArgument.sourceType != argumentType ||
              checkedArgument.parameterType != argumentType ||
              checkedArgument.adjustment != zc::none || argumentMember.node != argument ||
              argumentMember.memberType != argumentType ||
              argumentMember.receiverType != facts.nodeTypes().entries()[receiverTypeSlot].value ||
              argumentMember.adjustment != zc::none || hirArgument.type != argumentType ||
              hirArgument.value != zc::none || hirArgument.parameter != zc::none ||
              hirArgument.local != hirLocalId(1) || hirArgument.field != argumentMember.member ||
              !sameSpan(hirArgument.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          continue;
        }
        // A field-comparison argument on the receiver local
        // (`cell.compare(cell.value > 0)`) is admitted structurally: the
        // caller computes the comparison at runtime and passes the bool
        // result. The HIR argument carries the receiver local, field,
        // comparison operator, and literal right operand.
        if (tree.contains(argument) && tree.node(argument).kind == ast::SyntaxKind::BinaryExpr) {
          const auto syntaxOp = static_cast<ast::BinaryOperatorKind>(
              tree.node(argument).payload.words[ast::kBinaryExprOpWord]);
          auto operatorKind = checker::OperatorKind::fromBinary(syntaxOp);
          if (operatorKind != zc::none) {
            const auto& variant = ZC_ASSERT_NONNULL(operatorKind).variant();
            if (variant.is<checker::PrimitiveOperation>()) {
              const auto operation = variant.get<checker::PrimitiveOperation>();
              if (operation == checker::PrimitiveOperation::Eq ||
                  operation == checker::PrimitiveOperation::Ne ||
                  operation == checker::PrimitiveOperation::Lt ||
                  operation == checker::PrimitiveOperation::Le ||
                  operation == checker::PrimitiveOperation::Gt ||
                  operation == checker::PrimitiveOperation::Ge) {
                const ast::NodeId left(tree.node(argument).payload.words[ast::kBinaryExprLhsWord]);
                const ast::NodeId right(tree.node(argument).payload.words[ast::kBinaryExprRhsWord]);
                if (tree.contains(left) && tree.contains(right)) {
                  auto isReceiverField = [&](ast::NodeId operand) {
                    if (!tree.contains(operand) ||
                        tree.node(operand).kind != ast::SyntaxKind::MemberExpression) {
                      return false;
                    }
                    const ast::NodeId fieldObject(
                        tree.node(operand).payload.words[ast::kMemberExpressionObjectWord]);
                    return resolvedOwnerLocal(bound.bindings(), fieldObject) == receiverBinding;
                  };
                  const bool leftField = isReceiverField(left);
                  const bool rightField = isReceiverField(right);
                  if (leftField != rightField) {
                    const ast::NodeId fieldOperand = leftField ? left : right;
                    const ast::NodeId literalOperand = leftField ? right : left;
                    if (isScalarLiteral(tree.node(literalOperand).kind)) {
                      auto argumentMemberIndex = factIndex(facts.members(), fieldOperand);
                      auto comparisonLiteralIndex = factIndex(facts.literals(), literalOperand);
                      if (argumentTypeIndex != zc::none && argumentMemberIndex != zc::none &&
                          comparisonLiteralIndex != zc::none && argumentSpan != zc::none) {
                        size_t argumentTypeSlot = 0;
                        size_t argumentMemberSlot = 0;
                        size_t comparisonLiteralSlot = 0;
                        ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
                        ZC_IF_SOME(value, argumentMemberIndex) { argumentMemberSlot = value; }
                        ZC_IF_SOME(value, comparisonLiteralIndex) { comparisonLiteralSlot = value; }
                        const auto comparisonType =
                            facts.nodeTypes().entries()[argumentTypeSlot].value;
                        const auto& comparisonMember =
                            facts.members().entries()[argumentMemberSlot].value;
                        const auto& comparisonLiteral =
                            facts.literals().entries()[comparisonLiteralSlot].value;
                        const auto& checkedArgument = invocation.arguments[argumentIndex];
                        const auto& hirArgument =
                            ZC_ASSERT_NONNULL(receiverCall).arguments[argumentIndex];
                        bool literalMatches = false;
                        ZC_IF_SOME(value, hirArgument.value) {
                          literalMatches = sameConstant(value, comparisonLiteral.literal, module,
                                                        registries, semanticTypes);
                        }
                        if (checkedArgument.sourceNode != argument ||
                            checkedArgument.sourceType != comparisonType ||
                            checkedArgument.parameterType != comparisonType ||
                            checkedArgument.adjustment != zc::none ||
                            comparisonMember.node != fieldOperand ||
                            comparisonMember.receiverType !=
                                facts.nodeTypes().entries()[receiverTypeSlot].value ||
                            comparisonMember.adjustment != zc::none ||
                            comparisonLiteral.node != literalOperand ||
                            hirArgument.type != comparisonType || hirArgument.value == zc::none ||
                            hirArgument.parameter != zc::none ||
                            hirArgument.local != hirLocalId(1) ||
                            hirArgument.field != comparisonMember.member ||
                            hirArgument.comparisonOperation != operation || !literalMatches ||
                            !sameSpan(hirArgument.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
                          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                              ir::IrFailureKind::InvalidFact,
                                                              module, registries, index + 1);
                        }
                        continue;
                      }
                    }
                  }
                }
              }
            }
          }
        }
        if (!tree.contains(argument) || !isScalarLiteral(tree.node(argument).kind) ||
            argumentTypeIndex == zc::none || literalIndex == zc::none || argumentSpan == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t argumentTypeSlot = 0;
        size_t literalSlot = 0;
        ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
        ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
        const auto& checkedArgument = invocation.arguments[argumentIndex];
        const auto& hirArgument = ZC_ASSERT_NONNULL(receiverCall).arguments[argumentIndex];
        const auto& literal = facts.literals().entries()[literalSlot].value;
        const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
        bool constantMatches = false;
        ZC_IF_SOME(value, hirArgument.value) {
          constantMatches = sameConstant(value, literal.literal, module, registries, semanticTypes);
        }
        if (checkedArgument.sourceNode != argument || checkedArgument.sourceType != argumentType ||
            checkedArgument.parameterType != argumentType ||
            checkedArgument.adjustment != zc::none || literal.node != argument ||
            literal.type != argumentType || hirArgument.type != argumentType ||
            hirArgument.value == zc::none || hirArgument.parameter != zc::none ||
            !constantMatches || !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan)) ||
            !sameSpan(hirArgument.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      const auto baseIncrement = static_cast<uint32_t>(7);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, receiverCallNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
      continue;
    }
    zc::Maybe<const HirNominalAggregateExpression&> aggregateExpression;
    for (const auto& aggregate : candidate.impl->aggregates) {
      if (aggregate.node != materializedNode) continue;
      if (aggregateExpression != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      aggregateExpression = aggregate;
    }
    zc::Maybe<const HirLocalFieldProjectionExpression&> localFieldProjection;
    for (const auto& projection : candidate.impl->localFieldProjections) {
      if (projection.node != valueNode) continue;
      if (localFieldProjection != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      localFieldProjection = projection;
    }
    zc::Maybe<const HirParameterReferenceExpression&> parameterReference;
    for (const auto& reference : candidate.impl->parameterReferences) {
      const auto expectedReferenceNode = returnsLocal ? materializedNode : valueNode;
      if (reference.node != expectedReferenceNode) continue;
      if (parameterReference != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      parameterReference = reference;
    }
    zc::Maybe<const HirParameterReborrowExpression&> parameterReborrow;
    for (const auto& reborrow : candidate.impl->parameterReborrows) {
      if (reborrow.node != valueNode) continue;
      if (parameterReborrow != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      parameterReborrow = reborrow;
    }
    if (localFieldProjection != zc::none && !localHasInitializer && !hasLocalWrite) {
      const bool initializesField = hasLocalWrite;
      if (aggregateExpression != zc::none || localBinding == zc::none ||
          localReference != zc::none || (!initializesField && literalExpression != zc::none) ||
          directCall != zc::none || parameterReference != zc::none ||
          (initializesField &&
           (functionLocalWriteCount != 1 || localWrite == zc::none || writeLiteral == zc::none ||
            ZC_ASSERT_NONNULL(localWrite).field == zc::none ||
            ZC_ASSERT_NONNULL(localWrite).kind != HirLocalWriteKind::Initialize)) ||
          function.node.ordinal() != expectedFunction ||
          block.node.ordinal() != expectedFunction + 1 ||
          ZC_ASSERT_NONNULL(localBinding).node != hirId(expectedFunction + 2) ||
          ZC_ASSERT_NONNULL(localBinding).initializer != zc::none ||
          returnStatement.node != hirId(expectedFunction + 3 + functionLocalWriteCount * 2) ||
          ZC_ASSERT_NONNULL(localFieldProjection).node !=
              hirId(expectedFunction + 4 + functionLocalWriteCount * 2) ||
          function.body != block.node || block.statements.size() != functionLocalWriteCount + 2 ||
          block.statements[0] != ZC_ASSERT_NONNULL(localBinding).node ||
          block.statements[block.statements.size() - 1] != returnStatement.node ||
          returnStatement.value != ZC_ASSERT_NONNULL(localFieldProjection).node ||
          function.resultType != returnStatement.resultType ||
          ZC_ASSERT_NONNULL(localFieldProjection).local != ZC_ASSERT_NONNULL(localBinding).local ||
          ZC_ASSERT_NONNULL(localFieldProjection).receiverType !=
              ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(localFieldProjection).type != function.resultType ||
          ZC_ASSERT_NONNULL(localFieldProjection).category != HirValueCategory::Place ||
          !typeExists(ZC_ASSERT_NONNULL(localBinding).type, semanticTypes) ||
          !typeExists(function.resultType, semanticTypes)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
          !tree.contains(sourceDefinition.node)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (sourceShape == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      FunctionReturnShape source = zc::mv(sourceShape).orDefault(FunctionReturnShape{});
      auto ownerBinding = resolvedOwnerLocal(bound.bindings(), source.localReference);
      auto memberIndex = factIndex(facts.members(), source.value);
      auto placeIndex = factIndex(facts.places(), source.value);
      auto returnType = factIndex(facts.nodeTypes(), source.value);
      auto projectionSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
      if (!source.returnsLocal || !source.returnsLocalField ||
          source.localInitializer != zc::none ||
          source.localWrites.size != functionLocalWriteCount || ownerBinding == zc::none ||
          memberIndex == zc::none || placeIndex == zc::none || returnType == zc::none ||
          projectionSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(ownerBinding), source.localPattern,
                             tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t memberSlot = 0;
      size_t placeSlot = 0;
      size_t returnTypeSlot = 0;
      ZC_IF_SOME(value, memberIndex) { memberSlot = value; }
      ZC_IF_SOME(value, placeIndex) { placeSlot = value; }
      ZC_IF_SOME(value, returnType) { returnTypeSlot = value; }
      const auto& checkedMember = facts.members().entries()[memberSlot].value;
      const auto& checkedPlace = facts.places().entries()[placeSlot].value;
      const auto& checkedRoot = checkedPlace.root.variant();
      if (checkedMember.node != source.value ||
          checkedMember.receiverType != ZC_ASSERT_NONNULL(localBinding).type ||
          checkedMember.member != ZC_ASSERT_NONNULL(localFieldProjection).field ||
          checkedMember.memberType != ZC_ASSERT_NONNULL(localFieldProjection).type ||
          checkedMember.adjustment != zc::none ||
          !checkedRoot.is<checker::checked::OwnerLocalPlaceRoot>() ||
          checkedRoot.get<checker::checked::OwnerLocalPlaceRoot>().binding !=
              ZC_ASSERT_NONNULL(ownerBinding) ||
          checkedPlace.projections.size() != 1 ||
          !checkedPlace.projections[0].variant().is<checker::checked::FieldProjection>() ||
          checkedPlace.projections[0].variant().get<checker::checked::FieldProjection>().field !=
              checkedMember.member ||
          checkedPlace.type != ZC_ASSERT_NONNULL(localFieldProjection).type ||
          !checkedPlace.movable ||
          facts.nodeTypes().entries()[returnTypeSlot].value != function.resultType ||
          !sameSpan(ZC_ASSERT_NONNULL(localFieldProjection).sourceSpan,
                    ZC_ASSERT_NONNULL(projectionSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      if (initializesField) {
        const auto expectedWrite = hirId(expectedFunction + 3);
        const auto expectedLiteral = hirId(expectedFunction + 4);
        auto sourceStatement = statementItem(tree, tree.list(source.localWrites)[0]);
        if (sourceStatement == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        ast::NodeId statement;
        ZC_IF_SOME(value, sourceStatement) { statement = value; }
        const ast::NodeId assignment(
            tree.node(statement).payload.words[ast::kExpressionStatementExpressionWord]);
        if (tree.node(statement).kind != ast::SyntaxKind::ExpressionStatement ||
            !tree.contains(assignment) ||
            tree.node(assignment).kind != ast::SyntaxKind::AssignmentExpr) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const ast::NodeId target(tree.node(assignment).payload.words[ast::kAssignmentExprLhsWord]);
        const ast::NodeId value(tree.node(assignment).payload.words[ast::kAssignmentExprRhsWord]);
        auto assignmentType = factIndex(facts.nodeTypes(), assignment);
        auto targetType = factIndex(facts.nodeTypes(), target);
        auto valueType = factIndex(facts.nodeTypes(), value);
        auto valueLiteral = factIndex(facts.literals(), value);
        auto writeMember = factIndex(facts.members(), target);
        auto writePlace = factIndex(facts.places(), target);
        auto assignmentSpan = bound.parsedModule().spanFor(tree.node(assignment).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(value).range);
        ast::NodeId targetReference = target;
        if (tree.contains(target) && tree.node(target).kind == ast::SyntaxKind::MemberExpression) {
          targetReference =
              ast::NodeId(tree.node(target).payload.words[ast::kMemberExpressionObjectWord]);
        }
        auto targetBinding = resolvedOwnerLocal(bound.bindings(), targetReference);
        if (!tree.contains(target) || !tree.contains(value) ||
            tree.node(target).kind != ast::SyntaxKind::MemberExpression ||
            assignmentType == zc::none || targetType == zc::none || valueType == zc::none ||
            valueLiteral == zc::none || writeMember == zc::none || writePlace == zc::none ||
            assignmentSpan == zc::none || valueSpan == zc::none || targetBinding == zc::none ||
            targetBinding != ZC_ASSERT_NONNULL(ownerBinding)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t assignmentSlot = 0;
        size_t targetSlot = 0;
        size_t valueSlot = 0;
        size_t literalSlot = 0;
        size_t memberSlot = 0;
        size_t placeSlot = 0;
        ZC_IF_SOME(slot, assignmentType) { assignmentSlot = slot; }
        ZC_IF_SOME(slot, targetType) { targetSlot = slot; }
        ZC_IF_SOME(slot, valueType) { valueSlot = slot; }
        ZC_IF_SOME(slot, valueLiteral) { literalSlot = slot; }
        ZC_IF_SOME(slot, writeMember) { memberSlot = slot; }
        ZC_IF_SOME(slot, writePlace) { placeSlot = slot; }
        const auto& member = facts.members().entries()[memberSlot].value;
        const auto& place = facts.places().entries()[placeSlot].value;
        const auto& root = place.root.variant();
        if (block.statements[1] != expectedWrite ||
            ZC_ASSERT_NONNULL(localWrite).node != expectedWrite ||
            ZC_ASSERT_NONNULL(writeLiteral).node != expectedLiteral ||
            ZC_ASSERT_NONNULL(localWrite).local != ZC_ASSERT_NONNULL(localBinding).local ||
            ZC_ASSERT_NONNULL(localWrite).field != ZC_ASSERT_NONNULL(localFieldProjection).field ||
            ZC_ASSERT_NONNULL(localWrite).value != expectedLiteral ||
            ZC_ASSERT_NONNULL(writeLiteral).type != ZC_ASSERT_NONNULL(localWrite).type ||
            ZC_ASSERT_NONNULL(writeLiteral).category != HirValueCategory::Value ||
            facts.nodeTypes().entries()[assignmentSlot].value !=
                ZC_ASSERT_NONNULL(localWrite).type ||
            facts.nodeTypes().entries()[targetSlot].value != ZC_ASSERT_NONNULL(localWrite).type ||
            facts.nodeTypes().entries()[valueSlot].value != ZC_ASSERT_NONNULL(localWrite).type ||
            facts.literals().entries()[literalSlot].value.type !=
                ZC_ASSERT_NONNULL(localWrite).type ||
            member.node != target || member.member != ZC_ASSERT_NONNULL(localWrite).field ||
            member.memberType != ZC_ASSERT_NONNULL(localWrite).type || !place.mutablePlace ||
            !root.is<checker::checked::OwnerLocalPlaceRoot>() ||
            root.get<checker::checked::OwnerLocalPlaceRoot>().binding !=
                ZC_ASSERT_NONNULL(ownerBinding) ||
            place.projections.size() != 1 ||
            !place.projections[0].variant().is<checker::checked::FieldProjection>() ||
            place.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                ZC_ASSERT_NONNULL(localWrite).field ||
            place.type != ZC_ASSERT_NONNULL(localWrite).type ||
            !sameSpan(ZC_ASSERT_NONNULL(localWrite).sourceSpan,
                      ZC_ASSERT_NONNULL(assignmentSpan)) ||
            !sameSpan(ZC_ASSERT_NONNULL(localWrite).valueSpan, ZC_ASSERT_NONNULL(valueSpan)) ||
            !sameSpan(ZC_ASSERT_NONNULL(writeLiteral).sourceSpan, ZC_ASSERT_NONNULL(valueSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const auto baseIncrement = static_cast<uint32_t>(initializesField ? 7 : 5);
        auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
        if (unsafeExtra == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
      } else {
        const auto baseIncrement = static_cast<uint32_t>(5);
        auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
        if (unsafeExtra == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
      }
      continue;
    }
    if (aggregateExpression != zc::none && localFieldProjection == zc::none &&
        localReference != zc::none) {
      if (localBinding == zc::none || literalExpression != zc::none || directCall != zc::none ||
          parameterReference != zc::none || hasLocalWrite || !localHasInitializer ||
          function.node.ordinal() != expectedFunction ||
          block.node.ordinal() != expectedFunction + 1 ||
          ZC_ASSERT_NONNULL(localBinding).node != hirId(expectedFunction + 2) ||
          ZC_ASSERT_NONNULL(localBinding).initializer != hirId(expectedFunction + 3) ||
          ZC_ASSERT_NONNULL(aggregateExpression).node != hirId(expectedFunction + 3) ||
          returnStatement.node != hirId(expectedFunction + 4) ||
          ZC_ASSERT_NONNULL(localReference).node != hirId(expectedFunction + 5) ||
          function.body != block.node || block.statements.size() != 2 ||
          block.statements[0] != ZC_ASSERT_NONNULL(localBinding).node ||
          block.statements[1] != returnStatement.node ||
          returnStatement.value != ZC_ASSERT_NONNULL(localReference).node ||
          function.resultType != returnStatement.resultType ||
          ZC_ASSERT_NONNULL(localReference).local != ZC_ASSERT_NONNULL(localBinding).local ||
          ZC_ASSERT_NONNULL(localReference).type != function.resultType ||
          ZC_ASSERT_NONNULL(localReference).category != HirValueCategory::Place ||
          ZC_ASSERT_NONNULL(aggregateExpression).type != ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(aggregateExpression).category != HirValueCategory::Value ||
          !typeExists(ZC_ASSERT_NONNULL(localBinding).type, semanticTypes) ||
          !typeExists(function.resultType, semanticTypes)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
          !tree.contains(sourceDefinition.node)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (sourceShape == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      FunctionReturnShape source = zc::mv(sourceShape).orDefault(FunctionReturnShape{});
      ast::NodeId initializer;
      ZC_IF_SOME(value, source.localInitializer) { initializer = value; }
      auto ownerBinding = resolvedOwnerLocal(bound.bindings(), source.localReference);
      auto aggregateIndex = factIndex(facts.aggregates(), initializer);
      auto initializerType = factIndex(facts.nodeTypes(), initializer);
      auto returnType = factIndex(facts.nodeTypes(), source.value);
      auto aggregateSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
      auto returnValueSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
      if (!source.returnsLocal || source.returnsLocalField || source.localWrites.size != 0 ||
          ownerBinding == zc::none || aggregateIndex == zc::none || initializerType == zc::none ||
          returnType == zc::none || aggregateSpan == zc::none || returnValueSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(ownerBinding), source.localPattern,
                             tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t aggregateSlot = 0;
      size_t initializerTypeSlot = 0;
      size_t returnTypeSlot = 0;
      ZC_IF_SOME(value, aggregateIndex) { aggregateSlot = value; }
      ZC_IF_SOME(value, initializerType) { initializerTypeSlot = value; }
      ZC_IF_SOME(value, returnType) { returnTypeSlot = value; }
      const auto& checkedAggregate = facts.aggregates().entries()[aggregateSlot].value;
      if (checkedAggregate.node != initializer ||
          !checkedAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
          checkedAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition !=
              ZC_ASSERT_NONNULL(aggregateExpression).definition ||
          checkedAggregate.resultType != ZC_ASSERT_NONNULL(aggregateExpression).type ||
          checkedAggregate.resultType != facts.nodeTypes().entries()[initializerTypeSlot].value ||
          !sameSpan(checkedAggregate.sourceSpan, ZC_ASSERT_NONNULL(aggregateSpan)) ||
          facts.nodeTypes().entries()[returnTypeSlot].value != function.resultType ||
          !sameSpan(ZC_ASSERT_NONNULL(localReference).sourceSpan,
                    ZC_ASSERT_NONNULL(returnValueSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      if (checkedAggregate.elements.size() !=
          ZC_ASSERT_NONNULL(aggregateExpression).elements.size()) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      for (size_t elementIndex = 0; elementIndex < checkedAggregate.elements.size();
           ++elementIndex) {
        const auto& checkedElement = checkedAggregate.elements[elementIndex];
        const auto& aggregateElement =
            ZC_ASSERT_NONNULL(aggregateExpression).elements[elementIndex];
        auto literalIndex = factIndex(facts.literals(), checkedElement.sourceNode);
        if (checkedElement.field == zc::none || checkedElement.index != elementIndex ||
            checkedElement.sourceType != checkedElement.destinationType ||
            checkedElement.adjustment != zc::none || literalIndex == zc::none ||
            ZC_ASSERT_NONNULL(checkedElement.field) != aggregateElement.field ||
            checkedElement.destinationType != aggregateElement.type) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        size_t literalSlot = 0;
        ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
        const auto& literal = facts.literals().entries()[literalSlot].value;
        if (literal.type != aggregateElement.type ||
            !sameConstant(literal.literal, aggregateElement.value, module, registries,
                          semanticTypes) ||
            !sameSpan(literal.sourceSpan, aggregateElement.sourceSpan)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      const auto baseIncrement = static_cast<uint32_t>(6);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
      continue;
    }
    if (aggregateExpression != zc::none) {
      const bool aggregateFieldOverwrite = hasLocalWrite;
      if (aggregateExpression == zc::none || localFieldProjection == zc::none ||
          localBinding == zc::none || localReference != zc::none || literalExpression != zc::none ||
          directCall != zc::none || parameterReference != zc::none || !localHasInitializer ||
          (aggregateFieldOverwrite && functionLocalWriteCount == 0) ||
          function.node.ordinal() != expectedFunction ||
          block.node.ordinal() != expectedFunction + 1 ||
          ZC_ASSERT_NONNULL(localBinding).node != hirId(expectedFunction + 2) ||
          ZC_ASSERT_NONNULL(localBinding).initializer != hirId(expectedFunction + 3) ||
          ZC_ASSERT_NONNULL(aggregateExpression).node != hirId(expectedFunction + 3) ||
          returnStatement.node != hirId(expectedFunction + 4 + functionLocalWriteCount * 2) ||
          ZC_ASSERT_NONNULL(localFieldProjection).node !=
              hirId(expectedFunction + 5 + functionLocalWriteCount * 2) ||
          function.body != block.node || block.statements.size() != functionLocalWriteCount + 2 ||
          block.statements[0] != ZC_ASSERT_NONNULL(localBinding).node ||
          block.statements[functionLocalWriteCount + 1] != returnStatement.node ||
          returnStatement.value != ZC_ASSERT_NONNULL(localFieldProjection).node ||
          function.resultType != returnStatement.resultType ||
          ZC_ASSERT_NONNULL(localFieldProjection).local != ZC_ASSERT_NONNULL(localBinding).local ||
          ZC_ASSERT_NONNULL(localFieldProjection).receiverType !=
              ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(localFieldProjection).type != function.resultType ||
          ZC_ASSERT_NONNULL(localFieldProjection).category != HirValueCategory::Place ||
          ZC_ASSERT_NONNULL(aggregateExpression).type != ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(aggregateExpression).category != HirValueCategory::Value ||
          !typeExists(ZC_ASSERT_NONNULL(localBinding).type, semanticTypes) ||
          !typeExists(function.resultType, semanticTypes)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
          !tree.contains(sourceDefinition.node)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (sourceShape == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      FunctionReturnShape source = zc::mv(sourceShape).orDefault(FunctionReturnShape{});
      ast::NodeId initializer;
      ZC_IF_SOME(value, source.localInitializer) { initializer = value; }
      auto ownerBinding = resolvedOwnerLocal(bound.bindings(), source.localReference);
      auto aggregateIndex = factIndex(facts.aggregates(), initializer);
      auto memberIndex = factIndex(facts.members(), source.value);
      auto placeIndex = factIndex(facts.places(), source.value);
      auto initializerType = factIndex(facts.nodeTypes(), initializer);
      auto returnType = factIndex(facts.nodeTypes(), source.value);
      auto aggregateSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
      auto projectionSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
      if (!source.returnsLocal || !source.returnsLocalField ||
          source.localWrites.size != functionLocalWriteCount || ownerBinding == zc::none ||
          aggregateIndex == zc::none || memberIndex == zc::none || placeIndex == zc::none ||
          initializerType == zc::none || returnType == zc::none || aggregateSpan == zc::none ||
          projectionSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(ownerBinding), source.localPattern,
                             tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t aggregateSlot = 0;
      size_t memberSlot = 0;
      size_t placeSlot = 0;
      size_t initializerTypeSlot = 0;
      size_t returnTypeSlot = 0;
      ZC_IF_SOME(value, aggregateIndex) { aggregateSlot = value; }
      ZC_IF_SOME(value, memberIndex) { memberSlot = value; }
      ZC_IF_SOME(value, placeIndex) { placeSlot = value; }
      ZC_IF_SOME(value, initializerType) { initializerTypeSlot = value; }
      ZC_IF_SOME(value, returnType) { returnTypeSlot = value; }
      const auto& checkedAggregate = facts.aggregates().entries()[aggregateSlot].value;
      const auto& checkedMember = facts.members().entries()[memberSlot].value;
      const auto& checkedPlace = facts.places().entries()[placeSlot].value;
      const auto& checkedRoot = checkedPlace.root.variant();
      if (checkedAggregate.node != initializer ||
          !checkedAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
          checkedAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition !=
              ZC_ASSERT_NONNULL(aggregateExpression).definition ||
          checkedAggregate.resultType != ZC_ASSERT_NONNULL(aggregateExpression).type ||
          checkedAggregate.resultType != facts.nodeTypes().entries()[initializerTypeSlot].value ||
          !sameSpan(checkedAggregate.sourceSpan, ZC_ASSERT_NONNULL(aggregateSpan)) ||
          checkedMember.node != source.value ||
          checkedMember.receiverType != ZC_ASSERT_NONNULL(localBinding).type ||
          checkedMember.member != ZC_ASSERT_NONNULL(localFieldProjection).field ||
          checkedMember.memberType != ZC_ASSERT_NONNULL(localFieldProjection).type ||
          checkedMember.adjustment != zc::none ||
          !checkedRoot.is<checker::checked::OwnerLocalPlaceRoot>() ||
          checkedRoot.get<checker::checked::OwnerLocalPlaceRoot>().binding !=
              ZC_ASSERT_NONNULL(ownerBinding) ||
          checkedPlace.projections.size() != 1 ||
          !checkedPlace.projections[0].variant().is<checker::checked::FieldProjection>() ||
          checkedPlace.projections[0].variant().get<checker::checked::FieldProjection>().field !=
              checkedMember.member ||
          checkedPlace.type != ZC_ASSERT_NONNULL(localFieldProjection).type ||
          !checkedPlace.movable ||
          facts.nodeTypes().entries()[returnTypeSlot].value != function.resultType ||
          !sameSpan(ZC_ASSERT_NONNULL(localFieldProjection).sourceSpan,
                    ZC_ASSERT_NONNULL(projectionSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      if (checkedAggregate.elements.size() !=
          ZC_ASSERT_NONNULL(aggregateExpression).elements.size()) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      for (size_t elementIndex = 0; elementIndex < checkedAggregate.elements.size();
           ++elementIndex) {
        const auto& checkedElement = checkedAggregate.elements[elementIndex];
        const auto& aggregateElement =
            ZC_ASSERT_NONNULL(aggregateExpression).elements[elementIndex];
        auto literalIndex = factIndex(facts.literals(), checkedElement.sourceNode);
        if (checkedElement.field == zc::none || checkedElement.index != elementIndex ||
            checkedElement.sourceType != checkedElement.destinationType ||
            checkedElement.adjustment != zc::none || literalIndex == zc::none ||
            ZC_ASSERT_NONNULL(checkedElement.field) != aggregateElement.field ||
            checkedElement.destinationType != aggregateElement.type) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        size_t literalSlot = 0;
        ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
        const auto& literal = facts.literals().entries()[literalSlot].value;
        if (literal.type != aggregateElement.type ||
            !sameConstant(literal.literal, aggregateElement.value, module, registries,
                          semanticTypes) ||
            !sameSpan(literal.sourceSpan, aggregateElement.sourceSpan)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      if (aggregateFieldOverwrite) {
        for (size_t writeIndex = 0; writeIndex < functionLocalWriteCount; ++writeIndex) {
          auto sourceStatement = statementItem(tree, tree.list(source.localWrites)[writeIndex]);
          if (sourceStatement == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          ast::NodeId statement;
          ZC_IF_SOME(value, sourceStatement) { statement = value; }
          const ast::NodeId assignment(
              tree.node(statement).payload.words[ast::kExpressionStatementExpressionWord]);
          if (tree.node(statement).kind != ast::SyntaxKind::ExpressionStatement ||
              !tree.contains(assignment) ||
              tree.node(assignment).kind != ast::SyntaxKind::AssignmentExpr) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const ast::NodeId target(
              tree.node(assignment).payload.words[ast::kAssignmentExprLhsWord]);
          const ast::NodeId value(tree.node(assignment).payload.words[ast::kAssignmentExprRhsWord]);
          auto assignmentType = factIndex(facts.nodeTypes(), assignment);
          auto targetType = factIndex(facts.nodeTypes(), target);
          auto valueType = factIndex(facts.nodeTypes(), value);
          auto valueLiteral = factIndex(facts.literals(), value);
          auto writeMember = factIndex(facts.members(), target);
          auto writePlace = factIndex(facts.places(), target);
          auto assignmentSpan = bound.parsedModule().spanFor(tree.node(assignment).range);
          auto valueSpan = bound.parsedModule().spanFor(tree.node(value).range);
          const auto expectedWrite = hirId(expectedFunction + 4 + writeIndex * 2);
          const auto expectedLiteral = hirId(expectedWrite.ordinal() + 1);
          zc::Maybe<const HirLocalWriteStatement&> write;
          zc::Maybe<const HirScalarLiteralExpression&> literal;
          for (const auto& candidateWrite : candidate.impl->localWrites) {
            if (candidateWrite.node != expectedWrite) continue;
            if (write != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            write = candidateWrite;
          }
          for (const auto& expression : candidate.impl->expressions) {
            if (expression.node != expectedLiteral) continue;
            if (literal != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            literal = expression;
          }
          if (!tree.contains(target) || !tree.contains(value) ||
              tree.node(target).kind != ast::SyntaxKind::MemberExpression ||
              assignmentType == zc::none || targetType == zc::none || valueType == zc::none ||
              valueLiteral == zc::none || writeMember == zc::none || writePlace == zc::none ||
              assignmentSpan == zc::none || valueSpan == zc::none || write == zc::none ||
              literal == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          size_t assignmentSlot = 0;
          size_t targetSlot = 0;
          size_t valueSlot = 0;
          size_t literalSlot = 0;
          size_t memberSlot = 0;
          size_t placeSlot = 0;
          ZC_IF_SOME(slot, assignmentType) { assignmentSlot = slot; }
          ZC_IF_SOME(slot, targetType) { targetSlot = slot; }
          ZC_IF_SOME(slot, valueType) { valueSlot = slot; }
          ZC_IF_SOME(slot, valueLiteral) { literalSlot = slot; }
          ZC_IF_SOME(slot, writeMember) { memberSlot = slot; }
          ZC_IF_SOME(slot, writePlace) { placeSlot = slot; }
          const auto& member = facts.members().entries()[memberSlot].value;
          const auto& place = facts.places().entries()[placeSlot].value;
          const auto& root = place.root.variant();
          const auto& writeValue = ZC_ASSERT_NONNULL(write);
          const auto& literalValue = ZC_ASSERT_NONNULL(literal);
          if (writeValue.local != ZC_ASSERT_NONNULL(localBinding).local ||
              writeValue.field == zc::none || writeValue.kind != HirLocalWriteKind::Overwrite ||
              writeValue.value != literalValue.node || literalValue.type != writeValue.type ||
              literalValue.category != HirValueCategory::Value ||
              facts.nodeTypes().entries()[assignmentSlot].value != writeValue.type ||
              facts.nodeTypes().entries()[targetSlot].value != writeValue.type ||
              facts.nodeTypes().entries()[valueSlot].value != writeValue.type ||
              facts.literals().entries()[literalSlot].value.type != writeValue.type ||
              member.node != target || member.member != ZC_ASSERT_NONNULL(writeValue.field) ||
              member.memberType != writeValue.type || !place.mutablePlace ||
              !root.is<checker::checked::OwnerLocalPlaceRoot>() ||
              root.get<checker::checked::OwnerLocalPlaceRoot>().binding !=
                  ZC_ASSERT_NONNULL(ownerBinding) ||
              place.projections.size() != 1 ||
              !place.projections[0].variant().is<checker::checked::FieldProjection>() ||
              place.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                  ZC_ASSERT_NONNULL(writeValue.field) ||
              place.type != writeValue.type ||
              !sameSpan(writeValue.sourceSpan, ZC_ASSERT_NONNULL(assignmentSpan)) ||
              !sameSpan(writeValue.valueSpan, ZC_ASSERT_NONNULL(valueSpan)) ||
              !sameSpan(literalValue.sourceSpan, ZC_ASSERT_NONNULL(valueSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
      }
      const auto baseIncrement = static_cast<uint32_t>(6 + functionLocalWriteCount * 2);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
      continue;
    }
    const bool uninitializedLocal = returnsLocal && !localHasInitializer && !hasLocalWrite;
    const bool returnsParameter = parameterReference != zc::none && !returnsLocal;
    const bool returnsParameterReborrow = parameterReborrow != zc::none && !returnsLocal;
    const bool returnsLocalAliasReborrow = parameterReborrow != zc::none && returnsLocal;
    const bool initializesFromParameter = parameterReference != zc::none && returnsLocal;
    if (returnsParameterReborrow) {
      if (parameterReference != zc::none || literalExpression != zc::none ||
          directCall != zc::none || localReference != zc::none ||
          localFieldProjection != zc::none || aggregateExpression != zc::none || hasLocalWrite ||
          function.node.ordinal() != expectedFunction ||
          block.node.ordinal() != expectedFunction + 1 || returnStatement.node != returnNode ||
          function.body != block.node || block.statements.size() != 1 ||
          block.statements[0] != returnStatement.node || returnStatement.value != valueNode ||
          function.resultType != returnStatement.resultType ||
          ZC_ASSERT_NONNULL(parameterReborrow).type != function.resultType) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
    }
    if ((!uninitializedLocal && !returnsParameter && !returnsParameterReborrow &&
         !initializesFromParameter &&
         (literalExpression == zc::none) == (directCall == zc::none)) ||
        (uninitializedLocal && (literalExpression != zc::none || directCall != zc::none)) ||
        (returnsParameter &&
         (returnsLocal || literalExpression != zc::none || directCall != zc::none)) ||
        (initializesFromParameter &&
         (literalExpression != zc::none || directCall != zc::none ||
          (localReference == zc::none && !returnsLocalAliasReborrow))) ||
        (hasLocalWrite && (returnsLocal == false || !hasFirstWriteValue ||
                           (localHasInitializer &&
                            ZC_ASSERT_NONNULL(localWrite).kind != HirLocalWriteKind::Overwrite) ||
                           (!localHasInitializer && ZC_ASSERT_NONNULL(localWrite).kind !=
                                                        HirLocalWriteKind::Initialize))) ||
        (returnsLocal && !returnsLocalAliasReborrow && localReference == zc::none &&
         localFieldProjection == zc::none && localBorrow == zc::none) ||
        function.node.ordinal() != expectedFunction ||
        block.node.ordinal() != expectedFunction + 1 || returnStatement.node != returnNode ||
        function.body != block.node ||
        block.statements.size() != (returnsLocal ? functionLocalWriteCount + 2 : 1) ||
        !writesMatchBlock ||
        block.statements[block.statements.size() - 1] != returnStatement.node ||
        returnStatement.value != valueNode || function.resultType != returnStatement.resultType ||
        !typeExists(function.resultType, semanticTypes)) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }

    auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
    auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), function.definition);
    if (sourceDefinitionIndex == zc::none || signaturePosition == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    size_t definitionSlot = 0;
    size_t signatureSlot = 0;
    ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
    ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
    const auto& sourceDefinition = definitions.definitions()[definitionSlot];
    const auto& tree = bound.tree();
    const bool isMethod = sourceDefinition.record.kind() == identity::DefinitionKind::Method;
    // An inherent member carries no module-scope signature root.
    auto rootPosition = zc::Maybe<size_t>(zc::none);
    if (!isMethod) {
      rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
      if (rootPosition == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
    }
    size_t rootSlot = 0;
    ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
    const auto expectedSourceKind =
        isMethod ? ast::SyntaxKind::MethodDecl : ast::SyntaxKind::FunctionDecl;
    if (!hasExecutableBody(sourceDefinition, definitions) ||
        !definitionBelongsToModule(sourceDefinition, definitions) ||
        (sourceDefinition.record.kind() != identity::DefinitionKind::Function && !isMethod) ||
        !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
        !tree.contains(sourceDefinition.node) ||
        tree.node(sourceDefinition.node).kind != expectedSourceKind) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
    if (sourceShape == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    FunctionReturnShape source = zc::mv(sourceShape).orDefault(FunctionReturnShape{});
    auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
    auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
    if (bodySpan == zc::none || returnSpan == zc::none || source.returnsLocal != returnsLocal) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    if ((source.unsafeBlock != zc::none) != (function.unsafeBlock != zc::none)) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto sourceReturnValueNode = source.value;
    ast::NodeId sourceValueNode = sourceReturnValueNode;
    ZC_IF_SOME(initializer, source.localInitializer) { sourceValueNode = initializer; }
    if (source.localWrites.size != functionLocalWriteCount) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    for (size_t writeIndex = 0; writeIndex < source.localWrites.size; ++writeIndex) {
      auto sourceStatement = statementItem(tree, tree.list(source.localWrites)[writeIndex]);
      if (sourceStatement == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ast::NodeId sourceStatementNode;
      ZC_IF_SOME(value, sourceStatement) { sourceStatementNode = value; }
      const ast::NodeId sourceWrite(
          tree.node(sourceStatementNode).payload.words[ast::kExpressionStatementExpressionWord]);
      // A postfix increment/decrement (`x++` / `x--`) desugars to a binary write
      // (`x = x + 1` / `x = x - 1`). The PostfixExpression node replaces the
      // assignment plus binary nodes: the target is the postfix operand and the
      // write value is the postfix node itself, whose call fact records the
      // PostIncrement/PostDecrement operation. The synthetic literal 1 has no
      // AST node and therefore no checked literal fact.
      const bool isPostfixWrite = tree.node(sourceWrite).kind == ast::SyntaxKind::PostfixExpression;
      // A compound assignment (`x += 1`) desugars to a binary write
      // (`x = x + 1`) in the HIR builder, so the HIR value is a
      // HirPrimitiveBinaryExpression even though the source RHS is a scalar
      // literal, not a BinaryExpr.
      const bool isCompoundWrite =
          !isPostfixWrite && tree.node(sourceWrite).kind == ast::SyntaxKind::AssignmentExpr &&
          isCompoundAssignment(static_cast<ast::AssignmentOperatorKind>(
              tree.node(sourceWrite).payload.words[ast::kAssignmentExprOpWord]));
      // For a compound assignment the binary operation is derived from the
      // assignment operator; there is no checked call fact on the RHS.
      const auto compoundOperation =
          isCompoundWrite
              ? compoundAssignmentBinaryOperation(static_cast<ast::AssignmentOperatorKind>(
                    tree.node(sourceWrite).payload.words[ast::kAssignmentExprOpWord]))
              : zc::none;
      const ast::NodeId sourceTarget(
          isPostfixWrite
              ? ast::NodeId(
                    tree.node(sourceWrite).payload.words[ast::kPostfixExpressionOperandWord])
              : ast::NodeId(tree.node(sourceWrite).payload.words[ast::kAssignmentExprLhsWord]));
      const ast::NodeId sourceWriteValue(
          isPostfixWrite
              ? sourceWrite
              : ast::NodeId(tree.node(sourceWrite).payload.words[ast::kAssignmentExprRhsWord]));
      auto sourceWriteSpan = bound.parsedModule().spanFor(tree.node(sourceWrite).range);
      auto sourceValueSpan = bound.parsedModule().spanFor(tree.node(sourceWriteValue).range);
      auto assignmentType = factIndex(facts.nodeTypes(), sourceWrite);
      auto targetType = factIndex(facts.nodeTypes(), sourceTarget);
      auto valueType = factIndex(facts.nodeTypes(), sourceWriteValue);
      // A reference or binary write value has no literal fact; a literal write
      // value does. A postfix or compound-assignment write always has a binary
      // HIR value.
      const bool sourceReferenceValue =
          !isPostfixWrite && !isCompoundWrite && tree.contains(sourceWriteValue) &&
          tree.node(sourceWriteValue).kind == ast::SyntaxKind::IdentExpr;
      const bool sourceBinaryValue =
          isPostfixWrite || isCompoundWrite ||
          (tree.contains(sourceWriteValue) &&
           tree.node(sourceWriteValue).kind == ast::SyntaxKind::BinaryExpr);
      auto valueLiteral = (sourceReferenceValue || sourceBinaryValue)
                              ? zc::none
                              : factIndex(facts.literals(), sourceWriteValue);
      ast::NodeId sourceTargetReference = sourceTarget;
      if (tree.contains(sourceTarget) &&
          tree.node(sourceTarget).kind == ast::SyntaxKind::MemberExpression) {
        sourceTargetReference =
            ast::NodeId(tree.node(sourceTarget).payload.words[ast::kMemberExpressionObjectWord]);
      }
      auto targetBinding = resolvedOwnerLocal(bound.bindings(), sourceTargetReference);
      ast::NodeId sourceReturnReference = sourceReturnValueNode;
      if (tree.node(sourceReturnValueNode).kind == ast::SyntaxKind::MemberExpression) {
        sourceReturnReference = ast::NodeId(
            tree.node(sourceReturnValueNode).payload.words[ast::kMemberExpressionObjectWord]);
      }
      auto returnBinding = resolvedOwnerLocal(bound.bindings(), sourceReturnReference);
      if (sourceWriteSpan == zc::none || sourceValueSpan == zc::none ||
          assignmentType == zc::none || targetType == zc::none || valueType == zc::none ||
          (!sourceReferenceValue && !sourceBinaryValue && valueLiteral == zc::none) ||
          targetBinding == zc::none || returnBinding == zc::none ||
          targetBinding != returnBinding) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t assignmentSlot = 0;
      size_t targetSlot = 0;
      size_t valueSlot = 0;
      size_t literalSlot = 0;
      ZC_IF_SOME(value, assignmentType) { assignmentSlot = value; }
      ZC_IF_SOME(value, targetType) { targetSlot = value; }
      ZC_IF_SOME(value, valueType) { valueSlot = value; }
      ZC_IF_SOME(value, valueLiteral) { literalSlot = value; }
      const auto expectedWrite =
          hirId(expectedFunction + (localHasInitializer ? 4 : 3) + writeIndex * 2);
      const auto expectedValue = hirId(expectedWrite.ordinal() + 1);
      zc::Maybe<const HirLocalWriteStatement&> write;
      zc::Maybe<const HirScalarLiteralExpression&> literal;
      zc::Maybe<const HirParameterReferenceExpression&> parameter;
      zc::Maybe<const HirPrimitiveBinaryExpression&> binary;
      for (const auto& candidateWrite : candidate.impl->localWrites) {
        if (candidateWrite.node != expectedWrite) continue;
        if (write != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        write = candidateWrite;
      }
      for (const auto& expression : candidate.impl->expressions) {
        if (expression.node != expectedValue) continue;
        if (literal != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        literal = expression;
      }
      for (const auto& reference : candidate.impl->parameterReferences) {
        if (reference.node != expectedValue) continue;
        if (parameter != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        parameter = reference;
      }
      for (const auto& operation : candidate.impl->primitiveBinaryOperations) {
        if (operation.node != expectedValue) continue;
        if (binary != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        binary = operation;
      }
      // Exactly one materialized value must match the write's kind: a literal
      // write materializes a scalar literal, a reference write a parameter
      // reference, a binary write a HirPrimitiveBinaryExpression. The three are
      // mutually exclusive.
      const bool binaryOk =
          sourceBinaryValue ? (binary != zc::none && literal == zc::none && parameter == zc::none)
                            : binary == zc::none;
      if (write == zc::none || localBinding == zc::none || !binaryOk ||
          (sourceReferenceValue ? (parameter == zc::none || literal != zc::none)
           : sourceBinaryValue  ? false
                                : (literal == zc::none || parameter != zc::none))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ZC_IF_SOME(writeValue, write) {
        ZC_IF_SOME(local, localBinding) {
          const auto& assignmentFact = facts.nodeTypes().entries()[assignmentSlot].value;
          const auto& targetFact = facts.nodeTypes().entries()[targetSlot].value;
          const auto& valueFact = facts.nodeTypes().entries()[valueSlot].value;
          bool firstFieldWrite = writeValue.field != zc::none;
          if (firstFieldWrite) {
            for (const auto& previous : candidate.impl->localWrites) {
              if (previous.node.ordinal() >= expectedWrite.ordinal() ||
                  previous.field != writeValue.field) {
                continue;
              }
              firstFieldWrite = false;
              break;
            }
          }
          const auto expectedKind =
              !localHasInitializer &&
                      (writeValue.field != zc::none ? firstFieldWrite : writeIndex == 0)
                  ? HirLocalWriteKind::Initialize
                  : HirLocalWriteKind::Overwrite;
          if (writeValue.local != local.local ||
              (writeValue.field == zc::none && writeValue.type != local.type) ||
              writeValue.value != expectedValue || assignmentFact != writeValue.type ||
              targetFact != writeValue.type || valueFact != writeValue.type ||
              writeValue.kind != expectedKind ||
              !sameSpan(writeValue.sourceSpan, ZC_ASSERT_NONNULL(sourceWriteSpan)) ||
              !sameSpan(writeValue.valueSpan, ZC_ASSERT_NONNULL(sourceValueSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          if (sourceReferenceValue) {
            // A reference write value is a parameter reference: it carries the
            // write's type, is a place category, and shares the value node id.
            ZC_IF_SOME(parameterValue, parameter) {
              if (parameterValue.type != writeValue.type ||
                  parameterValue.category != HirValueCategory::Place ||
                  !sameSpan(parameterValue.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan))) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::InvalidFact, module,
                                                    registries, index + 1);
              }
            }
          } else if (sourceBinaryValue) {
            // A binary write value is a HirPrimitiveBinaryExpression at the value
            // node id, whose two operands live at trailing node ids reserved after
            // the return value and any unsafe block, in write order. Its checked
            // call fact is keyed on the RHS binary node. The operands are each a
            // scalar literal or a parameter reference. The operand ordinals are
            // derived identically to the materializer: the trailing base is the
            // node after the function's return value / unsafe block, then each
            // earlier binary write consumes two ids.
            //
            // A postfix write (`x++` -> `x = x + 1`) has no RHS binary node in
            // the source: the left operand is the postfix target and the right
            // operand is a synthetic literal 1 with no AST node. The call fact is
            // keyed on the PostfixExpression node and records PostIncrement or
            // PostDecrement, not the desugared Add/Sub.
            const ast::NodeId binaryLeftNode(
                isPostfixWrite ? sourceTarget
                : isCompoundWrite
                    ? sourceTarget
                    : ast::NodeId(
                          tree.node(sourceWriteValue).payload.words[ast::kBinaryExprLhsWord]));
            const ast::NodeId binaryRightNode(
                isPostfixWrite ? ast::NodeId()
                : isCompoundWrite
                    ? sourceWriteValue
                    : ast::NodeId(
                          tree.node(sourceWriteValue).payload.words[ast::kBinaryExprRhsWord]));
            uint32_t trailingBase = expectedFunction + (localHasInitializer ? 6 : 5) +
                                    static_cast<uint32_t>(functionLocalWriteCount) * 2 +
                                    (source.unsafeBlock != zc::none ? 1u : 0u);
            uint32_t priorBinaryWrites = 0;
            for (size_t earlier = 0; earlier < writeIndex; ++earlier) {
              auto earlierStatement = statementItem(tree, tree.list(source.localWrites)[earlier]);
              ZC_IF_SOME(earlierValue, earlierStatement) {
                const ast::NodeId earlierWrite(
                    tree.node(earlierValue).payload.words[ast::kExpressionStatementExpressionWord]);
                if (tree.node(earlierWrite).kind == ast::SyntaxKind::PostfixExpression) {
                  ++priorBinaryWrites;
                  continue;
                }
                if (tree.node(earlierWrite).kind == ast::SyntaxKind::AssignmentExpr &&
                    isCompoundAssignment(static_cast<ast::AssignmentOperatorKind>(
                        tree.node(earlierWrite).payload.words[ast::kAssignmentExprOpWord]))) {
                  ++priorBinaryWrites;
                  continue;
                }
                const ast::NodeId earlierRhs(
                    tree.node(earlierWrite).payload.words[ast::kAssignmentExprRhsWord]);
                if (tree.contains(earlierRhs) &&
                    tree.node(earlierRhs).kind == ast::SyntaxKind::BinaryExpr) {
                  ++priorBinaryWrites;
                }
              }
            }
            const auto leftOperandId = hirId(trailingBase + priorBinaryWrites * 2);
            const auto rightOperandId = hirId(trailingBase + priorBinaryWrites * 2 + 1);
            // A compound assignment has no checked call fact: the body checker
            // admits the operator without producing one. The binary operation is
            // derived directly from the compound operator kind, and both operands
            // share the written local's type.
            checker::PrimitiveOperation operation;
            bool comparison = false;
            bool arithmetic = false;
            identity::SemanticTypeId binaryOperandType = writeValue.type;
            bool operationSupported = false;
            const checker::checked::TypedCallFact* callFact = nullptr;
            const checker::checked::CheckedCallEnvelope* call = nullptr;
            if (isCompoundWrite) {
              const auto writeOp = static_cast<ast::AssignmentOperatorKind>(
                  tree.node(sourceWrite).payload.words[ast::kAssignmentExprOpWord]);
              auto compoundOp = compoundAssignmentBinaryOperation(writeOp);
              if (compoundOp == zc::none) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::InvalidFact, module,
                                                    registries, index + 1);
              }
              operation = ZC_ASSERT_NONNULL(compoundOp);
              arithmetic = isScalarArithmeticOperation(operation);
              operationSupported = arithmetic;
            } else {
              auto callIndex = factIndex(facts.calls(), sourceWriteValue);
              if (callIndex == zc::none) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::MissingRequiredFact, module,
                                                    registries, index + 1);
              }
              size_t callSlot = 0;
              ZC_IF_SOME(value, callIndex) { callSlot = value; }
              callFact = &facts.calls().entries()[callSlot].value;
              call = &callFact->invocation;
              const auto& selected = call->selected.variant();
              if (!selected.is<checker::checked::PrimitiveCallable>()) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::InvalidFact, module,
                                                    registries, index + 1);
              }
              operation = selected.get<checker::checked::PrimitiveCallable>().operation;
              comparison = isScalarComparisonOperation(operation);
              arithmetic = isScalarArithmeticOperation(operation);
              binaryOperandType =
                  call->arguments.size() == 2 ? call->arguments[0].sourceType : writeValue.type;
              operationSupported =
                  comparison || (arithmetic && writeValue.type == binaryOperandType);
            }
            // Verify one operand at a fixed node id against its classification: a
            // scalar literal or a parameter reference of the operand type. Shared
            // by the compound-assignment and regular binary write paths.
            auto verifyWriteOperand = [&](HirNodeId operandId, ast::NodeId operandNode) -> bool {
              auto operandSpan = bound.parsedModule().spanFor(tree.node(operandNode).range);
              if (operandSpan == zc::none) return false;
              if (isScalarLiteral(tree.node(operandNode).kind)) {
                zc::Maybe<const HirScalarLiteralExpression&> operandLiteral;
                for (const auto& expression : candidate.impl->expressions) {
                  if (expression.node != operandId) continue;
                  if (operandLiteral != zc::none) return false;
                  operandLiteral = expression;
                }
                auto operandLiteralIndex = factIndex(facts.literals(), operandNode);
                if (operandLiteral == zc::none || operandLiteralIndex == zc::none) return false;
                size_t operandLiteralSlot = 0;
                ZC_IF_SOME(value, operandLiteralIndex) { operandLiteralSlot = value; }
                const auto& operandLiteralFact =
                    facts.literals().entries()[operandLiteralSlot].value;
                const auto& literalValue = ZC_ASSERT_NONNULL(operandLiteral);
                return literalValue.type == binaryOperandType &&
                       literalValue.category == HirValueCategory::Value &&
                       operandLiteralFact.type == binaryOperandType &&
                       sameConstant(literalValue.value, operandLiteralFact.literal, module,
                                    registries, semanticTypes) &&
                       sameSpan(literalValue.sourceSpan, ZC_ASSERT_NONNULL(operandSpan));
              }
              // A non-literal operand that names the written user local
              // (`x = x + 1`) materializes a localReference, not a
              // parameterReference.
              auto ownerBinding = resolvedOwnerLocal(bound.bindings(), operandNode);
              if (ownerBinding != zc::none) {
                zc::Maybe<const HirLocalReferenceExpression&> operandLocal;
                for (const auto& reference : candidate.impl->localReferences) {
                  if (reference.node != operandId) continue;
                  if (operandLocal != zc::none) return false;
                  operandLocal = reference;
                }
                if (operandLocal == zc::none) return false;
                const auto& localValue = ZC_ASSERT_NONNULL(operandLocal);
                return localValue.local == ZC_ASSERT_NONNULL(localBinding).local &&
                       localValue.type == binaryOperandType &&
                       localValue.category == HirValueCategory::Place &&
                       sameSpan(localValue.sourceSpan, ZC_ASSERT_NONNULL(operandSpan));
              }
              zc::Maybe<const HirParameterReferenceExpression&> operandReference;
              for (const auto& reference : candidate.impl->parameterReferences) {
                if (reference.node != operandId) continue;
                if (operandReference != zc::none) return false;
                operandReference = reference;
              }
              auto parameterHandle = resolvedCallableParameter(bound.bindings(), operandNode);
              if (operandReference == zc::none || parameterHandle == zc::none) return false;
              bool parameterMatches = false;
              ZC_IF_SOME(handle, parameterHandle) {
                auto authority = registries.callableParameter(handle);
                ZC_IF_SOME(entry, authority) {
                  parameterMatches = ZC_ASSERT_NONNULL(operandReference).parameter == entry.key();
                }
              }
              const auto& referenceValue = ZC_ASSERT_NONNULL(operandReference);
              return parameterMatches && referenceValue.type == binaryOperandType &&
                     referenceValue.category == HirValueCategory::Place &&
                     sameSpan(referenceValue.sourceSpan, ZC_ASSERT_NONNULL(operandSpan));
            };
            if (isPostfixWrite) {
              // Postfix desugaring: the call fact records PostIncrement or
              // PostDecrement with one argument (the postfix target), and the
              // binary lowers to Add or Sub. The right operand is a synthetic
              // literal 1 with no AST node and no checked literal fact.
              const bool postfixIncrement = operation == checker::PrimitiveOperation::PostIncrement;
              const bool postfixDecrement = operation == checker::PrimitiveOperation::PostDecrement;
              if (!postfixIncrement && !postfixDecrement) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::InvalidFact, module,
                                                    registries, index + 1);
              }
              const auto expectedBinaryOperation = postfixIncrement
                                                       ? checker::PrimitiveOperation::Add
                                                       : checker::PrimitiveOperation::Sub;
              ZC_IF_SOME(binaryValue, binary) {
                if (binaryValue.node != expectedValue || binaryValue.left != leftOperandId ||
                    binaryValue.right != rightOperandId || binaryValue.type != writeValue.type ||
                    binaryValue.operandType != binaryOperandType ||
                    binaryValue.category != HirValueCategory::Value ||
                    binaryValue.operation != expectedBinaryOperation ||
                    !sameSpan(binaryValue.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan)) ||
                    callFact->node != sourceWriteValue || call->calleeType != binaryOperandType ||
                    call->receiver != zc::none || call->receiverMode != zc::none ||
                    call->receiverAdjustment != zc::none || call->arguments.size() != 1 ||
                    call->arguments[0].sourceNode != binaryLeftNode ||
                    call->arguments[0].sourceType != binaryOperandType ||
                    call->successType != writeValue.type || call->resultType != writeValue.type ||
                    call->substitutions != zc::none || call->witnesses != zc::none ||
                    call->raises != zc::none) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
                // Left operand: a local reference to the written user local.
                auto operandSpan = bound.parsedModule().spanFor(tree.node(binaryLeftNode).range);
                if (operandSpan == zc::none) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
                auto ownerBinding = resolvedOwnerLocal(bound.bindings(), binaryLeftNode);
                if (ownerBinding == zc::none) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
                zc::Maybe<const HirLocalReferenceExpression&> operandLocal;
                for (const auto& reference : candidate.impl->localReferences) {
                  if (reference.node != leftOperandId) continue;
                  if (operandLocal != zc::none) {
                    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                        ir::IrFailureKind::AdditionalFact, module,
                                                        registries, index + 1);
                  }
                  operandLocal = reference;
                }
                if (operandLocal == zc::none) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::MissingRequiredFact,
                                                      module, registries, index + 1);
                }
                const auto& localValue = ZC_ASSERT_NONNULL(operandLocal);
                if (localValue.local != ZC_ASSERT_NONNULL(localBinding).local ||
                    localValue.type != binaryOperandType ||
                    localValue.category != HirValueCategory::Place ||
                    !sameSpan(localValue.sourceSpan, ZC_ASSERT_NONNULL(operandSpan))) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
                // Right operand: a synthetic literal 1 with no AST node and no
                // checked literal fact.
                zc::Maybe<const HirScalarLiteralExpression&> operandLiteral;
                for (const auto& expression : candidate.impl->expressions) {
                  if (expression.node != rightOperandId) continue;
                  if (operandLiteral != zc::none) {
                    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                        ir::IrFailureKind::AdditionalFact, module,
                                                        registries, index + 1);
                  }
                  operandLiteral = expression;
                }
                if (operandLiteral == zc::none) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::MissingRequiredFact,
                                                      module, registries, index + 1);
                }
                const auto& literalValue = ZC_ASSERT_NONNULL(operandLiteral);
                if (literalValue.type != binaryOperandType ||
                    literalValue.category != HirValueCategory::Value) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
                auto integerValue = literalValue.value.integerValue();
                if (integerValue == zc::none ||
                    ZC_ASSERT_NONNULL(integerValue).sign !=
                        checker::signature::IntegerSign::NonNegative ||
                    ZC_ASSERT_NONNULL(integerValue).magnitude.size() != 1 ||
                    ZC_ASSERT_NONNULL(integerValue).magnitude[0] != 1) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
              }
            } else if (isCompoundWrite) {
              // Compound assignment desugaring (`x += 1` -> `x = x + 1`): the
              // binary's operation is derived from the compound operator kind,
              // and there is no checked call fact. The binary's source span is
              // the assignment span, not the RHS span.
              ZC_IF_SOME(binaryValue, binary) {
                if (!operationSupported || binaryValue.node != expectedValue ||
                    binaryValue.left != leftOperandId || binaryValue.right != rightOperandId ||
                    binaryValue.type != writeValue.type ||
                    binaryValue.operandType != binaryOperandType ||
                    binaryValue.category != HirValueCategory::Value ||
                    binaryValue.operation != operation ||
                    !sameSpan(binaryValue.sourceSpan, ZC_ASSERT_NONNULL(sourceWriteSpan))) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
                if (!verifyWriteOperand(leftOperandId, binaryLeftNode) ||
                    !verifyWriteOperand(rightOperandId, binaryRightNode)) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
              }
            } else {
              ZC_IF_SOME(binaryValue, binary) {
                if (!operationSupported || binaryValue.node != expectedValue ||
                    binaryValue.left != leftOperandId || binaryValue.right != rightOperandId ||
                    binaryValue.type != writeValue.type ||
                    binaryValue.operandType != binaryOperandType ||
                    binaryValue.category != HirValueCategory::Value ||
                    binaryValue.operation != operation ||
                    !sameSpan(binaryValue.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan)) ||
                    callFact->node != sourceWriteValue || call->calleeType != binaryOperandType ||
                    call->receiver != zc::none || call->receiverMode != zc::none ||
                    call->receiverAdjustment != zc::none || call->arguments.size() != 2 ||
                    call->arguments[0].sourceNode != binaryLeftNode ||
                    call->arguments[0].sourceType != binaryOperandType ||
                    call->arguments[1].sourceNode != binaryRightNode ||
                    call->arguments[1].sourceType != binaryOperandType ||
                    call->successType != writeValue.type || call->resultType != writeValue.type ||
                    call->substitutions != zc::none || call->witnesses != zc::none ||
                    call->raises != zc::none) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
                if (!verifyWriteOperand(leftOperandId, binaryLeftNode) ||
                    !verifyWriteOperand(rightOperandId, binaryRightNode)) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
              }
            }
          } else {
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            ZC_IF_SOME(literalValue, literal) {
              if (literalValue.type != writeValue.type ||
                  literalValue.category != HirValueCategory::Value ||
                  literalFact.type != writeValue.type ||
                  !sameConstant(literalValue.value, literalFact.literal, module, registries,
                                semanticTypes) ||
                  !sameSpan(literalValue.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan)) ||
                  !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan))) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::InvalidFact, module,
                                                    registries, index + 1);
              }
            }
          }
        }
      }
    }
    if (localFieldProjection != zc::none && !localHasInitializer && hasLocalWrite) {
      const auto baseIncrement = static_cast<uint32_t>(5 + functionLocalWriteCount * 2);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
      continue;
    }
    const auto& signature = signatures.definitions[signatureSlot];
    zc::Maybe<checker::signature::MemberSignatureScope> memberScope;
    if (isMethod) {
      if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
          !signature.scope.variant().is<checker::signature::MemberSignatureScope>()) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      memberScope = signature.scope.variant().get<checker::signature::MemberSignatureScope>();
    } else if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
               !signature.scope.variant()
                    .is<checker::signature::ModuleDefinitionSignatureScope>()) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& callable = signature.payload.variant().get<checker::signature::CallableSignature>();
    // An ordinary function has no receiver while an inherent method must have
    // exactly one, and the materialized header must agree.
    if ((callable.receiver != zc::none) != isMethod ||
        (function.receiver != zc::none) != isMethod) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    zc::Maybe<HirVisibility> expectedVisibility;
    if (isMethod) {
      ZC_IF_SOME(scope, memberScope) {
        expectedVisibility = memberVisibility(scope.visibility, module);
      }
    } else {
      expectedVisibility = visibility(signatures.roots[rootSlot].visibility);
    }
    auto expectedLinkage = linkage(callable);
    bool visibilityMatches = false;
    bool linkageMatches = false;
    bool bodySpanMatches = false;
    bool returnSpanMatches = false;
    ZC_IF_SOME(value, expectedVisibility) {
      visibilityMatches = sameVisibility(function.visibility, value);
    }
    ZC_IF_SOME(value, expectedLinkage) { linkageMatches = function.linkage == value; }
    ZC_IF_SOME(value, bodySpan) { bodySpanMatches = sameSpan(block.sourceSpan, value); }
    ZC_IF_SOME(value, returnSpan) {
      returnSpanMatches = sameSpan(returnStatement.sourceSpan, value);
    }
    const bool moduleMembershipValid =
        isMethod ? definitionBelongsToModule(sourceDefinition, definitions)
                 : (signatures.roots[rootSlot].canonicalDefinition == function.definition &&
                    signatures.roots[rootSlot].sourceModule == module);
    if (signature.definition != function.definition ||
        signature.definitionKind !=
            (isMethod ? identity::DefinitionKind::Method : identity::DefinitionKind::Function) ||
        !moduleMembershipValid || callable.raises != zc::none ||
        callable.success != function.resultType ||
        !sameSpan(function.sourceSpan, sourceDefinition.source) ||
        !sameSpan(signature.declarationSpan, sourceDefinition.source) || !visibilityMatches ||
        !linkageMatches || !bodySpanMatches || !returnSpanMatches) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    // The member scope's owner is the enclosing nominal, never the method
    // itself; the receiver type is validated below against that same owner.
    ZC_IF_SOME(scope, memberScope) {
      if (scope.owner == function.definition) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
    }
    if (isMethod) {
      bool receiverMatches = false;
      ZC_IF_SOME(expected, callable.receiver) {
        ZC_IF_SOME(actual, function.receiver) {
          if (actual.key == expected.parameter) {
            auto receiverTypeLookup = semanticTypes.get(actual.type);
            if (receiverTypeLookup.is<type::SemanticTypeLookup>()) {
              const auto& receiverData = receiverTypeLookup.get<type::SemanticTypeLookup>().data();
              if (receiverData.is<type::semantic::ReferenceTypeData>()) {
                const auto& reference = receiverData.get<type::semantic::ReferenceTypeData>();
                const auto expectedMutability =
                    expected.mode == checker::signature::ReceiverMode::Mutable
                        ? type::semantic::Mutability::Mutable
                        : type::semantic::Mutability::Const;
                auto referentLookup = semanticTypes.get(reference.referent);
                if (reference.mutability == expectedMutability &&
                    referentLookup.is<type::SemanticTypeLookup>()) {
                  const auto& referentData = referentLookup.get<type::SemanticTypeLookup>().data();
                  ZC_IF_SOME(scope, memberScope) {
                    if (referentData.is<type::semantic::NominalTypeData>() &&
                        referentData.get<type::semantic::NominalTypeData>().definition ==
                            scope.owner) {
                      receiverMatches = true;
                    }
                  }
                }
              }
            }
          }
        }
      }
      if (!receiverMatches) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
    }

    const auto paramsWord =
        isMethod ? ast::kMethodDeclParamsIdWord : ast::kFunctionDeclParamsIdWord;
    const ast::NodeId parameterListNode(tree.node(sourceDefinition.node).payload.words[paramsWord]);
    if (!tree.contains(parameterListNode) ||
        tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& parameterList = tree.node(parameterListNode);
    const ast::NodeList parameterNodes{
        parameterList.payload.words[ast::kFunctionParameterListParamsFirstWord],
        parameterList.payload.words[ast::kFunctionParameterListParamsSizeWord]};
    // A method's AST parameter list may lead with the implicit `this`
    // receiver; it is verified separately and never counted as an ordinary
    // parameter.
    if (!tree.contains(parameterNodes) || parameterNodes.size < function.parameters.size()) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    const size_t receiverCount = parameterNodes.size - function.parameters.size();
    if ((!isMethod && receiverCount != 0) || (isMethod && receiverCount > 1) ||
        (isMethod && (receiverCount == 1) != (function.receiver != zc::none))) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    for (size_t parameterIndex = 0; parameterIndex < function.parameters.size(); ++parameterIndex) {
      const auto parameterNode = tree.list(parameterNodes)[receiverCount + parameterIndex];
      if (!tree.contains(parameterNode) ||
          tree.node(parameterNode).kind != ast::SyntaxKind::FunctionParameterDecl) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto parameterSpan = bound.parsedModule().spanFor(tree.node(parameterNode).range);
      const auto& parameter = function.parameters[parameterIndex];
      const auto& signatureParameter = callable.parameters[parameterIndex];
      if (parameterSpan == zc::none || parameter.key != signatureParameter.parameter ||
          parameter.type != signatureParameter.type || signatureParameter.hasDefault ||
          !typeExists(parameter.type, semanticTypes) ||
          !sameSpan(parameter.sourceSpan, ZC_ASSERT_NONNULL(parameterSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
    }
    if (isMethod && receiverCount == 1) {
      const auto receiverNode = tree.list(parameterNodes)[0];
      if (!tree.contains(receiverNode) ||
          tree.node(receiverNode).kind != ast::SyntaxKind::FunctionParameterDecl) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto receiverSpan = bound.parsedModule().spanFor(tree.node(receiverNode).range);
      if (receiverSpan == zc::none || !sameSpan(ZC_ASSERT_NONNULL(function.receiver).sourceSpan,
                                                ZC_ASSERT_NONNULL(receiverSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
    }

    if (returnsLocalAliasReborrow) {
      ast::NodeId initializer;
      ZC_IF_SOME(value, source.localInitializer) { initializer = value; }
      auto alias = reborrowReference(tree, sourceReturnValueNode);
      if (source.localInitializer == zc::none || alias == zc::none ||
          parameterReference == zc::none || literalExpression != zc::none ||
          directCall != zc::none || localReference != zc::none ||
          localFieldProjection != zc::none || aggregateExpression != zc::none || hasLocalWrite ||
          function.node.ordinal() != expectedFunction ||
          block.node.ordinal() != expectedFunction + 1 || returnStatement.node != returnNode ||
          function.body != block.node || block.statements.size() != 2 ||
          block.statements[0] != ZC_ASSERT_NONNULL(localBinding).node ||
          block.statements[1] != returnStatement.node || returnStatement.value != valueNode ||
          function.resultType != returnStatement.resultType ||
          ZC_ASSERT_NONNULL(localBinding).node != hirId(expectedFunction + 2) ||
          ZC_ASSERT_NONNULL(localBinding).initializer != hirId(expectedFunction + 3) ||
          ZC_ASSERT_NONNULL(localBinding).local != hirLocalId(1) ||
          ZC_ASSERT_NONNULL(parameterReference).node != hirId(expectedFunction + 3) ||
          ZC_ASSERT_NONNULL(parameterReborrow).node != valueNode ||
          ZC_ASSERT_NONNULL(parameterReborrow).sourceAlias == zc::none ||
          ZC_ASSERT_NONNULL(parameterReborrow).type != function.resultType ||
          ZC_ASSERT_NONNULL(parameterReborrow).sourceType != ZC_ASSERT_NONNULL(localBinding).type) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      ZC_IF_SOME(sourceAlias, ZC_ASSERT_NONNULL(parameterReborrow).sourceAlias) {
        if (sourceAlias != ZC_ASSERT_NONNULL(localBinding).local) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      auto aliasBinding = resolvedOwnerLocal(bound.bindings(), ZC_ASSERT_NONNULL(alias));
      auto parameterBinding = resolvedCallableParameter(bound.bindings(), initializer);
      auto initializerType = factIndex(facts.nodeTypes(), initializer);
      auto aliasType = factIndex(facts.nodeTypes(), ZC_ASSERT_NONNULL(alias));
      auto returnType = factIndex(facts.nodeTypes(), sourceReturnValueNode);
      auto patternSpan = bound.parsedModule().spanFor(tree.node(source.localPattern).range);
      auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
      auto returnSpan = bound.parsedModule().spanFor(tree.node(sourceReturnValueNode).range);
      if (aliasBinding == zc::none || parameterBinding == zc::none || initializerType == zc::none ||
          aliasType == zc::none || returnType == zc::none || patternSpan == zc::none ||
          initializerSpan == zc::none || returnSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(aliasBinding), source.localPattern,
                             tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t initializerSlot = 0;
      size_t aliasSlot = 0;
      size_t returnSlot = 0;
      ZC_IF_SOME(value, initializerType) { initializerSlot = value; }
      ZC_IF_SOME(value, aliasType) { aliasSlot = value; }
      ZC_IF_SOME(value, returnType) { returnSlot = value; }
      auto parameterAuthority = registries.callableParameter(ZC_ASSERT_NONNULL(parameterBinding));
      if (parameterAuthority == zc::none ||
          facts.nodeTypes().entries()[initializerSlot].value !=
              ZC_ASSERT_NONNULL(localBinding).type ||
          facts.nodeTypes().entries()[aliasSlot].value != ZC_ASSERT_NONNULL(localBinding).type ||
          facts.nodeTypes().entries()[returnSlot].value != function.resultType ||
          ZC_ASSERT_NONNULL(parameterReference).type != ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(parameterReference).category != HirValueCategory::Place ||
          !sameSpan(ZC_ASSERT_NONNULL(localBinding).sourceSpan, ZC_ASSERT_NONNULL(patternSpan)) ||
          !sameSpan(ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(localBinding).initializerSpan),
                    ZC_ASSERT_NONNULL(initializerSpan)) ||
          !sameSpan(ZC_ASSERT_NONNULL(parameterReference).sourceSpan,
                    ZC_ASSERT_NONNULL(initializerSpan)) ||
          !sameSpan(ZC_ASSERT_NONNULL(parameterReborrow).sourceSpan,
                    ZC_ASSERT_NONNULL(returnSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      ZC_IF_SOME(parameter, parameterAuthority) {
        if (ZC_ASSERT_NONNULL(parameterReborrow).parameter != parameter.key()) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      const auto baseIncrement = static_cast<uint32_t>(6);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
      continue;
    }

    if (returnsLocal && source.localInitializer == zc::none && !hasLocalWrite) {
      auto returnTypeIndex = factIndex(facts.nodeTypes(), sourceReturnValueNode);
      auto binding = resolvedOwnerLocal(bound.bindings(), sourceReturnValueNode);
      auto patternSpan = bound.parsedModule().spanFor(tree.node(source.localPattern).range);
      auto referenceSpan = bound.parsedModule().spanFor(tree.node(sourceReturnValueNode).range);
      if (returnTypeIndex == zc::none || binding == zc::none || patternSpan == zc::none ||
          referenceSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(binding), source.localPattern, tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ZC_IF_SOME(local, localBinding) {
        ZC_IF_SOME(reference, localReference) {
          size_t returnTypeSlot = 0;
          ZC_IF_SOME(value, returnTypeIndex) { returnTypeSlot = value; }
          if (local.node != hirId(expectedFunction + 2) || local.initializer != zc::none ||
              local.initializerSpan != zc::none || local.local != hirLocalId(1) ||
              reference.local != local.local || reference.type != local.type ||
              reference.category != HirValueCategory::Place || block.statements[0] != local.node ||
              local.type != function.resultType ||
              facts.nodeTypes().entries()[returnTypeSlot].value != local.type ||
              !sameSpan(local.sourceSpan, ZC_ASSERT_NONNULL(patternSpan)) ||
              !sameSpan(reference.sourceSpan, ZC_ASSERT_NONNULL(referenceSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
      }
      const auto baseIncrement = static_cast<uint32_t>(5);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
      continue;
    }

    if (returnsLocal && source.localInitializer == zc::none && localFieldProjection == zc::none) {
      auto returnTypeIndex = factIndex(facts.nodeTypes(), sourceReturnValueNode);
      auto binding = resolvedOwnerLocal(bound.bindings(), sourceReturnValueNode);
      auto patternSpan = bound.parsedModule().spanFor(tree.node(source.localPattern).range);
      auto referenceSpan = bound.parsedModule().spanFor(tree.node(sourceReturnValueNode).range);
      if (returnTypeIndex == zc::none || binding == zc::none || patternSpan == zc::none ||
          referenceSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(binding), source.localPattern, tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ZC_IF_SOME(local, localBinding) {
        ZC_IF_SOME(reference, localReference) {
          size_t returnTypeSlot = 0;
          ZC_IF_SOME(value, returnTypeIndex) { returnTypeSlot = value; }
          if (local.node != hirId(expectedFunction + 2) || local.initializer != zc::none ||
              local.initializerSpan != zc::none || local.local != hirLocalId(1) ||
              reference.local != local.local || reference.type != local.type ||
              reference.category != HirValueCategory::Place || block.statements[0] != local.node ||
              local.type != function.resultType ||
              facts.nodeTypes().entries()[returnTypeSlot].value != local.type ||
              !sameSpan(local.sourceSpan, ZC_ASSERT_NONNULL(patternSpan)) ||
              !sameSpan(reference.sourceSpan, ZC_ASSERT_NONNULL(referenceSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          for (size_t writeIndex = 0; writeIndex < functionLocalWriteCount; ++writeIndex) {
            const auto expectedWrite = hirId(expectedFunction + 3 + writeIndex * 2);
            const auto expectedValue = hirId(expectedFunction + 4 + writeIndex * 2);
            auto write = zc::Maybe<const HirLocalWriteStatement&>();
            auto literal = zc::Maybe<const HirScalarLiteralExpression&>();
            for (const auto& candidateWrite : candidate.impl->localWrites) {
              if (candidateWrite.node == expectedWrite) { write = candidateWrite; }
            }
            for (const auto& expression : candidate.impl->expressions) {
              if (expression.node == expectedValue) { literal = expression; }
            }
            if (write == zc::none || literal == zc::none ||
                ZC_ASSERT_NONNULL(write).kind != (writeIndex == 0 ? HirLocalWriteKind::Initialize
                                                                  : HirLocalWriteKind::Overwrite) ||
                ZC_ASSERT_NONNULL(write).local != local.local ||
                ZC_ASSERT_NONNULL(write).type != local.type ||
                ZC_ASSERT_NONNULL(write).value != expectedValue ||
                ZC_ASSERT_NONNULL(literal).node != expectedValue ||
                ZC_ASSERT_NONNULL(literal).type != local.type ||
                ZC_ASSERT_NONNULL(literal).category != HirValueCategory::Value ||
                block.statements[writeIndex + 1] != expectedWrite) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
          }
        }
      }
      const auto baseIncrement = static_cast<uint32_t>(5 + functionLocalWriteCount * 2);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
      continue;
    }

    if (returnsLocal && source.returnsLocalBorrow) {
      ast::NodeId localInitializer;
      ZC_IF_SOME(value, source.localInitializer) { localInitializer = value; }
      auto initializerTypeIndex = factIndex(facts.nodeTypes(), localInitializer);
      auto returnTypeIndex = factIndex(facts.nodeTypes(), sourceReturnValueNode);
      auto binding = resolvedOwnerLocal(bound.bindings(), source.localReference);
      auto patternSpan = bound.parsedModule().spanFor(tree.node(source.localPattern).range);
      auto initializerSpan = bound.parsedModule().spanFor(tree.node(localInitializer).range);
      auto referenceSpan = bound.parsedModule().spanFor(tree.node(sourceReturnValueNode).range);
      if (initializerTypeIndex == zc::none || returnTypeIndex == zc::none || binding == zc::none ||
          patternSpan == zc::none || initializerSpan == zc::none || referenceSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(binding), source.localPattern, tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ZC_IF_SOME(local, localBinding) {
        ZC_IF_SOME(borrow, localBorrow) {
          size_t initializerTypeSlot = 0;
          size_t returnTypeSlot = 0;
          ZC_IF_SOME(value, initializerTypeIndex) { initializerTypeSlot = value; }
          ZC_IF_SOME(value, returnTypeIndex) { returnTypeSlot = value; }
          const auto operation = static_cast<ast::UnaryOperatorKind>(
              tree.node(source.value).payload.words[ast::kUnaryExpressionOpWord]);
          const auto expectedMutability = operation == ast::UnaryOperatorKind::Ref
                                              ? type::semantic::Mutability::Const
                                              : type::semantic::Mutability::Mutable;
          if (local.node != hirId(expectedFunction + 2) || local.initializer == zc::none ||
              local.initializerSpan == zc::none ||
              local.initializer != hirId(expectedFunction + 3) || local.local != hirLocalId(1) ||
              borrow.local != local.local || borrow.sourceType != local.type ||
              borrow.type != function.resultType || borrow.mutability != expectedMutability ||
              block.statements[0] != local.node ||
              facts.nodeTypes().entries()[initializerTypeSlot].value != local.type ||
              facts.nodeTypes().entries()[returnTypeSlot].value != function.resultType ||
              !sameSpan(local.sourceSpan, ZC_ASSERT_NONNULL(patternSpan)) ||
              !sameSpan(ZC_ASSERT_NONNULL(local.initializerSpan),
                        ZC_ASSERT_NONNULL(initializerSpan)) ||
              !sameSpan(borrow.sourceSpan, ZC_ASSERT_NONNULL(referenceSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          ZC_IF_SOME(expression, literalExpression) {
            auto literalIndex = factIndex(facts.literals(), localInitializer);
            if (literalIndex == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
            if (expression.type != local.type || expression.category != HirValueCategory::Value ||
                !sameSpan(expression.sourceSpan, ZC_ASSERT_NONNULL(local.initializerSpan)) ||
                facts.literals().entries()[literalSlot].value.type != local.type ||
                !sameConstant(expression.value,
                              facts.literals().entries()[literalSlot].value.literal, module,
                              registries, semanticTypes)) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
          }
        }
      }
      const auto baseIncrement = static_cast<uint32_t>(6);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
      continue;
    }

    if (returnsLocal && localFieldProjection == zc::none) {
      ast::NodeId localInitializer;
      ZC_IF_SOME(value, source.localInitializer) { localInitializer = value; }
      auto initializerTypeIndex = factIndex(facts.nodeTypes(), localInitializer);
      auto returnTypeIndex = factIndex(facts.nodeTypes(), sourceReturnValueNode);
      auto binding = resolvedOwnerLocal(bound.bindings(), sourceReturnValueNode);
      auto patternSpan = bound.parsedModule().spanFor(tree.node(source.localPattern).range);
      auto initializerSpan = bound.parsedModule().spanFor(tree.node(localInitializer).range);
      auto referenceSpan = bound.parsedModule().spanFor(tree.node(sourceReturnValueNode).range);
      if (initializerTypeIndex == zc::none || returnTypeIndex == zc::none || binding == zc::none ||
          patternSpan == zc::none || initializerSpan == zc::none || referenceSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(binding), source.localPattern, tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ZC_IF_SOME(local, localBinding) {
        ZC_IF_SOME(reference, localReference) {
          size_t initializerTypeSlot = 0;
          size_t returnTypeSlot = 0;
          ZC_IF_SOME(value, initializerTypeIndex) { initializerTypeSlot = value; }
          ZC_IF_SOME(value, returnTypeIndex) { returnTypeSlot = value; }
          if (local.node != hirId(expectedFunction + 2) || local.initializer == zc::none ||
              local.initializerSpan == zc::none ||
              local.initializer != hirId(expectedFunction + 3) || local.local != hirLocalId(1) ||
              reference.local != local.local || reference.type != local.type ||
              reference.category != HirValueCategory::Place || block.statements[0] != local.node ||
              local.type != function.resultType ||
              facts.nodeTypes().entries()[initializerTypeSlot].value != local.type ||
              facts.nodeTypes().entries()[returnTypeSlot].value != local.type ||
              !sameSpan(local.sourceSpan, ZC_ASSERT_NONNULL(patternSpan)) ||
              !sameSpan(ZC_ASSERT_NONNULL(local.initializerSpan),
                        ZC_ASSERT_NONNULL(initializerSpan)) ||
              !sameSpan(reference.sourceSpan, ZC_ASSERT_NONNULL(referenceSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          ZC_IF_SOME(expression, literalExpression) {
            auto literalIndex = factIndex(facts.literals(), localInitializer);
            if (literalIndex == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
            if (expression.type != local.type || expression.category != HirValueCategory::Value ||
                !sameSpan(expression.sourceSpan, ZC_ASSERT_NONNULL(local.initializerSpan)) ||
                facts.literals().entries()[literalSlot].value.type != local.type ||
                !sameConstant(expression.value,
                              facts.literals().entries()[literalSlot].value.literal, module,
                              registries, semanticTypes)) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
          }
        }
      }
    }

    auto sourceValueSpan = bound.parsedModule().spanFor(tree.node(sourceValueNode).range);
    auto nodeTypeIndex = factIndex(facts.nodeTypes(), sourceValueNode);
    if (sourceValueSpan == zc::none || nodeTypeIndex == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    size_t nodeTypeSlot = 0;
    ZC_IF_SOME(value, nodeTypeIndex) { nodeTypeSlot = value; }
    const auto& sourceType = facts.nodeTypes().entries()[nodeTypeSlot].value;
    if (sourceType != function.resultType) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }

    ZC_IF_SOME(reborrow, parameterReborrow) {
      const auto& sourceNode = tree.node(sourceValueNode);
      if (sourceNode.kind != ast::SyntaxKind::UnaryExpression ||
          (static_cast<ast::UnaryOperatorKind>(
               sourceNode.payload.words[ast::kUnaryExpressionOpWord]) !=
               ast::UnaryOperatorKind::Ref &&
           static_cast<ast::UnaryOperatorKind>(
               sourceNode.payload.words[ast::kUnaryExpressionOpWord]) !=
               ast::UnaryOperatorKind::RefMut) ||
          !sameSpan(reborrow.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const ast::NodeId dereference(sourceNode.payload.words[ast::kUnaryExpressionOperandWord]);
      if (!tree.contains(dereference) ||
          tree.node(dereference).kind != ast::SyntaxKind::UnaryExpression ||
          static_cast<ast::UnaryOperatorKind>(
              tree.node(dereference).payload.words[ast::kUnaryExpressionOpWord]) !=
              ast::UnaryOperatorKind::Deref) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const ast::NodeId parameter(
          tree.node(dereference).payload.words[ast::kUnaryExpressionOperandWord]);
      auto parameterBinding = resolvedCallableParameter(bound.bindings(), parameter);
      auto parameterTypeIndex = factIndex(facts.nodeTypes(), parameter);
      auto dereferenceTypeIndex = factIndex(facts.nodeTypes(), dereference);
      if (!tree.contains(parameter) || tree.node(parameter).kind != ast::SyntaxKind::IdentExpr ||
          parameterBinding == zc::none || parameterTypeIndex == zc::none ||
          dereferenceTypeIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t parameterTypeSlot = 0;
      size_t dereferenceTypeSlot = 0;
      ZC_IF_SOME(value, parameterTypeIndex) { parameterTypeSlot = value; }
      ZC_IF_SOME(value, dereferenceTypeIndex) { dereferenceTypeSlot = value; }
      const auto parameterType = facts.nodeTypes().entries()[parameterTypeSlot].value;
      const auto referentType = facts.nodeTypes().entries()[dereferenceTypeSlot].value;
      const auto operation = static_cast<ast::UnaryOperatorKind>(
          sourceNode.payload.words[ast::kUnaryExpressionOpWord]);
      const auto expectedMutability = operation == ast::UnaryOperatorKind::Ref
                                          ? type::semantic::Mutability::Const
                                          : type::semantic::Mutability::Mutable;
      auto sourceLookup = semanticTypes.get(parameterType);
      if (!sourceLookup.is<type::SemanticTypeLookup>() ||
          !sourceLookup.get<type::SemanticTypeLookup>()
               .data()
               .is<type::semantic::ReferenceTypeData>() ||
          sourceLookup.get<type::SemanticTypeLookup>()
                  .data()
                  .get<type::semantic::ReferenceTypeData>()
                  .mutability != expectedMutability ||
          sourceLookup.get<type::SemanticTypeLookup>()
                  .data()
                  .get<type::semantic::ReferenceTypeData>()
                  .referent != referentType ||
          reborrow.sourceType != parameterType || reborrow.type != sourceType ||
          reborrow.mutability != expectedMutability) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      ZC_IF_SOME(handle, parameterBinding) {
        auto authority = registries.callableParameter(handle);
        if (authority == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        ZC_IF_SOME(entry, authority) {
          bool found = false;
          for (const auto& candidateParameter : function.parameters) {
            if (candidateParameter.key == entry.key() && candidateParameter.type == parameterType) {
              found = true;
            }
          }
          if (!found || reborrow.parameter != entry.key()) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
      }
      const auto baseIncrement = 4;
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
      continue;
    }

    ZC_IF_SOME(reference, parameterReference) {
      auto parameter = resolvedCallableParameter(bound.bindings(), sourceValueNode);
      if (tree.node(sourceValueNode).kind != ast::SyntaxKind::IdentExpr || parameter == zc::none ||
          reference.type != function.resultType || reference.category != HirValueCategory::Place ||
          !sameSpan(reference.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      ZC_IF_SOME(handle, parameter) {
        auto authority = registries.callableParameter(handle);
        if (authority == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        ZC_IF_SOME(entry, authority) {
          bool found = false;
          for (const auto& candidateParameter : function.parameters) {
            if (candidateParameter.key == entry.key() &&
                candidateParameter.type == reference.type) {
              found = true;
            }
          }
          if (!found || reference.parameter != entry.key()) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
      }
      if (returnsParameter) {
        nextFunction += 4;
        continue;
      }
    }

    ZC_IF_SOME(expression, literalExpression) {
      auto literalIndex = factIndex(facts.literals(), sourceValueNode);
      bool valueSpanMatches = false;
      ZC_IF_SOME(value, sourceValueSpan) {
        valueSpanMatches = sameSpan(expression.sourceSpan, value);
      }
      if (!isScalarLiteral(tree.node(sourceValueNode).kind) || literalIndex == zc::none ||
          expression.type != function.resultType ||
          expression.category != HirValueCategory::Value || !valueSpanMatches) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t literalSlot = 0;
      ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
      const auto& literal = facts.literals().entries()[literalSlot].value;
      if (literal.type != function.resultType ||
          !sameConstant(expression.value, literal.literal, module, registries, semanticTypes) ||
          !sameSpan(expression.sourceSpan, literal.sourceSpan)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      if (source.unsafeBlock != zc::none && !returnsLocal) {
        const auto expectedUnsafeBlock = hirId(expectedFunction + 4);
        if (function.unsafeBlock != expectedUnsafeBlock) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        zc::Maybe<const HirUnsafeBlockExpression&> unsafeBlock;
        for (const auto& candidate : candidate.impl->unsafeBlocks) {
          if (candidate.node != expectedUnsafeBlock) continue;
          if (unsafeBlock != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          unsafeBlock = candidate;
        }
        if (unsafeBlock == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        ZC_IF_SOME(block, unsafeBlock) {
          auto sourceUnsafeSpan =
              bound.parsedModule().spanFor(tree.node(ZC_ASSERT_NONNULL(source.unsafeBlock)).range);
          if (block.body != valueNode || block.type != function.resultType ||
              sourceUnsafeSpan == zc::none ||
              !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(sourceUnsafeSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        nextFunction += 5;
      } else {
        // A binary write value reserves two trailing operand node ids after the
        // return value and any unsafe block; count the binary writes so the
        // next function's fixed-id base advances past them.
        uint32_t binaryWriteOperandIds = 0;
        for (size_t writeIndex = 0; writeIndex < source.localWrites.size; ++writeIndex) {
          auto sourceStatement = statementItem(tree, tree.list(source.localWrites)[writeIndex]);
          ZC_IF_SOME(statementValue, sourceStatement) {
            const ast::NodeId sourceWrite(
                tree.node(statementValue).payload.words[ast::kExpressionStatementExpressionWord]);
            if (tree.node(sourceWrite).kind == ast::SyntaxKind::PostfixExpression) {
              binaryWriteOperandIds += 2;
              continue;
            }
            if (tree.node(sourceWrite).kind == ast::SyntaxKind::AssignmentExpr &&
                isCompoundAssignment(static_cast<ast::AssignmentOperatorKind>(
                    tree.node(sourceWrite).payload.words[ast::kAssignmentExprOpWord]))) {
              binaryWriteOperandIds += 2;
              continue;
            }
            const ast::NodeId sourceRhs(
                tree.node(sourceWrite).payload.words[ast::kAssignmentExprRhsWord]);
            if (tree.contains(sourceRhs) &&
                tree.node(sourceRhs).kind == ast::SyntaxKind::BinaryExpr) {
              binaryWriteOperandIds += 2;
            }
          }
        }
        const auto baseIncrement =
            returnsLocal
                ? static_cast<uint32_t>((localHasInitializer ? 6 : 5) + functionLocalWriteCount * 2)
                : 4;
        auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
        if (unsafeExtra == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra) + binaryWriteOperandIds;
      }
      continue;
    }

    ZC_IF_SOME(call, directCall) {
      const auto& sourceCall = tree.node(sourceValueNode);
      const ast::NodeId calleeNode(sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
      const ast::NodeList typeArguments{
          sourceCall.payload.words[ast::kCallExpressionTypeArgsFirstWord],
          sourceCall.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
      const ast::NodeList arguments{sourceCall.payload.words[ast::kCallExpressionArgsFirstWord],
                                    sourceCall.payload.words[ast::kCallExpressionArgsSizeWord]};
      auto calleeTypeIndex = factIndex(facts.nodeTypes(), calleeNode);
      auto checkedCallIndex = factIndex(facts.calls(), sourceValueNode);
      auto callKey = checkedNodeKey(tree, bound.parsedModule(), sourceValueNode);
      auto callee = resolvedDefinition(bound.bindings(), calleeNode);
      if (sourceCall.kind != ast::SyntaxKind::CallExpression || !tree.contains(calleeNode) ||
          tree.node(calleeNode).kind != ast::SyntaxKind::IdentExpr ||
          !tree.contains(typeArguments) || !tree.contains(arguments) || !typeArguments.empty() ||
          call.arguments.size() != arguments.size || calleeTypeIndex == zc::none ||
          checkedCallIndex == zc::none || callKey == zc::none || callee == zc::none ||
          call.node != materializedNode || call.resultType != function.resultType ||
          !typeExists(call.calleeType, semanticTypes)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      auto dispatchIndex = dispatchFactIndex(candidate.impl->checkedModule.dispatchFacts().facts(),
                                             ZC_ASSERT_NONNULL(callKey));
      if (dispatchIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t calleeTypeSlot = 0;
      size_t checkedCallSlot = 0;
      size_t dispatchSlot = 0;
      ZC_IF_SOME(value, calleeTypeIndex) { calleeTypeSlot = value; }
      ZC_IF_SOME(value, checkedCallIndex) { checkedCallSlot = value; }
      ZC_IF_SOME(value, dispatchIndex) { dispatchSlot = value; }
      const auto& checkedCall = facts.calls().entries()[checkedCallSlot].value;
      const auto& invocation = checkedCall.invocation;
      const auto& selected = invocation.selected.variant();
      const auto& dispatch = candidate.impl->checkedModule.dispatchFacts().facts()[dispatchSlot];
      const auto& target = dispatch.fact.target.variant();
      const auto& transform = dispatch.fact.resultTransform.variant();
      bool callSpanMatches = false;
      bool dispatchSpanMatches = false;
      bool hirSpanMatches = false;
      bool dispatchOwnerMatches = false;
      ZC_IF_SOME(value, sourceValueSpan) {
        callSpanMatches = sameSpan(checkedCall.sourceSpan, value);
        dispatchSpanMatches = sameSpan(dispatch.fact.sourceSpan, value);
        hirSpanMatches = sameSpan(call.sourceSpan, value);
      }
      ZC_IF_SOME(owner, dispatch.owner) { dispatchOwnerMatches = owner == function.definition; }
      if (call.callee != ZC_ASSERT_NONNULL(callee) ||
          call.calleeType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
          !selected.is<checker::checked::DirectCallable>() ||
          selected.get<checker::checked::DirectCallable>().callee != call.callee ||
          invocation.calleeType != call.calleeType ||
          invocation.successType != function.resultType ||
          invocation.resultType != function.resultType || invocation.receiver != zc::none ||
          invocation.receiverMode != zc::none || invocation.receiverAdjustment != zc::none ||
          invocation.arguments.size() != arguments.size || invocation.substitutions != zc::none ||
          invocation.witnesses != zc::none || invocation.raises != zc::none || !callSpanMatches ||
          !dispatchOwnerMatches || !target.is<checker::dispatch::DirectTarget>() ||
          target.get<checker::dispatch::DirectTarget>().callee != call.callee ||
          !transform.is<checker::dispatch::IdentityResultTransform>() ||
          dispatch.fact.receiver != zc::none || dispatch.fact.arguments.size() != arguments.size ||
          dispatch.fact.successType != function.resultType ||
          dispatch.fact.resultType != function.resultType ||
          dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
          dispatch.fact.raises != zc::none || !dispatchSpanMatches || !hirSpanMatches) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const auto argumentNodes = tree.list(arguments);
      for (size_t argumentIndex = 0; argumentIndex < argumentNodes.size(); ++argumentIndex) {
        const auto argument = argumentNodes[argumentIndex];
        auto argumentTypeIndex = factIndex(facts.nodeTypes(), argument);
        auto argumentKey = checkedNodeKey(tree, bound.parsedModule(), argument);
        auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
        const bool isLiteralArgument =
            tree.contains(argument) && isScalarLiteral(tree.node(argument).kind);
        const bool isParameterArgument =
            tree.contains(argument) && tree.node(argument).kind == ast::SyntaxKind::IdentExpr;
        if ((!isLiteralArgument && !isParameterArgument) || argumentTypeIndex == zc::none ||
            argumentKey == zc::none || argumentSpan == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t argumentTypeSlot = 0;
        ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
        const auto& checkedArgument = invocation.arguments[argumentIndex];
        const auto& dispatchArgument = dispatch.fact.arguments[argumentIndex];
        const auto& hirArgument = call.arguments[argumentIndex];
        const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
        if (checkedArgument.sourceNode != argument || checkedArgument.sourceType != argumentType ||
            checkedArgument.parameterType != argumentType ||
            checkedArgument.adjustment != zc::none ||
            !sameNodeKey(dispatchArgument.sourceNode, ZC_ASSERT_NONNULL(argumentKey)) ||
            dispatchArgument.sourceType != argumentType ||
            dispatchArgument.parameterType != argumentType ||
            dispatchArgument.adjustment != zc::none || hirArgument.type != argumentType ||
            !sameSpan(hirArgument.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        if (isLiteralArgument) {
          auto literalIndex = factIndex(facts.literals(), argument);
          if (literalIndex == zc::none || hirArgument.value == zc::none ||
              hirArgument.parameter != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          size_t literalSlot = 0;
          ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
          const auto& literal = facts.literals().entries()[literalSlot].value;
          bool constantMatches = false;
          ZC_IF_SOME(value, hirArgument.value) {
            constantMatches =
                sameConstant(value, literal.literal, module, registries, semanticTypes);
          }
          if (literal.node != argument || literal.type != argumentType || !constantMatches ||
              !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          continue;
        }
        // Parameter-reference argument: the HIR must carry the parameter key that
        // the argument node resolves to and no constant.
        if (hirArgument.parameter == zc::none || hirArgument.value != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto parameter = resolvedCallableParameter(bound.bindings(), argument);
        bool parameterMatches = false;
        ZC_IF_SOME(handle, parameter) {
          auto authority = registries.callableParameter(handle);
          ZC_IF_SOME(entry, authority) {
            ZC_IF_SOME(key, hirArgument.parameter) { parameterMatches = key == entry.key(); }
          }
        }
        if (!parameterMatches) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
    }
    const auto baseIncrement =
        returnsLocal
            ? static_cast<uint32_t>((localHasInitializer ? 6 : 5) + functionLocalWriteCount * 2)
            : 4;
    auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
    if (unsafeExtra == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
  }

  auto retainedBoundModule = candidate.impl->checkedModule.retainAdmittedBoundModule();
  auto retainedIdentities = candidate.impl->checkedModule.retainIdentityAuthority();
  const auto& checkedRepository = candidate.impl->checkedModule.checkedRepository();
  auto borrowEvidenceCapability = candidate.impl->checkedModule.borrowEvidenceCapability();
  const auto& semanticTypeStore = candidate.impl->checkedModule.semanticTypes();
  auto impl = zc::heap<VerifiedHirModule::Impl>(
      zc::mv(candidate.impl->checkedModule), zc::mv(retainedBoundModule),
      zc::mv(retainedIdentities), checkedRepository, zc::mv(borrowEvidenceCapability),
      semanticTypeStore, zc::mv(candidate.impl->declarations), zc::mv(candidate.impl->functions),
      zc::mv(candidate.impl->blocks), zc::mv(candidate.impl->returns),
      zc::mv(candidate.impl->patterns), zc::mv(candidate.impl->expressions),
      zc::mv(candidate.impl->aggregates), zc::mv(candidate.impl->locals),
      zc::mv(candidate.impl->localWrites), zc::mv(candidate.impl->localReferences),
      zc::mv(candidate.impl->localFieldProjections),
      zc::mv(candidate.impl->parameterFieldProjections),
      zc::mv(candidate.impl->parameterFieldWrites), zc::mv(candidate.impl->parameterReferences),
      zc::mv(candidate.impl->parameterIndexes), zc::mv(candidate.impl->parameterReborrows),
      zc::mv(candidate.impl->localBorrows), zc::mv(candidate.impl->calls),
      zc::mv(candidate.impl->receiverCalls), zc::mv(candidate.impl->unsafeBlocks),
      zc::mv(candidate.impl->primitiveBinaryOperations), zc::mv(candidate.impl->conditionals),
      zc::mv(candidate.impl->loops));
  return ir::IrOperationResult<VerifiedHirModule>::verified(VerifiedHirModule(zc::mv(impl)));
}

}  // namespace zomlang::compiler::hir
