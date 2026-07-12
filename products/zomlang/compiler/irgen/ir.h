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

#pragma once

#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/frozen-registry.h"
#include "zomlang/compiler/irgen/error-union-layout.h"
#include "zomlang/compiler/irgen/target-data-layout.h"
#include "zomlang/compiler/type/semantic-type-store.h"

namespace zomlang {
namespace compiler {
namespace irgen {

class ValueId final {
public:
  constexpr ValueId() : value(0) {}
  explicit constexpr ValueId(uint32_t value) : value(value) {}

  constexpr bool isValid() const { return value != 0; }
  constexpr bool operator==(ValueId other) const { return value == other.value; }
  constexpr bool operator!=(ValueId other) const { return value != other.value; }

  uint32_t value;
};

class BlockId final {
public:
  constexpr BlockId() : value(0) {}
  explicit constexpr BlockId(uint32_t value) : value(value) {}

  constexpr bool isValid() const { return value != 0; }
  constexpr bool operator==(BlockId other) const { return value == other.value; }
  constexpr bool operator!=(BlockId other) const { return value != other.value; }

  uint32_t value;
};

struct IntegerConstant final {
  type::SemanticTypeId resultType;
  zc::String value;
};

struct RaisingCall final {
  type::SemanticTypeId resultType;
  identity::DefId target;
};

struct ErrorUnionConstruct final {
  type::SemanticTypeId resultType;
  ValueId payload;
  uint32_t layoutIndex = 0;
  uint64_t tag = 0;
};

struct ErrorUnionMovePayload final {
  type::SemanticTypeId resultType;
  ValueId source;
  uint32_t layoutIndex = 0;
  uint64_t tag = 0;
};

using InstructionData =
    zc::OneOf<IntegerConstant, RaisingCall, ErrorUnionConstruct, ErrorUnionMovePayload>;

struct Instruction final {
  Instruction(ValueId result, InstructionData data) noexcept;
  Instruction(Instruction&& other) noexcept;
  Instruction& operator=(Instruction&& other) noexcept;

  ZC_DISALLOW_COPY(Instruction);

  ValueId result;
  InstructionData data;
};

struct BlockParameter final {
  ValueId value;
  type::SemanticTypeId type;
};

struct ReturnTerminator final {
  ValueId value;
  type::SemanticTypeId valueType;
};

struct JumpTerminator final {
  BlockId target;
  ValueId argument;
};

struct ErrorUnionBranchTerminator final {
  ValueId value;
  uint32_t layoutIndex = 0;
  BlockId successTarget;
  BlockId errorTarget;
};

struct PanicSourceMetadata final {
  zc::String file;
  uint32_t line = 0;
  uint32_t column = 0;
  uint32_t byteStart = 0;
  uint32_t byteEnd = 0;
  type::SemanticTypeId payloadType;
};

struct ForcedUnwrapPanicTerminator final {
  ValueId value;
  uint32_t layoutIndex = 0;
  uint64_t tag = 0;
  PanicSourceMetadata metadata;
};

using Terminator = zc::OneOf<ReturnTerminator, JumpTerminator, ErrorUnionBranchTerminator,
                             ForcedUnwrapPanicTerminator>;

struct BasicBlock final {
  BasicBlock(BlockId id, zc::Maybe<BlockParameter> parameter, zc::Vector<Instruction> instructions,
             Terminator terminator) noexcept;
  BasicBlock(BasicBlock&& other) noexcept;
  BasicBlock& operator=(BasicBlock&& other) noexcept;

  ZC_DISALLOW_COPY(BasicBlock);

  BlockId id;
  zc::Maybe<BlockParameter> parameter;
  zc::Vector<Instruction> instructions;
  Terminator terminator;
};

struct Function final {
  Function(zc::String name, identity::DefId definition, type::SemanticTypeId checkedSignature,
           type::SemanticTypeId abiReturnType, uint32_t errorUnionLayout,
           zc::Vector<BasicBlock> blocks) noexcept;
  Function(Function&& other) noexcept;
  Function& operator=(Function&& other) noexcept;

  ZC_DISALLOW_COPY(Function);

  zc::String name;
  identity::DefId definition;
  type::SemanticTypeId checkedSignature;
  type::SemanticTypeId abiReturnType;
  uint32_t errorUnionLayout = 0;
  zc::Vector<BasicBlock> blocks;
};

class Module final {
public:
  Module(type::SemanticTypeStore& semanticTypes, TargetDataLayout target);
  ~Module() noexcept(false);

  ZC_DISALLOW_COPY(Module);
  Module(Module&& other) noexcept;
  Module& operator=(Module&& other) noexcept;

  const TargetDataLayout& getTarget() const;
  type::SemanticTypeStore& getSemanticTypeStore();
  const type::SemanticTypeStore& getSemanticTypeStore() const;

  zc::Maybe<uint32_t> addErrorUnionLayout(ErrorUnionLayout layout);
  zc::Maybe<const ErrorUnionLayout&> getErrorUnionLayout(uint32_t index) const;
  zc::ArrayPtr<const ErrorUnionLayout> getErrorUnionLayouts() const;

  Function& addFunction(Function function);
  zc::ArrayPtr<const Function> getFunctions() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace irgen
}  // namespace compiler
}  // namespace zomlang
