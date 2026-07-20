// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/canonical-header-type-producer.h"

#include <cstdint>

#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/identity/canonical-header-name.h"

namespace zomlang::compiler::binder {
namespace {

using identity::CanonicalAssociatedBinding;
using identity::CanonicalHeaderTypeSyntax;
using identity::CanonicalNamedHeaderType;
using identity::CanonicalNameReference;
using identity::CanonicalNameRoot;
using identity::CanonicalObjectTypeMember;
using identity::PredefinedTypeKind;
using identity::RawPointerMutability;
using identity::ReferenceMutability;
using identity::SemanticIdentifier;

class Producer final {
public:
  Producer(const ast::Tree& tree, zc::ArrayPtr<const CanonicalGenericBinderFrame> binders) noexcept
      : tree(tree), binders(binders) {}

  CanonicalHeaderTypeProduction produce(ast::NodeId type) {
    if (!validateBinders()) { return failure; }
    auto value = build(type);
    ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
    return failure;
  }

  zc::Maybe<CanonicalHeaderSyntaxFailure> validate() {
    if (!validateBinders()) { return failure; }
    return zc::none;
  }

private:
  bool reject(CanonicalHeaderSyntaxFailureKind kind, ast::NodeId node) {
    if (!rejected) {
      rejected = true;
      failure = CanonicalHeaderSyntaxFailure{kind, node};
    }
    return false;
  }

  zc::Maybe<SemanticIdentifier> semanticName(ast::IdentId name, ast::NodeId owner) {
    auto value = SemanticIdentifier::fromCanonical(tree.ident(name));
    if (value == zc::none) { reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, owner); }
    return value;
  }

  bool validateBinders() {
    for (const auto& frame : binders) {
      if (!frame.genericParameters) { continue; }
      if (!tree.contains(frame.genericParameters) ||
          tree.node(frame.genericParameters).kind != ast::SyntaxKind::GenericParams) {
        return reject(CanonicalHeaderSyntaxFailureKind::InvalidBinderStack,
                      frame.genericParameters);
      }
      const auto& syntax = tree.node(frame.genericParameters);
      const ast::NodeList parameters{syntax.payload.words[ast::kGenericParamsParamsFirstWord],
                                     syntax.payload.words[ast::kGenericParamsParamsSizeWord]};
      if (!tree.contains(parameters)) {
        return reject(CanonicalHeaderSyntaxFailureKind::InvalidBinderStack,
                      frame.genericParameters);
      }
      const auto values = tree.list(parameters);
      for (size_t index = 0; index < values.size(); ++index) {
        if (!tree.contains(values[index]) ||
            tree.node(values[index]).kind != ast::SyntaxKind::GenericTypeParam) {
          return reject(CanonicalHeaderSyntaxFailureKind::InvalidBinderStack, values[index]);
        }
        const ast::IdentId name(
            tree.node(values[index]).payload.words[ast::kGenericTypeParamNameWord]);
        if (semanticName(name, values[index]) == zc::none) { return false; }
      }
    }
    return true;
  }

  zc::Maybe<CanonicalNameRoot> genericRoot(ast::IdentId first) {
    for (size_t depth = 0; depth < binders.size(); ++depth) {
      if (!binders[depth].genericParameters) { continue; }
      const auto& syntax = tree.node(binders[depth].genericParameters);
      const ast::NodeList parameters{syntax.payload.words[ast::kGenericParamsParamsFirstWord],
                                     syntax.payload.words[ast::kGenericParamsParamsSizeWord]};
      const auto values = tree.list(parameters);
      for (size_t ordinal = 0; ordinal < values.size(); ++ordinal) {
        const ast::IdentId name(
            tree.node(values[ordinal]).payload.words[ast::kGenericTypeParamNameWord]);
        if (tree.ident(first) == tree.ident(name)) {
          if (depth > UINT32_MAX || ordinal > UINT32_MAX) {
            reject(CanonicalHeaderSyntaxFailureKind::InvalidBinderStack, values[ordinal]);
            return zc::none;
          }
          return CanonicalNameRoot::generic(static_cast<uint32_t>(depth),
                                            static_cast<uint32_t>(ordinal));
        }
      }
    }
    return zc::none;
  }

  zc::Maybe<CanonicalNameReference> modulePathName(ast::NodeId path) {
    if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::ModulePath) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, path);
      return zc::none;
    }
    const auto& syntax = tree.node(path);
    const ast::IdentList segments{syntax.payload.words[ast::kModulePathSegmentsFirstWord],
                                  syntax.payload.words[ast::kModulePathSegmentsSizeWord]};
    if (segments.size == 0 || !tree.contains(segments)) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, path);
      return zc::none;
    }
    const auto names = tree.identList(segments);
    const auto rootTag = syntax.payload.words[ast::kModulePathRootWord];
    CanonicalNameRoot root =
        rootTag == 1 ? CanonicalNameRoot::absolute() : CanonicalNameRoot::relative();
    size_t suffixStart = 0;
    if (rootTag == 0) {
      auto generic = genericRoot(names[0]);
      ZC_IF_SOME(value, generic) {
        root = zc::mv(value);
        suffixStart = 1;
      }
    } else if (rootTag != 1) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, path);
      return zc::none;
    }
    zc::Vector<SemanticIdentifier> suffix(names.size() - suffixStart);
    for (size_t index = suffixStart; index < names.size(); ++index) {
      auto segment = semanticName(names[index], path);
      if (segment == zc::none) { return zc::none; }
      ZC_IF_SOME(value, segment) { suffix.add(zc::mv(value)); }
    }
    auto name = CanonicalNameReference::from(zc::mv(root), zc::mv(suffix));
    if (name == zc::none) { reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, path); }
    return name;
  }

  zc::Maybe<CanonicalNameReference> attributePathName(ast::NodeId path) {
    if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::AttributePath) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, path);
      return zc::none;
    }
    const auto& syntax = tree.node(path);
    const ast::IdentList segments{syntax.payload.words[ast::kAttributePathSegmentsFirstWord],
                                  syntax.payload.words[ast::kAttributePathSegmentsSizeWord]};
    if (segments.size == 0 || !tree.contains(segments)) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, path);
      return zc::none;
    }
    const auto leading = syntax.payload.words[ast::kAttributePathLeadingWord];
    if (leading != 0) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, path);
      return zc::none;
    }
    const auto names = tree.identList(segments);
    zc::Vector<SemanticIdentifier> suffix(names.size());
    for (const auto name : names) {
      auto segment = semanticName(name, path);
      if (segment == zc::none) { return zc::none; }
      ZC_IF_SOME(value, segment) { suffix.add(zc::mv(value)); }
    }
    return CanonicalNameReference::from(CanonicalNameRoot::relative(), zc::mv(suffix));
  }

  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> buildTypes(ast::NodeList nodes) {
    if (!tree.contains(nodes)) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, ast::NodeId());
      return zc::none;
    }
    zc::Vector<CanonicalHeaderTypeSyntax> result(nodes.size);
    for (const auto node : tree.list(nodes)) {
      auto value = build(node);
      if (value == zc::none) { return zc::none; }
      ZC_IF_SOME(admitted, value) { result.add(zc::mv(admitted)); }
    }
    return zc::mv(result);
  }

  bool appendRaises(ast::NodeId node, zc::Vector<CanonicalHeaderTypeSyntax>& result) {
    if (!tree.contains(node)) {
      return reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, node);
    }
    const auto& syntax = tree.node(node);
    if (syntax.kind == ast::SyntaxKind::UnionTypeExpr) {
      const ast::NodeList alternatives{syntax.payload.words[ast::kUnionTypeExprAltsFirstWord],
                                       syntax.payload.words[ast::kUnionTypeExprAltsSizeWord]};
      if (!tree.contains(alternatives) || alternatives.size == 0) {
        return reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, node);
      }
      for (const auto alternative : tree.list(alternatives)) {
        if (!appendRaises(alternative, result)) { return false; }
      }
      return true;
    }
    auto value = build(node);
    if (value == zc::none) { return false; }
    ZC_IF_SOME(admitted, value) { result.add(zc::mv(admitted)); }
    return true;
  }

  zc::Maybe<uint64_t> evaluatedArrayLength(ast::NodeId node) {
    if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::IntLiteral) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidConstantExpression, node);
      return zc::none;
    }
    const auto& syntax = tree.node(node);
    const auto base = syntax.payload.words[ast::kIntLiteralBaseWord];
    if (base != 2 && base != 8 && base != 10 && base != 16) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidConstantExpression, node);
      return zc::none;
    }
    const auto text = tree.bigInt(ast::BigIntId(syntax.payload.words[ast::kIntLiteralValueWord]));
    uint64_t value = 0;
    for (const auto character : text) {
      if (character == '_') { continue; }
      uint8_t digit = 0xff;
      if (character >= '0' && character <= '9') {
        digit = static_cast<uint8_t>(character - '0');
      } else if (character >= 'a' && character <= 'f') {
        digit = static_cast<uint8_t>(character - 'a' + 10);
      } else if (character >= 'A' && character <= 'F') {
        digit = static_cast<uint8_t>(character - 'A' + 10);
      }
      if (digit >= base || value > (UINT64_MAX - digit) / base) {
        reject(CanonicalHeaderSyntaxFailureKind::InvalidConstantExpression, node);
        return zc::none;
      }
      value = value * base + digit;
    }
    return value;
  }

  zc::Maybe<CanonicalHeaderTypeSyntax> build(ast::NodeId node) {
    if (!tree.contains(node)) {
      reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, node);
      return zc::none;
    }
    const auto& syntax = tree.node(node);
    switch (syntax.kind) {
      case ast::SyntaxKind::NamedTypeExpr: {
        auto name = modulePathName(ast::NodeId(syntax.payload.words[ast::kNamedTypeExprPathWord]));
        const ast::NodeList arguments{syntax.payload.words[ast::kNamedTypeExprArgsFirstWord],
                                      syntax.payload.words[ast::kNamedTypeExprArgsSizeWord]};
        auto types = buildTypes(arguments);
        if (name == zc::none || types == zc::none) { return zc::none; }
        ZC_IF_SOME(nameValue, name) {
          ZC_IF_SOME(typeValues, types) {
            return CanonicalHeaderTypeSyntax::named(
                CanonicalNamedHeaderType::from(zc::mv(nameValue), zc::mv(typeValues)));
          }
        }
        return zc::none;
      }
      case ast::SyntaxKind::PredefinedTypeExpr: {
        const auto kind = syntax.payload.words[ast::kPredefinedTypeExprKindWord];
        if (kind > 16) {
          reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, node);
          return zc::none;
        }
        return CanonicalHeaderTypeSyntax::predefined(static_cast<PredefinedTypeKind>(kind + 1));
      }
      case ast::SyntaxKind::FunctionTypeExpr: {
        const ast::NodeList parameters{syntax.payload.words[ast::kFunctionTypeExprParamsFirstWord],
                                       syntax.payload.words[ast::kFunctionTypeExprParamsSizeWord]};
        auto parameterTypes = buildTypes(parameters);
        auto result = build(ast::NodeId(syntax.payload.words[ast::kFunctionTypeExprRetTyWord]));
        if (parameterTypes == zc::none || result == zc::none) { return zc::none; }
        zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises;
        const ast::NodeId raisesNode(syntax.payload.words[ast::kFunctionTypeExprRaisesWord]);
        if (raisesNode) {
          zc::Vector<CanonicalHeaderTypeSyntax> members;
          if (!appendRaises(raisesNode, members) || members.size() == 0) { return zc::none; }
          raises = zc::mv(members);
        }
        ZC_IF_SOME(parameterValues, parameterTypes) {
          ZC_IF_SOME(resultValue, result) {
            auto admitted = CanonicalHeaderTypeSyntax::function(
                zc::mv(parameterValues), zc::mv(resultValue), zc::mv(raises));
            if (admitted == zc::none) {
              reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, node);
            }
            return admitted;
          }
        }
        return zc::none;
      }
      case ast::SyntaxKind::UnionTypeExpr:
      case ast::SyntaxKind::IntersectionTypeExpr: {
        const ast::NodeList alternatives{
            syntax.payload.words[syntax.kind == ast::SyntaxKind::UnionTypeExpr
                                     ? ast::kUnionTypeExprAltsFirstWord
                                     : ast::kIntersectionTypeExprAltsFirstWord],
            syntax.payload.words[syntax.kind == ast::SyntaxKind::UnionTypeExpr
                                     ? ast::kUnionTypeExprAltsSizeWord
                                     : ast::kIntersectionTypeExprAltsSizeWord]};
        auto members = buildTypes(alternatives);
        if (members == zc::none) { return zc::none; }
        ZC_IF_SOME(values, members) {
          auto admitted = syntax.kind == ast::SyntaxKind::UnionTypeExpr
                              ? CanonicalHeaderTypeSyntax::unionOf(zc::mv(values))
                              : CanonicalHeaderTypeSyntax::intersectionOf(zc::mv(values));
          if (admitted == zc::none) {
            reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, node);
          }
          return admitted;
        }
        return zc::none;
      }
      case ast::SyntaxKind::FixedArrayTypeExpr: {
        auto element = build(ast::NodeId(syntax.payload.words[ast::kFixedArrayTypeExprElemWord]));
        auto length = evaluatedArrayLength(
            ast::NodeId(syntax.payload.words[ast::kFixedArrayTypeExprLenExprWord]));
        if (element == zc::none || length == zc::none) { return zc::none; }
        ZC_IF_SOME(elementValue, element) {
          ZC_IF_SOME(lengthValue, length) {
            return CanonicalHeaderTypeSyntax::fixedArray(zc::mv(elementValue), lengthValue);
          }
        }
        return zc::none;
      }
      case ast::SyntaxKind::ArrayTypeExpr:
      case ast::SyntaxKind::SliceArrayTypeExpr: {
        const auto word = syntax.kind == ast::SyntaxKind::ArrayTypeExpr
                              ? ast::kArrayTypeExprElemWord
                              : ast::kSliceArrayTypeExprElemWord;
        auto element = build(ast::NodeId(syntax.payload.words[word]));
        if (element == zc::none) { return zc::none; }
        ZC_IF_SOME(value, element) {
          return syntax.kind == ast::SyntaxKind::ArrayTypeExpr
                     ? CanonicalHeaderTypeSyntax::dynamicArray(zc::mv(value))
                     : CanonicalHeaderTypeSyntax::slice(zc::mv(value));
        }
        return zc::none;
      }
      case ast::SyntaxKind::OptionalTypeExpr: {
        auto element = build(ast::NodeId(syntax.payload.words[ast::kOptionalTypeExprInnerWord]));
        if (element == zc::none) { return zc::none; }
        ZC_IF_SOME(value, element) {
          auto admitted = CanonicalHeaderTypeSyntax::optional(
              zc::mv(value), syntax.payload.words[ast::kOptionalTypeExprDoubleWord] != 0 ? 2 : 1);
          if (admitted == zc::none) {
            reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, node);
          }
          return admitted;
        }
        return zc::none;
      }
      case ast::SyntaxKind::ReferenceTypeExpr:
      case ast::SyntaxKind::RawPointerTypeExpr: {
        const bool raw = syntax.kind == ast::SyntaxKind::RawPointerTypeExpr;
        const auto elementWord =
            raw ? ast::kRawPointerTypeExprElemWord : ast::kReferenceTypeExprElemWord;
        const auto mutableWord =
            raw ? ast::kRawPointerTypeExprIsMutWord : ast::kReferenceTypeExprIsMutWord;
        auto element = build(ast::NodeId(syntax.payload.words[elementWord]));
        if (element == zc::none) { return zc::none; }
        ZC_IF_SOME(value, element) {
          auto admitted =
              raw ? CanonicalHeaderTypeSyntax::rawPointer(syntax.payload.words[mutableWord] != 0
                                                              ? RawPointerMutability::Mutable
                                                              : RawPointerMutability::Const,
                                                          zc::mv(value))
                  : CanonicalHeaderTypeSyntax::reference(syntax.payload.words[mutableWord] != 0
                                                             ? ReferenceMutability::Mutable
                                                             : ReferenceMutability::Shared,
                                                         zc::mv(value));
          if (admitted == zc::none) {
            reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, node);
          }
          return admitted;
        }
        return zc::none;
      }
      case ast::SyntaxKind::TypeQueryExpr: {
        auto name = modulePathName(ast::NodeId(syntax.payload.words[ast::kTypeQueryExprPathWord]));
        if (name == zc::none) { return zc::none; }
        ZC_IF_SOME(value, name) { return CanonicalHeaderTypeSyntax::typeQuery(zc::mv(value)); }
        return zc::none;
      }
      case ast::SyntaxKind::ObjectTypeExpr: {
        const ast::NodeList members{syntax.payload.words[ast::kObjectTypeExprMembersFirstWord],
                                    syntax.payload.words[ast::kObjectTypeExprMembersSizeWord]};
        if (!tree.contains(members)) {
          reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, node);
          return zc::none;
        }
        zc::Vector<CanonicalObjectTypeMember> result(members.size);
        for (const auto member : tree.list(members)) {
          if (!tree.contains(member) ||
              tree.node(member).kind != ast::SyntaxKind::ObjectTypeMember) {
            reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, member);
            return zc::none;
          }
          const auto& memberSyntax = tree.node(member);
          auto name = semanticName(
              ast::IdentId(memberSyntax.payload.words[ast::kObjectTypeMemberNameWord]), member);
          auto type = build(ast::NodeId(memberSyntax.payload.words[ast::kObjectTypeMemberTyWord]));
          if (name == zc::none || type == zc::none) { return zc::none; }
          ZC_IF_SOME(nameValue, name) {
            ZC_IF_SOME(typeValue, type) {
              result.add(CanonicalObjectTypeMember::from(
                  zc::mv(nameValue), zc::mv(typeValue),
                  memberSyntax.payload.words[ast::kObjectTypeMemberIsMutWord] != 0,
                  memberSyntax.payload.words[ast::kObjectTypeMemberIsOptionalWord] != 0));
            }
          }
        }
        return CanonicalHeaderTypeSyntax::object(zc::mv(result));
      }
      case ast::SyntaxKind::TupleTypeExpr: {
        const ast::NodeList elements{syntax.payload.words[ast::kTupleTypeExprElemsFirstWord],
                                     syntax.payload.words[ast::kTupleTypeExprElemsSizeWord]};
        auto values = buildTypes(elements);
        if (values == zc::none) { return zc::none; }
        ZC_IF_SOME(admitted, values) { return CanonicalHeaderTypeSyntax::tuple(zc::mv(admitted)); }
        return zc::none;
      }
      case ast::SyntaxKind::AssociatedTypeProjectionExpr: {
        auto base =
            build(ast::NodeId(syntax.payload.words[ast::kAssociatedTypeProjectionExprBaseTyWord]));
        if (base == zc::none) { return zc::none; }
        zc::Maybe<CanonicalHeaderTypeSyntax> interfaceType;
        const ast::NodeId interfaceNode(
            syntax.payload.words[ast::kAssociatedTypeProjectionExprIfaceTyWord]);
        if (interfaceNode) {
          interfaceType = build(interfaceNode);
          if (interfaceType == zc::none) { return zc::none; }
        }
        auto member = semanticName(
            ast::IdentId(syntax.payload.words[ast::kAssociatedTypeProjectionExprNameWord]), node);
        if (member == zc::none) { return zc::none; }
        ZC_IF_SOME(baseValue, base) {
          ZC_IF_SOME(memberValue, member) {
            return CanonicalHeaderTypeSyntax::associatedProjection(
                zc::mv(baseValue), zc::mv(interfaceType), zc::mv(memberValue));
          }
        }
        return zc::none;
      }
      case ast::SyntaxKind::DynTypeExpr: {
        auto principalType =
            build(ast::NodeId(syntax.payload.words[ast::kDynTypeExprPrincipalWord]));
        if (principalType == zc::none) { return zc::none; }
        zc::Maybe<CanonicalNamedHeaderType> principal;
        ZC_IF_SOME(value, principalType) {
          ZC_IF_SOME(named, value.namedType()) { principal = named.clone(); }
        }
        if (principal == zc::none) {
          reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, node);
          return zc::none;
        }

        zc::Vector<CanonicalNameReference> markers;
        const ast::NodeId markerList(syntax.payload.words[ast::kDynTypeExprMarkersIdWord]);
        if (markerList) {
          if (!tree.contains(markerList) ||
              tree.node(markerList).kind != ast::SyntaxKind::DynTypeMarkerList) {
            reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, markerList);
            return zc::none;
          }
          const auto& markerSyntax = tree.node(markerList);
          const ast::NodeList values{
              markerSyntax.payload.words[ast::kDynTypeMarkerListMarkersFirstWord],
              markerSyntax.payload.words[ast::kDynTypeMarkerListMarkersSizeWord]};
          if (!tree.contains(values)) {
            reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, markerList);
            return zc::none;
          }
          for (const auto marker : tree.list(values)) {
            auto name = attributePathName(marker);
            if (name == zc::none) { return zc::none; }
            ZC_IF_SOME(value, name) { markers.add(zc::mv(value)); }
          }
        }

        zc::Vector<CanonicalAssociatedBinding> bindings;
        const ast::NodeId bindingList(syntax.payload.words[ast::kDynTypeExprAssocBindingsIdWord]);
        if (bindingList) {
          if (!tree.contains(bindingList) ||
              tree.node(bindingList).kind != ast::SyntaxKind::DynTypeAssocBindingList) {
            reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, bindingList);
            return zc::none;
          }
          const auto& bindingSyntax = tree.node(bindingList);
          const ast::NodeList values{
              bindingSyntax.payload.words[ast::kDynTypeAssocBindingListBindingsFirstWord],
              bindingSyntax.payload.words[ast::kDynTypeAssocBindingListBindingsSizeWord]};
          if (!tree.contains(values)) {
            reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, bindingList);
            return zc::none;
          }
          for (const auto binding : tree.list(values)) {
            if (!tree.contains(binding) ||
                tree.node(binding).kind != ast::SyntaxKind::DynTypeAssocBinding) {
              reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, binding);
              return zc::none;
            }
            const auto& valueSyntax = tree.node(binding);
            auto name = semanticName(
                ast::IdentId(valueSyntax.payload.words[ast::kDynTypeAssocBindingNameWord]),
                binding);
            auto type =
                build(ast::NodeId(valueSyntax.payload.words[ast::kDynTypeAssocBindingTyWord]));
            if (name == zc::none || type == zc::none) { return zc::none; }
            ZC_IF_SOME(nameValue, name) {
              ZC_IF_SOME(typeValue, type) {
                bindings.add(
                    CanonicalAssociatedBinding::from(zc::mv(nameValue), zc::mv(typeValue)));
              }
            }
          }
        }
        ZC_IF_SOME(principalValue, principal) {
          return CanonicalHeaderTypeSyntax::dynamic(zc::mv(principalValue), zc::mv(markers),
                                                    zc::mv(bindings));
        }
        return zc::none;
      }
      default:
        reject(CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax, node);
        return zc::none;
    }
  }

  const ast::Tree& tree;
  zc::ArrayPtr<const CanonicalGenericBinderFrame> binders;
  bool rejected = false;
  CanonicalHeaderSyntaxFailure failure{CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax,
                                       ast::NodeId()};
};

}  // namespace

CanonicalHeaderTypeProduction CanonicalHeaderTypeProducer::produceType(
    const ast::Tree& tree, ast::NodeId type,
    zc::ArrayPtr<const CanonicalGenericBinderFrame> binders) {
  return Producer(tree, binders).produce(type);
}

zc::Maybe<CanonicalHeaderSyntaxFailure> CanonicalHeaderTypeProducer::validateBinderStack(
    const ast::Tree& tree, zc::ArrayPtr<const CanonicalGenericBinderFrame> binders) {
  return Producer(tree, binders).validate();
}

}  // namespace zomlang::compiler::binder
