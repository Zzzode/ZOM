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

#include "zomlang/compiler/driver/driver.h"

#include "zc/core/filesystem.h"
#include "zc/core/map.h"
#include "zc/core/mutex.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/basic/compiler-opts.h"
#include "zomlang/compiler/basic/frontend.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/consoling-diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/compiler/type/type-env.h"

namespace zomlang {
namespace compiler {
namespace driver {

// ================================================================================
// CompilerDriver::Impl

struct CompilerDriver::Impl {
  Impl(const basic::LangOptions& opts, const basic::CompilerOptions& compOpts) noexcept
      : langOpts(opts),
        compilerOpts(compOpts),
        stringPool(zc::heap<basic::StringPool>()),
        sourceManager(zc::heap<source::SourceManager>(*stringPool)),
        diagnosticEngine(zc::heap<diagnostics::DiagnosticEngine>(*sourceManager)),
        symbolTable(zc::heap<symbol::SymbolTable>()) {
    diagnosticEngine->addConsumer(zc::heap<diagnostics::ConsolingDiagnosticConsumer>());
  }
  ~Impl() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(Impl);

  struct OutputDirective {
    zc::ArrayPtr<zc::byte> name;
    zc::Maybe<zc::Path> dir;

    ZC_DISALLOW_COPY(OutputDirective);
    OutputDirective(OutputDirective&&) noexcept = default;
    OutputDirective(const zc::ArrayPtr<zc::byte> name, zc::Maybe<zc::Path> dir)
        : name(name), dir(zc::mv(dir)) {}
  };

  /// Language options
  const basic::LangOptions& langOpts;
  /// Compiler options
  const basic::CompilerOptions& compilerOpts;
  /// String pool to manage interned strings.
  zc::Own<basic::StringPool> stringPool;
  /// Source manager to manage source files.
  zc::Own<source::SourceManager> sourceManager;
  /// Diagnostic engine to report diagnostics.
  zc::Own<diagnostics::DiagnosticEngine> diagnosticEngine;
  /// Symbol table to manage symbols and scopes.
  zc::Own<symbol::SymbolTable> symbolTable;
  /// Mutex-guarded map from BufferId to parsed syntax tree.
  zc::MutexGuarded<zc::HashMap<source::BufferId, ast::Tree>> astMutex;
  /// Mutex-guarded map from BufferId to binder metadata side tables.
  zc::MutexGuarded<zc::HashMap<source::BufferId, ast::BindingMetadata>> bindingMetadataMutex;
  /// Mutex-guarded map from BufferId to type environments from type checking.
  zc::MutexGuarded<zc::HashMap<source::BufferId, type::TypeEnv>> typeEnvMutex;
};

// ================================================================================
// CompilerDriver

CompilerDriver::CompilerDriver(const basic::LangOptions& langOpts,
                               const basic::CompilerOptions& compilerOpts) noexcept
    : impl(zc::heap<Impl>(langOpts, compilerOpts)) {}
CompilerDriver::~CompilerDriver() noexcept(false) = default;

zc::Maybe<source::BufferId> CompilerDriver::addSourceFile(const zc::StringPtr file) {
  const zc::Maybe<source::BufferId> bufferId =
      impl->sourceManager->getFileSystemSourceBufferID(file);
  if (bufferId == zc::none) {
    impl->diagnosticEngine->diagnose<diagnostics::DiagID::InvalidPath>(source::SourceLoc(), file);
  }
  return bufferId;
}

const diagnostics::DiagnosticEngine& CompilerDriver::getDiagnosticEngine() const {
  return *impl->diagnosticEngine;
}

diagnostics::DiagnosticEngine& CompilerDriver::getDiagnosticEngine() {
  return *impl->diagnosticEngine;
}

const zc::HashMap<source::BufferId, ast::Tree>& CompilerDriver::getASTs() const {
  auto lockedAsts = impl->astMutex.lockShared();
  return *lockedAsts;
}

const zc::HashMap<source::BufferId, ast::BindingMetadata>& CompilerDriver::getBindingMetadata()
    const {
  auto lockedMetadata = impl->bindingMetadataMutex.lockShared();
  return *lockedMetadata;
}

const zc::HashMap<source::BufferId, type::TypeEnv>& CompilerDriver::getTypeEnvs() const {
  auto lockedTypeEnvs = impl->typeEnvMutex.lockShared();
  return *lockedTypeEnvs;
}

bool CompilerDriver::parseSources() {
  // Get BufferIds directly from SourceManager
  zc::Vector<source::BufferId> bufferIds = impl->sourceManager->getManagedBufferIds();

  basic::ThreadPool threadPool;

  for (const source::BufferId& bufferId : bufferIds) {  // Iterate over the retrieved vector
    // Create a thread for each buffer ID
    threadPool.enqueue([this, bufferId]() -> void {
      // Perform lexing and parsing for the buffer.
      zc::Maybe<ast::Tree> maybeAst =
          basic::performParse(*impl->sourceManager, *impl->diagnosticEngine, impl->langOpts,
                              *impl->stringPool, bufferId);

      // Store the result if successful
      ZC_IF_SOME(ast, maybeAst) {
        // Lock the mutex to safely access the shared map
        auto lockedAsts = impl->astMutex.lockExclusive();
        // Insert or update the AST in the map
        lockedAsts->upsert(bufferId, zc::mv(ast));
      }
      // Errors during parsing should be reported via the DiagnosticEngine
    });
  }

  // Return true if no errors were reported
  return !impl->diagnosticEngine->hasErrors();
}

bool CompilerDriver::bindSources() {
  auto lockedAsts = impl->astMutex.lockShared();
  auto lockedMetadata = impl->bindingMetadataMutex.lockExclusive();
  for (const auto& entry : *lockedAsts) {
    ast::BindingMetadata metadata;
    basic::performBind(*impl->symbolTable, *impl->diagnosticEngine, entry.value, metadata);
    lockedMetadata->upsert(entry.key, zc::mv(metadata));
  }

  // Return true if no errors were reported
  return !impl->diagnosticEngine->hasErrors();
}

bool CompilerDriver::checkSources() {
  auto lockedAsts = impl->astMutex.lockShared();
  auto lockedMetadata = impl->bindingMetadataMutex.lockShared();
  auto lockedTypeEnvs = impl->typeEnvMutex.lockExclusive();

  for (const auto& entry : *lockedAsts) {
    // Find the corresponding binding metadata
    auto metadataMaybe = lockedMetadata->find(entry.key);
    if (metadataMaybe == zc::none) continue;

    type::TypeEnv typeEnv;
    ZC_IF_SOME(metadata, metadataMaybe) {
      basic::performCheck(*impl->symbolTable, *impl->diagnosticEngine, entry.value, metadata,
                          typeEnv);
      lockedTypeEnvs->upsert(entry.key, zc::mv(typeEnv));
    }
  }

  // Return true if no errors were reported
  return !impl->diagnosticEngine->hasErrors();
}

const symbol::SymbolTable& CompilerDriver::getSymbolTable() const { return *impl->symbolTable; }

basic::StringPool& CompilerDriver::getStringPool() { return *impl->stringPool; }

const basic::StringPool& CompilerDriver::getStringPool() const { return *impl->stringPool; }

const basic::CompilerOptions& CompilerDriver::getCompilerOptions() const {
  return impl->compilerOpts;
}

const source::SourceManager& CompilerDriver::getSourceManager() const {
  return *impl->sourceManager;
}

}  // namespace driver
}  // namespace compiler
}  // namespace zomlang
