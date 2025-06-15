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

#ifndef COMPILER_OPTS_H
#define COMPILER_OPTS_H

#include "zc/core/string.h"
#include "zomlang/compiler/ast/dumper.h"

namespace zomlang {
namespace compiler {
namespace basic {

/// \brief Configuration options for the compiler frontend and backend.
/// This class encapsulates all compiler-specific options that control
/// compilation behavior, separate from language-specific options.
struct CompilerOptions {
  /// \brief Output and emission options
  struct EmissionOptions {
    enum class Type {
      AST,
      IR,
      Binary,
    };

    /// Whether to dump AST to stdout (deprecated, use outputType == Type::AST instead)
    bool dumpASTEnabled = false;
    /// Format for AST dumping
    ast::DumpFormat dumpFormat = ast::DumpFormat::kText;
    /// Output file path
    zc::String outputPath;
    /// Emission type
    Type outputType = Type::Binary;
    /// Syntax only compilation
    bool syntaxOnly = false;

    EmissionOptions() = default;
  };

  /// \brief Optimization options
  struct OptimizationOptions {
    /// Optimization level (0-3)
    int level = 0;
    /// Enable debug information
    bool enableDebugInfo = false;

    OptimizationOptions() = default;
  };

  /// \brief Diagnostic options
  struct DiagnosticOptions {
    /// Treat warnings as errors
    bool warningsAsErrors = false;
    /// Maximum number of errors before stopping
    int maxErrors = 20;

    DiagnosticOptions() = default;
  };

  EmissionOptions emission;
  OptimizationOptions optimization;
  DiagnosticOptions diagnostics;

  CompilerOptions() = default;
};

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang

#endif  // COMPILER_OPTS_H
