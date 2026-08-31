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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "compiler/backend/llvm/llvm-translator.h"

// This is the ONLY translation unit permitted to include `llvm/...` headers.
// Everything below the isolation wall is confined to this .cc; the public header
// exposes no LLVM type. std:: is used here because these are the LLVM ABI types
// (std::string, std::unique_ptr, llvm::raw_string_ostream); they never escape
// into a ZOM header.
#include <llvm/ADT/SmallVector.h>       // IWYU pragma: keep
#include <llvm/IR/BasicBlock.h>         // IWYU pragma: keep
#include <llvm/IR/Constants.h>          // IWYU pragma: keep
#include <llvm/IR/DataLayout.h>         // IWYU pragma: keep
#include <llvm/IR/DerivedTypes.h>       // IWYU pragma: keep
#include <llvm/IR/Function.h>           // IWYU pragma: keep
#include <llvm/IR/GlobalValue.h>        // IWYU pragma: keep
#include <llvm/IR/Instructions.h>       // IWYU pragma: keep
#include <llvm/IR/LLVMContext.h>        // IWYU pragma: keep
#include <llvm/IR/LegacyPassManager.h>  // IWYU pragma: keep
#include <llvm/IR/Module.h>             // IWYU pragma: keep
#include <llvm/IR/Type.h>               // IWYU pragma: keep
#include <llvm/IR/Verifier.h>           // IWYU pragma: keep
#include <llvm/MC/TargetRegistry.h>     // IWYU pragma: keep
#include <llvm/Support/CodeGen.h>       // IWYU pragma: keep
#include <llvm/Support/TargetSelect.h>  // IWYU pragma: keep
#include <llvm/Support/raw_ostream.h>   // IWYU pragma: keep
#include <llvm/Target/TargetMachine.h>  // IWYU pragma: keep
#include <llvm/Target/TargetOptions.h>  // IWYU pragma: keep
#include <llvm/TargetParser/Host.h>     // IWYU pragma: keep
#include <llvm/TargetParser/Triple.h>   // IWYU pragma: keep

#include <memory>
#include <optional>
#include <string>

namespace zomlang::compiler::backend::llvm {
namespace {

/// \brief Maps a closed LIR integer width to its bit count for LLVM iN types.
uint32_t bitCountFor(lir::IntegerBitWidth width) noexcept { return static_cast<uint32_t>(width); }

/// \brief Ensures the host target is registered exactly once for this process.
///
/// Registration is idempotent in LLVM. We register the native target, its data
/// layout provider, and its asm printer so a host TargetMachine (the source of
/// the data layout for this minimal slice) can be created.
void ensureNativeTargetInitialized() {
  static const bool initialized = [] {
    ::llvm::InitializeNativeTarget();
    ::llvm::InitializeNativeTargetAsmPrinter();
    return true;
  }();
  (void)initialized;
}

}  // namespace

struct LlvmTranslator::Impl {
  // No retained LLVM state between translations; each translate() call owns its
  // own context and module so the translator is a pure function of its input.
};

LlvmTranslator::LlvmTranslator() : impl(zc::heap<Impl>()) {}
LlvmTranslator::~LlvmTranslator() noexcept = default;
LlvmTranslator::LlvmTranslator(LlvmTranslator&&) noexcept = default;
LlvmTranslator& LlvmTranslator::operator=(LlvmTranslator&&) noexcept = default;

LlvmTranslationResult LlvmTranslator::translate(const lir::LirModule& module) {
  const auto functions = module.functions();
  if (functions.size() < 1 || functions.size() > 2) {
    return LlvmTranslationResult::failure(
        zc::heapString("LIR module must contain one or two functions in this slice"));
  }
  for (const auto& candidate : functions) {
    if (candidate.returnCarrier().kind() != lir::LirValueTypeKind::Integer) {
      return LlvmTranslationResult::failure(
          zc::heapString("LIR function return carrier must be an integer"));
    }
  }
  // Each function is one of: the single-block integer-constant return
  // (scalar-initializer / callee shape) or a generic multi-block control-flow
  // function (conditional diamond, reducible while-loop, comparison-driven
  // conditional, or the two-block Call+Return caller) whose entry begins with a
  // Goto, a conditional branch, or a call.
  auto isSupportedShape = [](const lir::LirFunction& candidate) -> bool {
    const auto candidateBlocks = candidate.blocks();
    // A multi-slot aggregate return is lowered only as a single entry block that
    // returns the bundle directly (mirroring the single-block scalar return). A
    // ReturnAggregate in any other position is not lowered in this slice, so
    // reject it before translation begins and the body emitter never reaches it.
    for (size_t index = 0; index < candidateBlocks.size(); ++index) {
      if (candidateBlocks[index].terminator().kind() == lir::LirTerminatorKind::ReturnAggregate) {
        if (index != 0 || candidateBlocks.size() != 1) { return false; }
      }
    }
    if (candidateBlocks.size() == 1) {
      // A single-block function either returns an integer constant (scalar
      // initializer / constant-return callee), returns a local slot (a
      // one-parameter callee that returns its parameter), or returns a multi-slot
      // aggregate bundle.
      const auto kind = candidateBlocks[0].terminator().kind();
      return kind == lir::LirTerminatorKind::ReturnInteger ||
             kind == lir::LirTerminatorKind::ReturnLocal ||
             kind == lir::LirTerminatorKind::ReturnAggregate;
    }
    if (candidateBlocks.size() < 2) { return false; }
    const auto entryKind = candidateBlocks[0].terminator().kind();
    return entryKind == lir::LirTerminatorKind::CondBranch ||
           entryKind == lir::LirTerminatorKind::Goto || entryKind == lir::LirTerminatorKind::Call;
  };
  for (const auto& candidate : functions) {
    if (!isSupportedShape(candidate)) {
      return LlvmTranslationResult::failure(
          zc::heapString("LIR function is outside the supported translation shapes"));
    }
  }

  // RFC 0021 deterministic translation order.
  ensureNativeTargetInitialized();

  // 1. Create one LLVM context and module.
  auto context = std::make_unique<::llvm::LLVMContext>();
  auto llvmModule = std::make_unique<::llvm::Module>("zom.module", *context);

  // 2. Set the exact triple and data-layout. For this minimal slice they come
  //    from a host TargetMachine (the provisioned LLVM's default target). This
  //    is the documented boundary: RFC 0016 target selection will supply the
  //    verified triple/data-layout bytes in a later step.
  const std::string triple = ::llvm::sys::getDefaultTargetTriple();
  const ::llvm::Triple parsedTriple(triple);
  std::string lookupError;
  const ::llvm::Target* target = ::llvm::TargetRegistry::lookupTarget(parsedTriple, lookupError);
  if (target == nullptr) {
    return LlvmTranslationResult::failure(
        zc::str("no LLVM target for host triple: ", lookupError.c_str()));
  }
  ::llvm::TargetOptions options;
  std::unique_ptr<::llvm::TargetMachine> targetMachine(target->createTargetMachine(
      parsedTriple, "generic", "", options, std::optional<::llvm::Reloc::Model>()));
  if (!targetMachine) {
    return LlvmTranslationResult::failure(zc::heapString("failed to create host TargetMachine"));
  }
  llvmModule->setTargetTriple(parsedTriple);
  llvmModule->setDataLayout(targetMachine->createDataLayout());

  // 3. Declare every function first so a call can reference a defined callee by
  //    its module-local index. Each function's symbol is its LIR symbol name (a
  //    module-local name, the same documented boundary as the reserved
  //    module-initializer symbol); no external/synthetic symbol is invented.
  auto integerType = [&](lir::IntegerBitWidth width) -> ::llvm::IntegerType* {
    return ::llvm::Type::getIntNTy(*context, bitCountFor(width));
  };
  // The LLVM return type of one LIR function. A single-block ReturnAggregate
  // returns a literal struct whose element types are the slot carriers in slot
  // order (RFC 0021 carrier bundle); every other shape returns its scalar integer
  // carrier. The struct is an LLVM literal aggregate, not an LIR SSA value type.
  auto aggregateReturnSlots = [](const lir::LirFunction& candidate)
      -> zc::Maybe<zc::ArrayPtr<const lir::LirIntegerConstant>> {
    const auto candidateBlocks = candidate.blocks();
    if (candidateBlocks.size() == 1 &&
        candidateBlocks[0].terminator().kind() == lir::LirTerminatorKind::ReturnAggregate) {
      return candidateBlocks[0].terminator().returnAggregateSlots();
    }
    return zc::none;
  };
  auto functionReturnType = [&](const lir::LirFunction& candidate) -> ::llvm::Type* {
    ZC_IF_SOME(slots, aggregateReturnSlots(candidate)) {
      zc::Vector<::llvm::Type*> elementTypes(slots.size());
      for (const auto& slot : slots) {
        elementTypes.add(integerType(slot.carrier().integerWidth()));
      }
      ::llvm::ArrayRef<::llvm::Type*> elementRef(elementTypes.begin(), elementTypes.size());
      return ::llvm::StructType::get(*context, elementRef);
    }
    return integerType(candidate.returnCarrier().integerWidth());
  };
  zc::Vector<::llvm::Function*> llvmFunctions;
  for (const auto& candidate : functions) {
    ::llvm::Type* candidateReturn = functionReturnType(candidate);
    zc::Vector<::llvm::Type*> paramTypes;
    for (const auto& parameter : candidate.parameters()) {
      paramTypes.add(integerType(parameter.carrier().integerWidth()));
    }
    ::llvm::ArrayRef<::llvm::Type*> paramTypeRef(paramTypes.begin(), paramTypes.size());
    ::llvm::FunctionType* functionType =
        ::llvm::FunctionType::get(candidateReturn, paramTypeRef, /*isVarArg=*/false);
    llvmFunctions.add(::llvm::Function::Create(functionType, ::llvm::GlobalValue::ExternalLinkage,
                                               candidate.symbolName().cStr(), llvmModule.get()));
  }

  // 4. Emit each function body.
  for (size_t functionIndex = 0; functionIndex < functions.size(); ++functionIndex) {
    const auto& function = functions[functionIndex];
    ::llvm::Function* llvmFunction = llvmFunctions[functionIndex];
    const auto blocks = function.blocks();
    ::llvm::IntegerType* returnType = integerType(function.returnCarrier().integerWidth());

    if (blocks.size() == 1 &&
        blocks[0].terminator().kind() == lir::LirTerminatorKind::ReturnInteger) {
      // Single entry block returning the integer constant.
      ::llvm::BasicBlock* entryBlock = ::llvm::BasicBlock::Create(*context, "entry", llvmFunction);
      const auto& returnConstant = blocks[0].terminator().returnIntegerValue();
      ::llvm::Constant* value =
          ::llvm::ConstantInt::get(returnType, returnConstant.bits(), /*IsSigned=*/false);
      ::llvm::ReturnInst::Create(*context, value, entryBlock);
      continue;
    }

    if (blocks.size() == 1 &&
        blocks[0].terminator().kind() == lir::LirTerminatorKind::ReturnAggregate) {
      // Single entry block returning a multi-slot bundle as a literal struct:
      // build the struct value from zeroinitializer and insertvalue each slot at
      // its monotone index, then return it. The function's declared return type
      // is the matching literal StructType.
      ::llvm::BasicBlock* entryBlock = ::llvm::BasicBlock::Create(*context, "entry", llvmFunction);
      auto* structType = ::llvm::dyn_cast<::llvm::StructType>(llvmFunction->getReturnType());
      const auto returnSlots = blocks[0].terminator().returnAggregateSlots();
      if (structType == nullptr || structType->getNumElements() != returnSlots.size()) {
        return LlvmTranslationResult::failure(
            zc::heapString("aggregate return type does not match its slot count"));
      }
      ::llvm::Value* aggregate = ::llvm::ConstantAggregateZero::get(structType);
      for (unsigned slotIndex = 0; slotIndex < returnSlots.size(); ++slotIndex) {
        auto* slotType = integerType(returnSlots[slotIndex].carrier().integerWidth());
        ::llvm::Constant* slotValue =
            ::llvm::ConstantInt::get(slotType, returnSlots[slotIndex].bits(), /*IsSigned=*/false);
        aggregate =
            ::llvm::InsertValueInst::Create(aggregate, slotValue, {slotIndex}, "agg", entryBlock);
      }
      ::llvm::ReturnInst::Create(*context, aggregate, entryBlock);
      continue;
    }

    // Generic multi-block control-flow function. Every parameter and body local
    // is materialized as an alloca in the entry block (the classic
    // locals-to-memory lowering, needing no SSA/phi reasoning); parameters store
    // their incoming argument, Assign/Compare statements store into a local, a
    // CondBranch loads its boolean condition local, a Call stores its callee's
    // integer result, and a ReturnLocal loads the result. This lowers any
    // reducible CFG built from Goto / CondBranch / Call / ReturnLocal over
    // Assign / Compare statements, keeping the RFC 0021 faithful block-by-block
    // translation.
    const auto parameters = function.parameters();
    const auto locals = function.locals();

    // Pass 1: create one LLVM block per LIR block (one-based dense ordinals).
    zc::Vector<::llvm::BasicBlock*> llvmBlocks;
    for (size_t index = 0; index < blocks.size(); ++index) {
      llvmBlocks.add(::llvm::BasicBlock::Create(*context, "bb", llvmFunction));
    }
    auto blockFor = [&](lir::LirBlockId id) -> ::llvm::BasicBlock* {
      return llvmBlocks[id.ordinal() - 1];
    };
    ::llvm::BasicBlock* entryBlock = llvmBlocks[0];

    // Alloca every local slot (parameters and body locals), keyed by ordinal, in
    // the entry block; then store each incoming argument into its slot.
    auto slotType = [&](uint32_t ordinal) -> ::llvm::IntegerType* {
      for (const auto& parameter : parameters) {
        if (parameter.ordinal() == ordinal) {
          return integerType(parameter.carrier().integerWidth());
        }
      }
      for (const auto& local : locals) {
        if (local.ordinal() == ordinal) { return integerType(local.carrier().integerWidth()); }
      }
      return returnType;
    };
    zc::Vector<uint32_t> slotOrdinals;
    zc::Vector<::llvm::AllocaInst*> slots;
    auto slotFor = [&](uint32_t ordinal) -> ::llvm::AllocaInst* {
      for (size_t i = 0; i < slotOrdinals.size(); ++i) {
        if (slotOrdinals[i] == ordinal) { return slots[i]; }
      }
      auto* slot = new ::llvm::AllocaInst(slotType(ordinal), /*AddrSpace=*/0, "slot", entryBlock);
      slotOrdinals.add(ordinal);
      slots.add(slot);
      return slot;
    };
    for (size_t i = 0; i < parameters.size(); ++i) {
      auto* slot = slotFor(parameters[i].ordinal());
      new ::llvm::StoreInst(llvmFunction->getArg(static_cast<unsigned>(i)), slot,
                            /*isVolatile=*/false, entryBlock);
    }
    for (const auto& local : locals) { (void)slotFor(local.ordinal()); }

    auto loadOperand = [&](const lir::LirOperand& operand,
                           ::llvm::BasicBlock* target) -> ::llvm::Value* {
      if (operand.isConstant()) {
        auto* type = integerType(operand.constantValue().carrier().integerWidth());
        return ::llvm::ConstantInt::get(type, operand.constantValue().bits(), /*IsSigned=*/false);
      }
      auto* slot = slotFor(operand.localOrdinal());
      return new ::llvm::LoadInst(slot->getAllocatedType(), slot, "use", target);
    };

    // Pass 2: emit each block's statements then its terminator.
    for (size_t index = 0; index < blocks.size(); ++index) {
      const auto& source = blocks[index];
      ::llvm::BasicBlock* target = llvmBlocks[index];
      for (const auto& statement : source.statements()) {
        auto* destination = slotFor(statement.destinationOrdinal());
        ::llvm::Value* stored = nullptr;
        if (statement.kind() == lir::LirStatementKind::Compare) {
          ::llvm::Value* left = loadOperand(statement.left(), target);
          ::llvm::Value* right = loadOperand(statement.right(), target);
          ::llvm::CmpInst::Predicate predicate = ::llvm::CmpInst::ICMP_EQ;
          switch (statement.comparisonOp()) {
            case lir::LirComparisonOp::Eq:
              predicate = ::llvm::CmpInst::ICMP_EQ;
              break;
            case lir::LirComparisonOp::Ne:
              predicate = ::llvm::CmpInst::ICMP_NE;
              break;
            case lir::LirComparisonOp::Lt:
              predicate = ::llvm::CmpInst::ICMP_SLT;
              break;
            case lir::LirComparisonOp::Le:
              predicate = ::llvm::CmpInst::ICMP_SLE;
              break;
            case lir::LirComparisonOp::Gt:
              predicate = ::llvm::CmpInst::ICMP_SGT;
              break;
            case lir::LirComparisonOp::Ge:
              predicate = ::llvm::CmpInst::ICMP_SGE;
              break;
          }
          stored = ::llvm::CmpInst::Create(::llvm::Instruction::ICmp, predicate, left, right, "cmp",
                                           target);
        } else {
          stored = loadOperand(statement.value(), target);
        }
        new ::llvm::StoreInst(stored, destination, /*isVolatile=*/false, target);
      }
      const auto& terminator = source.terminator();
      switch (terminator.kind()) {
        case lir::LirTerminatorKind::Goto:
          ::llvm::BranchInst::Create(blockFor(terminator.gotoTarget()), target);
          break;
        case lir::LirTerminatorKind::CondBranch: {
          auto* conditionSlot = slotFor(terminator.conditionOrdinal());
          auto* condition = new ::llvm::LoadInst(conditionSlot->getAllocatedType(), conditionSlot,
                                                 "cond", target);
          ::llvm::BranchInst::Create(blockFor(terminator.condTrueTarget()),
                                     blockFor(terminator.condFalseTarget()), condition, target);
          break;
        }
        case lir::LirTerminatorKind::Call: {
          // Call a module-local defined function, passing zero, one, or a bounded
          // vector of integer-constant arguments; store the integer result into
          // the destination slot, then branch to the normal target.
          ::llvm::Function* callee = llvmFunctions[terminator.calleeIndex()];
          zc::Vector<::llvm::Value*> callArgs;
          if (terminator.callHasArgument()) {
            auto* argType = integerType(terminator.callArgument().carrier().integerWidth());
            callArgs.add(::llvm::ConstantInt::get(argType, terminator.callArgument().bits(),
                                                  /*IsSigned=*/false));
          }
          for (const auto& argument : terminator.callArguments()) {
            auto* argType = integerType(argument.carrier().integerWidth());
            callArgs.add(::llvm::ConstantInt::get(argType, argument.bits(), /*IsSigned=*/false));
          }
          ::llvm::ArrayRef<::llvm::Value*> callArgsRef(callArgs.begin(), callArgs.size());
          auto* callResult = ::llvm::CallInst::Create(callee->getFunctionType(), callee,
                                                      callArgsRef, "call", target);
          auto* destination = slotFor(terminator.callDestinationOrdinal());
          new ::llvm::StoreInst(callResult, destination, /*isVolatile=*/false, target);
          ::llvm::BranchInst::Create(blockFor(terminator.callNormalTarget()), target);
          break;
        }
        case lir::LirTerminatorKind::ReturnLocal: {
          auto* slot = slotFor(terminator.returnLocalOrdinal());
          auto* loaded = new ::llvm::LoadInst(slot->getAllocatedType(), slot, "value", target);
          ::llvm::ReturnInst::Create(*context, loaded, target);
          break;
        }
        case lir::LirTerminatorKind::ReturnInteger: {
          ::llvm::Constant* value = ::llvm::ConstantInt::get(
              returnType, terminator.returnIntegerValue().bits(), /*IsSigned=*/false);
          ::llvm::ReturnInst::Create(*context, value, target);
          break;
        }
        case lir::LirTerminatorKind::ReturnAggregate:
          // A multi-slot aggregate return is not lowered in this slice; the
          // shape gate above (isSupportedShape) rejects a function carrying it
          // before translation reaches here, so this arm is unreachable. The
          // literal-struct lowering is the next RFC 0021 step.
          ZC_UNREACHABLE;
      }
    }
  }

  // 10. Run mandatory LLVM module verification.
  std::string verifyBuffer;
  ::llvm::raw_string_ostream verifyStream(verifyBuffer);
  if (::llvm::verifyModule(*llvmModule, &verifyStream)) {
    verifyStream.flush();
    return LlvmTranslationResult::failure(
        zc::str("llvm::verifyModule reported a broken module: ", verifyBuffer.c_str()));
  }

  // 11. Materialize the verified IR to text for inspection and testing.
  std::string irBuffer;
  ::llvm::raw_string_ostream irStream(irBuffer);
  llvmModule->print(irStream, /*AAW=*/nullptr);
  irStream.flush();

  // 12. RFC 0021 ObjectEmission phase: lower the verified module to a native
  //     object file for the same host TargetMachine. A legacy PassManager is the
  //     supported entry point for addPassesToEmitFile; it writes the object bytes
  //     into an owned seekable buffer (raw_pwrite_stream). Object emission runs
  //     only after verifyModule succeeds, so no broken module reaches codegen.
  ::llvm::SmallVector<char, 0> objectBytes;
  ::llvm::raw_svector_ostream objectStream(objectBytes);
  {
    ::llvm::legacy::PassManager passManager;
    if (targetMachine->addPassesToEmitFile(passManager, objectStream, /*DwoOut=*/nullptr,
                                           ::llvm::CodeGenFileType::ObjectFile)) {
      return LlvmTranslationResult::failure(
          zc::heapString("host TargetMachine cannot emit an object file for this module"));
    }
    passManager.run(*llvmModule);
  }
  if (objectBytes.empty()) {
    return LlvmTranslationResult::failure(zc::heapString("object emission produced no bytes"));
  }

  // 13. Publish only on success: the verified IR text and the native object bytes.
  auto objectCode = zc::heapArray<uint8_t>(objectBytes.size());
  for (size_t index = 0; index < objectBytes.size(); ++index) {
    objectCode[index] = static_cast<uint8_t>(objectBytes[index]);
  }
  return LlvmTranslationResult::success(zc::heapString(irBuffer.c_str()), zc::mv(objectCode));
}

}  // namespace zomlang::compiler::backend::llvm
