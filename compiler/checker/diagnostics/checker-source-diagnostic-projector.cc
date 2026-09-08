// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/checker/diagnostics/checker-source-diagnostic-projector.h"

#include <cstring>

#include "compiler/ast/generated/node-traverse.h"
#include "compiler/diagnostics/text/diagnostic-text.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::checker {
namespace {

using diagnostics::SemanticDiagnosticBatchProjectionFailure;
using diagnostics::SemanticDiagnosticBatchProjectionResult;
using diagnostics::SemanticDiagnosticFactBatch;

zc::String renderPrimitive(type::semantic::PrimitiveKind kind) {
  using type::semantic::PrimitiveKind;
  switch (kind) {
    case PrimitiveKind::I8:
      return zc::str("i8");
    case PrimitiveKind::I16:
      return zc::str("i16");
    case PrimitiveKind::I32:
      return zc::str("i32");
    case PrimitiveKind::I64:
      return zc::str("i64");
    case PrimitiveKind::U8:
      return zc::str("u8");
    case PrimitiveKind::U16:
      return zc::str("u16");
    case PrimitiveKind::U32:
      return zc::str("u32");
    case PrimitiveKind::U64:
      return zc::str("u64");
    case PrimitiveKind::Isize:
      return zc::str("isize");
    case PrimitiveKind::Usize:
      return zc::str("usize");
    case PrimitiveKind::F32:
      return zc::str("f32");
    case PrimitiveKind::F64:
      return zc::str("f64");
    case PrimitiveKind::Bool:
      return zc::str("bool");
    case PrimitiveKind::Char:
      return zc::str("char");
    case PrimitiveKind::Str:
      return zc::str("str");
    case PrimitiveKind::Unit:
      return zc::str("unit");
    case PrimitiveKind::Never:
      return zc::str("never");
    case PrimitiveKind::Any:
      return zc::str("any");
    case PrimitiveKind::Null:
      return zc::str("null");
  }
  ZC_UNREACHABLE
}

constexpr size_t MAXIMUM_DISPLAY_SCALARS = 64;
constexpr size_t MAXIMUM_DISPLAY_ITEMS = 16;
constexpr uint32_t MAXIMUM_TYPE_DEPTH = 12;

void append(zc::Vector<char>& output, zc::StringPtr text) { output.addAll(text); }

void append(zc::Vector<char>& output, zc::String&& text) { output.addAll(text); }

zc::String escaped(zc::StringPtr text,
                   diagnostics::DiagnosticQuote quote = diagnostics::DiagnosticQuote::None) {
  return diagnostics::escapeDiagnosticText(text.asBytes(), quote, MAXIMUM_DISPLAY_SCALARS);
}

void appendDefinition(zc::Vector<char>& output, identity::DefId definition,
                      const CheckerIdentityAuthority& identities) {
  auto lookup = identities.definition(definition);
  if (lookup == zc::none) {
    append(output, "<invalid-definition>"_zc);
    return;
  }
  ZC_IF_SOME(record, lookup) {
    const auto& module = record.record().module();
    const auto& unit = module.crate().unit();
    if (unit.kind() == identity::CompilationUnitKind::UserPackage) {
      append(output, escaped(unit.userPackage().name()));
    } else {
      append(output, "core"_zc);
    }
    append(output, "::"_zc);
    append(output, escaped(module.crate().targetName()));
    for (const auto& segment : module.path()) {
      append(output, "::"_zc);
      append(output, escaped(segment.text()));
    }
    for (const auto& owner : record.record().owners()) {
      append(output, "::"_zc);
      if (owner.kind() == identity::EnclosingStableOwnerKind::Definition) {
        ZC_IF_SOME(key, owner.definitionKey()) {
          ZC_IF_SOME(owner, identities.definition(key)) {
            append(output, escaped(owner.record().name()));
            continue;
          }
        }
        append(output, "<invalid-owner>"_zc);
        continue;
      }
      append(output, "<impl>"_zc);
    }
    append(output, "::"_zc);
    append(output, escaped(record.record().name()));
  }
}

void appendGenericParameter(zc::Vector<char>& output, const identity::GenericParameterKey& key,
                            const CheckerIdentityAuthority& identities) {
  auto parameter = identities.genericParameter(key);
  if (parameter == zc::none) {
    append(output, "<invalid-type-parameter>"_zc);
    return;
  }
  ZC_IF_SOME(value, parameter) {
    append(output, "<type-parameter#"_zc);
    append(output, zc::str(value.record().ordinal()));
    append(output, ">"_zc);
    return;
  }
  append(output, "<invalid-type-parameter>"_zc);
}

void appendType(zc::Vector<char>& output, identity::SemanticTypeId typeId,
                const CheckerIdentityAuthority& identities,
                const type::SemanticTypeStore& semanticTypes, uint32_t depth);

void appendTypeList(zc::Vector<char>& output, zc::ArrayPtr<const identity::SemanticTypeId> types,
                    const CheckerIdentityAuthority& identities,
                    const type::SemanticTypeStore& semanticTypes, uint32_t depth,
                    zc::StringPtr separator) {
  const size_t count = types.size() < MAXIMUM_DISPLAY_ITEMS ? types.size() : MAXIMUM_DISPLAY_ITEMS;
  for (size_t index = 0; index < count; ++index) {
    if (index != 0) append(output, separator);
    appendType(output, types[index], identities, semanticTypes, depth + 1);
  }
  if (count != types.size()) {
    if (count != 0) append(output, separator);
    append(output, "..."_zc);
  }
}

void appendInterface(zc::Vector<char>& output,
                     const type::semantic::InterfaceInstantiation& interface,
                     const CheckerIdentityAuthority& identities,
                     const type::SemanticTypeStore& semanticTypes, uint32_t depth) {
  appendDefinition(output, interface.interface, identities);
  if (interface.arguments.size() == 0) return;
  append(output, "<"_zc);
  appendTypeList(output, interface.arguments.asPtr(), identities, semanticTypes, depth, ", "_zc);
  append(output, ">"_zc);
}

void appendType(zc::Vector<char>& output, identity::SemanticTypeId typeId,
                const CheckerIdentityAuthority& identities,
                const type::SemanticTypeStore& semanticTypes, uint32_t depth) {
  if (depth >= MAXIMUM_TYPE_DEPTH) {
    append(output, "..."_zc);
    return;
  }
  auto lookup = semanticTypes.get(typeId);
  if (!lookup.is<type::SemanticTypeLookup>()) {
    append(output, "<invalid-type>"_zc);
    return;
  }
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_IF_SOME(kind, data.primitiveKind()) {
    append(output, renderPrimitive(kind));
    return;
  }
  if (data.is<type::semantic::TupleTypeData>()) {
    append(output, "("_zc);
    const auto& value = data.get<type::semantic::TupleTypeData>();
    appendTypeList(output, value.elements.asPtr(), identities, semanticTypes, depth, ", "_zc);
    if (value.elements.size() == 1) append(output, ","_zc);
    append(output, ")"_zc);
    return;
  }
  if (data.is<type::semantic::ObjectTypeData>()) {
    append(output, "{"_zc);
    const auto& value = data.get<type::semantic::ObjectTypeData>();
    const size_t count =
        value.fields.size() < MAXIMUM_DISPLAY_ITEMS ? value.fields.size() : MAXIMUM_DISPLAY_ITEMS;
    for (size_t index = 0; index < count; ++index) {
      if (index != 0) append(output, ", "_zc);
      const auto& field = value.fields[index];
      if (field.mutability == type::semantic::Mutability::Mutable) append(output, "mut "_zc);
      append(output, escaped(field.name.text()));
      if (field.presence == type::semantic::FieldPresence::Optional) append(output, "?"_zc);
      append(output, ": "_zc);
      appendType(output, field.type, identities, semanticTypes, depth + 1);
    }
    if (count != value.fields.size()) {
      if (count != 0) append(output, ", "_zc);
      append(output, "..."_zc);
    }
    append(output, "}"_zc);
    return;
  }
  if (data.is<type::semantic::DynamicArrayTypeData>()) {
    append(output, "["_zc);
    appendType(output, data.get<type::semantic::DynamicArrayTypeData>().element, identities,
               semanticTypes, depth + 1);
    append(output, "]"_zc);
    return;
  }
  if (data.is<type::semantic::SliceTypeData>()) {
    append(output, "slice<"_zc);
    appendType(output, data.get<type::semantic::SliceTypeData>().element, identities, semanticTypes,
               depth + 1);
    append(output, ">"_zc);
    return;
  }
  if (data.is<type::semantic::FixedArrayTypeData>()) {
    const auto& value = data.get<type::semantic::FixedArrayTypeData>();
    append(output, "["_zc);
    appendType(output, value.element, identities, semanticTypes, depth + 1);
    append(output, "; "_zc);
    append(output, zc::str(value.length));
    append(output, "]"_zc);
    return;
  }
  if (data.is<type::semantic::FunctionTypeData>()) {
    const auto& value = data.get<type::semantic::FunctionTypeData>();
    append(output, "("_zc);
    appendTypeList(output, value.parameters.asPtr(), identities, semanticTypes, depth, ", "_zc);
    append(output, ") -> "_zc);
    appendType(output, value.success, identities, semanticTypes, depth + 1);
    ZC_IF_SOME(raises, value.raises) {
      append(output, " raises "_zc);
      appendType(output, raises, identities, semanticTypes, depth + 1);
    }
    return;
  }
  if (data.is<type::semantic::NominalTypeData>()) {
    const auto& value = data.get<type::semantic::NominalTypeData>();
    appendDefinition(output, value.definition, identities);
    if (value.arguments.size() != 0) {
      append(output, "<"_zc);
      appendTypeList(output, value.arguments.asPtr(), identities, semanticTypes, depth, ", "_zc);
      append(output, ">"_zc);
    }
    return;
  }
  if (data.is<type::semantic::TypeParameterTypeData>()) {
    appendGenericParameter(output, data.get<type::semantic::TypeParameterTypeData>().parameter,
                           identities);
    return;
  }
  if (data.is<type::semantic::UnionTypeData>()) {
    appendTypeList(output, data.get<type::semantic::UnionTypeData>().alternatives.asPtr(),
                   identities, semanticTypes, depth, " | "_zc);
    return;
  }
  if (data.is<type::semantic::IntersectionTypeData>()) {
    appendTypeList(output, data.get<type::semantic::IntersectionTypeData>().conjuncts.asPtr(),
                   identities, semanticTypes, depth, " & "_zc);
    return;
  }
  if (data.is<type::semantic::ReferenceTypeData>()) {
    const auto& value = data.get<type::semantic::ReferenceTypeData>();
    append(output, value.mutability == type::semantic::Mutability::Mutable ? "&mut "_zc : "&"_zc);
    appendType(output, value.referent, identities, semanticTypes, depth + 1);
    return;
  }
  if (data.is<type::semantic::RawPointerTypeData>()) {
    const auto& value = data.get<type::semantic::RawPointerTypeData>();
    append(output,
           value.mutability == type::semantic::Mutability::Mutable ? "*mut "_zc : "*const "_zc);
    appendType(output, value.pointee, identities, semanticTypes, depth + 1);
    return;
  }
  if (data.is<type::semantic::ExistentialTypeData>()) {
    const auto& value = data.get<type::semantic::ExistentialTypeData>();
    append(output, "dyn "_zc);
    appendDefinition(output, value.principal.definition, identities);
    if (value.principal.arguments.size() != 0) {
      append(output, "<"_zc);
      appendTypeList(output, value.principal.arguments.asPtr(), identities, semanticTypes, depth,
                     ", "_zc);
      append(output, ">"_zc);
    }
    for (const auto& additional : value.additionalInterfaces) {
      append(output, " + "_zc);
      appendDefinition(output, additional.definition, identities);
    }
    for (const auto marker : value.markers) {
      append(output, " + "_zc);
      appendDefinition(output, marker, identities);
    }
    if (value.associatedBindings.size() != 0) {
      append(output, " where "_zc);
      for (size_t index = 0; index < value.associatedBindings.size(); ++index) {
        if (index != 0) append(output, ", "_zc);
        appendDefinition(output, value.associatedBindings[index].associated, identities);
        append(output, " = "_zc);
        appendType(output, value.associatedBindings[index].type, identities, semanticTypes,
                   depth + 1);
      }
    }
    return;
  }
  if (data.is<type::semantic::InterfaceBoundTypeData>()) {
    appendInterface(output, data.get<type::semantic::InterfaceBoundTypeData>().interface,
                    identities, semanticTypes, depth);
    return;
  }
  ZC_IREQUIRE(data.is<type::semantic::InterfaceSelfTypeData>(),
              "Semantic type renderer received no closed branch");
  append(output, "Self("_zc);
  appendDefinition(output, data.get<type::semantic::InterfaceSelfTypeData>().interface, identities);
  append(output, ")"_zc);
}

zc::String renderType(const checked::TypeDisplayArg& argument,
                      const CheckerIdentityAuthority& identities,
                      const type::SemanticTypeStore& semanticTypes) {
  ZC_IF_SOME(alias, argument.sourceAlias) { return escaped(alias.text()); }
  zc::Vector<char> output;
  appendType(output, argument.type, identities, semanticTypes, 0);
  return zc::str(output.releaseAsArray());
}

zc::String renderConstraintReason(checked::ConstraintReasonKind reason) {
  using checked::ConstraintReasonKind;
  switch (reason) {
    case ConstraintReasonKind::Annotation:
      return zc::str("annotation");
    case ConstraintReasonKind::Initializer:
      return zc::str("initializer");
    case ConstraintReasonKind::Argument:
      return zc::str("argument");
    case ConstraintReasonKind::Return:
      return zc::str("return");
    case ConstraintReasonKind::Assignment:
      return zc::str("assignment");
    case ConstraintReasonKind::ConditionalJoin:
      return zc::str("conditional-join");
    case ConstraintReasonKind::Operator:
      return zc::str("operator");
    case ConstraintReasonKind::Projection:
      return zc::str("projection");
    case ConstraintReasonKind::Bound:
      return zc::str("bound");
    case ConstraintReasonKind::Raises:
      return zc::str("raises");
    case ConstraintReasonKind::Pattern:
      return zc::str("pattern");
    case ConstraintReasonKind::Cast:
      return zc::str("cast");
  }
  ZC_UNREACHABLE
}

void appendInteger(zc::Vector<char>& output, const signature::CanonicalInteger& integer) {
  if (integer.sign == signature::IntegerSign::Negative) append(output, "-"_zc);
  if (integer.magnitude.size() == 0) {
    append(output, "0"_zc);
    return;
  }
  zc::Vector<uint8_t> digits;
  digits.add(0);
  for (const auto byte : integer.magnitude) {
    uint16_t carry = byte;
    for (size_t index = 0; index < digits.size(); ++index) {
      const uint16_t value = static_cast<uint16_t>(digits[index] * 256U + carry);
      digits[index] = static_cast<uint8_t>(value % 10U);
      carry = static_cast<uint16_t>(value / 10U);
    }
    while (carry != 0) {
      digits.add(static_cast<uint8_t>(carry % 10U));
      carry = static_cast<uint16_t>(carry / 10U);
    }
  }
  for (size_t index = digits.size(); index != 0; --index) {
    output.add(static_cast<char>('0' + digits[index - 1]));
  }
}

void appendLiteral(zc::Vector<char>& output, const signature::CanonicalConstValue& literal,
                   const CheckerIdentityAuthority& identities, uint32_t depth) {
  if (depth >= MAXIMUM_TYPE_DEPTH) {
    append(output, "..."_zc);
    return;
  }
  ZC_IF_SOME(integer, literal.integerValue()) {
    appendInteger(output, integer);
    return;
  }
  ZC_IF_SOME(floating, literal.floatValue()) {
    if (floating.width == signature::CanonicalFloatWidth::Bits32) {
      const uint32_t bits = static_cast<uint32_t>(floating.bits);
      float value = 0;
      memcpy(&value, &bits, sizeof(value));
      append(output, zc::str(value));
    } else {
      double value = 0;
      memcpy(&value, &floating.bits, sizeof(value));
      append(output, zc::str(value));
    }
    return;
  }
  ZC_IF_SOME(value, literal.booleanValue()) {
    append(output, value ? "true"_zc : "false"_zc);
    return;
  }
  ZC_IF_SOME(value, literal.characterValue()) {
    append(output, "'"_zc);
    diagnostics::appendDiagnosticScalar(output, value, diagnostics::DiagnosticQuote::Single);
    append(output, "'"_zc);
    return;
  }
  ZC_IF_SOME(value, literal.stringValue()) {
    append(output, "\""_zc);
    append(output, diagnostics::escapeDiagnosticText(value, diagnostics::DiagnosticQuote::Double,
                                                     MAXIMUM_DISPLAY_SCALARS));
    append(output, "\""_zc);
    return;
  }
  if (literal.tag() == signature::CanonicalConstValueTag::Null) {
    append(output, "null"_zc);
    return;
  }
  if (literal.tag() == signature::CanonicalConstValueTag::Unit) {
    append(output, "unit"_zc);
    return;
  }
  ZC_IF_SOME(values, literal.elements()) {
    const bool tuple = literal.tag() == signature::CanonicalConstValueTag::Tuple;
    append(output, tuple ? "("_zc : "["_zc);
    const size_t count =
        values.size() < MAXIMUM_DISPLAY_ITEMS ? values.size() : MAXIMUM_DISPLAY_ITEMS;
    for (size_t index = 0; index < count; ++index) {
      if (index != 0) append(output, ", "_zc);
      appendLiteral(output, values[index], identities, depth + 1);
    }
    if (count != values.size()) {
      if (count != 0) append(output, ", "_zc);
      append(output, "..."_zc);
    }
    if (tuple && values.size() == 1) append(output, ","_zc);
    append(output, tuple ? ")"_zc : "]"_zc);
    return;
  }
  ZC_IF_SOME(fields, literal.objectFields()) {
    append(output, "{"_zc);
    const size_t count =
        fields.size() < MAXIMUM_DISPLAY_ITEMS ? fields.size() : MAXIMUM_DISPLAY_ITEMS;
    for (size_t index = 0; index < count; ++index) {
      if (index != 0) append(output, ", "_zc);
      append(output, escaped(fields[index].name.text()));
      append(output, ": "_zc);
      appendLiteral(output, fields[index].value, identities, depth + 1);
    }
    if (count != fields.size()) {
      if (count != 0) append(output, ", "_zc);
      append(output, "..."_zc);
    }
    append(output, "}"_zc);
    return;
  }
  ZC_IF_SOME(enumeration, literal.enumerationValue()) {
    appendDefinition(output, enumeration.variant, identities);
    if (enumeration.payload.size() != 0) {
      append(output, "("_zc);
      const size_t count = enumeration.payload.size() < MAXIMUM_DISPLAY_ITEMS
                               ? enumeration.payload.size()
                               : MAXIMUM_DISPLAY_ITEMS;
      for (size_t index = 0; index < count; ++index) {
        if (index != 0) append(output, ", "_zc);
        appendLiteral(output, enumeration.payload[index], identities, depth + 1);
      }
      if (count != enumeration.payload.size()) {
        if (count != 0) append(output, ", "_zc);
        append(output, "..."_zc);
      }
      append(output, ")"_zc);
    }
    return;
  }
  ZC_UNREACHABLE
}

void appendPattern(zc::Vector<char>& output, const checked::PatternConstructor& pattern,
                   const CheckerIdentityAuthority& identities,
                   const type::SemanticTypeStore& semanticTypes) {
  const auto& value = pattern.variant();
  if (value.is<checked::WildcardPattern>()) {
    append(output, "_"_zc);
    return;
  }
  if (value.is<checked::LiteralPattern>()) {
    appendLiteral(output, value.get<checked::LiteralPattern>().value, identities, 0);
    return;
  }
  if (value.is<checked::TuplePattern>()) {
    append(output, "(..arity="_zc);
    append(output, zc::str(value.get<checked::TuplePattern>().arity));
    append(output, ")"_zc);
    return;
  }
  if (value.is<checked::ObjectPattern>()) {
    append(output, "{"_zc);
    const auto& fields = value.get<checked::ObjectPattern>().fields;
    const size_t count =
        fields.size() < MAXIMUM_DISPLAY_ITEMS ? fields.size() : MAXIMUM_DISPLAY_ITEMS;
    for (size_t index = 0; index < count; ++index) {
      if (index != 0) append(output, ","_zc);
      append(output, escaped(fields[index].text()));
    }
    if (count != fields.size()) {
      if (count != 0) append(output, ","_zc);
      append(output, "..."_zc);
    }
    append(output, "}"_zc);
    return;
  }
  if (value.is<checked::UnionAlternativePattern>()) {
    const auto& alternative = value.get<checked::UnionAlternativePattern>();
    append(output, "union["_zc);
    append(output, zc::str(alternative.index));
    append(output, "]: "_zc);
    appendType(output, alternative.type, identities, semanticTypes, 0);
    return;
  }
  if (value.is<checked::EnumVariantPattern>()) {
    appendDefinition(output, value.get<checked::EnumVariantPattern>().variant, identities);
    return;
  }
  ZC_IREQUIRE(value.is<checked::NominalPattern>(), "Pattern renderer received no closed branch");
  appendDefinition(output, value.get<checked::NominalPattern>().definition, identities);
}

zc::String renderDisplayArgument(const checked::CheckerDisplayArgument& argument,
                                 const CheckerIdentityAuthority& identities,
                                 const type::SemanticTypeStore& semanticTypes) {
  const auto& value = argument.variant();
  if (value.is<checked::TypeDisplayArg>()) {
    return renderType(value.get<checked::TypeDisplayArg>(), identities, semanticTypes);
  }
  if (value.is<checked::PrimitiveTypeDisplayArg>()) {
    return renderPrimitive(value.get<checked::PrimitiveTypeDisplayArg>().kind);
  }
  if (value.is<checked::DefinitionDisplayArg>()) {
    zc::Vector<char> output;
    appendDefinition(output, value.get<checked::DefinitionDisplayArg>().definition, identities);
    return zc::str(output.releaseAsArray());
  }
  if (value.is<checked::IdentifierDisplayArg>()) {
    return zc::str("`",
                   escaped(value.get<checked::IdentifierDisplayArg>().identifier.text(),
                           diagnostics::DiagnosticQuote::Backtick),
                   "`");
  }
  if (value.is<checked::CountDisplayArg>()) {
    return zc::str(value.get<checked::CountDisplayArg>().count);
  }
  if (value.is<checked::ConstraintContextDisplayArg>()) {
    return renderConstraintReason(value.get<checked::ConstraintContextDisplayArg>().reason);
  }
  if (value.is<checked::OperatorDisplayArg>()) {
    auto spelling = renderOperatorKind(value.get<checked::OperatorDisplayArg>().operation);
    ZC_IF_SOME(text, spelling) { return zc::str(text); }
    return zc::str("<operator>");
  }
  if (value.is<checked::LiteralDisplayArg>()) {
    zc::Vector<char> output;
    appendLiteral(output, value.get<checked::LiteralDisplayArg>().literal, identities, 0);
    return zc::str(output.releaseAsArray());
  }
  const auto& patterns = value.get<checked::PatternsDisplayArg>().patterns;
  zc::Vector<char> output;
  const size_t count =
      patterns.size() < MAXIMUM_DISPLAY_ITEMS ? patterns.size() : MAXIMUM_DISPLAY_ITEMS;
  for (size_t index = 0; index < count; ++index) {
    if (index != 0) append(output, ", "_zc);
    appendPattern(output, patterns[index], identities, semanticTypes);
  }
  if (count != patterns.size()) {
    if (count != 0) append(output, ", "_zc);
    append(output, "..."_zc);
  }
  return zc::str(output.releaseAsArray());
}

bool appendProjection(SemanticDiagnosticFactBatch& batch,
                      diagnostics::SemanticDiagnosticProjectionResult&& result) {
  if (!result.is<diagnostics::SemanticDiagnosticFactProjection>()) return false;
  auto projected = zc::mv(result).get<diagnostics::SemanticDiagnosticFactProjection>();
  batch.facts.add(zc::mv(projected.fact));
  for (auto& entry : projected.provenance) { batch.provenance.add(zc::mv(entry)); }
  return true;
}

zc::Maybe<zc::Array<uint8_t>> checkerOwner(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    const CheckerIdentityAuthority& identities, uint32_t ownerSchemaPreorder) {
  zc::Maybe<ast::NodeId> ownerNode;
  uint32_t preorder = 0;
  ast::visitTreePreOrder(boundModule.tree(), boundModule.tree().root(),
                         [&](ast::NodeId node, const ast::Node&) {
                           if (preorder == ownerSchemaPreorder) ownerNode = node;
                           ++preorder;
                         });
  if (ownerNode != zc::none) {
    ZC_IF_SOME(node, ownerNode) {
      auto definition = boundModule.definitions().definitionAt(node);
      if (definition != zc::none) {
        ZC_IF_SOME(handle, definition) {
          auto entry = identities.definition(handle);
          if (entry == zc::none) return zc::none;
          ZC_IF_SOME(value, entry) { return value.key().encode(); }
        }
      }
    }
  }
  auto module = identities.module(boundModule.module());
  if (module == zc::none) return zc::none;
  ZC_IF_SOME(value, module) { return value.key().encode(); }
  return zc::none;
}

zc::Vector<uint32_t> checkerEventPath(const checked::CheckerFailureRef& failure) {
  zc::Vector<uint32_t> path;
  path.add(failure.emitterOrdinal.stageTag);
  path.add(static_cast<uint32_t>(failure.producer));
  path.add(failure.emitterOrdinal.ownerSchemaPreorder);
  path.add(failure.emitterOrdinal.siteSchemaPreorder);
  path.add(failure.emitterOrdinal.itemOrdinal);
  path.add(failure.primaryNode.value);
  return path;
}

zc::Vector<uint32_t> signatureEventPath(uint32_t category, uint32_t diagnostic,
                                        const signature::SignatureEmitterOrdinal& ordinal,
                                        ast::NodeId primaryNode) {
  zc::Vector<uint32_t> path;
  path.add(category);
  path.add(diagnostic);
  path.add(ordinal.ownerSchemaPreorder);
  path.add(ordinal.siteSchemaPreorder);
  path.add(ordinal.itemOrdinal);
  path.add(primaryNode.value);
  return path;
}

zc::Maybe<zc::String> renderSignatureArgument(const signature::SignatureSourceArgument& argument,
                                              const CheckerIdentityAuthority& identities,
                                              const type::SemanticTypeStore& semanticTypes) {
  if (argument.variant().is<signature::SignatureLiteralDisplayArg>()) {
    checked::CheckerDisplayArgument converted(checked::LiteralDisplayArg{
        argument.variant().get<signature::SignatureLiteralDisplayArg>().literal.clone()});
    return renderDisplayArgument(converted, identities, semanticTypes);
  }
  if (argument.variant().is<signature::SignaturePrimitiveTypeDisplayArg>()) {
    checked::CheckerDisplayArgument converted(checked::PrimitiveTypeDisplayArg{
        argument.variant().get<signature::SignaturePrimitiveTypeDisplayArg>().kind});
    return renderDisplayArgument(converted, identities, semanticTypes);
  }
  if (argument.variant().is<signature::SignatureOperatorDisplayArg>()) {
    checked::CheckerDisplayArgument converted(checked::OperatorDisplayArg{checker::OperatorKind(
        argument.variant().get<signature::SignatureOperatorDisplayArg>().operation)});
    return renderDisplayArgument(converted, identities, semanticTypes);
  }
  return zc::none;
}

void appendDigestPath(zc::Vector<uint32_t>& path, zc::ArrayPtr<const uint8_t> bytes) {
  path.add(static_cast<uint32_t>(bytes.size()));
  for (size_t offset = 0; offset < bytes.size(); offset += 4) {
    uint32_t word = 0;
    const size_t remaining = bytes.size() - offset;
    const size_t count = remaining < 4 ? remaining : 4;
    for (size_t index = 0; index < count; ++index) {
      word |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8);
    }
    path.add(word);
  }
}

}  // namespace

SemanticDiagnosticBatchProjectionResult projectSignatureSourceDiagnostics(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    const CheckerIdentityAuthority& identities, const type::SemanticTypeStore& semanticTypes,
    const signature::SignatureFactsSourceRejected& rejected) {
  auto module = identities.module(boundModule.module());
  if (module == zc::none) return SemanticDiagnosticBatchProjectionFailure::InvalidModuleIdentity;
  SemanticDiagnosticFactBatch batch;
  for (const auto& failure : rejected.failures) {
    auto owner = checkerOwner(boundModule, identities, failure.emitterOrdinal.ownerSchemaPreorder);
    if (owner == zc::none) return SemanticDiagnosticBatchProjectionFailure::InvalidOwnerIdentity;
    zc::Vector<zc::String> arguments(failure.arguments.size());
    for (const auto& argument : failure.arguments) {
      auto rendered = renderSignatureArgument(argument, identities, semanticTypes);
      if (rendered == zc::none) {
        return SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
      }
      arguments.add(zc::mv(ZC_ASSERT_NONNULL(rendered)));
    }
    if ((failure.diagnostic == signature::SignatureSourceDiagnostic::ConflictingImpl ||
         failure.diagnostic == signature::SignatureSourceDiagnostic::OrphanImpl) &&
        arguments.size() == 0) {
      arguments.add(zc::str("interface"));
      arguments.add(zc::str("type"));
    }
    auto path = signatureEventPath(0x01, static_cast<uint32_t>(failure.diagnostic),
                                   failure.emitterOrdinal, failure.primaryNode);
    zc::Vector<diagnostics::SemanticDiagnosticRelatedSite> related;
    ZC_IF_SOME(moduleEntry, module) {
      if (!appendProjection(
              batch, diagnostics::projectSemanticDiagnosticFact(
                         moduleEntry.key(), diagnostics::SemanticDiagnosticDomain::Signature,
                         ZC_ASSERT_NONNULL(owner).asPtr(), path.asPtr(),
                         static_cast<diagnostics::DiagID>(failure.diagnostic), zc::mv(arguments),
                         failure.primarySpan, zc::mv(related)))) {
        return SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
      }
    }
  }
  auto advisories =
      projectSignatureAdvisories(boundModule.module(), identities, rejected.advisories.asPtr());
  if (!advisories.is<SemanticDiagnosticFactBatch>()) {
    return SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  auto advisoryBatch = zc::mv(advisories).get<SemanticDiagnosticFactBatch>();
  for (auto& fact : advisoryBatch.facts) { batch.facts.add(zc::mv(fact)); }
  for (auto& provenance : advisoryBatch.provenance) { batch.provenance.add(zc::mv(provenance)); }
  return batch;
}

SemanticDiagnosticBatchProjectionResult projectSignatureAdvisories(
    identity::ModuleId moduleId, const CheckerIdentityAuthority& identities,
    zc::ArrayPtr<const signature::SignatureAdvisoryRef> advisories) {
  auto module = identities.module(moduleId);
  if (module == zc::none) return SemanticDiagnosticBatchProjectionFailure::InvalidModuleIdentity;
  SemanticDiagnosticFactBatch batch;
  ZC_IF_SOME(moduleEntry, module) {
    auto owner = moduleEntry.key().encode();
    for (const auto& advisory : advisories) {
      auto path = signatureEventPath(0x02, static_cast<uint32_t>(advisory.diagnostic),
                                     advisory.emitterOrdinal, advisory.primaryNode);
      zc::Vector<zc::String> arguments;
      zc::Vector<diagnostics::SemanticDiagnosticRelatedSite> related;
      if (!appendProjection(batch,
                            diagnostics::projectSemanticDiagnosticFact(
                                moduleEntry.key(), diagnostics::SemanticDiagnosticDomain::Signature,
                                owner.asPtr(), path.asPtr(), advisory.diagnostic, zc::mv(arguments),
                                advisory.primarySpan, zc::mv(related)))) {
        return SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
      }
    }
  }
  return batch;
}

SemanticDiagnosticBatchProjectionResult projectCheckedFactsSourceFailures(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    const CheckerIdentityAuthority& identities, const type::SemanticTypeStore& semanticTypes,
    zc::ArrayPtr<const checked::CheckerFailureRef> failures) {
  auto module = identities.module(boundModule.module());
  if (module == zc::none) return SemanticDiagnosticBatchProjectionFailure::InvalidModuleIdentity;
  SemanticDiagnosticFactBatch batch;
  for (const auto& failure : failures) {
    auto owner = checkerOwner(boundModule, identities, failure.emitterOrdinal.ownerSchemaPreorder);
    if (owner == zc::none) return SemanticDiagnosticBatchProjectionFailure::InvalidOwnerIdentity;
    zc::Vector<zc::String> arguments(failure.arguments.size());
    for (const auto& argument : failure.arguments) {
      arguments.add(renderDisplayArgument(argument, identities, semanticTypes));
    }
    zc::Vector<diagnostics::SemanticDiagnosticRelatedSite> related(failure.notes.size());
    for (const auto& note : failure.notes) {
      zc::Vector<zc::String> noteArguments(note.arguments.size());
      for (const auto& argument : note.arguments) {
        noteArguments.add(renderDisplayArgument(argument, identities, semanticTypes));
      }
      related.add(diagnostics::SemanticDiagnosticRelatedSite{
          diagnostics::DiagnosticSecondaryRole::Note, note.diagnostic.diagnosticId(),
          note.span.clone(), zc::mv(noteArguments)});
    }
    ZC_IF_SOME(moduleEntry, module) {
      auto path = checkerEventPath(failure);
      if (!appendProjection(
              batch,
              diagnostics::projectSemanticDiagnosticFact(
                  moduleEntry.key(), diagnostics::SemanticDiagnosticDomain::Checker,
                  ZC_ASSERT_NONNULL(owner).asPtr(), path.asPtr(), failure.diagnostic.diagnosticId(),
                  zc::mv(arguments), failure.primarySpan, zc::mv(related)))) {
        return SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
      }
    }
  }
  return batch;
}

SemanticDiagnosticBatchProjectionResult projectCoherenceSourceFailure(
    const CheckerIdentityAuthority& identities, const type::SemanticTypeStore& semanticTypes,
    const coherence::CoherenceFailureRef& failure) {
  auto implementation = identities.implementation(failure.primaryImpl);
  if (implementation == zc::none)
    return SemanticDiagnosticBatchProjectionFailure::InvalidOwnerIdentity;
  zc::Vector<zc::String> arguments(failure.arguments.size());
  for (const auto& argument : failure.arguments) {
    arguments.add(renderDisplayArgument(argument, identities, semanticTypes));
  }
  zc::Vector<diagnostics::SemanticDiagnosticRelatedSite> related(failure.notes.size());
  for (const auto& note : failure.notes) {
    zc::Vector<zc::String> noteArguments(note.arguments.size());
    for (const auto& argument : note.arguments) {
      noteArguments.add(renderDisplayArgument(argument, identities, semanticTypes));
    }
    related.add(diagnostics::SemanticDiagnosticRelatedSite{
        diagnostics::DiagnosticSecondaryRole::Note, note.diagnostic.diagnosticId(),
        note.span.clone(), zc::mv(noteArguments)});
  }
  ZC_IF_SOME(entry, implementation) {
    auto module = identities.module(entry.record().module());
    if (module == zc::none) return SemanticDiagnosticBatchProjectionFailure::InvalidModuleIdentity;
    auto owner = entry.key().encode();
    zc::Vector<uint32_t> path;
    path.add(static_cast<uint32_t>(failure.producer));
    path.add(static_cast<uint32_t>(failure.diagnostic.diagnosticId()));
    if (failure.relatedImpl == zc::none) {
      path.add(0);
    } else {
      ZC_IF_SOME(relatedImpl, failure.relatedImpl) {
        auto relatedEntry = identities.implementation(relatedImpl);
        if (relatedEntry == zc::none) {
          return SemanticDiagnosticBatchProjectionFailure::InvalidOwnerIdentity;
        }
        ZC_IF_SOME(value, relatedEntry) { appendDigestPath(path, value.key().bytes()); }
      }
    }
    SemanticDiagnosticFactBatch batch;
    ZC_IF_SOME(moduleEntry, module) {
      if (!appendProjection(batch,
                            diagnostics::projectSemanticDiagnosticFact(
                                moduleEntry.key(), diagnostics::SemanticDiagnosticDomain::Coherence,
                                owner.asPtr(), path.asPtr(), failure.diagnostic.diagnosticId(),
                                zc::mv(arguments), failure.primarySpan, zc::mv(related)))) {
        return SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
      }
    }
    return batch;
  }
  return SemanticDiagnosticBatchProjectionFailure::InvalidOwnerIdentity;
}

zc::String renderCheckerDisplayArgument(const checked::CheckerDisplayArgument& argument,
                                        const CheckerIdentityAuthority& identities,
                                        const type::SemanticTypeStore& semanticTypes) {
  return renderDisplayArgument(argument, identities, semanticTypes);
}

}  // namespace zomlang::compiler::checker
