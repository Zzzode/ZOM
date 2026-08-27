// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/canonical/canonical-header-verifier.h"

#include <cstdint>

#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/identity/canonical/header-name.h"

namespace zomlang::compiler::binder {
namespace {

using identity::CallableHeaderKind;
using identity::CanonicalAssociatedBinding;
using identity::CanonicalBoundObligation;
using identity::CanonicalCallableParameter;
using identity::CanonicalCallableResult;
using identity::CanonicalGenericParameter;
using identity::CanonicalHeaderTypeSyntax;
using identity::CanonicalHeaderTypeSyntaxKind;
using identity::ImplHeader;
using identity::CanonicalNamedHeaderType;
using identity::CanonicalNameReference;
using identity::CanonicalNameRoot;
using identity::CanonicalObjectTypeMember;
using identity::OverloadHeader;
using identity::CanonicalTraitReference;
using identity::DeclaredDefinitionName;
using identity::ExternalAbi;
using identity::ImplPolarity;
using identity::ImplSafety;
using identity::PredefinedTypeKind;
using identity::RawPointerMutability;
using identity::ReceiverShape;
using identity::ReferenceMutability;
using identity::SemanticIdentifier;

struct OracleBinderFrame final {
  ast::NodeId syntax;
  zc::Vector<ast::IdentId> names;
};

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

zc::Maybe<const DefinitionInventoryEntry&> definitionAt(const CanonicalHeaderSyntaxView& syntax,
                                                        ast::NodeId node) {
  for (const auto& entry : syntax.definitions()) {
    if (entry.node == node) return entry;
  }
  return zc::none;
}

zc::Maybe<const ImplInventoryEntry&> implAt(const CanonicalHeaderSyntaxView& syntax,
                                            ast::NodeId node) {
  for (const auto& entry : syntax.implementations()) {
    if (entry.node == node) return entry;
  }
  return zc::none;
}

ast::NodeId definitionBinder(const ast::Tree& tree, ast::NodeId node) {
  if (!tree.contains(node)) return {};
  const auto& syntax = tree.node(node);
  switch (syntax.kind) {
    case ast::SyntaxKind::EnumDeclaration:
      return ast::NodeId(syntax.payload.words[ast::kEnumDeclarationTypeParamsIdWord]);
    case ast::SyntaxKind::FunctionDecl:
      return ast::NodeId(syntax.payload.words[ast::kFunctionDeclTypeParamsIdWord]);
    case ast::SyntaxKind::ClassDecl:
      return ast::NodeId(syntax.payload.words[ast::kClassDeclTypeParamsIdWord]);
    case ast::SyntaxKind::StructDecl:
      return ast::NodeId(syntax.payload.words[ast::kStructDeclTypeParamsIdWord]);
    case ast::SyntaxKind::InterfaceDecl:
      return ast::NodeId(syntax.payload.words[ast::kInterfaceDeclTypeParamsIdWord]);
    case ast::SyntaxKind::AliasDecl:
      return ast::NodeId(syntax.payload.words[ast::kAliasDeclTypeParamsIdWord]);
    case ast::SyntaxKind::MethodDecl:
      return ast::NodeId(syntax.payload.words[ast::kMethodDeclTypeParamsIdWord]);
    case ast::SyntaxKind::AssociatedTypeDecl:
      return ast::NodeId(syntax.payload.words[ast::kAssociatedTypeDeclTypeParamsIdWord]);
    default:
      return {};
  }
}

ast::NodeId implBinder(const ast::Tree& tree, ast::NodeId node) {
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::StandaloneImplDecl) {
    return {};
  }
  return ast::NodeId(tree.node(node).payload.words[ast::kStandaloneImplDeclTypeParamsIdWord]);
}

bool appendBinderFrame(const ast::Tree& tree, ast::NodeId genericParameters,
                       zc::Vector<OracleBinderFrame>& frames, ast::NodeId& badNode) {
  zc::Vector<ast::IdentId> names;
  if (!genericParameters) {
    frames.add(OracleBinderFrame{genericParameters, zc::mv(names)});
    return true;
  }
  if (!tree.contains(genericParameters) ||
      tree.node(genericParameters).kind != ast::SyntaxKind::GenericParams) {
    badNode = genericParameters;
    return false;
  }
  const auto& syntax = tree.node(genericParameters);
  const ast::NodeList parameters{syntax.payload.words[ast::kGenericParamsParamsFirstWord],
                                 syntax.payload.words[ast::kGenericParamsParamsSizeWord]};
  if (!tree.contains(parameters)) {
    badNode = genericParameters;
    return false;
  }
  for (const auto parameter : tree.list(parameters)) {
    if (!tree.contains(parameter) ||
        tree.node(parameter).kind != ast::SyntaxKind::GenericTypeParam) {
      badNode = parameter;
      return false;
    }
    const ast::IdentId name(tree.node(parameter).payload.words[ast::kGenericTypeParamNameWord]);
    if (SemanticIdentifier::fromCanonical(tree.ident(name)) == zc::none) {
      badNode = parameter;
      return false;
    }
    names.add(name);
  }
  frames.add(OracleBinderFrame{genericParameters, zc::mv(names)});
  return true;
}

zc::Maybe<zc::Vector<OracleBinderFrame>> buildBinderStack(
    const ast::Tree& tree, const CanonicalHeaderSyntaxView& syntax,
    zc::ArrayPtr<const StructuralIdentityParent> parents, ast::NodeId currentBinder,
    ast::NodeId& badNode) {
  zc::Vector<OracleBinderFrame> frames(parents.size() + 1);
  if (!appendBinderFrame(tree, currentBinder, frames, badNode)) return zc::none;
  for (size_t remaining = parents.size(); remaining > 0; --remaining) {
    const auto& parent = parents[remaining - 1];
    ast::NodeId binder;
    if (parent.kind == StructuralIdentityParentKind::Definition) {
      auto definition = definitionAt(syntax, parent.node);
      if (definition == zc::none) {
        badNode = parent.node;
        return zc::none;
      }
      binder = definitionBinder(tree, parent.node);
    } else {
      auto implementation = implAt(syntax, parent.node);
      if (implementation == zc::none) {
        badNode = parent.node;
        return zc::none;
      }
      binder = implBinder(tree, parent.node);
    }
    if (!appendBinderFrame(tree, binder, frames, badNode)) return zc::none;
  }
  return zc::mv(frames);
}

class TypeOracle final {
public:
  TypeOracle(const ast::Tree& tree, zc::ArrayPtr<const OracleBinderFrame> frames,
             ast::NodeId& badNode) noexcept
      : tree(tree), frames(frames), badNode(badNode) {}

  zc::Maybe<CanonicalHeaderTypeSyntax> normalize(ast::NodeId node) {
    if (!tree.contains(node)) return reject(node);
    const auto& syntax = tree.node(node);
    switch (syntax.kind) {
      case ast::SyntaxKind::NamedTypeExpr:
        return namedType(node, syntax);
      case ast::SyntaxKind::PredefinedTypeExpr:
        return predefinedType(node, syntax);
      case ast::SyntaxKind::FunctionTypeExpr:
        return functionType(node, syntax);
      case ast::SyntaxKind::UnionTypeExpr:
      case ast::SyntaxKind::IntersectionTypeExpr:
        return setType(node, syntax);
      case ast::SyntaxKind::FixedArrayTypeExpr:
        return fixedArray(node, syntax);
      case ast::SyntaxKind::ArrayTypeExpr:
      case ast::SyntaxKind::SliceArrayTypeExpr:
        return sequenceType(node, syntax);
      case ast::SyntaxKind::OptionalTypeExpr:
        return optionalType(node, syntax);
      case ast::SyntaxKind::ReferenceTypeExpr:
      case ast::SyntaxKind::RawPointerTypeExpr:
        return pointerType(node, syntax);
      case ast::SyntaxKind::TypeQueryExpr:
        return typeQuery(node, syntax);
      case ast::SyntaxKind::ObjectTypeExpr:
        return objectType(node, syntax);
      case ast::SyntaxKind::TupleTypeExpr:
        return tupleType(node, syntax);
      case ast::SyntaxKind::AssociatedTypeProjectionExpr:
        return associatedType(node, syntax);
      case ast::SyntaxKind::DynTypeExpr:
        return dynamicType(node, syntax);
      default:
        return reject(node);
    }
  }

private:
  zc::Maybe<CanonicalHeaderTypeSyntax> reject(ast::NodeId node) {
    if (!badNode) badNode = node;
    return zc::none;
  }

  zc::Maybe<SemanticIdentifier> identifier(ast::IdentId value, ast::NodeId node) {
    auto name = SemanticIdentifier::fromCanonical(tree.ident(value));
    if (name == zc::none && !badNode) badNode = node;
    return name;
  }

  zc::Maybe<CanonicalNameRoot> genericRoot(ast::IdentId name) const {
    for (size_t depth = 0; depth < frames.size(); ++depth) {
      for (size_t ordinal = 0; ordinal < frames[depth].names.size(); ++ordinal) {
        if (tree.ident(frames[depth].names[ordinal]) == tree.ident(name)) {
          if (depth > UINT32_MAX || ordinal > UINT32_MAX) return zc::none;
          return CanonicalNameRoot::generic(static_cast<uint32_t>(depth),
                                            static_cast<uint32_t>(ordinal));
        }
      }
    }
    return zc::none;
  }

  zc::Maybe<CanonicalNameReference> moduleName(ast::NodeId path) {
    if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::ModulePath) {
      if (!badNode) badNode = path;
      return zc::none;
    }
    const auto& syntax = tree.node(path);
    const ast::IdentList segments{syntax.payload.words[ast::kModulePathSegmentsFirstWord],
                                  syntax.payload.words[ast::kModulePathSegmentsSizeWord]};
    if (segments.size == 0 || !tree.contains(segments)) {
      if (!badNode) badNode = path;
      return zc::none;
    }
    const auto values = tree.identList(segments);
    const uint32_t rootTag = syntax.payload.words[ast::kModulePathRootWord];
    if (rootTag > 1) {
      if (!badNode) badNode = path;
      return zc::none;
    }
    CanonicalNameRoot root =
        rootTag == 1 ? CanonicalNameRoot::absolute() : CanonicalNameRoot::relative();
    size_t firstSuffix = 0;
    if (rootTag == 0) {
      ZC_IF_SOME(generic, genericRoot(values[0])) {
        root = zc::mv(generic);
        firstSuffix = 1;
      }
    }
    zc::Vector<SemanticIdentifier> suffix(values.size() - firstSuffix);
    for (size_t index = firstSuffix; index < values.size(); ++index) {
      auto segment = identifier(values[index], path);
      if (segment == zc::none) return zc::none;
      ZC_IF_SOME(value, segment) { suffix.add(zc::mv(value)); }
    }
    auto result = CanonicalNameReference::from(zc::mv(root), zc::mv(suffix));
    if (result == zc::none && !badNode) badNode = path;
    return result;
  }

  zc::Maybe<CanonicalNameReference> attributeName(ast::NodeId path) {
    if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::AttributePath) {
      if (!badNode) badNode = path;
      return zc::none;
    }
    const auto& syntax = tree.node(path);
    const ast::IdentList segments{syntax.payload.words[ast::kAttributePathSegmentsFirstWord],
                                  syntax.payload.words[ast::kAttributePathSegmentsSizeWord]};
    if (syntax.payload.words[ast::kAttributePathLeadingWord] != 0 || segments.size == 0 ||
        !tree.contains(segments)) {
      if (!badNode) badNode = path;
      return zc::none;
    }
    zc::Vector<SemanticIdentifier> suffix(segments.size);
    for (const auto segment : tree.identList(segments)) {
      auto name = identifier(segment, path);
      if (name == zc::none) return zc::none;
      ZC_IF_SOME(value, name) { suffix.add(zc::mv(value)); }
    }
    return CanonicalNameReference::from(CanonicalNameRoot::relative(), zc::mv(suffix));
  }

  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> typeList(ast::NodeList nodes) {
    if (!tree.contains(nodes)) {
      if (!badNode) badNode = ast::NodeId();
      return zc::none;
    }
    zc::Vector<CanonicalHeaderTypeSyntax> result(nodes.size);
    for (const auto node : tree.list(nodes)) {
      auto type = normalize(node);
      if (type == zc::none) return zc::none;
      ZC_IF_SOME(value, type) { result.add(zc::mv(value)); }
    }
    return zc::mv(result);
  }

  bool appendUnionLeaves(ast::NodeId node, zc::Vector<CanonicalHeaderTypeSyntax>& leaves) {
    if (!tree.contains(node)) {
      reject(node);
      return false;
    }
    const auto& syntax = tree.node(node);
    if (syntax.kind != ast::SyntaxKind::UnionTypeExpr) {
      auto type = normalize(node);
      if (type == zc::none) return false;
      ZC_IF_SOME(value, type) { leaves.add(zc::mv(value)); }
      return true;
    }
    const ast::NodeList alternatives{syntax.payload.words[ast::kUnionTypeExprAltsFirstWord],
                                     syntax.payload.words[ast::kUnionTypeExprAltsSizeWord]};
    if (!tree.contains(alternatives) || alternatives.size == 0) {
      reject(node);
      return false;
    }
    for (const auto alternative : tree.list(alternatives)) {
      if (!appendUnionLeaves(alternative, leaves)) return false;
    }
    return true;
  }

  zc::Maybe<uint64_t> arrayLength(ast::NodeId node) {
    if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::IntLiteral) {
      if (!badNode) badNode = node;
      return zc::none;
    }
    const auto& syntax = tree.node(node);
    const uint32_t base = syntax.payload.words[ast::kIntLiteralBaseWord];
    if (base != 2 && base != 8 && base != 10 && base != 16) {
      if (!badNode) badNode = node;
      return zc::none;
    }
    uint64_t result = 0;
    for (const auto character :
         tree.bigInt(ast::BigIntId(syntax.payload.words[ast::kIntLiteralValueWord]))) {
      if (character == '_') continue;
      uint8_t digit = 0xff;
      if (character >= '0' && character <= '9') {
        digit = static_cast<uint8_t>(character - '0');
      } else if (character >= 'a' && character <= 'f') {
        digit = static_cast<uint8_t>(character - 'a' + 10);
      } else if (character >= 'A' && character <= 'F') {
        digit = static_cast<uint8_t>(character - 'A' + 10);
      }
      if (digit >= base || result > (UINT64_MAX - digit) / base) {
        if (!badNode) badNode = node;
        return zc::none;
      }
      result = result * base + digit;
    }
    return result;
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> namedType(ast::NodeId node, const ast::Node& syntax) {
    auto name = moduleName(ast::NodeId(syntax.payload.words[ast::kNamedTypeExprPathWord]));
    auto arguments = typeList(ast::NodeList{syntax.payload.words[ast::kNamedTypeExprArgsFirstWord],
                                            syntax.payload.words[ast::kNamedTypeExprArgsSizeWord]});
    if (name == zc::none || arguments == zc::none) return zc::none;
    ZC_IF_SOME(nameValue, name) {
      ZC_IF_SOME(argumentValues, arguments) {
        return CanonicalHeaderTypeSyntax::named(
            CanonicalNamedHeaderType::from(zc::mv(nameValue), zc::mv(argumentValues)));
      }
    }
    return reject(node);
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> predefinedType(ast::NodeId node, const ast::Node& syntax) {
    const uint32_t kind = syntax.payload.words[ast::kPredefinedTypeExprKindWord];
    if (kind > 16) return reject(node);
    auto result = CanonicalHeaderTypeSyntax::predefined(static_cast<PredefinedTypeKind>(kind + 1));
    if (result == zc::none) return reject(node);
    return result;
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> functionType(ast::NodeId node, const ast::Node& syntax) {
    auto parameters =
        typeList(ast::NodeList{syntax.payload.words[ast::kFunctionTypeExprParamsFirstWord],
                               syntax.payload.words[ast::kFunctionTypeExprParamsSizeWord]});
    auto result = normalize(ast::NodeId(syntax.payload.words[ast::kFunctionTypeExprRetTyWord]));
    if (parameters == zc::none || result == zc::none) return zc::none;
    zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises;
    const ast::NodeId raisesNode(syntax.payload.words[ast::kFunctionTypeExprRaisesWord]);
    if (raisesNode) {
      zc::Vector<CanonicalHeaderTypeSyntax> leaves;
      if (!appendUnionLeaves(raisesNode, leaves) || leaves.size() == 0) return reject(node);
      raises = zc::mv(leaves);
    }
    ZC_IF_SOME(parameterValues, parameters) {
      ZC_IF_SOME(resultValue, result) {
        auto admitted = CanonicalHeaderTypeSyntax::function(zc::mv(parameterValues),
                                                            zc::mv(resultValue), zc::mv(raises));
        if (admitted == zc::none) return reject(node);
        return admitted;
      }
    }
    return reject(node);
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> setType(ast::NodeId node, const ast::Node& syntax) {
    const bool isUnion = syntax.kind == ast::SyntaxKind::UnionTypeExpr;
    auto members = typeList(
        ast::NodeList{syntax.payload.words[isUnion ? ast::kUnionTypeExprAltsFirstWord
                                                   : ast::kIntersectionTypeExprAltsFirstWord],
                      syntax.payload.words[isUnion ? ast::kUnionTypeExprAltsSizeWord
                                                   : ast::kIntersectionTypeExprAltsSizeWord]});
    if (members == zc::none) return zc::none;
    ZC_IF_SOME(values, members) {
      auto result = isUnion ? CanonicalHeaderTypeSyntax::unionOf(zc::mv(values))
                            : CanonicalHeaderTypeSyntax::intersectionOf(zc::mv(values));
      if (result == zc::none) return reject(node);
      return result;
    }
    return reject(node);
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> fixedArray(ast::NodeId node, const ast::Node& syntax) {
    auto element = normalize(ast::NodeId(syntax.payload.words[ast::kFixedArrayTypeExprElemWord]));
    auto length =
        arrayLength(ast::NodeId(syntax.payload.words[ast::kFixedArrayTypeExprLenExprWord]));
    if (element == zc::none || length == zc::none) return zc::none;
    ZC_IF_SOME(elementValue, element) {
      ZC_IF_SOME(lengthValue, length) {
        return CanonicalHeaderTypeSyntax::fixedArray(zc::mv(elementValue), lengthValue);
      }
    }
    return reject(node);
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> sequenceType(ast::NodeId node, const ast::Node& syntax) {
    const bool dynamic = syntax.kind == ast::SyntaxKind::ArrayTypeExpr;
    auto element =
        normalize(ast::NodeId(syntax.payload.words[dynamic ? ast::kArrayTypeExprElemWord
                                                           : ast::kSliceArrayTypeExprElemWord]));
    if (element == zc::none) return zc::none;
    ZC_IF_SOME(value, element) {
      return dynamic ? CanonicalHeaderTypeSyntax::dynamicArray(zc::mv(value))
                     : CanonicalHeaderTypeSyntax::slice(zc::mv(value));
    }
    return reject(node);
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> optionalType(ast::NodeId node, const ast::Node& syntax) {
    auto element = normalize(ast::NodeId(syntax.payload.words[ast::kOptionalTypeExprInnerWord]));
    if (element == zc::none) return zc::none;
    ZC_IF_SOME(value, element) {
      auto result = CanonicalHeaderTypeSyntax::optional(
          zc::mv(value), syntax.payload.words[ast::kOptionalTypeExprDoubleWord] != 0 ? 2 : 1);
      if (result == zc::none) return reject(node);
      return result;
    }
    return reject(node);
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> pointerType(ast::NodeId node, const ast::Node& syntax) {
    const bool raw = syntax.kind == ast::SyntaxKind::RawPointerTypeExpr;
    auto element =
        normalize(ast::NodeId(syntax.payload.words[raw ? ast::kRawPointerTypeExprElemWord
                                                       : ast::kReferenceTypeExprElemWord]));
    if (element == zc::none) return zc::none;
    const bool isMutable =
        syntax.payload
            .words[raw ? ast::kRawPointerTypeExprIsMutWord : ast::kReferenceTypeExprIsMutWord] != 0;
    ZC_IF_SOME(value, element) {
      auto result =
          raw ? CanonicalHeaderTypeSyntax::rawPointer(
                    isMutable ? RawPointerMutability::Mutable : RawPointerMutability::Const,
                    zc::mv(value))
              : CanonicalHeaderTypeSyntax::reference(
                    isMutable ? ReferenceMutability::Mutable : ReferenceMutability::Shared,
                    zc::mv(value));
      if (result == zc::none) return reject(node);
      return result;
    }
    return reject(node);
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> typeQuery(ast::NodeId node, const ast::Node& syntax) {
    auto name = moduleName(ast::NodeId(syntax.payload.words[ast::kTypeQueryExprPathWord]));
    if (name == zc::none) return zc::none;
    ZC_IF_SOME(value, name) { return CanonicalHeaderTypeSyntax::typeQuery(zc::mv(value)); }
    return reject(node);
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> objectType(ast::NodeId node, const ast::Node& syntax) {
    const ast::NodeList members{syntax.payload.words[ast::kObjectTypeExprMembersFirstWord],
                                syntax.payload.words[ast::kObjectTypeExprMembersSizeWord]};
    if (!tree.contains(members)) return reject(node);
    zc::Vector<CanonicalObjectTypeMember> result(members.size);
    for (const auto member : tree.list(members)) {
      if (!tree.contains(member) || tree.node(member).kind != ast::SyntaxKind::ObjectTypeMember) {
        return reject(member);
      }
      const auto& value = tree.node(member);
      auto name =
          identifier(ast::IdentId(value.payload.words[ast::kObjectTypeMemberNameWord]), member);
      auto type = normalize(ast::NodeId(value.payload.words[ast::kObjectTypeMemberTyWord]));
      if (name == zc::none || type == zc::none) return zc::none;
      ZC_IF_SOME(nameValue, name) {
        ZC_IF_SOME(typeValue, type) {
          result.add(CanonicalObjectTypeMember::from(
              zc::mv(nameValue), zc::mv(typeValue),
              value.payload.words[ast::kObjectTypeMemberIsMutWord] != 0,
              value.payload.words[ast::kObjectTypeMemberIsOptionalWord] != 0));
        }
      }
    }
    return CanonicalHeaderTypeSyntax::object(zc::mv(result));
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> tupleType(ast::NodeId node, const ast::Node& syntax) {
    auto elements = typeList(ast::NodeList{syntax.payload.words[ast::kTupleTypeExprElemsFirstWord],
                                           syntax.payload.words[ast::kTupleTypeExprElemsSizeWord]});
    if (elements == zc::none) return zc::none;
    ZC_IF_SOME(values, elements) { return CanonicalHeaderTypeSyntax::tuple(zc::mv(values)); }
    return reject(node);
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> associatedType(ast::NodeId node, const ast::Node& syntax) {
    auto base =
        normalize(ast::NodeId(syntax.payload.words[ast::kAssociatedTypeProjectionExprBaseTyWord]));
    if (base == zc::none) return zc::none;
    zc::Maybe<CanonicalHeaderTypeSyntax> interfaceType;
    const ast::NodeId interfaceNode(
        syntax.payload.words[ast::kAssociatedTypeProjectionExprIfaceTyWord]);
    if (interfaceNode) {
      interfaceType = normalize(interfaceNode);
      if (interfaceType == zc::none) return zc::none;
    }
    auto member = identifier(
        ast::IdentId(syntax.payload.words[ast::kAssociatedTypeProjectionExprNameWord]), node);
    if (member == zc::none) return zc::none;
    ZC_IF_SOME(baseValue, base) {
      ZC_IF_SOME(memberValue, member) {
        return CanonicalHeaderTypeSyntax::associatedProjection(
            zc::mv(baseValue), zc::mv(interfaceType), zc::mv(memberValue));
      }
    }
    return reject(node);
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> dynamicType(ast::NodeId node, const ast::Node& syntax) {
    auto principalType =
        normalize(ast::NodeId(syntax.payload.words[ast::kDynTypeExprPrincipalWord]));
    if (principalType == zc::none) return zc::none;
    zc::Maybe<CanonicalNamedHeaderType> principal;
    ZC_IF_SOME(value, principalType) {
      ZC_IF_SOME(named, value.namedType()) { principal = named.clone(); }
    }
    if (principal == zc::none) return reject(node);

    zc::Vector<CanonicalNameReference> markers;
    const ast::NodeId markerList(syntax.payload.words[ast::kDynTypeExprMarkersIdWord]);
    if (markerList) {
      if (!tree.contains(markerList) ||
          tree.node(markerList).kind != ast::SyntaxKind::DynTypeMarkerList) {
        return reject(markerList);
      }
      const auto& list = tree.node(markerList);
      const ast::NodeList values{list.payload.words[ast::kDynTypeMarkerListMarkersFirstWord],
                                 list.payload.words[ast::kDynTypeMarkerListMarkersSizeWord]};
      if (!tree.contains(values)) return reject(markerList);
      for (const auto marker : tree.list(values)) {
        auto name = attributeName(marker);
        if (name == zc::none) return zc::none;
        ZC_IF_SOME(value, name) { markers.add(zc::mv(value)); }
      }
    }

    zc::Vector<CanonicalAssociatedBinding> bindings;
    const ast::NodeId bindingList(syntax.payload.words[ast::kDynTypeExprAssocBindingsIdWord]);
    if (bindingList) {
      if (!tree.contains(bindingList) ||
          tree.node(bindingList).kind != ast::SyntaxKind::DynTypeAssocBindingList) {
        return reject(bindingList);
      }
      const auto& list = tree.node(bindingList);
      const ast::NodeList values{list.payload.words[ast::kDynTypeAssocBindingListBindingsFirstWord],
                                 list.payload.words[ast::kDynTypeAssocBindingListBindingsSizeWord]};
      if (!tree.contains(values)) return reject(bindingList);
      for (const auto binding : tree.list(values)) {
        if (!tree.contains(binding) ||
            tree.node(binding).kind != ast::SyntaxKind::DynTypeAssocBinding) {
          return reject(binding);
        }
        const auto& value = tree.node(binding);
        auto name = identifier(ast::IdentId(value.payload.words[ast::kDynTypeAssocBindingNameWord]),
                               binding);
        auto type = normalize(ast::NodeId(value.payload.words[ast::kDynTypeAssocBindingTyWord]));
        if (name == zc::none || type == zc::none) return zc::none;
        ZC_IF_SOME(nameValue, name) {
          ZC_IF_SOME(typeValue, type) {
            bindings.add(CanonicalAssociatedBinding::from(zc::mv(nameValue), zc::mv(typeValue)));
          }
        }
      }
    }
    ZC_IF_SOME(value, principal) {
      return CanonicalHeaderTypeSyntax::dynamic(zc::mv(value), zc::mv(markers), zc::mv(bindings));
    }
    return reject(node);
  }

  const ast::Tree& tree;
  zc::ArrayPtr<const OracleBinderFrame> frames;
  ast::NodeId& badNode;
};

class HeaderOracle final {
public:
  HeaderOracle(const ast::Tree& tree, const CanonicalHeaderSyntaxView& syntax) noexcept
      : tree(tree), syntaxView(syntax) {}

  CanonicalDefinitionHeaderVerification definition(const DefinitionInventoryEntry& entry) {
    auto syntax = classifyCallable(entry);
    if (syntax == zc::none) return failure(entry.node);
    ZC_IF_SOME(callable, syntax) {
      auto frames = buildBinderStack(tree, syntaxView, entry.parentPath.asPtr(),
                                     callable.genericParameters, badNode);
      if (frames == zc::none) return failure(entry.node);
      ZC_IF_SOME(frameValues, frames) {
        TypeOracle types(tree, frameValues.asPtr(), badNode);
        auto name = declaredCallableName(entry, callable.name);
        if (name == zc::none) return failure(entry.node);
        zc::Vector<CanonicalGenericParameter> generics;
        zc::Vector<CanonicalBoundObligation> obligations;
        if (!genericBlock(callable.genericParameters, true, types, generics, obligations)) {
          return failure(entry.node);
        }
        zc::Vector<CanonicalCallableParameter> parameters;
        zc::Maybe<ReceiverShape> receiver;
        if (!callableParameters(callable, types, parameters, receiver)) {
          return failure(entry.node);
        }
        auto result = callableResult(callable, types);
        auto raises = callableRaises(callable.raises, types);
        if (result == zc::none || (callable.raises && raises == zc::none)) {
          return failure(entry.node);
        }
        ZC_IF_SOME(nameValue, name) {
          ZC_IF_SOME(resultValue, result) {
            auto header = OverloadHeader::from(
                callable.kind, zc::mv(nameValue), zc::mv(receiver), zc::mv(generics),
                zc::mv(obligations), zc::mv(parameters), zc::mv(resultValue), zc::mv(raises),
                zc::mv(callable.externalAbi));
            if (header == zc::none) return failure(entry.node);
            ZC_IF_SOME(value, header) {
              return VerifiedCanonicalDefinitionHeader{
                  identity::OverloadHeaderAuthority::from(zc::mv(value)), zc::mv(boundOccurrences)};
            }
          }
        }
      }
    }
    return failure(entry.node);
  }

  CanonicalImplHeaderVerification implementation(const ImplInventoryEntry& entry) {
    if (!tree.contains(entry.node)) return failure(entry.node);
    const auto& syntax = tree.node(entry.node);
    ast::NodeId genericParameters;
    ast::NodeId whereClause;
    ast::NodeId selfTypeNode;
    ImplPolarity polarity = ImplPolarity::Positive;
    ImplSafety safety = ImplSafety::Safe;
    zc::Maybe<CanonicalTraitReference> markerTrait;
    if (syntax.kind == ast::SyntaxKind::StandaloneImplDecl) {
      genericParameters =
          ast::NodeId(syntax.payload.words[ast::kStandaloneImplDeclTypeParamsIdWord]);
      whereClause = ast::NodeId(syntax.payload.words[ast::kStandaloneImplDeclWhereWord]);
      selfTypeNode = ast::NodeId(syntax.payload.words[ast::kStandaloneImplDeclForTyWord]);
      if (syntax.payload.words[ast::kStandaloneImplDeclIsUnsafeWord] != 0) {
        safety = ImplSafety::Unsafe;
      }
    } else if (syntax.kind == ast::SyntaxKind::MarkerImpl) {
      selfTypeNode = ast::NodeId(syntax.payload.words[ast::kMarkerImplForTyWord]);
      if (syntax.payload.words[ast::kMarkerImplIsNegatedWord] != 0) {
        polarity = ImplPolarity::Negative;
      }
      if (syntax.payload.words[ast::kMarkerImplIsUnsafeWord] != 0) safety = ImplSafety::Unsafe;
      if (polarity == ImplPolarity::Negative && safety == ImplSafety::Unsafe) {
        return failure(entry.node);
      }
      markerTrait =
          markerTraitReference(ast::NodeId(syntax.payload.words[ast::kMarkerImplMarkerPathWord]));
      if (markerTrait == zc::none) return failure(entry.node);
    } else {
      return failure(entry.node);
    }

    auto frames =
        buildBinderStack(tree, syntaxView, entry.parentPath.asPtr(), genericParameters, badNode);
    if (frames == zc::none) return failure(entry.node);
    ZC_IF_SOME(frameValues, frames) {
      TypeOracle types(tree, frameValues.asPtr(), badNode);
      zc::Vector<CanonicalGenericParameter> generics;
      zc::Vector<CanonicalBoundObligation> obligations;
      if (!genericBlock(genericParameters, false, types, generics, obligations) ||
          !whereBounds(whereClause, types, obligations)) {
        return failure(entry.node);
      }
      zc::Maybe<CanonicalTraitReference> trait;
      if (syntax.kind == ast::SyntaxKind::StandaloneImplDecl) {
        trait = standaloneTrait(
            ast::NodeId(syntax.payload.words[ast::kStandaloneImplDeclInterfaceWord]), types);
      } else {
        trait = zc::mv(markerTrait);
      }
      auto selfType = types.normalize(selfTypeNode);
      if (trait == zc::none || selfType == zc::none) return failure(entry.node);
      ZC_IF_SOME(traitValue, trait) {
        ZC_IF_SOME(selfTypeValue, selfType) {
          auto header =
              ImplHeader::from(zc::mv(generics), polarity, safety, zc::mv(traitValue),
                                        zc::mv(selfTypeValue), zc::mv(obligations));
          if (header == zc::none) return failure(entry.node);
          ZC_IF_SOME(value, header) {
            return VerifiedCanonicalImplHeader{zc::mv(value), zc::mv(boundOccurrences)};
          }
        }
      }
    }
    return failure(entry.node);
  }

private:
  CanonicalHeaderVerificationFailure failure(ast::NodeId fallback) const {
    return CanonicalHeaderVerificationFailure{badNode ? badNode : fallback};
  }

  zc::Maybe<CallableSyntax> classifyCallable(const DefinitionInventoryEntry& entry) {
    if (!tree.contains(entry.node)) return zc::none;
    const auto& syntax = tree.node(entry.node);
    switch (syntax.kind) {
      case ast::SyntaxKind::FunctionDecl:
        if (entry.kind != identity::DefinitionKind::Function) return zc::none;
        return CallableSyntax{CallableHeaderKind::Function,
                              ast::IdentId(syntax.payload.words[ast::kFunctionDeclNameWord]),
                              ast::NodeId(syntax.payload.words[ast::kFunctionDeclParamsIdWord]),
                              ast::NodeId(syntax.payload.words[ast::kFunctionDeclTypeParamsIdWord]),
                              ast::NodeId(syntax.payload.words[ast::kFunctionDeclRetTyWord]),
                              ast::NodeId(syntax.payload.words[ast::kFunctionDeclRaisesTyWord]),
                              zc::none,
                              0};
      case ast::SyntaxKind::ExternDecl: {
        if (entry.kind != identity::DefinitionKind::Function) return zc::none;
        const uint32_t abi = syntax.payload.words[ast::kExternDeclAbiWord];
        if (abi > 2) return zc::none;
        return CallableSyntax{CallableHeaderKind::Function,
                              ast::IdentId(syntax.payload.words[ast::kExternDeclNameWord]),
                              entry.node,
                              ast::NodeId(),
                              ast::NodeId(syntax.payload.words[ast::kExternDeclRetTyWord]),
                              ast::NodeId(syntax.payload.words[ast::kExternDeclRaisesTyWord]),
                              zc::Maybe<ExternalAbi>(static_cast<ExternalAbi>(abi + 1)),
                              0};
      }
      case ast::SyntaxKind::MethodDecl:
        if (entry.kind != identity::DefinitionKind::Method) return zc::none;
        return CallableSyntax{CallableHeaderKind::Method,
                              ast::IdentId(syntax.payload.words[ast::kMethodDeclNameWord]),
                              ast::NodeId(syntax.payload.words[ast::kMethodDeclParamsIdWord]),
                              ast::NodeId(syntax.payload.words[ast::kMethodDeclTypeParamsIdWord]),
                              ast::NodeId(syntax.payload.words[ast::kMethodDeclRetTyWord]),
                              ast::NodeId(syntax.payload.words[ast::kMethodDeclRaisesTyWord]),
                              zc::none,
                              static_cast<uint8_t>(syntax.payload.words[ast::kMethodDeclModeWord])};
      case ast::SyntaxKind::ConstructorDecl:
        if (entry.kind != identity::DefinitionKind::Constructor) return zc::none;
        return CallableSyntax{CallableHeaderKind::Constructor,
                              ast::IdentId(syntax.payload.words[ast::kConstructorDeclNameWord]),
                              ast::NodeId(syntax.payload.words[ast::kConstructorDeclParamsIdWord]),
                              ast::NodeId(),
                              ast::NodeId(),
                              ast::NodeId(syntax.payload.words[ast::kConstructorDeclRaisesTyWord]),
                              zc::none,
                              0};
      default:
        return zc::none;
    }
  }

  zc::Maybe<DeclaredDefinitionName> declaredCallableName(const DefinitionInventoryEntry& entry,
                                                         ast::IdentId syntaxName) {
    if (entry.nameKind != InventoryDefinitionNameKind::Declared || !entry.declaredName ||
        tree.ident(entry.declaredName) != tree.ident(syntaxName)) {
      return zc::none;
    }
    return DeclaredDefinitionName::fromSource(tree.ident(syntaxName));
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

  void retainBound(CanonicalBoundObligation&& obligation, ast::NodeId node,
                   zc::Vector<CanonicalBoundObligation>& obligations) {
    boundOccurrences.add(CanonicalBoundSyntaxOccurrence{obligation.clone(), node});
    obligations.add(zc::mv(obligation));
  }

  bool whereBounds(ast::NodeId whereClause, TypeOracle& types,
                   zc::Vector<CanonicalBoundObligation>& obligations) {
    if (!whereClause) return true;
    if (!tree.contains(whereClause) ||
        tree.node(whereClause).kind != ast::SyntaxKind::WhereClause) {
      if (!badNode) badNode = whereClause;
      return false;
    }
    const auto& syntax = tree.node(whereClause);
    const ast::NodeList predicates{syntax.payload.words[ast::kWhereClausePredsFirstWord],
                                   syntax.payload.words[ast::kWhereClausePredsSizeWord]};
    if (!tree.contains(predicates)) {
      if (!badNode) badNode = whereClause;
      return false;
    }
    for (const auto predicate : tree.list(predicates)) {
      if (!tree.contains(predicate) || tree.node(predicate).kind != ast::SyntaxKind::WherePred ||
          tree.node(predicate).payload.words[ast::kWherePredKindWord] !=
              static_cast<uint32_t>(ast::WhereBoundKind::Implements)) {
        if (!badNode) badNode = predicate;
        return false;
      }
      const auto& value = tree.node(predicate);
      auto subject = types.normalize(ast::NodeId(value.payload.words[ast::kWherePredTyWord]));
      auto bound = types.normalize(ast::NodeId(value.payload.words[ast::kWherePredBoundWord]));
      if (subject == zc::none || bound == zc::none) return false;
      ZC_IF_SOME(subjectValue, subject) {
        ZC_IF_SOME(boundValue, bound) {
          retainBound(CanonicalBoundObligation::from(zc::mv(subjectValue), zc::mv(boundValue)),
                      predicate, obligations);
        }
      }
    }
    return true;
  }

  bool genericBlock(ast::NodeId genericParameters, bool consumeWhere, TypeOracle& types,
                    zc::Vector<CanonicalGenericParameter>& generics,
                    zc::Vector<CanonicalBoundObligation>& obligations) {
    if (!genericParameters) return true;
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
        defaultType = types.normalize(defaultNode);
        if (defaultType == zc::none) return false;
      }
      generics.add(CanonicalGenericParameter::from(zc::mv(defaultType)));
      const ast::NodeId boundList(
          parameterSyntax.payload.words[ast::kGenericTypeParamBoundsIdWord]);
      if (boundList) {
        if (!tree.contains(boundList) ||
            tree.node(boundList).kind != ast::SyntaxKind::TypeParameterBoundList) {
          if (!badNode) badNode = boundList;
          return false;
        }
        const auto& listSyntax = tree.node(boundList);
        const ast::NodeList bounds{
            listSyntax.payload.words[ast::kTypeParameterBoundListBoundsFirstWord],
            listSyntax.payload.words[ast::kTypeParameterBoundListBoundsSizeWord]};
        if (!tree.contains(bounds)) {
          if (!badNode) badNode = boundList;
          return false;
        }
        for (const auto boundNode : tree.list(bounds)) {
          auto bound = types.normalize(boundNode);
          if (bound == zc::none) return false;
          ZC_IF_SOME(value, bound) {
            retainBound(CanonicalBoundObligation::from(genericSubject(ordinal), zc::mv(value)),
                        boundNode, obligations);
          }
        }
      }
      ++ordinal;
    }
    const ast::NodeId nestedWhere(syntax.payload.words[ast::kGenericParamsWhereWord]);
    if (!consumeWhere && nestedWhere) {
      if (!badNode) badNode = nestedWhere;
      return false;
    }
    return !consumeWhere || whereBounds(nestedWhere, types, obligations);
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

  bool moveAttribute(ast::NodeId parameter, bool& malformed) const {
    malformed = false;
    const auto& syntax = tree.node(parameter);
    const ast::NodeId list(syntax.payload.words[ast::kFunctionParameterDeclAttrsWord]);
    if (!list) return false;
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
      const bool isMove = pathSyntax.payload.words[ast::kAttributePathLeadingWord] == 0 &&
                          names.size() == 3 && tree.ident(names[0]) == "zom"_zc &&
                          tree.ident(names[1]) == "param"_zc && tree.ident(names[2]) == "move"_zc;
      if (!isMove) continue;
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

  bool receiver(ast::NodeId parameter, uint8_t methodMode, ReceiverShape& shape) {
    const auto& syntax = tree.node(parameter);
    if (syntax.payload.words[ast::kFunctionParameterDeclDefaultWord] != 0 || methodMode == 1 ||
        methodMode >= 3) {
      if (!badNode) badNode = parameter;
      return false;
    }
    const ast::NodeId type(syntax.payload.words[ast::kFunctionParameterDeclTyWord]);
    bool malformed = false;
    const bool move = moveAttribute(parameter, malformed);
    if (malformed) {
      if (!badNode) badNode = parameter;
      return false;
    }
    if (exactSelf(type)) {
      if (move && methodMode == 2) {
        if (!badNode) badNode = parameter;
        return false;
      }
      shape = move ? ReceiverShape::Move
                   : (methodMode == 2 ? ReceiverShape::Mutable : ReceiverShape::Shared);
      return true;
    }
    if (!tree.contains(type) || tree.node(type).kind != ast::SyntaxKind::ReferenceTypeExpr ||
        move) {
      if (!badNode) badNode = parameter;
      return false;
    }
    const auto& reference = tree.node(type);
    if (!exactSelf(ast::NodeId(reference.payload.words[ast::kReferenceTypeExprElemWord]))) {
      if (!badNode) badNode = parameter;
      return false;
    }
    const bool isMutable = reference.payload.words[ast::kReferenceTypeExprIsMutWord] != 0;
    if (!isMutable && methodMode == 2) {
      if (!badNode) badNode = parameter;
      return false;
    }
    shape = isMutable ? ReceiverShape::Mutable : ReceiverShape::Shared;
    return true;
  }

  bool ordinaryParameter(ast::NodeId parameter, TypeOracle& types,
                         zc::Vector<CanonicalCallableParameter>& parameters) {
    const auto& syntax = tree.node(parameter);
    auto label = SemanticIdentifier::fromCanonical(
        tree.ident(ast::IdentId(syntax.payload.words[ast::kFunctionParameterDeclNameWord])));
    auto type =
        types.normalize(ast::NodeId(syntax.payload.words[ast::kFunctionParameterDeclTyWord]));
    if (label == zc::none || type == zc::none) {
      if (!badNode) badNode = parameter;
      return false;
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

  bool callableParameters(const CallableSyntax& callable, TypeOracle& types,
                          zc::Vector<CanonicalCallableParameter>& parameters,
                          zc::Maybe<ReceiverShape>& receiverShape) {
    ast::NodeList parameterNodes;
    if (callable.externalAbi != zc::none) {
      const auto& syntax = tree.node(callable.parameters);
      parameterNodes = ast::NodeList{syntax.payload.words[ast::kExternDeclParamsFirstWord],
                                     syntax.payload.words[ast::kExternDeclParamsSizeWord]};
    } else {
      if (!tree.contains(callable.parameters) ||
          tree.node(callable.parameters).kind != ast::SyntaxKind::FunctionParameterList) {
        if (!badNode) badNode = callable.parameters;
        return false;
      }
      const auto& syntax = tree.node(callable.parameters);
      parameterNodes =
          ast::NodeList{syntax.payload.words[ast::kFunctionParameterListParamsFirstWord],
                        syntax.payload.words[ast::kFunctionParameterListParamsSizeWord]};
    }
    if (!tree.contains(parameterNodes)) {
      if (!badNode) badNode = callable.parameters;
      return false;
    }
    bool foundReceiver = false;
    size_t ordinal = 0;
    for (const auto parameter : tree.list(parameterNodes)) {
      if (!tree.contains(parameter) ||
          tree.node(parameter).kind != ast::SyntaxKind::FunctionParameterDecl) {
        if (!badNode) badNode = parameter;
        return false;
      }
      const auto& syntax = tree.node(parameter);
      const auto name =
          tree.ident(ast::IdentId(syntax.payload.words[ast::kFunctionParameterDeclNameWord]));
      if (name == "this"_zc) {
        if (callable.kind != CallableHeaderKind::Method || foundReceiver || ordinal != 0) {
          if (!badNode) badNode = parameter;
          return false;
        }
        ReceiverShape shape = ReceiverShape::Shared;
        if (!receiver(parameter, callable.methodMode, shape)) return false;
        receiverShape = shape;
        foundReceiver = true;
      } else if (!ordinaryParameter(parameter, types, parameters)) {
        return false;
      }
      ++ordinal;
    }
    if (callable.kind == CallableHeaderKind::Method &&
        ((callable.methodMode == 2 && !foundReceiver) || callable.methodMode >= 3)) {
      return false;
    }
    return true;
  }

  zc::Maybe<CanonicalCallableResult> callableResult(const CallableSyntax& callable,
                                                    TypeOracle& types) {
    if (callable.kind == CallableHeaderKind::Constructor) {
      return CanonicalCallableResult::constructorSelf();
    }
    if (!callable.result) return CanonicalCallableResult::unit();
    auto type = types.normalize(callable.result);
    if (type == zc::none) return zc::none;
    ZC_IF_SOME(value, type) { return CanonicalCallableResult::type(zc::mv(value)); }
    return zc::none;
  }

  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> callableRaises(ast::NodeId raises,
                                                                  TypeOracle& types) {
    if (!raises) return zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>>();
    auto type = types.normalize(raises);
    if (type == zc::none) return zc::none;
    zc::Vector<CanonicalHeaderTypeSyntax> values;
    ZC_IF_SOME(value, type) {
      if (value.kind() == CanonicalHeaderTypeSyntaxKind::Union) {
        ZC_IF_SOME(members, value.members()) {
          for (const auto& member : members) values.add(member.clone());
        }
      } else {
        values.add(zc::mv(value));
      }
    }
    if (values.size() == 0) return zc::none;
    return zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>>(zc::mv(values));
  }

  zc::Maybe<CanonicalTraitReference> standaloneTrait(ast::NodeId node, TypeOracle& types) {
    auto type = types.normalize(node);
    if (type == zc::none) return zc::none;
    ZC_IF_SOME(value, type) {
      ZC_IF_SOME(named, value.namedType()) {
        zc::Vector<CanonicalHeaderTypeSyntax> arguments(named.arguments().size());
        for (const auto& argument : named.arguments()) arguments.add(argument.clone());
        return CanonicalTraitReference::from(named.name().clone(), zc::mv(arguments));
      }
    }
    if (!badNode) badNode = node;
    return zc::none;
  }

  zc::Maybe<CanonicalTraitReference> markerTraitReference(ast::NodeId path) {
    if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::AttributePath) {
      if (!badNode) badNode = path;
      return zc::none;
    }
    const auto& syntax = tree.node(path);
    const ast::IdentList segments{syntax.payload.words[ast::kAttributePathSegmentsFirstWord],
                                  syntax.payload.words[ast::kAttributePathSegmentsSizeWord]};
    if (syntax.payload.words[ast::kAttributePathLeadingWord] != 0 || segments.size == 0 ||
        !tree.contains(segments)) {
      if (!badNode) badNode = path;
      return zc::none;
    }
    zc::Vector<SemanticIdentifier> suffix(segments.size);
    for (const auto segment : tree.identList(segments)) {
      auto name = SemanticIdentifier::fromCanonical(tree.ident(segment));
      if (name == zc::none) {
        if (!badNode) badNode = path;
        return zc::none;
      }
      ZC_IF_SOME(value, name) { suffix.add(zc::mv(value)); }
    }
    auto name = CanonicalNameReference::from(CanonicalNameRoot::relative(), zc::mv(suffix));
    if (name == zc::none) return zc::none;
    ZC_IF_SOME(value, name) {
      zc::Vector<CanonicalHeaderTypeSyntax> arguments;
      return CanonicalTraitReference::from(zc::mv(value), zc::mv(arguments));
    }
    return zc::none;
  }

  const ast::Tree& tree;
  const CanonicalHeaderSyntaxView& syntaxView;
  zc::Vector<CanonicalBoundSyntaxOccurrence> boundOccurrences;
  ast::NodeId badNode;
};

}  // namespace

CanonicalDefinitionHeaderVerification CanonicalHeaderVerifier::reconstructDefinition(
    const ast::Tree& tree, const CanonicalHeaderSyntaxView& syntax,
    const DefinitionInventoryEntry& definition) {
  return HeaderOracle(tree, syntax).definition(definition);
}

CanonicalImplHeaderVerification CanonicalHeaderVerifier::reconstructImpl(
    const ast::Tree& tree, const CanonicalHeaderSyntaxView& syntax,
    const ImplInventoryEntry& implementation) {
  return HeaderOracle(tree, syntax).implementation(implementation);
}

}  // namespace zomlang::compiler::binder
