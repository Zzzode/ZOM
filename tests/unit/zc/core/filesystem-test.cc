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

#include "zc/core/filesystem.h"

#include <wchar.h>

#include "zc/ztest/test.h"

#if __linux__
#include <unistd.h>
#endif  // __linux__

namespace zc {
namespace {

ZC_TEST("Path") {
  ZC_EXPECT(Path(nullptr).toString() == ".");
  ZC_EXPECT(Path(nullptr).toString(true) == "/");
  ZC_EXPECT(Path("foo").toString() == "foo");
  ZC_EXPECT(Path("foo").toString(true) == "/foo");

  ZC_EXPECT(Path({"foo", "bar"}).toString() == "foo/bar");
  ZC_EXPECT(Path({"foo", "bar"}).toString(true) == "/foo/bar");

  ZC_EXPECT(Path::parse("foo/bar").toString() == "foo/bar");
  ZC_EXPECT(Path::parse("foo//bar").toString() == "foo/bar");
  ZC_EXPECT(Path::parse("foo/./bar").toString() == "foo/bar");
  ZC_EXPECT(Path::parse("foo/../bar").toString() == "bar");
  ZC_EXPECT(Path::parse("foo/bar/..").toString() == "foo");
  ZC_EXPECT(Path::parse("foo/bar/../..").toString() == ".");

  ZC_EXPECT(Path({"foo", "bar"}).eval("baz").toString() == "foo/bar/baz");
  ZC_EXPECT(Path({"foo", "bar"}).eval("./baz").toString() == "foo/bar/baz");
  ZC_EXPECT(Path({"foo", "bar"}).eval("baz/qux").toString() == "foo/bar/baz/qux");
  ZC_EXPECT(Path({"foo", "bar"}).eval("baz//qux").toString() == "foo/bar/baz/qux");
  ZC_EXPECT(Path({"foo", "bar"}).eval("baz/./qux").toString() == "foo/bar/baz/qux");
  ZC_EXPECT(Path({"foo", "bar"}).eval("baz/../qux").toString() == "foo/bar/qux");
  ZC_EXPECT(Path({"foo", "bar"}).eval("baz/qux/..").toString() == "foo/bar/baz");
  ZC_EXPECT(Path({"foo", "bar"}).eval("../baz").toString() == "foo/baz");
  ZC_EXPECT(Path({"foo", "bar"}).eval("baz/../../qux/").toString() == "foo/qux");
  ZC_EXPECT(Path({"foo", "bar"}).eval("/baz/qux").toString() == "baz/qux");
  ZC_EXPECT(Path({"foo", "bar"}).eval("//baz/qux").toString() == "baz/qux");
  ZC_EXPECT(Path({"foo", "bar"}).eval("/baz/../qux").toString() == "qux");

  ZC_EXPECT(Path({"foo", "bar"}).basename()[0] == "bar");
  ZC_EXPECT(Path({"foo", "bar", "baz"}).parent().toString() == "foo/bar");

  ZC_EXPECT(Path({"foo", "bar"}).append("baz").toString() == "foo/bar/baz");
  ZC_EXPECT(Path({"foo", "bar"}).append(Path({"baz", "qux"})).toString() == "foo/bar/baz/qux");

  {
    // Test methods which are overloaded for && on a non-rvalue path.
    Path path({"foo", "bar"});
    ZC_EXPECT(path.eval("baz").toString() == "foo/bar/baz");
    ZC_EXPECT(path.eval("./baz").toString() == "foo/bar/baz");
    ZC_EXPECT(path.eval("baz/qux").toString() == "foo/bar/baz/qux");
    ZC_EXPECT(path.eval("baz//qux").toString() == "foo/bar/baz/qux");
    ZC_EXPECT(path.eval("baz/./qux").toString() == "foo/bar/baz/qux");
    ZC_EXPECT(path.eval("baz/../qux").toString() == "foo/bar/qux");
    ZC_EXPECT(path.eval("baz/qux/..").toString() == "foo/bar/baz");
    ZC_EXPECT(path.eval("../baz").toString() == "foo/baz");
    ZC_EXPECT(path.eval("baz/../../qux/").toString() == "foo/qux");
    ZC_EXPECT(path.eval("/baz/qux").toString() == "baz/qux");
    ZC_EXPECT(path.eval("/baz/../qux").toString() == "qux");

    ZC_EXPECT(path.basename()[0] == "bar");
    ZC_EXPECT(path.parent().toString() == "foo");

    ZC_EXPECT(path.append("baz").toString() == "foo/bar/baz");
    ZC_EXPECT(path.append(Path({"baz", "qux"})).toString() == "foo/bar/baz/qux");
  }

  ZC_EXPECT(zc::str(Path({"foo", "bar"})) == "foo/bar");
}

ZC_TEST("Path comparisons") {
  ZC_EXPECT(Path({"foo", "bar"}) == Path({"foo", "bar"}));
  ZC_EXPECT(!(Path({"foo", "bar"}) != Path({"foo", "bar"})));
  ZC_EXPECT(Path({"foo", "bar"}) != Path({"foo", "baz"}));
  ZC_EXPECT(!(Path({"foo", "bar"}) == Path({"foo", "baz"})));

  ZC_EXPECT(Path({"foo", "bar"}) != Path({"fob", "bar"}));
  ZC_EXPECT(Path({"foo", "bar"}) != Path({"foo", "bar", "baz"}));
  ZC_EXPECT(Path({"foo", "bar", "baz"}) != Path({"foo", "bar"}));

  ZC_EXPECT(Path({"foo", "bar"}) <= Path({"foo", "bar"}));
  ZC_EXPECT(Path({"foo", "bar"}) >= Path({"foo", "bar"}));
  ZC_EXPECT(!(Path({"foo", "bar"}) < Path({"foo", "bar"})));
  ZC_EXPECT(!(Path({"foo", "bar"}) > Path({"foo", "bar"})));

  ZC_EXPECT(Path({"foo", "bar"}) < Path({"foo", "bar", "baz"}));
  ZC_EXPECT(!(Path({"foo", "bar"}) > Path({"foo", "bar", "baz"})));
  ZC_EXPECT(Path({"foo", "bar", "baz"}) > Path({"foo", "bar"}));
  ZC_EXPECT(!(Path({"foo", "bar", "baz"}) < Path({"foo", "bar"})));

  ZC_EXPECT(Path({"foo", "bar"}) < Path({"foo", "baz"}));
  ZC_EXPECT(Path({"foo", "bar"}) > Path({"foo", "baa"}));
  ZC_EXPECT(Path({"foo", "bar"}) > Path({"foo"}));

  ZC_EXPECT(Path({"foo", "bar"}).startsWith(Path({})));
  ZC_EXPECT(Path({"foo", "bar"}).startsWith(Path({"foo"})));
  ZC_EXPECT(Path({"foo", "bar"}).startsWith(Path({"foo", "bar"})));
  ZC_EXPECT(!Path({"foo", "bar"}).startsWith(Path({"foo", "bar", "baz"})));
  ZC_EXPECT(!Path({"foo", "bar"}).startsWith(Path({"foo", "baz"})));
  ZC_EXPECT(!Path({"foo", "bar"}).startsWith(Path({"baz", "foo", "bar"})));
  ZC_EXPECT(!Path({"foo", "bar"}).startsWith(Path({"baz"})));

  ZC_EXPECT(Path({"foo", "bar"}).endsWith(Path({})));
  ZC_EXPECT(Path({"foo", "bar"}).endsWith(Path({"bar"})));
  ZC_EXPECT(Path({"foo", "bar"}).endsWith(Path({"foo", "bar"})));
  ZC_EXPECT(!Path({"foo", "bar"}).endsWith(Path({"baz", "foo", "bar"})));
  ZC_EXPECT(!Path({"foo", "bar"}).endsWith(Path({"fob", "bar"})));
  ZC_EXPECT(!Path({"foo", "bar"}).endsWith(Path({"foo", "bar", "baz"})));
  ZC_EXPECT(!Path({"foo", "bar"}).endsWith(Path({"baz"})));
}

ZC_TEST("Path exceptions") {
  ZC_EXPECT_THROW_MESSAGE("invalid path component", Path(""));
  ZC_EXPECT_THROW_MESSAGE("invalid path component", Path("."));
  ZC_EXPECT_THROW_MESSAGE("invalid path component", Path(".."));
  ZC_EXPECT_THROW_MESSAGE("NUL character", Path(StringPtr("foo\0bar", 7)));

  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting", Path::parse(".."));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting", Path::parse("../foo"));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting", Path::parse("foo/../.."));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("expected a relative path", Path::parse("/foo"));

  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("NUL character", Path::parse(zc::StringPtr("foo\0bar", 7)));

  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
                                      Path({"foo", "bar"}).eval("../../.."));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
                                      Path({"foo", "bar"}).eval("../baz/../../.."));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
                                      Path({"foo", "bar"}).eval("baz/../../../.."));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting", Path({"foo", "bar"}).eval("/.."));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
                                      Path({"foo", "bar"}).eval("/baz/../.."));

  ZC_EXPECT_THROW_MESSAGE("root path has no basename", Path(nullptr).basename());
  ZC_EXPECT_THROW_MESSAGE("root path has no parent", Path(nullptr).parent());
}

constexpr zc::ArrayPtr<const wchar_t> operator""_a(const wchar_t* str, size_t n) {
  return {str, n};
}

ZC_TEST("Win32 Path") {
  ZC_EXPECT(Path({"foo", "bar"}).toWin32String() == "foo\\bar");
  ZC_EXPECT(Path({"foo", "bar"}).toWin32String(true) == "\\\\foo\\bar");
  ZC_EXPECT(Path({"c:", "foo", "bar"}).toWin32String(true) == "c:\\foo\\bar");
  ZC_EXPECT(Path({"A:", "foo", "bar"}).toWin32String(true) == "A:\\foo\\bar");

  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("baz").toWin32String() == "foo\\bar\\baz");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("./baz").toWin32String() == "foo\\bar\\baz");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("baz/qux").toWin32String() == "foo\\bar\\baz\\qux");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("baz//qux").toWin32String() == "foo\\bar\\baz\\qux");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("baz/./qux").toWin32String() == "foo\\bar\\baz\\qux");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("baz/../qux").toWin32String() == "foo\\bar\\qux");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("baz/qux/..").toWin32String() == "foo\\bar\\baz");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("../baz").toWin32String() == "foo\\baz");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("baz/../../qux/").toWin32String() == "foo\\qux");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32(".\\baz").toWin32String() == "foo\\bar\\baz");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("baz\\qux").toWin32String() == "foo\\bar\\baz\\qux");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("baz\\\\qux").toWin32String() == "foo\\bar\\baz\\qux");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("baz\\.\\qux").toWin32String() == "foo\\bar\\baz\\qux");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("baz\\..\\qux").toWin32String() == "foo\\bar\\qux");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("baz\\qux\\..").toWin32String() == "foo\\bar\\baz");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("..\\baz").toWin32String() == "foo\\baz");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("baz\\..\\..\\qux\\").toWin32String() == "foo\\qux");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("baz\\../..\\qux/").toWin32String() == "foo\\qux");

  ZC_EXPECT(Path({"c:", "foo", "bar"}).evalWin32("/baz/qux").toWin32String(true) == "c:\\baz\\qux");
  ZC_EXPECT(Path({"c:", "foo", "bar"}).evalWin32("\\baz\\qux").toWin32String(true) ==
            "c:\\baz\\qux");
  ZC_EXPECT(Path({"c:", "foo", "bar"}).evalWin32("d:\\baz\\qux").toWin32String(true) ==
            "d:\\baz\\qux");
  ZC_EXPECT(Path({"c:", "foo", "bar"}).evalWin32("d:\\baz\\..\\qux").toWin32String(true) ==
            "d:\\qux");
  ZC_EXPECT(Path({"c:", "foo", "bar"}).evalWin32("\\\\baz\\qux").toWin32String(true) ==
            "\\\\baz\\qux");
  ZC_EXPECT(Path({"foo", "bar"}).evalWin32("d:\\baz\\..\\qux").toWin32String(true) == "d:\\qux");
  ZC_EXPECT(Path({"foo", "bar", "baz"}).evalWin32("\\qux").toWin32String(true) ==
            "\\\\foo\\bar\\qux");

  ZC_EXPECT(Path({"foo", "bar"}).forWin32Api(false) == L"foo\\bar");
  ZC_EXPECT(Path({"foo", "bar"}).forWin32Api(true) == L"\\\\?\\UNC\\foo\\bar");
  ZC_EXPECT(Path({"c:", "foo", "bar"}).forWin32Api(true) == L"\\\\?\\c:\\foo\\bar");
  ZC_EXPECT(Path({"A:", "foo", "bar"}).forWin32Api(true) == L"\\\\?\\A:\\foo\\bar");

  ZC_EXPECT(Path::parseWin32Api(L"\\\\?\\c:\\foo\\bar"_a).toString() == "c:/foo/bar");
  ZC_EXPECT(Path::parseWin32Api(L"\\\\?\\UNC\\foo\\bar"_a).toString() == "foo/bar");
  ZC_EXPECT(Path::parseWin32Api(L"c:\\foo\\bar"_a).toString() == "c:/foo/bar");
  ZC_EXPECT(Path::parseWin32Api(L"\\\\foo\\bar"_a).toString() == "foo/bar");
}

ZC_TEST("Win32 Path exceptions") {
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("colons are prohibited",
                                      Path({"c:", "foo", "bar"}).toWin32String());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("colons are prohibited",
                                      Path({"c:", "foo:bar"}).toWin32String(true));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"con"}).toWin32String());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"CON", "bar"}).toWin32String());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"foo", "cOn"}).toWin32String());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"prn"}).toWin32String());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"aux"}).toWin32String());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"NUL"}).toWin32String());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"nul.txt"}).toWin32String());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"com3"}).toWin32String());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"lpt9"}).toWin32String());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"com1.hello"}).toWin32String());

  ZC_EXPECT_THROW_MESSAGE("drive letter or netbios", Path({"?", "foo"}).toWin32String(true));

  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
                                      Path({"foo", "bar"}).evalWin32("../../.."));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
                                      Path({"foo", "bar"}).evalWin32("../baz/../../.."));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
                                      Path({"foo", "bar"}).evalWin32("baz/../../../.."));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
                                      Path({"foo", "bar"}).evalWin32("c:\\..\\.."));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
                                      Path({"c:", "foo", "bar"}).evalWin32("/baz/../../.."));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("must specify drive letter",
                                      Path({"foo"}).evalWin32("\\baz\\qux"));
}

ZC_TEST("WriteMode operators") {
  WriteMode createOrModify = WriteMode::CREATE | WriteMode::MODIFY;

  ZC_EXPECT(has(createOrModify, WriteMode::MODIFY));
  ZC_EXPECT(has(createOrModify, WriteMode::CREATE));
  ZC_EXPECT(!has(createOrModify, WriteMode::CREATE_PARENT));
  ZC_EXPECT(has(createOrModify, createOrModify));
  ZC_EXPECT(!has(createOrModify, createOrModify | WriteMode::CREATE_PARENT));
  ZC_EXPECT(!has(createOrModify, WriteMode::CREATE | WriteMode::CREATE_PARENT));
  ZC_EXPECT(!has(WriteMode::CREATE, createOrModify));

  ZC_EXPECT(createOrModify != WriteMode::MODIFY);
  ZC_EXPECT(createOrModify != WriteMode::CREATE);

  ZC_EXPECT(createOrModify - WriteMode::CREATE == WriteMode::MODIFY);
  ZC_EXPECT(WriteMode::CREATE + WriteMode::MODIFY == createOrModify);

  // Adding existing bit / subtracting non-existing bit are no-ops.
  ZC_EXPECT(createOrModify + WriteMode::MODIFY == createOrModify);
  ZC_EXPECT(createOrModify - WriteMode::CREATE_PARENT == createOrModify);
}

// ======================================================================================

class TestClock final : public Clock {
public:
  void tick() { time += 1 * SECONDS; }

  Date now() const override { return time; }

  void expectChanged(const FsNode& file) {
    ZC_EXPECT(file.stat().lastModified == time);
    time += 1 * SECONDS;
  }
  void expectUnchanged(const FsNode& file) { ZC_EXPECT(file.stat().lastModified != time); }

private:
  Date time = UNIX_EPOCH + 1 * SECONDS;
};

ZC_TEST("InMemoryFile") {
  TestClock clock;

  auto file = newInMemoryFile(clock);
  clock.expectChanged(*file);

  ZC_EXPECT(file->readAllText() == "");
  clock.expectUnchanged(*file);

  file->writeAll("foo");
  clock.expectChanged(*file);
  ZC_EXPECT(file->readAllText() == "foo");

  file->write(3, StringPtr("bar").asBytes());
  clock.expectChanged(*file);
  ZC_EXPECT(file->readAllText() == "foobar");

  file->write(3, StringPtr("baz").asBytes());
  clock.expectChanged(*file);
  ZC_EXPECT(file->readAllText() == "foobaz");

  file->write(9, StringPtr("qux").asBytes());
  clock.expectChanged(*file);
  ZC_EXPECT(file->readAllText() == zc::StringPtr("foobaz\0\0\0qux", 12));

  file->truncate(6);
  clock.expectChanged(*file);
  ZC_EXPECT(file->readAllText() == "foobaz");

  file->truncate(18);
  clock.expectChanged(*file);
  ZC_EXPECT(file->readAllText() == zc::StringPtr("foobaz\0\0\0\0\0\0\0\0\0\0\0\0", 18));

  {
    auto mapping = file->mmap(0, 18);
    auto privateMapping = file->mmapPrivate(0, 18);
    auto writableMapping = file->mmapWritable(0, 18);
    clock.expectUnchanged(*file);

    ZC_EXPECT(mapping.size() == 18);
    ZC_EXPECT(privateMapping.size() == 18);
    ZC_EXPECT(writableMapping->get().size() == 18);
    clock.expectUnchanged(*file);

    ZC_EXPECT(writableMapping->get().begin() == mapping.begin());
    ZC_EXPECT(privateMapping.begin() != mapping.begin());

    ZC_EXPECT(zc::str(mapping.first(6).asChars()) == "foobaz");
    ZC_EXPECT(zc::str(privateMapping.first(6).asChars()) == "foobaz");
    clock.expectUnchanged(*file);

    file->write(0, StringPtr("qux").asBytes());
    clock.expectChanged(*file);
    ZC_EXPECT(zc::str(mapping.first(6).asChars()) == "quxbaz");
    ZC_EXPECT(zc::str(privateMapping.first(6).asChars()) == "foobaz");

    file->write(12, StringPtr("corge").asBytes());
    ZC_EXPECT(zc::str(mapping.slice(12, 17).asChars()) == "corge");

    // Can shrink.
    file->truncate(6);
    ZC_EXPECT(zc::str(mapping.slice(12, 17).asChars()) == zc::StringPtr("\0\0\0\0\0", 5));

    // Can regrow.
    file->truncate(18);
    ZC_EXPECT(zc::str(mapping.slice(12, 17).asChars()) == zc::StringPtr("\0\0\0\0\0", 5));

    // Can't grow past previous capacity.
    ZC_EXPECT_THROW_MESSAGE("cannot resize the file backing store", file->truncate(100));

    clock.expectChanged(*file);
    writableMapping->changed(writableMapping->get().first(3));
    clock.expectChanged(*file);
    writableMapping->sync(writableMapping->get().first(3));
    clock.expectChanged(*file);
  }

  // But now we can since the mapping is gone.
  file->truncate(100);

  file->truncate(6);
  clock.expectChanged(*file);

  ZC_EXPECT(file->readAllText() == "quxbaz");
  file->zero(3, 3);
  clock.expectChanged(*file);
  ZC_EXPECT(file->readAllText() == StringPtr("qux\0\0\0", 6));
}

ZC_TEST("InMemoryFile::copy()") {
  TestClock clock;

  auto source = newInMemoryFile(clock);
  source->writeAll("foobarbaz");

  auto dest = newInMemoryFile(clock);
  dest->writeAll("quxcorge");
  clock.expectChanged(*dest);

  ZC_EXPECT(dest->copy(3, *source, 6, zc::maxValue) == 3);
  clock.expectChanged(*dest);
  ZC_EXPECT(dest->readAllText() == "quxbazge");

  ZC_EXPECT(dest->copy(0, *source, 3, 4) == 4);
  clock.expectChanged(*dest);
  ZC_EXPECT(dest->readAllText() == "barbazge");

  ZC_EXPECT(dest->copy(0, *source, 128, zc::maxValue) == 0);
  clock.expectUnchanged(*dest);

  ZC_EXPECT(dest->copy(4, *source, 3, 0) == 0);
  clock.expectUnchanged(*dest);

  String bigString = strArray(repeat("foobar", 10000), "");
  source->truncate(bigString.size() + 1000);
  source->write(123, bigString.asBytes());

  dest->copy(321, *source, 123, bigString.size());
  ZC_EXPECT(dest->readAllText().slice(321) == bigString);
}

ZC_TEST("File::copy()") {
  TestClock clock;

  auto source = newInMemoryFile(clock);
  source->writeAll("foobarbaz");

  auto dest = newInMemoryFile(clock);
  dest->writeAll("quxcorge");
  clock.expectChanged(*dest);

  ZC_EXPECT(dest->File::copy(3, *source, 6, zc::maxValue) == 3);
  clock.expectChanged(*dest);
  ZC_EXPECT(dest->readAllText() == "quxbazge");

  ZC_EXPECT(dest->File::copy(0, *source, 3, 4) == 4);
  clock.expectChanged(*dest);
  ZC_EXPECT(dest->readAllText() == "barbazge");

  ZC_EXPECT(dest->File::copy(0, *source, 128, zc::maxValue) == 0);
  clock.expectUnchanged(*dest);

  ZC_EXPECT(dest->File::copy(4, *source, 3, 0) == 0);
  clock.expectUnchanged(*dest);

  String bigString = strArray(repeat("foobar", 10000), "");
  source->truncate(bigString.size() + 1000);
  source->write(123, bigString.asBytes());

  dest->File::copy(321, *source, 123, bigString.size());
  ZC_EXPECT(dest->readAllText().slice(321) == bigString);
}

ZC_TEST("InMemoryDirectory") {
  TestClock clock;

  auto dir = newInMemoryDirectory(clock);
  clock.expectChanged(*dir);

  ZC_EXPECT(dir->listNames() == nullptr);
  ZC_EXPECT(dir->listEntries() == nullptr);
  ZC_EXPECT(!dir->exists(Path("foo")));
  ZC_EXPECT(dir->tryOpenFile(Path("foo")) == zc::none);
  ZC_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::MODIFY) == zc::none);
  clock.expectUnchanged(*dir);

  {
    auto file = dir->openFile(Path("foo"), WriteMode::CREATE);
    ZC_EXPECT(file->getFd() == zc::none);
    clock.expectChanged(*dir);
    file->writeAll("foobar");
    clock.expectUnchanged(*dir);
  }
  clock.expectUnchanged(*dir);

  ZC_EXPECT(dir->exists(Path("foo")));
  clock.expectUnchanged(*dir);

  {
    auto stats = dir->lstat(Path("foo"));
    clock.expectUnchanged(*dir);
    ZC_EXPECT(stats.type == FsNode::Type::FILE);
    ZC_EXPECT(stats.size == 6);
  }

  {
    auto list = dir->listNames();
    clock.expectUnchanged(*dir);
    ZC_ASSERT(list.size() == 1);
    ZC_EXPECT(list[0] == "foo");
  }

  {
    auto list = dir->listEntries();
    clock.expectUnchanged(*dir);
    ZC_ASSERT(list.size() == 1);
    ZC_EXPECT(list[0].name == "foo");
    ZC_EXPECT(list[0].type == FsNode::Type::FILE);
  }

  ZC_EXPECT(dir->openFile(Path("foo"))->readAllText() == "foobar");
  clock.expectUnchanged(*dir);

  ZC_EXPECT(dir->tryOpenFile(Path({"foo", "bar"}), WriteMode::MODIFY) == zc::none);
  ZC_EXPECT(dir->tryOpenFile(Path({"bar", "baz"}), WriteMode::MODIFY) == zc::none);
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("parent is not a directory",
                                      dir->tryOpenFile(Path({"bar", "baz"}), WriteMode::CREATE));
  clock.expectUnchanged(*dir);

  {
    auto file = dir->openFile(Path({"bar", "baz"}), WriteMode::CREATE | WriteMode::CREATE_PARENT);
    clock.expectChanged(*dir);
    file->writeAll("bazqux");
    clock.expectUnchanged(*dir);
  }
  clock.expectUnchanged(*dir);

  ZC_EXPECT(dir->openFile(Path({"bar", "baz"}))->readAllText() == "bazqux");
  clock.expectUnchanged(*dir);

  {
    auto stats = dir->lstat(Path("bar"));
    clock.expectUnchanged(*dir);
    ZC_EXPECT(stats.type == FsNode::Type::DIRECTORY);
  }

  {
    auto list = dir->listNames();
    clock.expectUnchanged(*dir);
    ZC_ASSERT(list.size() == 2);
    ZC_EXPECT(list[0] == "bar");
    ZC_EXPECT(list[1] == "foo");
  }

  {
    auto list = dir->listEntries();
    clock.expectUnchanged(*dir);
    ZC_ASSERT(list.size() == 2);
    ZC_EXPECT(list[0].name == "bar");
    ZC_EXPECT(list[0].type == FsNode::Type::DIRECTORY);
    ZC_EXPECT(list[1].name == "foo");
    ZC_EXPECT(list[1].type == FsNode::Type::FILE);
  }

  {
    auto subdir = dir->openSubdir(Path("bar"));
    clock.expectUnchanged(*dir);
    clock.expectUnchanged(*subdir);

    ZC_EXPECT(subdir->openFile(Path("baz"))->readAllText() == "bazqux");
    clock.expectUnchanged(*subdir);
  }

  auto subdir = dir->openSubdir(Path("corge"), WriteMode::CREATE);
  clock.expectChanged(*dir);

  subdir->openFile(Path("grault"), WriteMode::CREATE)->writeAll("garply");
  clock.expectUnchanged(*dir);
  clock.expectChanged(*subdir);

  ZC_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "garply");

  dir->openFile(Path({"corge", "grault"}), WriteMode::CREATE | WriteMode::MODIFY)
      ->write(0, StringPtr("rag").asBytes());
  ZC_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "ragply");
  clock.expectUnchanged(*dir);

  {
    auto replacer =
        dir->replaceFile(Path({"corge", "grault"}), WriteMode::CREATE | WriteMode::MODIFY);
    clock.expectUnchanged(*subdir);
    replacer->get().writeAll("rag");
    clock.expectUnchanged(*subdir);
    // Don't commit.
  }
  clock.expectUnchanged(*subdir);
  ZC_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "ragply");

  {
    auto replacer =
        dir->replaceFile(Path({"corge", "grault"}), WriteMode::CREATE | WriteMode::MODIFY);
    clock.expectUnchanged(*subdir);
    replacer->get().writeAll("rag");
    clock.expectUnchanged(*subdir);
    replacer->commit();
    clock.expectChanged(*subdir);
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
  clock.expectUnchanged(*dir);
  dir->remove(Path("foo"));
  clock.expectChanged(*dir);
  ZC_EXPECT(!dir->exists(Path("foo")));
  ZC_EXPECT(!dir->tryRemove(Path("foo")));
  clock.expectUnchanged(*dir);

  ZC_EXPECT(dir->exists(Path({"bar", "baz"})));
  clock.expectUnchanged(*dir);
  dir->remove(Path({"bar", "baz"}));
  clock.expectUnchanged(*dir);
  ZC_EXPECT(!dir->exists(Path({"bar", "baz"})));
  ZC_EXPECT(dir->exists(Path("bar")));
  ZC_EXPECT(!dir->tryRemove(Path({"bar", "baz"})));
  clock.expectUnchanged(*dir);

  ZC_EXPECT(dir->exists(Path("corge")));
  ZC_EXPECT(dir->exists(Path({"corge", "grault"})));
  clock.expectUnchanged(*dir);
  dir->remove(Path("corge"));
  clock.expectChanged(*dir);
  ZC_EXPECT(!dir->exists(Path("corge")));
  ZC_EXPECT(!dir->exists(Path({"corge", "grault"})));
  ZC_EXPECT(!dir->tryRemove(Path("corge")));
  clock.expectUnchanged(*dir);
}

ZC_TEST("InMemoryDirectory symlinks") {
  TestClock clock;

  auto dir = newInMemoryDirectory(clock);
  clock.expectChanged(*dir);

  dir->symlink(Path("foo"), "bar/qux/../baz", WriteMode::CREATE);
  clock.expectChanged(*dir);

  ZC_EXPECT(!dir->trySymlink(Path("foo"), "bar/qux/../baz", WriteMode::CREATE));
  clock.expectUnchanged(*dir);

  {
    auto stats = dir->lstat(Path("foo"));
    clock.expectUnchanged(*dir);
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
  clock.expectChanged(*dir);

  // Link still points to non-existing file so cannot be open in most modes.
  ZC_EXPECT(dir->tryOpenFile(Path("foo")) == zc::none);
  ZC_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::CREATE) == zc::none);
  ZC_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::MODIFY) == zc::none);
  clock.expectUnchanged(*dir);

  // But... CREATE | MODIFY works.
  dir->openFile(Path("foo"), WriteMode::CREATE | WriteMode::MODIFY)->writeAll("foobar");
  clock.expectUnchanged(*dir);  // Change is only to subdir!

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

ZC_TEST("InMemoryDirectory link") {
  TestClock clock;

  auto src = newInMemoryDirectory(clock);
  auto dst = newInMemoryDirectory(clock);

  src->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("foobar");
  src->openFile(Path({"foo", "baz", "qux"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("bazqux");
  clock.expectChanged(*src);
  clock.expectUnchanged(*dst);

  dst->transfer(Path("link"), WriteMode::CREATE, *src, Path("foo"), TransferMode::LINK);
  clock.expectUnchanged(*src);
  clock.expectChanged(*dst);

  ZC_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
  ZC_EXPECT(dst->openFile(Path({"link", "baz", "qux"}))->readAllText() == "bazqux");

  ZC_EXPECT(dst->exists(Path({"link", "bar"})));
  src->remove(Path({"foo", "bar"}));
  ZC_EXPECT(!dst->exists(Path({"link", "bar"})));
}

ZC_TEST("InMemoryDirectory copy") {
  TestClock clock;

  auto src = newInMemoryDirectory(clock);
  auto dst = newInMemoryDirectory(clock);

  src->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("foobar");
  src->openFile(Path({"foo", "baz", "qux"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("bazqux");
  clock.expectChanged(*src);
  clock.expectUnchanged(*dst);

  dst->transfer(Path("link"), WriteMode::CREATE, *src, Path("foo"), TransferMode::COPY);
  clock.expectUnchanged(*src);
  clock.expectChanged(*dst);

  ZC_EXPECT(src->openFile(Path({"foo", "bar"}))->readAllText() == "foobar");
  ZC_EXPECT(src->openFile(Path({"foo", "baz", "qux"}))->readAllText() == "bazqux");
  ZC_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
  ZC_EXPECT(dst->openFile(Path({"link", "baz", "qux"}))->readAllText() == "bazqux");

  ZC_EXPECT(dst->exists(Path({"link", "bar"})));
  src->remove(Path({"foo", "bar"}));
  ZC_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
}

ZC_TEST("InMemoryDirectory move") {
  TestClock clock;

  auto src = newInMemoryDirectory(clock);
  auto dst = newInMemoryDirectory(clock);

  src->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("foobar");
  src->openFile(Path({"foo", "baz", "qux"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
      ->writeAll("bazqux");
  clock.expectChanged(*src);
  clock.expectUnchanged(*dst);

  dst->transfer(Path("link"), WriteMode::CREATE, *src, Path("foo"), TransferMode::MOVE);
  clock.expectChanged(*src);

  ZC_EXPECT(!src->exists(Path({"foo"})));
  ZC_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
  ZC_EXPECT(dst->openFile(Path({"link", "baz", "qux"}))->readAllText() == "bazqux");
}

ZC_TEST("InMemoryDirectory transfer from self") {
  TestClock clock;

  auto dir = newInMemoryDirectory(clock);

  auto file = dir->openFile(Path({"foo"}), WriteMode::CREATE);

  dir->transfer(Path({"bar"}), WriteMode::CREATE, Path({"foo"}), TransferMode::MOVE);

  auto list = dir->listNames();
  ZC_EXPECT(list.size() == 1);
  ZC_EXPECT(list[0] == "bar");

  auto file2 = dir->openFile(Path({"bar"}));
  ZC_EXPECT(file.get() == file2.get());
}

ZC_TEST("InMemoryDirectory createTemporary") {
  TestClock clock;

  auto dir = newInMemoryDirectory(clock);
  auto file = dir->createTemporary();
  file->writeAll("foobar");
  ZC_EXPECT(file->readAllText() == "foobar");
  ZC_EXPECT(dir->listNames() == nullptr);
  ZC_EXPECT(file->getFd() == zc::none);
}

#if __linux__

ZC_TEST("InMemoryDirectory backed my memfd") {
  // Test memfd-backed in-memory directory. We're not going to test all functionality here, since
  // we assume filesystem-disk-test covers fd-backed files in depth.

  TestClock clock;
  auto dir = newInMemoryDirectory(clock, memfdInMemoryFileFactory());
  auto file = dir->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT);

  // Write directly to the FD, verify it is reflected in the file object.
  int fd = ZC_ASSERT_NONNULL(file->getFd());
  ssize_t n;
  ZC_SYSCALL(n = write(fd, "foo", 3));
  ZC_EXPECT(n == 3);

  ZC_EXPECT(file->readAllText() == "foo"_zc);

  // Re-opening the same file produces an alias of the same memfd.
  auto file2 = dir->openFile(Path({"foo", "bar"}));
  ZC_EXPECT(file2->readAllText() == "foo"_zc);
  file->writeAll("bar"_zc);
  ZC_EXPECT(file2->readAllText() == "bar"_zc);
  ZC_EXPECT(file2->getFd() != zc::none);
  ZC_EXPECT(file->stat().hashCode == file2->stat().hashCode);

  ZC_EXPECT(dir->createTemporary()->getFd() != zc::none);
}

#endif  // __linux__

}  // namespace
}  // namespace zc
