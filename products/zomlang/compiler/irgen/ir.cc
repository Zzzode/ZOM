// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/irgen/ir.h"

namespace zomlang {
namespace compiler {
namespace irgen {

Instruction::Instruction(ValueId result, InstructionData data) noexcept
    : result(result), data(zc::mv(data)) {}

Instruction::Instruction(Instruction&& other) noexcept = default;

Instruction& Instruction::operator=(Instruction&& other) noexcept = default;

BasicBlock::BasicBlock(BlockId id, zc::Maybe<BlockParameter> parameter,
                       zc::Vector<Instruction> instructions, Terminator terminator) noexcept
    : id(id),
      parameter(parameter),
      instructions(zc::mv(instructions)),
      terminator(zc::mv(terminator)) {}

BasicBlock::BasicBlock(BasicBlock&& other) noexcept = default;

BasicBlock& BasicBlock::operator=(BasicBlock&& other) noexcept = default;

Function::Function(zc::String name, symbol::SymbolId symbol, type::TypeId checkedSignature,
                   type::TypeId abiReturnType, uint32_t errorUnionLayout,
                   zc::Vector<BasicBlock> blocks) noexcept
    : name(zc::mv(name)),
      symbol(symbol),
      checkedSignature(checkedSignature),
      abiReturnType(abiReturnType),
      errorUnionLayout(errorUnionLayout),
      blocks(zc::mv(blocks)) {}

Function::Function(Function&& other) noexcept = default;

Function& Function::operator=(Function&& other) noexcept = default;

struct Module::Impl {
  explicit Impl(TargetDataLayout target) : target(target) {}

  TargetDataLayout target;
  type::TypeInterner interner;
  zc::Vector<ErrorUnionLayout> errorUnionLayouts;
  zc::Vector<Function> functions;
};

Module::Module(TargetDataLayout target) : impl(zc::heap<Impl>(target)) {}

Module::~Module() noexcept(false) = default;

Module::Module(Module&& other) noexcept = default;

Module& Module::operator=(Module&& other) noexcept = default;

const TargetDataLayout& Module::getTarget() const { return impl->target; }

type::TypeInterner& Module::getTypeInterner() { return impl->interner; }

const type::TypeInterner& Module::getTypeInterner() const { return impl->interner; }

zc::Maybe<uint32_t> Module::addErrorUnionLayout(ErrorUnionLayout layout) {
  if (layout.alternatives.empty()) { return zc::none; }
  const auto successType = layout.alternatives[0].typeId;
  for (size_t i = 0; i < impl->errorUnionLayouts.size(); ++i) {
    const auto& existing = impl->errorUnionLayouts[i];
    if (existing.typeId == layout.typeId && !existing.alternatives.empty() &&
        existing.alternatives[0].typeId == successType) {
      return static_cast<uint32_t>(i);
    }
  }

  const auto index = static_cast<uint32_t>(impl->errorUnionLayouts.size());
  impl->errorUnionLayouts.add(zc::mv(layout));
  return index;
}

zc::Maybe<const ErrorUnionLayout&> Module::getErrorUnionLayout(uint32_t index) const {
  if (index >= impl->errorUnionLayouts.size()) { return zc::none; }
  return impl->errorUnionLayouts[index];
}

zc::ArrayPtr<const ErrorUnionLayout> Module::getErrorUnionLayouts() const {
  return impl->errorUnionLayouts.asPtr();
}

Function& Module::addFunction(Function function) {
  impl->functions.add(zc::mv(function));
  return impl->functions.back();
}

zc::ArrayPtr<const Function> Module::getFunctions() const { return impl->functions.asPtr(); }

}  // namespace irgen
}  // namespace compiler
}  // namespace zomlang
