// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// D3a: prove SubprocessCommand::executableDescriptor execs exactly the object
// held open by the descriptor, not whatever the pathname resolves to at exec
// time. It uses real compiled ELF host binaries (/bin/true, /bin/false) as the
// helper fixtures, never a shell script, so the exec-by-fd semantics are
// unambiguous. This closes only the pathname-replacement half of the TOCTOU; the
// hash-equals-exec (in-place rewrite) guarantee is D3b in the IR/driver layer.
//
// The successful exec-by-fd cases require execveat(AT_EMPTY_PATH) (Linux) and are
// guarded accordingly. The fail-closed cases (negative or invalid descriptor) are
// portable: on every platform they must fail as a spawn SystemError and never
// fall back to a pathname exec.

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "zc/core/filesystem.h"
#include "zc/core/io.h"
#include "zc/core/subprocess.h"
#include "zc/ztest/gtest.h"
#include "zc/ztest/test.h"

namespace zc {
namespace {

#if defined(__linux__)

// Reads a whole host file into an owned byte array.
Array<byte> readHostFile(StringPtr absolutePath) {
  auto fs = newDiskFilesystem();
  return fs->getRoot().openFile(Path::parse(absolutePath.slice(1)))->readAllBytes();
}

// A unique temp directory for this process's run.
String tempDir() { return str("/tmp/zom-subprocess-exec-fd-", getpid()); }

Own<const Directory> openTempDir(Filesystem& fs, StringPtr absoluteDir) {
  return fs.getRoot().openSubdir(Path::parse(absoluteDir.slice(1)),
                                 WriteMode::CREATE | WriteMode::MODIFY | WriteMode::CREATE_PARENT);
}

TEST(SubprocessExecFd, ExecutesTheOpenedObjectNotThePathname) {
  // helper is a copy of /bin/true (exit 0). Open an O_PATH descriptor on it, then
  // overwrite the SAME pathname with the bytes of /bin/false (exit 1). Executing
  // the descriptor must still run the original /bin/true (exit 0); a pathname
  // exec would instead run /bin/false (exit 1).
  auto trueBytes = readHostFile("/bin/true"_zc);
  auto falseBytes = readHostFile("/bin/false"_zc);

  auto fs = newDiskFilesystem();
  String base = tempDir();
  auto dir = openTempDir(*fs, base);
  auto helperPath = str(base, "/helper");
  Path helperRel = Path::parse("helper"_zc);  // relative to `dir`
  dir->openFile(helperRel, WriteMode::CREATE | WriteMode::EXECUTABLE)->writeAll(trueBytes.asPtr());

  // Open the executable descriptor on the current (true) bytes.
  int fd = ::open(helperPath.cStr(), O_PATH | O_CLOEXEC);
  ASSERT_TRUE(fd >= 0);

  // Replace the PATHNAME with a new inode holding /bin/false's bytes (remove +
  // create, so the original inode the descriptor holds is untouched). This is
  // the pathname-replacement attack; the in-place inode-rewrite case is D3b.
  dir->remove(helperRel);
  dir->openFile(helperRel, WriteMode::CREATE | WriteMode::EXECUTABLE)->writeAll(falseBytes.asPtr());

  SubprocessCommand command(helperPath);
  command.executableDescriptor(fd);
  SubprocessResult result = command.run();

  // The caller's descriptor must remain owned by the caller and still open after
  // run(): run() execs an internal duplicate, never the caller's descriptor.
  EXPECT_TRUE(::fcntl(fd, F_GETFD) >= 0);
  ::close(fd);

  ASSERT_TRUE(result.spawned());
  EXPECT_TRUE(result.output().terminationKind == SubprocessTerminationKind::Exited);
  // 0 == ran the opened /bin/true; 1 would mean it ran the replaced /bin/false.
  EXPECT_EQ(result.output().code, 0);

  fs->getRoot().remove(Path::parse(base.slice(1)));
}

TEST(SubprocessExecFd, InvalidDescriptorFailsClosed) {
  // A positive descriptor that is not a valid open file must fail closed as a
  // spawn SystemError (the private F_DUPFD_CLOEXEC duplication fails with EBADF),
  // never falling back to a pathname exec of /bin/true. Obtain a real descriptor
  // and close it, so the number is guaranteed invalid rather than a magic value
  // that could, in an extreme environment, happen to be an open fd.
  int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
  ASSERT_TRUE(fd >= 0);
  ::close(fd);

  SubprocessCommand command("/bin/true");
  command.executableDescriptor(fd);  // now-closed, definitely invalid
  SubprocessResult result = command.run();
  ASSERT_FALSE(result.spawned());
  EXPECT_TRUE(result.spawnFailure() == SubprocessSpawnFailure::SystemError);
  EXPECT_EQ(result.spawnErrno(), EBADF);
}

#endif  // defined(__linux__)

TEST(SubprocessExecFd, NegativeDescriptorFailsClosed) {
  // A negative descriptor is never a valid open object. run() must fail closed as
  // a spawn SystemError and must NOT silently fall back to resolving the program
  // pathname (which would run /bin/true and report spawned() == true, code 0).
  SubprocessCommand command("/bin/true");
  command.executableDescriptor(-1);
  SubprocessResult result = command.run();
  ASSERT_FALSE(result.spawned());
  EXPECT_TRUE(result.spawnFailure() == SubprocessSpawnFailure::SystemError);
  EXPECT_EQ(result.spawnErrno(), EBADF);
}

}  // namespace
}  // namespace zc
