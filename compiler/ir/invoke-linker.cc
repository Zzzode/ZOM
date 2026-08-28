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

#include "compiler/ir/invoke-linker.h"

#include "zc/core/debug.h"
#include "zc/core/subprocess.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ir {

// =======================================================================================
// LinkerInvocationResult

LinkerInvocationResult LinkerInvocationResult::forExecutable(zc::Array<uint8_t>&& executableBytes) {
  return LinkerInvocationResult(true, zc::mv(executableBytes),
                                LinkerInvocationFailure::DriverNotSpawnable, 0,
                                zc::Array<uint8_t>());
}

LinkerInvocationResult LinkerInvocationResult::forFailure(LinkerInvocationFailure reason,
                                                          int exitCode,
                                                          zc::Array<uint8_t>&& capturedStderr) {
  return LinkerInvocationResult(false, zc::Array<uint8_t>(), reason, exitCode,
                                zc::mv(capturedStderr));
}

// =======================================================================================
// invokeLinker

namespace {

// Copies a captured byte view into an owned array.
zc::Array<uint8_t> ownBytes(zc::ArrayPtr<const zc::byte> view) {
  auto owned = zc::heapArray<uint8_t>(view.size());
  for (size_t index = 0; index < view.size(); ++index) { owned[index] = view[index]; }
  return owned;
}

}  // namespace

LinkerInvocationResult invokeLinker(const LinkerInvocation& invocation,
                                    const zc::ReadableDirectory& outputDir,
                                    zc::StringPtr outputRelativePath) {
  // Build the shell-free command from the canonical invocation. argv[0] is the
  // driver name; the remaining tokens are passed verbatim. The environment is
  // exactly the invocation's explicit set (empty policy plus its pairs); the
  // parent environment is never inherited.
  zc::SubprocessCommand command(invocation.program());
  command.envPolicy(zc::SubprocessEnvPolicy::Empty);
  command.cwd(invocation.workingDirectory());

  zc::ArrayPtr<const zc::String> argv = invocation.argv();
  // Skip argv[0]: SubprocessCommand sets it from the program path. Override it
  // to match the invocation's argv[0] so the child sees the exact vector.
  if (argv.size() >= 1) { command.argv0(argv[0]); }
  for (size_t index = 1; index < argv.size(); ++index) { command.arg(argv[index]); }

  zc::ArrayPtr<const zc::String> environment = invocation.environment();
  for (size_t index = 0; index + 1 < environment.size(); index += 2) {
    command.env(environment[index], environment[index + 1]);
  }

  zc::SubprocessResult result = command.run();

  if (!result.spawned()) {
    return LinkerInvocationResult::forFailure(LinkerInvocationFailure::DriverNotSpawnable, 0,
                                              zc::Array<uint8_t>());
  }

  const zc::SubprocessOutput& output = result.output();
  if (output.terminationKind == zc::SubprocessTerminationKind::Signaled) {
    return LinkerInvocationResult::forFailure(LinkerInvocationFailure::DriverSignaled, 0,
                                              ownBytes(output.capturedStderr.asPtr()));
  }
  if (output.code != 0) {
    return LinkerInvocationResult::forFailure(LinkerInvocationFailure::DriverExitedNonZero,
                                              output.code, ownBytes(output.capturedStderr.asPtr()));
  }

  // The driver exited successfully; the planned output must now exist.
  zc::Path outputPath = zc::Path::parse(outputRelativePath);
  ZC_IF_SOME(file, outputDir.tryOpenFile(outputPath)) {
    zc::Array<zc::byte> bytes = file->readAllBytes();
    return LinkerInvocationResult::forExecutable(ownBytes(bytes.asPtr()));
  }
  return LinkerInvocationResult::forFailure(LinkerInvocationFailure::OutputMissing, 0,
                                            zc::Array<uint8_t>());
}

}  // namespace zomlang::compiler::ir
