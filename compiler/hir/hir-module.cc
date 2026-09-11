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
  const auto& signatures = candidate.impl->checkedModule.ownModuleInterface().signatures();
  const auto declarationCount = candidate.impl->declarations.size();
  const auto functionCount = candidate.impl->functions.size();
  const auto directCallCount = candidate.impl->calls.size();
  const auto receiverCallCount = candidate.impl->receiverCalls.size();
  size_t directCallArgumentCount = 0;
  size_t directCallLiteralArgumentCount = 0;
  size_t receiverCallArgumentCount = 0;
  const auto localReturnCount = candidate.impl->locals.size();
  const auto localWriteCount = candidate.impl->localWrites.size();
  const auto parameterReferenceCount = candidate.impl->parameterReferences.size();
  const auto parameterIndexCount = candidate.impl->parameterIndexes.size();
  const auto parameterReborrowCount = candidate.impl->parameterReborrows.size();
  const auto localBorrowCount = candidate.impl->localBorrows.size();
  const auto aggregateCount = candidate.impl->aggregates.size();
  const auto localFieldProjectionCount = candidate.impl->localFieldProjections.size();
  size_t localFieldWriteCount = 0;
  size_t localAliasReborrowCount = 0;
  size_t aggregateElementCount = 0;
  const auto unsafeBlockCount = candidate.impl->unsafeBlocks.size();
  const auto conditionalCount = candidate.impl->conditionals.size();
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
  }
  for (const auto& call : candidate.impl->receiverCalls) {
    receiverCallArgumentCount += call.arguments.size();
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
        ++sequentialFunctionCount;
        sequentialLocalCount += value.bindings.size();
        for (const auto& binding : value.bindings) {
          switch (binding.initializerKind) {
            case SequentialInitializerKind::Literal:
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
  // Mutable-local writes whose value node is a materialized primitive binary.
  // Each pools one entry into primitiveBinaryOperations, carries one call and one
  // dispatch fact, and its two operands are each a scalar literal (one literal
  // fact + one expression) or a parameter reference. Tally them from the
  // candidate HIR so the same corrections the builder applied balance here.
  size_t binaryWriteCount = 0;
  size_t binaryWriteParameterOperands = 0;
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
      }
    }
  }
  // The materialized primitive-binary operations pool three sources: conditional
  // conditions, comparison-return values, and sequential-local binary
  // initializers. Restore equalityConditionalCount to only the first two so the
  // pooled comparison/conditional equation terms stay exact; sequential binaries
  // and binary-write values are balanced by explicit count terms below.
  const auto equalityConditionalCount =
      candidate.impl->primitiveBinaryOperations.size() - sequentialBinaryCount - binaryWriteCount;
  // localReferences: sequential locals contribute N to the localReturnCount
  // baseline but only L_loc + R_loc + binary-local-operand actual local
  // references. Signed because binary local operands can exceed the shortfall.
  const int64_t sequentialLocalReferenceCorrection =
      static_cast<int64_t>(sequentialLocalCount) -
      static_cast<int64_t>(sequentialLocalInitializers + sequentialLocalReturns +
                           sequentialBinaryLocalOperands);
  // expressions and literals: each sequential function contributes one baseline
  // value node minus its aggregate and parameter-reference credits, but the true
  // scalar-literal count is L_lit plus any binary literal operands. Both
  // equations need the same correction. Signed because an all-reference binary
  // body carries fewer literals than the per-function baseline grants.
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
              binaryWriteCount ||
      !noUnsupportedFacts(facts) || candidate.impl->patterns.size() != declarationCount ||
      static_cast<int64_t>(candidate.impl->localReferences.size() + localFieldProjectionCount +
                           localAliasReborrowCount + localBorrowCount) +
              sequentialLocalReferenceCorrection !=
          static_cast<int64_t>(localReturnCount) ||
      parameterReferenceCount + parameterIndexCount + parameterReborrowCount >
          functionCount + localAliasReborrowCount + conditionalCount * 2 +
              equalityConditionalCount * 2 + loopCount + sequentialParameterInitializers +
              sequentialParameterReturns + sequentialBinaryParameterOperands +
              binaryWriteParameterOperands ||
      candidate.impl->blocks.size() != functionCount ||
      candidate.impl->returns.size() != functionCount ||
      static_cast<int64_t>(candidate.impl->expressions.size()) !=
          static_cast<int64_t>(declarationCount + functionCount - directCallCount - aggregateCount -
                               uninitializedLocalReturnCount - parameterReferenceCount -
                               parameterReborrowCount + localAliasReborrowCount + localWriteCount +
                               conditionalCount * 2 + equalityConditionalCount + loopCount +
                               binaryWriteCount) +
              sequentialLiteralCorrection ||
      executableDefinitions != declarationCount + functionCount ||
      facts.definitionTypes().size() != declarationCount ||
      facts.nodeTypes().size() !=
          declarationCount + functionCount + directCallCount + receiverCallCount * 2 +
              localReturnCount - uninitializedLocalReturnCount + localWriteCount * 3 +
              aggregateElementCount + localFieldProjectionCount + localFieldWriteCount +
              parameterIndexCount * 2 + parameterReborrowCount * 2 + directCallArgumentCount +
              receiverCallArgumentCount + localBorrowCount + unsafeBlockCount +
              conditionalCount * 2 + equalityConditionalCount * 2 + loopCount +
              sequentialBinaryCount * 2 + binaryWriteCount * 2 ||
      static_cast<int64_t>(facts.literals().size()) !=
          static_cast<int64_t>(declarationCount + functionCount - directCallCount - aggregateCount -
                               uninitializedLocalReturnCount - parameterReferenceCount -
                               parameterReborrowCount + localAliasReborrowCount + localWriteCount +
                               aggregateElementCount + directCallLiteralArgumentCount +
                               receiverCallArgumentCount + conditionalCount * 2 +
                               equalityConditionalCount + loopCount + binaryWriteCount) +
              sequentialLiteralCorrection ||
      facts.calls().size() != directCallCount + receiverCallCount + parameterIndexCount +
                                  equalityConditionalCount + sequentialBinaryCount +
                                  binaryWriteCount ||
      facts.patterns().size() != declarationCount || facts.aggregates().size() != aggregateCount ||
      facts.members().size() !=
          localFieldProjectionCount + localFieldWriteCount + receiverCallCount ||
      facts.places().size() !=
          localFieldProjectionCount + localFieldWriteCount + parameterIndexCount ||
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
    const auto& returnStatement = candidate.impl->returns[index];
    const uint32_t expectedFunction = nextFunction;
    // Sequential N-local shape: recompute the source classification and verify
    // the materialized bindings against it and against checked facts. Each
    // binding consumes its localNode then its initializerNode, plus two operand
    // nodes when the initializer is a primitive binary; after the last binding
    // come returnNode, returnValueNode, and an optional unsafe block. The
    // materializer and this verifier derive the same per-binding widths, so the
    // ordinals agree.
    bool isSequentialShape = false;
    {
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex != zc::none) {
        size_t definitionSlot = 0;
        ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
        const auto& sourceDefinition = definitions.definitions()[definitionSlot];
        const auto& tree = bound.tree();
        if (tree.contains(sourceDefinition.node)) {
          auto probe = functionReturnShape(tree, tree.node(sourceDefinition.node));
          ZC_IF_SOME(value, probe) { isSequentialShape = value.isSequentialLocalReturn; }
        }
      }
    }
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
        if (binding.initializerKind != SequentialInitializerKind::PrimitiveBinary) return 2u;
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
        if (binding.initializerKind == SequentialInitializerKind::Literal) {
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
      FunctionReturnShape returnShape{};
      ZC_IF_SOME(value, functionShape) { returnShape = value; }
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
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
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
      FunctionReturnShape source{};
      ZC_IF_SOME(value, sourceShape) { source = value; }
      if (source.isConditional) {
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
        // The condition is either a bare bool parameter reference or an `a == b`
        // equality comparison of two same-typed scalar parameters. The two forms
        // materialize different node counts, so the arm/conditional/return
        // offsets and the nextFunction increment differ between them.
        const bool conditionIsEquality = source.conditionIsEquality;
        const uint32_t thenOffset = conditionIsEquality ? 5 : 3;
        const uint32_t elseOffset = conditionIsEquality ? 6 : 4;
        const uint32_t conditionalOffset = conditionIsEquality ? 7 : 5;
        const uint32_t returnOffset = conditionIsEquality ? 8 : 6;
        const uint32_t functionNodeCount = conditionIsEquality ? 9 : 7;
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
              !isScalarComparisonOperation(
                  selected.get<checker::checked::PrimitiveCallable>().operation) ||
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
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
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
      FunctionReturnShape source{};
      ZC_IF_SOME(value, sourceShape) { source = value; }
      if (source.returnsComparison) {
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
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
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
      FunctionReturnShape source{};
      ZC_IF_SOME(value, sourceShape) { source = value; }
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
        FunctionReturnShape source{};
        ZC_IF_SOME(value, sourceShapeMaybe) { source = value; }
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
        if (conditionParameter == zc::none || conditionTypeIndex == zc::none ||
            conditionSpan == zc::none || loopSpan == zc::none || bodySpan == zc::none ||
            returnSpan == zc::none || returnValueSpan == zc::none) {
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
        // each binary write's two operand nodes.
        uint32_t binaryWriteNodes = 0;
        for (size_t writeIndex = 0; writeIndex < writeCount; ++writeIndex) {
          auto sourceStatement = statementItem(tree, tree.list(source.localWrites)[writeIndex]);
          ZC_IF_SOME(value, sourceStatement) {
            const ast::NodeId writeNode(
                tree.node(value).payload.words[ast::kExpressionStatementExpressionWord]);
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
          ZC_ASSERT_NONNULL(receiverCall).receiverMode != checker::checked::ReceiverMode::Mutable ||
          ZC_ASSERT_NONNULL(receiverCall).receiverAdjustments.size() != 1 ||
          ZC_ASSERT_NONNULL(receiverCall).receiverAdjustments[0] !=
              checker::checked::ReceiverAdjustmentStep::BorrowMutable ||
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
                  .mutability != type::semantic::Mutability::Mutable ||
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
      FunctionReturnShape source{};
      ZC_IF_SOME(value, sourceShape) { source = value; }
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
                adjustment.steps.size() != 1 ||
                adjustment.steps[0] != checker::checked::ReceiverAdjustmentStep::BorrowMutable) {
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
      FunctionReturnShape source{};
      ZC_IF_SOME(value, sourceShape) { source = value; }
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
      FunctionReturnShape source{};
      ZC_IF_SOME(value, sourceShape) { source = value; }
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
      FunctionReturnShape source{};
      ZC_IF_SOME(value, sourceShape) { source = value; }
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
    const auto& tree = bound.tree();
    if (!hasExecutableBody(sourceDefinition, definitions) ||
        !definitionBelongsToModule(sourceDefinition, definitions) ||
        sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
        !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
        !tree.contains(sourceDefinition.node) ||
        tree.node(sourceDefinition.node).kind != ast::SyntaxKind::FunctionDecl) {
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
    FunctionReturnShape source{};
    ZC_IF_SOME(value, sourceShape) { source = value; }
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
      const ast::NodeId sourceTarget(
          tree.node(sourceWrite).payload.words[ast::kAssignmentExprLhsWord]);
      const ast::NodeId sourceWriteValue(
          tree.node(sourceWrite).payload.words[ast::kAssignmentExprRhsWord]);
      auto sourceWriteSpan = bound.parsedModule().spanFor(tree.node(sourceWrite).range);
      auto sourceValueSpan = bound.parsedModule().spanFor(tree.node(sourceWriteValue).range);
      auto assignmentType = factIndex(facts.nodeTypes(), sourceWrite);
      auto targetType = factIndex(facts.nodeTypes(), sourceTarget);
      auto valueType = factIndex(facts.nodeTypes(), sourceWriteValue);
      // A reference or binary write value has no literal fact; a literal write
      // value does.
      const bool sourceReferenceValue =
          tree.contains(sourceWriteValue) &&
          tree.node(sourceWriteValue).kind == ast::SyntaxKind::IdentExpr;
      const bool sourceBinaryValue =
          tree.contains(sourceWriteValue) &&
          tree.node(sourceWriteValue).kind == ast::SyntaxKind::BinaryExpr;
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
            const ast::NodeId binaryLeftNode(
                tree.node(sourceWriteValue).payload.words[ast::kBinaryExprLhsWord]);
            const ast::NodeId binaryRightNode(
                tree.node(sourceWriteValue).payload.words[ast::kBinaryExprRhsWord]);
            uint32_t trailingBase = expectedFunction + (localHasInitializer ? 6 : 5) +
                                    static_cast<uint32_t>(functionLocalWriteCount) * 2 +
                                    (source.unsafeBlock != zc::none ? 1u : 0u);
            uint32_t priorBinaryWrites = 0;
            for (size_t earlier = 0; earlier < writeIndex; ++earlier) {
              auto earlierStatement = statementItem(tree, tree.list(source.localWrites)[earlier]);
              ZC_IF_SOME(earlierValue, earlierStatement) {
                const ast::NodeId earlierWrite(
                    tree.node(earlierValue).payload.words[ast::kExpressionStatementExpressionWord]);
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
              // Verify one operand at a fixed node id against its classification: a
              // scalar literal or a parameter reference of the operand type.
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
    const auto& root = signatures.roots[rootSlot];
    if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
        !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& callable = signature.payload.variant().get<checker::signature::CallableSignature>();
    auto expectedVisibility = visibility(root.visibility);
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
    if (signature.definition != function.definition ||
        signature.definitionKind != identity::DefinitionKind::Function ||
        root.canonicalDefinition != function.definition || root.sourceModule != module ||
        callable.receiver != zc::none || callable.raises != zc::none ||
        callable.success != function.resultType ||
        !sameSpan(function.sourceSpan, sourceDefinition.source) ||
        !sameSpan(signature.declarationSpan, sourceDefinition.source) || !visibilityMatches ||
        !linkageMatches || !bodySpanMatches || !returnSpanMatches) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }

    const ast::NodeId parameterListNode(
        tree.node(sourceDefinition.node).payload.words[ast::kFunctionDeclParamsIdWord]);
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
    if (!tree.contains(parameterNodes) ||
        function.parameters.size() != callable.parameters.size() ||
        function.parameters.size() != parameterNodes.size) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    for (size_t parameterIndex = 0; parameterIndex < function.parameters.size(); ++parameterIndex) {
      const auto parameterNode = tree.list(parameterNodes)[parameterIndex];
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
      zc::mv(candidate.impl->localFieldProjections), zc::mv(candidate.impl->parameterReferences),
      zc::mv(candidate.impl->parameterIndexes), zc::mv(candidate.impl->parameterReborrows),
      zc::mv(candidate.impl->localBorrows), zc::mv(candidate.impl->calls),
      zc::mv(candidate.impl->receiverCalls), zc::mv(candidate.impl->unsafeBlocks),
      zc::mv(candidate.impl->primitiveBinaryOperations), zc::mv(candidate.impl->conditionals),
      zc::mv(candidate.impl->loops));
  return ir::IrOperationResult<VerifiedHirModule>::verified(VerifiedHirModule(zc::mv(impl)));
}

}  // namespace zomlang::compiler::hir
