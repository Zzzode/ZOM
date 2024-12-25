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

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
// Request 64-bit off_t and ino_t, otherwise this code will break when either value exceeds 2^32.
#endif

#include <stdlib.h>

#include <string>

#include "src/zc/core/debug.h"
#include "src/zc/core/encoding.h"
#include "src/zc/core/filesystem.h"
#include "src/zc/core/string.h"
#include "src/zc/ztest/test.h"
#if _WIN32
#include <windows.h>

#include "src/zc/core/windows-sanity.h"
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace zc {
namespace {

bool isWine() ZC_UNUSED;

#if _WIN32

bool detectWine() {
  HMODULE hntdll = GetModuleHandle("ntdll.dll");
  if (hntdll == NULL) return false;
  return GetProcAddress(hntdll, "wine_get_version") != nullptr;
}

bool isWine() {
  static bool result = detectWine();
  return result;
}

template <typename Func>
static auto newTemp(Func&& create) -> Decay<decltype(*zc::_::readMaybe(create(Array<wchar_t>())))> {
  wchar_t wtmpdir[MAX_PATH + 1];
  DWORD len = GetTempPathW(zc::size(wtmpdir), wtmpdir);
  ZC_ASSERT(len < zc::size(wtmpdir));
  auto tmpdir = decodeWideString(arrayPtr(wtmpdir, len));

  static uint counter = 0;
  for (;;) {
    auto path = zc::str(tmpdir, "zc-filesystem-test.", GetCurrentProcessId(), ".", counter++);
    ZC_IF_SOME(result, create(encodeWideString(path, true))) { return zc::mv(result); }
  }
}

static Own<File> newTempFile() {
  return newTemp([](Array<wchar_t> candidatePath) -> Maybe<Own<File>> {
    HANDLE handle;
    ZC_WIN32_HANDLE_ERRORS(
        handle =
            CreateFileW(candidatePath.begin(), FILE_GENERIC_READ | FILE_GENERIC_WRITE, 0, NULL,
                        CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL)) {
      case ERROR_ALREADY_EXISTS:
      case ERROR_FILE_EXISTS:
        return nullptr;
      default:
        ZC_FAIL_WIN32("CreateFileW", error);
    }
    return newDiskFile(AutoCloseHandle(handle));
  });
}

static Array<wchar_t> join16(ArrayPtr<const wchar_t> path, const wchar_t* file) {
  // Assumes `path` ends with a NUL terminator (and `file` is of course NUL terminated as well).

  size_t len = wcslen(file) + 1;
  auto result = zc::heapArray<wchar_t>(path.size() + len);
  memcpy(result.begin(), path.begin(), path.asBytes().size() - sizeof(wchar_t));
  result[path.size() - 1] = '\\';
  memcpy(result.begin() + path.size(), file, len * sizeof(wchar_t));
  return result;
}

class TempDir {
public:
  TempDir()
      : filename(newTemp([](Array<wchar_t> candidatePath) -> Maybe<Array<wchar_t>> {
          ZC_WIN32_HANDLE_ERRORS(CreateDirectoryW(candidatePath.begin(), NULL)) {
            case ERROR_ALREADY_EXISTS:
            case ERROR_FILE_EXISTS:
              return nullptr;
            default:
              ZC_FAIL_WIN32("CreateDirectoryW", error);
          }
          return zc::mv(candidatePath);
        })) {}

  Own<Directory> get() {
    HANDLE handle;
    ZC_WIN32(handle = CreateFileW(
                 filename.begin(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                 OPEN_EXISTING,
                 FILE_FLAG_BACKUP_SEMANTICS,  // apparently, this flag is required for directories
                 NULL));
    return newDiskDirectory(AutoCloseHandle(handle));
  }

  ~TempDir() noexcept(false) { recursiveDelete(filename); }

private:
  Array<wchar_t> filename;

  static void recursiveDelete(ArrayPtr<const wchar_t> path) {
    // Recursively delete the temp dir, verifying that no .zc-tmp. files were left over.
    //
    // Mostly copied from rmrfChildren() in filesystem-win32.c++.

    auto glob = join16(path, L"\\*");

    WIN32_FIND_DATAW data;
    HANDLE handle = FindFirstFileW(glob.begin(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
      auto error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND) return;
      ZC_FAIL_WIN32("FindFirstFile", error, path) { return; }
    }
    ZC_DEFER(ZC_WIN32(FindClose(handle)) { break; });

    do {
      // Ignore "." and "..", ugh.
      if (data.cFileName[0] == L'.') {
        if (data.cFileName[1] == L'\0' ||
            (data.cFileName[1] == L'.' && data.cFileName[2] == L'\0')) {
          continue;
        }
      }

      String utf8Name = decodeWideString(arrayPtr(data.cFileName, wcslen(data.cFileName)));
      ZC_EXPECT(!utf8Name.startsWith(".zc-tmp."), "temp file not cleaned up", utf8Name);

      auto child = join16(path, data.cFileName);
      if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
          !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        recursiveDelete(child);
      } else {
        ZC_WIN32(DeleteFileW(child.begin()));
      }
    } while (FindNextFileW(handle, &data));

    auto error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) {
      ZC_FAIL_WIN32("FindNextFile", error, path) { return; }
    }

    uint retryCount = 0;
  retry:
    ZC_WIN32_HANDLE_ERRORS(RemoveDirectoryW(path.begin())) {
      case ERROR_DIR_NOT_EMPTY:
        if (retryCount++ < 10) {
          Sleep(10);
          goto retry;
        }
        ZC_FALLTHROUGH;
      default:
        ZC_FAIL_WIN32("RemoveDirectory", error) { break; }
    }
  }
};

#else

bool isWine() { return false; }

#if __APPLE__ || __CYGWIN__
#define HOLES_NOT_SUPPORTED 1
#endif

#if __ANDROID__
#define VAR_TMP "/data/local/tmp"
#else
#define VAR_TMP "/var/tmp"
#endif

static Own<File> newTempFile() {
  const char* tmpDir = getenv("TEST_TMPDIR");
  auto filename = str(tmpDir != nullptr ? tmpDir : VAR_TMP, "/zc-filesystem-test.XXXXXX");
  int fd;
  ZC_SYSCALL(fd = mkstemp(filename.begin()));
  ZC_DEFER(ZC_SYSCALL(unlink(filename.cStr())));
  return newDiskFile(AutoCloseFd(fd));
}

class TempDir {
public:
  TempDir() {
    const char* tmpDir = getenv("TEST_TMPDIR");
    filename = str(tmpDir != nullptr ? tmpDir : VAR_TMP, "/zc-filesystem-test.XXXXXX");
    if (mkdtemp(filename.begin()) == nullptr) { ZC_FAIL_SYSCALL("mkdtemp", errno, filename); }
  }

  Own<Directory> get() {
    int fd;
    ZC_SYSCALL(fd = open(filename.cStr(), O_RDONLY));
    return newDiskDirectory(AutoCloseFd(fd));
  }

  ~TempDir() noexcept(false) { recursiveDelete(filename); }

private:
  String filename;

  static void recursiveDelete(StringPtr path) {
    // Recursively delete the temp dir, verifying that no .zc-tmp. files were left over.

    {
      DIR* dir = opendir(path.cStr());
      ZC_ASSERT(dir != nullptr);
      ZC_DEFER(closedir(dir));

      for (;;) {
        auto entry = readdir(dir);
        if (entry == nullptr) break;

        StringPtr name = entry->d_name;
        if (name == "." || name == "..") continue;

        auto subPath = zc::str(path, '/', entry->d_name);

        ZC_EXPECT(!name.startsWith(".zc-tmp."), "temp file not cleaned up", subPath);

        struct stat stats;
        ZC_SYSCALL(lstat(subPath.cStr(), &stats));

        if (S_ISDIR(stats.st_mode)) {
          recursiveDelete(subPath);
        } else {
          ZC_SYSCALL(unlink(subPath.cStr()));
        }
      }
    }

    ZC_SYSCALL(rmdir(path.cStr()));
  }
};

#endif  // _WIN32, else

ZC_TEST("DiskFile") {
  auto file = newTempFile();

  ZC_EXPECT(file->readAllText() == "");

  // mmaping empty file should work
  ZC_EXPECT(file->mmap(0, 0).size() == 0);
  ZC_EXPECT(file->mmapPrivate(0, 0).size() == 0);
  ZC_EXPECT(file->mmapWritable(0, 0)->get().size() == 0);

  file->writeAll("foo");
  ZC_EXPECT(file->readAllText() == "foo");

  file->write(3, StringPtr("bar").asBytes());
  ZC_EXPECT(file->readAllText() == "foobar");

  file->write(3, StringPtr("baz").asBytes());
  ZC_EXPECT(file->readAllText() == "foobaz");

  file->write(9, StringPtr("qux").asBytes());
  ZC_EXPECT(file->readAllText() == zc::StringPtr("foobaz\0\0\0qux", 12));

  file->truncate(6);
  ZC_EXPECT(file->readAllText() == "foobaz");

  file->truncate(18);
  ZC_EXPECT(file->readAllText() == zc::StringPtr("foobaz\0\0\0\0\0\0\0\0\0\0\0\0", 18));

  // empty mappings work, even if useless
  ZC_EXPECT(file->mmap(0, 0).size() == 0);
  ZC_EXPECT(file->mmapPrivate(0, 0).size() == 0);
  ZC_EXPECT(file->mmapWritable(0, 0)->get().size() == 0);
  ZC_EXPECT(file->mmap(2, 0).size() == 0);
  ZC_EXPECT(file->mmapPrivate(2, 0).size() == 0);
  ZC_EXPECT(file->mmapWritable(2, 0)->get().size() == 0);

  {
    auto mapping = file->mmap(0, 18);
    auto privateMapping = file->mmapPrivate(0, 18);
    auto writableMapping = file->mmapWritable(0, 18);

    ZC_EXPECT(mapping.size() == 18);
    ZC_EXPECT(privateMapping.size() == 18);
    ZC_EXPECT(writableMapping->get().size() == 18);

    ZC_EXPECT(writableMapping->get().begin() != mapping.begin());
    ZC_EXPECT(privateMapping.begin() != mapping.begin());
    ZC_EXPECT(writableMapping->get().begin() != privateMapping.begin());

    ZC_EXPECT(zc::str(mapping.first(6).asChars()) == "foobaz");
    ZC_EXPECT(zc::str(writableMapping->get().first(6).asChars()) == "foobaz");
    ZC_EXPECT(zc::str(privateMapping.first(6).asChars()) == "foobaz");

    privateMapping[0] = 'F';
    ZC_EXPECT(zc::str(mapping.first(6).asChars()) == "foobaz");
    ZC_EXPECT(zc::str(writableMapping->get().first(6).asChars()) == "foobaz");
    ZC_EXPECT(zc::str(privateMapping.first(6).asChars()) == "Foobaz");

    writableMapping->get()[1] = 'D';
    writableMapping->changed(writableMapping->get().slice(1, 2));
    ZC_EXPECT(zc::str(mapping.first(6).asChars()) == "fDobaz");
    ZC_EXPECT(zc::str(writableMapping->get().first(6).asChars()) == "fDobaz");
    ZC_EXPECT(zc::str(privateMapping.first(6).asChars()) == "Foobaz");

    file->write(0, StringPtr("qux").asBytes());
    ZC_EXPECT(zc::str(mapping.first(6).asChars()) == "quxbaz");
    ZC_EXPECT(zc::str(writableMapping->get().first(6).asChars()) == "quxbaz");
    ZC_EXPECT(zc::str(privateMapping.first(6).asChars()) == "Foobaz");

    file->write(12, StringPtr("corge").asBytes());
    ZC_EXPECT(zc::str(mapping.slice(12, 17).asChars()) == "corge");

#if !_WIN32 && !__CYGWIN__  // Windows doesn't allow the file size to change while mapped.
    // Can shrink.
    file->truncate(6);
    ZC_EXPECT(zc::str(mapping.slice(12, 17).asChars()) == zc::StringPtr("\0\0\0\0\0", 5));

    // Can regrow.
    file->truncate(18);
    ZC_EXPECT(zc::str(mapping.slice(12, 17).asChars()) == zc::StringPtr("\0\0\0\0\0", 5));

    // Can even regrow past previous capacity.
    file->truncate(100);
#endif
  }

  file->truncate(6);

  ZC_EXPECT(file->readAllText() == "quxbaz");
  file->zero(3, 3);
  ZC_EXPECT(file->readAllText() == StringPtr("qux\0\0\0", 6));
}

ZC_TEST("DiskFile::copy()") {
  auto source = newTempFile();
  source->writeAll("foobarbaz");

  auto dest = newTempFile();
  dest->writeAll("quxcorge");

  ZC_EXPECT(dest->copy(3, *source, 6, zc::maxValue) == 3);
  ZC_EXPECT(dest->readAllText() == "quxbazge");

  ZC_EXPECT(dest->copy(0, *source, 3, 4) == 4);
  ZC_EXPECT(dest->readAllText() == "barbazge");

  ZC_EXPECT(dest->copy(0, *source, 128, zc::maxValue) == 0);

  ZC_EXPECT(dest->copy(4, *source, 3, 0) == 0);

  String bigString = strArray(repeat("foobar", 10000), "");
  source->truncate(bigString.size() + 1000);
  source->write(123, bigString.asBytes());

  dest->copy(321, *source, 123, bigString.size());
  ZC_EXPECT(dest->readAllText().slice(321) == bigString);
}

ZC_TEST("DiskDirectory") {
  TempDir tempDir;
  auto dir = tempDir.get();

  ZC_EXPECT(dir->listNames() == nullptr);
  ZC_EXPECT(dir->listEntries() == nullptr);
  ZC_EXPECT(!dir->exists(Path("foo")));
  ZC_EXPECT(dir->tryOpenFile(Path("foo")) == zc::none);
  ZC_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::MODIFY) == zc::none);

  {
    auto file = dir->openFile(Path("foo"), WriteMode::CREATE);
    file->writeAll("foobar");
  }

  ZC_EXPECT(dir->exists(Path("foo")));

  {
    auto stats = dir->lstat(Path("foo"));
    ZC_EXPECT(stats.type == FsNode::Type::FILE);
    ZC_EXPECT(stats.size == 6);
  }

  {
    auto list = dir->listNames();
    ZC_ASSERT(list.size() == 1);
    ZC_EXPECT(list[0] == "foo");
  }

  {
    auto list = dir->listEntries();
    ZC_ASSERT(list.size() == 1);
    ZC_EXPECT(list[0].name == "foo");
    ZC_EXPECT(list[0].type == FsNode::Type::FILE);
  }

  ZC_EXPECT(dir->openFile(Path("foo"))->readAllText() == "foobar");

  ZC_EXPECT(dir->tryOpenFile(Path({"foo", "bar"}), WriteMode::MODIFY) == zc::none);
  ZC_EXPECT(dir->tryOpenFile(Path({"bar", "baz"}), WriteMode::MODIFY) == zc::none);
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("parent is not a directory",
                                      dir->tryOpenFile(Path({"bar", "baz"}), WriteMode::CREATE));

  {
    auto file = dir->openFile(Path({"bar", "baz"}), WriteMode::CREATE | WriteMode::CREATE_PARENT);
    file->writeAll("bazqux");
  }

  ZC_EXPECT(dir->openFile(Path({"bar", "baz"}))->readAllText() == "bazqux");

  {
    auto stats = dir->lstat(Path("bar"));
    ZC_EXPECT(stats.type == FsNode::Type::DIRECTORY);
  }

  {
    auto list = dir->listNames();
    ZC_ASSERT(list.size() == 2);
    ZC_EXPECT(list[0] == "bar");
    ZC_EXPECT(list[1] == "foo");
  }

  {
    auto list = dir->listEntries();
    ZC_ASSERT(list.size() == 2);
    ZC_EXPECT(list[0].name == "bar");
    ZC_EXPECT(list[0].type == FsNode::Type::DIRECTORY);
    ZC_EXPECT(list[1].name == "foo");
    ZC_EXPECT(list[1].type == FsNode::Type::FILE);
  }

  {
    auto subdir = dir->openSubdir(Path("bar"));

    ZC_EXPECT(subdir->openFile(Path("baz"))->readAllText() == "bazqux");
  }

  auto subdir = dir->openSubdir(Path("corge"), WriteMode::CREATE);

  subdir->openFile(Path("grault"), WriteMode::CREATE)->writeAll("garply");

  ZC_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "garply");

  dir->openFile(Path({"corge", "grault"}), WriteMode::CREATE | WriteMode::MODIFY)
      ->write(0, StringPtr("rag").asBytes());
  ZC_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "ragply");

  ZC_EXPECT(dir->openSubdir(Path("corge"))->listNames().size() == 1);

  {
    auto replacer =
        dir->replaceFile(Path({"corge", "grault"}), WriteMode::CREATE | WriteMode::MODIFY);
    replacer->get().writeAll("rag");

    // temp file not in list
    ZC_EXPECT(dir->openSubdir(Path("corge"))->listNames().size() == 1);

    // Don't commit.
  }
  ZC_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "ragply");

  {
    auto replacer =
        dir->replaceFile(Path({"corge", "grault"}), WriteMode::CREATE | WriteMode::MODIFY);
    replacer->get().writeAll("rag");

    // temp file not in list
    ZC_EXPECT(dir->openSubdir(Path("corge"))->listNames().size() == 1);

    replacer->commit();
    ZC_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "rag");
  }

  ZC_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "rag");

  {
    auto appender = dir->appendFile(Path({"corge", "grault"}), WriteMode::MODIFY);
    appender->write("waldo"_zcb);
    appender->write("fred"_zcb);
  }

  ZC_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "ragwaldofred");

  ZC_EXPECT(dir->exists(Path("foo")));
  dir->remove(Path("foo"));
  ZC_EXPECT(!dir->exists(Path("foo")));
  ZC_EXPECT(!dir->tryRemove(Path("foo")));

  ZC_EXPECT(dir->exists(Path({"bar", "baz"})));
  dir->remove(Path({"bar", "baz"}));
  ZC_EXPECT(!dir->exists(Path({"bar", "baz"})));
  ZC_EXPECT(dir->exists(Path("bar")));
  ZC_EXPECT(!dir->tryRemove(Path({"bar", "baz"})));

#if _WIN32
  // On Windows, we can't delete a directory while we still have it open.
  subdir = nullptr;
#endif

  ZC_EXPECT(dir->exists(Path("corge")));
  ZC_EXPECT(dir->exists(Path({"corge", "grault"})));
  dir->remove(Path("corge"));
  ZC_EXPECT(!dir->exists(Path("corge")));
  ZC_EXPECT(!dir->exists(Path({"corge", "grault"})));
  ZC_EXPECT(!dir->tryRemove(Path("corge")));
}

#if !_WIN32  // Creating symlinks on Win32 requires admin privileges prior to Windows 10.
ZC_TEST("DiskDirectory symlinks") {
  TempDir tempDir;
  auto dir = tempDir.get();

  dir->symlink(Path("foo"), "bar/qux/../baz", WriteMode::CREATE);

  ZC_EXPECT(!dir->trySymlink(Path("foo"), "bar/qux/../baz", WriteMode::CREATE));

  {
    auto stats = dir->lstat(Path("foo"));
    ZC_EXPECT(stats.type == FsNode::Type::SYMLINK);
  }

  ZC_EXPECT(dir->readlink(Path("foo")) == "bar/qux/../baz");

  // Broken link into non-existing directory cannot be opened in any mode.
  ZC_EXPECT(dir->tryOpenFile(Path("foo")) == zc::none);
  ZC_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::CREATE) == zc::none);
  ZC_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::MODIFY) == zc::none);
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE(
      "parent is not a directory",
      dir->tryOpenFile(Path("foo"), WriteMode::CREATE | WriteMode::MODIFY));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE(
      "parent is not a directory",
      dir->tryOpenFile(Path("foo"),
                       WriteMode::CREATE | WriteMode::MODIFY | WriteMode::CREATE_PARENT));

  // Create the directory.
  auto subdir = dir->openSubdir(Path("bar"), WriteMode::CREATE);
  subdir->openSubdir(Path("qux"), WriteMode::CREATE);

  // Link still points to non-existing file so cannot be open in most modes.
  ZC_EXPECT(dir->tryOpenFile(Path("foo")) == zc::none);
  ZC_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::CREATE) == zc::none);
  ZC_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::MODIFY) == zc::none);

  // But... CREATE | MODIFY works.
  dir->openFile(Path("foo"), WriteMode::CREATE | WriteMode::MODIFY)->writeAll("foobar");

  ZC_EXPECT(dir->openFile(Path({"bar", "baz"}))->readAllText() == "foobar");
  ZC_EXPECT(dir->openFile(Path("foo"))->readAllText() == "foobar");
  ZC_EXPECT(dir->openFile(Path("foo"), WriteMode::MODIFY)->readAllText() == "foobar");

  // operations that modify the symlink
  dir->symlink(Path("foo"), "corge", WriteMode::MODIFY);
  ZC_EXPECT(dir->openFile(Path({"bar", "baz"}))->readAllText() == "foobar");
  ZC_EXPECT(dir->readlink(Path("foo")) == "corge");
  ZC_EXPECT(!dir->exists(Path("foo")));
  ZC_EXPECT(dir->lstat(Path("foo")).type == FsNode::Type::SYMLINK);
  ZC_EXPECT(dir->tryOpenFile(Path("foo")) == zc::none);

  dir->remove(Path("foo"));
  ZC_EXPECT(!dir->exists(Path("foo")));
  ZC_EXPECT(dir->tryOpenFile(Path("foo")) == zc::none);
}
#endif

ZC_TEST("DiskDirectory link") {
  TempDir tempDirSrc;
  TempDir tempDirDst;

  auto src = tempDirSrc.get();
  auto dst = tempDirDst.get();

  src->openFile(Path("foo"), WriteMode::CREATE | WriteMode::CREATE_PARENT)->writeAll("foobar");

  dst->transfer(Path("link"), WriteMode::CREATE, *src, Path("foo"), TransferMode::LINK);

  ZC_EXPECT(dst->openFile(Path("link"))->readAllText() == "foobar");

  // Writing the old location modifies the new.
  src->openFile(Path("foo"), WriteMode::MODIFY)->writeAll("bazqux");
  ZC_EXPECT(dst->openFile(Path("link"))->readAllText() == "bazqux");

  // Replacing the old location doesn't modify the new.
  {
    auto replacer = src->replaceFile(Path("foo"), WriteMode::MODIFY);
    replacer->get().writeAll("corge");
    replacer->commit();
  }
  ZC_EXPECT(src->openFile(Path("foo"))->readAllText() == "corge");
  ZC_EXPECT(dst->openFile(Path("link"))->readAllText() == "bazqux");
}

ZC_TEST("DiskDirectory copy") {
  TempDir tempDirSrc;
  TempDir tempDirDst;

  auto src = tempDirSrc.get();
  auto dst = tempDirDst.get();

  src->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("foobar");
  src->openFile(Path({"foo", "baz", "qux"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("bazqux");

  dst->transfer(Path("link"), WriteMode::CREATE, *src, Path("foo"), TransferMode::COPY);

  ZC_EXPECT(src->openFile(Path({"foo", "bar"}))->readAllText() == "foobar");
  ZC_EXPECT(src->openFile(Path({"foo", "baz", "qux"}))->readAllText() == "bazqux");
  ZC_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
  ZC_EXPECT(dst->openFile(Path({"link", "baz", "qux"}))->readAllText() == "bazqux");

  ZC_EXPECT(dst->exists(Path({"link", "bar"})));
  src->remove(Path({"foo", "bar"}));
  ZC_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
}

ZC_TEST("DiskDirectory copy-replace") {
  TempDir tempDirSrc;
  TempDir tempDirDst;

  auto src = tempDirSrc.get();
  auto dst = tempDirDst.get();

  src->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("foobar");
  src->openFile(Path({"foo", "baz", "qux"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("bazqux");

  dst->openFile(Path({"link", "corge"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("abcd");

  // CREATE fails.
  ZC_EXPECT(
      !dst->tryTransfer(Path("link"), WriteMode::CREATE, *src, Path("foo"), TransferMode::COPY));

  // Verify nothing changed.
  ZC_EXPECT(dst->openFile(Path({"link", "corge"}))->readAllText() == "abcd");
  ZC_EXPECT(!dst->exists(Path({"foo", "bar"})));

  // Now try MODIFY.
  dst->transfer(Path("link"), WriteMode::MODIFY, *src, Path("foo"), TransferMode::COPY);

  ZC_EXPECT(src->openFile(Path({"foo", "bar"}))->readAllText() == "foobar");
  ZC_EXPECT(src->openFile(Path({"foo", "baz", "qux"}))->readAllText() == "bazqux");
  ZC_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
  ZC_EXPECT(dst->openFile(Path({"link", "baz", "qux"}))->readAllText() == "bazqux");
  ZC_EXPECT(!dst->exists(Path({"link", "corge"})));

  ZC_EXPECT(dst->exists(Path({"link", "bar"})));
  src->remove(Path({"foo", "bar"}));
  ZC_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
}

ZC_TEST("DiskDirectory move") {
  TempDir tempDirSrc;
  TempDir tempDirDst;

  auto src = tempDirSrc.get();
  auto dst = tempDirDst.get();

  src->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("foobar");
  src->openFile(Path({"foo", "baz", "qux"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("bazqux");

  dst->transfer(Path("link"), WriteMode::CREATE, *src, Path("foo"), TransferMode::MOVE);

  ZC_EXPECT(!src->exists(Path({"foo"})));
  ZC_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
  ZC_EXPECT(dst->openFile(Path({"link", "baz", "qux"}))->readAllText() == "bazqux");
}

ZC_TEST("DiskDirectory move-replace") {
  TempDir tempDirSrc;
  TempDir tempDirDst;

  auto src = tempDirSrc.get();
  auto dst = tempDirDst.get();

  src->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("foobar");
  src->openFile(Path({"foo", "baz", "qux"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("bazqux");

  dst->openFile(Path({"link", "corge"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("abcd");

  // CREATE fails.
  ZC_EXPECT(
      !dst->tryTransfer(Path("link"), WriteMode::CREATE, *src, Path("foo"), TransferMode::MOVE));

  // Verify nothing changed.
  ZC_EXPECT(dst->openFile(Path({"link", "corge"}))->readAllText() == "abcd");
  ZC_EXPECT(!dst->exists(Path({"foo", "bar"})));
  ZC_EXPECT(src->exists(Path({"foo"})));

  // Now try MODIFY.
  dst->transfer(Path("link"), WriteMode::MODIFY, *src, Path("foo"), TransferMode::MOVE);

  ZC_EXPECT(!src->exists(Path({"foo"})));
  ZC_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
  ZC_EXPECT(dst->openFile(Path({"link", "baz", "qux"}))->readAllText() == "bazqux");
}

ZC_TEST("DiskDirectory createTemporary") {
  TempDir tempDir;
  auto dir = tempDir.get();
  auto file = dir->createTemporary();
  file->writeAll("foobar");
  ZC_EXPECT(file->readAllText() == "foobar");
  ZC_EXPECT(dir->listNames() == nullptr);
}

#if !__CYGWIN__  // TODO(someday): Figure out why this doesn't work on Cygwin.
ZC_TEST("DiskDirectory replaceSubdir()") {
  TempDir tempDir;
  auto dir = tempDir.get();

  {
    auto replacer = dir->replaceSubdir(Path("foo"), WriteMode::CREATE);
    replacer->get().openFile(Path("bar"), WriteMode::CREATE)->writeAll("original");
    ZC_EXPECT(replacer->get().openFile(Path("bar"))->readAllText() == "original");
    ZC_EXPECT(!dir->exists(Path({"foo", "bar"})));

    replacer->commit();
    ZC_EXPECT(replacer->get().openFile(Path("bar"))->readAllText() == "original");
    ZC_EXPECT(dir->openFile(Path({"foo", "bar"}))->readAllText() == "original");
  }

  {
    // CREATE fails -- already exists.
    auto replacer = dir->replaceSubdir(Path("foo"), WriteMode::CREATE);
    replacer->get().openFile(Path("corge"), WriteMode::CREATE)->writeAll("bazqux");
    ZC_EXPECT(dir->listNames().size() == 1 && dir->listNames()[0] == "foo");
    ZC_EXPECT(!replacer->tryCommit());
  }

  // Unchanged.
  ZC_EXPECT(dir->openFile(Path({"foo", "bar"}))->readAllText() == "original");
  ZC_EXPECT(!dir->exists(Path({"foo", "corge"})));

  {
    // MODIFY succeeds.
    auto replacer = dir->replaceSubdir(Path("foo"), WriteMode::MODIFY);
    replacer->get().openFile(Path("corge"), WriteMode::CREATE)->writeAll("bazqux");
    ZC_EXPECT(dir->listNames().size() == 1 && dir->listNames()[0] == "foo");
    replacer->commit();
  }

  // Replaced with new contents.
  ZC_EXPECT(!dir->exists(Path({"foo", "bar"})));
  ZC_EXPECT(dir->openFile(Path({"foo", "corge"}))->readAllText() == "bazqux");
}
#endif  // !__CYGWIN__

ZC_TEST("DiskDirectory replace directory with file") {
  TempDir tempDir;
  auto dir = tempDir.get();

  dir->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("foobar");

  {
    // CREATE fails -- already exists.
    auto replacer = dir->replaceFile(Path("foo"), WriteMode::CREATE);
    replacer->get().writeAll("bazqux");
    ZC_EXPECT(!replacer->tryCommit());
  }

  // Still a directory.
  ZC_EXPECT(dir->lstat(Path("foo")).type == FsNode::Type::DIRECTORY);

  {
    // MODIFY succeeds.
    auto replacer = dir->replaceFile(Path("foo"), WriteMode::MODIFY);
    replacer->get().writeAll("bazqux");
    replacer->commit();
  }

  // Replaced with file.
  ZC_EXPECT(dir->openFile(Path("foo"))->readAllText() == "bazqux");
}

ZC_TEST("DiskDirectory replace file with directory") {
  TempDir tempDir;
  auto dir = tempDir.get();

  dir->openFile(Path("foo"), WriteMode::CREATE)->writeAll("foobar");

  {
    // CREATE fails -- already exists.
    auto replacer = dir->replaceSubdir(Path("foo"), WriteMode::CREATE);
    replacer->get().openFile(Path("bar"), WriteMode::CREATE)->writeAll("bazqux");
    ZC_EXPECT(dir->listNames().size() == 1 && dir->listNames()[0] == "foo");
    ZC_EXPECT(!replacer->tryCommit());
  }

  // Still a file.
  ZC_EXPECT(dir->openFile(Path("foo"))->readAllText() == "foobar");

  {
    // MODIFY succeeds.
    auto replacer = dir->replaceSubdir(Path("foo"), WriteMode::MODIFY);
    replacer->get().openFile(Path("bar"), WriteMode::CREATE)->writeAll("bazqux");
    ZC_EXPECT(dir->listNames().size() == 1 && dir->listNames()[0] == "foo");
    replacer->commit();
  }

  // Replaced with directory.
  ZC_EXPECT(dir->openFile(Path({"foo", "bar"}))->readAllText() == "bazqux");
}

#if !defined(HOLES_NOT_SUPPORTED) && (CAPNP_DEBUG_TYPES || CAPNP_EXPENSIVE_TESTS)
// Not all filesystems support sparse files, and if they do, they don't necessarily support
// copying them in a way that preserves holes. We don't want the capnp test suite to fail just
// because it was run on the wrong filesystem. We could design the test to check first if the
// filesystem supports holes, but the code to do that would be almost the same as the code being
// tested... Instead, we've marked this test so it only runs when building this library using
// defines that only the Cap'n Proto maintainers use. So, we run the test ourselves but we don't
// make other people run it.

ZC_TEST("DiskFile holes") {
  if (isWine()) {
    // WINE doesn't support sparse files.
    return;
  }

  TempDir tempDir;
  auto dir = tempDir.get();

  auto file = dir->openFile(Path("holes"), WriteMode::CREATE);

#if _WIN32
  FILE_SET_SPARSE_BUFFER sparseInfo;
  memset(&sparseInfo, 0, sizeof(sparseInfo));
  sparseInfo.SetSparse = TRUE;
  DWORD dummy;
  ZC_WIN32(DeviceIoControl(ZC_ASSERT_NONNULL(file->getWin32Handle()), FSCTL_SET_SPARSE, &sparseInfo,
                           sizeof(sparseInfo), NULL, 0, &dummy, NULL));
#endif

  file->writeAll("foobar");
  file->write(1 << 20, StringPtr("foobar").asBytes());

  // Some filesystems, like BTRFS, report zero `spaceUsed` until synced.
  file->datasync();

  // Allow for block sizes as low as 512 bytes and as high as 64k. Since we wrote two locations,
  // two blocks should be used.
  auto meta = file->stat();
#if __FreeBSD__
  // On FreeBSD with ZFS it seems to report 512 bytes used even if I write more than 512 random
  // (i.e. non-compressible) bytes. I couldn't figure it out so I'm giving up for now. Maybe it's
  // a bug in the system?
  ZC_EXPECT(meta.spaceUsed >= 512, meta.spaceUsed);
#else
  ZC_EXPECT(meta.spaceUsed >= 2 * 512, meta.spaceUsed);
#endif
  ZC_EXPECT(meta.spaceUsed <= 2 * 65536);

  byte buf[7]{};

#if !_WIN32  // Win32 CopyFile() does NOT preserve sparseness.
  {
    // Copy doesn't fill in holes.
    dir->transfer(Path("copy"), WriteMode::CREATE, Path("holes"), TransferMode::COPY);
    auto copy = dir->openFile(Path("copy"));
    ZC_EXPECT(copy->stat().spaceUsed == meta.spaceUsed);
    ZC_EXPECT(copy->read(0, buf) == 7);
    ZC_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == "foobar");

    ZC_EXPECT(copy->read(1 << 20, buf) == 6);
    ZC_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == "foobar");

    ZC_EXPECT(copy->read(1 << 19, buf) == 7);
    ZC_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == StringPtr("\0\0\0\0\0\0", 6));
  }
#endif

  file->truncate(1 << 21);
  file->datasync();
  ZC_EXPECT(file->stat().spaceUsed == meta.spaceUsed);
  ZC_EXPECT(file->read(1 << 20, buf) == 7);
  ZC_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == "foobar");

#if !_WIN32  // Win32 CopyFile() does NOT preserve sparseness.
  {
    dir->transfer(Path("copy"), WriteMode::MODIFY, Path("holes"), TransferMode::COPY);
    auto copy = dir->openFile(Path("copy"));
    ZC_EXPECT(copy->stat().spaceUsed == meta.spaceUsed);
    ZC_EXPECT(copy->read(0, buf) == 7);
    ZC_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == "foobar");

    ZC_EXPECT(copy->read(1 << 20, buf) == 7);
    ZC_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == "foobar");

    ZC_EXPECT(copy->read(1 << 19, buf) == 7);
    ZC_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == StringPtr("\0\0\0\0\0\0", 6));
  }
#endif

  // Try punching a hole with zero().
#if _WIN32
  uint64_t blockSize = 4096;  // TODO(someday): Actually ask the OS.
#else
  struct stat stats;
  ZC_SYSCALL(fstat(ZC_ASSERT_NONNULL(file->getFd()), &stats));
  uint64_t blockSize = stats.st_blksize;
#endif
  file->zero(1 << 20, blockSize);
  file->datasync();
#if !_WIN32 && !__FreeBSD__
  // TODO(someday): This doesn't work on Windows. I don't know why. We're definitely using the
  //   proper ioctl. Oh well. It also doesn't work on FreeBSD-ZFS, due to the bug(?) mentioned
  //   earlier -- the size is just always reported as 512.
  ZC_EXPECT(file->stat().spaceUsed < meta.spaceUsed);
#endif
  ZC_EXPECT(file->read(1 << 20, buf) == 7);
  ZC_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == StringPtr("\0\0\0\0\0\0", 6));
}
#endif

#if !_WIN32  // Only applies to Unix.
// Ensure the current path is correctly computed.
//
// See issue #1425.
ZC_TEST("DiskFilesystem::computeCurrentPath") {
  TempDir tempDir;
  auto dir = tempDir.get();

  // Paths can be PATH_MAX, but the segments which make up that path typically
  // can't exceed 255 bytes.
  auto maxPathSegment = std::string(255, 'a');

  // Create a path which exceeds the 256 byte buffer used in
  // computeCurrentPath.
  auto subdir =
      dir->openSubdir(Path({maxPathSegment, maxPathSegment, "some_path_longer_than_256_bytes"}),
                      WriteMode::CREATE | WriteMode::CREATE_PARENT);

  auto origDir = open(".", O_RDONLY);
  ZC_SYSCALL(fchdir(ZC_ASSERT_NONNULL(subdir->getFd())));
  ZC_DEFER(ZC_SYSCALL(fchdir(origDir)));

  // Test computeCurrentPath indirectly.
  newDiskFilesystem();
}
#endif

}  // namespace
}  // namespace zc
