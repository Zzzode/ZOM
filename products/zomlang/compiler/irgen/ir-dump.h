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

#include "zc/core/io.h"
#include "zomlang/compiler/irgen/ir.h"

namespace zomlang {
namespace compiler {
namespace irgen {

/// \brief Closed invariant facts returned by the pre-output IR verifier.
///
/// These values are not diagnostic identifiers. The compiler driver maps them
/// to a registered invariant diagnostic and retains all structural context.
enum class IrDumpFailureKind : uint8_t {
  InvalidTypeReference,
  InvalidLayout,
  InvalidLayoutReference,
  InvalidFunction,
  DuplicateFunctionSymbol,
  InvalidBlockReference,
  DuplicateBlock,
  InvalidValueReference,
  DuplicateValue,
  InvalidInstruction,
  InvalidTerminator,
  UnresolvedCallTarget,
};

enum class IrDumpVerifierSite : uint8_t {
  Module,
  Layout,
  Function,
  Block,
  Instruction,
  Terminator,
};

struct IrDumpFailure final {
  IrDumpFailureKind kind = IrDumpFailureKind::InvalidFunction;
  IrDumpVerifierSite site = IrDumpVerifierSite::Module;
  identity::DefId definition;
  BlockId block;
  ValueId value;
  type::SemanticTypeId type;
  uint32_t index = 0;
};

using IrDumpResult = zc::Maybe<IrDumpFailure>;

/// \brief Write deterministic textual target-independent IR.
/// \param output Destination stream.
/// \param module Typed IR module to serialize.
/// \return None on success or a typed invariant failure before writing output.
IrDumpResult dumpModule(zc::OutputStream& output, const Module& module);

}  // namespace irgen
}  // namespace compiler
}  // namespace zomlang
