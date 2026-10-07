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

/// \brief Maps one LIR integer width to its LLVM iN type.
::llvm::IntegerType* integerType(::llvm::LLVMContext& context,
                                 lir::IntegerBitWidth width) noexcept {
  return ::llvm::Type::getIntNTy(context, bitCountFor(width));
}

/// \brief Maps one LIR SSA carrier to its LLVM representation.
///
/// An opaque pointer carrier is a target pointer in its declared address space
/// (opaque-pointer LLVM, no pointee type); a float carrier maps to its
/// IEEE-754 LLVM type; an aggregate carrier maps to a literal struct whose
/// element types are the field carriers in declaration order (recursing for a
/// nested aggregate field); every other carrier is an integer today.
::llvm::Type* llvmType(::llvm::LLVMContext& context, const lir::ValueType& carrier) noexcept {
  if (carrier.kind() == lir::ValueTypeKind::Pointer) {
    return ::llvm::PointerType::get(context, carrier.pointerAddressSpace());
  }
  if (carrier.kind() == lir::ValueTypeKind::Unit) { return ::llvm::Type::getVoidTy(context); }
  if (carrier.kind() == lir::ValueTypeKind::Float) {
    if (carrier.floatFormat() == lir::FloatFormat::Binary32) {
      return ::llvm::Type::getFloatTy(context);
    }
    return ::llvm::Type::getDoubleTy(context);
  }
  if (carrier.kind() == lir::ValueTypeKind::Aggregate) {
    zc::Vector<::llvm::Type*> fieldTypes;
    for (const auto& field : carrier.aggregateFields()) {
      fieldTypes.add(llvmType(context, field.type));
    }
    ::llvm::ArrayRef<::llvm::Type*> fieldRef(fieldTypes.begin(), fieldTypes.size());
    return ::llvm::StructType::get(context, fieldRef);
  }
  return integerType(context, carrier.integerWidth());
}

/// \brief Lowers one constant operand to its LLVM constant.
///
/// An aggregate constant lowers to a literal ConstantStruct whose elements are
/// the lowered field constants, recursing for a nested aggregate. The carrier
/// is guaranteed aggregate by AggregateConstant::from, so the llvmType result
/// is a StructType.
::llvm::Constant* lowerConstant(::llvm::LLVMContext& context,
                                const lir::Operand& operand) noexcept {
  if (operand.isFloatConstant()) {
    const auto& floatConstant = operand.floatConstantValue();
    const bool isBinary32 = floatConstant.carrier().floatFormat() == lir::FloatFormat::Binary32;
    ::llvm::APFloat apf(isBinary32 ? ::llvm::APFloat::IEEEsingle() : ::llvm::APFloat::IEEEdouble(),
                        ::llvm::APInt(isBinary32 ? 32 : 64, floatConstant.bits()));
    return ::llvm::ConstantFP::get(context, apf);
  }
  if (operand.isAggregateConstant()) {
    const auto& aggregateConstant = operand.aggregateConstantValue();
    zc::Vector<::llvm::Constant*> fieldValues;
    for (const auto& field : aggregateConstant.fields()) {
      fieldValues.add(lowerConstant(context, field));
    }
    ::llvm::ArrayRef<::llvm::Constant*> fieldRef(fieldValues.begin(), fieldValues.size());
    return ::llvm::ConstantStruct::get(
        ::llvm::cast<::llvm::StructType>(llvmType(context, aggregateConstant.carrier())), fieldRef);
  }
  auto* type = integerType(context, operand.constantValue().carrier().integerWidth());
  return ::llvm::ConstantInt::get(type, operand.constantValue().bits(), /*IsSigned=*/false);
}

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

LlvmTranslationResult LlvmTranslator::translate(const lir::Module& module) {
  const auto functions = module.functions();
  if (functions.size() < 1 || functions.size() > 3) {
    return LlvmTranslationResult::failure(
        zc::heapString("LIR module must contain one, two, or three functions in this slice"));
  }
  for (const auto& candidate : functions) {
    if (candidate.returnCarrier().kind() != lir::ValueTypeKind::Integer &&
        candidate.returnCarrier().kind() != lir::ValueTypeKind::Unit &&
        candidate.returnCarrier().kind() != lir::ValueTypeKind::Pointer &&
        candidate.returnCarrier().kind() != lir::ValueTypeKind::Aggregate) {
      return LlvmTranslationResult::failure(zc::heapString(
          "LIR function return carrier must be an integer, unit, pointer, or aggregate"));
    }
  }
  // Each function is one of: the single-block integer-constant return
  // (scalar-initializer / callee shape) or a generic multi-block control-flow
  // function (conditional diamond, reducible while-loop, comparison-driven
  // conditional, or the two-block Call+Return caller) whose entry begins with a
  // Goto, a conditional branch, or a call.
  auto isSupportedShape = [](const lir::Function& candidate) -> bool {
    const auto candidateBlocks = candidate.blocks();
    // A multi-slot aggregate return is lowered only as a single entry block that
    // returns the bundle directly (mirroring the single-block scalar return). A
    // ReturnAggregate in any other position is not lowered in this slice, so
    // reject it before translation begins and the body emitter never reaches it.
    // The same single-block-only constraint applies to ReturnString: the body
    // emitter handles it only in the entry block of a one-block function.
    for (size_t index = 0; index < candidateBlocks.size(); ++index) {
      const auto terminatorKind = candidateBlocks[index].terminator().kind();
      if (terminatorKind == lir::TerminatorKind::ReturnAggregate ||
          terminatorKind == lir::TerminatorKind::ReturnString) {
        if (index != 0 || candidateBlocks.size() != 1) { return false; }
      }
    }
    if (candidateBlocks.size() == 1) {
      // A single-block function either returns an integer constant (scalar
      // initializer / constant-return callee), returns a local slot (a
      // one-parameter callee that returns its parameter), returns a multi-slot
      // aggregate bundle, or returns a string constant pointer.
      const auto kind = candidateBlocks[0].terminator().kind();
      return kind == lir::TerminatorKind::ReturnInteger ||
             kind == lir::TerminatorKind::ReturnLocal ||
             kind == lir::TerminatorKind::ReturnAggregate ||
             kind == lir::TerminatorKind::ReturnString || kind == lir::TerminatorKind::ReturnVoid;
    }
    if (candidateBlocks.size() < 2) { return false; }
    const auto entryKind = candidateBlocks[0].terminator().kind();
    return entryKind == lir::TerminatorKind::CondBranch || entryKind == lir::TerminatorKind::Goto ||
           entryKind == lir::TerminatorKind::Call;
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
  //    Carrier-to-LLVM-type mapping and constant lowering live in the anonymous
  //    namespace helpers above (llvmType / lowerConstant), which recurse through
  //    nested aggregate fields and so cannot be local lambdas.
  // The LLVM return type of one LIR function. A single-block ReturnAggregate
  // returns a literal struct whose element types are the slot carriers in slot
  // order (RFC 0021 carrier bundle); every other shape returns its scalar integer
  // carrier. The struct is an LLVM literal aggregate, not an LIR SSA value type.
  auto aggregateReturnSlots =
      [](const lir::Function& candidate) -> zc::Maybe<zc::ArrayPtr<const lir::IntegerConstant>> {
    const auto candidateBlocks = candidate.blocks();
    if (candidateBlocks.size() == 1 &&
        candidateBlocks[0].terminator().kind() == lir::TerminatorKind::ReturnAggregate) {
      return candidateBlocks[0].terminator().returnAggregateSlots();
    }
    return zc::none;
  };
  auto functionReturnType = [&](const lir::Function& candidate) -> ::llvm::Type* {
    ZC_IF_SOME(slots, aggregateReturnSlots(candidate)) {
      zc::Vector<::llvm::Type*> elementTypes(slots.size());
      for (const auto& slot : slots) {
        elementTypes.add(integerType(*context, slot.carrier().integerWidth()));
      }
      ::llvm::ArrayRef<::llvm::Type*> elementRef(elementTypes.begin(), elementTypes.size());
      return ::llvm::StructType::get(*context, elementRef);
    }
    if (candidate.returnCarrier().kind() == lir::ValueTypeKind::Unit) {
      return ::llvm::Type::getVoidTy(*context);
    }
    if (candidate.returnCarrier().kind() == lir::ValueTypeKind::Pointer) {
      return ::llvm::PointerType::get(*context, candidate.returnCarrier().pointerAddressSpace());
    }
    if (candidate.returnCarrier().kind() == lir::ValueTypeKind::Aggregate) {
      return llvmType(*context, candidate.returnCarrier());
    }
    return integerType(*context, candidate.returnCarrier().integerWidth());
  };
  zc::Vector<::llvm::Function*> llvmFunctions;
  for (const auto& candidate : functions) {
    ::llvm::Type* candidateReturn = functionReturnType(candidate);
    zc::Vector<::llvm::Type*> paramTypes;
    for (const auto& parameter : candidate.parameters()) {
      paramTypes.add(llvmType(*context, parameter.carrier()));
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

    ::llvm::IntegerType* returnType =
        function.returnCarrier().kind() == lir::ValueTypeKind::Integer
            ? integerType(*context, function.returnCarrier().integerWidth())
            : nullptr;

    if (blocks.size() == 1 && blocks[0].terminator().kind() == lir::TerminatorKind::ReturnInteger) {
      // Single entry block returning the integer constant.
      ::llvm::BasicBlock* entryBlock = ::llvm::BasicBlock::Create(*context, "entry", llvmFunction);
      const auto& returnConstant = blocks[0].terminator().returnIntegerValue();
      ::llvm::Constant* value =
          ::llvm::ConstantInt::get(returnType, returnConstant.bits(), /*IsSigned=*/false);
      ::llvm::ReturnInst::Create(*context, value, entryBlock);
      continue;
    }

    if (blocks.size() == 1 &&
        blocks[0].terminator().kind() == lir::TerminatorKind::ReturnAggregate) {
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
        auto* slotType = integerType(*context, returnSlots[slotIndex].carrier().integerWidth());
        ::llvm::Constant* slotValue =
            ::llvm::ConstantInt::get(slotType, returnSlots[slotIndex].bits(), /*IsSigned=*/false);
        aggregate =
            ::llvm::InsertValueInst::Create(aggregate, slotValue, {slotIndex}, "agg", entryBlock);
      }
      ::llvm::ReturnInst::Create(*context, aggregate, entryBlock);
      continue;
    }

    if (blocks.size() == 1 && blocks[0].terminator().kind() == lir::TerminatorKind::ReturnString) {
      // Single entry block returning a pointer to a null-terminated global
      // string constant. The UTF-8 bytes are emitted as a private
      // ConstantDataArray (with a trailing null) and the function returns its
      // address; the function's declared return type is an opaque pointer.
      ::llvm::BasicBlock* entryBlock = ::llvm::BasicBlock::Create(*context, "entry", llvmFunction);
      const auto bytes = blocks[0].terminator().returnStringValue().bytes();
      const char* data = bytes.size() == 0 ? "" : reinterpret_cast<const char*>(bytes.begin());
      ::llvm::StringRef stringRef(data, bytes.size());
      ::llvm::Constant* stringArray =
          ::llvm::ConstantDataArray::getString(*context, stringRef, /*AddNull=*/true);
      auto* global = new ::llvm::GlobalVariable(
          *llvmModule, stringArray->getType(),
          /*isConstant=*/true, ::llvm::GlobalValue::PrivateLinkage, stringArray, ".str");
      ::llvm::ReturnInst::Create(*context, global, entryBlock);
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
    auto slotType = [&](uint32_t ordinal) -> ::llvm::Type* {
      for (const auto& parameter : parameters) {
        if (parameter.ordinal() == ordinal) { return llvmType(*context, parameter.carrier()); }
      }
      for (const auto& local : locals) {
        if (local.ordinal() == ordinal) { return llvmType(*context, local.carrier()); }
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

    auto loadOperand = [&](const lir::Operand& operand,
                           ::llvm::BasicBlock* target) -> ::llvm::Value* {
      if (operand.isConstant()) { return lowerConstant(*context, operand); }
      auto* slot = slotFor(operand.localOrdinal());
      return new ::llvm::LoadInst(slot->getAllocatedType(), slot, "use", target);
    };

    // Pass 2: emit each block's statements then its terminator.
    for (size_t index = 0; index < blocks.size(); ++index) {
      const auto& source = blocks[index];
      ::llvm::BasicBlock* target = llvmBlocks[index];
      for (const auto& statement : source.statements()) {
        if (statement.kind() == lir::StatementKind::StoreField) {
          // Load the receiver pointer held by the base slot, then store the
          // value operand through it. The admitted one-field owner is one
          // scalar at offset zero; a non-zero offset steps byte-wise over the
          // opaque pointee with an i8 GEP. This statement has no destination
          // slot.
          auto* baseSlot = slotFor(statement.basePointerOrdinal());
          auto* fieldPointer =
              new ::llvm::LoadInst(baseSlot->getAllocatedType(), baseSlot, "fieldptr", target);
          ::llvm::Value* fieldAddress = fieldPointer;
          if (statement.fieldOffsetBytes() != 0) {
            ::llvm::Value* offset =
                ::llvm::ConstantInt::getSigned(::llvm::Type::getInt64Ty(*context),
                                               static_cast<int64_t>(statement.fieldOffsetBytes()));
            ::llvm::ArrayRef<::llvm::Value*> indices(offset);
            fieldAddress = ::llvm::GetElementPtrInst::Create(
                ::llvm::Type::getInt8Ty(*context), fieldPointer, indices, "fieldgep", target);
          }
          ::llvm::Value* written = loadOperand(statement.storedValue(), target);
          new ::llvm::StoreInst(written, fieldAddress, /*isVolatile=*/false, target);
          continue;
        }
        auto* destination = slotFor(statement.destinationOrdinal());
        ::llvm::Value* stored = nullptr;
        if (statement.kind() == lir::StatementKind::Compare) {
          ::llvm::Value* left = loadOperand(statement.left(), target);
          ::llvm::Value* right = loadOperand(statement.right(), target);
          // Float operands select ordered FCmp predicates (ZOM float
          // comparisons are total-order); integer operands use signed ICmp.
          if (left->getType()->isFloatingPointTy()) {
            ::llvm::CmpInst::Predicate predicate = ::llvm::CmpInst::FCMP_OEQ;
            switch (statement.comparisonOp()) {
              case lir::ComparisonOp::Eq:
                predicate = ::llvm::CmpInst::FCMP_OEQ;
                break;
              case lir::ComparisonOp::Ne:
                predicate = ::llvm::CmpInst::FCMP_ONE;
                break;
              case lir::ComparisonOp::Lt:
                predicate = ::llvm::CmpInst::FCMP_OLT;
                break;
              case lir::ComparisonOp::Le:
                predicate = ::llvm::CmpInst::FCMP_OLE;
                break;
              case lir::ComparisonOp::Gt:
                predicate = ::llvm::CmpInst::FCMP_OGT;
                break;
              case lir::ComparisonOp::Ge:
                predicate = ::llvm::CmpInst::FCMP_OGE;
                break;
            }
            stored = ::llvm::CmpInst::Create(::llvm::Instruction::FCmp, predicate, left, right,
                                             "cmp", target);
          } else {
            ::llvm::CmpInst::Predicate predicate = ::llvm::CmpInst::ICMP_EQ;
            switch (statement.comparisonOp()) {
              case lir::ComparisonOp::Eq:
                predicate = ::llvm::CmpInst::ICMP_EQ;
                break;
              case lir::ComparisonOp::Ne:
                predicate = ::llvm::CmpInst::ICMP_NE;
                break;
              case lir::ComparisonOp::Lt:
                predicate = ::llvm::CmpInst::ICMP_SLT;
                break;
              case lir::ComparisonOp::Le:
                predicate = ::llvm::CmpInst::ICMP_SLE;
                break;
              case lir::ComparisonOp::Gt:
                predicate = ::llvm::CmpInst::ICMP_SGT;
                break;
              case lir::ComparisonOp::Ge:
                predicate = ::llvm::CmpInst::ICMP_SGE;
                break;
            }
            stored = ::llvm::CmpInst::Create(::llvm::Instruction::ICmp, predicate, left, right,
                                             "cmp", target);
          }
        } else if (statement.kind() == lir::StatementKind::Arithmetic) {
          ::llvm::Value* left = loadOperand(statement.left(), target);
          ::llvm::Value* right = loadOperand(statement.right(), target);
          ::llvm::Instruction::BinaryOps opcode = ::llvm::Instruction::Add;
          if (left->getType()->isFloatingPointTy()) {
            // Float operands select IEEE-754 arithmetic opcodes; the LIR
            // verifier admits only Add, Sub, Mul, Div, Rem for float carriers.
            switch (statement.arithmeticOp()) {
              case lir::ArithmeticOp::Add:
                opcode = ::llvm::Instruction::FAdd;
                break;
              case lir::ArithmeticOp::Sub:
                opcode = ::llvm::Instruction::FSub;
                break;
              case lir::ArithmeticOp::Mul:
                opcode = ::llvm::Instruction::FMul;
                break;
              case lir::ArithmeticOp::Div:
                opcode = ::llvm::Instruction::FDiv;
                break;
              case lir::ArithmeticOp::Rem:
                opcode = ::llvm::Instruction::FRem;
                break;
              default:
                return LlvmTranslationResult::failure(
                    zc::str("LLVM translation rejected a non-float arithmetic operation on float "
                            "operands"));
            }
          } else {
            switch (statement.arithmeticOp()) {
              case lir::ArithmeticOp::Add:
                opcode = ::llvm::Instruction::Add;
                break;
              case lir::ArithmeticOp::Sub:
                opcode = ::llvm::Instruction::Sub;
                break;
              case lir::ArithmeticOp::Mul:
                opcode = ::llvm::Instruction::Mul;
                break;
              // Signed division and remainder mirror the signed comparison
              // predicates while the LIR integer carrier has no signedness.
              case lir::ArithmeticOp::Div:
                opcode = ::llvm::Instruction::SDiv;
                break;
              case lir::ArithmeticOp::Rem:
                opcode = ::llvm::Instruction::SRem;
                break;
              case lir::ArithmeticOp::Shl:
                opcode = ::llvm::Instruction::Shl;
                break;
              case lir::ArithmeticOp::Shr:
                opcode = ::llvm::Instruction::AShr;
                break;
              case lir::ArithmeticOp::UShr:
                opcode = ::llvm::Instruction::LShr;
                break;
              case lir::ArithmeticOp::BitAnd:
                opcode = ::llvm::Instruction::And;
                break;
              case lir::ArithmeticOp::BitOr:
                opcode = ::llvm::Instruction::Or;
                break;
              case lir::ArithmeticOp::BitXor:
                opcode = ::llvm::Instruction::Xor;
                break;
              case lir::ArithmeticOp::Pow:
                // No integer machine operation exists for exponentiation; the
                // admitted lowering shapes never emit it.
                return LlvmTranslationResult::failure(
                    zc::str("LLVM translation rejected an unsupported arithmetic operation: Pow"));
            }
          }
          stored = ::llvm::BinaryOperator::Create(opcode, left, right, "arith", target);
        } else if (statement.kind() == lir::StatementKind::TakeAddress) {
          // The address of a whole alloca slot is the alloca pointer itself in
          // opaque-pointer LLVM; the folded owner slot is one scalar, so no GEP
          // is needed.
          stored = slotFor(statement.sourceOrdinal());
        } else if (statement.kind() == lir::StatementKind::LoadField) {
          // Load the receiver pointer held by the base slot, then load the field
          // through it. The admitted one-field owner is one scalar at offset
          // zero, so the read is a direct load; a non-zero offset would step
          // byte-wise over the opaque pointee with an i8 GEP.
          auto* baseSlot = slotFor(statement.basePointerOrdinal());
          auto* fieldPointer =
              new ::llvm::LoadInst(baseSlot->getAllocatedType(), baseSlot, "fieldptr", target);
          if (statement.fieldOffsetBytes() != 0) {
            ::llvm::Value* offset =
                ::llvm::ConstantInt::getSigned(::llvm::Type::getInt64Ty(*context),
                                               static_cast<int64_t>(statement.fieldOffsetBytes()));
            ::llvm::ArrayRef<::llvm::Value*> indices(offset);
            ::llvm::Value* fieldAddress = ::llvm::GetElementPtrInst::Create(
                ::llvm::Type::getInt8Ty(*context), fieldPointer, indices, "fieldgep", target);
            stored = new ::llvm::LoadInst(destination->getAllocatedType(), fieldAddress, "field",
                                          target);
          } else {
            stored = new ::llvm::LoadInst(destination->getAllocatedType(), fieldPointer, "field",
                                          target);
          }
        } else if (statement.kind() == lir::StatementKind::ExtractField) {
          // Load the aggregate-typed source slot, then extract one field at
          // its zero-based index. The destination carrier matches the field
          // type (LIR-verifier validated), so the extracted value's LLVM type
          // matches the destination slot.
          auto* sourceSlot = slotFor(statement.sourceOrdinal());
          auto* aggregate =
              new ::llvm::LoadInst(sourceSlot->getAllocatedType(), sourceSlot, "agg", target);
          stored = ::llvm::ExtractValueInst::Create(aggregate, {statement.fieldIndex()}, "field",
                                                    target);
        } else if (statement.kind() == lir::StatementKind::InsertField) {
          // Load the aggregate-typed slot, insert the stored value at the
          // field index, producing a new aggregate in the destination. The
          // aggregate and destination share one carrier, and the inserted
          // value's carrier matches the field type (LIR-verifier validated).
          auto* aggregateSlot = slotFor(statement.sourceOrdinal());
          auto* aggregate =
              new ::llvm::LoadInst(aggregateSlot->getAllocatedType(), aggregateSlot, "agg", target);
          ::llvm::Value* fieldValue = loadOperand(statement.storedValue(), target);
          stored = ::llvm::InsertValueInst::Create(aggregate, fieldValue, {statement.fieldIndex()},
                                                   "ins", target);
        } else {
          stored = loadOperand(statement.value(), target);
        }
        new ::llvm::StoreInst(stored, destination, /*isVolatile=*/false, target);
      }
      const auto& terminator = source.terminator();
      switch (terminator.kind()) {
        case lir::TerminatorKind::Goto:
          ::llvm::BranchInst::Create(blockFor(terminator.gotoTarget()), target);
          break;
        case lir::TerminatorKind::CondBranch: {
          auto* conditionSlot = slotFor(terminator.conditionOrdinal());
          auto* condition = new ::llvm::LoadInst(conditionSlot->getAllocatedType(), conditionSlot,
                                                 "cond", target);
          ::llvm::BranchInst::Create(blockFor(terminator.condTrueTarget()),
                                     blockFor(terminator.condFalseTarget()), condition, target);
          break;
        }
        case lir::TerminatorKind::Call: {
          // Call a module-local defined function, passing zero, one, or a bounded
          // vector of integer arguments (each a constant or a load of a declared
          // slot); store the integer result into the destination slot, then
          // branch to the normal target.
          ::llvm::Function* callee = llvmFunctions[terminator.calleeIndex()];
          zc::Vector<::llvm::Value*> callArgs;
          for (const auto& argument : terminator.callArguments()) {
            callArgs.add(loadOperand(argument, target));
          }
          ::llvm::ArrayRef<::llvm::Value*> callArgsRef(callArgs.begin(), callArgs.size());
          if (terminator.hasCallDestination()) {
            auto* callResult = ::llvm::CallInst::Create(callee->getFunctionType(), callee,
                                                        callArgsRef, "call", target);
            auto* destination = slotFor(terminator.callDestinationOrdinal());
            new ::llvm::StoreInst(callResult, destination, /*isVolatile=*/false, target);
          } else {
            // A unit-returning call's result is discarded; emit the call with
            // no SSA name and store nothing.
            ::llvm::CallInst::Create(callee->getFunctionType(), callee, callArgsRef,
                                     /*NameStr=*/"", target);
          }
          ::llvm::BranchInst::Create(blockFor(terminator.callNormalTarget()), target);
          break;
        }
        case lir::TerminatorKind::ReturnLocal: {
          auto* slot = slotFor(terminator.returnLocalOrdinal());
          auto* loaded = new ::llvm::LoadInst(slot->getAllocatedType(), slot, "value", target);
          ::llvm::ReturnInst::Create(*context, loaded, target);
          break;
        }
        case lir::TerminatorKind::ReturnInteger: {
          ::llvm::Constant* value = ::llvm::ConstantInt::get(
              returnType, terminator.returnIntegerValue().bits(), /*IsSigned=*/false);
          ::llvm::ReturnInst::Create(*context, value, target);
          break;
        }
        case lir::TerminatorKind::ReturnAggregate:
          // A multi-slot aggregate return is not lowered in this slice; the
          // shape gate above (isSupportedShape) rejects a function carrying it
          // before translation reaches here, so this arm is unreachable. The
          // literal-struct lowering is the next RFC 0021 step.
          ZC_UNREACHABLE;
        case lir::TerminatorKind::ReturnString:
          // A string constant return is lowered only as a single entry block;
          // the shape gate above (isSupportedShape) rejects a multi-block
          // function carrying it before translation reaches here.
          ZC_UNREACHABLE;
        case lir::TerminatorKind::ReturnVoid:
          ::llvm::ReturnInst::Create(*context, target);
          break;
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
