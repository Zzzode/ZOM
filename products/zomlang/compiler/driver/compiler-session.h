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

#include "zc/core/map.h"
#include "zc/core/memory.h"
#include "zc/core/mutex.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/basic/compiler-opts.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/identity/brand.h"
#include "zomlang/compiler/identity/semantic-identity-registry-set.h"
#include "zomlang/compiler/type/type-env.h"

namespace zomlang {
namespace compiler {

namespace source {
class BufferId;
class SourceManager;
}  // namespace source

namespace diagnostics {
class DiagnosticEngine;
}  // namespace diagnostics

namespace symbol {
class SymbolTable;
}

namespace basic {
class StringPool;
}  // namespace basic

namespace driver {

class CompilerSession {
public:
  CompilerSession(identity::SemanticContextFactory& contextFactory,
                  const basic::LangOptions& langOpts, const basic::CompilerOptions& compilerOpts);
  ~CompilerSession() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(CompilerSession);

  /// Add a source file to the compiler.
  /// \param file The path to the source file to add
  /// \return The buffer ID of the added file, or none if the file could not be added
  zc::Maybe<source::BufferId> addSourceFile(zc::StringPtr file);

  /// Get the diagnostic engine used by the compiler.
  /// \return A reference to the diagnostic engine
  const diagnostics::DiagnosticEngine& getDiagnosticEngine() const;
  diagnostics::DiagnosticEngine& getDiagnosticEngine();

  /// Parses all added source files into ASTs.
  /// \return True if parsing succeeded without fatal errors, false otherwise.
  bool parseSources();

  /// Binds all parsed ASTs to create symbols and perform semantic analysis.
  /// \return True if binding succeeded without fatal errors, false otherwise.
  bool bindSources();

  /// Type-checks all bound ASTs.
  /// \return True if checking succeeded without fatal errors, false otherwise.
  bool checkSources();

  /// Get the parsed syntax trees.
  /// \return A reference to the map of buffer IDs to syntax trees.
  const zc::HashMap<source::BufferId, ast::Tree>& getASTs() const;

  /// Get binder metadata keyed by buffer ID.
  /// \return A reference to the map of buffer IDs to binder metadata.
  const zc::HashMap<source::BufferId, ast::BindingMetadata>& getBindingMetadata() const;

  /// Return the number of parsed source buffers with a prebinding inventory.
  size_t getDefinitionInventoryCount() const;

  /// Return an owning snapshot of one source buffer's prebinding inventory.
  zc::Maybe<binder::DefinitionInventory> getDefinitionInventory(
      const source::BufferId& buffer) const;

  /// Get type environments keyed by buffer ID.
  /// \return A reference to the map of buffer IDs to type environments.
  const zc::HashMap<source::BufferId, type::TypeEnv>& getTypeEnvs() const;

  /// Get the symbol table used by the compiler.
  /// \return A reference to the symbol table
  const symbol::SymbolTable& getSymbolTable() const;

  /// Get the string pool used by the compiler.
  /// \return A reference to the string pool
  basic::StringPool& getStringPool();
  const basic::StringPool& getStringPool() const;

  /// Get the compiler options used by the session.
  /// \return A reference to the compiler options
  const basic::CompilerOptions& getCompilerOptions() const;

  /// Get the source manager used by the session.
  /// \return A reference to the source manager
  const source::SourceManager& getSourceManager() const;

  /// \brief Returns the process-unique brand owned by this compilation session.
  identity::SemanticContextBrand getSemanticContextBrand() const noexcept;

  /// \brief Returns the sole RFC 0011 registry family owned by this session.
  zc::Maybe<const identity::SemanticIdentityRegistrySet&> getIdentityRegistries() const noexcept;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace driver
}  // namespace compiler
}  // namespace zomlang
