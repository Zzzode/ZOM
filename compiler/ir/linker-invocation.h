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

#pragma once

#include "compiler/ir/link-plan-codec.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"

namespace zomlang::compiler::ir {

/// \brief The complete, deterministic invocation a verified link plan expands to.
///
/// RFC 0043 "Linker Driver Invocation": a `VerifiedLinkPlan` expands to an
/// ordered argument vector, a working directory, and an explicit environment,
/// with no shell, no glob expansion, no response files, and no inherited search
/// variables. This value is exactly that expansion - the input a shell-free
/// child-process spawn consumes - and nothing more. It performs no I/O and holds
/// no session state.
class LinkerInvocation final {
public:
  LinkerInvocation(LinkerInvocation&&) noexcept = default;
  LinkerInvocation& operator=(LinkerInvocation&&) noexcept = default;
  ZC_DISALLOW_COPY(LinkerInvocation);
  ~LinkerInvocation() noexcept = default;

  /// \brief Builds an invocation directly from its parts.
  ///
  /// The `argv[0]` element must equal the program name the child sees. This is
  /// the low-level constructor the plan expansion and callers that already hold
  /// a resolved program/argv use.
  ZC_NODISCARD static LinkerInvocation forProgram(zc::StringPtr program,
                                                  zc::Array<zc::String>&& argv,
                                                  zc::StringPtr workingDirectory,
                                                  zc::Array<zc::String>&& environment);

  /// \brief The absolute path of the linker driver program to execute.
  ZC_NODISCARD zc::StringPtr program() const noexcept { return programValue; }

  /// \brief The complete argument vector. `argv[0]` is the driver program name;
  /// the remainder is the RFC 0043 canonical expansion order.
  ZC_NODISCARD zc::ArrayPtr<const zc::String> argv() const noexcept { return argvValues.asPtr(); }

  /// \brief The normalized working directory the driver runs in (the output
  /// directory), so no input path resolves against the current directory.
  ZC_NODISCARD zc::StringPtr workingDirectory() const noexcept { return workingDirectoryValue; }

  /// \brief The explicit environment entries (NAME, VALUE) pairs, flattened as
  /// [name0, value0, name1, value1, ...]. Empty means an empty environment.
  ZC_NODISCARD zc::ArrayPtr<const zc::String> environment() const noexcept {
    return environmentValues.asPtr();
  }

private:
  friend zc::Maybe<LinkerInvocation> expandLinkPlanToInvocation(const VerifiedLinkPlan& plan);

  LinkerInvocation(zc::String&& program, zc::Array<zc::String>&& argv,
                   zc::String&& workingDirectory, zc::Array<zc::String>&& environment) noexcept
      : programValue(zc::mv(program)),
        argvValues(zc::mv(argv)),
        workingDirectoryValue(zc::mv(workingDirectory)),
        environmentValues(zc::mv(environment)) {}

  zc::String programValue;
  zc::Array<zc::String> argvValues;
  zc::String workingDirectoryValue;
  zc::Array<zc::String> environmentValues;
};

/// \brief Expands a verified link plan into its canonical driver invocation.
///
/// RFC 0043: builds the ordered argument vector deterministically from the plan
/// - the driver program as `argv[0]`, `-o <outputPath>`, the entry-symbol flag,
/// the plan's target-owned argument records, then the object and runtime input
/// paths in their canonical order. The working directory is the output file's
/// parent directory. The environment is empty plus the closure's recorded
/// variables (none in the current closure shape). It spawns no process and reads
/// no filesystem.
///
/// \param plan The verified link plan to expand.
/// \return The canonical invocation, or none when the entry symbol is not valid
///         UTF-8 text or the output path has no parent directory.
ZC_NODISCARD zc::Maybe<LinkerInvocation> expandLinkPlanToInvocation(const VerifiedLinkPlan& plan);

}  // namespace zomlang::compiler::ir
