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

#include "compiler/ir/linker-invocation.h"

#include "zc/core/vector.h"

namespace zomlang::compiler::ir {

LinkerInvocation LinkerInvocation::forProgram(zc::StringPtr program, zc::Array<zc::String>&& argv,
                                              zc::StringPtr workingDirectory,
                                              zc::Array<zc::String>&& environment) {
  return LinkerInvocation(zc::str(program), zc::mv(argv), zc::str(workingDirectory),
                          zc::mv(environment));
}

namespace {

// The entry-symbol flag both supported compiler drivers accept as two argv
// tokens (`-e <symbol>`); passing it split avoids any driver-specific `=`
// quoting and keeps each token verbatim.
constexpr zc::StringPtr kEntrySymbolFlag = "-e"_zc;

// The output flag, passed as two argv tokens (`-o <path>`).
constexpr zc::StringPtr kOutputFlag = "-o"_zc;

// Interprets a byte sequence as UTF-8 text, or none when it contains an interior
// NUL (which cannot survive an argv token). The entry symbol is recorded as raw
// bytes; a linker argument must be a C string.
zc::Maybe<zc::String> bytesToArgument(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0) { return zc::none; }
  for (uint8_t b : bytes) {
    if (b == 0) { return zc::none; }
  }
  return zc::heapString(reinterpret_cast<const char*>(bytes.begin()), bytes.size());
}

// Returns the parent directory of a normalized absolute file path, or none when
// the path has no parent segment (a bare "/name" whose parent is the root, or a
// path that is not a normalized absolute path). The input is treated as an
// opaque POSIX string, matching the link-plan path contract, rather than parsed
// through zc::Path (which models relative paths).
zc::Maybe<zc::String> parentDirectory(zc::StringPtr path) {
  if (path.size() == 0 || path[0] != '/') { return zc::none; }
  // Find the last '/'; everything before it is the parent.
  size_t lastSlash = 0;
  bool found = false;
  for (size_t index = 1; index < path.size(); ++index) {
    if (path[index] == '/') {
      lastSlash = index;
      found = true;
    }
  }
  // No interior slash means the parent is the root itself ("/name" -> "/").
  if (!found) { return zc::str("/"); }
  return zc::heapString(path.cStr(), lastSlash);
}

}  // namespace

zc::Maybe<LinkerInvocation> expandLinkPlanToInvocation(const VerifiedLinkPlan& plan) {
  const ToolchainClosureRecord& closure = plan.toolchainClosure();

  zc::Maybe<zc::String> entryArgument = bytesToArgument(plan.entrySymbol());
  if (entryArgument == zc::none) { return zc::none; }

  zc::Maybe<zc::String> workingDirectory = parentDirectory(plan.outputPath());
  if (workingDirectory == zc::none) { return zc::none; }

  // Build the canonical argument vector in RFC 0043 expansion order. The
  // toolchain closure's CRT startup objects precede the user objects, and its
  // default libraries follow every object so left-to-right symbol resolution
  // sees the objects first:
  //   argv[0] = driver program
  //   -o <output>
  //   -e <entry symbol>
  //   <target-owned argument records, in order>
  //   <closure CRT objects, in canonical order>
  //   <object input paths, in canonical order>
  //   <runtime input paths, in canonical order>
  //   <closure default libraries, in canonical order>
  zc::Vector<zc::String> argv;
  argv.add(zc::str(closure.linkerPath()));
  argv.add(zc::str(kOutputFlag));
  argv.add(zc::str(plan.outputPath()));
  argv.add(zc::str(kEntrySymbolFlag));
  argv.add(ZC_REQUIRE_NONNULL(zc::mv(entryArgument)));
  for (const LinkerArgumentRecord& record : plan.argumentRecords()) {
    argv.add(zc::str(record.argument()));
  }
  for (const LinkInputRecord& record : closure.crtObjects()) { argv.add(zc::str(record.path())); }
  for (const LinkInputRecord& record : plan.objectRecords()) { argv.add(zc::str(record.path())); }
  for (const LinkInputRecord& record : plan.runtimeRecords()) { argv.add(zc::str(record.path())); }
  for (const LinkInputRecord& record : closure.defaultLibraries()) {
    argv.add(zc::str(record.path()));
  }

  // The environment is empty plus the closure's recorded variables. The current
  // closure shape carries no environment entries, so this is an empty set; the
  // hook is kept explicit so a later closure extension flows through unchanged.
  zc::Array<zc::String> environment;

  return LinkerInvocation(zc::str(closure.linkerPath()), argv.releaseAsArray(),
                          ZC_REQUIRE_NONNULL(zc::mv(workingDirectory)), zc::mv(environment));
}

}  // namespace zomlang::compiler::ir
