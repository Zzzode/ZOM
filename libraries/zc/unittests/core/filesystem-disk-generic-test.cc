// Copyright (c) 2016 Sandstorm Development Group, Inc. and contributors
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

#if __linux__

// This test compiles filesystem-disk-unix.c++ with various features #undefed, causing it to take
// different code paths, then runs filesystem-disk-test.c++ against that.
//
// This test is only intended to run on Linux, but is intended to make the code behave like it
// would on a generic flavor of Unix.
//
// At present this test only runs under Ekam builds. Integrating it into other builds would be
// awkward since it #includes filesystem-disk-unix.c++, so it cannot link against that file, but
// needs to link against the rest of ZC. Ekam "just figures it out", but other build systems would
// require a lot of work here.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

#include "zc/core/debug.h"
#include "zc/core/filesystem.h"
#include "zc/core/miniposix.h"
#include "zc/core/vector.h"

#undef __linux__
#undef O_CLOEXEC
#undef O_DIRECTORY
#undef O_TMPFILE
#undef FIOCLEX
#undef DT_UNKNOWN
#undef F_DUPFD_CLOEXEC
#undef FALLOC_FL_PUNCH_HOLE
#undef FICLONE
#undef FICLONERANGE
#undef SEEK_HOLE
#undef SEEK_DATA
#undef RENAME_EXCHANGE

#define HOLES_NOT_SUPPORTED

#include "zc/core/filesystem-disk-unix.cc"
#include "zc/unittests/core/filesystem-disk-test.cc"

#endif  // __linux__
