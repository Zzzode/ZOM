// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/canonical/canonical-definition-header-producer.h"

#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/identity/canonical/overload-header.h"

namespace zomlang::compiler::binder {
namespace {

using identity::CallableHeaderKind;
using identity::CanonicalBoundObligation;
using identity::CanonicalCallableParameter;
using identity::CanonicalCallableResult;
using identity::CanonicalGenericParameter;
using identity::CanonicalHeaderTypeSyntax;
using identity::CanonicalHeaderTypeSyntaxKind;
using identity::CanonicalNamedHeaderType;
using identity::CanonicalNameReference;
using identity::CanonicalNameRoot;
using identity::OverloadHeader;
using identity::DeclaredDefinitionName;
using identity::ExternalAbi;
using identity::OverloadHeaderAuthority;
using identity::ReceiverShape;
using identity::ReferenceMutability;
using identity::SemanticIdentifier;

struct CallableSyntax final {
  CallableHeaderKind kind;
  ast::IdentId name;
  ast::NodeId parameters;
  ast::NodeId genericParameters;
  ast::NodeId result;
  ast::NodeId raises;
  zc::Maybe<ExternalAbi> externalAbi;
  uint8_t methodMode;
};

class Producer final {
public:
  Producer(const ast::Tree& tree, const DefinitionInventoryEntry& definition,
           zc::ArrayPtr<const CanonicalGenericBinderFrame> enclosingBinders) noexcept
      : tree(tree), definition(definition), enclosingBinders(enclosingBinders) {}

  CanonicalDefinitionHeaderProvenanceProduction produce() {
    auto callable = classify();
    if (callable == zc::none) { return failure; }
    ZC_IF_SOME(syntax, callable) {
      zc::Vector<CanonicalGenericBinderFrame> frames(enclosingBinders.size() + 1);
      frames.add(CanonicalGenericBinderFrame{syntax.genericParameters});
      frames.addAll(enclosingBinders);
      auto binderFailure = CanonicalHeaderTypeProducer::validateBinderStack(tree, frames.asPtr());
      ZC_IF_SOME(value, binderFailure) {
        reject(value.kind, value.node);
        return failure;
      }

      auto name = declaredName(syntax.name);
      if (name == zc::none) { return failure; }
      zc::Vector<CanonicalGenericParameter> generics;
      zc::Vector<CanonicalBoundObligation> obligations;
      if (!buildGenericsAndObligations(syntax.genericParameters, frames.asPtr(), generics,
                                       obligations)) {
        return failure;
      }
      zc::Vector<CanonicalCallableParameter> parameters;
      zc::Maybe<ReceiverShape> receiver;
      if (!buildParameters(syntax, frames.asPtr(), parameters, receiver)) { return failure; }
      auto result = buildResult(syntax, frames.asPtr());
      auto raises = buildRaises(syntax.raises, frames.asPtr());
      if (result == zc::none || (syntax.raises && raises == zc::none)) { return failure; }
      ZC_IF_SOME(nameValue, name) {
        ZC_IF_SOME(resultValue, result) {
          auto header = OverloadHeader::from(
              syntax.kind, zc::mv(nameValue), zc::mv(receiver), zc::mv(generics),
              zc::mv(obligations), zc::mv(parameters), zc::mv(resultValue), zc::mv(raises),
              zc::mv(syntax.externalAbi));
          if (header == zc::none) {
            reject(CanonicalHeaderSyntaxFailureKind::InvalidCallableSyntax, definition.node);
            return failure;
          }
          ZC_IF_SOME(value, header) {
            return CanonicalDefinitionHeaderProvenance{OverloadHeaderAuthority::from(zc::mv(value)),
                                                       zc::mv(boundOccurrences)};
          }
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

  zc::Maybe<DeclaredDefinitionName> declaredName(ast::IdentId name) {
    if (definition.nameKind != InventoryDefinitionNameKind::Declared || !definition.declaredName ||
        tree.ident(name) != tree.ident(definition.declaredName)) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidCallableSyntax, definition.node);
      return zc::none;
    }
    auto value = DeclaredDefinitionName::fromSource(tree.ident(name));
    if (value == zc::none) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidCallableSyntax, definition.node);
    }
    return value;
  }

  zc::Maybe<CallableSyntax> classify() {
    if (!tree.contains(definition.node)) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidCallableSyntax, definition.node);
      return zc::none;
    }
    const auto& syntax = tree.node(definition.node);
    switch (syntax.kind) {
      case ast::SyntaxKind::FunctionDecl:
        if (definition.kind != identity::DefinitionKind::Function) { break; }
        return CallableSyntax{CallableHeaderKind::Function,
                              ast::IdentId(syntax.payload.words[ast::kFunctionDeclNameWord]),
                              ast::NodeId(syntax.payload.words[ast::kFunctionDeclParamsIdWord]),
                              ast::NodeId(syntax.payload.words[ast::kFunctionDeclTypeParamsIdWord]),
                              ast::NodeId(syntax.payload.words[ast::kFunctionDeclRetTyWord]),
                              ast::NodeId(syntax.payload.words[ast::kFunctionDeclRaisesTyWord]),
                              zc::none,
                              0};
      case ast::SyntaxKind::ExternDecl: {
        if (definition.kind != identity::DefinitionKind::Function) { break; }
        const auto abi = syntax.payload.words[ast::kExternDeclAbiWord];
        if (abi > 2) { break; }
        return CallableSyntax{CallableHeaderKind::Function,
                              ast::IdentId(syntax.payload.words[ast::kExternDeclNameWord]),
                              definition.node,
                              ast::NodeId(),
                              ast::NodeId(syntax.payload.words[ast::kExternDeclRetTyWord]),
                              ast::NodeId(syntax.payload.words[ast::kExternDeclRaisesTyWord]),
                              zc::Maybe<ExternalAbi>(static_cast<ExternalAbi>(abi + 1)),
                              0};
      }
      case ast::SyntaxKind::MethodDecl:
        if (definition.kind != identity::DefinitionKind::Method) { break; }
        return CallableSyntax{CallableHeaderKind::Method,
                              ast::IdentId(syntax.payload.words[ast::kMethodDeclNameWord]),
                              ast::NodeId(syntax.payload.words[ast::kMethodDeclParamsIdWord]),
                              ast::NodeId(syntax.payload.words[ast::kMethodDeclTypeParamsIdWord]),
                              ast::NodeId(syntax.payload.words[ast::kMethodDeclRetTyWord]),
                              ast::NodeId(syntax.payload.words[ast::kMethodDeclRaisesTyWord]),
                              zc::none,
                              static_cast<uint8_t>(syntax.payload.words[ast::kMethodDeclModeWord])};
      case ast::SyntaxKind::ConstructorDecl:
        if (definition.kind != identity::DefinitionKind::Constructor) { break; }
        return CallableSyntax{CallableHeaderKind::Constructor,
                              ast::IdentId(syntax.payload.words[ast::kConstructorDeclNameWord]),
                              ast::NodeId(syntax.payload.words[ast::kConstructorDeclParamsIdWord]),
                              ast::NodeId(),
                              ast::NodeId(),
                              ast::NodeId(syntax.payload.words[ast::kConstructorDeclRaisesTyWord]),
                              zc::none,
                              0};
      default:
        break;
    }
    reject(CanonicalHeaderSyntaxFailureKind::InvalidCallableSyntax, definition.node);
    return zc::none;
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

  CanonicalHeaderTypeSyntax genericSubject(uint32_t ordinal) {
    zc::Vector<SemanticIdentifier> suffix;
    auto name =
        CanonicalNameReference::from(CanonicalNameRoot::generic(0, ordinal), zc::mv(suffix));
    ZC_IF_SOME(value, name) {
      zc::Vector<CanonicalHeaderTypeSyntax> arguments;
      return CanonicalHeaderTypeSyntax::named(
          CanonicalNamedHeaderType::from(zc::mv(value), zc::mv(arguments)));
    }
    ZC_UNREACHABLE;
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
    if (!tree.contains(genericParameters) ||
        tree.node(genericParameters).kind != ast::SyntaxKind::GenericParams) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidBoundSyntax, genericParameters);
    }
    const auto& syntax = tree.node(genericParameters);
    const ast::NodeList parameters{syntax.payload.words[ast::kGenericParamsParamsFirstWord],
                                   syntax.payload.words[ast::kGenericParamsParamsSizeWord]};
    if (!tree.contains(parameters)) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidBoundSyntax, genericParameters);
    }
    uint32_t ordinal = 0;
    for (const auto parameter : tree.list(parameters)) {
      if (!tree.contains(parameter) ||
          tree.node(parameter).kind != ast::SyntaxKind::GenericTypeParam) {
        return reject(CanonicalHeaderSyntaxFailureKind::InvalidBoundSyntax, parameter);
      }
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
    return appendWhere(ast::NodeId(syntax.payload.words[ast::kGenericParamsWhereWord]), frames,
                       obligations);
  }

  bool exactSelf(ast::NodeId node) const {
    if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::NamedTypeExpr) {
      return false;
    }
    const auto& type = tree.node(node);
    const ast::NodeList arguments{type.payload.words[ast::kNamedTypeExprArgsFirstWord],
                                  type.payload.words[ast::kNamedTypeExprArgsSizeWord]};
    const ast::NodeId path(type.payload.words[ast::kNamedTypeExprPathWord]);
    if (arguments.size != 0 || !tree.contains(path) ||
        tree.node(path).kind != ast::SyntaxKind::ModulePath) {
      return false;
    }
    const auto& pathSyntax = tree.node(path);
    const ast::IdentList segments{pathSyntax.payload.words[ast::kModulePathSegmentsFirstWord],
                                  pathSyntax.payload.words[ast::kModulePathSegmentsSizeWord]};
    return pathSyntax.payload.words[ast::kModulePathRootWord] == 0 && segments.size == 1 &&
           tree.contains(segments) && tree.ident(tree.identList(segments)[0]) == "Self"_zc;
  }

  bool moveReceiverAttribute(ast::NodeId parameter, bool& malformed) const {
    malformed = false;
    const auto& parameterSyntax = tree.node(parameter);
    const ast::NodeId list(parameterSyntax.payload.words[ast::kFunctionParameterDeclAttrsWord]);
    if (!list) { return false; }
    if (!tree.contains(list) || tree.node(list).kind != ast::SyntaxKind::AttributeList) {
      malformed = true;
      return false;
    }
    const auto& listSyntax = tree.node(list);
    const ast::NodeList attributes{listSyntax.payload.words[ast::kAttributeListAttrsFirstWord],
                                   listSyntax.payload.words[ast::kAttributeListAttrsSizeWord]};
    if (!tree.contains(attributes)) {
      malformed = true;
      return false;
    }
    bool found = false;
    for (const auto attribute : tree.list(attributes)) {
      if (!tree.contains(attribute) || tree.node(attribute).kind != ast::SyntaxKind::Attribute) {
        malformed = true;
        return false;
      }
      const auto& attributeSyntax = tree.node(attribute);
      const ast::NodeId path(attributeSyntax.payload.words[ast::kAttributePathWord]);
      if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::AttributePath) {
        malformed = true;
        return false;
      }
      const auto& pathSyntax = tree.node(path);
      const ast::IdentList segments{pathSyntax.payload.words[ast::kAttributePathSegmentsFirstWord],
                                    pathSyntax.payload.words[ast::kAttributePathSegmentsSizeWord]};
      if (!tree.contains(segments)) {
        malformed = true;
        return false;
      }
      const auto names = tree.identList(segments);
      const bool move = pathSyntax.payload.words[ast::kAttributePathLeadingWord] == 0 &&
                        names.size() == 3 && tree.ident(names[0]) == "zom"_zc &&
                        tree.ident(names[1]) == "param"_zc && tree.ident(names[2]) == "move"_zc;
      if (!move) { continue; }
      const ast::NodeList arguments{attributeSyntax.payload.words[ast::kAttributeArgsFirstWord],
                                    attributeSyntax.payload.words[ast::kAttributeArgsSizeWord]};
      if (arguments.size != 0) {
        malformed = true;
        return false;
      }
      found = true;
    }
    return found;
  }

  bool normalizeReceiver(ast::NodeId parameter, uint8_t methodMode, ReceiverShape& result) {
    const auto& syntax = tree.node(parameter);
    if (syntax.payload.words[ast::kFunctionParameterDeclDefaultWord] != 0 || methodMode >= 3 ||
        methodMode == 1) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidReceiver, parameter);
    }
    const ast::NodeId type(syntax.payload.words[ast::kFunctionParameterDeclTyWord]);
    bool malformedAttribute = false;
    const bool move = moveReceiverAttribute(parameter, malformedAttribute);
    if (malformedAttribute) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidReceiver, parameter);
    }
    if (exactSelf(type)) {
      if (move) {
        if (methodMode == 2) {
          return reject(CanonicalHeaderSyntaxFailureKind::InvalidReceiver, parameter);
        }
        result = ReceiverShape::Move;
      } else {
        result = methodMode == 2 ? ReceiverShape::Mutable : ReceiverShape::Shared;
      }
      return true;
    }
    if (!tree.contains(type) || tree.node(type).kind != ast::SyntaxKind::ReferenceTypeExpr ||
        move) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidReceiver, parameter);
    }
    const auto& reference = tree.node(type);
    if (!exactSelf(ast::NodeId(reference.payload.words[ast::kReferenceTypeExprElemWord]))) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidReceiver, parameter);
    }
    const bool isMutable = reference.payload.words[ast::kReferenceTypeExprIsMutWord] != 0;
    if (!isMutable && methodMode == 2) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidReceiver, parameter);
    }
    result = isMutable ? ReceiverShape::Mutable : ReceiverShape::Shared;
    return true;
  }

  bool appendParameter(ast::NodeId parameter,
                       zc::ArrayPtr<const CanonicalGenericBinderFrame> frames,
                       zc::Vector<CanonicalCallableParameter>& parameters) {
    const auto& syntax = tree.node(parameter);
    auto label = SemanticIdentifier::fromCanonical(
        tree.ident(ast::IdentId(syntax.payload.words[ast::kFunctionParameterDeclNameWord])));
    auto type =
        buildType(ast::NodeId(syntax.payload.words[ast::kFunctionParameterDeclTyWord]), frames);
    if (label == zc::none || type == zc::none) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidCallableSyntax, parameter);
    }
    ZC_IF_SOME(labelValue, label) {
      ZC_IF_SOME(typeValue, type) {
        parameters.add(CanonicalCallableParameter::from(
            zc::mv(labelValue), zc::mv(typeValue),
            syntax.payload.words[ast::kFunctionParameterDeclDefaultWord] != 0));
      }
    }
    return true;
  }

  bool buildParameters(const CallableSyntax& callable,
                       zc::ArrayPtr<const CanonicalGenericBinderFrame> frames,
                       zc::Vector<CanonicalCallableParameter>& parameters,
                       zc::Maybe<ReceiverShape>& receiver) {
    ast::NodeList list;
    if (callable.kind == CallableHeaderKind::Function && callable.externalAbi != zc::none) {
      const auto& syntax = tree.node(callable.parameters);
      list = ast::NodeList{syntax.payload.words[ast::kExternDeclParamsFirstWord],
                           syntax.payload.words[ast::kExternDeclParamsSizeWord]};
    } else {
      if (!tree.contains(callable.parameters) ||
          tree.node(callable.parameters).kind != ast::SyntaxKind::FunctionParameterList) {
        return reject(CanonicalHeaderSyntaxFailureKind::InvalidCallableSyntax, callable.parameters);
      }
      const auto& syntax = tree.node(callable.parameters);
      list = ast::NodeList{syntax.payload.words[ast::kFunctionParameterListParamsFirstWord],
                           syntax.payload.words[ast::kFunctionParameterListParamsSizeWord]};
    }
    if (!tree.contains(list)) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidCallableSyntax, callable.parameters);
    }
    bool foundReceiver = false;
    size_t ordinal = 0;
    for (const auto parameter : tree.list(list)) {
      if (!tree.contains(parameter) ||
          tree.node(parameter).kind != ast::SyntaxKind::FunctionParameterDecl) {
        return reject(CanonicalHeaderSyntaxFailureKind::InvalidCallableSyntax, parameter);
      }
      const auto& syntax = tree.node(parameter);
      const auto name =
          tree.ident(ast::IdentId(syntax.payload.words[ast::kFunctionParameterDeclNameWord]));
      if (name == "this"_zc) {
        if (callable.kind != CallableHeaderKind::Method || foundReceiver || ordinal != 0) {
          return reject(CanonicalHeaderSyntaxFailureKind::InvalidReceiver, parameter);
        }
        ReceiverShape shape = ReceiverShape::Shared;
        if (!normalizeReceiver(parameter, callable.methodMode, shape)) { return false; }
        receiver = shape;
        foundReceiver = true;
      } else if (!appendParameter(parameter, frames, parameters)) {
        return false;
      }
      ++ordinal;
    }
    if (callable.kind == CallableHeaderKind::Method && callable.methodMode == 2 && !foundReceiver) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidReceiver, definition.node);
    }
    if (callable.kind == CallableHeaderKind::Method && callable.methodMode >= 3) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidReceiver, definition.node);
    }
    return true;
  }

  zc::Maybe<CanonicalCallableResult> buildResult(
      const CallableSyntax& callable, zc::ArrayPtr<const CanonicalGenericBinderFrame> frames) {
    if (callable.kind == CallableHeaderKind::Constructor) {
      return CanonicalCallableResult::constructorSelf();
    }
    if (!callable.result) { return CanonicalCallableResult::unit(); }
    auto type = buildType(callable.result, frames);
    if (type == zc::none) { return zc::none; }
    ZC_IF_SOME(value, type) { return CanonicalCallableResult::type(zc::mv(value)); }
    return zc::none;
  }

  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> buildRaises(
      ast::NodeId raises, zc::ArrayPtr<const CanonicalGenericBinderFrame> frames) {
    if (!raises) { return zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>>(); }
    auto type = buildType(raises, frames);
    if (type == zc::none) { return zc::none; }
    zc::Vector<CanonicalHeaderTypeSyntax> values;
    ZC_IF_SOME(value, type) {
      if (value.kind() == CanonicalHeaderTypeSyntaxKind::Union) {
        ZC_IF_SOME(members, value.members()) {
          for (const auto& member : members) { values.add(member.clone()); }
        }
      } else {
        values.add(zc::mv(value));
      }
    }
    if (values.size() == 0) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidCallableSyntax, raises);
      return zc::none;
    }
    return zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>>(zc::mv(values));
  }

  const ast::Tree& tree;
  const DefinitionInventoryEntry& definition;
  zc::ArrayPtr<const CanonicalGenericBinderFrame> enclosingBinders;
  zc::Vector<CanonicalBoundSyntaxOccurrence> boundOccurrences;
  bool rejected = false;
  CanonicalHeaderSyntaxFailure failure{CanonicalHeaderSyntaxFailureKind::InvalidCallableSyntax,
                                       ast::NodeId()};
};

}  // namespace

CanonicalDefinitionHeaderProduction CanonicalDefinitionHeaderProducer::produce(
    const ast::Tree& tree, const DefinitionInventoryEntry& definition,
    zc::ArrayPtr<const CanonicalGenericBinderFrame> enclosingBinders) {
  auto produced = produceWithProvenance(tree, definition, enclosingBinders);
  if (produced.is<CanonicalHeaderSyntaxFailure>()) {
    return zc::mv(produced.get<CanonicalHeaderSyntaxFailure>());
  }
  auto provenance = zc::mv(produced.get<CanonicalDefinitionHeaderProvenance>());
  return zc::mv(provenance.authority);
}

CanonicalDefinitionHeaderProvenanceProduction
CanonicalDefinitionHeaderProducer::produceWithProvenance(
    const ast::Tree& tree, const DefinitionInventoryEntry& definition,
    zc::ArrayPtr<const CanonicalGenericBinderFrame> enclosingBinders) {
  return Producer(tree, definition, enclosingBinders).produce();
}

}  // namespace zomlang::compiler::binder
