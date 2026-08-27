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
  if (functions.size() != 1) {
    return LlvmTranslationResult::failure(
        zc::heapString("LIR module must contain exactly one function in this slice"));
  }
  const auto& function = functions[0];
  if (function.returnCarrier().kind() != lir::LirValueTypeKind::Integer) {
    return LlvmTranslationResult::failure(
        zc::heapString("LIR function return carrier must be an integer"));
  }
  const auto blocks = function.blocks();
  if (blocks.size() != 1) {
    return LlvmTranslationResult::failure(
        zc::heapString("LIR function must contain exactly one block in this slice"));
  }
  const auto& entry = blocks[0];
  if (entry.terminator().kind() != lir::LirTerminatorKind::ReturnInteger) {
    return LlvmTranslationResult::failure(
        zc::heapString("LIR entry block terminator must be an integer return"));
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

  // 3. Create value types in canonical order (the single integer carrier).
  const uint32_t bitCount = bitCountFor(function.returnCarrier().integerWidth());
  ::llvm::IntegerType* returnType = ::llvm::Type::getIntNTy(*context, bitCount);

  // 4. Declare the function in canonical symbol order.
  ::llvm::FunctionType* functionType = ::llvm::FunctionType::get(returnType, /*isVarArg=*/false);
  ::llvm::Function* llvmFunction =
      ::llvm::Function::Create(functionType, ::llvm::GlobalValue::ExternalLinkage,
                               function.symbolName().cStr(), llvmModule.get());

  // 5. Create the single entry block.
  ::llvm::BasicBlock* entryBlock = ::llvm::BasicBlock::Create(*context, "entry", llvmFunction);

  // 7. Translate the terminator: return the integer constant. No undef/poison.
  const auto& returnConstant = entry.terminator().returnIntegerValue();
  ::llvm::Constant* value =
      ::llvm::ConstantInt::get(returnType, returnConstant.bits(), /*IsSigned=*/false);
  ::llvm::ReturnInst::Create(*context, value, entryBlock);

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
