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
// See the License for the specific language governing permissions and
// limitations under the License.

// RFC 0043 O5/KR5.3 slice (Tier 1.5): prove the InvokeLinker step binds a
// canonical driver invocation to the shell-free child-process primitive -
// spawning a real driver program with its exact argument vector, classifying a
// nonzero exit or missing output into the closed failure algebra, and reading
// back the produced executable on success. It runs a real fake-linker script on
// disk, not a mock, so the spawn, argv, and output-readback paths are exercised
// end to end.

#include "compiler/ir/invoke-linker.h"

#include <unistd.h>

#include "compiler/ir/linker-invocation.h"
#include "zc/core/filesystem.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ir {
namespace {

// A unique absolute temp directory for this process's run.
zc::String tempDirPath() { return zc::str("/tmp/zom-invoke-linker-", getpid()); }

// Opens (creating) an absolute directory on the real disk filesystem.
zc::Own<const zc::Directory> openDir(zc::Filesystem& fs, zc::StringPtr absoluteDir) {
  ZC_REQUIRE(absoluteDir.size() > 1 && absoluteDir[0] == '/');
  return fs.getRoot().openSubdir(
      zc::Path::parse(absoluteDir.slice(1)),
      zc::WriteMode::CREATE | zc::WriteMode::MODIFY | zc::WriteMode::CREATE_PARENT);
}

// Writes an executable shell script `name` under `dir` with `body`.
void writeScript(const zc::Directory& dir, zc::StringPtr name, zc::StringPtr body) {
  dir.openFile(zc::Path::parse(name),
               zc::WriteMode::CREATE | zc::WriteMode::MODIFY | zc::WriteMode::EXECUTABLE)
      ->writeAll(body);
}

// Builds a linker invocation for a real driver program and argument tail, run in
// the given working directory with an empty environment.
LinkerInvocation invocationFor(zc::StringPtr program, zc::ArrayPtr<const zc::StringPtr> tail,
                               zc::StringPtr workingDirectory) {
  zc::Vector<zc::String> argv(tail.size() + 1);
  argv.add(zc::str(program));
  for (const zc::StringPtr& token : tail) { argv.add(zc::str(token)); }
  auto value = LinkerInvocation::forProgram(program, argv.releaseAsArray(), workingDirectory,
                                            zc::Array<zc::String>());
  return zc::mv(value);
}

ZC_TEST("Invoke linker spawns the driver and reads back the produced executable") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);

  // A fake linker that writes ELF-like bytes to its second argument (`-o PATH`).
  writeScript(*dir, "fake-ld"_zc, "#!/bin/sh\nprintf '\\177ELF' > \"$2\"\n"_zc);

  zc::StringPtr tail[] = {"-o"_zc, "app"_zc};
  auto invocation = invocationFor(zc::str(base, "/fake-ld"), zc::arrayPtr(tail, 2), base);

  LinkerInvocationResult result = invokeLinker(invocation, *dir, "app"_zc);
  ZC_ASSERT(result.ok());
  zc::ArrayPtr<const uint8_t> bytes = result.executableBytes();
  ZC_ASSERT(bytes.size() == 4u);
  ZC_EXPECT(bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F');

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("Invoke linker reports a nonzero driver exit") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);

  writeScript(*dir, "fail-ld"_zc, "#!/bin/sh\necho 'link error' >&2\nexit 7\n"_zc);

  zc::StringPtr tail[] = {"-o"_zc, "app"_zc};
  auto invocation = invocationFor(zc::str(base, "/fail-ld"), zc::arrayPtr(tail, 2), base);

  LinkerInvocationResult result = invokeLinker(invocation, *dir, "app"_zc);
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == LinkerInvocationFailure::DriverExitedNonZero);
  ZC_EXPECT(result.exitCode() == 7);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("Invoke linker reports a missing output on a clean exit") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);

  // Exits cleanly but writes no output file.
  writeScript(*dir, "noop-ld"_zc, "#!/bin/sh\nexit 0\n"_zc);

  zc::StringPtr tail[] = {"-o"_zc, "app"_zc};
  auto invocation = invocationFor(zc::str(base, "/noop-ld"), zc::arrayPtr(tail, 2), base);

  LinkerInvocationResult result = invokeLinker(invocation, *dir, "app"_zc);
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == LinkerInvocationFailure::OutputMissing);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("Invoke linker reports a non-spawnable driver") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);

  zc::StringPtr tail[] = {"-o"_zc, "app"_zc};
  auto invocation = invocationFor(zc::str(base, "/does-not-exist-ld"), zc::arrayPtr(tail, 2), base);

  LinkerInvocationResult result = invokeLinker(invocation, *dir, "app"_zc);
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == LinkerInvocationFailure::DriverNotSpawnable);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

}  // namespace
}  // namespace zomlang::compiler::ir
