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

#include "zc/core/subprocess.h"

#include <signal.h>

#include "zc/core/string.h"
#include "zc/ztest/gtest.h"
#include "zc/ztest/test.h"

namespace zc {
namespace {

// Decode a captured byte array into a StringPtr for text comparison.
String capturedText(const Array<byte>& bytes) {
  return heapString(reinterpret_cast<const char*>(bytes.begin()), bytes.size());
}

TEST(Subprocess, TrueExitsZero) {
  SubprocessCommand command("/usr/bin/true");
  SubprocessResult result = command.run();
  ASSERT_TRUE(result.spawned());
  EXPECT_TRUE(result.output().succeeded());
  EXPECT_TRUE(result.output().terminationKind == SubprocessTerminationKind::Exited);
  EXPECT_EQ(result.output().code, 0);
}

TEST(Subprocess, FalseExitsNonZero) {
  SubprocessCommand command("/usr/bin/false");
  SubprocessResult result = command.run();
  ASSERT_TRUE(result.spawned());
  EXPECT_FALSE(result.output().succeeded());
  EXPECT_TRUE(result.output().terminationKind == SubprocessTerminationKind::Exited);
  EXPECT_EQ(result.output().code, 1);
}

TEST(Subprocess, CapturesStdout) {
  SubprocessCommand command("/bin/echo");
  command.arg("hello").arg("world");
  SubprocessResult result = command.run();
  ASSERT_TRUE(result.spawned());
  EXPECT_TRUE(result.output().succeeded());
  EXPECT_STREQ(capturedText(result.output().capturedStdout).cStr(), "hello world\n");
  EXPECT_FALSE(result.output().stdoutTruncated);
}

TEST(Subprocess, ArgvFidelityNoSplittingNoGlobNoExpansion) {
  // Every argument must reach the child verbatim: embedded spaces stay one
  // argument, glob metacharacters are not expanded, and $VAR is not substituted.
  // `printf %s\n` echoes each argument on its own line, so we can assert the
  // exact argv the child observed.
  SubprocessCommand command("/usr/bin/printf");
  command.arg("%s\n")
      .arg("one two three")  // spaces do not split
      .arg("*")              // glob is not expanded
      .arg("$HOME")          // variable is not expanded
      .arg("a\tb");          // tab survives
  SubprocessResult result = command.run();
  ASSERT_TRUE(result.spawned());
  ASSERT_TRUE(result.output().succeeded());
  EXPECT_STREQ(capturedText(result.output().capturedStdout).cStr(),
               "one two three\n*\n$HOME\na\tb\n");
}

TEST(Subprocess, EmptyEnvPolicyDropsInheritedVariables) {
  // Under the Empty policy the child sees only the explicit allow-list. `env`
  // with no arguments prints the child's environment; it must contain exactly
  // the one variable we added.
  SubprocessCommand command("/usr/bin/env");
  command.envPolicy(SubprocessEnvPolicy::Empty).env("ZOM_ONLY", "present");
  SubprocessResult result = command.run();
  ASSERT_TRUE(result.spawned());
  ASSERT_TRUE(result.output().succeeded());
  EXPECT_STREQ(capturedText(result.output().capturedStdout).cStr(), "ZOM_ONLY=present\n");
}

TEST(Subprocess, WorkingDirectoryApplies) {
  SubprocessCommand command("/bin/pwd");
  command.cwd("/tmp");
  SubprocessResult result = command.run();
  ASSERT_TRUE(result.spawned());
  ASSERT_TRUE(result.output().succeeded());
  // On some hosts /tmp is a symlink; accept either the literal or a suffix.
  String out = capturedText(result.output().capturedStdout);
  EXPECT_TRUE(out == "/tmp\n"_zc || out.endsWith("/tmp\n"));
}

TEST(Subprocess, MissingProgramReportsProgramNotFound) {
  SubprocessCommand command("/nonexistent/zom-no-such-binary");
  SubprocessResult result = command.run();
  ASSERT_FALSE(result.spawned());
  EXPECT_TRUE(result.spawnFailure() == SubprocessSpawnFailure::ProgramNotFound);
}

TEST(Subprocess, NonExecutableFileReportsPermissionDenied) {
  // A regular, non-executable file exists but cannot be exec'd: the child's
  // execv fails with EACCES, which must classify as PermissionDenied. /etc/hostname
  // is a readable, non-executable regular file on every Linux host.
  SubprocessCommand command("/etc/hostname");
  SubprocessResult result = command.run();
  ASSERT_FALSE(result.spawned());
  EXPECT_TRUE(result.spawnFailure() == SubprocessSpawnFailure::PermissionDenied);
}

TEST(Subprocess, SignalTerminationReportsSignaled) {
  // A child that terminates via a signal must report the Signaled kind and carry
  // the signal number. `sh -c 'kill -TERM $$'` sends SIGTERM to itself.
  SubprocessCommand command("/bin/sh");
  command.arg("-c").arg("kill -TERM $$");
  SubprocessResult result = command.run();
  ASSERT_TRUE(result.spawned());
  EXPECT_TRUE(result.output().terminationKind == SubprocessTerminationKind::Signaled);
  EXPECT_EQ(result.output().signal, SIGTERM);
  EXPECT_FALSE(result.output().succeeded());
}

TEST(Subprocess, CaptureLimitTruncatesLargeOutput) {
  // `yes` streams forever; a tiny cap must truncate and stay bounded rather than
  // deadlock or grow without limit. The child is killed by SIGPIPE once we stop
  // reading and the pipe fills, so it terminates via a signal.
  SubprocessCommand command("/usr/bin/yes");
  command.captureLimit(16);
  SubprocessResult result = command.run();
  ASSERT_TRUE(result.spawned());
  EXPECT_TRUE(result.output().stdoutTruncated);
  EXPECT_EQ(result.output().capturedStdout.size(), 16u);
}

}  // namespace
}  // namespace zc
