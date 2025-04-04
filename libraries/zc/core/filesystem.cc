// Copyright (c) 2015 Sandstorm Development Group, Inc. and contributors
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

#include "zc/core/filesystem.h"

#include <map>

#include "zc/core/debug.h"
#include "zc/core/encoding.h"
#include "zc/core/mutex.h"
#include "zc/core/one-of.h"
#include "zc/core/refcount.h"
#include "zc/core/vector.h"

#if __linux__
#include <sys/mman.h>  // for memfd_create()
#endif                 // __linux__

namespace zc {

Path::Path(StringPtr name) : Path(heapString(name)) {}
Path::Path(String&& name) : parts(heapArray<String>(1)) {
  parts[0] = zc::mv(name);
  validatePart(parts[0]);
}

Path::Path(ArrayPtr<const StringPtr> parts) : Path(ZC_MAP(p, parts) { return heapString(p); }) {}
Path::Path(Array<String> partsParam) : Path(zc::mv(partsParam), ALREADY_CHECKED) {
  for (auto& p : parts) { validatePart(p); }
}

Path PathPtr::clone() {
  return Path(ZC_MAP(p, parts) { return heapString(p); }, Path::ALREADY_CHECKED);
}

Path Path::parse(StringPtr path) {
  ZC_REQUIRE(!path.startsWith("/"), "expected a relative path, got absolute", path) {
    // When exceptions are disabled, go on -- the leading '/' will end up ignored.
    break;
  }
  return evalImpl(Vector<String>(countParts(path)), path);
}

Path Path::parseWin32Api(ArrayPtr<const wchar_t> text) {
  auto utf8 = decodeWideString(text);
  return evalWin32Impl(Vector<String>(countPartsWin32(utf8)), utf8, true);
}

Path PathPtr::append(Path&& suffix) const {
  auto newParts = zc::heapArrayBuilder<String>(parts.size() + suffix.parts.size());
  for (auto& p : parts) newParts.add(heapString(p));
  for (auto& p : suffix.parts) newParts.add(zc::mv(p));
  return Path(newParts.finish(), Path::ALREADY_CHECKED);
}
Path Path::append(Path&& suffix) && {
  auto newParts = zc::heapArrayBuilder<String>(parts.size() + suffix.parts.size());
  for (auto& p : parts) newParts.add(zc::mv(p));
  for (auto& p : suffix.parts) newParts.add(zc::mv(p));
  return Path(newParts.finish(), ALREADY_CHECKED);
}
Path PathPtr::append(PathPtr suffix) const {
  auto newParts = zc::heapArrayBuilder<String>(parts.size() + suffix.parts.size());
  for (auto& p : parts) newParts.add(heapString(p));
  for (auto& p : suffix.parts) newParts.add(heapString(p));
  return Path(newParts.finish(), Path::ALREADY_CHECKED);
}
Path Path::append(PathPtr suffix) && {
  auto newParts = zc::heapArrayBuilder<String>(parts.size() + suffix.parts.size());
  for (auto& p : parts) newParts.add(zc::mv(p));
  for (auto& p : suffix.parts) newParts.add(heapString(p));
  return Path(newParts.finish(), ALREADY_CHECKED);
}

Path PathPtr::eval(StringPtr pathText) const {
  if (pathText.startsWith("/")) {
    // Optimization: avoid copying parts that will just be dropped.
    return Path::evalImpl(Vector<String>(Path::countParts(pathText)), pathText);
  } else {
    Vector<String> newParts(parts.size() + Path::countParts(pathText));
    for (auto& p : parts) newParts.add(heapString(p));
    return Path::evalImpl(zc::mv(newParts), pathText);
  }
}
Path Path::eval(StringPtr pathText) && {
  if (pathText.startsWith("/")) {
    // Optimization: avoid copying parts that will just be dropped.
    return evalImpl(Vector<String>(countParts(pathText)), pathText);
  } else {
    Vector<String> newParts(parts.size() + countParts(pathText));
    for (auto& p : parts) newParts.add(zc::mv(p));
    return evalImpl(zc::mv(newParts), pathText);
  }
}

PathPtr PathPtr::basename() const {
  ZC_REQUIRE(parts.size() > 0, "root path has no basename");
  return PathPtr(parts.slice(parts.size() - 1, parts.size()));
}
Path Path::basename() && {
  ZC_REQUIRE(parts.size() > 0, "root path has no basename");
  auto newParts = zc::heapArrayBuilder<String>(1);
  newParts.add(zc::mv(parts[parts.size() - 1]));
  return Path(newParts.finish(), ALREADY_CHECKED);
}

PathPtr PathPtr::parent() const {
  ZC_REQUIRE(parts.size() > 0, "root path has no parent");
  return PathPtr(parts.first(parts.size() - 1));
}
Path Path::parent() && {
  ZC_REQUIRE(parts.size() > 0, "root path has no parent");
  return Path(ZC_MAP(p, parts.first(parts.size() - 1)) { return zc::mv(p); }, ALREADY_CHECKED);
}

String PathPtr::toString(bool absolute) const {
  if (parts.size() == 0) {
    // Special-case empty path.
    return absolute ? zc::str("/") : zc::str(".");
  }

  size_t size = absolute + (parts.size() - 1);
  for (auto& p : parts) size += p.size();

  String result = zc::heapString(size);

  char* ptr = result.begin();
  bool leadingSlash = absolute;
  for (auto& p : parts) {
    if (leadingSlash) *ptr++ = '/';
    leadingSlash = true;
    memcpy(ptr, p.begin(), p.size());
    ptr += p.size();
  }
  ZC_ASSERT(ptr == result.end());

  return result;
}

Path Path::slice(size_t start, size_t end) && {
  return Path(ZC_MAP(p, parts.slice(start, end)) { return zc::mv(p); });
}

bool PathPtr::operator==(PathPtr other) const { return parts == other.parts; }
bool PathPtr::operator<(PathPtr other) const {
  for (size_t i = 0; i < zc::min(parts.size(), other.parts.size()); i++) {
    int comp = strcmp(parts[i].cStr(), other.parts[i].cStr());
    if (comp < 0) return true;
    if (comp > 0) return false;
  }

  return parts.size() < other.parts.size();
}

bool PathPtr::startsWith(PathPtr prefix) const {
  return parts.size() >= prefix.parts.size() && parts.first(prefix.parts.size()) == prefix.parts;
}

bool PathPtr::endsWith(PathPtr suffix) const {
  return parts.size() >= suffix.parts.size() &&
         parts.slice(parts.size() - suffix.parts.size(), parts.size()) == suffix.parts;
}

Path PathPtr::evalWin32(StringPtr pathText) const {
  Vector<String> newParts(parts.size() + Path::countPartsWin32(pathText));
  for (auto& p : parts) newParts.add(heapString(p));
  return Path::evalWin32Impl(zc::mv(newParts), pathText);
}
Path Path::evalWin32(StringPtr pathText) && {
  Vector<String> newParts(parts.size() + countPartsWin32(pathText));
  for (auto& p : parts) newParts.add(zc::mv(p));
  return evalWin32Impl(zc::mv(newParts), pathText);
}

String PathPtr::toWin32StringImpl(bool absolute, bool forApi) const {
  if (parts.size() == 0) {
    // Special-case empty path.
    ZC_REQUIRE(!absolute, "absolute path is missing disk designator") { break; }
    return absolute ? zc::str("\\\\") : zc::str(".");
  }

  bool isUncPath = false;
  if (absolute) {
    if (Path::isWin32Drive(parts[0])) {
      // It's a win32 drive
    } else if (Path::isNetbiosName(parts[0])) {
      isUncPath = true;
    } else {
      ZC_FAIL_REQUIRE("absolute win32 path must start with drive letter or netbios host name",
                      parts[0]);
    }
  } else {
    // Currently we do nothing differently in the forApi case for relative paths.
    forApi = false;
  }

  size_t size =
      forApi ? (isUncPath ? 8 : 4) + (parts.size() - 1) : (isUncPath ? 2 : 0) + (parts.size() - 1);
  for (auto& p : parts) size += p.size();

  String result = heapString(size);

  char* ptr = result.begin();

  if (forApi) {
    *ptr++ = '\\';
    *ptr++ = '\\';
    *ptr++ = '?';
    *ptr++ = '\\';
    if (isUncPath) {
      *ptr++ = 'U';
      *ptr++ = 'N';
      *ptr++ = 'C';
      *ptr++ = '\\';
    }
  } else {
    if (isUncPath) {
      *ptr++ = '\\';
      *ptr++ = '\\';
    }
  }

  bool leadingSlash = false;
  for (auto& p : parts) {
    if (leadingSlash) *ptr++ = '\\';
    leadingSlash = true;

    ZC_REQUIRE(!Path::isWin32Special(p), "path cannot contain DOS reserved name", p) {
      // Recover by blotting out the name with invalid characters which Win32 syscalls will reject.
      for (size_t i = 0; i < p.size(); i++) { *ptr++ = '|'; }
      goto skip;
    }

    memcpy(ptr, p.begin(), p.size());
    ptr += p.size();
  skip:;
  }

  ZC_ASSERT(ptr == result.end());

  // Check for colons (other than in drive letter), which on NTFS would be interpreted as an
  // "alternate data stream", which can lead to surprising results. If we want to support ADS, we
  // should do so using an explicit API. Note that this check also prevents a relative path from
  // appearing to start with a drive letter.
  for (size_t i : zc::indices(result)) {
    if (result[i] == ':') {
      if (absolute && i == (forApi ? 5 : 1)) {
        // False alarm: this is the drive letter.
      } else {
        ZC_FAIL_REQUIRE(
            "colons are prohibited in win32 paths to avoid triggering alternate data streams",
            result) {
          // Recover by using a different character which we know Win32 syscalls will reject.
          result[i] = '|';
          break;
        }
      }
    }
  }

  return result;
}

Array<wchar_t> PathPtr::forWin32Api(bool absolute) const {
  return encodeWideString(toWin32StringImpl(absolute, true), true);
}

// -----------------------------------------------------------------------------

String Path::stripNul(String input) {
  zc::Vector<char> output(input.size());
  for (char c : input) {
    if (c != '\0') output.add(c);
  }
  output.add('\0');
  return String(output.releaseAsArray());
}

void Path::validatePart(StringPtr part) {
  ZC_REQUIRE(part != "" && part != "." && part != "..", "invalid path component", part);
  ZC_REQUIRE(strlen(part.begin()) == part.size(), "NUL character in path component", part);
  ZC_REQUIRE(part.findFirst('/') == zc::none,
             "'/' character in path component; did you mean to use Path::parse()?", part);
}

void Path::evalPart(Vector<String>& parts, ArrayPtr<const char> part) {
  if (part.size() == 0) {
    // Ignore consecutive or trailing '/'s.
  } else if (part.size() == 1 && part[0] == '.') {
    // Refers to current directory; ignore.
  } else if (part.size() == 2 && part[0] == '.' && part[1] == '.') {
    ZC_REQUIRE(parts.size() > 0, "can't use \"..\" to break out of starting directory") {
      // When exceptions are disabled, ignore.
      return;
    }
    parts.removeLast();
  } else {
    auto str = heapString(part);
    ZC_REQUIRE(strlen(str.begin()) == str.size(), "NUL character in path component", str) {
      // When exceptions are disabled, strip out '\0' chars.
      str = stripNul(zc::mv(str));
      break;
    }
    parts.add(zc::mv(str));
  }
}

Path Path::evalImpl(Vector<String>&& parts, StringPtr path) {
  if (path.startsWith("/")) { parts.clear(); }

  size_t partStart = 0;
  for (auto i : zc::indices(path)) {
    if (path[i] == '/') {
      evalPart(parts, path.slice(partStart, i));
      partStart = i + 1;
    }
  }
  evalPart(parts, path.slice(partStart));

  return Path(parts.releaseAsArray(), Path::ALREADY_CHECKED);
}

Path Path::evalWin32Impl(Vector<String>&& parts, StringPtr path, bool fromApi) {
  // Convert all forward slashes to backslashes.
  String ownPath;
  if (!fromApi && path.findFirst('/') != zc::none) {
    ownPath = heapString(path);
    for (char& c : ownPath) {
      if (c == '/') c = '\\';
    }
    path = ownPath;
  }

  // Interpret various forms of absolute paths.
  if (fromApi && path.startsWith("\\\\?\\")) {
    path = path.slice(4);
    if (path.startsWith("UNC\\")) { path = path.slice(4); }

    // The path is absolute.
    parts.clear();
  } else if (path.startsWith("\\\\")) {
    // UNC path.
    path = path.slice(2);

    // This path is absolute. The first component is a server name.
    parts.clear();
  } else if (path.startsWith("\\")) {
    ZC_REQUIRE(!fromApi, "parseWin32Api() requires absolute path");

    // Path is relative to the current drive / network share.
    if (parts.size() >= 1 && isWin32Drive(parts[0])) {
      // Leading \ interpreted as root of current drive.
      parts.truncate(1);
    } else if (parts.size() >= 2) {
      // Leading \ interpreted as root of current network share (which is indicated by the first
      // *two* components of the path).
      parts.truncate(2);
    } else {
      ZC_FAIL_REQUIRE("must specify drive letter", path) {
        // Recover by assuming C drive.
        parts.clear();
        parts.add(zc::str("c:"));
        break;
      }
    }
  } else if ((path.size() == 2 || (path.size() > 2 && path[2] == '\\')) &&
             isWin32Drive(path.first(2))) {
    // Starts with a drive letter.
    parts.clear();
  } else {
    ZC_REQUIRE(!fromApi, "parseWin32Api() requires absolute path");
  }

  size_t partStart = 0;
  for (auto i : zc::indices(path)) {
    if (path[i] == '\\') {
      evalPart(parts, path.slice(partStart, i));
      partStart = i + 1;
    }
  }
  evalPart(parts, path.slice(partStart));

  return Path(parts.releaseAsArray(), Path::ALREADY_CHECKED);
}

size_t Path::countParts(StringPtr path) {
  size_t result = 1;
  for (char c : path) { result += (c == '/'); }
  return result;
}

size_t Path::countPartsWin32(StringPtr path) {
  size_t result = 1;
  for (char c : path) { result += (c == '/' || c == '\\'); }
  return result;
}

bool Path::isWin32Drive(ArrayPtr<const char> part) {
  return part.size() == 2 && part[1] == ':' &&
         (('a' <= part[0] && part[0] <= 'z') || ('A' <= part[0] && part[0] <= 'Z'));
}

bool Path::isNetbiosName(ArrayPtr<const char> part) {
  // Characters must be alphanumeric or '.' or '-'.
  for (char c : part) {
    if (c != '.' && c != '-' && (c < 'a' || 'z' < c) && (c < 'A' || 'Z' < c) &&
        (c < '0' || '9' < c)) {
      return false;
    }
  }

  // Can't be empty nor start or end with a '.' or a '-'.
  return part.size() > 0 && part[0] != '.' && part[0] != '-' && part[part.size() - 1] != '.' &&
         part[part.size() - 1] != '-';
}

bool Path::isWin32Special(StringPtr part) {
  bool isNumbered;
  if (part.size() == 3 || (part.size() > 3 && part[3] == '.')) {
    // Filename is three characters or three characters followed by an extension.
    isNumbered = false;
  } else if ((part.size() == 4 || (part.size() > 4 && part[4] == '.')) && '1' <= part[3] &&
             part[3] <= '9') {
    // Filename is four characters or four characters followed by an extension, and the fourth
    // character is a nonzero digit.
    isNumbered = true;
  } else {
    return false;
  }

  // OK, this could be a Win32 special filename. We need to match the first three letters against
  // the list of specials, case-insensitively.
  char tmp[4]{};
  memcpy(tmp, part.begin(), 3);
  tmp[3] = '\0';
  for (char& c : tmp) {
    if ('A' <= c && c <= 'Z') { c += 'a' - 'A'; }
  }

  StringPtr str(tmp, 3);
  if (isNumbered) {
    // Specials that are followed by a digit.
    return str == "com" || str == "lpt";
  } else {
    // Specials that are not followed by a digit.
    return str == "con" || str == "prn" || str == "aux" || str == "nul";
  }
}

// =======================================================================================

String ReadableFile::readAllText() const {
  String result = heapString(stat().size);
  size_t n = read(0, result.asBytes());
  if (n < result.size()) {
    // Apparently file was truncated concurrently. Reduce to new size to match.
    result = heapString(result.first(n));
  }
  return result;
}

Array<byte> ReadableFile::readAllBytes() const {
  Array<byte> result = heapArray<byte>(stat().size);
  size_t n = read(0, result.asBytes());
  if (n < result.size()) {
    // Apparently file was truncated concurrently. Reduce to new size to match.
    result = heapArray(result.first(n));
  }
  return result;
}

void File::writeAll(ArrayPtr<const byte> bytes) const {
  truncate(0);
  write(0, bytes);
}

void File::writeAll(StringPtr text) const { writeAll(text.asBytes()); }

size_t File::copy(uint64_t offset, const ReadableFile& from, uint64_t fromOffset,
                  uint64_t size) const {
  byte buffer[8192]{};

  size_t result = 0;
  while (size > 0) {
    size_t n = from.read(fromOffset, zc::arrayPtr(buffer, zc::min(sizeof(buffer), size)));
    write(offset, arrayPtr(buffer, n));
    result += n;
    if (n < sizeof(buffer)) {
      // Either we copied the amount requested or we hit EOF.
      break;
    }
    fromOffset += n;
    offset += n;
    size -= n;
  }

  return result;
}

FsNode::Metadata ReadableDirectory::lstat(PathPtr path) const {
  ZC_IF_SOME(meta, tryLstat(path)) { return meta; }
  else {
    ZC_FAIL_REQUIRE("no such file or directory", path) { break; }
    return FsNode::Metadata();
  }
}

Own<const ReadableFile> ReadableDirectory::openFile(PathPtr path) const {
  ZC_IF_SOME(file, tryOpenFile(path)) { return zc::mv(file); }
  else {
    ZC_FAIL_REQUIRE("no such file", path) { break; }
    return newInMemoryFile(nullClock());
  }
}

Own<const ReadableDirectory> ReadableDirectory::openSubdir(PathPtr path) const {
  ZC_IF_SOME(dir, tryOpenSubdir(path)) { return zc::mv(dir); }
  else {
    ZC_FAIL_REQUIRE("no such directory", path) { break; }
    return newInMemoryDirectory(nullClock());
  }
}

String ReadableDirectory::readlink(PathPtr path) const {
  ZC_IF_SOME(p, tryReadlink(path)) { return zc::mv(p); }
  else {
    ZC_FAIL_REQUIRE("not a symlink", path) { break; }
    return zc::str(".");
  }
}

Own<const File> Directory::openFile(PathPtr path, WriteMode mode) const {
  ZC_IF_SOME(f, tryOpenFile(path, mode)) { return zc::mv(f); }
  else if (has(mode, WriteMode::CREATE) && !has(mode, WriteMode::MODIFY)) {
    ZC_FAIL_REQUIRE("file already exists", path) { break; }
  }
  else if (has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    ZC_FAIL_REQUIRE("file does not exist", path) { break; }
  }
  else if (!has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    ZC_FAIL_ASSERT("neither WriteMode::CREATE nor WriteMode::MODIFY was given", path) { break; }
  }
  else {
    // Shouldn't happen.
    ZC_FAIL_ASSERT("tryOpenFile() returned null despite no preconditions", path) { break; }
  }
  return newInMemoryFile(nullClock());
}

Own<AppendableFile> Directory::appendFile(PathPtr path, WriteMode mode) const {
  ZC_IF_SOME(f, tryAppendFile(path, mode)) { return zc::mv(f); }
  else if (has(mode, WriteMode::CREATE) && !has(mode, WriteMode::MODIFY)) {
    ZC_FAIL_REQUIRE("file already exists", path) { break; }
  }
  else if (has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    ZC_FAIL_REQUIRE("file does not exist", path) { break; }
  }
  else if (!has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    ZC_FAIL_ASSERT("neither WriteMode::CREATE nor WriteMode::MODIFY was given", path) { break; }
  }
  else {
    // Shouldn't happen.
    ZC_FAIL_ASSERT("tryAppendFile() returned null despite no preconditions", path) { break; }
  }
  return newFileAppender(newInMemoryFile(nullClock()));
}

Own<const Directory> Directory::openSubdir(PathPtr path, WriteMode mode) const {
  ZC_IF_SOME(f, tryOpenSubdir(path, mode)) { return zc::mv(f); }
  else if (has(mode, WriteMode::CREATE) && !has(mode, WriteMode::MODIFY)) {
    ZC_FAIL_REQUIRE("directory already exists", path) { break; }
  }
  else if (has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    ZC_FAIL_REQUIRE("directory does not exist", path) { break; }
  }
  else if (!has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    ZC_FAIL_ASSERT("neither WriteMode::CREATE nor WriteMode::MODIFY was given", path) { break; }
  }
  else {
    // Shouldn't happen.
    ZC_FAIL_ASSERT("tryOpenSubdir() returned null despite no preconditions", path) { break; }
  }
  return newInMemoryDirectory(nullClock());
}

void Directory::symlink(PathPtr linkpath, StringPtr content, WriteMode mode) const {
  if (!trySymlink(linkpath, content, mode)) {
    if (has(mode, WriteMode::CREATE)) {
      ZC_FAIL_REQUIRE("path already exists", linkpath) { break; }
    } else {
      // Shouldn't happen.
      ZC_FAIL_ASSERT("symlink() returned null despite no preconditions", linkpath) { break; }
    }
  }
}

void Directory::transfer(PathPtr toPath, WriteMode toMode, const Directory& fromDirectory,
                         PathPtr fromPath, TransferMode mode) const {
  if (!tryTransfer(toPath, toMode, fromDirectory, fromPath, mode)) {
    if (has(toMode, WriteMode::CREATE)) {
      ZC_FAIL_REQUIRE("toPath already exists or fromPath doesn't exist", toPath, fromPath) {
        break;
      }
    } else {
      ZC_FAIL_ASSERT("fromPath doesn't exist", fromPath) { break; }
    }
  }
}

static void copyContents(const Directory& to, const ReadableDirectory& from);

static bool tryCopyDirectoryEntry(const Directory& to, PathPtr toPath, WriteMode toMode,
                                  const ReadableDirectory& from, PathPtr fromPath,
                                  FsNode::Type type, bool atomic) {
  // TODO(cleanup): Make this reusable?

  switch (type) {
    case FsNode::Type::FILE: {
      ZC_IF_SOME(fromFile, from.tryOpenFile(fromPath)) {
        if (atomic) {
          auto replacer = to.replaceFile(toPath, toMode);
          replacer->get().copy(0, *fromFile, 0, zc::maxValue);
          return replacer->tryCommit();
        } else
          ZC_IF_SOME(toFile, to.tryOpenFile(toPath, toMode)) {
            toFile->copy(0, *fromFile, 0, zc::maxValue);
            return true;
          }
        else { return false; }
      }
      else {
        // Apparently disappeared. Treat as source-doesn't-exist.
        return false;
      }
    }
    case FsNode::Type::DIRECTORY:
      ZC_IF_SOME(fromSubdir, from.tryOpenSubdir(fromPath)) {
        if (atomic) {
          auto replacer = to.replaceSubdir(toPath, toMode);
          copyContents(replacer->get(), *fromSubdir);
          return replacer->tryCommit();
        } else
          ZC_IF_SOME(toSubdir, to.tryOpenSubdir(toPath, toMode)) {
            copyContents(*toSubdir, *fromSubdir);
            return true;
          }
        else { return false; }
      }
      else {
        // Apparently disappeared. Treat as source-doesn't-exist.
        return false;
      }
    case FsNode::Type::SYMLINK:
      ZC_IF_SOME(content, from.tryReadlink(fromPath)) {
        return to.trySymlink(toPath, content, toMode);
      }
      else {
        // Apparently disappeared. Treat as source-doesn't-exist.
        return false;
      }
      break;

    default:
      // Note: Unclear whether it's better to throw an error here or just ignore it / log a
      //   warning. Can reconsider when we see an actual use case.
      ZC_FAIL_REQUIRE("can only copy files, directories, and symlinks", fromPath) { return false; }
  }
}

static void copyContents(const Directory& to, const ReadableDirectory& from) {
  for (auto& entry : from.listEntries()) {
    Path subPath(zc::mv(entry.name));
    tryCopyDirectoryEntry(to, subPath, WriteMode::CREATE, from, subPath, entry.type, false);
  }
}

bool Directory::tryTransfer(PathPtr toPath, WriteMode toMode, const Directory& fromDirectory,
                            PathPtr fromPath, TransferMode mode) const {
  ZC_REQUIRE(toPath.size() > 0, "can't replace self") { return false; }

  // First try reversing.
  ZC_IF_SOME(result, fromDirectory.tryTransferTo(*this, toPath, toMode, fromPath, mode)) {
    return result;
  }

  switch (mode) {
    case TransferMode::COPY:
      ZC_IF_SOME(meta, fromDirectory.tryLstat(fromPath)) {
        return tryCopyDirectoryEntry(*this, toPath, toMode, fromDirectory, fromPath, meta.type,
                                     true);
      }
      else {
        // Source doesn't exist.
        return false;
      }
    case TransferMode::MOVE:
      // Implement move as copy-then-delete.
      if (!tryTransfer(toPath, toMode, fromDirectory, fromPath, TransferMode::COPY)) {
        return false;
      }
      fromDirectory.remove(fromPath);
      return true;
    case TransferMode::LINK:
      ZC_FAIL_REQUIRE("can't link across different Directory implementations") { return false; }
  }

  ZC_UNREACHABLE;
}

Maybe<bool> Directory::tryTransferTo(const Directory& toDirectory, PathPtr toPath, WriteMode toMode,
                                     PathPtr fromPath, TransferMode mode) const {
  return zc::none;
}

void Directory::remove(PathPtr path) const {
  if (!tryRemove(path)) {
    ZC_FAIL_REQUIRE("path to remove doesn't exist", path) { break; }
  }
}

void Directory::commitFailed(WriteMode mode) {
  if (has(mode, WriteMode::CREATE) && !has(mode, WriteMode::MODIFY)) {
    ZC_FAIL_REQUIRE("replace target already exists") { break; }
  } else if (has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    ZC_FAIL_REQUIRE("replace target does not exist") { break; }
  } else if (!has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    ZC_FAIL_ASSERT("neither WriteMode::CREATE nor WriteMode::MODIFY was given") { break; }
  } else {
    ZC_FAIL_ASSERT("tryCommit() returned null despite no preconditions") { break; }
  }
}

// =======================================================================================

namespace {

class InMemoryFile final : public File, public AtomicRefcounted {
public:
  InMemoryFile(const Clock& clock) : impl(clock) {}

  Own<const FsNode> cloneFsNode() const override { return atomicAddRef(*this); }

  Maybe<int> getFd() const override { return zc::none; }

  Metadata stat() const override {
    auto lock = impl.lockShared();
    uint64_t hash = reinterpret_cast<uintptr_t>(this);
    return Metadata{Type::FILE, lock->size, lock->size, lock->lastModified, 1, hash};
  }

  void sync() const override {}
  void datasync() const override {}
  // no-ops

  size_t read(uint64_t offset, ArrayPtr<byte> buffer) const override {
    auto lock = impl.lockShared();
    if (offset >= lock->size) {
      // Entirely out-of-range.
      return 0;
    }

    size_t readSize = zc::min(buffer.size(), lock->size - offset);
    memcpy(buffer.begin(), lock->bytes.begin() + offset, readSize);
    return readSize;
  }

  Array<const byte> mmap(uint64_t offset, uint64_t size) const override {
    ZC_REQUIRE(offset + size >= offset, "mmap() request overflows uint64");
    auto lock = impl.lockExclusive();
    lock->ensureCapacity(offset + size);

    ArrayDisposer* disposer = new MmapDisposer(atomicAddRef(*this));
    return Array<const byte>(lock->bytes.begin() + offset, size, *disposer);
  }

  Array<byte> mmapPrivate(uint64_t offset, uint64_t size) const override {
    // Return a copy.

    // Allocate exactly the size requested.
    auto result = heapArray<byte>(size);

    // Use read() to fill it.
    size_t actual = read(offset, result);

    // Ignore the rest.
    if (actual < size) { memset(result.begin() + actual, 0, size - actual); }

    return result;
  }

  void write(uint64_t offset, ArrayPtr<const byte> data) const override {
    if (data.size() == 0) return;
    auto lock = impl.lockExclusive();
    lock->modified();
    uint64_t end = offset + data.size();
    ZC_REQUIRE(end >= offset, "write() request overflows uint64");
    lock->ensureCapacity(end);
    lock->size = zc::max(lock->size, end);
    memcpy(lock->bytes.begin() + offset, data.begin(), data.size());
  }

  void zero(uint64_t offset, uint64_t zeroSize) const override {
    if (zeroSize == 0) return;
    auto lock = impl.lockExclusive();
    lock->modified();
    uint64_t end = offset + zeroSize;
    ZC_REQUIRE(end >= offset, "zero() request overflows uint64");
    lock->ensureCapacity(end);
    lock->size = zc::max(lock->size, end);
    memset(lock->bytes.begin() + offset, 0, zeroSize);
  }

  void truncate(uint64_t newSize) const override {
    auto lock = impl.lockExclusive();
    if (newSize < lock->size) {
      lock->modified();
      memset(lock->bytes.begin() + newSize, 0, lock->size - newSize);
      lock->size = newSize;
    } else if (newSize > lock->size) {
      lock->modified();
      lock->ensureCapacity(newSize);
      lock->size = newSize;
    }
  }

  Own<const WritableFileMapping> mmapWritable(uint64_t offset, uint64_t size) const override {
    uint64_t end = offset + size;
    ZC_REQUIRE(end >= offset, "mmapWritable() request overflows uint64");
    auto lock = impl.lockExclusive();
    lock->ensureCapacity(end);
    return heap<WritableFileMappingImpl>(atomicAddRef(*this), lock->bytes.slice(offset, end));
  }

  size_t copy(uint64_t offset, const ReadableFile& from, uint64_t fromOffset,
              uint64_t copySize) const override {
    size_t fromFileSize = from.stat().size;
    if (fromFileSize <= fromOffset) return 0;

    // Clamp size to EOF.
    copySize = zc::min(copySize, fromFileSize - fromOffset);
    if (copySize == 0) return 0;

    auto lock = impl.lockExclusive();

    // Allocate space for the copy.
    uint64_t end = offset + copySize;
    lock->ensureCapacity(end);

    // Read directly into our backing store.
    size_t n = from.read(fromOffset, lock->bytes.slice(offset, end));
    lock->size = zc::max(lock->size, offset + n);

    lock->modified();
    return n;
  }

private:
  struct Impl {
    const Clock& clock;
    Array<byte> bytes;
    size_t size = 0;  // bytes may be larger than this to accommodate mmaps
    Date lastModified;
    uint mmapCount = 0;  // number of mappings outstanding

    Impl(const Clock& clock) : clock(clock), lastModified(clock.now()) {}

    void ensureCapacity(size_t capacity) {
      if (bytes.size() < capacity) {
        ZC_ASSERT(mmapCount == 0,
                  "InMemoryFile cannot resize the file backing store while memory mappings exist.");

        auto newBytes = heapArray<byte>(zc::max(capacity, bytes.size() * 2));
        if (size > 0) {  // placate ubsan; bytes.begin() might be null
          memcpy(newBytes.begin(), bytes.begin(), size);
        }
        memset(newBytes.begin() + size, 0, newBytes.size() - size);
        bytes = zc::mv(newBytes);
      }
    }

    void modified() { lastModified = clock.now(); }
  };
  zc::MutexGuarded<Impl> impl;

  class MmapDisposer final : public ArrayDisposer {
  public:
    MmapDisposer(Own<const InMemoryFile>&& refParam) : ref(zc::mv(refParam)) {
      ++ref->impl.getAlreadyLockedExclusive().mmapCount;
    }
    ~MmapDisposer() noexcept(false) { --ref->impl.lockExclusive()->mmapCount; }

    void disposeImpl(void* firstElement, size_t elementSize, size_t elementCount, size_t capacity,
                     void (*destroyElement)(void*)) const override {
      delete this;
    }

  private:
    Own<const InMemoryFile> ref;
  };

  class WritableFileMappingImpl final : public WritableFileMapping {
  public:
    WritableFileMappingImpl(Own<const InMemoryFile>&& refParam, ArrayPtr<byte> range)
        : ref(zc::mv(refParam)), range(range) {
      ++ref->impl.getAlreadyLockedExclusive().mmapCount;
    }
    ~WritableFileMappingImpl() noexcept(false) { --ref->impl.lockExclusive()->mmapCount; }

    ArrayPtr<byte> get() const override {
      // const_cast OK because WritableFileMapping does indeed provide a writable view despite
      // being const itself.
      return arrayPtr(const_cast<byte*>(range.begin()), range.size());
    }

    void changed(ArrayPtr<byte> slice) const override { ref->impl.lockExclusive()->modified(); }

    void sync(ArrayPtr<byte> slice) const override { ref->impl.lockExclusive()->modified(); }

  private:
    Own<const InMemoryFile> ref;
    ArrayPtr<byte> range;
  };
};

// -----------------------------------------------------------------------------

class InMemoryDirectory final : public Directory, public AtomicRefcounted {
public:
  InMemoryDirectory(const Clock& clock, const InMemoryFileFactory& fileFactory)
      : impl(clock, fileFactory) {}
  InMemoryDirectory(const Clock& clock, const InMemoryFileFactory& fileFactory,
                    const Directory& copyFrom, bool copyFiles)
      : impl(clock, fileFactory, copyFrom, copyFiles) {}

  Own<const FsNode> cloneFsNode() const override { return atomicAddRef(*this); }

  Maybe<int> getFd() const override { return zc::none; }

  Metadata stat() const override {
    auto lock = impl.lockShared();
    uint64_t hash = reinterpret_cast<uintptr_t>(this);
    return Metadata{Type::DIRECTORY, 0, 0, lock->lastModified, 1, hash};
  }

  void sync() const override {}
  void datasync() const override {}
  // no-ops

  Array<String> listNames() const override {
    auto lock = impl.lockShared();
    return ZC_MAP(e, lock->entries) { return heapString(e.first); };
  }

  Array<Entry> listEntries() const override {
    auto lock = impl.lockShared();
    return ZC_MAP(e, lock->entries) {
      FsNode::Type type;
      if (e.second.node.is<SymlinkNode>()) {
        type = FsNode::Type::SYMLINK;
      } else if (e.second.node.is<FileNode>()) {
        type = FsNode::Type::FILE;
      } else {
        ZC_ASSERT(e.second.node.is<DirectoryNode>());
        type = FsNode::Type::DIRECTORY;
      }

      return Entry{type, heapString(e.first)};
    };
  }

  bool exists(PathPtr path) const override {
    if (path.size() == 0) {
      return true;
    } else if (path.size() == 1) {
      auto lock = impl.lockShared();
      ZC_IF_SOME(entry, lock->tryGetEntry(path[0])) { return exists(lock, entry); }
      else { return false; }
    } else {
      ZC_IF_SOME(subdir, tryGetParent(path[0])) {
        return subdir->exists(path.slice(1, path.size()));
      }
      else { return false; }
    }
  }

  Maybe<FsNode::Metadata> tryLstat(PathPtr path) const override {
    if (path.size() == 0) {
      return stat();
    } else if (path.size() == 1) {
      auto lock = impl.lockShared();
      ZC_IF_SOME(entry, lock->tryGetEntry(path[0])) {
        if (entry.node.is<FileNode>()) {
          return entry.node.get<FileNode>().file->stat();
        } else if (entry.node.is<DirectoryNode>()) {
          return entry.node.get<DirectoryNode>().directory->stat();
        } else if (entry.node.is<SymlinkNode>()) {
          auto& link = entry.node.get<SymlinkNode>();
          uint64_t hash = reinterpret_cast<uintptr_t>(link.content.begin());
          return FsNode::Metadata{FsNode::Type::SYMLINK, 0, 0, link.lastModified, 1, hash};
        } else {
          ZC_FAIL_ASSERT("unknown node type") { return zc::none; }
        }
      }
      else { return zc::none; }
    } else {
      ZC_IF_SOME(subdir, tryGetParent(path[0])) {
        return subdir->tryLstat(path.slice(1, path.size()));
      }
      else { return zc::none; }
    }
  }

  Maybe<Own<const ReadableFile>> tryOpenFile(PathPtr path) const override {
    if (path.size() == 0) {
      ZC_FAIL_REQUIRE("not a file") { return zc::none; }
    } else if (path.size() == 1) {
      auto lock = impl.lockShared();
      ZC_IF_SOME(entry, lock->tryGetEntry(path[0])) { return asFile(lock, entry); }
      else { return zc::none; }
    } else {
      ZC_IF_SOME(subdir, tryGetParent(path[0])) {
        return subdir->tryOpenFile(path.slice(1, path.size()));
      }
      else { return zc::none; }
    }
  }

  Maybe<Own<const ReadableDirectory>> tryOpenSubdir(PathPtr path) const override {
    if (path.size() == 0) {
      return clone();
    } else if (path.size() == 1) {
      auto lock = impl.lockShared();
      ZC_IF_SOME(entry, lock->tryGetEntry(path[0])) { return asDirectory(lock, entry); }
      else { return zc::none; }
    } else {
      ZC_IF_SOME(subdir, tryGetParent(path[0])) {
        return subdir->tryOpenSubdir(path.slice(1, path.size()));
      }
      else { return zc::none; }
    }
  }

  Maybe<String> tryReadlink(PathPtr path) const override {
    if (path.size() == 0) {
      ZC_FAIL_REQUIRE("not a symlink") { return zc::none; }
    } else if (path.size() == 1) {
      auto lock = impl.lockShared();
      ZC_IF_SOME(entry, lock->tryGetEntry(path[0])) { return asSymlink(lock, entry); }
      else { return zc::none; }
    } else {
      ZC_IF_SOME(subdir, tryGetParent(path[0])) {
        return subdir->tryReadlink(path.slice(1, path.size()));
      }
      else { return zc::none; }
    }
  }

  Maybe<Own<const File>> tryOpenFile(PathPtr path, WriteMode mode) const override {
    if (path.size() == 0) {
      if (has(mode, WriteMode::MODIFY)) {
        ZC_FAIL_REQUIRE("not a file") { return zc::none; }
      } else if (has(mode, WriteMode::CREATE)) {
        return zc::none;  // already exists (as a directory)
      } else {
        ZC_FAIL_REQUIRE("can't replace self") { return zc::none; }
      }
    } else if (path.size() == 1) {
      auto lock = impl.lockExclusive();
      ZC_IF_SOME(entry, lock->openEntry(path[0], mode)) { return asFile(lock, entry, mode); }
      else { return zc::none; }
    } else {
      ZC_IF_SOME(child, tryGetParent(path[0], mode)) {
        return child->tryOpenFile(path.slice(1, path.size()), mode);
      }
      else { return zc::none; }
    }
  }

  Own<Replacer<File>> replaceFile(PathPtr path, WriteMode mode) const override {
    if (path.size() == 0) {
      ZC_FAIL_REQUIRE("can't replace self") { break; }
    } else if (path.size() == 1) {
      // don't need lock just to construct a file
      return heap<ReplacerImpl<File>>(*this, path[0], impl.getWithoutLock().newFile(), mode);
    } else {
      ZC_IF_SOME(child, tryGetParent(path[0], mode)) {
        return child->replaceFile(path.slice(1, path.size()), mode);
      }
    }
    return heap<BrokenReplacer<File>>(impl.getWithoutLock().newFile());
  }

  Maybe<Own<const Directory>> tryOpenSubdir(PathPtr path, WriteMode mode) const override {
    if (path.size() == 0) {
      if (has(mode, WriteMode::MODIFY)) {
        return atomicAddRef(*this);
      } else if (has(mode, WriteMode::CREATE)) {
        return zc::none;  // already exists
      } else {
        ZC_FAIL_REQUIRE("can't replace self") { return zc::none; }
      }
    } else if (path.size() == 1) {
      auto lock = impl.lockExclusive();
      ZC_IF_SOME(entry, lock->openEntry(path[0], mode)) { return asDirectory(lock, entry, mode); }
      else { return zc::none; }
    } else {
      ZC_IF_SOME(child, tryGetParent(path[0], mode)) {
        return child->tryOpenSubdir(path.slice(1, path.size()), mode);
      }
      else { return zc::none; }
    }
  }

  Own<Replacer<Directory>> replaceSubdir(PathPtr path, WriteMode mode) const override {
    if (path.size() == 0) {
      ZC_FAIL_REQUIRE("can't replace self") { break; }
    } else if (path.size() == 1) {
      // don't need lock just to construct a directory
      return heap<ReplacerImpl<Directory>>(*this, path[0], impl.getWithoutLock().newDirectory(),
                                           mode);
    } else {
      ZC_IF_SOME(child, tryGetParent(path[0], mode)) {
        return child->replaceSubdir(path.slice(1, path.size()), mode);
      }
    }
    return heap<BrokenReplacer<Directory>>(impl.getWithoutLock().newDirectory());
  }

  Maybe<Own<AppendableFile>> tryAppendFile(PathPtr path, WriteMode mode) const override {
    if (path.size() == 0) {
      if (has(mode, WriteMode::MODIFY)) {
        ZC_FAIL_REQUIRE("not a file") { return zc::none; }
      } else if (has(mode, WriteMode::CREATE)) {
        return zc::none;  // already exists (as a directory)
      } else {
        ZC_FAIL_REQUIRE("can't replace self") { return zc::none; }
      }
    } else if (path.size() == 1) {
      auto lock = impl.lockExclusive();
      ZC_IF_SOME(entry, lock->openEntry(path[0], mode)) {
        return asFile(lock, entry, mode).map(newFileAppender);
      }
      else { return zc::none; }
    } else {
      ZC_IF_SOME(child, tryGetParent(path[0], mode)) {
        return child->tryAppendFile(path.slice(1, path.size()), mode);
      }
      else { return zc::none; }
    }
  }

  bool trySymlink(PathPtr path, StringPtr content, WriteMode mode) const override {
    if (path.size() == 0) {
      if (has(mode, WriteMode::CREATE)) {
        return false;
      } else {
        ZC_FAIL_REQUIRE("can't replace self") { return false; }
      }
    } else if (path.size() == 1) {
      auto lock = impl.lockExclusive();
      ZC_IF_SOME(entry, lock->openEntry(path[0], mode)) {
        entry.init(SymlinkNode{lock->clock.now(), heapString(content)});
        lock->modified();
        return true;
      }
      else { return false; }
    } else {
      ZC_IF_SOME(child, tryGetParent(path[0], mode)) {
        return child->trySymlink(path.slice(1, path.size()), content, mode);
      }
      else {
        ZC_FAIL_REQUIRE("couldn't create parent directory") { return false; }
      }
    }
  }

  Own<const File> createTemporary() const override {
    // Don't need lock just to construct a file.
    return impl.getWithoutLock().newFile();
  }

  bool tryTransfer(PathPtr toPath, WriteMode toMode, const Directory& fromDirectory,
                   PathPtr fromPath, TransferMode mode) const override {
    if (toPath.size() == 0) {
      if (has(toMode, WriteMode::CREATE)) {
        return false;
      } else {
        ZC_FAIL_REQUIRE("can't replace self") { return false; }
      }
    } else if (toPath.size() == 1) {
      if (!has(toMode, WriteMode::MODIFY)) {
        // Replacement is not allowed, so we'll have to check upfront if the target path exists.
        // Unfortunately we have to take a lock and then drop it immediately since we can't keep
        // the lock held while accessing `fromDirectory`.
        if (impl.lockShared()->tryGetEntry(toPath[0]) != zc::none) { return false; }
      }

      OneOf<FileNode, DirectoryNode, SymlinkNode> newNode;
      FsNode::Metadata meta;
      ZC_IF_SOME(m, fromDirectory.tryLstat(fromPath)) { meta = m; }
      else { return false; }

      switch (meta.type) {
        case FsNode::Type::FILE: {
          auto file =
              ZC_ASSERT_NONNULL(fromDirectory.tryOpenFile(fromPath, WriteMode::MODIFY),
                                "source node deleted concurrently during transfer", fromPath);

          if (mode == TransferMode::COPY) {
            auto copy = impl.getWithoutLock().newFile();
            copy->copy(0, *file, 0, meta.size);
            file = zc::mv(copy);
          }

          newNode = FileNode{zc::mv(file)};
          break;
        }
        case FsNode::Type::DIRECTORY: {
          auto subdir =
              ZC_ASSERT_NONNULL(fromDirectory.tryOpenSubdir(fromPath, WriteMode::MODIFY),
                                "source node deleted concurrently during transfer", fromPath);

          switch (mode) {
            case TransferMode::COPY:
              // Copying is straightforward: Make a deep copy of the entire directory tree,
              // including file contents.
              subdir = impl.getWithoutLock().copyDirectory(*subdir, /* copyFiles = */ true);
              break;

            case TransferMode::LINK:
              // To "link", we can safely just place `subdir` directly into our own tree.
              break;

            case TransferMode::MOVE:
              // Moving may be tricky:
              //
              // If `fromDirectory` is an `InMemoryDirectory`, then we know that removing the
              // subdir just unlinks the object without modifying it, so we can safely just link it
              // into our own tree.
              //
              // However, if `fromDirectory` is a disk directory, then removing the subdir will
              // likely perform a recursive delete, thus leaving `subdir` pointing to an empty
              // directory. If we link that into our tree, it's useless. So, instead, perform a
              // deep copy of the directory tree upfront, into an InMemoryDirectory. However, file
              // content need not be copied, since unlinked files keep their contents until closed.
              if (zc::dynamicDowncastIfAvailable<const InMemoryDirectory>(fromDirectory) ==
                  zc::none) {
                subdir = impl.getWithoutLock().copyDirectory(*subdir, /* copyFiles = */ false);
              }
              break;
          }

          newNode = DirectoryNode{zc::mv(subdir)};
          break;
        }
        case FsNode::Type::SYMLINK: {
          auto link =
              ZC_ASSERT_NONNULL(fromDirectory.tryReadlink(fromPath),
                                "source node deleted concurrently during transfer", fromPath);

          newNode = SymlinkNode{meta.lastModified, zc::mv(link)};
          break;
        }
        default:
          ZC_FAIL_REQUIRE("InMemoryDirectory can't link an inode of this type", fromPath);
      }

      if (mode == TransferMode::MOVE) {
        ZC_ASSERT(fromDirectory.tryRemove(fromPath), "couldn't move node", fromPath);
      }

      // Take the lock to insert the entry into our map. Remember that it's important we do not
      // manipulate `fromDirectory` while the lock is held, since it could be the same directory.
      {
        auto lock = impl.lockExclusive();
        ZC_IF_SOME(targetEntry, lock->openEntry(toPath[0], toMode)) {
          targetEntry.init(zc::mv(newNode));
          ;
        }
        else { return false; }
        lock->modified();
      }

      return true;
    } else {
      // TODO(someday): Ideally we wouldn't create parent directories if fromPath doesn't exist.
      //   This requires a different approach to the code here, though.
      ZC_IF_SOME(child, tryGetParent(toPath[0], toMode)) {
        return child->tryTransfer(toPath.slice(1, toPath.size()), toMode, fromDirectory, fromPath,
                                  mode);
      }
      else { return false; }
    }
  }

  Maybe<bool> tryTransferTo(const Directory& toDirectory, PathPtr toPath, WriteMode toMode,
                            PathPtr fromPath, TransferMode mode) const override {
    if (fromPath.size() <= 1) {
      // If `fromPath` is in this directory (or *is* this directory) then we don't have any
      // optimizations.
      return zc::none;
    }

    // `fromPath` is in a subdirectory. It could turn out that that subdirectory is not an
    // InMemoryDirectory and is instead something `toDirectory` is friendly with. So let's follow
    // the path.

    ZC_IF_SOME(child, tryGetParent(fromPath[0], WriteMode::MODIFY)) {
      // OK, switch back to tryTransfer() but use the subdirectory.
      return toDirectory.tryTransfer(toPath, toMode, *child, fromPath.slice(1, fromPath.size()),
                                     mode);
    }
    else {
      // Hmm, doesn't exist. Fall back to standard path.
      return zc::none;
    }
  }

  bool tryRemove(PathPtr path) const override {
    if (path.size() == 0) {
      ZC_FAIL_REQUIRE("can't remove self from self") { return false; }
    } else if (path.size() == 1) {
      auto lock = impl.lockExclusive();
      auto iter = lock->entries.find(path[0]);
      if (iter == lock->entries.end()) {
        return false;
      } else {
        lock->entries.erase(iter);
        lock->modified();
        return true;
      }
    } else {
      ZC_IF_SOME(child, tryGetParent(path[0], WriteMode::MODIFY)) {
        return child->tryRemove(path.slice(1, path.size()));
      }
      else { return false; }
    }
  }

private:
  struct FileNode {
    Own<const File> file;
  };
  struct DirectoryNode {
    Own<const Directory> directory;
  };
  struct SymlinkNode {
    Date lastModified;
    String content;

    Path parse() const {
      ZC_CONTEXT("parsing symlink", content);
      return Path::parse(content);
    }
  };

  struct EntryImpl {
    String name;
    OneOf<FileNode, DirectoryNode, SymlinkNode> node;

    EntryImpl(String&& name) : name(zc::mv(name)) {}

    Own<const File> init(FileNode&& value) {
      return node.init<FileNode>(zc::mv(value)).file->clone();
    }
    Own<const Directory> init(DirectoryNode&& value) {
      return node.init<DirectoryNode>(zc::mv(value)).directory->clone();
    }
    void init(SymlinkNode&& value) { node.init<SymlinkNode>(zc::mv(value)); }
    bool init(OneOf<FileNode, DirectoryNode, SymlinkNode>&& value) {
      node = zc::mv(value);
      return node != nullptr;
    }

    void set(Own<const File>&& value) { node.init<FileNode>(FileNode{zc::mv(value)}); }
    void set(Own<const Directory>&& value) {
      node.init<DirectoryNode>(DirectoryNode{zc::mv(value)});
    }
  };

  template <typename T>
  class ReplacerImpl final : public Replacer<T> {
  public:
    ReplacerImpl(const InMemoryDirectory& directory, zc::StringPtr name, Own<const T> inner,
                 WriteMode mode)
        : Replacer<T>(mode),
          directory(atomicAddRef(directory)),
          name(heapString(name)),
          inner(zc::mv(inner)) {}

    const T& get() override { return *inner; }

    bool tryCommit() override {
      ZC_REQUIRE(!committed, "commit() already called") { return true; }

      auto lock = directory->impl.lockExclusive();
      ZC_IF_SOME(entry, lock->openEntry(name, Replacer<T>::mode)) {
        entry.set(inner->clone());
        lock->modified();
        return true;
      }
      else { return false; }
    }

  private:
    bool committed = false;
    Own<const InMemoryDirectory> directory;
    zc::String name;
    Own<const T> inner;
  };

  template <typename T>
  class BrokenReplacer final : public Replacer<T> {
    // For recovery path when exceptions are disabled.

  public:
    BrokenReplacer(Own<const T> inner)
        : Replacer<T>(WriteMode::CREATE | WriteMode::MODIFY), inner(zc::mv(inner)) {}

    const T& get() override { return *inner; }
    bool tryCommit() override { return false; }

  private:
    Own<const T> inner;
  };

  struct Impl {
    const Clock& clock;
    const InMemoryFileFactory& fileFactory;

    std::map<StringPtr, EntryImpl> entries;
    // Note: If this changes to a non-sorted map, listNames() and listEntries() must be updated to
    //   sort their results.

    Date lastModified;

    Impl(const Clock& clock, const InMemoryFileFactory& fileFactory)
        : clock(clock), fileFactory(fileFactory), lastModified(clock.now()) {}

    Impl(const Clock& clock, const InMemoryFileFactory& fileFactory, const Directory& copyFrom,
         bool copyFiles)
        : clock(clock), fileFactory(fileFactory), lastModified(clock.now()) {
      // Implements copyDirectory() (see below).
      for (auto& fromEntry : copyFrom.listEntries()) {
        zc::Path filename({zc::mv(fromEntry.name)});
        OneOf<FileNode, DirectoryNode, SymlinkNode> newNode;
        switch (fromEntry.type) {
          case FsNode::Type::FILE: {
            ZC_IF_SOME(file, copyFrom.tryOpenFile(filename, WriteMode::MODIFY)) {
              if (copyFiles) {
                auto copy = newFile();
                copy->copy(0, *file, 0, zc::maxValue);
                file = zc::mv(copy);
              }

              newNode = FileNode{zc::mv(file)};
              break;
            }
            else { continue; }
          }

          case FsNode::Type::DIRECTORY: {
            ZC_IF_SOME(subdir, copyFrom.tryOpenSubdir(filename, WriteMode::MODIFY)) {
              subdir = copyDirectory(*subdir, copyFiles);
              newNode = DirectoryNode{zc::mv(subdir)};
              break;
            }
            else { continue; }
          }

          case FsNode::Type::SYMLINK: {
            ZC_IF_SOME(link, copyFrom.tryReadlink(filename)) {
              ZC_IF_SOME(metadata, copyFrom.tryLstat(filename)) {
                newNode = SymlinkNode{metadata.lastModified, zc::mv(link)};
                break;
              }
              else { continue; }
            }
            else { continue; }
          }

          default:
            ZC_LOG(ERROR, "couldn't copy node of type not supported by InMemoryDirectory",
                   filename);
            continue;
        }

        ZC_ASSERT(newNode != nullptr);

        EntryImpl entry(zc::mv(filename)[0]);
        StringPtr nameRef = entry.name;
        entry.init(zc::mv(newNode));
        ZC_ASSERT(entries.insert(std::make_pair(nameRef, zc::mv(entry))).second);
      }
    }

    Own<const File> newFile() const {
      // Construct a new empty file. Note: This function is expected to work without the lock held.
      return fileFactory.create(clock);
    }
    Own<const Directory> newDirectory() const {
      // Construct a new empty directory. Note: This function is expected to work without the lock
      // held.
      return newInMemoryDirectory(clock, fileFactory);
    }

    Own<const Directory> copyDirectory(const Directory& other, bool copyFiles) const {
      // Creates an in-memory deep copy of the given directory object. If `copyFiles` is true, then
      // file contents are copied too, otherwise they are just linked.
      return zc::atomicRefcounted<InMemoryDirectory>(clock, fileFactory, other, copyFiles);
    }

    Maybe<EntryImpl&> openEntry(zc::StringPtr name, WriteMode mode) {
      // TODO(perf): We could avoid a copy if the entry exists, at the expense of a double-lookup
      //   if it doesn't. Maybe a better map implementation will solve everything?
      return openEntry(heapString(name), mode);
    }

    Maybe<EntryImpl&> openEntry(String&& name, WriteMode mode) {
      if (has(mode, WriteMode::CREATE)) {
        EntryImpl entry(zc::mv(name));
        StringPtr nameRef = entry.name;
        auto insertResult = entries.insert(std::make_pair(nameRef, zc::mv(entry)));

        if (!insertResult.second && !has(mode, WriteMode::MODIFY)) {
          // Entry already existed and MODIFY not specified.
          return zc::none;
        }

        return insertResult.first->second;
      } else if (has(mode, WriteMode::MODIFY)) {
        return tryGetEntry(name);
      } else {
        // Neither CREATE nor MODIFY specified: precondition always fails.
        return zc::none;
      }
    }

    zc::Maybe<const EntryImpl&> tryGetEntry(zc::StringPtr name) const {
      auto iter = entries.find(name);
      if (iter == entries.end()) {
        return zc::none;
      } else {
        return iter->second;
      }
    }

    zc::Maybe<EntryImpl&> tryGetEntry(zc::StringPtr name) {
      auto iter = entries.find(name);
      if (iter == entries.end()) {
        return zc::none;
      } else {
        return iter->second;
      }
    }

    void modified() { lastModified = clock.now(); }
  };

  zc::MutexGuarded<Impl> impl;

  bool exists(zc::Locked<const Impl>& lock, const EntryImpl& entry) const {
    if (entry.node.is<SymlinkNode>()) {
      auto newPath = entry.node.get<SymlinkNode>().parse();
      lock.release();
      return exists(newPath);
    } else {
      return true;
    }
  }
  Maybe<Own<const ReadableFile>> asFile(zc::Locked<const Impl>& lock,
                                        const EntryImpl& entry) const {
    if (entry.node.is<FileNode>()) {
      return entry.node.get<FileNode>().file->clone();
    } else if (entry.node.is<SymlinkNode>()) {
      auto newPath = entry.node.get<SymlinkNode>().parse();
      lock.release();
      return tryOpenFile(newPath);
    } else {
      ZC_FAIL_REQUIRE("not a file") { return zc::none; }
    }
  }
  Maybe<Own<const ReadableDirectory>> asDirectory(zc::Locked<const Impl>& lock,
                                                  const EntryImpl& entry) const {
    if (entry.node.is<DirectoryNode>()) {
      return entry.node.get<DirectoryNode>().directory->clone();
    } else if (entry.node.is<SymlinkNode>()) {
      auto newPath = entry.node.get<SymlinkNode>().parse();
      lock.release();
      return tryOpenSubdir(newPath);
    } else {
      ZC_FAIL_REQUIRE("not a directory") { return zc::none; }
    }
  }
  Maybe<String> asSymlink(zc::Locked<const Impl>& lock, const EntryImpl& entry) const {
    if (entry.node.is<SymlinkNode>()) {
      return heapString(entry.node.get<SymlinkNode>().content);
    } else {
      ZC_FAIL_REQUIRE("not a symlink") { return zc::none; }
    }
  }

  Maybe<Own<const File>> asFile(zc::Locked<Impl>& lock, EntryImpl& entry, WriteMode mode) const {
    if (entry.node.is<FileNode>()) {
      return entry.node.get<FileNode>().file->clone();
    } else if (entry.node.is<SymlinkNode>()) {
      // CREATE_PARENT doesn't apply to creating the parents of a symlink target. However, the
      // target itself can still be created.
      auto newPath = entry.node.get<SymlinkNode>().parse();
      lock.release();
      return tryOpenFile(newPath, mode - WriteMode::CREATE_PARENT);
    } else if (entry.node == nullptr) {
      ZC_ASSERT(has(mode, WriteMode::CREATE));
      lock->modified();
      return entry.init(FileNode{lock->newFile()});
    } else {
      ZC_FAIL_REQUIRE("not a file") { return zc::none; }
    }
  }
  Maybe<Own<const Directory>> asDirectory(zc::Locked<Impl>& lock, EntryImpl& entry,
                                          WriteMode mode) const {
    if (entry.node.is<DirectoryNode>()) {
      return entry.node.get<DirectoryNode>().directory->clone();
    } else if (entry.node.is<SymlinkNode>()) {
      // CREATE_PARENT doesn't apply to creating the parents of a symlink target. However, the
      // target itself can still be created.
      auto newPath = entry.node.get<SymlinkNode>().parse();
      lock.release();
      return tryOpenSubdir(newPath, mode - WriteMode::CREATE_PARENT);
    } else if (entry.node == nullptr) {
      ZC_ASSERT(has(mode, WriteMode::CREATE));
      lock->modified();
      return entry.init(DirectoryNode{lock->newDirectory()});
    } else {
      ZC_FAIL_REQUIRE("not a directory") { return zc::none; }
    }
  }

  zc::Maybe<Own<const ReadableDirectory>> tryGetParent(zc::StringPtr name) const {
    auto lock = impl.lockShared();
    ZC_IF_SOME(entry, impl.lockShared()->tryGetEntry(name)) { return asDirectory(lock, entry); }
    else { return zc::none; }
  }

  zc::Maybe<Own<const Directory>> tryGetParent(zc::StringPtr name, WriteMode mode) const {
    // Get a directory which is a parent of the eventual target. If `mode` includes
    // WriteMode::CREATE_PARENTS, possibly create the parent directory.

    auto lock = impl.lockExclusive();

    WriteMode parentMode = has(mode, WriteMode::CREATE) && has(mode, WriteMode::CREATE_PARENT)
                               ? WriteMode::CREATE | WriteMode::MODIFY  // create parent
                               : WriteMode::MODIFY;                     // don't create parent

    // Possibly create parent.
    ZC_IF_SOME(entry, lock->openEntry(name, parentMode)) {
      if (entry.node.is<DirectoryNode>()) {
        return entry.node.get<DirectoryNode>().directory->clone();
      } else if (entry.node == nullptr) {
        lock->modified();
        return entry.init(DirectoryNode{lock->newDirectory()});
      }
      // Continue on.
    }

    if (has(mode, WriteMode::CREATE)) {
      // CREATE is documented as returning null when the file already exists. In this case, the
      // file does NOT exist because the parent directory does not exist or is not a directory.
      ZC_FAIL_REQUIRE("parent is not a directory") { return zc::none; }
    } else {
      return zc::none;
    }
  }
};

// -----------------------------------------------------------------------------

class AppendableFileImpl final : public AppendableFile {
public:
  AppendableFileImpl(Own<const File>&& fileParam) : file(zc::mv(fileParam)) {}

  Own<const FsNode> cloneFsNode() const override { return heap<AppendableFileImpl>(file->clone()); }

  Maybe<int> getFd() const override { return zc::none; }

  Metadata stat() const override { return file->stat(); }

  void sync() const override { file->sync(); }
  void datasync() const override { file->datasync(); }

  void write(ArrayPtr<const byte> data) override { file->write(file->stat().size, data); }

private:
  Own<const File> file;
};

}  // namespace

// -----------------------------------------------------------------------------

Own<File> newInMemoryFile(const Clock& clock) { return atomicRefcounted<InMemoryFile>(clock); }
Own<Directory> newInMemoryDirectory(const Clock& clock, const InMemoryFileFactory& fileFactory) {
  return atomicRefcounted<InMemoryDirectory>(clock, fileFactory);
}
Own<AppendableFile> newFileAppender(Own<const File> inner) {
  return heap<AppendableFileImpl>(zc::mv(inner));
}

const InMemoryFileFactory& defaultInMemoryFileFactory() {
  class FactoryImpl : public InMemoryFileFactory {
  public:
    zc::Own<const File> create(const Clock& clock) const override { return newInMemoryFile(clock); }
  };
  static const FactoryImpl instance;
  return instance;
}

#if __linux__

Own<File> newMemfdFile(uint flags) {
  int fd;
  ZC_SYSCALL(fd = memfd_create("zc-memfd", flags | MFD_CLOEXEC));
  return newDiskFile(AutoCloseFd(fd));
}

const InMemoryFileFactory& memfdInMemoryFileFactory() {
  class FactoryImpl : public InMemoryFileFactory {
  public:
    zc::Own<const File> create(const Clock& clock) const override { return newMemfdFile(0); }
  };
  static const FactoryImpl instance;
  return instance;
}

#endif  // __linux__

}  // namespace zc
