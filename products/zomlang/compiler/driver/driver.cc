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
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/basic/compiler-opts.h"
#include "zomlang/compiler/basic/frontend.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/consoling-diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace driver {

// ================================================================================
// CompilerDriver::Impl

struct CompilerDriver::Impl {
  Impl(const basic::LangOptions& opts, const basic::CompilerOptions& compOpts) noexcept
      : langOpts(opts),
        compilerOpts(compOpts),
        sourceManager(zc::heap<source::SourceManager>()),
        diagnosticEngine(zc::heap<diagnostics::DiagnosticEngine>(*sourceManager)) {
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
  /// Source manager to manage source files.
  zc::Own<source::SourceManager> sourceManager;
  /// Diagnostic engine to report diagnostics.
  zc::Own<diagnostics::DiagnosticEngine> diagnosticEngine;
  /// Mutex-guarded map from BufferId to parsed AST.
  zc::MutexGuarded<zc::HashMap<source::BufferId, zc::Own<ast::Node>>> astMutex;
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

const zc::HashMap<source::BufferId, zc::Own<ast::Node>>& CompilerDriver::getASTs() const {
  auto lockedAsts = impl->astMutex.lockShared();
  return *lockedAsts;
}

bool CompilerDriver::parseSources() {
  // Get BufferIds directly from SourceManager
  zc::Vector<source::BufferId> bufferIds = impl->sourceManager->getManagedBufferIds();

  basic::ThreadPool threadPool;

  for (const source::BufferId& bufferId : bufferIds) {  // Iterate over the retrieved vector
    // Create a thread for each buffer ID
    threadPool.enqueue([this, bufferId]() -> void {
      // Perform lexing and parsing for the buffer.
      zc::Maybe<zc::Own<ast::Node>> maybeAst = basic::performParse(
          *impl->sourceManager, *impl->diagnosticEngine, impl->langOpts, bufferId);

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

const basic::CompilerOptions& CompilerDriver::getCompilerOptions() const {
  return impl->compilerOpts;
}

const source::SourceManager& CompilerDriver::getSourceManager() const {
  return *impl->sourceManager;
}

}  // namespace driver
}  // namespace compiler
}  // namespace zomlang
