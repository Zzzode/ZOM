// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/scalar-literal-facts.h"

#include <cstdint>
#include <cstring>

#include "zc/core/exception.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/type/semantic-type-data.h"

namespace zomlang::compiler::checker::scalar_literal {
namespace {

using checked::CheckedFactGroup;

checked::CheckedFactsInvariantRejected rejectInvariant(
    signature::CheckerInvariantKind kind, identity::ModuleId module, uint32_t ordinal,
    zc::Maybe<identity::DefId>&& owner = zc::none, zc::Maybe<ast::NodeId>&& node = zc::none,
    zc::Maybe<identity::SourceSpan>&& span = zc::none,
    zc::Vector<uint32_t>&& structuralFieldPath = zc::Vector<uint32_t>()) {
  zc::Maybe<identity::Sha256Digest> noExpected;
  zc::Maybe<identity::Sha256Digest> noActual;
  zc::Vector<signature::CheckerVerificationFailure> failures;
  failures.add(signature::CheckerVerificationFailure(signature::CheckerInvariantFact{
      kind, signature::CheckerInvariantStage::Body, module, zc::mv(owner), zc::mv(node),
      zc::mv(span), zc::mv(structuralFieldPath), zc::mv(noExpected), zc::mv(noActual), ordinal}));
  return checked::CheckedFactsInvariantRejected{zc::mv(failures)};
}

checked::CheckedFactsInvariantRejected rejectIdentity(identity::IdentityInvariant&& invariant) {
  zc::Vector<signature::CheckerVerificationFailure> failures;
  failures.add(signature::CheckerVerificationFailure(zc::mv(invariant)));
  return checked::CheckedFactsInvariantRejected{zc::mv(failures)};
}

FactEmissionResult rejectLiteralOutOfRange(const FactEmissionInput& input,
                                           checked::CanonicalLiteral&& literal,
                                           type::semantic::PrimitiveKind target,
                                           uint32_t actualPreorder) {
  zc::Maybe<checked::TypeErrorId> noRecovery;
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  arguments.add(checked::CheckerDisplayArgument(checked::LiteralDisplayArg{zc::mv(literal)}));
  arguments.add(checked::CheckerDisplayArgument(checked::PrimitiveTypeDisplayArg{target}));
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Vector<checked::CheckerFailureRef> failures;
  failures.add(checked::CheckerFailureRef{
      checked::CheckerErrorId::BodyLiteralOutOfRange(), checked::CheckerDiagnosticStage::Body,
      input.node, input.checkedNode.sourceSpan.clone(), zc::mv(arguments), zc::mv(notes),
      checked::CheckerDiagnosticProducer::Constant,
      checked::CheckerRecoveryPolicy(
          checked::CreateRootRecoveryPolicy{checked::CheckerRecoveryClass::FailedInference, true}),
      checked::CheckerEmitterOrdinal{static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body), 0,
                                     actualPreorder, 0},
      zc::mv(noRecovery)});
  return checked::CheckedFactsSourceRejected{zc::mv(failures),
                                             zc::Vector<checked::CheckerAdvisoryRef>(),
                                             zc::Vector<checked::FrozenRecoveryLedger>()};
}

zc::Vector<uint32_t> factPath(CheckedFactGroup group) {
  zc::Vector<uint32_t> path;
  path.add(static_cast<uint32_t>(group));
  return path;
}

bool payloadHasOnlyWords(const ast::Node& syntax, uint32_t wordCount) noexcept {
  for (uint32_t index = wordCount; index < ast::kNodePayloadWordCount; ++index) {
    if (syntax.payload.words[index] != 0) return false;
  }
  return true;
}

zc::Maybe<zc::StringPtr> bigIntText(const ast::Tree& tree, uint32_t rawId) {
  if (rawId == 0) return zc::none;
  try {
    return tree.bigInt(ast::BigIntId(rawId));
  } catch (const zc::Exception&) { return zc::none; }
}

zc::Maybe<zc::StringPtr> floatText(const ast::Tree& tree, uint32_t rawId) {
  if (rawId == 0) return zc::none;
  try {
    return tree.floatLiteral(ast::FloatId(rawId));
  } catch (const zc::Exception&) { return zc::none; }
}

zc::Maybe<zc::StringPtr> stringText(const ast::Tree& tree, uint32_t rawId) {
  if (rawId == 0) return ""_zcc;
  try {
    return tree.string(ast::StringId(rawId));
  } catch (const zc::Exception&) { return zc::none; }
}

uint8_t digitValue(char value) noexcept {
  if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f') return static_cast<uint8_t>(value - 'a' + 10);
  if (value >= 'A' && value <= 'F') return static_cast<uint8_t>(value - 'A' + 10);
  return 0xff;
}

struct ParsedIntegerLiteral final {
  signature::CanonicalInteger value;
  type::semantic::PrimitiveKind primitive;
};

struct MalformedIntegerLiteral final {};
struct OutOfRangeIntegerLiteral final {
  signature::CanonicalInteger value;
};
using IntegerLiteralParseResult =
    zc::OneOf<ParsedIntegerLiteral, MalformedIntegerLiteral, OutOfRangeIntegerLiteral>;

bool magnitudeFits(zc::ArrayPtr<const uint8_t> magnitude,
                   zc::ArrayPtr<const uint8_t> maximum) noexcept {
  if (magnitude.size() != maximum.size()) return magnitude.size() < maximum.size();
  for (size_t index = 0; index < magnitude.size(); ++index) {
    if (magnitude[index] != maximum[index]) return magnitude[index] < maximum[index];
  }
  return true;
}

IntegerLiteralParseResult parseIntegerLiteral(zc::StringPtr text, uint8_t base,
                                              bool requiresBigIntSuffix) {
  if (base != 2 && base != 8 && base != 10 && base != 16) return MalformedIntegerLiteral{};
  size_t end = text.size();
  if (requiresBigIntSuffix) {
    if (end == 0 || text[end - 1] != 'n') return MalformedIntegerLiteral{};
    --end;
  } else if (end != 0 && text[end - 1] == 'n') {
    return MalformedIntegerLiteral{};
  }

  size_t start = 0;
  if (end >= 2 && text[0] == '0') {
    uint8_t prefixBase = 0;
    if (text[1] == 'b' || text[1] == 'B') prefixBase = 2;
    if (text[1] == 'o' || text[1] == 'O') prefixBase = 8;
    if (text[1] == 'x' || text[1] == 'X') prefixBase = 16;
    if (prefixBase != 0) {
      if (prefixBase != base) return MalformedIntegerLiteral{};
      start = 2;
    }
  }
  if (start == end) return MalformedIntegerLiteral{};

  zc::Vector<uint8_t> littleEndian;
  for (size_t index = start; index < end; ++index) {
    const uint8_t digit = digitValue(text[index]);
    if (digit >= base) return MalformedIntegerLiteral{};
    uint32_t carry = digit;
    for (size_t byteIndex = 0; byteIndex < littleEndian.size(); ++byteIndex) {
      const uint32_t expanded = static_cast<uint32_t>(littleEndian[byteIndex]) * base + carry;
      littleEndian[byteIndex] = static_cast<uint8_t>(expanded & 0xff);
      carry = expanded >> 8;
    }
    while (carry != 0) {
      littleEndian.add(static_cast<uint8_t>(carry & 0xff));
      carry >>= 8;
    }
  }
  while (!littleEndian.empty() && littleEndian.back() == 0) littleEndian.removeLast();

  auto magnitude = zc::heapArray<uint8_t>(littleEndian.size());
  for (size_t index = 0; index < littleEndian.size(); ++index) {
    magnitude[index] = littleEndian[littleEndian.size() - index - 1];
  }

  constexpr uint8_t maxI32Bytes[] = {0x7f, 0xff, 0xff, 0xff};
  constexpr uint8_t maxI64Bytes[] = {0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  constexpr uint8_t maxU64Bytes[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  type::semantic::PrimitiveKind primitive;
  if (magnitudeFits(magnitude.asPtr(), zc::arrayPtr(maxI32Bytes))) {
    primitive = type::semantic::PrimitiveKind::I32;
  } else if (magnitudeFits(magnitude.asPtr(), zc::arrayPtr(maxI64Bytes))) {
    primitive = type::semantic::PrimitiveKind::I64;
  } else if (magnitudeFits(magnitude.asPtr(), zc::arrayPtr(maxU64Bytes))) {
    primitive = type::semantic::PrimitiveKind::U64;
  } else {
    return OutOfRangeIntegerLiteral{
        signature::CanonicalInteger{signature::IntegerSign::NonNegative, zc::mv(magnitude)}};
  }
  return ParsedIntegerLiteral{
      signature::CanonicalInteger{signature::IntegerSign::NonNegative, zc::mv(magnitude)},
      primitive};
}

bool hasValidDecimalFloatSyntax(zc::StringPtr text) noexcept {
  if (text.size() == 0) return false;
  size_t index = 0;
  while (index < text.size() && text[index] >= '0' && text[index] <= '9') ++index;
  if (index == 0) return false;
  if (index < text.size() && text[index] == '.') {
    ++index;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') ++index;
  }
  if (index < text.size() && (text[index] == 'e' || text[index] == 'E')) {
    ++index;
    if (index < text.size() && (text[index] == '+' || text[index] == '-')) ++index;
    const size_t exponentStart = index;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') ++index;
    if (index == exponentStart) return false;
  }
  return index == text.size();
}

bool hasNonZeroSignificand(zc::StringPtr text) noexcept {
  for (const char value : text) {
    if (value == 'e' || value == 'E') return false;
    if (value >= '1' && value <= '9') return true;
  }
  return false;
}

bool isValidUtf8(zc::StringPtr text) noexcept {
  size_t index = 0;
  while (index < text.size()) {
    const uint8_t first = static_cast<uint8_t>(text[index]);
    if (first <= 0x7f) {
      ++index;
      continue;
    }
    size_t continuationCount = 0;
    uint8_t secondMinimum = 0x80;
    uint8_t secondMaximum = 0xbf;
    if (first >= 0xc2 && first <= 0xdf) {
      continuationCount = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      continuationCount = 2;
      if (first == 0xe0) secondMinimum = 0xa0;
      if (first == 0xed) secondMaximum = 0x9f;
    } else if (first >= 0xf0 && first <= 0xf4) {
      continuationCount = 3;
      if (first == 0xf0) secondMinimum = 0x90;
      if (first == 0xf4) secondMaximum = 0x8f;
    } else {
      return false;
    }
    if (index + continuationCount >= text.size()) return false;
    const uint8_t second = static_cast<uint8_t>(text[index + 1]);
    if (second < secondMinimum || second > secondMaximum) return false;
    for (size_t offset = 2; offset <= continuationCount; ++offset) {
      const uint8_t continuation = static_cast<uint8_t>(text[index + offset]);
      if (continuation < 0x80 || continuation > 0xbf) return false;
    }
    index += continuationCount + 1;
  }
  return true;
}

zc::Maybe<uint32_t> singleUtf8Scalar(zc::StringPtr text) noexcept {
  if (text.size() == 0 || !isValidUtf8(text)) return zc::none;
  const uint8_t first = static_cast<uint8_t>(text[0]);
  if (first <= 0x7f) {
    if (text.size() != 1) return zc::none;
    return first;
  }
  if (first <= 0xdf) {
    if (text.size() != 2) return zc::none;
    return static_cast<uint32_t>((first & 0x1f) << 6) |
           static_cast<uint32_t>(static_cast<uint8_t>(text[1]) & 0x3f);
  }
  if (first <= 0xef) {
    if (text.size() != 3) return zc::none;
    return static_cast<uint32_t>((first & 0x0f) << 12) |
           static_cast<uint32_t>((static_cast<uint8_t>(text[1]) & 0x3f) << 6) |
           static_cast<uint32_t>(static_cast<uint8_t>(text[2]) & 0x3f);
  }
  if (text.size() != 4) return zc::none;
  return static_cast<uint32_t>((first & 0x07) << 18) |
         static_cast<uint32_t>((static_cast<uint8_t>(text[1]) & 0x3f) << 12) |
         static_cast<uint32_t>((static_cast<uint8_t>(text[2]) & 0x3f) << 6) |
         static_cast<uint32_t>(static_cast<uint8_t>(text[3]) & 0x3f);
}

}  // namespace

FactEmissionResult FactEmitter::emit(const FactEmissionInput& input) {
  if (!input.semanticContext.isValid() ||
      input.identities.semanticContext() != input.semanticContext ||
      input.semanticTypes.context() != input.semanticContext || !input.tree.contains(input.node) ||
      input.identities.module(input.module) == zc::none) {
    return rejectInvariant(signature::CheckerInvariantKind::InputReceiptMismatch, input.module, 0,
                           zc::none, input.node);
  }
  const auto& syntax = input.tree.node(input.node);
  uint32_t preorder = 0;
  uint32_t actualPreorder = 0;
  bool found = false;
  ast::visitTreePreOrder(input.tree, input.tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (node == input.node) {
      actualPreorder = preorder;
      found = true;
    }
    ++preorder;
  });
  auto moduleEntry = input.identities.module(input.module);
  bool sourceMatches = false;
  ZC_IF_SOME(value, moduleEntry) {
    sourceMatches = input.identities.sourceFile(input.source) != zc::none &&
                    input.source.belongsTo(value.key().crate()) &&
                    input.checkedNode.sourceSpan.belongsTo(input.source);
  }
  if (!found || input.checkedNode.syntaxKind != static_cast<uint32_t>(syntax.kind) ||
      input.checkedNode.schemaPreorder != actualPreorder || !sourceMatches) {
    return rejectInvariant(signature::CheckerInvariantKind::InputReceiptMismatch, input.module,
                           actualPreorder, zc::none, input.node,
                           input.checkedNode.sourceSpan.clone());
  }

  type::semantic::PrimitiveKind primitive = type::semantic::PrimitiveKind::Unit;
  checked::CanonicalLiteral literal = checked::CanonicalLiteral::unit();
  if (syntax.kind == ast::SyntaxKind::NullLiteral) {
    if (!payloadHasOnlyWords(syntax, ast::kNullLiteralPayloadWordCount)) {
      return rejectInvariant(
          signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
          input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
    }
    primitive = type::semantic::PrimitiveKind::Null;
    literal = checked::CanonicalLiteral::null();
  } else if (syntax.kind == ast::SyntaxKind::BoolLiteral) {
    if (!payloadHasOnlyWords(syntax, ast::kBoolLiteralPayloadWordCount) ||
        syntax.payload.words[ast::kBoolLiteralValueWord] > 1) {
      return rejectInvariant(
          signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
          input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
    }
    primitive = type::semantic::PrimitiveKind::Bool;
    literal =
        checked::CanonicalLiteral::boolean(syntax.payload.words[ast::kBoolLiteralValueWord] != 0);
  } else if (syntax.kind == ast::SyntaxKind::IntLiteral ||
             syntax.kind == ast::SyntaxKind::BigIntLiteral) {
    const bool isBigInt = syntax.kind == ast::SyntaxKind::BigIntLiteral;
    const uint32_t valueWord = isBigInt ? ast::kBigIntLiteralValueWord : ast::kIntLiteralValueWord;
    const uint32_t wordCount =
        isBigInt ? ast::kBigIntLiteralPayloadWordCount : ast::kIntLiteralPayloadWordCount;
    uint8_t base = 10;
    if (!isBigInt) {
      const uint32_t rawBase = syntax.payload.words[ast::kIntLiteralBaseWord];
      if (rawBase > 0xff) {
        return rejectInvariant(
            signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
            input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
      }
      base = static_cast<uint8_t>(rawBase);
    }
    if (!payloadHasOnlyWords(syntax, wordCount)) {
      return rejectInvariant(
          signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
          input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
    }
    auto text = bigIntText(input.tree, syntax.payload.words[valueWord]);
    if (text == zc::none) {
      return rejectInvariant(
          signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
          input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
    }
    zc::Maybe<IntegerLiteralParseResult> parsed;
    ZC_IF_SOME(value, text) {
      if (isBigInt && value.size() >= 3 && value[0] == '0') {
        if (value[1] == 'b' || value[1] == 'B') base = 2;
        if (value[1] == 'o' || value[1] == 'O') base = 8;
        if (value[1] == 'x' || value[1] == 'X') base = 16;
      }
      parsed = parseIntegerLiteral(value, base, isBigInt);
    }
    if (parsed == zc::none) {
      return rejectInvariant(
          signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
          input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
    }
    ZC_IF_SOME(value, parsed) {
      if (value.is<OutOfRangeIntegerLiteral>()) {
        auto outOfRange = zc::mv(value).get<OutOfRangeIntegerLiteral>();
        return rejectLiteralOutOfRange(input,
                                       checked::CanonicalLiteral::integer(zc::mv(outOfRange.value)),
                                       type::semantic::PrimitiveKind::U64, actualPreorder);
      }
      if (value.is<MalformedIntegerLiteral>()) {
        return rejectInvariant(
            signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
            input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
      }
      auto accepted = zc::mv(value).get<ParsedIntegerLiteral>();
      primitive = accepted.primitive;
      literal = checked::CanonicalLiteral::integer(zc::mv(accepted.value));
    }
  } else if (syntax.kind == ast::SyntaxKind::FloatLiteralExpr) {
    const uint32_t width = syntax.payload.words[ast::kFloatLiteralExprWidthWord];
    if (!payloadHasOnlyWords(syntax, ast::kFloatLiteralExprPayloadWordCount) ||
        (width != 32 && width != 64)) {
      return rejectInvariant(
          signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
          input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
    }
    auto text = floatText(input.tree, syntax.payload.words[ast::kFloatLiteralExprValueWord]);
    if (text == zc::none) {
      return rejectInvariant(
          signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
          input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
    }
    bool accepted = false;
    zc::Maybe<checked::CanonicalLiteral> outOfRangeLiteral;
    ZC_IF_SOME(value, text) {
      if (hasValidDecimalFloatSyntax(value)) {
        if (width == 32) {
          ZC_IF_SOME(parsed, value.tryParseAs<float>()) {
            static_assert(sizeof(float) == sizeof(uint32_t));
            uint32_t bits = 0;
            memcpy(&bits, &parsed, sizeof(bits));
            if (!zc::isNaN(parsed) && parsed != zc::inf() && parsed != -zc::inf() &&
                (parsed != 0.0f || !hasNonZeroSignificand(value))) {
              primitive = type::semantic::PrimitiveKind::F32;
              literal = checked::CanonicalLiteral::float32(bits);
              accepted = true;
            } else {
              outOfRangeLiteral = checked::CanonicalLiteral::float32(bits);
            }
          }
        } else {
          ZC_IF_SOME(parsed, value.tryParseAs<double>()) {
            static_assert(sizeof(double) == sizeof(uint64_t));
            uint64_t bits = 0;
            memcpy(&bits, &parsed, sizeof(bits));
            if (!zc::isNaN(parsed) && parsed != zc::inf() && parsed != -zc::inf() &&
                (parsed != 0.0 || !hasNonZeroSignificand(value))) {
              primitive = type::semantic::PrimitiveKind::F64;
              literal = checked::CanonicalLiteral::float64(bits);
              accepted = true;
            } else {
              outOfRangeLiteral = checked::CanonicalLiteral::float64(bits);
            }
          }
        }
      }
    }
    if (!accepted) {
      ZC_IF_SOME(rejected, outOfRangeLiteral) {
        return rejectLiteralOutOfRange(
            input, zc::mv(rejected),
            width == 32 ? type::semantic::PrimitiveKind::F32 : type::semantic::PrimitiveKind::F64,
            actualPreorder);
      }
      return rejectInvariant(
          signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
          input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
    }
  } else if (syntax.kind == ast::SyntaxKind::StringLiteralExpr ||
             syntax.kind == ast::SyntaxKind::NoSubstitutionTemplateLiteralExpr) {
    const uint32_t wordCount = syntax.kind == ast::SyntaxKind::StringLiteralExpr
                                   ? ast::kStringLiteralExprPayloadWordCount
                                   : ast::kNoSubstitutionTemplateLiteralExprPayloadWordCount;
    const uint32_t valueWord = syntax.kind == ast::SyntaxKind::StringLiteralExpr
                                   ? ast::kStringLiteralExprValueWord
                                   : ast::kNoSubstitutionTemplateLiteralExprValueWord;
    if (!payloadHasOnlyWords(syntax, wordCount)) {
      return rejectInvariant(
          signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
          input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
    }
    auto text = stringText(input.tree, syntax.payload.words[valueWord]);
    bool accepted = false;
    ZC_IF_SOME(value, text) {
      if (isValidUtf8(value)) {
        auto bytes = zc::heapArray<uint8_t>(value.size());
        for (size_t index = 0; index < value.size(); ++index) {
          bytes[index] = static_cast<uint8_t>(value[index]);
        }
        primitive = type::semantic::PrimitiveKind::Str;
        literal = checked::CanonicalLiteral::string(zc::mv(bytes));
        accepted = true;
      }
    }
    if (!accepted) {
      return rejectInvariant(
          signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
          input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
    }
  } else if (syntax.kind == ast::SyntaxKind::CharacterLiteralExpr) {
    if (!payloadHasOnlyWords(syntax, ast::kCharacterLiteralExprPayloadWordCount)) {
      return rejectInvariant(
          signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
          input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
    }
    auto text = stringText(input.tree, syntax.payload.words[ast::kCharacterLiteralExprValueWord]);
    zc::Maybe<uint32_t> scalar;
    ZC_IF_SOME(value, text) { scalar = singleUtf8Scalar(value); }
    if (scalar == zc::none) {
      return rejectInvariant(
          signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
          input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
    }
    ZC_IF_SOME(value, scalar) {
      primitive = type::semantic::PrimitiveKind::Char;
      literal = checked::CanonicalLiteral::character(value);
    }
  } else if (syntax.kind == ast::SyntaxKind::UnitLiteral) {
    if (!payloadHasOnlyWords(syntax, ast::kUnitLiteralPayloadWordCount)) {
      return rejectInvariant(
          signature::CheckerInvariantKind::InvalidFact, input.module, actualPreorder, zc::none,
          input.node, input.checkedNode.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
    }
    primitive = type::semantic::PrimitiveKind::Unit;
  } else {
    return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, input.module,
                           actualPreorder, zc::none, input.node,
                           input.checkedNode.sourceSpan.clone(),
                           factPath(CheckedFactGroup::Literal));
  }

  auto canonical = input.semanticTypes.canonicalizeClosed(
      type::semantic::TypeData(type::semantic::PrimitiveTypeData{primitive}));
  if (canonical.is<identity::IdentityInvariant>()) {
    return rejectIdentity(canonical.get<identity::IdentityInvariant>().clone());
  }
  auto interned =
      input.semanticTypes.intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
  if (interned.is<identity::IdentityInvariant>()) {
    return rejectIdentity(interned.get<identity::IdentityInvariant>().clone());
  }
  const auto semanticType = interned.get<type::SemanticTypeInterned>().id;

  return EmittedFacts{checked::NodeTypeMap::Entry{input.node, semanticType, zc::Array<uint8_t>()},
                      checked::LiteralFactMap::Entry{
                          input.node,
                          checked::CheckedLiteralFact{input.node, zc::mv(literal), semanticType,
                                                      input.checkedNode.sourceSpan.clone()},
                          zc::Array<uint8_t>()}};
}

}  // namespace zomlang::compiler::checker::scalar_literal
