// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/canonical-impl-header-producer.h"

#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/identity/canonical-header-name.h"

namespace zomlang::compiler::binder {
namespace {

using identity::CanonicalBoundObligation;
using identity::CanonicalGenericParameter;
using identity::CanonicalHeaderTypeSyntax;
using identity::CanonicalImplHeader;
using identity::CanonicalNamedHeaderType;
using identity::CanonicalNameReference;
using identity::CanonicalNameRoot;
using identity::CanonicalTraitReference;
using identity::ImplPolarity;
using identity::ImplSafety;
using identity::SemanticIdentifier;

class Producer final {
public:
  Producer(const ast::Tree& tree, const ImplInventoryEntry& implementation,
           zc::ArrayPtr<const CanonicalGenericBinderFrame> enclosingBinders) noexcept
      : tree(tree), implementation(implementation), enclosingBinders(enclosingBinders) {}

  CanonicalImplHeaderProvenanceProduction produce() {
    if (!tree.contains(implementation.node)) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidImplSyntax, implementation.node);
      return failure;
    }
    const auto& syntax = tree.node(implementation.node);
    ast::NodeId genericParameters;
    ast::NodeId whereClause;
    ast::NodeId selfTypeNode;
    ImplPolarity polarity = ImplPolarity::Positive;
    ImplSafety safety = ImplSafety::Safe;
    zc::Maybe<CanonicalTraitReference> trait;

    if (syntax.kind == ast::SyntaxKind::StandaloneImplDecl) {
      genericParameters =
          ast::NodeId(syntax.payload.words[ast::kStandaloneImplDeclTypeParamsIdWord]);
      whereClause = ast::NodeId(syntax.payload.words[ast::kStandaloneImplDeclWhereWord]);
      selfTypeNode = ast::NodeId(syntax.payload.words[ast::kStandaloneImplDeclForTyWord]);
      safety = syntax.payload.words[ast::kStandaloneImplDeclIsUnsafeWord] != 0 ? ImplSafety::Unsafe
                                                                               : ImplSafety::Safe;
    } else if (syntax.kind == ast::SyntaxKind::MarkerImpl) {
      selfTypeNode = ast::NodeId(syntax.payload.words[ast::kMarkerImplForTyWord]);
      polarity = syntax.payload.words[ast::kMarkerImplIsNegatedWord] != 0 ? ImplPolarity::Negative
                                                                          : ImplPolarity::Positive;
      safety = syntax.payload.words[ast::kMarkerImplIsUnsafeWord] != 0 ? ImplSafety::Unsafe
                                                                       : ImplSafety::Safe;
      if (polarity == ImplPolarity::Negative && safety == ImplSafety::Unsafe) {
        reject(CanonicalHeaderSyntaxFailureKind::InvalidImplSyntax, implementation.node);
        return failure;
      }
      trait = buildMarkerTrait(ast::NodeId(syntax.payload.words[ast::kMarkerImplMarkerPathWord]));
      if (trait == zc::none) { return failure; }
    } else {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidImplSyntax, implementation.node);
      return failure;
    }

    zc::Vector<CanonicalGenericBinderFrame> frames(enclosingBinders.size() + 1);
    frames.add(CanonicalGenericBinderFrame{genericParameters});
    frames.addAll(enclosingBinders);
    auto binderFailure = CanonicalHeaderTypeProducer::validateBinderStack(tree, frames.asPtr());
    ZC_IF_SOME(value, binderFailure) {
      reject(value.kind, value.node);
      return failure;
    }

    zc::Vector<CanonicalGenericParameter> generics;
    zc::Vector<CanonicalBoundObligation> obligations;
    if (!buildGenericsAndObligations(genericParameters, frames.asPtr(), generics, obligations) ||
        !appendWhere(whereClause, frames.asPtr(), obligations)) {
      return failure;
    }
    if (syntax.kind == ast::SyntaxKind::StandaloneImplDecl) {
      trait = buildStandaloneTrait(
          ast::NodeId(syntax.payload.words[ast::kStandaloneImplDeclInterfaceWord]), frames.asPtr());
      if (trait == zc::none) { return failure; }
    }
    auto selfType = buildType(selfTypeNode, frames.asPtr());
    if (selfType == zc::none) { return failure; }
    ZC_IF_SOME(traitValue, trait) {
      ZC_IF_SOME(selfTypeValue, selfType) {
        auto admitted =
            CanonicalImplHeader::from(zc::mv(generics), polarity, safety, zc::mv(traitValue),
                                      zc::mv(selfTypeValue), zc::mv(obligations));
        if (admitted == zc::none) {
          reject(CanonicalHeaderSyntaxFailureKind::InvalidImplSyntax, implementation.node);
          return failure;
        }
        ZC_IF_SOME(value, admitted) {
          return CanonicalImplHeaderProvenance{zc::mv(value), zc::mv(boundOccurrences)};
        }
      }
    }
    return failure;
  }

private:
  bool reject(CanonicalHeaderSyntaxFailureKind kind, ast::NodeId node) {
    if (!rejected) {
      rejected = true;
      failure = CanonicalHeaderSyntaxFailure{kind, node};
    }
    return false;
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> buildType(
      ast::NodeId node, zc::ArrayPtr<const CanonicalGenericBinderFrame> frames) {
    auto result = CanonicalHeaderTypeProducer::produceType(tree, node, frames);
    if (result.is<CanonicalHeaderSyntaxFailure>()) {
      const auto& source = result.get<CanonicalHeaderSyntaxFailure>();
      reject(source.kind, source.node);
      return zc::none;
    }
    return zc::mv(result).get<CanonicalHeaderTypeSyntax>();
  }

  zc::Maybe<CanonicalTraitReference> buildStandaloneTrait(
      ast::NodeId node, zc::ArrayPtr<const CanonicalGenericBinderFrame> frames) {
    auto type = buildType(node, frames);
    if (type == zc::none) { return zc::none; }
    ZC_IF_SOME(value, type) {
      ZC_IF_SOME(named, value.namedType()) {
        zc::Vector<CanonicalHeaderTypeSyntax> arguments(named.arguments().size());
        for (const auto& argument : named.arguments()) { arguments.add(argument.clone()); }
        auto trait = CanonicalTraitReference::from(named.name().clone(), zc::mv(arguments));
        if (trait != zc::none) { return trait; }
      }
    }
    reject(CanonicalHeaderSyntaxFailureKind::InvalidImplSyntax, node);
    return zc::none;
  }

  zc::Maybe<CanonicalTraitReference> buildMarkerTrait(ast::NodeId path) {
    if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::AttributePath) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidImplSyntax, path);
      return zc::none;
    }
    const auto& syntax = tree.node(path);
    const ast::IdentList segments{syntax.payload.words[ast::kAttributePathSegmentsFirstWord],
                                  syntax.payload.words[ast::kAttributePathSegmentsSizeWord]};
    if (syntax.payload.words[ast::kAttributePathLeadingWord] != 0 || segments.size == 0 ||
        !tree.contains(segments)) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidImplSyntax, path);
      return zc::none;
    }
    zc::Vector<SemanticIdentifier> suffix(segments.size);
    for (const auto segment : tree.identList(segments)) {
      auto name = SemanticIdentifier::fromCanonical(tree.ident(segment));
      if (name == zc::none) {
        reject(CanonicalHeaderSyntaxFailureKind::InvalidImplSyntax, path);
        return zc::none;
      }
      ZC_IF_SOME(value, name) { suffix.add(zc::mv(value)); }
    }
    auto name = CanonicalNameReference::from(CanonicalNameRoot::relative(), zc::mv(suffix));
    if (name == zc::none) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidImplSyntax, path);
      return zc::none;
    }
    ZC_IF_SOME(value, name) {
      zc::Vector<CanonicalHeaderTypeSyntax> arguments;
      return CanonicalTraitReference::from(zc::mv(value), zc::mv(arguments));
    }
    return zc::none;
  }

  CanonicalHeaderTypeSyntax genericSubject(uint32_t ordinal) {
    zc::Vector<SemanticIdentifier> suffix;
    auto name =
        CanonicalNameReference::from(CanonicalNameRoot::generic(0, ordinal), zc::mv(suffix));
    ZC_IF_SOME(value, name) {
      zc::Vector<CanonicalHeaderTypeSyntax> arguments;
      return CanonicalHeaderTypeSyntax::named(
          CanonicalNamedHeaderType::from(zc::mv(value), zc::mv(arguments)));
    }
    ZC_UNREACHABLE
  }

  void appendObligation(CanonicalBoundObligation&& obligation, ast::NodeId occurrence,
                        zc::Vector<CanonicalBoundObligation>& obligations) {
    boundOccurrences.add(CanonicalBoundSyntaxOccurrence{obligation.clone(), occurrence});
    obligations.add(zc::mv(obligation));
  }

  bool appendWhere(ast::NodeId whereClause, zc::ArrayPtr<const CanonicalGenericBinderFrame> frames,
                   zc::Vector<CanonicalBoundObligation>& obligations) {
    if (!whereClause) { return true; }
    if (!tree.contains(whereClause) ||
        tree.node(whereClause).kind != ast::SyntaxKind::WhereClause) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidBoundSyntax, whereClause);
    }
    const auto& syntax = tree.node(whereClause);
    const ast::NodeList predicates{syntax.payload.words[ast::kWhereClausePredsFirstWord],
                                   syntax.payload.words[ast::kWhereClausePredsSizeWord]};
    if (!tree.contains(predicates)) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidBoundSyntax, whereClause);
    }
    for (const auto predicate : tree.list(predicates)) {
      if (!tree.contains(predicate) || tree.node(predicate).kind != ast::SyntaxKind::WherePred ||
          tree.node(predicate).payload.words[ast::kWherePredKindWord] !=
              static_cast<uint32_t>(ast::WhereBoundKind::Implements)) {
        return reject(CanonicalHeaderSyntaxFailureKind::InvalidBoundSyntax, predicate);
      }
      const auto& value = tree.node(predicate);
      auto subject = buildType(ast::NodeId(value.payload.words[ast::kWherePredTyWord]), frames);
      auto bound = buildType(ast::NodeId(value.payload.words[ast::kWherePredBoundWord]), frames);
      if (subject == zc::none || bound == zc::none) { return false; }
      ZC_IF_SOME(subjectValue, subject) {
        ZC_IF_SOME(boundValue, bound) {
          appendObligation(CanonicalBoundObligation::from(zc::mv(subjectValue), zc::mv(boundValue)),
                           predicate, obligations);
        }
      }
    }
    return true;
  }

  bool buildGenericsAndObligations(ast::NodeId genericParameters,
                                   zc::ArrayPtr<const CanonicalGenericBinderFrame> frames,
                                   zc::Vector<CanonicalGenericParameter>& generics,
                                   zc::Vector<CanonicalBoundObligation>& obligations) {
    if (!genericParameters) { return true; }
    const auto& syntax = tree.node(genericParameters);
    const ast::NodeList parameters{syntax.payload.words[ast::kGenericParamsParamsFirstWord],
                                   syntax.payload.words[ast::kGenericParamsParamsSizeWord]};
    uint32_t ordinal = 0;
    for (const auto parameter : tree.list(parameters)) {
      const auto& parameterSyntax = tree.node(parameter);
      zc::Maybe<CanonicalHeaderTypeSyntax> defaultType;
      const ast::NodeId defaultNode(
          parameterSyntax.payload.words[ast::kGenericTypeParamDefaultTyWord]);
      if (defaultNode) {
        defaultType = buildType(defaultNode, frames);
        if (defaultType == zc::none) { return false; }
      }
      generics.add(CanonicalGenericParameter::from(zc::mv(defaultType)));
      const ast::NodeId boundsNode(
          parameterSyntax.payload.words[ast::kGenericTypeParamBoundsIdWord]);
      if (boundsNode) {
        if (!tree.contains(boundsNode) ||
            tree.node(boundsNode).kind != ast::SyntaxKind::TypeParameterBoundList) {
          return reject(CanonicalHeaderSyntaxFailureKind::InvalidBoundSyntax, boundsNode);
        }
        const auto& boundsSyntax = tree.node(boundsNode);
        const ast::NodeList bounds{
            boundsSyntax.payload.words[ast::kTypeParameterBoundListBoundsFirstWord],
            boundsSyntax.payload.words[ast::kTypeParameterBoundListBoundsSizeWord]};
        if (!tree.contains(bounds)) {
          return reject(CanonicalHeaderSyntaxFailureKind::InvalidBoundSyntax, boundsNode);
        }
        for (const auto boundNode : tree.list(bounds)) {
          auto bound = buildType(boundNode, frames);
          if (bound == zc::none) { return false; }
          ZC_IF_SOME(value, bound) {
            appendObligation(CanonicalBoundObligation::from(genericSubject(ordinal), zc::mv(value)),
                             boundNode, obligations);
          }
        }
      }
      ++ordinal;
    }
    const ast::NodeId misplacedWhere(syntax.payload.words[ast::kGenericParamsWhereWord]);
    if (misplacedWhere) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidBoundSyntax, misplacedWhere);
    }
    return true;
  }

  const ast::Tree& tree;
  const ImplInventoryEntry& implementation;
  zc::ArrayPtr<const CanonicalGenericBinderFrame> enclosingBinders;
  zc::Vector<CanonicalBoundSyntaxOccurrence> boundOccurrences;
  bool rejected = false;
  CanonicalHeaderSyntaxFailure failure{CanonicalHeaderSyntaxFailureKind::InvalidImplSyntax,
                                       ast::NodeId()};
};

}  // namespace

CanonicalImplHeaderProduction CanonicalImplHeaderProducer::produce(
    const ast::Tree& tree, const ImplInventoryEntry& implementation,
    zc::ArrayPtr<const CanonicalGenericBinderFrame> enclosingBinders) {
  auto produced = produceWithProvenance(tree, implementation, enclosingBinders);
  if (produced.is<CanonicalHeaderSyntaxFailure>()) {
    return zc::mv(produced.get<CanonicalHeaderSyntaxFailure>());
  }
  auto provenance = zc::mv(produced.get<CanonicalImplHeaderProvenance>());
  return zc::mv(provenance.header);
}

CanonicalImplHeaderProvenanceProduction CanonicalImplHeaderProducer::produceWithProvenance(
    const ast::Tree& tree, const ImplInventoryEntry& implementation,
    zc::ArrayPtr<const CanonicalGenericBinderFrame> enclosingBinders) {
  return Producer(tree, implementation, enclosingBinders).produce();
}

}  // namespace zomlang::compiler::binder
