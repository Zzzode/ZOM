// Copyright (c) 2017 Cloudflare, Inc. and contributors
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

#include "zc/http/url.h"

#include <zc/core/debug.h>
#include <zc/ztest/test.h>

namespace zc {
namespace {

Url parseAndCheck(zc::StringPtr originalText, zc::StringPtr expectedRestringified = nullptr,
                  Url::Options options = {}) {
  if (expectedRestringified == nullptr) expectedRestringified = originalText;
  auto url = Url::parse(originalText, Url::REMOTE_HREF, options);
  ZC_EXPECT(zc::str(url) == expectedRestringified, url, originalText, expectedRestringified);
  // Make sure clones also restringify to the expected string.
  auto clone = url.clone();
  ZC_EXPECT(zc::str(clone) == expectedRestringified, clone, originalText, expectedRestringified);
  return url;
}

static constexpr Url::Options NO_DECODE{
    false,  // percentDecode
    false,  // allowEmpty
};

static constexpr Url::Options ALLOW_EMPTY{
    true,  // percentDecode
    true,  // allowEmpty
};

ZC_TEST("parse / stringify URL") {
  {
    auto url = parseAndCheck("https://capnproto.org");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path == nullptr);
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_EXPECT(url.query == nullptr);
    ZC_EXPECT(url.fragment == zc::none);
  }

  {
    auto url = parseAndCheck("https://capnproto.org:80");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "capnproto.org:80");
    ZC_EXPECT(url.path == nullptr);
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_EXPECT(url.query == nullptr);
    ZC_EXPECT(url.fragment == zc::none);
  }

  {
    auto url = parseAndCheck("https://capnproto.org/");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path == nullptr);
    ZC_EXPECT(url.hasTrailingSlash);
    ZC_EXPECT(url.query == nullptr);
    ZC_EXPECT(url.fragment == zc::none);
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"foo", "bar"}));
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_EXPECT(url.query == nullptr);
    ZC_EXPECT(url.fragment == zc::none);
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar/");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"foo", "bar"}));
    ZC_EXPECT(url.hasTrailingSlash);
    ZC_EXPECT(url.query == nullptr);
    ZC_EXPECT(url.fragment == zc::none);
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar?baz=qux&corge#garply");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"foo", "bar"}));
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_ASSERT(url.query.size() == 2);
    ZC_EXPECT(url.query[0].name == "baz");
    ZC_EXPECT(url.query[0].value == "qux");
    ZC_EXPECT(url.query[1].name == "corge");
    ZC_EXPECT(url.query[1].value == nullptr);
    ZC_EXPECT(ZC_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar?baz=qux&corge=#garply");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"foo", "bar"}));
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_ASSERT(url.query.size() == 2);
    ZC_EXPECT(url.query[0].name == "baz");
    ZC_EXPECT(url.query[0].value == "qux");
    ZC_EXPECT(url.query[1].name == "corge");
    ZC_EXPECT(url.query[1].value == nullptr);
    ZC_EXPECT(ZC_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar?baz=&corge=grault#garply");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"foo", "bar"}));
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_ASSERT(url.query.size() == 2);
    ZC_EXPECT(url.query[0].name == "baz");
    ZC_EXPECT(url.query[0].value == "");
    ZC_EXPECT(url.query[1].name == "corge");
    ZC_EXPECT(url.query[1].value == "grault");
    ZC_EXPECT(ZC_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar/?baz=qux&corge=grault#garply");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"foo", "bar"}));
    ZC_EXPECT(url.hasTrailingSlash);
    ZC_ASSERT(url.query.size() == 2);
    ZC_EXPECT(url.query[0].name == "baz");
    ZC_EXPECT(url.query[0].value == "qux");
    ZC_EXPECT(url.query[1].name == "corge");
    ZC_EXPECT(url.query[1].value == "grault");
    ZC_EXPECT(ZC_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar?baz=qux#garply");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"foo", "bar"}));
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_ASSERT(url.query.size() == 1);
    ZC_EXPECT(url.query[0].name == "baz");
    ZC_EXPECT(url.query[0].value == "qux");
    ZC_EXPECT(ZC_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo?bar%20baz=qux+quux",
                             "https://capnproto.org/foo?bar+baz=qux+quux");
    ZC_ASSERT(url.query.size() == 1);
    ZC_EXPECT(url.query[0].name == "bar baz");
    ZC_EXPECT(url.query[0].value == "qux quux");
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar#garply");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"foo", "bar"}));
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_EXPECT(url.query == nullptr);
    ZC_EXPECT(ZC_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar/#garply");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"foo", "bar"}));
    ZC_EXPECT(url.hasTrailingSlash);
    ZC_EXPECT(url.query == nullptr);
    ZC_EXPECT(ZC_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://capnproto.org#garply");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path == nullptr);
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_EXPECT(url.query == nullptr);
    ZC_EXPECT(ZC_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://capnproto.org/#garply");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path == nullptr);
    ZC_EXPECT(url.hasTrailingSlash);
    ZC_EXPECT(url.query == nullptr);
    ZC_EXPECT(ZC_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://foo@capnproto.org");
    ZC_EXPECT(url.scheme == "https");
    auto& user = ZC_ASSERT_NONNULL(url.userInfo);
    ZC_EXPECT(user.username == "foo");
    ZC_EXPECT(user.password == zc::none);
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path == nullptr);
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_EXPECT(url.query == nullptr);
    ZC_EXPECT(url.fragment == zc::none);
  }

  {
    auto url = parseAndCheck("https://$foo&:12+,34@capnproto.org");
    ZC_EXPECT(url.scheme == "https");
    auto& user = ZC_ASSERT_NONNULL(url.userInfo);
    ZC_EXPECT(user.username == "$foo&");
    ZC_EXPECT(ZC_ASSERT_NONNULL(user.password) == "12+,34");
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path == nullptr);
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_EXPECT(url.query == nullptr);
    ZC_EXPECT(url.fragment == zc::none);
  }

  {
    auto url = parseAndCheck("https://[2001:db8::1234]:80/foo");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.userInfo == zc::none);
    ZC_EXPECT(url.host == "[2001:db8::1234]:80");
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"foo"}));
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_EXPECT(url.query == nullptr);
    ZC_EXPECT(url.fragment == zc::none);
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo%2Fbar/baz");
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"foo/bar", "baz"}));
  }

  parseAndCheck("https://capnproto.org/foo/bar?", "https://capnproto.org/foo/bar");
  parseAndCheck("https://capnproto.org/foo/bar?#", "https://capnproto.org/foo/bar#");
  parseAndCheck("https://capnproto.org/foo/bar#");

  // Scheme and host are forced to lower-case.
  parseAndCheck("hTtP://capNprotO.org/fOo/bAr", "http://capnproto.org/fOo/bAr");

  // URLs with underscores in their hostnames are allowed, but you probably shouldn't use them. They
  // are not valid domain names.
  parseAndCheck("https://bad_domain.capnproto.org/");

  // Make sure URLs with %-encoded '%' signs in their userinfo, path, query, and fragment components
  // get correctly re-encoded.
  parseAndCheck("https://foo%25bar:baz%25qux@capnproto.org/");
  parseAndCheck("https://capnproto.org/foo%25bar");
  parseAndCheck("https://capnproto.org/?foo%25bar=baz%25qux");
  parseAndCheck("https://capnproto.org/#foo%25bar");

  // Make sure redundant /'s and &'s aren't collapsed when options.removeEmpty is false.
  parseAndCheck("https://capnproto.org/foo//bar///test//?foo=bar&&baz=qux&", nullptr, ALLOW_EMPTY);

  // "." and ".." are still processed, though.
  parseAndCheck("https://capnproto.org/foo//../bar/.", "https://capnproto.org/foo/bar/",
                ALLOW_EMPTY);

  {
    auto url = parseAndCheck("https://foo/", nullptr, ALLOW_EMPTY);
    ZC_EXPECT(url.path.size() == 0);
    ZC_EXPECT(url.hasTrailingSlash);
  }

  {
    auto url = parseAndCheck("https://foo/bar/", nullptr, ALLOW_EMPTY);
    ZC_EXPECT(url.path.size() == 1);
    ZC_EXPECT(url.hasTrailingSlash);
  }
}

ZC_TEST("URL percent encoding") {
  parseAndCheck("https://b%6fb:%61bcd@capnpr%6fto.org/f%6fo?b%61r=b%61z#q%75x",
                "https://bob:abcd@capnproto.org/foo?bar=baz#qux");

  parseAndCheck("https://b\001b:\001bcd@capnproto.org/f\001o?b\001r=b\001z#q\001x",
                "https://b%01b:%01bcd@capnproto.org/f%01o?b%01r=b%01z#q%01x");

  parseAndCheck("https://b b: bcd@capnproto.org/f o?b r=b z#q x",
                "https://b%20b:%20bcd@capnproto.org/f%20o?b+r=b+z#q%20x");

  parseAndCheck("https://capnproto.org/foo?bar=baz#@?#^[\\]{|}",
                "https://capnproto.org/foo?bar=baz#@?#^[\\]{|}");

  // All permissible non-alphanumeric, non-separator path characters.
  parseAndCheck("https://capnproto.org/!$&'()*+,-.:;=@[]^_|~",
                "https://capnproto.org/!$&'()*+,-.:;=@[]^_|~");
}

ZC_TEST("parse / stringify URL w/o decoding") {
  {
    auto url = parseAndCheck("https://capnproto.org/foo%2Fbar/baz", nullptr, NO_DECODE);
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"foo%2Fbar", "baz"}));
  }

  {
    // This case would throw an exception without NO_DECODE.
    Url url = parseAndCheck("https://capnproto.org/R%20%26%20S?%foo=%QQ", nullptr, NO_DECODE);
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"R%20%26%20S"}));
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_ASSERT(url.query.size() == 1);
    ZC_EXPECT(url.query[0].name == "%foo");
    ZC_EXPECT(url.query[0].value == "%QQ");
  }
}

ZC_TEST("URL relative paths") {
  parseAndCheck("https://capnproto.org/foo//bar", "https://capnproto.org/foo/bar");

  parseAndCheck("https://capnproto.org/foo/./bar", "https://capnproto.org/foo/bar");

  parseAndCheck("https://capnproto.org/foo/bar//", "https://capnproto.org/foo/bar/");

  parseAndCheck("https://capnproto.org/foo/bar/.", "https://capnproto.org/foo/bar/");

  parseAndCheck("https://capnproto.org/foo/baz/../bar", "https://capnproto.org/foo/bar");

  parseAndCheck("https://capnproto.org/foo/bar/baz/..", "https://capnproto.org/foo/bar/");

  parseAndCheck("https://capnproto.org/..", "https://capnproto.org/");

  parseAndCheck("https://capnproto.org/foo/../..", "https://capnproto.org/");
}

ZC_TEST("URL for HTTP request") {
  {
    Url url = Url::parse("https://bob:1234@capnproto.org/foo/bar?baz=qux#corge");
    ZC_EXPECT(url.toString(Url::REMOTE_HREF) ==
              "https://bob:1234@capnproto.org/foo/bar?baz=qux#corge");
    ZC_EXPECT(url.toString(Url::HTTP_PROXY_REQUEST) == "https://capnproto.org/foo/bar?baz=qux");
    ZC_EXPECT(url.toString(Url::HTTP_REQUEST) == "/foo/bar?baz=qux");
  }

  {
    Url url = Url::parse("https://capnproto.org");
    ZC_EXPECT(url.toString(Url::REMOTE_HREF) == "https://capnproto.org");
    ZC_EXPECT(url.toString(Url::HTTP_PROXY_REQUEST) == "https://capnproto.org");
    ZC_EXPECT(url.toString(Url::HTTP_REQUEST) == "/");
  }

  {
    Url url = Url::parse("/foo/bar?baz=qux&corge", Url::HTTP_REQUEST);
    ZC_EXPECT(url.scheme == nullptr);
    ZC_EXPECT(url.host == nullptr);
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"foo", "bar"}));
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_ASSERT(url.query.size() == 2);
    ZC_EXPECT(url.query[0].name == "baz");
    ZC_EXPECT(url.query[0].value == "qux");
    ZC_EXPECT(url.query[1].name == "corge");
    ZC_EXPECT(url.query[1].value == nullptr);
  }

  {
    Url url = Url::parse("https://capnproto.org/foo/bar?baz=qux&corge", Url::HTTP_PROXY_REQUEST);
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>({"foo", "bar"}));
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_ASSERT(url.query.size() == 2);
    ZC_EXPECT(url.query[0].name == "baz");
    ZC_EXPECT(url.query[0].value == "qux");
    ZC_EXPECT(url.query[1].name == "corge");
    ZC_EXPECT(url.query[1].value == nullptr);
  }

  {
    // '#' is allowed in path components in the HTTP_REQUEST context.
    Url url = Url::parse("/foo#bar", Url::HTTP_REQUEST);
    ZC_EXPECT(url.toString(Url::HTTP_REQUEST) == "/foo%23bar");
    ZC_EXPECT(url.scheme == nullptr);
    ZC_EXPECT(url.host == nullptr);
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>{"foo#bar"});
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_EXPECT(url.query == nullptr);
    ZC_EXPECT(url.fragment == zc::none);
  }

  {
    // '#' is allowed in path components in the HTTP_PROXY_REQUEST context.
    Url url = Url::parse("https://capnproto.org/foo#bar", Url::HTTP_PROXY_REQUEST);
    ZC_EXPECT(url.toString(Url::HTTP_PROXY_REQUEST) == "https://capnproto.org/foo%23bar");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path.asPtr() == zc::ArrayPtr<const StringPtr>{"foo#bar"});
    ZC_EXPECT(!url.hasTrailingSlash);
    ZC_EXPECT(url.query == nullptr);
    ZC_EXPECT(url.fragment == zc::none);
  }

  {
    // '#' is allowed in query components in the HTTP_REQUEST context.
    Url url = Url::parse("/?foo=bar#123", Url::HTTP_REQUEST);
    ZC_EXPECT(url.toString(Url::HTTP_REQUEST) == "/?foo=bar%23123");
    ZC_EXPECT(url.scheme == nullptr);
    ZC_EXPECT(url.host == nullptr);
    ZC_EXPECT(url.path == nullptr);
    ZC_EXPECT(url.hasTrailingSlash);
    ZC_ASSERT(url.query.size() == 1);
    ZC_EXPECT(url.query[0].name == "foo");
    ZC_EXPECT(url.query[0].value == "bar#123");
    ZC_EXPECT(url.fragment == zc::none);
  }

  {
    // '#' is allowed in query components in the HTTP_PROXY_REQUEST context.
    Url url = Url::parse("https://capnproto.org/?foo=bar#123", Url::HTTP_PROXY_REQUEST);
    ZC_EXPECT(url.toString(Url::HTTP_PROXY_REQUEST) == "https://capnproto.org/?foo=bar%23123");
    ZC_EXPECT(url.scheme == "https");
    ZC_EXPECT(url.host == "capnproto.org");
    ZC_EXPECT(url.path == nullptr);
    ZC_EXPECT(url.hasTrailingSlash);
    ZC_ASSERT(url.query.size() == 1);
    ZC_EXPECT(url.query[0].name == "foo");
    ZC_EXPECT(url.query[0].value == "bar#123");
    ZC_EXPECT(url.fragment == zc::none);
  }
}

ZC_TEST("parse URL failure") {
  ZC_EXPECT(Url::tryParse("ht/tps://capnproto.org") == zc::none);
  ZC_EXPECT(Url::tryParse("capnproto.org") == zc::none);
  ZC_EXPECT(Url::tryParse("https:foo") == zc::none);

  // percent-decode errors
  ZC_EXPECT(Url::tryParse("https://capnproto.org/f%nno") == zc::none);
  ZC_EXPECT(Url::tryParse("https://capnproto.org/foo?b%nnr=baz") == zc::none);

  // components not valid in context
  ZC_EXPECT(Url::tryParse("https://capnproto.org/foo", Url::HTTP_REQUEST) == zc::none);
  ZC_EXPECT(Url::tryParse("https://bob:123@capnproto.org/foo", Url::HTTP_PROXY_REQUEST) ==
            zc::none);
  ZC_EXPECT(Url::tryParse("https://capnproto.org#/foo", Url::HTTP_PROXY_REQUEST) == zc::none);
}

void parseAndCheckRelative(zc::StringPtr base, zc::StringPtr relative, zc::StringPtr expected,
                           Url::Options options = {}) {
  auto parsed = Url::parse(base, Url::REMOTE_HREF, options).parseRelative(relative);
  ZC_EXPECT(zc::str(parsed) == expected, parsed, expected);
  auto clone = parsed.clone();
  ZC_EXPECT(zc::str(clone) == expected, clone, expected);
}

ZC_TEST("parse relative URL") {
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge", "#grault",
                        "https://capnproto.org/foo/bar?baz=qux#grault");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz#corge", "#grault",
                        "https://capnproto.org/foo/bar?baz#grault");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=#corge", "#grault",
                        "https://capnproto.org/foo/bar?baz=#grault");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge", "?grault",
                        "https://capnproto.org/foo/bar?grault");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge",
                        "?grault=", "https://capnproto.org/foo/bar?grault=");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge", "?grault+garply=waldo",
                        "https://capnproto.org/foo/bar?grault+garply=waldo");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge", "grault",
                        "https://capnproto.org/foo/grault");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge", "/grault",
                        "https://capnproto.org/grault");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge", "//grault",
                        "https://grault");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge", "//grault/garply",
                        "https://grault/garply");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge", "http:/grault",
                        "http://capnproto.org/grault");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge", "/http:/grault",
                        "https://capnproto.org/http:/grault");
  parseAndCheckRelative("https://capnproto.org/", "/foo/../bar", "https://capnproto.org/bar");
}

ZC_TEST("parse relative URL w/o decoding") {
  // This case would throw an exception without NO_DECODE.
  parseAndCheckRelative("https://capnproto.org/R%20%26%20S?%foo=%QQ", "%ANOTH%ERBAD%URL",
                        "https://capnproto.org/%ANOTH%ERBAD%URL", NO_DECODE);
}

ZC_TEST("parse relative URL failure") {
  auto base = Url::parse("https://example.com/");
  ZC_EXPECT(base.tryParseRelative("https://[not a host]") == zc::none);
}

}  // namespace
}  // namespace zc
