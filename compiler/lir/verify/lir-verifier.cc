// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations
// under the License.

#include "compiler/lir/verify/lir-verifier.h"

#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::lir {
namespace {

constexpr uint32_t kFunctionLevel = 0;

LirVerificationFinding fault(LirVerificationFaultKind kind, uint32_t functionIndex,
                             uint32_t blockOrdinal = kFunctionLevel,
                             uint32_t statementIndex = kFunctionLevel) noexcept {
  return LirVerificationFinding{kind, functionIndex, blockOrdinal, statementIndex};
}

/// \brief Carrier of a declared slot, or none when the ordinal is undeclared.
const ValueType* declaredSlotCarrier(const Function& function, uint32_t ordinal) noexcept {
  for (const auto& parameter : function.parameters()) {
    if (parameter.ordinal() == ordinal) return &parameter.carrier();
  }
  for (const auto& local : function.locals()) {
    if (local.ordinal() == ordinal) return &local.carrier();
  }
  return nullptr;
}

/// \brief Carriers of one local-use operand slot, or none when undeclared.
const ValueType* operandSlotCarrier(const Function& function, const Operand& operand) noexcept {
  if (operand.isConstant()) return nullptr;
  return declaredSlotCarrier(function, operand.localOrdinal());
}

}  // namespace

zc::Maybe<LirVerificationFinding> LirStructuralVerifier::verify(const Module& module) noexcept {
  const auto functions = module.functions();

  // Function symbols are non-empty and unique within the module.
  for (uint32_t i = 0; i < functions.size(); ++i) {
    if (functions[i].symbolName() == ""_zc) {
      return fault(LirVerificationFaultKind::EmptyOrDuplicateSymbol, i);
    }
    for (uint32_t j = 0; j < i; ++j) {
      if (functions[i].symbolName() == functions[j].symbolName()) {
        return fault(LirVerificationFaultKind::EmptyOrDuplicateSymbol, i);
      }
    }
  }

  for (uint32_t functionIndex = 0; functionIndex < functions.size(); ++functionIndex) {
    const Function& function = functions[functionIndex];
    const auto blocks = function.blocks();

    // Block ordinals exist, start at one, and are dense and unique.
    if (blocks.size() == 0 || blocks[0].id().ordinal() != 1) {
      return fault(LirVerificationFaultKind::MissingEntryBlock, functionIndex);
    }
    for (uint32_t i = 1; i < blocks.size(); ++i) {
      const uint32_t expected = i + 1;
      if (blocks[i].id().ordinal() == blocks[i - 1].id().ordinal()) {
        return fault(LirVerificationFaultKind::DuplicateBlockOrdinal, functionIndex, expected);
      }
      if (blocks[i].id().ordinal() != expected) {
        return fault(LirVerificationFaultKind::NonDenseBlockOrdinals, functionIndex, expected);
      }
    }

    // Declared terminator targets must name a block of this function.
    for (const auto& block : blocks) {
      const Terminator& terminator = block.terminator();
      auto targetDeclared = [&](LirBlockId target) noexcept -> bool {
        if (!target.isValid()) return false;
        for (const auto& candidate : blocks) {
          if (candidate.id() == target) return true;
        }
        return false;
      };
      switch (terminator.kind()) {
        case TerminatorKind::Goto:
          if (!targetDeclared(terminator.gotoTarget())) {
            return fault(LirVerificationFaultKind::DanglingBlockTarget, functionIndex,
                         block.id().ordinal());
          }
          break;
        case TerminatorKind::CondBranch:
          if (!targetDeclared(terminator.condTrueTarget()) ||
              !targetDeclared(terminator.condFalseTarget())) {
            return fault(LirVerificationFaultKind::DanglingBlockTarget, functionIndex,
                         block.id().ordinal());
          }
          break;
        case TerminatorKind::Call:
          if (!targetDeclared(terminator.callNormalTarget())) {
            return fault(LirVerificationFaultKind::DanglingBlockTarget, functionIndex,
                         block.id().ordinal());
          }
          break;
        case TerminatorKind::ReturnInteger:
        case TerminatorKind::ReturnLocal:
        case TerminatorKind::ReturnAggregate:
          break;
      }
    }

    // Every block is reachable from the entry block through terminator edges.
    zc::Vector<bool> reached;
    reached.reserve(blocks.size());
    for (uint32_t i = 0; i < blocks.size(); ++i) reached.add(false);
    reached[0] = true;
    bool changed = true;
    while (changed) {
      changed = false;
      for (uint32_t i = 0; i < blocks.size(); ++i) {
        if (!reached[i]) continue;
        auto mark = [&](LirBlockId target) noexcept {
          for (uint32_t j = 0; j < blocks.size(); ++j) {
            if (blocks[j].id() == target && !reached[j]) {
              reached[j] = true;
              changed = true;
            }
          }
        };
        const Terminator& terminator = blocks[i].terminator();
        switch (terminator.kind()) {
          case TerminatorKind::Goto:
            mark(terminator.gotoTarget());
            break;
          case TerminatorKind::CondBranch:
            mark(terminator.condTrueTarget());
            mark(terminator.condFalseTarget());
            break;
          case TerminatorKind::Call:
            mark(terminator.callNormalTarget());
            break;
          case TerminatorKind::ReturnInteger:
          case TerminatorKind::ReturnLocal:
          case TerminatorKind::ReturnAggregate:
            break;
        }
      }
    }
    for (uint32_t i = 0; i < blocks.size(); ++i) {
      if (!reached[i]) {
        return fault(LirVerificationFaultKind::UnreachableBlock, functionIndex,
                     blocks[i].id().ordinal());
      }
    }

    // Parameter ordinals are dense 1..P; body locals follow densely P+1..P+L;
    // declaration order matches ordinal order and the ranges do not overlap.
    const auto parameters = function.parameters();
    const auto locals = function.locals();
    for (uint32_t i = 0; i < parameters.size(); ++i) {
      if (parameters[i].ordinal() != i + 1) {
        return fault(LirVerificationFaultKind::NonDenseLocalSlots, functionIndex);
      }
    }
    for (uint32_t i = 0; i < locals.size(); ++i) {
      if (locals[i].ordinal() != static_cast<uint32_t>(parameters.size()) + i + 1) {
        return fault(LirVerificationFaultKind::NonDenseLocalSlots, functionIndex);
      }
    }

    auto requireSlot = [&](uint32_t ordinal, uint32_t blockOrdinal,
                           uint32_t statementIndex) noexcept -> zc::Maybe<LirVerificationFinding> {
      if (declaredSlotCarrier(function, ordinal) == nullptr) {
        return fault(LirVerificationFaultKind::UndeclaredLocalSlot, functionIndex, blockOrdinal,
                     statementIndex);
      }
      return zc::none;
    };
    auto requireCarrier =
        [&](const ValueType& actual, const ValueType& expected, uint32_t blockOrdinal,
            uint32_t statementIndex) noexcept -> zc::Maybe<LirVerificationFinding> {
      if (actual != expected) {
        return fault(LirVerificationFaultKind::CarrierMismatch, functionIndex, blockOrdinal,
                     statementIndex);
      }
      return zc::none;
    };

    for (const auto& block : blocks) {
      const uint32_t blockOrdinal = block.id().ordinal();

      for (uint32_t s = 0; s < block.statements().size(); ++s) {
        const Statement& statement = block.statements()[s];
        const uint32_t statementIndex = s + 1;
        auto destinationFinding =
            requireSlot(statement.destinationOrdinal(), blockOrdinal, statementIndex);
        if (destinationFinding != zc::none) return destinationFinding;
        const ValueType& destinationCarrier =
            *declaredSlotCarrier(function, statement.destinationOrdinal());

        switch (statement.kind()) {
          case StatementKind::Assign: {
            const Operand& value = statement.value();
            if (value.isConstant()) {
              auto carrierFinding =
                  requireCarrier(value.constantValue().carrier(), destinationCarrier, blockOrdinal,
                                 statementIndex);
              if (carrierFinding != zc::none) return carrierFinding;
            } else {
              auto slotFinding = requireSlot(value.localOrdinal(), blockOrdinal, statementIndex);
              if (slotFinding != zc::none) return slotFinding;
              auto carrierFinding =
                  requireCarrier(*operandSlotCarrier(function, value), destinationCarrier,
                                 blockOrdinal, statementIndex);
              if (carrierFinding != zc::none) return carrierFinding;
            }
            break;
          }
          case StatementKind::Compare: {
            // The destination holds the one-bit comparison result.
            if (destinationCarrier.kind() != ValueTypeKind::Integer ||
                destinationCarrier.integerWidth() != IntegerBitWidth::Bit1) {
              return fault(LirVerificationFaultKind::CarrierMismatch, functionIndex, blockOrdinal,
                           statementIndex);
            }
            const Operand& left = statement.left();
            const Operand& right = statement.right();
            const ValueType* leftCarrier = nullptr;
            const ValueType* rightCarrier = nullptr;
            if (left.isConstant()) {
              leftCarrier = &left.constantValue().carrier();
            } else {
              auto slotFinding = requireSlot(left.localOrdinal(), blockOrdinal, statementIndex);
              if (slotFinding != zc::none) return slotFinding;
              leftCarrier = operandSlotCarrier(function, left);
            }
            if (right.isConstant()) {
              rightCarrier = &right.constantValue().carrier();
            } else {
              auto slotFinding = requireSlot(right.localOrdinal(), blockOrdinal, statementIndex);
              if (slotFinding != zc::none) return slotFinding;
              rightCarrier = operandSlotCarrier(function, right);
            }
            if (*leftCarrier != *rightCarrier) {
              return fault(LirVerificationFaultKind::CarrierMismatch, functionIndex, blockOrdinal,
                           statementIndex);
            }
            break;
          }
        }
      }

      const Terminator& terminator = block.terminator();
      switch (terminator.kind()) {
        case TerminatorKind::ReturnInteger:
          if (terminator.returnIntegerValue().carrier() != function.returnCarrier()) {
            return fault(LirVerificationFaultKind::ReturnCarrierMismatch, functionIndex,
                         blockOrdinal);
          }
          break;
        case TerminatorKind::ReturnLocal: {
          const uint32_t ordinal = terminator.returnLocalOrdinal();
          auto returnSlotFinding = requireSlot(ordinal, blockOrdinal, kFunctionLevel);
          if (returnSlotFinding != zc::none) return returnSlotFinding;
          if (*declaredSlotCarrier(function, ordinal) != function.returnCarrier()) {
            return fault(LirVerificationFaultKind::ReturnCarrierMismatch, functionIndex,
                         blockOrdinal);
          }
          break;
        }
        case TerminatorKind::ReturnAggregate: {
          const auto slots = terminator.returnAggregateSlots();
          if (slots.size() == 0 || slots.size() > kMaxAggregateReturnSlots) {
            return fault(LirVerificationFaultKind::ReturnCarrierMismatch, functionIndex,
                         blockOrdinal);
          }
          for (const auto& slot : slots) {
            if (slot.carrier().kind() != ValueTypeKind::Integer) {
              return fault(LirVerificationFaultKind::CarrierMismatch, functionIndex, blockOrdinal);
            }
          }
          if (slots[0].carrier() != function.returnCarrier()) {
            return fault(LirVerificationFaultKind::ReturnCarrierMismatch, functionIndex,
                         blockOrdinal);
          }
          break;
        }
        case TerminatorKind::CondBranch: {
          const uint32_t ordinal = terminator.conditionOrdinal();
          auto returnSlotFinding = requireSlot(ordinal, blockOrdinal, kFunctionLevel);
          if (returnSlotFinding != zc::none) return returnSlotFinding;
          const ValueType& carrier = *declaredSlotCarrier(function, ordinal);
          if (carrier.kind() != ValueTypeKind::Integer ||
              carrier.integerWidth() != IntegerBitWidth::Bit1) {
            return fault(LirVerificationFaultKind::ConditionNotBit1, functionIndex, blockOrdinal);
          }
          break;
        }
        case TerminatorKind::Call: {
          const uint32_t calleeIndex = terminator.calleeIndex();
          if (calleeIndex >= functions.size()) {
            return fault(LirVerificationFaultKind::CalleeIndexOutOfRange, functionIndex,
                         blockOrdinal);
          }
          const Function& callee = functions[calleeIndex];
          const uint32_t destinationOrdinal = terminator.callDestinationOrdinal();
          auto destinationSlotFinding =
              requireSlot(destinationOrdinal, blockOrdinal, kFunctionLevel);
          if (destinationSlotFinding != zc::none) return destinationSlotFinding;
          if (*declaredSlotCarrier(function, destinationOrdinal) != callee.returnCarrier()) {
            return fault(LirVerificationFaultKind::CarrierMismatch, functionIndex, blockOrdinal);
          }
          // The ordered argument vector matches the callee parameters in count
          // and carrier. A constant argument carries its own carrier; a
          // local-use argument must name a declared slot whose carrier matches
          // the callee parameter.
          const auto arguments = terminator.callArguments();
          if (arguments.size() != callee.parameters().size()) {
            return fault(LirVerificationFaultKind::TerminatorArity, functionIndex, blockOrdinal);
          }
          for (uint32_t a = 0; a < arguments.size(); ++a) {
            const Operand& argument = arguments[a];
            const ValueType& expectedCarrier = callee.parameters()[a].carrier();
            if (!argument.isConstant()) {
              auto slotFinding = requireSlot(argument.localOrdinal(), blockOrdinal, kFunctionLevel);
              if (slotFinding != zc::none) return slotFinding;
              if (*declaredSlotCarrier(function, argument.localOrdinal()) != expectedCarrier) {
                return fault(LirVerificationFaultKind::CarrierMismatch, functionIndex,
                             blockOrdinal);
              }
              continue;
            }
            if (argument.constantValue().carrier() != expectedCarrier) {
              return fault(LirVerificationFaultKind::CarrierMismatch, functionIndex, blockOrdinal);
            }
          }
          break;
        }
        case TerminatorKind::Goto:
          break;
      }
    }
  }

  return zc::none;
}

}  // namespace zomlang::compiler::lir
