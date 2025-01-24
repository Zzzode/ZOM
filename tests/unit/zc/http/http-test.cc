// Copyright (c) 2017 Sandstorm Development Group, Inc. and contributors
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

#include "zc/core/common.h"
#define ZC_TESTING_ZC 1

#include <zc/core/debug.h>
#include <zc/core/encoding.h>
#include <zc/core/vector.h>
#include <zc/ztest/test.h>

#include <map>

#include "zc/http/http.h"

#if ZC_HTTP_TEST_USE_OS_PIPE
// Run the test using OS-level socketpairs. (See http-socketpair-test.c++.)
#define ZC_HTTP_TEST_SETUP_IO   \
  auto io = zc::setupAsyncIo(); \
  auto& waitScope ZC_UNUSED = io.waitScope
#define ZC_HTTP_TEST_SETUP_LOOPBACK_LISTENER_AND_ADDR                                   \
  auto listener =                                                                       \
      io.provider->getNetwork().parseAddress("localhost", 0).wait(waitScope)->listen(); \
  auto addr =                                                                           \
      io.provider->getNetwork().parseAddress("localhost", listener->getPort()).wait(waitScope)
#define ZC_HTTP_TEST_CREATE_2PIPE io.provider->newTwoWayPipe()
#else
// Run the test using in-process two-way pipes.
#define ZC_HTTP_TEST_SETUP_IO   \
  auto io = zc::setupAsyncIo(); \
  auto& waitScope ZC_UNUSED = io.waitScope
#define ZC_HTTP_TEST_SETUP_LOOPBACK_LISTENER_AND_ADDR                                 \
  auto capPipe = newCapabilityPipe();                                                 \
  auto listener = zc::heap<zc::CapabilityStreamConnectionReceiver>(*capPipe.ends[0]); \
  auto addr = zc::heap<zc::CapabilityStreamNetworkAddress>(zc::none, *capPipe.ends[1])
#define ZC_HTTP_TEST_CREATE_2PIPE zc::newTwoWayPipe()
#endif

namespace zc {
namespace {

ZC_TEST("HttpMethod parse / stringify") {
#define TRY(name)                                                                  \
  ZC_EXPECT(zc::str(HttpMethod::name) == #name);                                   \
  ZC_IF_SOME(parsed, tryParseHttpMethodAllowingConnect(#name)) {                   \
    ZC_SWITCH_ONEOF(parsed) {                                                      \
      ZC_CASE_ONEOF(method, HttpMethod) { ZC_EXPECT(method == HttpMethod::name); } \
      ZC_CASE_ONEOF(method, HttpConnectMethod) {                                   \
        ZC_FAIL_EXPECT("http method parsed as CONNECT", #name);                    \
      }                                                                            \
    }                                                                              \
  }                                                                                \
  else { ZC_FAIL_EXPECT("couldn't parse \"" #name "\" as HttpMethod"); }

  ZC_HTTP_FOR_EACH_METHOD(TRY)
#undef TRY

  // Make sure attempting to stringify an invalid value doesn't segfault
  ZC_EXPECT_THROW(FAILED, zc::str(HttpMethod{101}));

  ZC_EXPECT(tryParseHttpMethod("FOO") == zc::none);
  ZC_EXPECT(tryParseHttpMethod("") == zc::none);
  ZC_EXPECT(tryParseHttpMethod("G") == zc::none);
  ZC_EXPECT(tryParseHttpMethod("GE") == zc::none);
  ZC_EXPECT(tryParseHttpMethod("GET ") == zc::none);
  ZC_EXPECT(tryParseHttpMethod("get") == zc::none);

  ZC_EXPECT(
      ZC_ASSERT_NONNULL(tryParseHttpMethodAllowingConnect("CONNECT")).is<HttpConnectMethod>());
  ZC_EXPECT(tryParseHttpMethod("connect") == zc::none);
}

ZC_TEST("HttpHeaderTable") {
  HttpHeaderTable::Builder builder;

  auto host = builder.add("Host");
  auto host2 = builder.add("hOsT");
  auto fooBar = builder.add("Foo-Bar");
  auto bazQux = builder.add("baz-qux");
  auto bazQux2 = builder.add("Baz-Qux");

  auto table = builder.build();

  uint builtinHeaderCount = 0;
#define INCREMENT(id, name) ++builtinHeaderCount;
  ZC_HTTP_FOR_EACH_BUILTIN_HEADER(INCREMENT)
#undef INCREMENT

  ZC_EXPECT(table->idCount() == builtinHeaderCount + 2);

  ZC_EXPECT(host == HttpHeaderId::HOST);
  ZC_EXPECT(host != HttpHeaderId::DATE);
  ZC_EXPECT(host2 == host);

  ZC_EXPECT(host != fooBar);
  ZC_EXPECT(host != bazQux);
  ZC_EXPECT(fooBar != bazQux);
  ZC_EXPECT(bazQux == bazQux2);

  ZC_EXPECT(zc::str(host) == "Host");
  ZC_EXPECT(zc::str(host2) == "Host");
  ZC_EXPECT(zc::str(fooBar) == "Foo-Bar");
  ZC_EXPECT(zc::str(bazQux) == "baz-qux");
  ZC_EXPECT(zc::str(HttpHeaderId::HOST) == "Host");

  ZC_EXPECT(table->idToString(HttpHeaderId::DATE) == "Date");
  ZC_EXPECT(table->idToString(fooBar) == "Foo-Bar");

  ZC_EXPECT(ZC_ASSERT_NONNULL(table->stringToId("Date")) == HttpHeaderId::DATE);
  ZC_EXPECT(ZC_ASSERT_NONNULL(table->stringToId("dATE")) == HttpHeaderId::DATE);
  ZC_EXPECT(ZC_ASSERT_NONNULL(table->stringToId("Foo-Bar")) == fooBar);
  ZC_EXPECT(ZC_ASSERT_NONNULL(table->stringToId("foo-BAR")) == fooBar);
  ZC_EXPECT(table->stringToId("foobar") == zc::none);
  ZC_EXPECT(table->stringToId("barfoo") == zc::none);
}

ZC_TEST("HttpHeaders::parseRequest") {
  HttpHeaderTable::Builder builder;

  auto fooBar = builder.add("Foo-Bar");
  auto bazQux = builder.add("baz-qux");

  auto table = builder.build();

  HttpHeaders headers(*table);
  auto text = zc::heapString(
      "POST   /some/path \t   HTTP/1.1\r\n"
      "Foo-BaR: Baz\r\n"
      "Host: example.com\r\n"
      "Content-Length: 123\r\n"
      "DATE:     early\r\n"
      "other-Header: yep\r\n"
      "with.dots: sure\r\n"
      "\r\n");
  auto result = headers.tryParseRequest(text.asArray()).get<HttpHeaders::Request>();

  ZC_EXPECT(result.method == HttpMethod::POST);
  ZC_EXPECT(result.url == "/some/path");
  ZC_EXPECT(ZC_ASSERT_NONNULL(headers.get(HttpHeaderId::HOST)) == "example.com");
  ZC_EXPECT(ZC_ASSERT_NONNULL(headers.get(HttpHeaderId::DATE)) == "early");
  ZC_EXPECT(ZC_ASSERT_NONNULL(headers.get(fooBar)) == "Baz");
  ZC_EXPECT(headers.get(bazQux) == zc::none);
  ZC_EXPECT(headers.get(HttpHeaderId::CONTENT_TYPE) == zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(headers.get(HttpHeaderId::CONTENT_LENGTH)) == "123");
  ZC_EXPECT(headers.get(HttpHeaderId::TRANSFER_ENCODING) == zc::none);

  std::map<zc::StringPtr, zc::StringPtr> unpackedHeaders;
  headers.forEach([&](zc::StringPtr name, zc::StringPtr value) {
    ZC_EXPECT(unpackedHeaders.insert(std::make_pair(name, value)).second);
  });
  ZC_EXPECT(unpackedHeaders.size() == 6);
  ZC_EXPECT(unpackedHeaders["Content-Length"] == "123");
  ZC_EXPECT(unpackedHeaders["Host"] == "example.com");
  ZC_EXPECT(unpackedHeaders["Date"] == "early");
  ZC_EXPECT(unpackedHeaders["Foo-Bar"] == "Baz");
  ZC_EXPECT(unpackedHeaders["other-Header"] == "yep");
  ZC_EXPECT(unpackedHeaders["with.dots"] == "sure");

  ZC_EXPECT(headers.serializeRequest(result.method, result.url) ==
            "POST /some/path HTTP/1.1\r\n"
            "Content-Length: 123\r\n"
            "Host: example.com\r\n"
            "Date: early\r\n"
            "Foo-Bar: Baz\r\n"
            "other-Header: yep\r\n"
            "with.dots: sure\r\n"
            "\r\n");
}

ZC_TEST("HttpHeaders::parseResponse") {
  HttpHeaderTable::Builder builder;

  auto fooBar = builder.add("Foo-Bar");
  auto bazQux = builder.add("baz-qux");

  auto table = builder.build();

  HttpHeaders headers(*table);
  auto text = zc::heapString(
      "HTTP/1.1\t\t  418\t    I'm a teapot\r\n"
      "Foo-BaR: Baz\r\n"
      "Host: example.com\r\n"
      "Content-Length: 123\r\n"
      "DATE:     early\r\n"
      "other-Header: yep\r\n"
      "\r\n");
  auto result = headers.tryParseResponse(text.asArray()).get<HttpHeaders::Response>();

  ZC_EXPECT(result.statusCode == 418);
  ZC_EXPECT(result.statusText == "I'm a teapot");
  ZC_EXPECT(ZC_ASSERT_NONNULL(headers.get(HttpHeaderId::HOST)) == "example.com");
  ZC_EXPECT(ZC_ASSERT_NONNULL(headers.get(HttpHeaderId::DATE)) == "early");
  ZC_EXPECT(ZC_ASSERT_NONNULL(headers.get(fooBar)) == "Baz");
  ZC_EXPECT(headers.get(bazQux) == zc::none);
  ZC_EXPECT(headers.get(HttpHeaderId::CONTENT_TYPE) == zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(headers.get(HttpHeaderId::CONTENT_LENGTH)) == "123");
  ZC_EXPECT(headers.get(HttpHeaderId::TRANSFER_ENCODING) == zc::none);

  std::map<zc::StringPtr, zc::StringPtr> unpackedHeaders;
  headers.forEach([&](zc::StringPtr name, zc::StringPtr value) {
    ZC_EXPECT(unpackedHeaders.insert(std::make_pair(name, value)).second);
  });
  ZC_EXPECT(unpackedHeaders.size() == 5);
  ZC_EXPECT(unpackedHeaders["Content-Length"] == "123");
  ZC_EXPECT(unpackedHeaders["Host"] == "example.com");
  ZC_EXPECT(unpackedHeaders["Date"] == "early");
  ZC_EXPECT(unpackedHeaders["Foo-Bar"] == "Baz");
  ZC_EXPECT(unpackedHeaders["other-Header"] == "yep");

  ZC_EXPECT(headers.serializeResponse(result.statusCode, result.statusText) ==
            "HTTP/1.1 418 I'm a teapot\r\n"
            "Content-Length: 123\r\n"
            "Host: example.com\r\n"
            "Date: early\r\n"
            "Foo-Bar: Baz\r\n"
            "other-Header: yep\r\n"
            "\r\n");
}

ZC_TEST("HttpHeaders parse invalid") {
  auto table = HttpHeaderTable::Builder().build();
  HttpHeaders headers(*table);

  // NUL byte in request.
  {
    auto input = zc::heapString(
        "POST  \0 /some/path \t   HTTP/1.1\r\n"
        "Foo-BaR: Baz\r\n"
        "Host: example.com\r\n"
        "DATE:     early\r\n"
        "other-Header: yep\r\n"
        "\r\n");

    auto protocolError = headers.tryParseRequest(input).get<HttpHeaders::ProtocolError>();

    ZC_EXPECT(protocolError.description == "Request headers have no terminal newline.",
              protocolError.description);
    ZC_EXPECT(protocolError.rawContent.asChars() == input);
  }

  // Control character in header name.
  {
    auto input = zc::heapString(
        "POST   /some/path \t   HTTP/1.1\r\n"
        "Foo-BaR: Baz\r\n"
        "Cont\001ent-Length: 123\r\n"
        "DATE:     early\r\n"
        "other-Header: yep\r\n"
        "\r\n");

    auto protocolError = headers.tryParseRequest(input).get<HttpHeaders::ProtocolError>();

    ZC_EXPECT(protocolError.description == "The headers sent by your client are not valid.",
              protocolError.description);
    ZC_EXPECT(protocolError.rawContent.asChars() == input);
  }

  // Separator character in header name.
  {
    auto input = zc::heapString(
        "POST   /some/path \t   HTTP/1.1\r\n"
        "Foo-BaR: Baz\r\n"
        "Host: example.com\r\n"
        "DATE/:     early\r\n"
        "other-Header: yep\r\n"
        "\r\n");

    auto protocolError = headers.tryParseRequest(input).get<HttpHeaders::ProtocolError>();

    ZC_EXPECT(protocolError.description == "The headers sent by your client are not valid.",
              protocolError.description);
    ZC_EXPECT(protocolError.rawContent.asChars() == input);
  }

  // Response status code not numeric.
  {
    auto input = zc::heapString(
        "HTTP/1.1\t\t  abc\t    I'm a teapot\r\n"
        "Foo-BaR: Baz\r\n"
        "Host: example.com\r\n"
        "DATE:     early\r\n"
        "other-Header: yep\r\n"
        "\r\n");

    auto protocolError = headers.tryParseRequest(input).get<HttpHeaders::ProtocolError>();

    ZC_EXPECT(protocolError.description == "Unrecognized request method.",
              protocolError.description);
    ZC_EXPECT(protocolError.rawContent.asChars() == input);
  }
}

ZC_TEST("HttpHeaders require valid HttpHeaderTable") {
  const auto ERROR_MESSAGE =
      "HttpHeaders object was constructed from HttpHeaderTable "
      "that wasn't fully built yet at the time of construction"_zc;

  {
    // A tabula rasa is valid.
    HttpHeaderTable table;
    ZC_REQUIRE(table.isReady());

    HttpHeaders headers(table);
  }

  {
    // A future table is not valid.
    HttpHeaderTable::Builder builder;

    auto& futureTable = builder.getFutureTable();
    ZC_REQUIRE(!futureTable.isReady());

    auto makeHeadersThenBuild = [&]() {
      HttpHeaders headers(futureTable);
      auto table = builder.build();
    };
    ZC_EXPECT_THROW_MESSAGE(ERROR_MESSAGE, makeHeadersThenBuild());
  }

  {
    // A well built table is valid.
    HttpHeaderTable::Builder builder;

    auto& futureTable = builder.getFutureTable();
    ZC_REQUIRE(!futureTable.isReady());

    auto ownedTable = builder.build();
    ZC_REQUIRE(futureTable.isReady());
    ZC_REQUIRE(ownedTable->isReady());

    HttpHeaders headers(futureTable);
  }
}

ZC_TEST("HttpHeaders validation") {
  auto table = HttpHeaderTable::Builder().build();
  HttpHeaders headers(*table);

  headers.add("Valid-Name", "valid value");

  // The HTTP RFC prohibits control characters, but browsers only prohibit \0, \r, and \n. ZC goes
  // with the browsers for compatibility.
  headers.add("Valid-Name", "valid\x01value");

  // The HTTP RFC does not permit non-ASCII values.
  // ZC chooses to interpret them as UTF-8, to avoid the need for any expensive conversion.
  // Browsers apparently interpret them as LATIN-1. Applications can reinterpet these strings as
  // LATIN-1 easily enough if they really need to.
  headers.add("Valid-Name", u8"valid€value");

  ZC_EXPECT_THROW_MESSAGE("invalid header name", headers.add("Invalid Name", "value"));
  ZC_EXPECT_THROW_MESSAGE("invalid header name", headers.add("Invalid@Name", "value"));

  ZC_EXPECT_THROW_MESSAGE("invalid header value", headers.set(HttpHeaderId::HOST, "in\nvalid"));
  ZC_EXPECT_THROW_MESSAGE("invalid header value", headers.add("Valid-Name", "in\nvalid"));
}

ZC_TEST("HttpHeaders Set-Cookie handling") {
  HttpHeaderTable::Builder builder;
  auto hCookie = builder.add("Cookie");
  auto hSetCookie = builder.add("Set-Cookie");
  auto table = builder.build();

  HttpHeaders headers(*table);
  headers.set(hCookie, "Foo");
  headers.add("Cookie", "Bar");
  headers.add("Cookie", "Baz");
  headers.set(hSetCookie, "Foo");
  headers.add("Set-Cookie", "Bar");
  headers.add("Set-Cookie", "Baz");

  auto text = headers.toString();
  ZC_EXPECT(text ==
                "Cookie: Foo, Bar, Baz\r\n"
                "Set-Cookie: Foo\r\n"
                "Set-Cookie: Bar\r\n"
                "Set-Cookie: Baz\r\n"
                "\r\n",
            text);
}

// =======================================================================================

class ReadFragmenter final : public zc::AsyncIoStream {
public:
  ReadFragmenter(AsyncIoStream& inner, size_t limit) : inner(inner), limit(limit) {}

  Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    return inner.read(buffer, minBytes, zc::max(minBytes, zc::min(limit, maxBytes)));
  }
  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return inner.tryRead(buffer, minBytes, zc::max(minBytes, zc::min(limit, maxBytes)));
  }

  Maybe<uint64_t> tryGetLength() override { return inner.tryGetLength(); }

  Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
    return inner.pumpTo(output, amount);
  }

  Promise<void> write(ArrayPtr<const byte> buffer) override { return inner.write(buffer); }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return inner.write(pieces);
  }

  Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
    return inner.tryPumpFrom(input, amount);
  }

  Promise<void> whenWriteDisconnected() override { return inner.whenWriteDisconnected(); }

  void shutdownWrite() override { return inner.shutdownWrite(); }

  void abortRead() override { return inner.abortRead(); }

  void getsockopt(int level, int option, void* value, uint* length) override {
    return inner.getsockopt(level, option, value, length);
  }
  void setsockopt(int level, int option, const void* value, uint length) override {
    return inner.setsockopt(level, option, value, length);
  }

  void getsockname(struct sockaddr* addr, uint* length) override {
    return inner.getsockname(addr, length);
  }
  void getpeername(struct sockaddr* addr, uint* length) override {
    return inner.getsockname(addr, length);
  }

private:
  zc::AsyncIoStream& inner;
  size_t limit;
};

template <typename T>
class InitializeableArray : public Array<T> {
public:
  InitializeableArray(std::initializer_list<T> init) : Array<T>(zc::heapArray(init)) {}
};

enum Side { BOTH, CLIENT_ONLY, SERVER_ONLY };

struct HeaderTestCase {
  HttpHeaderId id;
  zc::StringPtr value;
};

struct HttpRequestTestCase {
  zc::StringPtr raw;

  HttpMethod method;
  zc::StringPtr path;
  InitializeableArray<HeaderTestCase> requestHeaders;
  zc::Maybe<uint64_t> requestBodySize;
  InitializeableArray<zc::StringPtr> requestBodyParts;

  Side side = BOTH;
};

struct HttpResponseTestCase {
  zc::StringPtr raw;

  uint64_t statusCode;
  zc::StringPtr statusText;
  InitializeableArray<HeaderTestCase> responseHeaders;
  zc::Maybe<uint64_t> responseBodySize;
  InitializeableArray<zc::StringPtr> responseBodyParts;

  HttpMethod method = HttpMethod::GET;

  Side side = BOTH;
};

struct HttpTestCase {
  HttpRequestTestCase request;
  HttpResponseTestCase response;
};

zc::Promise<void> writeEach(zc::AsyncOutputStream& out, zc::ArrayPtr<const zc::StringPtr> parts) {
  for (auto part : parts) co_await out.write(part.asBytes());
}

zc::Promise<void> expectRead(zc::AsyncInputStream& in, zc::StringPtr expected) {
  if (expected.size() == 0) return zc::READY_NOW;

  auto buffer = zc::heapArray<char>(expected.size());

  auto promise = in.tryRead(buffer.begin(), 1, buffer.size());
  return promise.then([&in, expected, buffer = zc::mv(buffer)](size_t amount) {
    if (amount == 0) { ZC_FAIL_ASSERT("expected data never sent", expected); }

    auto actual = buffer.first(amount);
    if (actual != expected.asBytes().first(amount)) {
      ZC_FAIL_ASSERT("data from stream doesn't match expected", expected, actual);
    }

    return expectRead(in, expected.slice(amount));
  });
}

zc::Promise<void> expectRead(zc::AsyncInputStream& in, zc::ArrayPtr<const byte> expected) {
  if (expected.size() == 0) return zc::READY_NOW;

  auto buffer = zc::heapArray<byte>(expected.size());

  auto promise = in.tryRead(buffer.begin(), 1, buffer.size());
  return promise.then([&in, expected, buffer = zc::mv(buffer)](size_t amount) {
    if (amount == 0) { ZC_FAIL_ASSERT("expected data never sent", expected); }

    auto actual = buffer.first(amount);
    if (actual != expected.first(amount)) {
      ZC_FAIL_ASSERT("data from stream doesn't match expected", expected, actual);
    }

    return expectRead(in, expected.slice(amount, expected.size()));
  });
}

zc::Promise<void> expectEnd(zc::AsyncInputStream& in) {
  static char buffer;

  auto promise = in.tryRead(&buffer, 1, 1);
  return promise.then([](size_t amount) { ZC_ASSERT(amount == 0, "expected EOF"); });
}

void testHttpClientRequest(zc::WaitScope& waitScope, const HttpRequestTestCase& testCase,
                           zc::TwoWayPipe pipe) {
  auto serverTask = expectRead(*pipe.ends[1], testCase.raw)
                        .then([&]() {
                          static const auto SIMPLE_RESPONSE =
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Length: 0\r\n"
                              "\r\n"_zcb;
                          return pipe.ends[1]->write(SIMPLE_RESPONSE);
                        })
                        .then([&]() -> zc::Promise<void> { return zc::NEVER_DONE; });

  HttpHeaderTable table;
  auto client = newHttpClient(table, *pipe.ends[0]);

  HttpHeaders headers(table);
  for (auto& header : testCase.requestHeaders) { headers.set(header.id, header.value); }

  auto request = client->request(testCase.method, testCase.path, headers, testCase.requestBodySize);
  if (testCase.requestBodyParts.size() > 0) {
    writeEach(*request.body, testCase.requestBodyParts).wait(waitScope);
  }
  request.body = nullptr;
  auto clientTask = request.response
                        .then([&](HttpClient::Response&& response) {
                          auto promise = response.body->readAllText();
                          return promise.attach(zc::mv(response.body));
                        })
                        .ignoreResult();

  serverTask.exclusiveJoin(zc::mv(clientTask)).wait(waitScope);

  // Verify no more data written by client.
  client = nullptr;
  pipe.ends[0]->shutdownWrite();
  ZC_EXPECT(pipe.ends[1]->readAllText().wait(waitScope) == "");
}

void testHttpClientResponse(zc::WaitScope& waitScope, const HttpResponseTestCase& testCase,
                            size_t readFragmentSize, zc::TwoWayPipe pipe) {
  ReadFragmenter fragmenter(*pipe.ends[0], readFragmentSize);

  auto expectedReqText = testCase.method == HttpMethod::GET || testCase.method == HttpMethod::HEAD
                             ? zc::str(testCase.method, " / HTTP/1.1\r\n\r\n")
                             : zc::str(testCase.method, " / HTTP/1.1\r\nContent-Length: 0\r\n");

  auto serverTask = expectRead(*pipe.ends[1], expectedReqText)
                        .then([&]() { return pipe.ends[1]->write(testCase.raw.asBytes()); })
                        .then([&]() -> zc::Promise<void> {
                          pipe.ends[1]->shutdownWrite();
                          return zc::NEVER_DONE;
                        });

  HttpHeaderTable table;
  auto client = newHttpClient(table, fragmenter);

  HttpHeaders headers(table);
  auto request = client->request(testCase.method, "/", headers, uint64_t(0));
  request.body = nullptr;
  auto clientTask =
      request.response
          .then([&](HttpClient::Response&& response) {
            ZC_EXPECT(response.statusCode == testCase.statusCode);
            ZC_EXPECT(response.statusText == testCase.statusText);

            for (auto& header : testCase.responseHeaders) {
              ZC_EXPECT(ZC_ASSERT_NONNULL(response.headers->get(header.id)) == header.value);
            }
            auto promise = response.body->readAllText();
            return promise.attach(zc::mv(response.body));
          })
          .then([&](zc::String body) {
            ZC_EXPECT(body == zc::strArray(testCase.responseBodyParts, ""), body);
          });

  serverTask.exclusiveJoin(zc::mv(clientTask)).wait(waitScope);

  // Verify no more data written by client.
  client = nullptr;
  pipe.ends[0]->shutdownWrite();
  ZC_EXPECT(pipe.ends[1]->readAllText().wait(waitScope) == "");
}

void testHttpClient(zc::WaitScope& waitScope, HttpHeaderTable& table, HttpClient& client,
                    const HttpTestCase& testCase) {
  ZC_CONTEXT(testCase.request.raw, testCase.response.raw);

  HttpHeaders headers(table);
  for (auto& header : testCase.request.requestHeaders) { headers.set(header.id, header.value); }

  auto request = client.request(testCase.request.method, testCase.request.path, headers,
                                testCase.request.requestBodySize);
  for (auto& part : testCase.request.requestBodyParts) {
    request.body->write(part.asBytes()).wait(waitScope);
  }
  request.body = nullptr;

  auto response = request.response.wait(waitScope);

  ZC_EXPECT(response.statusCode == testCase.response.statusCode);
  auto body = response.body->readAllText().wait(waitScope);
  if (testCase.request.method == HttpMethod::HEAD) {
    ZC_EXPECT(body == "");
  } else {
    ZC_EXPECT(body == zc::strArray(testCase.response.responseBodyParts, ""), body);
  }
}

class TestHttpService final : public HttpService {
public:
  TestHttpService(const HttpRequestTestCase& expectedRequest, const HttpResponseTestCase& response,
                  HttpHeaderTable& table)
      : singleExpectedRequest(&expectedRequest),
        singleResponse(&response),
        responseHeaders(table) {}
  TestHttpService(zc::ArrayPtr<const HttpTestCase> testCases, HttpHeaderTable& table)
      : singleExpectedRequest(nullptr),
        singleResponse(nullptr),
        testCases(testCases),
        responseHeaders(table) {}

  uint getRequestCount() { return requestCount; }

  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& responseSender) override {
    auto& expectedRequest = testCases == nullptr
                                ? *singleExpectedRequest
                                : testCases[requestCount % testCases.size()].request;
    auto& response = testCases == nullptr ? *singleResponse
                                          : testCases[requestCount % testCases.size()].response;

    ++requestCount;

    ZC_EXPECT(method == expectedRequest.method, method);
    ZC_EXPECT(url == expectedRequest.path, url);

    for (auto& header : expectedRequest.requestHeaders) {
      ZC_EXPECT(ZC_ASSERT_NONNULL(headers.get(header.id)) == header.value);
    }

    auto size = requestBody.tryGetLength();
    ZC_IF_SOME(expectedSize, expectedRequest.requestBodySize) {
      ZC_IF_SOME(s, size) { ZC_EXPECT(s == expectedSize, s); }
      else { ZC_FAIL_EXPECT("tryGetLength() returned nullptr; expected known size"); }
    }
    else { ZC_EXPECT(size == zc::none); }

    return requestBody.readAllText().then(
        [this, &expectedRequest, &response, &responseSender](zc::String text) {
          ZC_EXPECT(text == zc::strArray(expectedRequest.requestBodyParts, ""), text);

          responseHeaders.clear();
          for (auto& header : response.responseHeaders) {
            responseHeaders.set(header.id, header.value);
          }

          auto stream = responseSender.send(response.statusCode, response.statusText,
                                            responseHeaders, response.responseBodySize);
          auto promise = writeEach(*stream, response.responseBodyParts);
          return promise.attach(zc::mv(stream));
        });
  }

private:
  const HttpRequestTestCase* singleExpectedRequest;
  const HttpResponseTestCase* singleResponse;
  zc::ArrayPtr<const HttpTestCase> testCases;
  HttpHeaders responseHeaders;
  uint requestCount = 0;
};

void testHttpServerRequest(zc::WaitScope& waitScope, zc::Timer& timer,
                           const HttpRequestTestCase& requestCase,
                           const HttpResponseTestCase& responseCase, zc::TwoWayPipe pipe) {
  HttpHeaderTable table;
  TestHttpService service(requestCase, responseCase, table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  pipe.ends[1]->write(requestCase.raw.asBytes()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1], responseCase.raw).wait(waitScope);

  listenTask.wait(waitScope);

  ZC_EXPECT(service.getRequestCount() == 1);
}

zc::ArrayPtr<const HttpRequestTestCase> requestTestCases() {
  static const auto HUGE_STRING = zc::strArray(zc::repeat("abcdefgh", 4096), "");
  static const auto HUGE_REQUEST = zc::str(
      "GET / HTTP/1.1\r\n"
      "Host: ",
      HUGE_STRING,
      "\r\n"
      "\r\n");

  static const HttpRequestTestCase REQUEST_TEST_CASES[]{
      {
          "GET /foo/bar HTTP/1.1\r\n"
          "Host: example.com\r\n"
          "\r\n",

          HttpMethod::GET,
          "/foo/bar",
          {{HttpHeaderId::HOST, "example.com"}},
          uint64_t(0),
          {},
      },

      {
          "HEAD /foo/bar HTTP/1.1\r\n"
          "Host: example.com\r\n"
          "\r\n",

          HttpMethod::HEAD,
          "/foo/bar",
          {{HttpHeaderId::HOST, "example.com"}},
          uint64_t(0),
          {},
      },

      {
          "POST / HTTP/1.1\r\n"
          "Content-Length: 9\r\n"
          "Host: example.com\r\n"
          "Content-Type: text/plain\r\n"
          "\r\n"
          "foobarbaz",

          HttpMethod::POST,
          "/",
          {
              {HttpHeaderId::HOST, "example.com"},
              {HttpHeaderId::CONTENT_TYPE, "text/plain"},
          },
          9,
          {"foo", "bar", "baz"},
      },

      {
          "POST / HTTP/1.1\r\n"
          "Transfer-Encoding: chunked\r\n"
          "Host: example.com\r\n"
          "Content-Type: text/plain\r\n"
          "\r\n"
          "3\r\n"
          "foo\r\n"
          "6\r\n"
          "barbaz\r\n"
          "0\r\n"
          "\r\n",

          HttpMethod::POST,
          "/",
          {
              {HttpHeaderId::HOST, "example.com"},
              {HttpHeaderId::CONTENT_TYPE, "text/plain"},
          },
          zc::none,
          {"foo", "barbaz"},
      },

      {
          "POST / HTTP/1.1\r\n"
          "Transfer-Encoding: chunked\r\n"
          "Host: example.com\r\n"
          "Content-Type: text/plain\r\n"
          "\r\n"
          "1d\r\n"
          "0123456789abcdef0123456789abc\r\n"
          "0\r\n"
          "\r\n",

          HttpMethod::POST,
          "/",
          {
              {HttpHeaderId::HOST, "example.com"},
              {HttpHeaderId::CONTENT_TYPE, "text/plain"},
          },
          zc::none,
          {"0123456789abcdef0123456789abc"},
      },

      {HUGE_REQUEST,

       HttpMethod::GET,
       "/",
       {{HttpHeaderId::HOST, HUGE_STRING}},
       uint64_t(0),
       {}},

      {
          "GET /foo/bar HTTP/1.1\r\n"
          "Content-Length: 6\r\n"
          "Host: example.com\r\n"
          "\r\n"
          "foobar",

          HttpMethod::GET,
          "/foo/bar",
          {{HttpHeaderId::HOST, "example.com"}},
          uint64_t(6),
          {"foobar"},
      },

      {
          "GET /foo/bar HTTP/1.1\r\n"
          "Transfer-Encoding: chunked\r\n"
          "Host: example.com\r\n"
          "\r\n"
          "3\r\n"
          "foo\r\n"
          "3\r\n"
          "bar\r\n"
          "0\r\n"
          "\r\n",

          HttpMethod::GET,
          "/foo/bar",
          {{HttpHeaderId::HOST, "example.com"}, {HttpHeaderId::TRANSFER_ENCODING, "chunked"}},
          zc::none,
          {"foo", "bar"},
      }};

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents REQUEST_TEST_CASES from implicitly
  //   casting to our return type.
  return zc::arrayPtr(REQUEST_TEST_CASES, zc::size(REQUEST_TEST_CASES));
}

zc::ArrayPtr<const HttpResponseTestCase> responseTestCases() {
  static const HttpResponseTestCase RESPONSE_TEST_CASES[]{
      {
          "HTTP/1.1 200 OK\r\n"
          "Content-Type: text/plain\r\n"
          "Connection: close\r\n"
          "\r\n"
          "baz qux",

          200,
          "OK",
          {{HttpHeaderId::CONTENT_TYPE, "text/plain"}},
          zc::none,
          {"baz qux"},

          HttpMethod::GET,
          CLIENT_ONLY,  // Server never sends connection: close
      },

      {
          "HTTP/1.1 200 OK\r\n"
          "Content-Type: text/plain\r\n"
          "Transfer-Encoding: identity\r\n"
          "Content-Length: foobar\r\n"  // intentionally wrong
          "\r\n"
          "baz qux",

          200,
          "OK",
          {{HttpHeaderId::CONTENT_TYPE, "text/plain"}},
          zc::none,
          {"baz qux"},

          HttpMethod::GET,
          CLIENT_ONLY,  // Server never sends transfer-encoding: identity
      },

      {
          "HTTP/1.1 200 OK\r\n"
          "Content-Type: text/plain\r\n"
          "\r\n"
          "baz qux",

          200,
          "OK",
          {{HttpHeaderId::CONTENT_TYPE, "text/plain"}},
          zc::none,
          {"baz qux"},

          HttpMethod::GET,
          CLIENT_ONLY,  // Server never sends non-delimited message
      },

      {
          "HTTP/1.1 200 OK\r\n"
          "Content-Length: 123\r\n"
          "Content-Type: text/plain\r\n"
          "\r\n",

          200,
          "OK",
          {{HttpHeaderId::CONTENT_TYPE, "text/plain"}},
          123,
          {},

          HttpMethod::HEAD,
      },

      {
          "HTTP/1.1 200 OK\r\n"
          "Content-Length: foobar\r\n"
          "Content-Type: text/plain\r\n"
          "\r\n",

          200,
          "OK",
          {{HttpHeaderId::CONTENT_TYPE, "text/plain"}, {HttpHeaderId::CONTENT_LENGTH, "foobar"}},
          123,
          {},

          HttpMethod::HEAD,
      },

      // Zero-length expected size response to HEAD request has no Content-Length header.
      {
          "HTTP/1.1 200 OK\r\n"
          "\r\n",

          200,
          "OK",
          {},
          uint64_t(0),
          {},

          HttpMethod::HEAD,
      },

      {
          "HTTP/1.1 204 No Content\r\n"
          "\r\n",

          204,
          "No Content",
          {},
          uint64_t(0),
          {},
      },

      {
          "HTTP/1.1 205 Reset Content\r\n"
          "Content-Length: 0\r\n"
          "\r\n",

          205,
          "Reset Content",
          {},
          uint64_t(0),
          {},
      },

      {
          "HTTP/1.1 304 Not Modified\r\n"
          "\r\n",

          304,
          "Not Modified",
          {},
          uint64_t(0),
          {},
      },

      {"HTTP/1.1 200 OK\r\n"
       "Content-Length: 8\r\n"
       "Content-Type: text/plain\r\n"
       "\r\n"
       "quxcorge",

       200,
       "OK",
       {{HttpHeaderId::CONTENT_TYPE, "text/plain"}},
       8,
       {"qux", "corge"}},

      {"HTTP/1.1 200 OK\r\n"
       "Transfer-Encoding: chunked\r\n"
       "Content-Type: text/plain\r\n"
       "\r\n"
       "3\r\n"
       "qux\r\n"
       "5\r\n"
       "corge\r\n"
       "0\r\n"
       "\r\n",

       200,
       "OK",
       {{HttpHeaderId::CONTENT_TYPE, "text/plain"}},
       zc::none,
       {"qux", "corge"}},
  };

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents RESPONSE_TEST_CASES from implicitly
  //   casting to our return type.
  return zc::arrayPtr(RESPONSE_TEST_CASES, zc::size(RESPONSE_TEST_CASES));
}

ZC_TEST("HttpClient requests") {
  ZC_HTTP_TEST_SETUP_IO;

  for (auto& testCase : requestTestCases()) {
    if (testCase.side == SERVER_ONLY) continue;
    ZC_CONTEXT(testCase.raw);
    testHttpClientRequest(waitScope, testCase, ZC_HTTP_TEST_CREATE_2PIPE);
  }
}

ZC_TEST("HttpClient responses") {
  ZC_HTTP_TEST_SETUP_IO;
  size_t FRAGMENT_SIZES[] = {1, 2, 3, 4, 5, 6, 7, 8, 16, 31, zc::maxValue};

  for (auto& testCase : responseTestCases()) {
    if (testCase.side == SERVER_ONLY) continue;
    for (size_t fragmentSize : FRAGMENT_SIZES) {
      ZC_CONTEXT(testCase.raw, fragmentSize);
      testHttpClientResponse(waitScope, testCase, fragmentSize, ZC_HTTP_TEST_CREATE_2PIPE);
    }
  }
}

ZC_TEST("HttpClient canceled write") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto serverPromise = pipe.ends[1]->readAllText();

  {
    HttpHeaderTable table;
    auto client = newHttpClient(table, *pipe.ends[0]);

    auto body = zc::heapArray<byte>(4096);
    memset(body.begin(), 0xcf, body.size());

    auto req = client->request(HttpMethod::POST, "/", HttpHeaders(table), uint64_t(4096));

    // Note: This poll() forces the server to receive what was written so far. Otherwise,
    //   cancelling the write below may in fact cancel the previous write as well.
    ZC_EXPECT(!serverPromise.poll(waitScope));

    // Start a write and immediately cancel it.
    { auto ignore ZC_UNUSED = req.body->write(body); }

    ZC_EXPECT_THROW_MESSAGE("overwrote", req.body->write("foo"_zcb).wait(waitScope));
    req.body = nullptr;

    ZC_EXPECT(!serverPromise.poll(waitScope));

    ZC_EXPECT_THROW_MESSAGE(
        "can't start new request until previous request body",
        client->request(HttpMethod::GET, "/", HttpHeaders(table)).response.wait(waitScope));
  }

  pipe.ends[0]->shutdownWrite();
  auto text = serverPromise.wait(waitScope);
  ZC_EXPECT(text == "POST / HTTP/1.1\r\nContent-Length: 4096\r\n\r\n", text);
}

ZC_TEST("HttpClient chunked body gather-write") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto serverPromise = pipe.ends[1]->readAllText();

  {
    HttpHeaderTable table;
    auto client = newHttpClient(table, *pipe.ends[0]);

    auto req = client->request(HttpMethod::POST, "/", HttpHeaders(table));

    zc::ArrayPtr<const byte> bodyParts[] = {"foo"_zcb, " "_zcb, "bar"_zcb, " "_zcb, "baz"_zcb};

    req.body->write(zc::arrayPtr(bodyParts, zc::size(bodyParts))).wait(waitScope);
    req.body = nullptr;

    // Wait for a response so the client has a chance to end the request body with a 0-chunk.
    zc::StringPtr responseText = "HTTP/1.1 204 No Content\r\n\r\n";
    pipe.ends[1]->write(responseText.asBytes()).wait(waitScope);
    auto response = req.response.wait(waitScope);
  }

  pipe.ends[0]->shutdownWrite();

  auto text = serverPromise.wait(waitScope);
  ZC_EXPECT(text ==
                "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
                "b\r\nfoo bar baz\r\n0\r\n\r\n",
            text);
}

ZC_TEST("HttpClient chunked body pump from fixed length stream") {
  class FixedBodyStream final : public zc::AsyncInputStream {
    Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
      auto n = zc::min(body.size(), maxBytes);
      n = zc::max(n, minBytes);
      n = zc::min(n, body.size());
      memcpy(buffer, body.begin(), n);
      body = body.slice(n);
      return n;
    }

    Maybe<uint64_t> tryGetLength() override { return body.size(); }

    zc::StringPtr body = "foo bar baz";
  };

  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto serverPromise = pipe.ends[1]->readAllText();

  {
    HttpHeaderTable table;
    auto client = newHttpClient(table, *pipe.ends[0]);

    auto req = client->request(HttpMethod::POST, "/", HttpHeaders(table));

    FixedBodyStream bodyStream;
    bodyStream.pumpTo(*req.body).wait(waitScope);
    req.body = nullptr;

    // Wait for a response so the client has a chance to end the request body with a 0-chunk.
    zc::StringPtr responseText = "HTTP/1.1 204 No Content\r\n\r\n";
    pipe.ends[1]->write(responseText.asBytes()).wait(waitScope);
    auto response = req.response.wait(waitScope);
  }

  pipe.ends[0]->shutdownWrite();

  auto text = serverPromise.wait(waitScope);
  ZC_EXPECT(text ==
                "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
                "b\r\nfoo bar baz\r\n0\r\n\r\n",
            text);
}

ZC_TEST("HttpServer requests") {
  HttpResponseTestCase RESPONSE = {
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 3\r\n"
      "\r\n"
      "foo",

      200,
      "OK",
      {},
      3,
      {"foo"}};

  HttpResponseTestCase HEAD_RESPONSE = {
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 3\r\n"
      "\r\n",

      200,
      "OK",
      {},
      3,
      {"foo"}};

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  for (auto& testCase : requestTestCases()) {
    if (testCase.side == CLIENT_ONLY) continue;
    ZC_CONTEXT(testCase.raw);
    testHttpServerRequest(waitScope, timer, testCase,
                          testCase.method == HttpMethod::HEAD ? HEAD_RESPONSE : RESPONSE,
                          ZC_HTTP_TEST_CREATE_2PIPE);
  }
}

ZC_TEST("HttpServer responses") {
  HttpRequestTestCase REQUEST = {
      "GET / HTTP/1.1\r\n"
      "\r\n",

      HttpMethod::GET,
      "/",
      {},
      uint64_t(0),
      {},
  };

  HttpRequestTestCase HEAD_REQUEST = {
      "HEAD / HTTP/1.1\r\n"
      "\r\n",

      HttpMethod::HEAD,
      "/",
      {},
      uint64_t(0),
      {},
  };

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  for (auto& testCase : responseTestCases()) {
    if (testCase.side == CLIENT_ONLY) continue;
    ZC_CONTEXT(testCase.raw);
    testHttpServerRequest(waitScope, timer,
                          testCase.method == HttpMethod::HEAD ? HEAD_REQUEST : REQUEST, testCase,
                          ZC_HTTP_TEST_CREATE_2PIPE);
  }
}

// -----------------------------------------------------------------------------

zc::ArrayPtr<const HttpTestCase> pipelineTestCases() {
  static const HttpTestCase PIPELINE_TESTS[] = {
      {
          {
              "GET / HTTP/1.1\r\n"
              "\r\n",

              HttpMethod::GET,
              "/",
              {},
              uint64_t(0),
              {},
          },
          {"HTTP/1.1 200 OK\r\n"
           "Content-Length: 7\r\n"
           "\r\n"
           "foo bar",

           200,
           "OK",
           {},
           7,
           {"foo bar"}},
      },

      {
          {
              "POST /foo HTTP/1.1\r\n"
              "Content-Length: 6\r\n"
              "\r\n"
              "grault",

              HttpMethod::POST,
              "/foo",
              {},
              6,
              {"grault"},
          },
          {"HTTP/1.1 404 Not Found\r\n"
           "Content-Length: 13\r\n"
           "\r\n"
           "baz qux corge",

           404,
           "Not Found",
           {},
           13,
           {"baz qux corge"}},
      },

      // Throw a zero-size request/response into the pipeline to check for a bug that existed with
      // them previously.
      {
          {
              "POST /foo HTTP/1.1\r\n"
              "Content-Length: 0\r\n"
              "\r\n",

              HttpMethod::POST,
              "/foo",
              {},
              uint64_t(0),
              {},
          },
          {"HTTP/1.1 200 OK\r\n"
           "Content-Length: 0\r\n"
           "\r\n",

           200,
           "OK",
           {},
           uint64_t(0),
           {}},
      },

      // Also a zero-size chunked request/response.
      {
          {
              "POST /foo HTTP/1.1\r\n"
              "Transfer-Encoding: chunked\r\n"
              "\r\n"
              "0\r\n"
              "\r\n",

              HttpMethod::POST,
              "/foo",
              {},
              zc::none,
              {},
          },
          {"HTTP/1.1 200 OK\r\n"
           "Transfer-Encoding: chunked\r\n"
           "\r\n"
           "0\r\n"
           "\r\n",

           200,
           "OK",
           {},
           zc::none,
           {}},
      },

      {
          {
              "POST /bar HTTP/1.1\r\n"
              "Transfer-Encoding: chunked\r\n"
              "\r\n"
              "6\r\n"
              "garply\r\n"
              "5\r\n"
              "waldo\r\n"
              "0\r\n"
              "\r\n",

              HttpMethod::POST,
              "/bar",
              {},
              zc::none,
              {"garply", "waldo"},
          },
          {"HTTP/1.1 200 OK\r\n"
           "Transfer-Encoding: chunked\r\n"
           "\r\n"
           "4\r\n"
           "fred\r\n"
           "5\r\n"
           "plugh\r\n"
           "0\r\n"
           "\r\n",

           200,
           "OK",
           {},
           zc::none,
           {"fred", "plugh"}},
      },

      {
          {
              "HEAD / HTTP/1.1\r\n"
              "\r\n",

              HttpMethod::HEAD,
              "/",
              {},
              uint64_t(0),
              {},
          },
          {"HTTP/1.1 200 OK\r\n"
           "Content-Length: 7\r\n"
           "\r\n",

           200,
           "OK",
           {},
           7,
           {"foo bar"}},
      },

      // Zero-length expected size response to HEAD request has no Content-Length header.
      {
          {
              "HEAD / HTTP/1.1\r\n"
              "\r\n",

              HttpMethod::HEAD,
              "/",
              {},
              uint64_t(0),
              {},
          },
          {
              "HTTP/1.1 200 OK\r\n"
              "\r\n",

              200,
              "OK",
              {},
              uint64_t(0),
              {},
              HttpMethod::HEAD,
          },
      },
  };

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents RESPONSE_TEST_CASES from implicitly
  //   casting to our return type.
  return zc::arrayPtr(PIPELINE_TESTS, zc::size(PIPELINE_TESTS));
}

ZC_TEST("HttpClient pipeline") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::Promise<void> writeResponsesPromise = zc::READY_NOW;
  for (auto& testCase : PIPELINE_TESTS) {
    writeResponsesPromise =
        writeResponsesPromise
            .then([&]() { return expectRead(*pipe.ends[1], testCase.request.raw); })
            .then([&]() { return pipe.ends[1]->write(testCase.response.raw.asBytes()); });
  }

  HttpHeaderTable table;
  auto client = newHttpClient(table, *pipe.ends[0]);

  for (auto& testCase : PIPELINE_TESTS) { testHttpClient(waitScope, table, *client, testCase); }

  client = nullptr;
  pipe.ends[0]->shutdownWrite();

  writeResponsesPromise.wait(waitScope);
}

ZC_TEST("HttpClient parallel pipeline") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::Promise<void> readRequestsPromise = zc::READY_NOW;
  zc::Promise<void> writeResponsesPromise = zc::READY_NOW;
  for (auto& testCase : PIPELINE_TESTS) {
    auto forked =
        readRequestsPromise.then([&]() { return expectRead(*pipe.ends[1], testCase.request.raw); })
            .fork();
    readRequestsPromise = forked.addBranch();

    // Don't write each response until the corresponding request is received.
    auto promises = zc::heapArrayBuilder<zc::Promise<void>>(2);
    promises.add(forked.addBranch());
    promises.add(zc::mv(writeResponsesPromise));
    writeResponsesPromise = zc::joinPromises(promises.finish()).then([&]() {
      return pipe.ends[1]->write(testCase.response.raw.asBytes());
    });
  }

  HttpHeaderTable table;
  auto client = newHttpClient(table, *pipe.ends[0]);

  auto responsePromises = ZC_MAP(testCase, PIPELINE_TESTS) {
    ZC_CONTEXT(testCase.request.raw, testCase.response.raw);

    HttpHeaders headers(table);
    for (auto& header : testCase.request.requestHeaders) { headers.set(header.id, header.value); }

    auto request = client->request(testCase.request.method, testCase.request.path, headers,
                                   testCase.request.requestBodySize);
    for (auto& part : testCase.request.requestBodyParts) {
      request.body->write(part.asBytes()).wait(waitScope);
    }

    return zc::mv(request.response);
  };

  for (auto i : zc::indices(PIPELINE_TESTS)) {
    auto& testCase = PIPELINE_TESTS[i];
    auto response = responsePromises[i].wait(waitScope);

    ZC_EXPECT(response.statusCode == testCase.response.statusCode);
    auto body = response.body->readAllText().wait(waitScope);
    if (testCase.request.method == HttpMethod::HEAD) {
      ZC_EXPECT(body == "");
    } else {
      ZC_EXPECT(body == zc::strArray(testCase.response.responseBodyParts, ""), body);
    }
  }

  client = nullptr;
  pipe.ends[0]->shutdownWrite();

  writeResponsesPromise.wait(waitScope);
}

ZC_TEST("HttpServer pipeline") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  for (auto& testCase : PIPELINE_TESTS) {
    ZC_CONTEXT(testCase.request.raw, testCase.response.raw);
    pipe.ends[1]->write(testCase.request.raw.asBytes()).wait(waitScope);
    expectRead(*pipe.ends[1], testCase.response.raw).wait(waitScope);
  }

  pipe.ends[1]->shutdownWrite();
  listenTask.wait(waitScope);

  ZC_EXPECT(service.getRequestCount() == zc::size(PIPELINE_TESTS));
}

ZC_TEST("HttpServer parallel pipeline") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto allRequestText =
      zc::strArray(ZC_MAP(testCase, PIPELINE_TESTS) { return testCase.request.raw; }, "");
  auto allResponseText =
      zc::strArray(ZC_MAP(testCase, PIPELINE_TESTS) { return testCase.response.raw; }, "");

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  pipe.ends[1]->write(allRequestText.asBytes()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  auto rawResponse = pipe.ends[1]->readAllText().wait(waitScope);
  ZC_EXPECT(rawResponse == allResponseText, rawResponse);

  listenTask.wait(waitScope);

  ZC_EXPECT(service.getRequestCount() == zc::size(PIPELINE_TESTS));
}

ZC_TEST("HttpClient <-> HttpServer") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[1]));
  auto client = newHttpClient(table, *pipe.ends[0]);

  for (auto& testCase : PIPELINE_TESTS) { testHttpClient(waitScope, table, *client, testCase); }

  client = nullptr;
  pipe.ends[0]->shutdownWrite();
  listenTask.wait(waitScope);
  ZC_EXPECT(service.getRequestCount() == zc::size(PIPELINE_TESTS));
}

// -----------------------------------------------------------------------------

ZC_TEST("HttpInputStream requests") {
  ZC_HTTP_TEST_SETUP_IO;

  zc::HttpHeaderTable table;

  auto pipe = zc::newOneWayPipe();
  auto input = newHttpInputStream(*pipe.in, table);

  zc::Promise<void> writeQueue = zc::READY_NOW;

  for (auto& testCase : requestTestCases()) {
    writeQueue = writeQueue.then([&]() { return pipe.out->write(testCase.raw.asBytes()); });
  }
  writeQueue = writeQueue.then([&]() { pipe.out = nullptr; });

  for (auto& testCase : requestTestCases()) {
    ZC_CONTEXT(testCase.raw);

    ZC_ASSERT(input->awaitNextMessage().wait(waitScope));

    auto req = input->readRequest().wait(waitScope);
    ZC_EXPECT(req.method == testCase.method);
    ZC_EXPECT(req.url == testCase.path);
    for (auto& header : testCase.requestHeaders) {
      ZC_EXPECT(ZC_ASSERT_NONNULL(req.headers.get(header.id)) == header.value);
    }
    auto body = req.body->readAllText().wait(waitScope);
    ZC_EXPECT(body == zc::strArray(testCase.requestBodyParts, ""));
  }

  writeQueue.wait(waitScope);
  ZC_EXPECT(!input->awaitNextMessage().wait(waitScope));
}

ZC_TEST("HttpInputStream responses") {
  ZC_HTTP_TEST_SETUP_IO;

  zc::HttpHeaderTable table;

  auto pipe = zc::newOneWayPipe();
  auto input = newHttpInputStream(*pipe.in, table);

  zc::Promise<void> writeQueue = zc::READY_NOW;

  for (auto& testCase : responseTestCases()) {
    if (testCase.side == CLIENT_ONLY) continue;  // skip Connection: close case.
    writeQueue = writeQueue.then([&]() { return pipe.out->write(testCase.raw.asBytes()); });
  }
  writeQueue = writeQueue.then([&]() { pipe.out = nullptr; });

  for (auto& testCase : responseTestCases()) {
    if (testCase.side == CLIENT_ONLY) continue;  // skip Connection: close case.
    ZC_CONTEXT(testCase.raw);

    ZC_ASSERT(input->awaitNextMessage().wait(waitScope));

    auto resp = input->readResponse(testCase.method).wait(waitScope);
    ZC_EXPECT(resp.statusCode == testCase.statusCode);
    ZC_EXPECT(resp.statusText == testCase.statusText);
    for (auto& header : testCase.responseHeaders) {
      ZC_EXPECT(ZC_ASSERT_NONNULL(resp.headers.get(header.id)) == header.value);
    }
    auto body = resp.body->readAllText().wait(waitScope);
    ZC_EXPECT(body == zc::strArray(testCase.responseBodyParts, ""));
  }

  writeQueue.wait(waitScope);
  ZC_EXPECT(!input->awaitNextMessage().wait(waitScope));
}

ZC_TEST("HttpInputStream bare messages") {
  ZC_HTTP_TEST_SETUP_IO;

  zc::HttpHeaderTable table;

  auto pipe = zc::newOneWayPipe();
  auto input = newHttpInputStream(*pipe.in, table);

  zc::StringPtr messages =
      "Content-Length: 6\r\n"
      "\r\n"
      "foobar"
      "Content-Length: 11\r\n"
      "Content-Type: some/type\r\n"
      "\r\n"
      "bazquxcorge"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "6\r\n"
      "grault\r\n"
      "b\r\n"
      "garplywaldo\r\n"
      "0\r\n"
      "\r\n"_zc;

  zc::Promise<void> writeTask =
      pipe.out->write(messages.asBytes()).then([&]() { pipe.out = nullptr; });

  {
    ZC_ASSERT(input->awaitNextMessage().wait(waitScope));
    auto message = input->readMessage().wait(waitScope);
    ZC_EXPECT(ZC_ASSERT_NONNULL(message.headers.get(HttpHeaderId::CONTENT_LENGTH)) == "6");
    ZC_EXPECT(message.body->readAllText().wait(waitScope) == "foobar");
  }
  {
    ZC_ASSERT(input->awaitNextMessage().wait(waitScope));
    auto message = input->readMessage().wait(waitScope);
    ZC_EXPECT(ZC_ASSERT_NONNULL(message.headers.get(HttpHeaderId::CONTENT_LENGTH)) == "11");
    ZC_EXPECT(ZC_ASSERT_NONNULL(message.headers.get(HttpHeaderId::CONTENT_TYPE)) == "some/type");
    ZC_EXPECT(message.body->readAllText().wait(waitScope) == "bazquxcorge");
  }
  {
    ZC_ASSERT(input->awaitNextMessage().wait(waitScope));
    auto message = input->readMessage().wait(waitScope);
    ZC_EXPECT(ZC_ASSERT_NONNULL(message.headers.get(HttpHeaderId::TRANSFER_ENCODING)) == "chunked");
    ZC_EXPECT(message.body->readAllText().wait(waitScope) == "graultgarplywaldo");
  }

  writeTask.wait(waitScope);
  ZC_EXPECT(!input->awaitNextMessage().wait(waitScope));
}

// -----------------------------------------------------------------------------

ZC_TEST("WebSocket core protocol") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto client = newWebSocket(zc::mv(pipe.ends[0]), zc::none);
  auto server = newWebSocket(zc::mv(pipe.ends[1]), zc::none);

  auto mediumString = zc::strArray(zc::repeat(zc::StringPtr("123456789"), 30), "");
  auto bigString = zc::strArray(zc::repeat(zc::StringPtr("123456789"), 10000), "");

  auto clientTask = client->send(zc::StringPtr("hello"))
                        .then([&]() { return client->send(mediumString); })
                        .then([&]() { return client->send(bigString); })
                        .then([&]() { return client->send(zc::StringPtr("world").asBytes()); })
                        .then([&]() { return client->close(1234, "bored"); })
                        .then([&]() { ZC_EXPECT(client->sentByteCount() == 90307) });

  {
    auto message = server->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "hello");
  }

  {
    auto message = server->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == mediumString);
  }

  {
    auto message = server->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == bigString);
  }

  {
    auto message = server->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::Array<byte>>());
    ZC_EXPECT(zc::str(message.get<zc::Array<byte>>().asChars()) == "world");
  }

  {
    auto message = server->receive().wait(waitScope);
    ZC_ASSERT(message.is<WebSocket::Close>());
    ZC_EXPECT(message.get<WebSocket::Close>().code == 1234);
    ZC_EXPECT(message.get<WebSocket::Close>().reason == "bored");
    ZC_EXPECT(server->receivedByteCount() == 90307);
  }

  auto serverTask = server->close(4321, "whatever");

  {
    auto message = client->receive().wait(waitScope);
    ZC_ASSERT(message.is<WebSocket::Close>());
    ZC_EXPECT(message.get<WebSocket::Close>().code == 4321);
    ZC_EXPECT(message.get<WebSocket::Close>().reason == "whatever");
    ZC_EXPECT(client->receivedByteCount() == 12);
  }

  clientTask.wait(waitScope);
  serverTask.wait(waitScope);
}

ZC_TEST("WebSocket fragmented") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto client = zc::mv(pipe.ends[0]);
  auto server = newWebSocket(zc::mv(pipe.ends[1]), zc::none);

  byte DATA[] = {
      0x01, 0x06, 'h', 'e', 'l', 'l', 'o', ' ',

      0x00, 0x03, 'w', 'o', 'r',

      0x80, 0x02, 'l', 'd',
  };

  auto clientTask = client->write(DATA);

  {
    auto message = server->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "hello world");
  }

  clientTask.wait(waitScope);
}

#if ZC_HAS_ZLIB
ZC_TEST("WebSocket compressed fragment") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto client = zc::mv(pipe.ends[0]);
  auto server = newWebSocket(zc::mv(pipe.ends[1]), zc::none,
                             CompressionParameters{
                                 .outboundNoContextTakeover = false,
                                 .inboundNoContextTakeover = false,
                                 .outboundMaxWindowBits = 15,
                                 .inboundMaxWindowBits = 15,
                             });

  // The message is "Hello", sent in two fragments, see the fragmented example at the bottom of:
  // https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.3.1
  byte COMPRESSED_DATA[] = {0x41, 0x03, 0xf2, 0x48, 0xcd,

                            0x80, 0x04, 0xc9, 0xc9, 0x07, 0x00};

  auto clientTask = client->write(COMPRESSED_DATA);

  {
    auto message = server->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "Hello");
  }

  clientTask.wait(waitScope);
}
#endif  // ZC_HAS_ZLIB

class FakeEntropySource final : public EntropySource {
public:
  void generate(zc::ArrayPtr<byte> buffer) override {
    static constexpr byte DUMMY[4] = {12, 34, 56, 78};

    for (auto i : zc::indices(buffer)) { buffer[i] = DUMMY[i % sizeof(DUMMY)]; }
  }
};

ZC_TEST("WebSocket masked") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;
  FakeEntropySource maskGenerator;

  auto client = zc::mv(pipe.ends[0]);
  auto server = newWebSocket(zc::mv(pipe.ends[1]), maskGenerator);

  byte DATA[] = {
      0x81, 0x86, 12, 34, 56, 78, 'h' ^ 12, 'e' ^ 34, 'l' ^ 56, 'l' ^ 78, 'o' ^ 12, ' ' ^ 34,
  };

  auto clientTask = client->write(DATA);
  auto serverTask = server->send(zc::StringPtr("hello "));

  {
    auto message = server->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "hello ");
  }

  expectRead(*client, DATA).wait(waitScope);

  clientTask.wait(waitScope);
  serverTask.wait(waitScope);
}

class WebSocketErrorCatcher : public WebSocketErrorHandler {
public:
  zc::Vector<zc::WebSocket::ProtocolError> errors;

  zc::Exception handleWebSocketProtocolError(zc::WebSocket::ProtocolError protocolError) override {
    errors.add(zc::mv(protocolError));
    return ZC_EXCEPTION(FAILED, protocolError.description);
  }
};

void assertContainsWebSocketClose(zc::ArrayPtr<zc::byte> data, uint16_t code,
                                  zc::Maybe<zc::StringPtr> messageSubstr) {
  ZC_ASSERT(data.size() >= 2);          // The smallest possible Close frame has size 2.
  ZC_ASSERT(data.size() <= 127);        // Maximum size for control frames.
  ZC_ASSERT((data[0] & 0xf0) == 0x80);  // Only the FIN flag is set.
  ZC_ASSERT((data[0] & 0x0f) == 8);     // OPCODE_CLOSE

  size_t payloadSize = data[1] & 0x7f;

  if (payloadSize == 0) {
    // A Close frame with no body has no status code and no reason.
    ZC_ASSERT(code == 1005);
    ZC_ASSERT(messageSubstr == zc::none);
  } else {
    ZC_ASSERT(code != 1005);
  }
  auto payload = data.slice(2);

  ZC_ASSERT(payload.size() >=
            2);  // The first two bytes are the status code, so we better have at least two bytes.
  uint16_t gotCode = (payload[0] << 8) | payload[1];
  ZC_ASSERT(gotCode == code);

  ZC_IF_SOME(needle, messageSubstr) {
    auto reason = zc::str(payload.asChars().slice(2));
    ZC_ASSERT(reason.contains(needle), reason, needle);
  }
}

ZC_TEST("WebSocket unexpected RSV bits") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  WebSocketErrorCatcher errorCatcher;
  auto client = zc::mv(pipe.ends[0]);
  auto server = newWebSocket(zc::mv(pipe.ends[1]), zc::none, zc::none, errorCatcher);

  byte DATA[] = {
      0x01, 0x06, 'h', 'e', 'l', 'l', 'o', ' ',

      0xF0, 0x05, 'w', 'o', 'r', 'l', 'd'  // all RSV bits set, plus FIN
  };

  auto rawCloseMessage = zc::heapArray<zc::byte>(129);
  auto clientTask = client->write(DATA).then(
      [&]() { return client->tryRead(rawCloseMessage.begin(), 2, rawCloseMessage.size()); });

  {
    bool gotException = false;
    auto serverTask = server->receive().then(
        [](auto&& m) {}, [&gotException](zc::Exception&& ex) { gotException = true; });
    serverTask.wait(waitScope);
    ZC_ASSERT(gotException);
    ZC_ASSERT(errorCatcher.errors.size() == 1);
    ZC_ASSERT(errorCatcher.errors[0].statusCode == 1002);
  }

  auto nread = clientTask.wait(waitScope);
  assertContainsWebSocketClose(rawCloseMessage.first(nread), 1002, "RSV bits"_zcc);
}

ZC_TEST("WebSocket unexpected continuation frame") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  WebSocketErrorCatcher errorCatcher;
  auto client = zc::mv(pipe.ends[0]);
  auto server = newWebSocket(zc::mv(pipe.ends[1]), zc::none, zc::none, errorCatcher);

  byte DATA[] = {
      0x80, 0x06, 'h', 'e', 'l', 'l', 'o', ' ',  // Continuation frame with no start frame, plus FIN
  };

  auto rawCloseMessage = zc::heapArray<zc::byte>(129);
  auto clientTask = client->write(DATA).then(
      [&]() { return client->tryRead(rawCloseMessage.begin(), 2, rawCloseMessage.size()); });

  {
    bool gotException = false;
    auto serverTask = server->receive().then(
        [](auto&& m) {}, [&gotException](zc::Exception&& ex) { gotException = true; });
    serverTask.wait(waitScope);
    ZC_ASSERT(gotException);
    ZC_ASSERT(errorCatcher.errors.size() == 1);
    ZC_ASSERT(errorCatcher.errors[0].statusCode == 1002);
  }

  auto nread = clientTask.wait(waitScope);
  assertContainsWebSocketClose(rawCloseMessage.first(nread), 1002,
                               "Unexpected continuation frame"_zcc);
}

ZC_TEST("WebSocket missing continuation frame") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  WebSocketErrorCatcher errorCatcher;
  auto client = zc::mv(pipe.ends[0]);
  auto server = newWebSocket(zc::mv(pipe.ends[1]), zc::none, zc::none, errorCatcher);

  byte DATA[] = {
      0x01, 0x06, 'h', 'e', 'l', 'l', 'o', ' ',  // Start frame
      0x01, 0x06, 'w', 'o', 'r', 'l', 'd', '!',  // Another start frame
  };

  auto rawCloseMessage = zc::heapArray<zc::byte>(129);
  auto clientTask = client->write(DATA).then(
      [&]() { return client->tryRead(rawCloseMessage.begin(), 2, rawCloseMessage.size()); });

  {
    bool gotException = false;
    auto serverTask = server->receive().then(
        [](auto&& m) {}, [&gotException](zc::Exception&& ex) { gotException = true; });
    serverTask.wait(waitScope);
    ZC_ASSERT(gotException);
    ZC_ASSERT(errorCatcher.errors.size() == 1);
  }

  auto nread = clientTask.wait(waitScope);
  assertContainsWebSocketClose(rawCloseMessage.first(nread), 1002,
                               "Missing continuation frame"_zcc);
}

ZC_TEST("WebSocket fragmented control frame") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  WebSocketErrorCatcher errorCatcher;
  auto client = zc::mv(pipe.ends[0]);
  auto server = newWebSocket(zc::mv(pipe.ends[1]), zc::none, zc::none, errorCatcher);

  byte DATA[] = {
      0x09, 0x04, 'd', 'a', 't', 'a'  // Fragmented ping frame
  };

  auto rawCloseMessage = zc::heapArray<zc::byte>(129);
  auto clientTask = client->write(DATA).then(
      [&]() { return client->tryRead(rawCloseMessage.begin(), 2, rawCloseMessage.size()); });

  {
    bool gotException = false;
    auto serverTask = server->receive().then(
        [](auto&& m) {}, [&gotException](zc::Exception&& ex) { gotException = true; });
    serverTask.wait(waitScope);
    ZC_ASSERT(gotException);
    ZC_ASSERT(errorCatcher.errors.size() == 1);
    ZC_ASSERT(errorCatcher.errors[0].statusCode == 1002);
  }

  auto nread = clientTask.wait(waitScope);
  assertContainsWebSocketClose(rawCloseMessage.first(nread), 1002,
                               "Received fragmented control frame"_zcc);
}

ZC_TEST("WebSocket unknown opcode") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  WebSocketErrorCatcher errorCatcher;
  auto client = zc::mv(pipe.ends[0]);
  auto server = newWebSocket(zc::mv(pipe.ends[1]), zc::none, zc::none, errorCatcher);

  byte DATA[] = {
      0x85, 0x04, 'd', 'a', 't', 'a'  // 5 is a reserved opcode
  };

  auto rawCloseMessage = zc::heapArray<zc::byte>(129);
  auto clientTask = client->write(DATA).then(
      [&]() { return client->tryRead(rawCloseMessage.begin(), 2, rawCloseMessage.size()); });

  {
    bool gotException = false;
    auto serverTask = server->receive().then(
        [](auto&& m) {}, [&gotException](zc::Exception&& ex) { gotException = true; });
    serverTask.wait(waitScope);
    ZC_ASSERT(gotException);
    ZC_ASSERT(errorCatcher.errors.size() == 1);
    ZC_ASSERT(errorCatcher.errors[0].statusCode == 1002);
  }

  auto nread = clientTask.wait(waitScope);
  assertContainsWebSocketClose(rawCloseMessage.first(nread), 1002, "Unknown opcode 5"_zcc);
}

ZC_TEST("WebSocket unsolicited pong") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto client = zc::mv(pipe.ends[0]);
  auto server = newWebSocket(zc::mv(pipe.ends[1]), zc::none);

  byte DATA[] = {
      0x01, 0x06, 'h', 'e', 'l', 'l', 'o', ' ',

      0x8A, 0x03, 'f', 'o', 'o',

      0x80, 0x05, 'w', 'o', 'r', 'l', 'd',
  };

  auto clientTask = client->write(DATA);

  {
    auto message = server->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "hello world");
  }

  clientTask.wait(waitScope);
}

void doWebSocketPingTest(zc::Maybe<EntropySource&> maskGenerator) {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto client = zc::mv(pipe.ends[0]);
  auto server = newWebSocket(zc::mv(pipe.ends[1]), maskGenerator);

  // Be extra-annoying by having the ping arrive between fragments.
  byte DATA[] = {
      0x01, 0x06, 'h', 'e', 'l', 'l', 'o', ' ',

      0x89, 0x03, 'f', 'o', 'o',

      0x80, 0x05, 'w', 'o', 'r', 'l', 'd',
  };

  auto clientTask = client->write(DATA);

  {
    auto message = server->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "hello world");
  }

  auto serverTask = server->send(zc::StringPtr("bar"));

  zc::ArrayPtr<const byte> expected;

  if (maskGenerator == zc::none) {
    static const byte EXPECTED[] = {
        0x8A, 0x03, 'f', 'o', 'o',  // pong
        0x81, 0x03, 'b', 'a', 'r',  // message
    };
    expected = EXPECTED;
  } else {
    static const byte EXPECTED[] = {
        0x8A, 0x83, 12, 34, 56, 78, 'f' ^ 12, 'o' ^ 34, 'o' ^ 56,  // masked pong
        0x81, 0x83, 12, 34, 56, 78, 'b' ^ 12, 'a' ^ 34, 'r' ^ 56,  // masked message
    };
    expected = EXPECTED;
  }

  expectRead(*client, expected).wait(waitScope);

  clientTask.wait(waitScope);
  serverTask.wait(waitScope);
}

ZC_TEST("WebSocket ping") { doWebSocketPingTest(zc::none); }

ZC_TEST("WebSocket ping with mask") {
  FakeEntropySource maskGenerator;
  doWebSocketPingTest(maskGenerator);
}

ZC_TEST("WebSocket ping mid-send") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto client = zc::mv(pipe.ends[0]);
  auto server = newWebSocket(zc::mv(pipe.ends[1]), zc::none);

  auto bigString = zc::strArray(zc::repeat(zc::StringPtr("12345678"), 65536), "");
  auto serverTask = server->send(bigString).eagerlyEvaluate(nullptr);

  byte DATA[] = {
      0x89, 0x03, 'f', 'o', 'o',  // ping
      0x81, 0x03, 'b', 'a', 'r',  // some other message
  };

  auto clientTask = client->write(DATA);

  {
    auto message = server->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "bar");
  }

  byte EXPECTED1[] = {0x81, 0x7f, 0, 0, 0, 0, 0, 8, 0, 0};
  expectRead(*client, EXPECTED1).wait(waitScope);
  expectRead(*client, bigString).wait(waitScope);

  byte EXPECTED2[] = {0x8A, 0x03, 'f', 'o', 'o'};
  expectRead(*client, EXPECTED2).wait(waitScope);

  clientTask.wait(waitScope);
  serverTask.wait(waitScope);
}

class InputOutputPair final : public zc::AsyncIoStream {
  // Creates an AsyncIoStream out of an AsyncInputStream and an AsyncOutputStream.

public:
  InputOutputPair(zc::Own<zc::AsyncInputStream> in, zc::Own<zc::AsyncOutputStream> out)
      : in(zc::mv(in)), out(zc::mv(out)) {}

  zc::Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    return in->read(buffer, minBytes, maxBytes);
  }
  zc::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return in->tryRead(buffer, minBytes, maxBytes);
  }

  Maybe<uint64_t> tryGetLength() override { return in->tryGetLength(); }

  Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount = zc::maxValue) override {
    return in->pumpTo(output, amount);
  }

  zc::Promise<void> write(ArrayPtr<const byte> buffer) override { return out->write(buffer); }

  zc::Promise<void> write(zc::ArrayPtr<const zc::ArrayPtr<const byte>> pieces) override {
    return out->write(pieces);
  }

  zc::Maybe<zc::Promise<uint64_t>> tryPumpFrom(zc::AsyncInputStream& input,
                                               uint64_t amount = zc::maxValue) override {
    return out->tryPumpFrom(input, amount);
  }

  Promise<void> whenWriteDisconnected() override { return out->whenWriteDisconnected(); }

  void shutdownWrite() override { out = nullptr; }

private:
  zc::Own<zc::AsyncInputStream> in;
  zc::Own<zc::AsyncOutputStream> out;
};

ZC_TEST("WebSocket double-ping mid-send") {
  ZC_HTTP_TEST_SETUP_IO;

  auto upPipe = newOneWayPipe();
  auto downPipe = newOneWayPipe();
  InputOutputPair client(zc::mv(downPipe.in), zc::mv(upPipe.out));
  auto server =
      newWebSocket(zc::heap<InputOutputPair>(zc::mv(upPipe.in), zc::mv(downPipe.out)), zc::none);

  auto bigString = zc::strArray(zc::repeat(zc::StringPtr("12345678"), 65536), "");
  auto serverTask = server->send(bigString).eagerlyEvaluate(nullptr);

  byte DATA[] = {
      0x89, 0x03, 'f', 'o', 'o',  // ping
      0x89, 0x03, 'q', 'u', 'x',  // ping2
      0x81, 0x03, 'b', 'a', 'r',  // some other message
  };

  auto clientTask = client.write(DATA);

  {
    auto message = server->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "bar");
  }

  byte EXPECTED1[] = {0x81, 0x7f, 0, 0, 0, 0, 0, 8, 0, 0};
  expectRead(client, EXPECTED1).wait(waitScope);
  expectRead(client, bigString).wait(waitScope);

  byte EXPECTED2[] = {0x8A, 0x03, 'q', 'u', 'x'};
  expectRead(client, EXPECTED2).wait(waitScope);

  clientTask.wait(waitScope);
  serverTask.wait(waitScope);
}

ZC_TEST("WebSocket multiple ping outside of send") {
  ZC_HTTP_TEST_SETUP_IO;

  auto upPipe = newOneWayPipe();
  auto downPipe = newOneWayPipe();
  InputOutputPair client(zc::mv(downPipe.in), zc::mv(upPipe.out));
  auto server =
      newWebSocket(zc::heap<InputOutputPair>(zc::mv(upPipe.in), zc::mv(downPipe.out)), zc::none);

  byte DATA[] = {
      0x89, 0x05, 'p',  'i',  'n',  'g',  '1', 0x89, 0x05, 'p',  'i',  'n',
      'g',  '2',  0x89, 0x05, 'p',  'i',  'n', 'g',  '3',  0x89, 0x05, 'p',
      'i',  'n',  'g',  '4',  0x81, 0x05, 'o', 't',  'h',  'e',  'r',
  };

  auto clientTask = client.write(DATA);

  {
    auto message = server->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "other");
  }

  auto bigString = zc::strArray(zc::repeat(zc::StringPtr("12345678"), 65536), "");
  auto serverTask = server->send(bigString).eagerlyEvaluate(nullptr);

  // We expect to receive pongs for only the first and last pings, because the server has the
  // option of only sending pongs for the most recently processed ping, and the last three pings
  // were processed while waiting for the write of the first pong to complete.
  byte EXPECTED1[] = {
      0x8A, 0x05, 'p', 'i', 'n', 'g', '1', 0x8A, 0x05, 'p', 'i', 'n', 'g', '4',
  };
  expectRead(client, EXPECTED1).wait(waitScope);

  byte EXPECTED2[] = {0x81, 0x7f, 0, 0, 0, 0, 0, 8, 0, 0};
  expectRead(client, EXPECTED2).wait(waitScope);
  expectRead(client, bigString).wait(waitScope);

  clientTask.wait(waitScope);
  serverTask.wait(waitScope);
}

ZC_TEST("WebSocket ping received during pong send") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto client = zc::mv(pipe.ends[0]);
  auto server = newWebSocket(zc::mv(pipe.ends[1]), zc::none);

  // Send a very large ping so that sending the pong takes a while. Then send a second ping
  // immediately after.
  byte PREFIX[] = {0x89, 0x7f, 0, 0, 0, 0, 0, 8, 0, 0};
  auto bigString = zc::strArray(zc::repeat(zc::StringPtr("12345678"), 65536), "");
  byte POSTFIX[] = {
      0x89, 0x03, 'f', 'o', 'o', 0x81, 0x03, 'b', 'a', 'r',
  };

  zc::ArrayPtr<const byte> parts[] = {PREFIX, bigString.asBytes(), POSTFIX};
  auto clientTask = client->write(parts);

  {
    auto message = server->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "bar");
  }

  byte EXPECTED1[] = {0x8A, 0x7f, 0, 0, 0, 0, 0, 8, 0, 0};
  expectRead(*client, EXPECTED1).wait(waitScope);
  expectRead(*client, bigString).wait(waitScope);

  byte EXPECTED2[] = {0x8A, 0x03, 'f', 'o', 'o'};
  expectRead(*client, EXPECTED2).wait(waitScope);

  clientTask.wait(waitScope);
}

ZC_TEST("WebSocket pump byte counting") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe1 = ZC_HTTP_TEST_CREATE_2PIPE;
  auto pipe2 = ZC_HTTP_TEST_CREATE_2PIPE;

  FakeEntropySource maskGenerator;
  auto server1 = newWebSocket(zc::mv(pipe1.ends[1]), zc::none);
  auto client2 = newWebSocket(zc::mv(pipe2.ends[0]), maskGenerator);
  auto server2 = newWebSocket(zc::mv(pipe2.ends[1]), zc::none);

  auto pumpTask = server1->pumpTo(*client2);
  auto receiveTask = server2->receive();

  // Client sends three bytes of a valid message then disconnects.
  const byte DATA[] = {0x01, 0x06, 'h'};
  pipe1.ends[0]->write(DATA).wait(waitScope);
  pipe1.ends[0] = nullptr;

  // The pump completes successfully, forwarding the disconnect.
  pumpTask.wait(waitScope);

  // The eventual receiver gets a disconnect exception.
  ZC_EXPECT_THROW(DISCONNECTED, receiveTask.wait(waitScope));

  ZC_EXPECT(server1->receivedByteCount() == 3);
#if ZC_NO_RTTI
  // Optimized socket pump will be disabled, so only whole messages are counted by client2/server2.
  ZC_EXPECT(client2->sentByteCount() == 0);
  ZC_EXPECT(server2->receivedByteCount() == 0);
#else
  ZC_EXPECT(client2->sentByteCount() == 3);
  ZC_EXPECT(server2->receivedByteCount() == 3);
#endif
}

ZC_TEST("WebSocket pump disconnect on send") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe1 = ZC_HTTP_TEST_CREATE_2PIPE;
  auto pipe2 = ZC_HTTP_TEST_CREATE_2PIPE;

  FakeEntropySource maskGenerator;
  auto client1 = newWebSocket(zc::mv(pipe1.ends[0]), maskGenerator);
  auto server1 = newWebSocket(zc::mv(pipe1.ends[1]), zc::none);
  auto client2 = newWebSocket(zc::mv(pipe2.ends[0]), maskGenerator);

  auto pumpTask = server1->pumpTo(*client2);
  auto sendTask = client1->send("hello"_zc);

  // Endpoint reads three bytes and then disconnects.
  char buffer[3]{};
  pipe2.ends[1]->read(buffer, 3).wait(waitScope);
  pipe2.ends[1] = nullptr;

  // Pump throws disconnected.
  ZC_EXPECT_THROW_RECOVERABLE(DISCONNECTED, pumpTask.wait(waitScope));

  // client1 may or may not have been able to send its whole message depending on buffering.
  sendTask
      .then([]() {},
            [](zc::Exception&& e) { ZC_EXPECT(e.getType() == zc::Exception::Type::DISCONNECTED); })
      .wait(waitScope);
}

ZC_TEST("WebSocket pump disconnect on receive") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe1 = ZC_HTTP_TEST_CREATE_2PIPE;
  auto pipe2 = ZC_HTTP_TEST_CREATE_2PIPE;

  FakeEntropySource maskGenerator;
  auto server1 = newWebSocket(zc::mv(pipe1.ends[1]), zc::none);
  auto client2 = newWebSocket(zc::mv(pipe2.ends[0]), maskGenerator);
  auto server2 = newWebSocket(zc::mv(pipe2.ends[1]), zc::none);

  auto pumpTask = server1->pumpTo(*client2);
  auto receiveTask = server2->receive();

  // Client sends three bytes of a valid message then disconnects.
  const byte DATA[] = {0x01, 0x06, 'h'};
  pipe1.ends[0]->write(DATA).wait(waitScope);
  pipe1.ends[0] = nullptr;

  // The pump completes successfully, forwarding the disconnect.
  pumpTask.wait(waitScope);

  // The eventual receiver gets a disconnect exception.
  ZC_EXPECT_THROW(DISCONNECTED, receiveTask.wait(waitScope));
}

ZC_TEST("WebSocket abort propagates through pipe") {
  // Pumping one end of a WebSocket pipe into another WebSocket which later becomes aborted will
  // cancel the pump promise with a DISCONNECTED exception.

  ZC_HTTP_TEST_SETUP_IO;
  auto pipe1 = ZC_HTTP_TEST_CREATE_2PIPE;

  auto server = newWebSocket(zc::mv(pipe1.ends[1]), zc::none);
  auto client = newWebSocket(zc::mv(pipe1.ends[0]), zc::none);

  auto wsPipe = newWebSocketPipe();

  auto downstreamPump = wsPipe.ends[0]->pumpTo(*server);
  ZC_EXPECT(!downstreamPump.poll(waitScope));

  client->abort();

  ZC_EXPECT(downstreamPump.poll(waitScope));
  ZC_EXPECT_THROW_RECOVERABLE(DISCONNECTED, downstreamPump.wait(waitScope));
}

ZC_TEST("WebSocket maximum message size") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  WebSocketErrorCatcher errorCatcher;
  FakeEntropySource maskGenerator;
  auto* rawClient = pipe.ends[0].get();
  auto client = newWebSocket(zc::mv(pipe.ends[0]), maskGenerator);
  auto server = newWebSocket(zc::mv(pipe.ends[1]), zc::none, zc::none, errorCatcher);

  size_t maxSize = 100;
  auto biggestAllowedString = zc::strArray(zc::repeat(zc::StringPtr("A"), maxSize), "");
  auto tooBigString = zc::strArray(zc::repeat(zc::StringPtr("B"), maxSize + 1), "");

  auto rawCloseMessage = zc::heapArray<zc::byte>(129);
  auto clientTask =
      client->send(biggestAllowedString)
          .then([&]() { return client->send(tooBigString); })
          .then([&]() {
            return rawClient->tryRead(rawCloseMessage.begin(), 2, rawCloseMessage.size());
          });

  {
    auto message = server->receive(maxSize).wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>().size() == maxSize);
  }

  {
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("too large",
                                        server->receive(maxSize).ignoreResult().wait(waitScope));
    ZC_ASSERT(errorCatcher.errors.size() == 1);
    ZC_ASSERT(errorCatcher.errors[0].statusCode == 1009);
  }

  auto nread = clientTask.wait(waitScope);
  assertContainsWebSocketClose(rawCloseMessage.first(nread), 1009, "too large"_zcc);
}

#if ZC_HAS_ZLIB
ZC_TEST("WebSocket maximum compressed message size") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  WebSocketErrorCatcher errorCatcher;
  FakeEntropySource maskGenerator;
  auto* rawClient = pipe.ends[0].get();
  auto client = newWebSocket(zc::mv(pipe.ends[0]), maskGenerator,
                             CompressionParameters{
                                 .outboundNoContextTakeover = false,
                                 .inboundNoContextTakeover = false,
                                 .outboundMaxWindowBits = 15,
                                 .inboundMaxWindowBits = 15,
                             });
  auto server = newWebSocket(zc::mv(pipe.ends[1]), zc::none,
                             CompressionParameters{
                                 .outboundNoContextTakeover = false,
                                 .inboundNoContextTakeover = false,
                                 .outboundMaxWindowBits = 15,
                                 .inboundMaxWindowBits = 15,
                             },
                             errorCatcher);

  size_t maxSize = 100;
  auto biggestAllowedString = zc::strArray(zc::repeat(zc::StringPtr("A"), maxSize), "");
  auto tooBigString = zc::strArray(zc::repeat(zc::StringPtr("B"), maxSize + 1), "");

  auto rawCloseMessage = zc::heapArray<zc::byte>(129);
  auto clientTask =
      client->send(biggestAllowedString)
          .then([&]() { return client->send(tooBigString); })
          .then([&]() {
            return rawClient->tryRead(rawCloseMessage.begin(), 2, rawCloseMessage.size());
          });

  {
    auto message = server->receive(maxSize).wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>().size() == maxSize);
  }

  {
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("too large",
                                        server->receive(maxSize).ignoreResult().wait(waitScope));
    ZC_ASSERT(errorCatcher.errors.size() == 1);
    ZC_ASSERT(errorCatcher.errors[0].statusCode == 1009);
  }

  auto nread = clientTask.wait(waitScope);
  assertContainsWebSocketClose(rawCloseMessage.first(nread), 1009, "too large"_zcc);
}
#endif

class TestWebSocketService final : public HttpService, private zc::TaskSet::ErrorHandler {
public:
  TestWebSocketService(HttpHeaderTable& headerTable, HttpHeaderId hMyHeader)
      : headerTable(headerTable), hMyHeader(hMyHeader), tasks(*this) {}

  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    ZC_ASSERT(headers.isWebSocket());

    HttpHeaders responseHeaders(headerTable);
    ZC_IF_SOME(h, headers.get(hMyHeader)) {
      responseHeaders.set(hMyHeader, zc::str("respond-", h));
    }

    if (url == "/return-error") {
      response.send(404, "Not Found", responseHeaders, uint64_t(0));
      return zc::READY_NOW;
    } else if (url == "/websocket") {
      auto ws = response.acceptWebSocket(responseHeaders);
      return doWebSocket(*ws, "start-inline").attach(zc::mv(ws));
    } else {
      ZC_FAIL_ASSERT("unexpected path", url);
    }
  }

private:
  HttpHeaderTable& headerTable;
  HttpHeaderId hMyHeader;
  zc::TaskSet tasks;

  void taskFailed(zc::Exception&& exception) override { ZC_LOG(ERROR, exception); }

  static zc::Promise<void> doWebSocket(WebSocket& ws, zc::StringPtr message) {
    auto copy = zc::str(message);
    return ws.send(copy)
        .attach(zc::mv(copy))
        .then([&ws]() { return ws.receive(); })
        .then([&ws](WebSocket::Message&& message) {
          ZC_SWITCH_ONEOF(message) {
            ZC_CASE_ONEOF(str, zc::String) { return doWebSocket(ws, zc::str("reply:", str)); }
            ZC_CASE_ONEOF(data, zc::Array<byte>) {
              return doWebSocket(ws, zc::str("reply:", data));
            }
            ZC_CASE_ONEOF(close, WebSocket::Close) {
              auto reason = zc::str("close-reply:", close.reason);
              return ws.close(close.code + 1, reason).attach(zc::mv(reason));
            }
          }
          ZC_UNREACHABLE;
        });
  }
};

const char WEBSOCKET_REQUEST_HANDSHAKE[] =
    " HTTP/1.1\r\n"
    "Connection: Upgrade\r\n"
    "Upgrade: websocket\r\n"
    "Sec-WebSocket-Key: DCI4TgwiOE4MIjhODCI4Tg==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "My-Header: foo\r\n"
    "\r\n";
const char WEBSOCKET_RESPONSE_HANDSHAKE[] =
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Connection: Upgrade\r\n"
    "Upgrade: websocket\r\n"
    "Sec-WebSocket-Accept: pShtIFKT0s8RYZvnWY/CrjQD8CM=\r\n"
    "My-Header: respond-foo\r\n"
    "\r\n";
#if ZC_HAS_ZLIB
const char WEBSOCKET_COMPRESSION_HANDSHAKE[] =
    " HTTP/1.1\r\n"
    "Connection: Upgrade\r\n"
    "Upgrade: websocket\r\n"
    "Sec-WebSocket-Key: DCI4TgwiOE4MIjhODCI4Tg==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "Sec-WebSocket-Extensions: permessage-deflate; server_no_context_takeover\r\n"
    "\r\n";
const char WEBSOCKET_COMPRESSION_RESPONSE_HANDSHAKE[] =
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Connection: Upgrade\r\n"
    "Upgrade: websocket\r\n"
    "Sec-WebSocket-Accept: pShtIFKT0s8RYZvnWY/CrjQD8CM=\r\n"
    "Sec-WebSocket-Extensions: permessage-deflate; server_no_context_takeover\r\n"
    "\r\n";
const char WEBSOCKET_COMPRESSION_CLIENT_DISCARDS_CTX_HANDSHAKE[] =
    " HTTP/1.1\r\n"
    "Connection: Upgrade\r\n"
    "Upgrade: websocket\r\n"
    "Sec-WebSocket-Key: DCI4TgwiOE4MIjhODCI4Tg==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "Sec-WebSocket-Extensions: permessage-deflate; client_no_context_takeover; "
    "server_no_context_takeover\r\n"
    "\r\n";
const char WEBSOCKET_COMPRESSION_CLIENT_DISCARDS_CTX_RESPONSE_HANDSHAKE[] =
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Connection: Upgrade\r\n"
    "Upgrade: websocket\r\n"
    "Sec-WebSocket-Accept: pShtIFKT0s8RYZvnWY/CrjQD8CM=\r\n"
    "Sec-WebSocket-Extensions: permessage-deflate; client_no_context_takeover; "
    "server_no_context_takeover\r\n"
    "\r\n";
#endif  // ZC_HAS_ZLIB
const char WEBSOCKET_RESPONSE_HANDSHAKE_ERROR[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Length: 0\r\n"
    "My-Header: respond-foo\r\n"
    "\r\n";
const byte WEBSOCKET_FIRST_MESSAGE_INLINE[] = {0x81, 0x0c, 's', 't', 'a', 'r', 't',
                                               '-',  'i',  'n', 'l', 'i', 'n', 'e'};
const byte WEBSOCKET_SEND_MESSAGE[] = {0x81, 0x83, 12, 34, 56, 78, 'b' ^ 12, 'a' ^ 34, 'r' ^ 56};
const byte WEBSOCKET_REPLY_MESSAGE[] = {0x81, 0x09, 'r', 'e', 'p', 'l', 'y', ':', 'b', 'a', 'r'};
#if ZC_HAS_ZLIB
const byte WEBSOCKET_SEND_HI[] = {0x81, 0x82, 12, 34, 56, 78, 'H' ^ 12, 'i' ^ 34};
#endif  // ZC_HAS_ZLIB
const byte WEBSOCKET_SEND_CLOSE[] = {0x88,      0x85,      12,       34,       56,      78,
                                     0x12 ^ 12, 0x34 ^ 34, 'q' ^ 56, 'u' ^ 78, 'x' ^ 12};
const byte WEBSOCKET_REPLY_CLOSE[] = {0x88, 0x11, 0x12, 0x35, 'c', 'l', 'o', 's', 'e', '-',
                                      'r',  'e',  'p',  'l',  'y', ':', 'q', 'u', 'x'};

#if ZC_HAS_ZLIB
const byte WEBSOCKET_FIRST_COMPRESSED_MESSAGE[] = {0xc1, 0x07, 0xf2, 0x48, 0xcd,
                                                   0xc9, 0xc9, 0x07, 0x00};
// See this example: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.3.2
const byte WEBSOCKET_SEND_COMPRESSED_MESSAGE[] = {
    0xc1,      0x87,      12,        34,        56,        78,       0xf2 ^ 12,
    0x48 ^ 34, 0xcd ^ 56, 0xc9 ^ 78, 0xc9 ^ 12, 0x07 ^ 34, 0x00 ^ 56};
const byte WEBSOCKET_SEND_COMPRESSED_MESSAGE_REUSE_CTX[] = {
    0xc1, 0x85, 12, 34, 56, 78, 0xf2 ^ 12, 0x00 ^ 34, 0x11 ^ 56, 0x00 ^ 78, 0x00 ^ 12};
const byte WEBSOCKET_COMPRESSED_HI[] = {0xc1, 0x84,      12,        34,        56,
                                        78,   0xf2 ^ 12, 0xc8 ^ 34, 0x04 ^ 56, 0x00 ^ 78};
// See same compression example, but where `client_no_context_takeover` is used (saves 2 bytes).
const byte WEBSOCKET_DEFLATE_NO_COMPRESSION_MESSAGE[] = {0xc1, 0x0b, 0x00, 0x05, 0x00, 0xfa, 0xff,
                                                         0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x00};
// See this example: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.3.3
// This uses a DEFLATE block with no compression.
const byte WEBSOCKET_BFINAL_SET_MESSAGE[] = {0xc1, 0x08, 0xf3, 0x48, 0xcd,
                                             0xc9, 0xc9, 0x07, 0x00, 0x00};
// See this example: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.3.4
// This uses a DEFLATE block with BFINAL set to 1.
const byte WEBSOCKET_TWO_DEFLATE_BLOCKS_MESSAGE[] = {0xc1, 0x0d, 0xf2, 0x48, 0x05, 0x00, 0x00, 0x00,
                                                     0xff, 0xff, 0xca, 0xc9, 0xc9, 0x07, 0x00};
// See this example: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.3.5
// This uses two DEFLATE blocks in a single message.
const byte WEBSOCKET_EMPTY_COMPRESSED_MESSAGE[] = {0xc1, 0x01, 0x00};
const byte WEBSOCKET_EMPTY_SEND_COMPRESSED_MESSAGE[] = {0xc1, 0x81, 12, 34, 56, 78, 0x00 ^ 12};
const byte WEBSOCKET_SEND_COMPRESSED_HELLO_REUSE_CTX[] = {
    0xc1, 0x85, 12, 34, 56, 78, 0xf2 ^ 12, 0x00 ^ 34, 0x51 ^ 56, 0x00 ^ 78, 0x00 ^ 12};
#endif  // ZC_HAS_ZLIB

template <size_t s>
zc::ArrayPtr<const byte> asBytes(const char (&chars)[s]) {
  return zc::ArrayPtr<const char>(chars, s - 1).asBytes();
}

void testWebSocketClient(zc::WaitScope& waitScope, HttpHeaderTable& headerTable,
                         zc::HttpHeaderId hMyHeader, HttpClient& client) {
  zc::HttpHeaders headers(headerTable);
  headers.set(hMyHeader, "foo");
  auto response = client.openWebSocket("/websocket", headers).wait(waitScope);

  ZC_EXPECT(response.statusCode == 101);
  ZC_EXPECT(response.statusText == "Switching Protocols", response.statusText);
  ZC_EXPECT(ZC_ASSERT_NONNULL(response.headers->get(hMyHeader)) == "respond-foo");
  ZC_ASSERT(response.webSocketOrBody.is<zc::Own<WebSocket>>());
  auto ws = zc::mv(response.webSocketOrBody.get<zc::Own<WebSocket>>());

  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "start-inline");
  }

  ws->send(zc::StringPtr("bar")).wait(waitScope);
  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "reply:bar");
  }

  ws->close(0x1234, "qux").wait(waitScope);
  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<WebSocket::Close>());
    ZC_EXPECT(message.get<WebSocket::Close>().code == 0x1235);
    ZC_EXPECT(message.get<WebSocket::Close>().reason == "close-reply:qux");
  }
}

#if ZC_HAS_ZLIB
void testWebSocketTwoMessageCompression(zc::WaitScope& waitScope, HttpHeaderTable& headerTable,
                                        zc::HttpHeaderId extHeader, zc::StringPtr extensions,
                                        HttpClient& client) {
  // In this test, the server will always use `server_no_context_takeover` (since we can just reuse
  // the message). However, we will modify the client's compressor in different ways to see how the
  // compressed message changes.

  zc::HttpHeaders headers(headerTable);
  headers.set(extHeader, extensions);
  auto response = client.openWebSocket("/websocket", headers).wait(waitScope);

  ZC_EXPECT(response.statusCode == 101);
  ZC_EXPECT(response.statusText == "Switching Protocols", response.statusText);
  ZC_EXPECT(ZC_ASSERT_NONNULL(response.headers->get(extHeader)).startsWith("permessage-deflate"));
  ZC_ASSERT(response.webSocketOrBody.is<zc::Own<WebSocket>>());
  auto ws = zc::mv(response.webSocketOrBody.get<zc::Own<WebSocket>>());

  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "Hello");
  }
  ws->send(zc::StringPtr("Hello")).wait(waitScope);

  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "Hello");
  }
  ws->send(zc::StringPtr("Hello")).wait(waitScope);

  ws->close(0x1234, "qux").wait(waitScope);
  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<WebSocket::Close>());
    ZC_EXPECT(message.get<WebSocket::Close>().code == 0x1235);
    ZC_EXPECT(message.get<WebSocket::Close>().reason == "close-reply:qux");
  }
}
#endif  // ZC_HAS_ZLIB

#if ZC_HAS_ZLIB
void testWebSocketThreeMessageCompression(zc::WaitScope& waitScope, HttpHeaderTable& headerTable,
                                          zc::HttpHeaderId extHeader, zc::StringPtr extensions,
                                          HttpClient& client) {
  // The first message we receive is compressed, and so it our reply.
  // The second message we receive is not compressed, but our response to it is.
  // The third message is the same as the first (from the application code's perspective).

  zc::HttpHeaders headers(headerTable);
  headers.set(extHeader, extensions);
  auto response = client.openWebSocket("/websocket", headers).wait(waitScope);

  ZC_EXPECT(response.statusCode == 101);
  ZC_EXPECT(response.statusText == "Switching Protocols", response.statusText);
  ZC_EXPECT(ZC_ASSERT_NONNULL(response.headers->get(extHeader)).startsWith("permessage-deflate"));
  ZC_ASSERT(response.webSocketOrBody.is<zc::Own<WebSocket>>());
  auto ws = zc::mv(response.webSocketOrBody.get<zc::Own<WebSocket>>());

  // Compressed message.
  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "Hello");
  }
  ws->send(zc::StringPtr("Hello")).wait(waitScope);

  // The message we receive is not compressed, but the one we send is.
  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "Hi");
  }
  ws->send(zc::StringPtr("Hi")).wait(waitScope);

  // Compressed message.
  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "Hello");
  }
  ws->send(zc::StringPtr("Hello")).wait(waitScope);

  ws->close(0x1234, "qux").wait(waitScope);
  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<WebSocket::Close>());
    ZC_EXPECT(message.get<WebSocket::Close>().code == 0x1235);
    ZC_EXPECT(message.get<WebSocket::Close>().reason == "close-reply:qux");
  }
}
#endif  // ZC_HAS_ZLIB

#if ZC_HAS_ZLIB
void testWebSocketEmptyMessageCompression(zc::WaitScope& waitScope, HttpHeaderTable& headerTable,
                                          zc::HttpHeaderId extHeader, zc::StringPtr extensions,
                                          HttpClient& client) {
  // Confirm that we can send empty messages when compression is enabled.

  zc::HttpHeaders headers(headerTable);
  headers.set(extHeader, extensions);
  auto response = client.openWebSocket("/websocket", headers).wait(waitScope);

  ZC_EXPECT(response.statusCode == 101);
  ZC_EXPECT(response.statusText == "Switching Protocols", response.statusText);
  ZC_EXPECT(ZC_ASSERT_NONNULL(response.headers->get(extHeader)).startsWith("permessage-deflate"));
  ZC_ASSERT(response.webSocketOrBody.is<zc::Own<WebSocket>>());
  auto ws = zc::mv(response.webSocketOrBody.get<zc::Own<WebSocket>>());

  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "Hello");
  }
  ws->send(zc::StringPtr("Hello")).wait(waitScope);

  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "");
  }
  ws->send(zc::StringPtr("")).wait(waitScope);

  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<zc::String>());
    ZC_EXPECT(message.get<zc::String>() == "Hello");
  }
  ws->send(zc::StringPtr("Hello")).wait(waitScope);

  ws->close(0x1234, "qux").wait(waitScope);
  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<WebSocket::Close>());
    ZC_EXPECT(message.get<WebSocket::Close>().code == 0x1235);
    ZC_EXPECT(message.get<WebSocket::Close>().reason == "close-reply:qux");
  }
}
#endif  // ZC_HAS_ZLIB

#if ZC_HAS_ZLIB
void testWebSocketOptimizePumpProxy(zc::WaitScope& waitScope, HttpHeaderTable& headerTable,
                                    zc::HttpHeaderId extHeader, zc::StringPtr extensions,
                                    HttpClient& client) {
  // Suppose we are proxying a websocket conversation between a client and a server.
  // This looks something like: CLIENT <--> (proxyServer <==PUMP==> proxyClient) <--> SERVER
  //
  // We want to enable optimizedPumping from the proxy's server (which communicates with the
  // client), to the proxy's client (which communicates with the origin server).
  //
  // For this to work, proxyServer's inbound settings must map to proxyClient's outbound settings
  // (and vice versa). In this case, `ws` is `proxyClient`, so we want to take `ws`'s compression
  // configuration and pass it to `proxyServer` in a way that would allow for optimizedPumping.

  zc::HttpHeaders headers(headerTable);
  headers.set(extHeader, extensions);
  auto response = client.openWebSocket("/websocket", headers).wait(waitScope);

  ZC_EXPECT(response.statusCode == 101);
  ZC_EXPECT(response.statusText == "Switching Protocols", response.statusText);
  ZC_EXPECT(ZC_ASSERT_NONNULL(response.headers->get(extHeader)).startsWith("permessage-deflate"));
  ZC_ASSERT(response.webSocketOrBody.is<zc::Own<WebSocket>>());
  auto ws = zc::mv(response.webSocketOrBody.get<zc::Own<WebSocket>>());

  auto maybeExt = ws->getPreferredExtensions(zc::WebSocket::ExtensionsContext::REQUEST);
  // Should be none since we are asking `ws` (a client) to give us extensions that we can give to
  // another client. Since clients cannot `optimizedPumpTo` each other, we must get null.
  ZC_ASSERT(maybeExt == zc::none);

  maybeExt = ws->getPreferredExtensions(zc::WebSocket::ExtensionsContext::RESPONSE);
  zc::StringPtr extStr = ZC_ASSERT_NONNULL(maybeExt);
  ZC_ASSERT(extStr == "permessage-deflate; server_no_context_takeover");
  // We got back the string the client sent!
  // We could then pass this string as a header to `acceptWebSocket` and ensure the `proxyServer`s
  // inbound settings match the `proxyClient`s outbound settings.

  ws->close(0x1234, "qux").wait(waitScope);
  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<WebSocket::Close>());
    ZC_EXPECT(message.get<WebSocket::Close>().code == 0x1235);
    ZC_EXPECT(message.get<WebSocket::Close>().reason == "close-reply:qux");
  }
}
#endif  // ZC_HAS_ZLIB
#if ZC_HAS_ZLIB
void testWebSocketFourMessageCompression(zc::WaitScope& waitScope, HttpHeaderTable& headerTable,
                                         zc::HttpHeaderId extHeader, zc::StringPtr extensions,
                                         HttpClient& client) {
  // In this test, the server will always use `server_no_context_takeover` (since we can just reuse
  // the message). We will receive three messages.

  zc::HttpHeaders headers(headerTable);
  headers.set(extHeader, extensions);
  auto response = client.openWebSocket("/websocket", headers).wait(waitScope);

  ZC_EXPECT(response.statusCode == 101);
  ZC_EXPECT(response.statusText == "Switching Protocols", response.statusText);
  ZC_EXPECT(ZC_ASSERT_NONNULL(response.headers->get(extHeader)).startsWith("permessage-deflate"));
  ZC_ASSERT(response.webSocketOrBody.is<zc::Own<WebSocket>>());
  auto ws = zc::mv(response.webSocketOrBody.get<zc::Own<WebSocket>>());

  for (size_t i = 0; i < 4; i++) {
    {
      auto message = ws->receive().wait(waitScope);
      ZC_ASSERT(message.is<zc::String>());
      ZC_EXPECT(message.get<zc::String>() == "Hello");
    }
  }

  ws->close(0x1234, "qux").wait(waitScope);
  {
    auto message = ws->receive().wait(waitScope);
    ZC_ASSERT(message.is<WebSocket::Close>());
    ZC_EXPECT(message.get<WebSocket::Close>().code == 0x1235);
    ZC_EXPECT(message.get<WebSocket::Close>().reason == "close-reply:qux");
  }
}
#endif  // ZC_HAS_ZLIB

inline zc::Promise<void> writeA(zc::AsyncOutputStream& out, zc::ArrayPtr<const byte> data) {
  return out.write(data);
}

ZC_TEST("HttpClient WebSocket handshake") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto request = zc::str("GET /websocket", WEBSOCKET_REQUEST_HANDSHAKE);

  auto serverTask =
      expectRead(*pipe.ends[1], request)
          .then([&]() { return writeA(*pipe.ends[1], asBytes(WEBSOCKET_RESPONSE_HANDSHAKE)); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_MESSAGE_INLINE); })
          .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_MESSAGE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_MESSAGE); })
          .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_CLOSE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
          .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  testWebSocketClient(waitScope, *headerTable, hMyHeader, *client);

  serverTask.wait(waitScope);
}

ZC_TEST("WebSocket Compression String Parsing (splitNext)") {
  // Test `splitNext()`.
  // We want to assert that:
  // If a delimiter is found:
  // - `input` is updated to point to the rest of the string after the delimiter.
  // - The text before the delimiter is returned.
  // If no delimiter is found:
  // - `input` is updated to an empty string.
  // - The text that had been in `input` is returned.

  const auto s = "permessage-deflate;   client_max_window_bits=10;server_no_context_takeover"_zc;

  const auto expectedPartOne = "permessage-deflate"_zc;
  const auto expectedRemainingOne = "client_max_window_bits=10;server_no_context_takeover"_zc;

  auto cursor = s.asArray();
  auto actual = _::splitNext(cursor, ';');
  ZC_ASSERT(actual == expectedPartOne);

  _::stripLeadingAndTrailingSpace(cursor);
  ZC_ASSERT(cursor == expectedRemainingOne.asArray());

  const auto expectedPartTwo = "client_max_window_bits=10"_zc;
  const auto expectedRemainingTwo = "server_no_context_takeover"_zc;

  actual = _::splitNext(cursor, ';');
  ZC_ASSERT(actual == expectedPartTwo);
  ZC_ASSERT(cursor == expectedRemainingTwo);

  const auto expectedPartThree = "server_no_context_takeover"_zc;
  const auto expectedRemainingThree = ""_zc;
  actual = _::splitNext(cursor, ';');
  ZC_ASSERT(actual == expectedPartThree);
  ZC_ASSERT(cursor == expectedRemainingThree);
}

ZC_TEST("WebSocket Compression String Parsing (splitParts)") {
  // Test `splitParts()`.
  // We want to assert that we:
  //  1. Correctly split by the delimiter.
  //  2. Strip whitespace before/after the extracted part.
  const auto permitted = "permessage-deflate"_zc;

  const auto s =
      "permessage-deflate; client_max_window_bits=10;server_no_context_takeover,    "
      "    permessage-deflate;  ;   ,"  // strips leading whitespace
      "permessage-deflate"_zc;

  // These are the expected values.
  const auto extOne = "permessage-deflate; client_max_window_bits=10;server_no_context_takeover"_zc;
  const auto extTwo = "permessage-deflate;  ;"_zc;
  const auto extThree = "permessage-deflate"_zc;

  auto actualExtensions = zc::_::splitParts(s, ',');
  ZC_ASSERT(actualExtensions.size() == 3);
  ZC_ASSERT(actualExtensions[0] == extOne);
  ZC_ASSERT(actualExtensions[1] == extTwo);
  ZC_ASSERT(actualExtensions[2] == extThree);
  // Splitting by ',' was fine, now let's try splitting the parameters (split by ';').

  const auto paramOne = "client_max_window_bits=10"_zc;
  const auto paramTwo = "server_no_context_takeover"_zc;

  auto actualParamsFirstExt = zc::_::splitParts(actualExtensions[0], ';');
  ZC_ASSERT(actualParamsFirstExt.size() == 3);
  ZC_ASSERT(actualParamsFirstExt[0] == permitted);
  ZC_ASSERT(actualParamsFirstExt[1] == paramOne);
  ZC_ASSERT(actualParamsFirstExt[2] == paramTwo);

  auto actualParamsSecondExt = zc::_::splitParts(actualExtensions[1], ';');
  ZC_ASSERT(actualParamsSecondExt.size() == 2);
  ZC_ASSERT(actualParamsSecondExt[0] == permitted);
  ZC_ASSERT(actualParamsSecondExt[1] == ""_zc);  // Note that the whitespace was stripped.

  auto actualParamsThirdExt = zc::_::splitParts(actualExtensions[2], ';');
  // No parameters supplied in the third offer. We expect to only see the extension name.
  ZC_ASSERT(actualParamsThirdExt.size() == 1);
  ZC_ASSERT(actualParamsThirdExt[0] == permitted);
}

ZC_TEST("WebSocket Compression String Parsing (toKeysAndVals)") {
  // If an "=" is found, everything before the "=" goes into the `Key` and everything after goes
  // into the `Value`. Otherwise, everything goes into the `Key` and the `Value` remains null.
  const auto cleanParameters =
      "client_no_context_takeover; client_max_window_bits; "
      "server_max_window_bits=10"_zc;
  auto parts = _::splitParts(cleanParameters, ';');
  auto keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  ZC_ASSERT(keysMaybeValues.size() == 3);

  auto firstKey = "client_no_context_takeover"_zc;
  ZC_ASSERT(keysMaybeValues[0].key == firstKey.asArray());
  ZC_ASSERT(keysMaybeValues[0].val == zc::none);

  auto secondKey = "client_max_window_bits"_zc;
  ZC_ASSERT(keysMaybeValues[1].key == secondKey.asArray());
  ZC_ASSERT(keysMaybeValues[1].val == zc::none);

  auto thirdKey = "server_max_window_bits"_zc;
  auto thirdVal = "10"_zc;
  ZC_ASSERT(keysMaybeValues[2].key == thirdKey.asArray());
  ZC_ASSERT(keysMaybeValues[2].val == thirdVal.asArray());

  const auto weirdParameters = "= 14 ; client_max_window_bits= ; server_max_window_bits =hello"_zc;
  // This is weird because:
  //  1. Parameter 1 has no key.
  //  2. Parameter 2 has an "=" but no subsequent value.
  //  3. Parameter 3 has an "=" with an invalid value.
  // That said, we don't mind if the parameters are weird when calling this function. The point
  // is to create KeyMaybeVal pairs and process them later.

  parts = _::splitParts(weirdParameters, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  ZC_ASSERT(keysMaybeValues.size() == 3);

  firstKey = ""_zc;
  auto firstVal = "14"_zc;
  ZC_ASSERT(keysMaybeValues[0].key == firstKey.asArray());
  ZC_ASSERT(keysMaybeValues[0].val == firstVal.asArray());

  secondKey = "client_max_window_bits"_zc;
  auto secondVal = ""_zc;
  ZC_ASSERT(keysMaybeValues[1].key == secondKey.asArray());
  ZC_ASSERT(keysMaybeValues[1].val == secondVal.asArray());

  thirdKey = "server_max_window_bits"_zc;
  thirdVal = "hello"_zc;
  ZC_ASSERT(keysMaybeValues[2].key == thirdKey.asArray());
  ZC_ASSERT(keysMaybeValues[2].val == thirdVal.asArray());
}

ZC_TEST("WebSocket Compression String Parsing (populateUnverifiedConfig)") {
  // First we'll cover cases where the `UnverifiedConfig` is successfully constructed,
  // which indicates the offer was structured in a parseable way. Next, we'll cover cases where the
  // offer is structured incorrectly.
  const auto cleanParameters =
      "client_no_context_takeover; client_max_window_bits; "
      "server_max_window_bits=10"_zc;
  auto parts = _::splitParts(cleanParameters, ';');
  auto keysMaybeValues = _::toKeysAndVals(parts.asPtr());

  auto unverified = _::populateUnverifiedConfig(keysMaybeValues);
  auto config = ZC_ASSERT_NONNULL(unverified);
  ZC_ASSERT(config.clientNoContextTakeover == true);
  ZC_ASSERT(config.serverNoContextTakeover == false);

  auto clientBits = ZC_ASSERT_NONNULL(config.clientMaxWindowBits);
  ZC_ASSERT(clientBits == ""_zc);
  auto serverBits = ZC_ASSERT_NONNULL(config.serverMaxWindowBits);
  ZC_ASSERT(serverBits == "10"_zc);
  // Valid config can be populated successfully.

  const auto weirdButValidParameters =
      "client_no_context_takeover; client_max_window_bits; "
      "server_max_window_bits=this_should_be_a_number"_zc;
  parts = _::splitParts(weirdButValidParameters, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());

  unverified = _::populateUnverifiedConfig(keysMaybeValues);
  config = ZC_ASSERT_NONNULL(unverified);
  ZC_ASSERT(config.clientNoContextTakeover == true);
  ZC_ASSERT(config.serverNoContextTakeover == false);

  clientBits = ZC_ASSERT_NONNULL(config.clientMaxWindowBits);
  ZC_ASSERT(clientBits == ""_zc);
  serverBits = ZC_ASSERT_NONNULL(config.serverMaxWindowBits);
  ZC_ASSERT(serverBits == "this_should_be_a_number"_zc);
  // Note that while the value associated with `server_max_window_bits` is not a number,
  // `populateUnverifiedConfig` succeeds because the parameter[=value] is generally structured
  // correctly.

  // --- HANDLE INCORRECTLY STRUCTURED OFFERS ---
  auto invalidKey = "somethingKey; client_max_window_bits;"_zc;
  parts = _::splitParts(invalidKey, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  ZC_ASSERT(_::populateUnverifiedConfig(keysMaybeValues) == zc::none);
  // Fail to populate due to invalid key name

  auto invalidKeyTwo = "client_max_window_bitsJUNK; server_no_context_takeover"_zc;
  parts = _::splitParts(invalidKeyTwo, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  ZC_ASSERT(_::populateUnverifiedConfig(keysMaybeValues) == zc::none);
  // Fail to populate due to invalid key name (invalid characters after valid parameter name).

  auto repeatedKey = "client_no_context_takeover; client_no_context_takeover"_zc;
  parts = _::splitParts(repeatedKey, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  ZC_ASSERT(_::populateUnverifiedConfig(keysMaybeValues) == zc::none);
  // Fail to populate due to repeated key name.

  auto unexpectedValue = "client_no_context_takeover="_zc;
  parts = _::splitParts(unexpectedValue, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  ZC_ASSERT(_::populateUnverifiedConfig(keysMaybeValues) == zc::none);
  // Fail to populate due to value in `x_no_context_takeover` parameter (unexpected value).

  auto unexpectedValueTwo = "client_no_context_takeover=   "_zc;
  parts = _::splitParts(unexpectedValueTwo, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  ZC_ASSERT(_::populateUnverifiedConfig(keysMaybeValues) == zc::none);
  // Fail to populate due to value in `x_no_context_takeover` parameter.

  auto emptyValue = "client_max_window_bits="_zc;
  parts = _::splitParts(emptyValue, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  ZC_ASSERT(_::populateUnverifiedConfig(keysMaybeValues) == zc::none);
  // Fail to populate due to empty value in `x_max_window_bits` parameter.
  // "Empty" in this case means an "=" was provided, but no subsequent value was provided.

  auto emptyValueTwo = "client_max_window_bits=   "_zc;
  parts = _::splitParts(emptyValueTwo, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  ZC_ASSERT(_::populateUnverifiedConfig(keysMaybeValues) == zc::none);
  // Fail to populate due to empty value in `x_max_window_bits` parameter.
  // "Empty" in this case means an "=" was provided, but no subsequent value was provided.
}

ZC_TEST("WebSocket Compression String Parsing (validateCompressionConfig)") {
  // We've tested `toKeysAndVals()` and `populateUnverifiedConfig()`, so we only need to test
  // correctly structured offers/agreements here.
  const auto cleanParameters =
      "client_no_context_takeover; client_max_window_bits; "
      "server_max_window_bits=10"_zc;
  auto parts = _::splitParts(cleanParameters, ';');
  auto keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  auto maybeUnverified = _::populateUnverifiedConfig(keysMaybeValues);
  auto unverified = ZC_ASSERT_NONNULL(maybeUnverified);
  auto maybeValid = _::validateCompressionConfig(zc::mv(unverified), false);  // Validate as Server.
  auto valid = ZC_ASSERT_NONNULL(maybeValid);
  ZC_ASSERT(valid.inboundNoContextTakeover == true);
  ZC_ASSERT(valid.outboundNoContextTakeover == false);
  auto inboundBits = ZC_ASSERT_NONNULL(valid.inboundMaxWindowBits);
  ZC_ASSERT(inboundBits == 15);  // `client_max_window_bits` can be empty in an offer.
  auto outboundBits = ZC_ASSERT_NONNULL(valid.outboundMaxWindowBits);
  ZC_ASSERT(outboundBits == 10);
  // Valid config successfully constructed.

  const auto correctStructureButInvalid =
      "client_no_context_takeover; client_max_window_bits; "
      "server_max_window_bits=this_should_be_a_number"_zc;
  parts = _::splitParts(correctStructureButInvalid, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());

  maybeUnverified = _::populateUnverifiedConfig(keysMaybeValues);
  unverified = ZC_ASSERT_NONNULL(maybeUnverified);
  maybeValid = _::validateCompressionConfig(zc::mv(unverified), false);  // Validate as Server.
  ZC_ASSERT(maybeValid == zc::none);
  // The config "looks" correct, but the `server_max_window_bits` parameter has an invalid value.

  const auto invalidRange = "client_max_window_bits; server_max_window_bits=18;"_zc;
  // `server_max_window_bits` is out of range, decline.
  parts = _::splitParts(invalidRange, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  maybeUnverified = _::populateUnverifiedConfig(keysMaybeValues);
  maybeValid = _::validateCompressionConfig(zc::mv(ZC_REQUIRE_NONNULL(maybeUnverified)), false);
  ZC_ASSERT(maybeValid == zc::none);

  const auto invalidRangeTwo = "client_max_window_bits=4"_zc;
  // `server_max_window_bits` is out of range, decline.
  parts = _::splitParts(invalidRangeTwo, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  maybeUnverified = _::populateUnverifiedConfig(keysMaybeValues);
  maybeValid = _::validateCompressionConfig(zc::mv(ZC_REQUIRE_NONNULL(maybeUnverified)), false);
  ZC_ASSERT(maybeValid == zc::none);

  const auto invalidRequest = "server_max_window_bits"_zc;
  // `sever_max_window_bits` must have a value in a request AND a response.
  parts = _::splitParts(invalidRequest, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  maybeUnverified = _::populateUnverifiedConfig(keysMaybeValues);
  maybeValid = _::validateCompressionConfig(zc::mv(ZC_REQUIRE_NONNULL(maybeUnverified)), false);
  ZC_ASSERT(maybeValid == zc::none);

  const auto invalidResponse = "client_max_window_bits"_zc;
  // `client_max_window_bits` must have a value in a response.
  parts = _::splitParts(invalidResponse, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  maybeUnverified = _::populateUnverifiedConfig(keysMaybeValues);
  maybeValid = _::validateCompressionConfig(zc::mv(ZC_REQUIRE_NONNULL(maybeUnverified)), true);
  ZC_ASSERT(maybeValid == zc::none);
}

ZC_TEST("WebSocket Compression String Parsing (findValidExtensionOffers)") {
  // Test that we can extract only the valid extensions from a string of offers.
  constexpr auto extensions =
      "permessage-deflate; "  // Valid offer.
      "client_no_context_takeover; "
      "client_max_window_bits; "
      "server_max_window_bits=10, "
      "permessage-deflate; "  // Another valid offer.
      "client_no_context_takeover; "
      "client_max_window_bits, "
      "permessage-invalid; "  // Invalid ext name.
      "client_no_context_takeover, "
      "permessage-deflate; "  // Invalid parameter.
      "invalid_parameter; "
      "client_max_window_bits; "
      "server_max_window_bits=10, "
      "permessage-deflate; "  // Invalid parameter value.
      "server_max_window_bits=should_be_a_number, "
      "permessage-deflate; "  // Unexpected parameter value.
      "client_max_window_bits=true, "
      "permessage-deflate; "  // Missing expected parameter value.
      "server_max_window_bits, "
      "permessage-deflate; "  // Invalid parameter value (too high).
      "client_max_window_bits=99, "
      "permessage-deflate; "  // Invalid parameter value (too low).
      "client_max_window_bits=4, "
      "permessage-deflate; "  // Invalid parameter (repeated).
      "client_max_window_bits; "
      "client_max_window_bits, "
      "permessage-deflate"_zc;  // Valid offer (no parameters).

  auto validOffers = _::findValidExtensionOffers(extensions);
  ZC_ASSERT(validOffers.size() == 3);
  ZC_ASSERT(validOffers[0].outboundNoContextTakeover == true);
  ZC_ASSERT(validOffers[0].inboundNoContextTakeover == false);
  ZC_ASSERT(validOffers[0].outboundMaxWindowBits == 15);
  ZC_ASSERT(validOffers[0].inboundMaxWindowBits == 10);

  ZC_ASSERT(validOffers[1].outboundNoContextTakeover == true);
  ZC_ASSERT(validOffers[1].inboundNoContextTakeover == false);
  ZC_ASSERT(validOffers[1].outboundMaxWindowBits == 15);
  ZC_ASSERT(validOffers[1].inboundMaxWindowBits == zc::none);

  ZC_ASSERT(validOffers[2].outboundNoContextTakeover == false);
  ZC_ASSERT(validOffers[2].inboundNoContextTakeover == false);
  ZC_ASSERT(validOffers[2].outboundMaxWindowBits == zc::none);
  ZC_ASSERT(validOffers[2].inboundMaxWindowBits == zc::none);
}

ZC_TEST("WebSocket Compression String Parsing (generateExtensionRequest)") {
  // Test that we can extract only the valid extensions from a string of offers.
  constexpr auto extensions =
      "permessage-deflate; "
      "client_no_context_takeover; "
      "server_max_window_bits=10; "
      "client_max_window_bits, "
      "permessage-deflate; "
      "client_no_context_takeover; "
      "client_max_window_bits, "
      "permessage-deflate"_zc;
  constexpr auto EXPECTED =
      "permessage-deflate; "
      "client_no_context_takeover; "
      "client_max_window_bits=15; "
      "server_max_window_bits=10, "
      "permessage-deflate; "
      "client_no_context_takeover; "
      "client_max_window_bits=15, "
      "permessage-deflate"_zc;
  auto validOffers = _::findValidExtensionOffers(extensions);
  auto extensionRequest = _::generateExtensionRequest(validOffers);
  ZC_ASSERT(extensionRequest == EXPECTED);
}

ZC_TEST("WebSocket Compression String Parsing (tryParseExtensionOffers)") {
  // Test that we can accept a valid offer from string of offers.
  constexpr auto extensions =
      "permessage-invalid; "  // Invalid ext name.
      "client_no_context_takeover, "
      "permessage-deflate; "  // Invalid parameter.
      "invalid_parameter; "
      "client_max_window_bits; "
      "server_max_window_bits=10, "
      "permessage-deflate; "  // Invalid parameter value.
      "server_max_window_bits=should_be_a_number, "
      "permessage-deflate; "  // Unexpected parameter value.
      "client_max_window_bits=true, "
      "permessage-deflate; "  // Missing expected parameter value.
      "server_max_window_bits, "
      "permessage-deflate; "  // Invalid parameter value (too high).
      "client_max_window_bits=99, "
      "permessage-deflate; "  // Invalid parameter value (too low).
      "client_max_window_bits=4, "
      "permessage-deflate; "  // Invalid parameter (repeated).
      "client_max_window_bits; "
      "client_max_window_bits, "
      "permessage-deflate; "  // Valid offer.
      "client_no_context_takeover; "
      "client_max_window_bits; "
      "server_max_window_bits=10, "
      "permessage-deflate; "  // Another valid offer.
      "client_no_context_takeover; "
      "client_max_window_bits, "
      "permessage-deflate"_zc;  // Valid offer (no parameters).

  auto maybeAccepted = _::tryParseExtensionOffers(extensions);
  auto accepted = ZC_ASSERT_NONNULL(maybeAccepted);
  ZC_ASSERT(accepted.outboundNoContextTakeover == false);
  ZC_ASSERT(accepted.inboundNoContextTakeover == true);
  ZC_ASSERT(accepted.outboundMaxWindowBits == 10);
  ZC_ASSERT(accepted.inboundMaxWindowBits == 15);

  // Try the second valid offer from the big list above.
  auto offerTwo = "permessage-deflate; client_no_context_takeover; client_max_window_bits"_zc;
  maybeAccepted = _::tryParseExtensionOffers(offerTwo);
  accepted = ZC_ASSERT_NONNULL(maybeAccepted);
  ZC_ASSERT(accepted.outboundNoContextTakeover == false);
  ZC_ASSERT(accepted.inboundNoContextTakeover == true);
  ZC_ASSERT(accepted.outboundMaxWindowBits == zc::none);
  ZC_ASSERT(accepted.inboundMaxWindowBits == 15);

  auto offerThree = "permessage-deflate"_zc;  // The third valid offer.
  maybeAccepted = _::tryParseExtensionOffers(offerThree);
  accepted = ZC_ASSERT_NONNULL(maybeAccepted);
  ZC_ASSERT(accepted.outboundNoContextTakeover == false);
  ZC_ASSERT(accepted.inboundNoContextTakeover == false);
  ZC_ASSERT(accepted.outboundMaxWindowBits == zc::none);
  ZC_ASSERT(accepted.inboundMaxWindowBits == zc::none);

  auto invalid = "invalid"_zc;  // Any of the invalid offers we saw above would return NULL.
  maybeAccepted = _::tryParseExtensionOffers(invalid);
  ZC_ASSERT(maybeAccepted == zc::none);
}

ZC_TEST("WebSocket Compression String Parsing (tryParseAllExtensionOffers)") {
  // We want to test the following:
  //  1. We reject all if we don't find an offer we can accept.
  //  2. We accept one after iterating over offers that we have to reject.
  //  3. We accept an offer with a `server_max_window_bits` parameter if the manual config allows
  //     it, and choose the smaller "number of bits" (from clients request).
  //  4. We accept an offer with a `server_no_context_takeover` parameter if the manual config
  //     allows it, and choose the smaller "number of bits" (from manual config) from
  //     `server_max_window_bits`.
  constexpr auto serverOnly =
      "permessage-deflate; "
      "client_no_context_takeover; "
      "server_max_window_bits = 14; "
      "server_no_context_takeover, "
      "permessage-deflate; "
      "client_no_context_takeover; "
      "server_no_context_takeover, "
      "permessage-deflate; "
      "client_no_context_takeover; "
      "server_max_window_bits = 14"_zc;

  constexpr auto acceptLast =
      "permessage-deflate; "
      "client_no_context_takeover; "
      "server_max_window_bits = 14; "
      "server_no_context_takeover, "
      "permessage-deflate; "
      "client_no_context_takeover; "
      "server_no_context_takeover, "
      "permessage-deflate; "
      "client_no_context_takeover; "
      "server_max_window_bits = 14, "
      "permessage-deflate; "  // accept this
      "client_no_context_takeover"_zc;

  const auto defaultConfig = CompressionParameters();
  // Our default config is equivalent to `permessage-deflate` with no parameters.

  auto maybeAccepted = _::tryParseAllExtensionOffers(serverOnly, defaultConfig);
  ZC_ASSERT(maybeAccepted == zc::none);
  // Asserts that we rejected all the offers with `server_x` parameters.

  maybeAccepted = _::tryParseAllExtensionOffers(acceptLast, defaultConfig);
  auto accepted = ZC_ASSERT_NONNULL(maybeAccepted);
  ZC_ASSERT(accepted.outboundNoContextTakeover == false);
  ZC_ASSERT(accepted.inboundNoContextTakeover == false);
  ZC_ASSERT(accepted.outboundMaxWindowBits == zc::none);
  ZC_ASSERT(accepted.inboundMaxWindowBits == zc::none);
  // Asserts that we accepted the only offer that did not have a `server_x` parameter.

  const auto allowServerBits = CompressionParameters{false, false,
                                                     15,  // server_max_window_bits = 15
                                                     zc::none};
  maybeAccepted = _::tryParseAllExtensionOffers(serverOnly, allowServerBits);
  accepted = ZC_ASSERT_NONNULL(maybeAccepted);
  ZC_ASSERT(accepted.outboundNoContextTakeover == false);
  ZC_ASSERT(accepted.inboundNoContextTakeover == false);
  ZC_ASSERT(accepted.outboundMaxWindowBits == 14);  // Note that we chose the lower of (14, 15).
  ZC_ASSERT(accepted.inboundMaxWindowBits == zc::none);
  // Asserts that we accepted an offer that allowed for `server_max_window_bits` AND we chose the
  // lower number of bits (in this case, the clients offer of 14).

  const auto allowServerTakeoverAndBits =
      CompressionParameters{true,  // server_no_context_takeover = true
                            false,
                            13,  // server_max_window_bits = 13
                            zc::none};

  maybeAccepted = _::tryParseAllExtensionOffers(serverOnly, allowServerTakeoverAndBits);
  accepted = ZC_ASSERT_NONNULL(maybeAccepted);
  ZC_ASSERT(accepted.outboundNoContextTakeover == true);
  ZC_ASSERT(accepted.inboundNoContextTakeover == false);
  ZC_ASSERT(accepted.outboundMaxWindowBits == 13);  // Note that we chose the lower of (14, 15).
  ZC_ASSERT(accepted.inboundMaxWindowBits == zc::none);
  // Asserts that we accepted an offer that allowed for `server_no_context_takeover` AND we chose
  // the lower number of bits (in this case, the manual config's choice of 13).
}

ZC_TEST("WebSocket Compression String Parsing (generateExtensionResponse)") {
  // Test that we can extract only the valid extensions from a string of offers.
  constexpr auto extensions =
      "permessage-deflate; "
      "client_no_context_takeover; "
      "server_max_window_bits=10; "
      "client_max_window_bits, "
      "permessage-deflate; "
      "client_no_context_takeover; "
      "client_max_window_bits, "
      "permessage-deflate"_zc;
  constexpr auto EXPECTED =
      "permessage-deflate; "
      "client_no_context_takeover; "
      "client_max_window_bits=15; "
      "server_max_window_bits=10"_zc;
  auto accepted = _::tryParseExtensionOffers(extensions);
  auto extensionResponse = _::generateExtensionResponse(ZC_ASSERT_NONNULL(accepted));
  ZC_ASSERT(extensionResponse == EXPECTED);
}

ZC_TEST("WebSocket Compression String Parsing (tryParseExtensionAgreement)") {
  constexpr auto didNotOffer =
      "Server failed WebSocket handshake: "
      "added Sec-WebSocket-Extensions when client did not offer any."_zc;
  constexpr auto tooMany =
      "Server failed WebSocket handshake: "
      "expected exactly one extension (permessage-deflate) but received more than one."_zc;
  constexpr auto badExt =
      "Server failed WebSocket handshake: "
      "response included a Sec-WebSocket-Extensions value that was not permessage-deflate."_zc;
  constexpr auto badVal =
      "Server failed WebSocket handshake: "
      "the Sec-WebSocket-Extensions header in the Response included an invalid value."_zc;

  constexpr auto tooManyExtensions =
      "permessage-deflate; client_no_context_takeover; "
      "client_max_window_bits; server_max_window_bits=10, "
      "permessage-deflate; client_no_context_takeover; client_max_window_bits;"_zc;

  auto maybeAccepted = _::tryParseExtensionAgreement(zc::none, tooManyExtensions);
  ZC_ASSERT(ZC_ASSERT_NONNULL(maybeAccepted.tryGet<zc::Exception>()).getDescription() ==
            didNotOffer);

  Maybe<CompressionParameters> defaultConfig = CompressionParameters{};
  maybeAccepted = _::tryParseExtensionAgreement(defaultConfig, tooManyExtensions);
  ZC_ASSERT(ZC_ASSERT_NONNULL(maybeAccepted.tryGet<zc::Exception>()).getDescription() == tooMany);

  constexpr auto invalidExt =
      "permessage-invalid; "
      "client_no_context_takeover; "
      "client_max_window_bits; "
      "server_max_window_bits=10;";
  maybeAccepted = _::tryParseExtensionAgreement(defaultConfig, invalidExt);
  ZC_ASSERT(ZC_ASSERT_NONNULL(maybeAccepted.tryGet<zc::Exception>()).getDescription() == badExt);

  constexpr auto invalidVal =
      "permessage-deflate; "
      "client_no_context_takeover; "
      "client_max_window_bits; "
      "server_max_window_bits=100;";
  maybeAccepted = _::tryParseExtensionAgreement(defaultConfig, invalidVal);
  ZC_ASSERT(ZC_ASSERT_NONNULL(maybeAccepted.tryGet<zc::Exception>()).getDescription() == badVal);

  constexpr auto missingVal =
      "permessage-deflate; "
      "client_no_context_takeover; "
      "client_max_window_bits; "  // This must have a value in a Response!
      "server_max_window_bits=10;";
  maybeAccepted = _::tryParseExtensionAgreement(defaultConfig, missingVal);
  ZC_ASSERT(ZC_ASSERT_NONNULL(maybeAccepted.tryGet<zc::Exception>()).getDescription() == badVal);

  constexpr auto valid =
      "permessage-deflate; client_no_context_takeover; "
      "client_max_window_bits=15; server_max_window_bits=10"_zc;
  maybeAccepted = _::tryParseExtensionAgreement(defaultConfig, valid);
  auto config = ZC_ASSERT_NONNULL(maybeAccepted.tryGet<CompressionParameters>());
  ZC_ASSERT(config.outboundNoContextTakeover == true);
  ZC_ASSERT(config.inboundNoContextTakeover == false);
  ZC_ASSERT(config.outboundMaxWindowBits == 15);
  ZC_ASSERT(config.inboundMaxWindowBits == 10);

  auto client = CompressionParameters{true, false, 15, 10};
  // If the server ignores our `client_no_context_takeover` parameter, we (the client) still use it.
  constexpr auto serverIgnores =
      "permessage-deflate; client_max_window_bits=15; "
      "server_max_window_bits=10"_zc;
  maybeAccepted = _::tryParseExtensionAgreement(client, serverIgnores);
  config = ZC_ASSERT_NONNULL(maybeAccepted.tryGet<CompressionParameters>());
  ZC_ASSERT(config.outboundNoContextTakeover ==
            true);  // Note that this is missing in the response.
  ZC_ASSERT(config.inboundNoContextTakeover == false);
  ZC_ASSERT(config.outboundMaxWindowBits == 15);
  ZC_ASSERT(config.inboundMaxWindowBits == 10);
}

#if ZC_HAS_ZLIB
ZC_TEST("HttpClient WebSocket Empty Message Compression") {
  // We'll try to send and receive "Hello", then "", followed by "Hello" again.
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto request = zc::str("GET /websocket", WEBSOCKET_COMPRESSION_HANDSHAKE);

  auto serverTask =
      expectRead(*pipe.ends[1], request)
          .then([&]() {
            return writeA(*pipe.ends[1], asBytes(WEBSOCKET_COMPRESSION_RESPONSE_HANDSHAKE));
          })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_COMPRESSED_MESSAGE); })
          .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_EMPTY_COMPRESSED_MESSAGE); })
          .then(
              [&]() { return expectRead(*pipe.ends[1], WEBSOCKET_EMPTY_SEND_COMPRESSED_MESSAGE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_COMPRESSED_MESSAGE); })
          .then([&]() {
            return expectRead(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE_REUSE_CTX);
          })
          .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_CLOSE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
          .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId extHeader = tableBuilder.add("Sec-WebSocket-Extensions");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.webSocketCompressionMode = HttpClientSettings::MANUAL_COMPRESSION;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  constexpr auto extensions = "permessage-deflate; server_no_context_takeover"_zc;
  testWebSocketEmptyMessageCompression(waitScope, *headerTable, extHeader, extensions, *client);

  serverTask.wait(waitScope);
}
#endif  // ZC_HAS_ZLIB

#if ZC_HAS_ZLIB
ZC_TEST("HttpClient WebSocket Default Compression") {
  // We'll try to send and receive "Hello" twice. The second time we receive "Hello", the compressed
  // message will be smaller as a result of the client reusing the lookback window.
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto request = zc::str("GET /websocket", WEBSOCKET_COMPRESSION_HANDSHAKE);

  auto serverTask =
      expectRead(*pipe.ends[1], request)
          .then([&]() {
            return writeA(*pipe.ends[1], asBytes(WEBSOCKET_COMPRESSION_RESPONSE_HANDSHAKE));
          })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_COMPRESSED_MESSAGE); })
          .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_COMPRESSED_MESSAGE); })
          .then([&]() {
            return expectRead(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE_REUSE_CTX);
          })
          .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_CLOSE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
          .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId extHeader = tableBuilder.add("Sec-WebSocket-Extensions");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.webSocketCompressionMode = HttpClientSettings::MANUAL_COMPRESSION;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  constexpr auto extensions = "permessage-deflate; server_no_context_takeover"_zc;
  testWebSocketTwoMessageCompression(waitScope, *headerTable, extHeader, extensions, *client);

  serverTask.wait(waitScope);
}
#endif  // ZC_HAS_ZLIB

#if ZC_HAS_ZLIB
ZC_TEST("HttpClient WebSocket negotiate compression and interleave it") {
  // We will tell the server we
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto request = zc::str("GET /websocket", WEBSOCKET_COMPRESSION_HANDSHAKE);

  auto serverTask =
      expectRead(*pipe.ends[1], request)
          .then([&]() {
            return writeA(*pipe.ends[1], asBytes(WEBSOCKET_COMPRESSION_RESPONSE_HANDSHAKE));
          })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_COMPRESSED_MESSAGE); })
          .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE); })
          // Server sends uncompressed "Hi" -- client responds with compressed "Hi".
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_SEND_HI); })
          .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_COMPRESSED_HI); })
          // Back to compressed messages.
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_COMPRESSED_MESSAGE); })
          .then([&]() {
            return expectRead(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_HELLO_REUSE_CTX);
          })
          .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_CLOSE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
          .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId extHeader = tableBuilder.add("Sec-WebSocket-Extensions");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.webSocketCompressionMode = HttpClientSettings::MANUAL_COMPRESSION;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  constexpr auto extensions = "permessage-deflate; server_no_context_takeover"_zc;
  testWebSocketThreeMessageCompression(waitScope, *headerTable, extHeader, extensions, *client);

  serverTask.wait(waitScope);
}
#endif  // ZC_HAS_ZLIB

#if ZC_HAS_ZLIB
ZC_TEST("HttpClient WebSocket Extract Extensions") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto request = zc::str("GET /websocket", WEBSOCKET_COMPRESSION_HANDSHAKE);

  auto serverTask =
      expectRead(*pipe.ends[1], request)
          .then([&]() {
            return writeA(*pipe.ends[1], asBytes(WEBSOCKET_COMPRESSION_RESPONSE_HANDSHAKE));
          })
          .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_CLOSE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
          .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId extHeader = tableBuilder.add("Sec-WebSocket-Extensions");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.webSocketCompressionMode = HttpClientSettings::MANUAL_COMPRESSION;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  constexpr auto extensions = "permessage-deflate; server_no_context_takeover"_zc;
  testWebSocketOptimizePumpProxy(waitScope, *headerTable, extHeader, extensions, *client);

  serverTask.wait(waitScope);
}
#endif  // ZC_HAS_ZLIB

#if ZC_HAS_ZLIB
ZC_TEST("HttpClient WebSocket Compression (Client Discards Compression Context)") {
  // We'll try to send and receive "Hello" twice. The second time we receive "Hello", the compressed
  // message will be the same size as the first time, since the client discards the lookback window.
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto request = zc::str("GET /websocket", WEBSOCKET_COMPRESSION_CLIENT_DISCARDS_CTX_HANDSHAKE);

  auto serverTask =
      expectRead(*pipe.ends[1], request)
          .then([&]() {
            return writeA(*pipe.ends[1],
                          asBytes(WEBSOCKET_COMPRESSION_CLIENT_DISCARDS_CTX_RESPONSE_HANDSHAKE));
          })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_COMPRESSED_MESSAGE); })
          .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_COMPRESSED_MESSAGE); })
          .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE); })
          .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_CLOSE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
          .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId extHeader = tableBuilder.add("Sec-WebSocket-Extensions");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.webSocketCompressionMode = HttpClientSettings::MANUAL_COMPRESSION;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  constexpr auto extensions =
      "permessage-deflate; client_no_context_takeover; server_no_context_takeover"_zc;
  testWebSocketTwoMessageCompression(waitScope, *headerTable, extHeader, extensions, *client);

  serverTask.wait(waitScope);
}
#endif  // ZC_HAS_ZLIB

#if ZC_HAS_ZLIB
ZC_TEST("HttpClient WebSocket Compression (Different DEFLATE blocks)") {
  // In this test, we'll try to use the following DEFLATE blocks:
  //  - Two DEFLATE blocks in 1 message.
  //  - A block with no compression.
  //  - A block with BFINAL set to 1.
  // Then, we'll try to send a normal compressed message following the BFINAL message to ensure we
  // can still process messages after receiving BFINAL.
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto request = zc::str("GET /websocket", WEBSOCKET_COMPRESSION_CLIENT_DISCARDS_CTX_HANDSHAKE);

  auto serverTask =
      expectRead(*pipe.ends[1], request)
          .then([&]() {
            return writeA(*pipe.ends[1],
                          asBytes(WEBSOCKET_COMPRESSION_CLIENT_DISCARDS_CTX_RESPONSE_HANDSHAKE));
          })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_TWO_DEFLATE_BLOCKS_MESSAGE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_DEFLATE_NO_COMPRESSION_MESSAGE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_BFINAL_SET_MESSAGE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE); })
          .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_CLOSE); })
          .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
          .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId extHeader = tableBuilder.add("Sec-WebSocket-Extensions");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.webSocketCompressionMode = HttpClientSettings::MANUAL_COMPRESSION;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  constexpr auto extensions =
      "permessage-deflate; client_no_context_takeover; server_no_context_takeover"_zc;
  testWebSocketFourMessageCompression(waitScope, *headerTable, extHeader, extensions, *client);

  serverTask.wait(waitScope);
}
#endif  // ZC_HAS_ZLIB

ZC_TEST("HttpClient WebSocket error") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto request = zc::str("GET /websocket", WEBSOCKET_REQUEST_HANDSHAKE);

  auto serverTask = expectRead(*pipe.ends[1], request)
                        .then([&]() {
                          return writeA(*pipe.ends[1], asBytes(WEBSOCKET_RESPONSE_HANDSHAKE_ERROR));
                        })
                        .then([&]() { return expectRead(*pipe.ends[1], request); })
                        .then([&]() {
                          return writeA(*pipe.ends[1], asBytes(WEBSOCKET_RESPONSE_HANDSHAKE_ERROR));
                        })
                        .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  zc::HttpHeaders headers(*headerTable);
  headers.set(hMyHeader, "foo");

  {
    auto response = client->openWebSocket("/websocket", headers).wait(waitScope);

    ZC_EXPECT(response.statusCode == 404);
    ZC_EXPECT(response.statusText == "Not Found", response.statusText);
    ZC_EXPECT(ZC_ASSERT_NONNULL(response.headers->get(hMyHeader)) == "respond-foo");
    ZC_ASSERT(response.webSocketOrBody.is<zc::Own<AsyncInputStream>>());
  }

  {
    auto response = client->openWebSocket("/websocket", headers).wait(waitScope);

    ZC_EXPECT(response.statusCode == 404);
    ZC_EXPECT(response.statusText == "Not Found", response.statusText);
    ZC_EXPECT(ZC_ASSERT_NONNULL(response.headers->get(hMyHeader)) == "respond-foo");
    ZC_ASSERT(response.webSocketOrBody.is<zc::Own<AsyncInputStream>>());
  }

  serverTask.wait(waitScope);
}

ZC_TEST("HttpServer WebSocket handshake") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();
  TestWebSocketService service(*headerTable, hMyHeader);
  HttpServer server(timer, *headerTable, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto request = zc::str("GET /websocket", WEBSOCKET_REQUEST_HANDSHAKE);
  writeA(*pipe.ends[1], request.asBytes()).wait(waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_RESPONSE_HANDSHAKE).wait(waitScope);

  expectRead(*pipe.ends[1], WEBSOCKET_FIRST_MESSAGE_INLINE).wait(waitScope);
  writeA(*pipe.ends[1], WEBSOCKET_SEND_MESSAGE).wait(waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_REPLY_MESSAGE).wait(waitScope);
  writeA(*pipe.ends[1], WEBSOCKET_SEND_CLOSE).wait(waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE).wait(waitScope);

  listenTask.wait(waitScope);
}

ZC_TEST("HttpServer WebSocket handshake error") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();
  TestWebSocketService service(*headerTable, hMyHeader);
  HttpServer server(timer, *headerTable, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto request = zc::str("GET /return-error", WEBSOCKET_REQUEST_HANDSHAKE);
  writeA(*pipe.ends[1], request.asBytes()).wait(waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_RESPONSE_HANDSHAKE_ERROR).wait(waitScope);

  // Can send more requests!
  writeA(*pipe.ends[1], request.asBytes()).wait(waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_RESPONSE_HANDSHAKE_ERROR).wait(waitScope);

  pipe.ends[1]->shutdownWrite();

  listenTask.wait(waitScope);
}

void testBadWebSocketHandshake(WaitScope& waitScope, Timer& timer, StringPtr request,
                               StringPtr response, TwoWayPipe pipe) {
  // Write an invalid WebSocket GET request, and expect a particular error response.

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();
  TestWebSocketService service(*headerTable, hMyHeader);

  class ErrorHandler final : public HttpServerErrorHandler {
    Promise<void> handleApplicationError(Exception exception,
                                         Maybe<HttpService::Response&> response) override {
      // When I first wrote this, I expected this function to be called, because
      // `TestWebSocketService::request()` definitely throws. However, the exception it throws comes
      // from `HttpService::Response::acceptWebSocket()`, which stores the fact which it threw a
      // WebSocket error. This prevents the HttpServer's listen loop from propagating the exception
      // to our HttpServerErrorHandler (i.e., this function), because it assumes the exception is
      // related to the WebSocket error response. See `HttpServer::Connection::startLoop()` for
      // details.
      bool responseWasSent = response == zc::none;
      ZC_FAIL_EXPECT("Unexpected application error", responseWasSent, exception);
      return READY_NOW;
    }
  };

  ErrorHandler errorHandler;

  HttpServerSettings serverSettings;
  serverSettings.errorHandler = errorHandler;

  HttpServer server(timer, *headerTable, service, serverSettings);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  pipe.ends[1]->write(request.asBytes()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1], response).wait(waitScope);

  listenTask.wait(waitScope);
}

ZC_TEST("HttpServer WebSocket handshake with unsupported Sec-WebSocket-Version") {
  static constexpr auto REQUEST =
      "GET /websocket HTTP/1.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: DCI4TgwiOE4MIjhODCI4Tg==\r\n"
      "Sec-WebSocket-Version: 1\r\n"
      "My-Header: foo\r\n"
      "\r\n"_zc;

  static constexpr auto RESPONSE =
      "HTTP/1.1 400 Bad Request\r\n"
      "Connection: close\r\n"
      "Content-Length: 56\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "ERROR: The requested WebSocket version is not supported."_zc;

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  testBadWebSocketHandshake(waitScope, timer, REQUEST, RESPONSE, ZC_HTTP_TEST_CREATE_2PIPE);
}

ZC_TEST("HttpServer WebSocket handshake with missing Sec-WebSocket-Key") {
  static constexpr auto REQUEST =
      "GET /websocket HTTP/1.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "My-Header: foo\r\n"
      "\r\n"_zc;

  static constexpr auto RESPONSE =
      "HTTP/1.1 400 Bad Request\r\n"
      "Connection: close\r\n"
      "Content-Length: 32\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "ERROR: Missing Sec-WebSocket-Key"_zc;

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  testBadWebSocketHandshake(waitScope, timer, REQUEST, RESPONSE, ZC_HTTP_TEST_CREATE_2PIPE);
}

ZC_TEST("HttpServer WebSocket with application error after accept") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  class WebSocketApplicationErrorService : public HttpService, public HttpServerErrorHandler {
    // Accepts a WebSocket, receives a message, and throws an exception (application error).

  public:
    Promise<void> request(HttpMethod method, zc::StringPtr, const HttpHeaders&, AsyncInputStream&,
                          Response& response) override {
      ZC_ASSERT(method == HttpMethod::GET);
      HttpHeaderTable headerTable;
      HttpHeaders responseHeaders(headerTable);
      auto webSocket = response.acceptWebSocket(responseHeaders);
      return webSocket->receive()
          .then([](WebSocket::Message) {
            throwRecoverableException(ZC_EXCEPTION(FAILED, "test exception"));
          })
          .attach(zc::mv(webSocket));
    }

    Promise<void> handleApplicationError(Exception exception, Maybe<Response&> response) override {
      // We accepted the WebSocket, so the response was already sent. At one time, we _did_ expose a
      // useless Response reference here, so this is a regression test.
      bool responseWasSent = response == zc::none;
      ZC_EXPECT(responseWasSent);
      ZC_EXPECT(exception.getDescription() == "test exception"_zc);
      return READY_NOW;
    }
  };

  // Set up the HTTP service.

  WebSocketApplicationErrorService service;

  HttpServerSettings serverSettings;
  serverSettings.errorHandler = service;

  HttpHeaderTable headerTable;
  HttpServer server(timer, headerTable, service, serverSettings);

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  // Make a client and open a WebSocket to the service.

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  auto client = newHttpClient(headerTable, *pipe.ends[1], clientSettings);

  HttpHeaders headers(headerTable);
  auto webSocketResponse = client->openWebSocket("/websocket"_zc, headers).wait(waitScope);

  ZC_ASSERT(webSocketResponse.statusCode == 101);
  auto webSocket =
      zc::mv(ZC_ASSERT_NONNULL(webSocketResponse.webSocketOrBody.tryGet<Own<WebSocket>>()));

  webSocket->send("ignored"_zc).wait(waitScope);

  listenTask.wait(waitScope);
}

// -----------------------------------------------------------------------------

ZC_TEST("HttpServer request timeout") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServerSettings settings;
  settings.headerTimeout = 1 * zc::MILLISECONDS;
  HttpServer server(timer, table, service, settings);

  // Shouldn't hang! Should time out.
  auto promise = server.listenHttp(zc::mv(pipe.ends[0]));
  ZC_EXPECT(!promise.poll(waitScope));
  timer.advanceTo(timer.now() + settings.headerTimeout / 2);
  ZC_EXPECT(!promise.poll(waitScope));
  timer.advanceTo(timer.now() + settings.headerTimeout);
  promise.wait(waitScope);

  // Closes the connection without sending anything.
  ZC_EXPECT(pipe.ends[1]->readAllText().wait(waitScope) == "");
}

ZC_TEST("HttpServer pipeline timeout") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServerSettings settings;
  settings.pipelineTimeout = 1 * zc::MILLISECONDS;
  HttpServer server(timer, table, service, settings);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.asBytes()).wait(waitScope);
  expectRead(*pipe.ends[1], PIPELINE_TESTS[0].response.raw).wait(waitScope);

  // Listen task should time out even though we didn't shutdown the socket.
  ZC_EXPECT(!listenTask.poll(waitScope));
  timer.advanceTo(timer.now() + settings.pipelineTimeout / 2);
  ZC_EXPECT(!listenTask.poll(waitScope));
  timer.advanceTo(timer.now() + settings.pipelineTimeout);
  listenTask.wait(waitScope);

  // In this case, no data is sent back.
  ZC_EXPECT(pipe.ends[1]->readAllText().wait(waitScope) == "");
}

class BrokenHttpService final : public HttpService {
  // HttpService that doesn't send a response.
public:
  BrokenHttpService() = default;
  explicit BrokenHttpService(zc::Exception&& exception) : exception(zc::mv(exception)) {}

  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& responseSender) override {
    return requestBody.readAllBytes().then([this](zc::Array<byte>&&) -> zc::Promise<void> {
      ZC_IF_SOME(e, exception) { return zc::cp(e); }
      else { return zc::READY_NOW; }
    });
  }

private:
  zc::Maybe<zc::Exception> exception;
};

ZC_TEST("HttpServer no response") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  BrokenHttpService service;
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.asBytes()).wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  ZC_EXPECT(text ==
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Connection: close\r\n"
                "Content-Length: 51\r\n"
                "Content-Type: text/plain\r\n"
                "\r\n"
                "ERROR: The HttpService did not generate a response.",
            text);
}

ZC_TEST("HttpServer disconnected") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  BrokenHttpService service(ZC_EXCEPTION(DISCONNECTED, "disconnected"));
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.asBytes()).wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  ZC_EXPECT(text == "", text);
}

ZC_TEST("HttpServer overloaded") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  BrokenHttpService service(ZC_EXCEPTION(OVERLOADED, "overloaded"));
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.asBytes()).wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  ZC_EXPECT(text.startsWith("HTTP/1.1 503 Service Unavailable"), text);
}

ZC_TEST("HttpServer unimplemented") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  BrokenHttpService service(ZC_EXCEPTION(UNIMPLEMENTED, "unimplemented"));
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.asBytes()).wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  ZC_EXPECT(text.startsWith("HTTP/1.1 501 Not Implemented"), text);
}

ZC_TEST("HttpServer threw exception") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  BrokenHttpService service(ZC_EXCEPTION(FAILED, "failed"));
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.asBytes()).wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  ZC_EXPECT(text.startsWith("HTTP/1.1 500 Internal Server Error"), text);
}

ZC_TEST("HttpServer bad requests") {
  struct TestCase {
    zc::StringPtr request;
    zc::StringPtr expectedResponse;
    bool expectWriteError = false;
  };

  static auto hugeHeaderRequest =
      zc::str("GET /foo/bar HTTP/1.1\r\n", "Host: ", zc::strArray(zc::repeat("0", 1024 * 1024), ""),
              "\r\n", "\r\n");

  static TestCase testCases[]{
      {// bad request
       .request = "GET / HTTP/1.1\r\nbad request\r\n\r\n"_zc,
       .expectedResponse = "HTTP/1.1 400 Bad Request\r\n"
                           "Connection: close\r\n"
                           "Content-Length: 53\r\n"
                           "Content-Type: text/plain\r\n"
                           "\r\n"
                           "ERROR: The headers sent by your client are not valid."_zc},
      {// invalid method
       .request = "bad request\r\n\r\n"_zc,
       .expectedResponse = "HTTP/1.1 501 Not Implemented\r\n"
                           "Connection: close\r\n"
                           "Content-Length: 35\r\n"
                           "Content-Type: text/plain\r\n"
                           "\r\n"
                           "ERROR: Unrecognized request method."_zc},
      {
          // broken service generates 5000
          .request = "GET /foo/bar HTTP/1.1\r\n"
                     "Host: example.com\r\n"
                     "\r\n"_zc,
          .expectedResponse = "HTTP/1.1 500 Internal Server Error\r\n"
                              "Connection: close\r\n"
                              "Content-Length: 51\r\n"
                              "Content-Type: text/plain\r\n"
                              "\r\n"
                              "ERROR: The HttpService did not generate a response."_zc,
      },
      {
          // huge header shouldn't break the server
          .request = hugeHeaderRequest,
          .expectedResponse = "HTTP/1.1 431 Request Header Fields Too Large\r\n"
                              "Connection: close\r\n"
                              "Content-Length: 24\r\n"
                              "Content-Type: text/plain\r\n"
                              "\r\n"
                              "ERROR: header too large."_zc,
          .expectWriteError = true,
      },
  };

  ZC_HTTP_TEST_SETUP_IO;
  // we need a real timer to test http server grace behavior.
  auto& timer = io.provider->getTimer();

  for (auto testCase : testCases) {
    auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

    HttpHeaderTable table;
    BrokenHttpService service;
    HttpServer server(timer, table, service,
                      {
                          .canceledUploadGraceBytes = 1024 * 1024,
                      });

    auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

    auto request = testCase.request;
    auto writePromise = pipe.ends[1]->write(request.asBytes());
    try {
      auto response = pipe.ends[1]->readAllText().wait(waitScope);
      auto expectedResponse = testCase.expectedResponse;
      ZC_EXPECT(expectedResponse == response, expectedResponse, response);
    } catch (...) {
      auto ex = zc::getCaughtExceptionAsKj();
      ZC_FAIL_REQUIRE("not supposed to happen", ex);
    }

    // write promise should have been resolved already
    ZC_EXPECT(writePromise.poll(waitScope));
    try {
      writePromise.wait(waitScope);
    } catch (...) { ZC_EXPECT(testCase.expectWriteError, "write error wasn't expected"); }
  }
}

// Ensure that HttpServerSettings can continue to be constexpr.
ZC_UNUSED static constexpr HttpServerSettings STATIC_CONSTEXPR_SETTINGS{};

class TestErrorHandler : public HttpServerErrorHandler {
public:
  zc::Promise<void> handleClientProtocolError(HttpHeaders::ProtocolError protocolError,
                                              zc::HttpService::Response& response) override {
    // In a real error handler, you should redact `protocolError.rawContent`.
    auto message = zc::str("Saw protocol error: ", protocolError.description,
                           "; rawContent = ", encodeCEscape(protocolError.rawContent));
    return sendError(400, "Bad Request", zc::mv(message), response);
  }

  zc::Promise<void> handleApplicationError(
      zc::Exception exception, zc::Maybe<zc::HttpService::Response&> response) override {
    return sendError(500, "Internal Server Error",
                     zc::str("Saw application error: ", exception.getDescription()), response);
  }

  zc::Promise<void> handleNoResponse(zc::HttpService::Response& response) override {
    return sendError(500, "Internal Server Error", zc::str("Saw no response."), response);
  }

  static TestErrorHandler instance;

private:
  zc::Promise<void> sendError(uint statusCode, zc::StringPtr statusText, String message,
                              Maybe<HttpService::Response&> response) {
    ZC_IF_SOME(r, response) {
      HttpHeaderTable headerTable;
      HttpHeaders headers(headerTable);
      auto body = r.send(statusCode, statusText, headers, message.size());
      return body->write(message.asBytes()).attach(zc::mv(body), zc::mv(message));
    }
    else {
      ZC_LOG(ERROR, "Saw an error but too late to report to client.");
      return zc::READY_NOW;
    }
  }
};

TestErrorHandler TestErrorHandler::instance{};

ZC_TEST("HttpServer no response, custom error handler") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpServerSettings settings{};
  settings.errorHandler = TestErrorHandler::instance;

  HttpHeaderTable table;
  BrokenHttpService service;
  HttpServer server(timer, table, service, settings);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.asBytes()).wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  ZC_EXPECT(text ==
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Connection: close\r\n"
                "Content-Length: 16\r\n"
                "\r\n"
                "Saw no response.",
            text);
}

ZC_TEST("HttpServer threw exception, custom error handler") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpServerSettings settings{};
  settings.errorHandler = TestErrorHandler::instance;

  HttpHeaderTable table;
  BrokenHttpService service(ZC_EXCEPTION(FAILED, "failed"));
  HttpServer server(timer, table, service, settings);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.asBytes()).wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  ZC_EXPECT(text ==
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Connection: close\r\n"
                "Content-Length: 29\r\n"
                "\r\n"
                "Saw application error: failed",
            text);
}

ZC_TEST("HttpServer bad request, custom error handler") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpServerSettings settings{};
  settings.errorHandler = TestErrorHandler::instance;

  HttpHeaderTable table;
  BrokenHttpService service;
  HttpServer server(timer, table, service, settings);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto writePromise = pipe.ends[1]->write("bad request\r\n\r\n"_zcb);
  auto response = pipe.ends[1]->readAllText().wait(waitScope);
  ZC_EXPECT(writePromise.poll(waitScope));
  writePromise.wait(waitScope);

  static constexpr auto expectedResponse =
      "HTTP/1.1 400 Bad Request\r\n"
      "Connection: close\r\n"
      "Content-Length: 80\r\n"
      "\r\n"
      "Saw protocol error: Unrecognized request method.; "
      "rawContent = bad request\\000\\n"_zc;

  ZC_EXPECT(expectedResponse == response, expectedResponse, response);
}

class PartialResponseService final : public HttpService {
  // HttpService that sends a partial response then throws.
public:
  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    return requestBody.readAllBytes().then(
        [this, &response](zc::Array<byte>&&) -> zc::Promise<void> {
          HttpHeaders headers(table);
          auto body = response.send(200, "OK", headers, 32);
          auto promise = body->write("foo"_zcb);
          return promise.attach(zc::mv(body)).then([]() -> zc::Promise<void> {
            return ZC_EXCEPTION(FAILED, "failed");
          });
        });
  }

private:
  zc::Maybe<zc::Exception> exception;
  HttpHeaderTable table;
};

ZC_TEST("HttpServer threw exception after starting response") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  PartialResponseService service;
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  ZC_EXPECT_LOG(ERROR, "HttpService threw exception after generating a partial response");

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.asBytes()).wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  ZC_EXPECT(text ==
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 32\r\n"
                "\r\n"
                "foo",
            text);
}

class PartialResponseNoThrowService final : public HttpService {
  // HttpService that sends a partial response then returns without throwing.
public:
  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    return requestBody.readAllBytes().then(
        [this, &response](zc::Array<byte>&&) -> zc::Promise<void> {
          HttpHeaders headers(table);
          auto body = response.send(200, "OK", headers, 32);
          auto promise = body->write("foo"_zcb);
          return promise.attach(zc::mv(body));
        });
  }

private:
  zc::Maybe<zc::Exception> exception;
  HttpHeaderTable table;
};

ZC_TEST("HttpServer failed to write complete response but didn't throw") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  PartialResponseNoThrowService service;
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.asBytes()).wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  ZC_EXPECT(text ==
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 32\r\n"
                "\r\n"
                "foo",
            text);
}

class SimpleInputStream final : public zc::AsyncInputStream {
  // An InputStream that returns bytes out of a static string.

public:
  SimpleInputStream(zc::StringPtr text) : unread(text.asBytes()) {}

  zc::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    size_t amount = zc::min(maxBytes, unread.size());
    memcpy(buffer, unread.begin(), amount);
    unread = unread.slice(amount, unread.size());
    return amount;
  }

private:
  zc::ArrayPtr<const byte> unread;
};

class PumpResponseService final : public HttpService {
  // HttpService that uses pumpTo() to write a response, without carefully specifying how much to
  // pump, but the stream happens to be the right size.
public:
  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    return requestBody.readAllBytes().then(
        [this, &response](zc::Array<byte>&&) -> zc::Promise<void> {
          HttpHeaders headers(table);
          zc::StringPtr text = "Hello, World!";
          auto body = response.send(200, "OK", headers, text.size());

          auto stream = zc::heap<SimpleInputStream>(text);
          auto promise = stream->pumpTo(*body);
          return promise.attach(zc::mv(body), zc::mv(stream)).then([text](uint64_t amount) {
            ZC_EXPECT(amount == text.size());
          });
        });
  }

private:
  zc::Maybe<zc::Exception> exception;
  HttpHeaderTable table;
};

ZC_TEST("HttpFixedLengthEntityWriter correctly implements tryPumpFrom") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  PumpResponseService service;
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.asBytes()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  ZC_EXPECT(text ==
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 13\r\n"
                "\r\n"
                "Hello, World!",
            text);
}

class HangingHttpService final : public HttpService {
  // HttpService that hangs forever.
public:
  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& responseSender) override {
    zc::Promise<void> result = zc::NEVER_DONE;
    ++inFlight;
    return result.attach(zc::defer([this]() {
      if (--inFlight == 0) {
        ZC_IF_SOME(f, onCancelFulfiller) { f->fulfill(); }
      }
    }));
  }

  zc::Promise<void> onCancel() {
    auto paf = zc::newPromiseAndFulfiller<void>();
    onCancelFulfiller = zc::mv(paf.fulfiller);
    return zc::mv(paf.promise);
  }

  uint inFlight = 0;

private:
  zc::Maybe<zc::Exception> exception;
  zc::Maybe<zc::Own<zc::PromiseFulfiller<void>>> onCancelFulfiller;
};

ZC_TEST("HttpServer cancels request when client disconnects") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  HangingHttpService service;
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  ZC_EXPECT(service.inFlight == 0);

  static constexpr auto request = "GET / HTTP/1.1\r\n\r\n"_zc;
  pipe.ends[1]->write(request.asBytes()).wait(waitScope);

  auto cancelPromise = service.onCancel();
  ZC_EXPECT(!cancelPromise.poll(waitScope));
  ZC_EXPECT(service.inFlight == 1);

  // Disconnect client and verify server cancels.
  pipe.ends[1] = nullptr;
  ZC_ASSERT(cancelPromise.poll(waitScope));
  ZC_EXPECT(service.inFlight == 0);
  cancelPromise.wait(waitScope);
}

class SuspendAfter : private HttpService {
  // A SuspendableHttpServiceFactory which responds to the first `n` requests with 200 OK, then
  // suspends all subsequent requests until its counter is reset.

public:
  void suspendAfter(uint countdownParam) { countdown = countdownParam; }

  zc::Maybe<zc::Own<HttpService>> operator()(HttpServer::SuspendableRequest& sr) {
    if (countdown == 0) {
      suspendedRequest = sr.suspend();
      return zc::none;
    }
    --countdown;
    return zc::Own<HttpService>(static_cast<HttpService*>(this), zc::NullDisposer::instance);
  }

  zc::Maybe<HttpServer::SuspendedRequest> getSuspended() {
    ZC_DEFER(suspendedRequest = zc::none);
    return zc::mv(suspendedRequest);
  }

private:
  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    HttpHeaders responseHeaders(table);
    response.send(200, "OK", responseHeaders);
    return requestBody.readAllBytes().ignoreResult();
  }

  HttpHeaderTable table;

  uint countdown = zc::maxValue;
  zc::Maybe<HttpServer::SuspendedRequest> suspendedRequest;
};

ZC_TEST("HttpServer can suspend a request") {
  // This test sends a single request to an HttpServer three times. First it writes the request to
  // its pipe and arranges for the HttpServer to suspend the request. Then it resumes the suspended
  // request and arranges for this resumption to be suspended as well. Then it resumes once more and
  // arranges for the request to be completed.

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  // This HttpService will not actually be used, because we're passing a factory in to
  // listenHttpCleanDrain().
  HangingHttpService service;
  HttpServer server(timer, table, service);

  zc::Maybe<HttpServer::SuspendedRequest> suspendedRequest;

  SuspendAfter factory;

  {
    // Observe the HttpServer suspend.

    factory.suspendAfter(0);
    auto listenPromise = server.listenHttpCleanDrain(*pipe.ends[0], factory);

    static constexpr zc::StringPtr REQUEST =
        "POST / HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "6\r\n"
        "foobar\r\n"
        "0\r\n"
        "\r\n"_zc;
    pipe.ends[1]->write(REQUEST.asBytes()).wait(waitScope);

    // The listen promise is fulfilled with false.
    ZC_EXPECT(listenPromise.poll(waitScope));
    ZC_EXPECT(!listenPromise.wait(waitScope));

    // And we have a SuspendedRequest.
    suspendedRequest = factory.getSuspended();
    ZC_EXPECT(suspendedRequest != zc::none);
  }

  {
    // Observe the HttpServer suspend again without reading from the connection.

    factory.suspendAfter(0);
    auto listenPromise =
        server.listenHttpCleanDrain(*pipe.ends[0], factory, zc::mv(suspendedRequest));

    // The listen promise is again fulfilled with false.
    ZC_EXPECT(listenPromise.poll(waitScope));
    ZC_EXPECT(!listenPromise.wait(waitScope));

    // We again have a suspendedRequest.
    suspendedRequest = factory.getSuspended();
    ZC_EXPECT(suspendedRequest != zc::none);
  }

  {
    // The SuspendedRequest is completed.

    factory.suspendAfter(1);
    auto listenPromise =
        server.listenHttpCleanDrain(*pipe.ends[0], factory, zc::mv(suspendedRequest));

    auto drainPromise = zc::evalLast([&]() { return server.drain(); });

    // We need to read the response for the HttpServer to drain.
    auto readPromise = pipe.ends[1]->readAllText();

    // This time, the server drained cleanly.
    ZC_EXPECT(listenPromise.poll(waitScope));
    ZC_EXPECT(listenPromise.wait(waitScope));

    drainPromise.wait(waitScope);

    // Close the server side of the pipe so our read promise completes.
    pipe.ends[0] = nullptr;

    auto response = readPromise.wait(waitScope);
    static constexpr zc::StringPtr RESPONSE =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "0\r\n"
        "\r\n"_zc;
    ZC_EXPECT(RESPONSE == response);
  }
}

ZC_TEST("HttpServer can suspend and resume pipelined requests") {
  // This test sends multiple requests with both Content-Length and Transfer-Encoding: chunked
  // bodies, and verifies that suspending both kinds does not corrupt the stream.

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  // This HttpService will not actually be used, because we're passing a factory in to
  // listenHttpCleanDrain().
  HangingHttpService service;
  HttpServer server(timer, table, service);

  // We'll suspend the second request.
  zc::Maybe<HttpServer::SuspendedRequest> suspendedRequest;
  SuspendAfter factory;

  static auto LENGTHFUL_REQUEST =
      "POST / HTTP/1.1\r\n"
      "Content-Length: 6\r\n"
      "\r\n"
      "foobar"_zcb;
  static auto CHUNKED_REQUEST =
      "POST / HTTP/1.1\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "6\r\n"
      "foobar\r\n"
      "0\r\n"
      "\r\n"_zcb;

  // Set up several requests; we'll suspend and transfer the second and third one.
  auto writePromise = pipe.ends[1]
                          ->write(LENGTHFUL_REQUEST)
                          .then([&]() { return pipe.ends[1]->write(CHUNKED_REQUEST); })
                          .then([&]() { return pipe.ends[1]->write(LENGTHFUL_REQUEST); })
                          .then([&]() { return pipe.ends[1]->write(CHUNKED_REQUEST); });

  auto readPromise = pipe.ends[1]->readAllText();

  {
    // Observe the HttpServer suspend the second request.

    factory.suspendAfter(1);
    auto listenPromise = server.listenHttpCleanDrain(*pipe.ends[0], factory);

    ZC_EXPECT(listenPromise.poll(waitScope));
    ZC_EXPECT(!listenPromise.wait(waitScope));
    suspendedRequest = factory.getSuspended();
    ZC_EXPECT(suspendedRequest != zc::none);
  }

  {
    // Let's resume one request and suspend the next pipelined request.

    factory.suspendAfter(1);
    auto listenPromise =
        server.listenHttpCleanDrain(*pipe.ends[0], factory, zc::mv(suspendedRequest));

    ZC_EXPECT(listenPromise.poll(waitScope));
    ZC_EXPECT(!listenPromise.wait(waitScope));
    suspendedRequest = factory.getSuspended();
    ZC_EXPECT(suspendedRequest != zc::none);
  }

  {
    // Resume again and run to completion.

    factory.suspendAfter(zc::maxValue);
    auto listenPromise =
        server.listenHttpCleanDrain(*pipe.ends[0], factory, zc::mv(suspendedRequest));

    auto drainPromise = zc::evalLast([&]() { return server.drain(); });

    // This time, the server drained cleanly.
    ZC_EXPECT(listenPromise.poll(waitScope));
    ZC_EXPECT(listenPromise.wait(waitScope));
    // No suspended request this time.
    suspendedRequest = factory.getSuspended();
    ZC_EXPECT(suspendedRequest == zc::none);

    drainPromise.wait(waitScope);
  }

  writePromise.wait(waitScope);

  // Close the server side of the pipe so our read promise completes.
  pipe.ends[0] = nullptr;

  auto responses = readPromise.wait(waitScope);
  static constexpr zc::StringPtr RESPONSE =
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "0\r\n"
      "\r\n"_zc;
  ZC_EXPECT(zc::str(zc::delimited(zc::repeat(RESPONSE, 4), "")) == responses);
}

ZC_TEST("HttpServer can suspend a request with no leftover") {
  // This test verifies that if the request loop's read perfectly ends at the end of message
  // headers, leaving no leftover section, we can still successfully suspend and resume.

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  // This HttpService will not actually be used, because we're passing a factory in to
  // listenHttpCleanDrain().
  HangingHttpService service;
  HttpServer server(timer, table, service);

  zc::Maybe<HttpServer::SuspendedRequest> suspendedRequest;

  SuspendAfter factory;

  {
    factory.suspendAfter(0);
    auto listenPromise = server.listenHttpCleanDrain(*pipe.ends[0], factory);

    static auto REQUEST_HEADERS =
        "POST / HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"_zcb;
    pipe.ends[1]->write(REQUEST_HEADERS).wait(waitScope);

    // The listen promise is fulfilled with false.
    ZC_EXPECT(listenPromise.poll(waitScope));
    ZC_EXPECT(!listenPromise.wait(waitScope));

    // And we have a SuspendedRequest. We know that it has no leftover, because we only wrote
    // headers, no body yet.
    suspendedRequest = factory.getSuspended();
    ZC_EXPECT(suspendedRequest != zc::none);
  }

  {
    factory.suspendAfter(1);
    auto listenPromise =
        server.listenHttpCleanDrain(*pipe.ends[0], factory, zc::mv(suspendedRequest));

    auto drainPromise = zc::evalLast([&]() { return server.drain(); });

    // We need to read the response for the HttpServer to drain.
    auto readPromise = pipe.ends[1]->readAllText();

    static auto REQUEST_BODY =
        "6\r\n"
        "foobar\r\n"
        "0\r\n"
        "\r\n"_zcb;
    pipe.ends[1]->write(REQUEST_BODY).wait(waitScope);

    // Clean drain.
    ZC_EXPECT(listenPromise.poll(waitScope));
    ZC_EXPECT(listenPromise.wait(waitScope));

    drainPromise.wait(waitScope);

    // No SuspendedRequest.
    suspendedRequest = factory.getSuspended();
    ZC_EXPECT(suspendedRequest == zc::none);

    // Close the server side of the pipe so our read promise completes.
    pipe.ends[0] = nullptr;

    auto response = readPromise.wait(waitScope);
    static constexpr zc::StringPtr RESPONSE =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "0\r\n"
        "\r\n"_zc;
    ZC_EXPECT(RESPONSE == response);
  }
}

ZC_TEST("HttpServer::listenHttpCleanDrain() factory-created services outlive requests") {
  // Test that the lifetimes of factory-created Own<HttpService> objects are handled correctly.

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  // This HttpService will not actually be used, because we're passing a factory in to
  // listenHttpCleanDrain().
  HangingHttpService service;
  HttpServer server(timer, table, service);

  uint serviceCount = 0;

  // A factory which returns a service whose request() function responds asynchronously.
  auto factory = [&](HttpServer::SuspendableRequest&) -> zc::Own<HttpService> {
    class ServiceImpl final : public HttpService {
    public:
      explicit ServiceImpl(uint& serviceCount) : serviceCount(++serviceCount) {}
      ~ServiceImpl() noexcept(false) { --serviceCount; }
      ZC_DISALLOW_COPY_AND_MOVE(ServiceImpl);

      zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                                zc::AsyncInputStream& requestBody, Response& response) override {
        return evalLater([&serviceCount = serviceCount, &table = table, &requestBody, &response]() {
          // This ZC_EXPECT here is the entire point of this test.
          ZC_EXPECT(serviceCount == 1)
          HttpHeaders responseHeaders(table);
          response.send(200, "OK", responseHeaders);
          return requestBody.readAllBytes().ignoreResult();
        });
      }

    private:
      HttpHeaderTable table;

      uint& serviceCount;
    };

    return zc::heap<ServiceImpl>(serviceCount);
  };

  auto listenPromise = server.listenHttpCleanDrain(*pipe.ends[0], factory);

  static constexpr zc::StringPtr REQUEST =
      "POST / HTTP/1.1\r\n"
      "Content-Length: 6\r\n"
      "\r\n"
      "foobar"_zc;
  pipe.ends[1]->write(REQUEST.asBytes()).wait(waitScope);

  // We need to read the response for the HttpServer to drain.
  auto readPromise = pipe.ends[1]->readAllText();

  // http-socketpair-test quirk: we must drive the request loop past the point of receiving request
  // headers so that our call to server.drain() doesn't prematurely cancel the request.
  ZC_EXPECT(!listenPromise.poll(waitScope));

  auto drainPromise = zc::evalLast([&]() { return server.drain(); });

  // Clean drain.
  ZC_EXPECT(listenPromise.poll(waitScope));
  ZC_EXPECT(listenPromise.wait(waitScope));

  drainPromise.wait(waitScope);

  // Close the server side of the pipe so our read promise completes.
  pipe.ends[0] = nullptr;
  auto response = readPromise.wait(waitScope);

  static constexpr zc::StringPtr RESPONSE =
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "0\r\n"
      "\r\n"_zc;
  ZC_EXPECT(RESPONSE == response);
}

// -----------------------------------------------------------------------------

ZC_TEST("newHttpService from HttpClient") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto frontPipe = ZC_HTTP_TEST_CREATE_2PIPE;
  auto backPipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::Promise<void> writeResponsesPromise = zc::READY_NOW;
  for (auto& testCase : PIPELINE_TESTS) {
    writeResponsesPromise =
        writeResponsesPromise
            .then([&]() { return expectRead(*backPipe.ends[1], testCase.request.raw); })
            .then([&]() { return backPipe.ends[1]->write(testCase.response.raw.asBytes()); });
  }

  {
    HttpHeaderTable table;
    auto backClient = newHttpClient(table, *backPipe.ends[0]);
    auto frontService = newHttpService(*backClient);
    HttpServer frontServer(timer, table, *frontService);
    auto listenTask = frontServer.listenHttp(zc::mv(frontPipe.ends[1]));

    for (auto& testCase : PIPELINE_TESTS) {
      ZC_CONTEXT(testCase.request.raw, testCase.response.raw);

      frontPipe.ends[0]->write(testCase.request.raw.asBytes()).wait(waitScope);

      expectRead(*frontPipe.ends[0], testCase.response.raw).wait(waitScope);
    }

    frontPipe.ends[0]->shutdownWrite();
    listenTask.wait(waitScope);
  }

  backPipe.ends[0]->shutdownWrite();
  writeResponsesPromise.wait(waitScope);
}

ZC_TEST("newHttpService from HttpClient WebSockets") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto frontPipe = ZC_HTTP_TEST_CREATE_2PIPE;
  auto backPipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto request = zc::str("GET /websocket", WEBSOCKET_REQUEST_HANDSHAKE);
  auto writeResponsesPromise =
      expectRead(*backPipe.ends[1], request)
          .then([&]() { return writeA(*backPipe.ends[1], asBytes(WEBSOCKET_RESPONSE_HANDSHAKE)); })
          .then([&]() { return writeA(*backPipe.ends[1], WEBSOCKET_FIRST_MESSAGE_INLINE); })
          .then([&]() { return expectRead(*backPipe.ends[1], WEBSOCKET_SEND_MESSAGE); })
          .then([&]() { return writeA(*backPipe.ends[1], WEBSOCKET_REPLY_MESSAGE); })
          .then([&]() { return expectRead(*backPipe.ends[1], WEBSOCKET_SEND_CLOSE); })
          .then([&]() { return writeA(*backPipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
          .then([&]() { return expectEnd(*backPipe.ends[1]); })
          .then([&]() { backPipe.ends[1]->shutdownWrite(); })
          .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });

  {
    HttpHeaderTable table;
    FakeEntropySource entropySource;
    HttpClientSettings clientSettings;
    clientSettings.entropySource = entropySource;
    auto backClientStream = zc::mv(backPipe.ends[0]);
    auto backClient = newHttpClient(table, *backClientStream, clientSettings);
    auto frontService = newHttpService(*backClient);
    HttpServer frontServer(timer, table, *frontService);
    auto listenTask = frontServer.listenHttp(zc::mv(frontPipe.ends[1]));

    writeA(*frontPipe.ends[0], request.asBytes()).wait(waitScope);
    expectRead(*frontPipe.ends[0], WEBSOCKET_RESPONSE_HANDSHAKE).wait(waitScope);

    expectRead(*frontPipe.ends[0], WEBSOCKET_FIRST_MESSAGE_INLINE).wait(waitScope);
    writeA(*frontPipe.ends[0], WEBSOCKET_SEND_MESSAGE).wait(waitScope);
    expectRead(*frontPipe.ends[0], WEBSOCKET_REPLY_MESSAGE).wait(waitScope);
    writeA(*frontPipe.ends[0], WEBSOCKET_SEND_CLOSE).wait(waitScope);
    expectRead(*frontPipe.ends[0], WEBSOCKET_REPLY_CLOSE).wait(waitScope);

    frontPipe.ends[0]->shutdownWrite();
    listenTask.wait(waitScope);
  }

  writeResponsesPromise.wait(waitScope);
}

ZC_TEST("HttpClient WebSocket: client can have a custom WebSocket error handler") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  // These are WEBSOCKET_REQUEST_HANDSHAKE and WEBSOCKET_RESPONSE_HANDSHAKE but without the
  // "My-Header" header. This test isn't about the HTTP handshake, so the headers are just noise.
  const char wsRequestHandshake[] =
      " HTTP/1.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: DCI4TgwiOE4MIjhODCI4Tg==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "\r\n";
  const char wsResponseHandshake[] =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Accept: pShtIFKT0s8RYZvnWY/CrjQD8CM=\r\n"
      "\r\n";

  const byte badFrame[] = {
      0xF0, 0x02, 'y', 'o'  // all RSV bits set, plus FIN
  };
  const byte closeFrame[] = {
      0x88,      0xa8,     12,       34,       56,       78,       0x3 ^ 12,
      0xea ^ 34,  // FIN, opcode=Close, code=1009
      'R' ^ 56,  'e' ^ 78, 'c' ^ 12, 'e' ^ 34, 'i' ^ 56, 'v' ^ 78, 'e' ^ 12, 'd' ^ 34,
      ' ' ^ 56,  'f' ^ 78, 'r' ^ 12, 'a' ^ 34, 'm' ^ 56, 'e' ^ 78, ' ' ^ 12, 'h' ^ 34,
      'a' ^ 56,  'd' ^ 78, ' ' ^ 12, 'R' ^ 34, 'S' ^ 56, 'V' ^ 78, ' ' ^ 12, 'b' ^ 34,
      'i' ^ 56,  't' ^ 78, 's' ^ 12, ' ' ^ 34, '2' ^ 56, ' ' ^ 78, 'o' ^ 12, 'r' ^ 34,
      ' ' ^ 56,  '3' ^ 78, ' ' ^ 12, 's' ^ 34, 'e' ^ 56, 't' ^ 78,
  };

  auto request = zc::str("GET /websocket", wsRequestHandshake);
  auto serverPromise =
      expectRead(*pipe.ends[1], request)
          .then([&]() { return writeA(*pipe.ends[1], asBytes(wsResponseHandshake)); })
          .then([&]() { return writeA(*pipe.ends[1], badFrame); })
          .then([&]() { return expectRead(*pipe.ends[1], closeFrame); })
          .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });

  {
    HttpHeaderTable table;
    FakeEntropySource entropySource;
    HttpClientSettings clientSettings;
    WebSocketErrorCatcher errorCatcher;
    clientSettings.entropySource = entropySource;
    clientSettings.webSocketErrorHandler = errorCatcher;

    auto clientStream = zc::mv(pipe.ends[0]);
    auto httpClient = newHttpClient(table, *clientStream, clientSettings);
    auto wsClientPromise =
        httpClient->openWebSocket("/websocket", HttpHeaders(table))
            .then([&](zc::HttpClient::WebSocketResponse resp) {
              return zc::mv(resp.webSocketOrBody.get<zc::Own<zc::WebSocket>>());
            })
            .then([](zc::Own<zc::WebSocket> webSocket) -> zc::Promise<zc::WebSocket::Message> {
              return webSocket->receive().attach(zc::mv(webSocket));
            })
            .eagerlyEvaluate([](zc::Exception e) -> zc::WebSocket::Message {
              return zc::str("irrelevant value");
            });

    wsClientPromise.wait(waitScope);
    ZC_EXPECT(errorCatcher.errors.size() == 1);
  }

  serverPromise.wait(waitScope);
}

ZC_TEST("newHttpService from HttpClient WebSockets disconnect") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto frontPipe = ZC_HTTP_TEST_CREATE_2PIPE;
  auto backPipe = ZC_HTTP_TEST_CREATE_2PIPE;

  auto request = zc::str("GET /websocket", WEBSOCKET_REQUEST_HANDSHAKE);
  auto writeResponsesPromise =
      expectRead(*backPipe.ends[1], request)
          .then([&]() { return writeA(*backPipe.ends[1], asBytes(WEBSOCKET_RESPONSE_HANDSHAKE)); })
          .then([&]() { return writeA(*backPipe.ends[1], WEBSOCKET_FIRST_MESSAGE_INLINE); })
          .then([&]() { return expectRead(*backPipe.ends[1], WEBSOCKET_SEND_MESSAGE); })
          .then([&]() { backPipe.ends[1]->shutdownWrite(); })
          .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });

  {
    HttpHeaderTable table;
    FakeEntropySource entropySource;
    HttpClientSettings clientSettings;
    clientSettings.entropySource = entropySource;
    auto backClient = newHttpClient(table, *backPipe.ends[0], clientSettings);
    auto frontService = newHttpService(*backClient);
    HttpServer frontServer(timer, table, *frontService);
    auto listenTask = frontServer.listenHttp(zc::mv(frontPipe.ends[1]));

    writeA(*frontPipe.ends[0], request.asBytes()).wait(waitScope);
    expectRead(*frontPipe.ends[0], WEBSOCKET_RESPONSE_HANDSHAKE).wait(waitScope);

    expectRead(*frontPipe.ends[0], WEBSOCKET_FIRST_MESSAGE_INLINE).wait(waitScope);
    writeA(*frontPipe.ends[0], WEBSOCKET_SEND_MESSAGE).wait(waitScope);

    ZC_EXPECT(frontPipe.ends[0]->readAllText().wait(waitScope) == "");

    frontPipe.ends[0]->shutdownWrite();
    listenTask.wait(waitScope);
  }

  writeResponsesPromise.wait(waitScope);
}

// -----------------------------------------------------------------------------

ZC_TEST("newHttpClient from HttpService") {
  auto PIPELINE_TESTS = pipelineTestCases();

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  auto client = newHttpClient(service);

  for (auto& testCase : PIPELINE_TESTS) { testHttpClient(waitScope, table, *client, testCase); }
}

ZC_TEST("newHttpClient from HttpService WebSockets") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();
  TestWebSocketService service(*headerTable, hMyHeader);
  auto client = newHttpClient(service);

  testWebSocketClient(waitScope, *headerTable, hMyHeader, *client);
}

ZC_TEST("adapted client/server propagates request exceptions like non-adapted client") {
  ZC_HTTP_TEST_SETUP_IO;

  HttpHeaderTable table;
  HttpHeaders headers(table);

  class FailingHttpClient final : public HttpClient {
  public:
    Request request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                    zc::Maybe<uint64_t> expectedBodySize = zc::none) override {
      ZC_FAIL_ASSERT("request_fail");
    }

    zc::Promise<WebSocketResponse> openWebSocket(zc::StringPtr url,
                                                 const HttpHeaders& headers) override {
      ZC_FAIL_ASSERT("websocket_fail");
    }
  };

  auto rawClient = zc::heap<FailingHttpClient>();

  auto innerClient = zc::heap<FailingHttpClient>();
  auto adaptedService = zc::newHttpService(*innerClient).attach(zc::mv(innerClient));
  auto adaptedClient = zc::newHttpClient(*adaptedService).attach(zc::mv(adaptedService));

  ZC_EXPECT_THROW_MESSAGE("request_fail", rawClient->request(HttpMethod::POST, "/"_zc, headers));
  ZC_EXPECT_THROW_MESSAGE("request_fail",
                          adaptedClient->request(HttpMethod::POST, "/"_zc, headers));

  ZC_EXPECT_THROW_MESSAGE("websocket_fail", rawClient->openWebSocket("/"_zc, headers));
  ZC_EXPECT_THROW_MESSAGE("websocket_fail", adaptedClient->openWebSocket("/"_zc, headers));
}

class DelayedCompletionHttpService final : public HttpService {
public:
  DelayedCompletionHttpService(HttpHeaderTable& table, zc::Maybe<uint64_t> expectedLength)
      : table(table), expectedLength(expectedLength) {}

  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    auto stream = response.send(200, "OK", HttpHeaders(table), expectedLength);
    auto promise = stream->write("foo"_zcb);
    return promise.attach(zc::mv(stream)).then([this]() { return zc::mv(paf.promise); });
  }

  zc::PromiseFulfiller<void>& getFulfiller() { return *paf.fulfiller; }

private:
  HttpHeaderTable& table;
  zc::Maybe<uint64_t> expectedLength;
  zc::PromiseFulfillerPair<void> paf = zc::newPromiseAndFulfiller<void>();
};

void doDelayedCompletionTest(bool exception, zc::Maybe<uint64_t> expectedLength) noexcept {
  ZC_HTTP_TEST_SETUP_IO;

  HttpHeaderTable table;

  DelayedCompletionHttpService service(table, expectedLength);
  auto client = newHttpClient(service);

  auto resp = client->request(HttpMethod::GET, "/", HttpHeaders(table), uint64_t(0))
                  .response.wait(waitScope);
  ZC_EXPECT(resp.statusCode == 200);

  // Read "foo" from the response body: works
  char buffer[16]{};
  ZC_ASSERT(resp.body->tryRead(buffer, 1, sizeof(buffer)).wait(waitScope) == 3);
  buffer[3] = '\0';
  ZC_EXPECT(buffer == "foo"_zc);

  // But reading any more hangs.
  auto promise = resp.body->tryRead(buffer, 1, sizeof(buffer));

  ZC_EXPECT(!promise.poll(waitScope));

  // Until we cause the service to return.
  if (exception) {
    service.getFulfiller().reject(ZC_EXCEPTION(FAILED, "service-side failure"));
  } else {
    service.getFulfiller().fulfill();
  }

  ZC_ASSERT(promise.poll(waitScope));

  if (exception) {
    ZC_EXPECT_THROW_MESSAGE("service-side failure", promise.wait(waitScope));
  } else {
    promise.wait(waitScope);
  }
};

ZC_TEST("adapted client waits for service to complete before returning EOF on response stream") {
  doDelayedCompletionTest(false, uint64_t(3));
}

ZC_TEST("adapted client waits for service to complete before returning EOF on chunked response") {
  doDelayedCompletionTest(false, zc::none);
}

ZC_TEST("adapted client propagates throw from service after complete response body sent") {
  doDelayedCompletionTest(true, uint64_t(3));
}

ZC_TEST("adapted client propagates throw from service after incomplete response body sent") {
  doDelayedCompletionTest(true, uint64_t(6));
}

ZC_TEST("adapted client propagates throw from service after chunked response body sent") {
  doDelayedCompletionTest(true, zc::none);
}

class DelayedCompletionWebSocketHttpService final : public HttpService {
public:
  DelayedCompletionWebSocketHttpService(HttpHeaderTable& table, bool closeUpstreamFirst)
      : table(table), closeUpstreamFirst(closeUpstreamFirst) {}

  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    ZC_ASSERT(headers.isWebSocket());

    auto ws = response.acceptWebSocket(HttpHeaders(table));
    zc::Promise<void> promise = zc::READY_NOW;
    if (closeUpstreamFirst) {
      // Wait for a close message from the client before starting.
      promise = promise.then([&ws = *ws]() { return ws.receive(); }).ignoreResult();
    }
    promise = promise.then([&ws = *ws]() { return ws.send("foo"_zc); }).then([&ws = *ws]() {
      return ws.close(1234, "closed"_zc);
    });
    if (!closeUpstreamFirst) {
      // Wait for a close message from the client at the end.
      promise = promise.then([&ws = *ws]() { return ws.receive(); }).ignoreResult();
    }
    return promise.attach(zc::mv(ws)).then([this]() { return zc::mv(paf.promise); });
  }

  zc::PromiseFulfiller<void>& getFulfiller() { return *paf.fulfiller; }

private:
  HttpHeaderTable& table;
  bool closeUpstreamFirst;
  zc::PromiseFulfillerPair<void> paf = zc::newPromiseAndFulfiller<void>();
};

void doDelayedCompletionWebSocketTest(bool exception, bool closeUpstreamFirst) noexcept {
  ZC_HTTP_TEST_SETUP_IO;

  HttpHeaderTable table;

  DelayedCompletionWebSocketHttpService service(table, closeUpstreamFirst);
  auto client = newHttpClient(service);

  auto resp = client->openWebSocket("/", HttpHeaders(table)).wait(waitScope);
  auto ws = zc::mv(ZC_ASSERT_NONNULL(resp.webSocketOrBody.tryGet<zc::Own<WebSocket>>()));

  if (closeUpstreamFirst) {
    // Send "close" immediately.
    ws->close(1234, "whatever"_zc).wait(waitScope);
  }

  // Read "foo" from the WebSocket: works
  {
    auto msg = ws->receive().wait(waitScope);
    ZC_ASSERT(msg.is<zc::String>());
    ZC_ASSERT(msg.get<zc::String>() == "foo");
  }

  zc::Promise<void> promise = nullptr;
  if (closeUpstreamFirst) {
    // Receiving the close hangs.
    promise =
        ws->receive().then([](WebSocket::Message&& msg) { ZC_EXPECT(msg.is<WebSocket::Close>()); });
  } else {
    auto msg = ws->receive().wait(waitScope);
    ZC_ASSERT(msg.is<WebSocket::Close>());

    // Sending a close hangs.
    promise = ws->close(1234, "whatever"_zc);
  }
  ZC_EXPECT(!promise.poll(waitScope));

  // Until we cause the service to return.
  if (exception) {
    service.getFulfiller().reject(ZC_EXCEPTION(FAILED, "service-side failure"));
  } else {
    service.getFulfiller().fulfill();
  }

  ZC_ASSERT(promise.poll(waitScope));

  if (exception) {
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("service-side failure", promise.wait(waitScope));
  } else {
    promise.wait(waitScope);
  }
};

ZC_TEST(
    "adapted client waits for service to complete before completing upstream close on WebSocket") {
  doDelayedCompletionWebSocketTest(false, false);
}

ZC_TEST(
    "adapted client waits for service to complete before returning downstream close on WebSocket") {
  doDelayedCompletionWebSocketTest(false, true);
}

ZC_TEST("adapted client propagates throw from service after WebSocket upstream close sent") {
  doDelayedCompletionWebSocketTest(true, false);
}

ZC_TEST("adapted client propagates throw from service after WebSocket downstream close sent") {
  doDelayedCompletionWebSocketTest(true, true);
}

// -----------------------------------------------------------------------------

class CountingIoStream final : public zc::AsyncIoStream {
  // An AsyncIoStream wrapper which decrements a counter when destroyed (allowing us to count how
  // many connections are open).

public:
  CountingIoStream(zc::Own<zc::AsyncIoStream> inner, uint& count)
      : inner(zc::mv(inner)), count(count) {}
  ~CountingIoStream() noexcept(false) { --count; }

  zc::Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    return inner->read(buffer, minBytes, maxBytes);
  }
  zc::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return inner->tryRead(buffer, minBytes, maxBytes);
  }
  zc::Maybe<uint64_t> tryGetLength() override {
    return inner->tryGetLength();
    ;
  }
  zc::Promise<uint64_t> pumpTo(zc::AsyncOutputStream& output, uint64_t amount) override {
    return inner->pumpTo(output, amount);
  }
  zc::Promise<void> write(ArrayPtr<const byte> buffer) override { return inner->write(buffer); }
  zc::Promise<void> write(zc::ArrayPtr<const zc::ArrayPtr<const byte>> pieces) override {
    return inner->write(pieces);
  }
  zc::Maybe<zc::Promise<uint64_t>> tryPumpFrom(zc::AsyncInputStream& input,
                                               uint64_t amount = zc::maxValue) override {
    return inner->tryPumpFrom(input, amount);
  }
  Promise<void> whenWriteDisconnected() override { return inner->whenWriteDisconnected(); }
  void shutdownWrite() override { return inner->shutdownWrite(); }
  void abortRead() override { return inner->abortRead(); }

public:
  zc::Own<AsyncIoStream> inner;
  uint& count;
};

class CountingNetworkAddress final : public zc::NetworkAddress {
public:
  CountingNetworkAddress(zc::NetworkAddress& inner, uint& count, uint& cumulative)
      : inner(inner), count(count), addrCount(ownAddrCount), cumulative(cumulative) {}
  CountingNetworkAddress(zc::Own<zc::NetworkAddress> inner, uint& count, uint& addrCount)
      : inner(*inner),
        ownInner(zc::mv(inner)),
        count(count),
        addrCount(addrCount),
        cumulative(ownCumulative) {}
  ~CountingNetworkAddress() noexcept(false) { --addrCount; }

  zc::Promise<zc::Own<zc::AsyncIoStream>> connect() override {
    ++count;
    ++cumulative;
    return inner.connect().then(
        [this](zc::Own<zc::AsyncIoStream> stream) -> zc::Own<zc::AsyncIoStream> {
          return zc::heap<CountingIoStream>(zc::mv(stream), count);
        });
  }

  zc::Own<zc::ConnectionReceiver> listen() override { ZC_UNIMPLEMENTED("test"); }
  zc::Own<zc::NetworkAddress> clone() override { ZC_UNIMPLEMENTED("test"); }
  zc::String toString() override { ZC_UNIMPLEMENTED("test"); }

private:
  zc::NetworkAddress& inner;
  zc::Own<zc::NetworkAddress> ownInner;
  uint& count;
  uint ownAddrCount = 1;
  uint& addrCount;
  uint ownCumulative = 0;
  uint& cumulative;
};

class ConnectionCountingNetwork final : public zc::Network {
public:
  ConnectionCountingNetwork(zc::Network& inner, uint& count, uint& addrCount)
      : inner(inner), count(count), addrCount(addrCount) {}

  Promise<Own<NetworkAddress>> parseAddress(StringPtr addr, uint portHint = 0) override {
    ++addrCount;
    return inner.parseAddress(addr, portHint)
        .then([this](Own<NetworkAddress>&& addr) -> Own<NetworkAddress> {
          return zc::heap<CountingNetworkAddress>(zc::mv(addr), count, addrCount);
        });
  }
  Own<NetworkAddress> getSockaddr(const void* sockaddr, uint len) override {
    ZC_UNIMPLEMENTED("test");
  }
  Own<Network> restrictPeers(zc::ArrayPtr<const zc::StringPtr> allow,
                             zc::ArrayPtr<const zc::StringPtr> deny = nullptr) override {
    ZC_UNIMPLEMENTED("test");
  }

private:
  zc::Network& inner;
  uint& count;
  uint& addrCount;
};

class DummyService final : public HttpService {
public:
  DummyService(HttpHeaderTable& headerTable) : headerTable(headerTable) {}

  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    if (!headers.isWebSocket()) {
      if (url == "/throw") { return ZC_EXCEPTION(FAILED, "client requested failure"); }

      auto body = zc::str(headers.get(HttpHeaderId::HOST).orDefault("null"), ":", url);
      auto stream = response.send(200, "OK", HttpHeaders(headerTable), body.size());
      auto promises = zc::heapArrayBuilder<zc::Promise<void>>(2);
      promises.add(stream->write(body.asBytes()));
      promises.add(requestBody.readAllBytes().ignoreResult());
      return zc::joinPromises(promises.finish()).attach(zc::mv(stream), zc::mv(body));
    } else {
      auto ws = response.acceptWebSocket(HttpHeaders(headerTable));
      auto body = zc::str(headers.get(HttpHeaderId::HOST).orDefault("null"), ":", url);
      auto sendPromise = ws->send(body);

      auto promises = zc::heapArrayBuilder<zc::Promise<void>>(2);
      promises.add(sendPromise.attach(zc::mv(body)));
      promises.add(ws->receive().ignoreResult());
      return zc::joinPromises(promises.finish()).attach(zc::mv(ws));
    }
  }

private:
  HttpHeaderTable& headerTable;
};

ZC_TEST("HttpClient connection management") {
  ZC_HTTP_TEST_SETUP_IO;
  ZC_HTTP_TEST_SETUP_LOOPBACK_LISTENER_AND_ADDR;

  zc::TimerImpl serverTimer(zc::origin<zc::TimePoint>());
  zc::TimerImpl clientTimer(zc::origin<zc::TimePoint>());
  HttpHeaderTable headerTable;

  DummyService service(headerTable);
  HttpServerSettings serverSettings;
  HttpServer server(serverTimer, headerTable, service, serverSettings);
  auto listenTask = server.listenHttp(*listener);

  uint count = 0;
  uint cumulative = 0;
  CountingNetworkAddress countingAddr(*addr, count, cumulative);

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  auto client = newHttpClient(clientTimer, headerTable, countingAddr, clientSettings);

  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 0);

  uint i = 0;
  auto doRequest = [&]() {
    uint n = i++;
    return client->request(HttpMethod::GET, zc::str("/", n), HttpHeaders(headerTable))
        .response
        .then([](HttpClient::Response&& response) {
          auto promise = response.body->readAllText();
          return promise.attach(zc::mv(response.body));
        })
        .then([n](zc::String body) { ZC_EXPECT(body == zc::str("null:/", n)); });
  };

  // We can do several requests in a row and only have one connection.
  doRequest().wait(waitScope);
  doRequest().wait(waitScope);
  doRequest().wait(waitScope);
  ZC_EXPECT(count == 1);
  ZC_EXPECT(cumulative == 1);

  // But if we do two in parallel, we'll end up with two connections.
  auto req1 = doRequest();
  auto req2 = doRequest();
  req1.wait(waitScope);
  req2.wait(waitScope);
  ZC_EXPECT(count == 2);
  ZC_EXPECT(cumulative == 2);

  // We can reuse after a POST, provided we write the whole POST body properly.
  {
    auto req =
        client->request(HttpMethod::POST, zc::str("/foo"), HttpHeaders(headerTable), size_t(6));
    req.body->write("foobar"_zcb).wait(waitScope);
    req.response.wait(waitScope).body->readAllBytes().wait(waitScope);
  }
  ZC_EXPECT(count == 2);
  ZC_EXPECT(cumulative == 2);
  doRequest().wait(waitScope);
  ZC_EXPECT(count == 2);
  ZC_EXPECT(cumulative == 2);

  // Advance time for half the timeout, then exercise one of the connections.
  clientTimer.advanceTo(clientTimer.now() + clientSettings.idleTimeout / 2);
  doRequest().wait(waitScope);
  doRequest().wait(waitScope);
  waitScope.poll();
  ZC_EXPECT(count == 2);
  ZC_EXPECT(cumulative == 2);

  // Advance time past when the other connection should time out. It should be dropped.
  clientTimer.advanceTo(clientTimer.now() + clientSettings.idleTimeout * 3 / 4);
  waitScope.poll();
  ZC_EXPECT(count == 1);
  ZC_EXPECT(cumulative == 2);

  // Wait for the other to drop.
  clientTimer.advanceTo(clientTimer.now() + clientSettings.idleTimeout / 2);
  waitScope.poll();
  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 2);

  // New request creates a new connection again.
  doRequest().wait(waitScope);
  ZC_EXPECT(count == 1);
  ZC_EXPECT(cumulative == 3);

  // WebSocket connections are not reused.
  client->openWebSocket(zc::str("/websocket"), HttpHeaders(headerTable)).wait(waitScope);
  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 3);

  // Errored connections are not reused.
  doRequest().wait(waitScope);
  ZC_EXPECT(count == 1);
  ZC_EXPECT(cumulative == 4);
  client->request(HttpMethod::GET, zc::str("/throw"), HttpHeaders(headerTable))
      .response.wait(waitScope)
      .body->readAllBytes()
      .wait(waitScope);
  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 4);

  // Connections where we failed to read the full response body are not reused.
  doRequest().wait(waitScope);
  ZC_EXPECT(count == 1);
  ZC_EXPECT(cumulative == 5);
  client->request(HttpMethod::GET, zc::str("/foo"), HttpHeaders(headerTable))
      .response.wait(waitScope);
  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 5);

  // Connections where we didn't even wait for the response headers are not reused.
  doRequest().wait(waitScope);
  ZC_EXPECT(count == 1);
  ZC_EXPECT(cumulative == 6);
  client->request(HttpMethod::GET, zc::str("/foo"), HttpHeaders(headerTable));
  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 6);

  // Connections where we failed to write the full request body are not reused.
  doRequest().wait(waitScope);
  ZC_EXPECT(count == 1);
  ZC_EXPECT(cumulative == 7);
  client->request(HttpMethod::POST, zc::str("/foo"), HttpHeaders(headerTable), size_t(6))
      .response.wait(waitScope)
      .body->readAllBytes()
      .wait(waitScope);
  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 7);

  // If the server times out the connection, we figure it out on the client.
  doRequest().wait(waitScope);

  // TODO(someday): Figure out why the following poll is necessary for the test to pass on Windows
  //   and Mac.  Without it, it seems that the request's connection never starts, so the
  //   subsequent advanceTo() does not actually time out the connection.
  waitScope.poll();

  ZC_EXPECT(count == 1);
  ZC_EXPECT(cumulative == 8);
  serverTimer.advanceTo(serverTimer.now() + serverSettings.pipelineTimeout * 2);
  waitScope.poll();
  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 8);

  // Can still make requests.
  doRequest().wait(waitScope);
  ZC_EXPECT(count == 1);
  ZC_EXPECT(cumulative == 9);
}

ZC_TEST("HttpClient disable connection reuse") {
  ZC_HTTP_TEST_SETUP_IO;
  ZC_HTTP_TEST_SETUP_LOOPBACK_LISTENER_AND_ADDR;

  zc::TimerImpl serverTimer(zc::origin<zc::TimePoint>());
  zc::TimerImpl clientTimer(zc::origin<zc::TimePoint>());
  HttpHeaderTable headerTable;

  DummyService service(headerTable);
  HttpServerSettings serverSettings;
  HttpServer server(serverTimer, headerTable, service, serverSettings);
  auto listenTask = server.listenHttp(*listener);

  uint count = 0;
  uint cumulative = 0;
  CountingNetworkAddress countingAddr(*addr, count, cumulative);

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.idleTimeout = 0 * zc::SECONDS;
  auto client = newHttpClient(clientTimer, headerTable, countingAddr, clientSettings);

  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 0);

  uint i = 0;
  auto doRequest = [&]() {
    uint n = i++;
    return client->request(HttpMethod::GET, zc::str("/", n), HttpHeaders(headerTable))
        .response
        .then([](HttpClient::Response&& response) {
          auto promise = response.body->readAllText();
          return promise.attach(zc::mv(response.body));
        })
        .then([n](zc::String body) { ZC_EXPECT(body == zc::str("null:/", n)); });
  };

  // Each serial request gets its own connection.
  doRequest().wait(waitScope);
  doRequest().wait(waitScope);
  doRequest().wait(waitScope);
  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 3);

  // Each parallel request gets its own connection.
  auto req1 = doRequest();
  auto req2 = doRequest();
  req1.wait(waitScope);
  req2.wait(waitScope);
  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 5);
}

ZC_TEST("HttpClient concurrency limiting") {
#if ZC_HTTP_TEST_USE_OS_PIPE && !__linux__
  // On Windows and Mac, OS event delivery is not always immediate, and that seems to make this
  // test flakey. On Linux, events are always immediately delivered. For now, we compile the test
  // but we don't run it outside of Linux. We do run the in-memory-pipes version on all OSs since
  // that mode shouldn't depend on kernel behavior at all.
  return;
#endif

  ZC_HTTP_TEST_SETUP_IO;
  ZC_HTTP_TEST_SETUP_LOOPBACK_LISTENER_AND_ADDR;

  zc::TimerImpl serverTimer(zc::origin<zc::TimePoint>());
  zc::TimerImpl clientTimer(zc::origin<zc::TimePoint>());
  HttpHeaderTable headerTable;

  DummyService service(headerTable);
  HttpServerSettings serverSettings;
  HttpServer server(serverTimer, headerTable, service, serverSettings);
  auto listenTask = server.listenHttp(*listener);

  uint count = 0;
  uint cumulative = 0;
  CountingNetworkAddress countingAddr(*addr, count, cumulative);

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.idleTimeout = 0 * zc::SECONDS;
  auto innerClient = newHttpClient(clientTimer, headerTable, countingAddr, clientSettings);

  struct CallbackEvent {
    uint runningCount;
    uint pendingCount;

    bool operator==(const CallbackEvent& other) const {
      return runningCount == other.runningCount && pendingCount == other.pendingCount;
    }
    // TODO(someday): Can use default spaceship operator in C++20:
    // auto operator<=>(const CallbackEvent&) const = default;
  };

  zc::Vector<CallbackEvent> callbackEvents;
  auto callback = [&](uint runningCount, uint pendingCount) {
    callbackEvents.add(CallbackEvent{runningCount, pendingCount});
  };
  auto client = newConcurrencyLimitingHttpClient(*innerClient, 1, zc::mv(callback));

  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 0);

  uint i = 0;
  auto doRequest = [&]() {
    uint n = i++;
    return client->request(HttpMethod::GET, zc::str("/", n), HttpHeaders(headerTable))
        .response
        .then([](HttpClient::Response&& response) {
          auto promise = response.body->readAllText();
          return promise.attach(zc::mv(response.body));
        })
        .then([n](zc::String body) { ZC_EXPECT(body == zc::str("null:/", n)); });
  };

  // Second connection blocked by first.
  auto req1 = doRequest();

  ZC_EXPECT(callbackEvents == zc::ArrayPtr<const CallbackEvent>({{1, 0}}));
  callbackEvents.clear();

  auto req2 = doRequest();

  // TODO(someday): Figure out why this poll() is necessary on Windows and macOS.
  waitScope.poll();

  ZC_EXPECT(req1.poll(waitScope));
  ZC_EXPECT(!req2.poll(waitScope));
  ZC_EXPECT(count == 1);
  ZC_EXPECT(cumulative == 1);
  ZC_EXPECT(callbackEvents == zc::ArrayPtr<const CallbackEvent>({{1, 1}}));
  callbackEvents.clear();

  // Releasing first connection allows second to start.
  req1.wait(waitScope);
  ZC_EXPECT(req2.poll(waitScope));
  ZC_EXPECT(count == 1);
  ZC_EXPECT(cumulative == 2);
  ZC_EXPECT(callbackEvents == zc::ArrayPtr<const CallbackEvent>({{1, 0}}));
  callbackEvents.clear();

  req2.wait(waitScope);
  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 2);
  ZC_EXPECT(callbackEvents == zc::ArrayPtr<const CallbackEvent>({{0, 0}}));
  callbackEvents.clear();

  // Using body stream after releasing blocked response promise throws no exception
  auto req3 = doRequest();
  {
    zc::Own<zc::AsyncOutputStream> req4Body;
    {
      auto req4 = client->request(HttpMethod::GET, zc::str("/", ++i), HttpHeaders(headerTable));
      waitScope.poll();
      req4Body = zc::mv(req4.body);
    }
    auto writePromise = req4Body->write("a"_zcb);
    ZC_EXPECT(!writePromise.poll(waitScope));
  }
  req3.wait(waitScope);
  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 3);

  // Similar connection limiting for web sockets
  // TODO(someday): Figure out why the sequencing of websockets events does
  // not work correctly on Windows (and maybe macOS?).  The solution is not as
  // simple as inserting poll()s as above, since doing so puts the websocket in
  // a state that trips a "previous HTTP message body incomplete" assertion,
  // while trying to write 500 network response.
  callbackEvents.clear();
  auto ws1 = zc::heap(client->openWebSocket(zc::str("/websocket"), HttpHeaders(headerTable)));
  ZC_EXPECT(callbackEvents == zc::ArrayPtr<const CallbackEvent>({{1, 0}}));
  callbackEvents.clear();
  auto ws2 = zc::heap(client->openWebSocket(zc::str("/websocket"), HttpHeaders(headerTable)));
  ZC_EXPECT(ws1->poll(waitScope));
  ZC_EXPECT(!ws2->poll(waitScope));
  ZC_EXPECT(count == 1);
  ZC_EXPECT(cumulative == 4);
  ZC_EXPECT(callbackEvents == zc::ArrayPtr<const CallbackEvent>({{1, 1}}));
  callbackEvents.clear();

  {
    auto response1 = ws1->wait(waitScope);
    ZC_EXPECT(!ws2->poll(waitScope));
    ZC_EXPECT(callbackEvents == zc::ArrayPtr<const CallbackEvent>({}));
  }
  ZC_EXPECT(ws2->poll(waitScope));
  ZC_EXPECT(count == 1);
  ZC_EXPECT(cumulative == 5);
  ZC_EXPECT(callbackEvents == zc::ArrayPtr<const CallbackEvent>({{1, 0}}));
  callbackEvents.clear();
  {
    auto response2 = ws2->wait(waitScope);
    ZC_EXPECT(callbackEvents == zc::ArrayPtr<const CallbackEvent>({}));
  }
  ZC_EXPECT(count == 0);
  ZC_EXPECT(cumulative == 5);
  ZC_EXPECT(callbackEvents == zc::ArrayPtr<const CallbackEvent>({{0, 0}}));
}

ZC_TEST("HttpClientImpl connect()") {
  ZC_HTTP_TEST_SETUP_IO;
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable headerTable;
  auto client = newHttpClient(headerTable, *pipe.ends[0]);

  auto req = client->connect("foo:123", HttpHeaders(headerTable), {});

  char buffer[16]{};
  auto readPromise = req.connection->tryRead(buffer, 16, 16);

  expectRead(*pipe.ends[1], "CONNECT foo:123 HTTP/1.1\r\n\r\n").wait(waitScope);

  pipe.ends[1]->write("HTTP/1.1 200 OK\r\n\r\nthis is the"_zcb).wait(waitScope);

  ZC_EXPECT(!readPromise.poll(waitScope));

  zc::Promise<void> writePromise = nullptr;
  writePromise = pipe.ends[1]->write(" connection content!!"_zcb);

  ZC_ASSERT(readPromise.poll(waitScope));
  ZC_ASSERT(readPromise.wait(waitScope) == 16);
  ZC_EXPECT(zc::str(zc::ArrayPtr<char>(buffer)) == "this is the conn"_zc);

  ZC_EXPECT(req.connection->tryRead(buffer, 16, 16).wait(waitScope) == 16);
  ZC_EXPECT(zc::str(zc::ArrayPtr<char>(buffer)) == "ection content!!"_zc);

  ZC_ASSERT(writePromise.poll(waitScope));
  writePromise.wait(waitScope);
}

#if ZC_HTTP_TEST_USE_OS_PIPE
// This test relies on access to the network.
ZC_TEST("NetworkHttpClient connect impl") {
  ZC_HTTP_TEST_SETUP_IO;
  auto listener1 =
      io.provider->getNetwork().parseAddress("localhost", 0).wait(io.waitScope)->listen();

  auto ignored ZC_UNUSED =
      listener1->accept()
          .then([](Own<zc::AsyncIoStream> stream) {
            auto buffer = zc::str("test");
            return stream->write(buffer.asBytes()).attach(zc::mv(stream), zc::mv(buffer));
          })
          .eagerlyEvaluate(nullptr);

  HttpClientSettings clientSettings;
  zc::TimerImpl clientTimer(zc::origin<zc::TimePoint>());
  HttpHeaderTable headerTable;
  auto client =
      newHttpClient(clientTimer, headerTable, io.provider->getNetwork(), zc::none, clientSettings);
  auto request =
      client->connect(zc::str("localhost:", listener1->getPort()), HttpHeaders(headerTable), {});

  auto buf = zc::heapArray<char>(4);
  return request.connection->tryRead(buf.begin(), 1, buf.size())
      .then([buf = zc::mv(buf)](size_t count) {
        ZC_ASSERT(count == 4);
        ZC_ASSERT(zc::str(buf.asChars()) == "test");
      })
      .attach(zc::mv(request.connection))
      .wait(io.waitScope);
}
#endif

#if ZC_HTTP_TEST_USE_OS_PIPE
// TODO(someday): Implement mock zc::Network for userspace version of this test?
ZC_TEST("HttpClient multi host") {
  auto io = zc::setupAsyncIo();

  zc::TimerImpl serverTimer(zc::origin<zc::TimePoint>());
  zc::TimerImpl clientTimer(zc::origin<zc::TimePoint>());
  HttpHeaderTable headerTable;

  auto listener1 =
      io.provider->getNetwork().parseAddress("localhost", 0).wait(io.waitScope)->listen();
  auto listener2 =
      io.provider->getNetwork().parseAddress("localhost", 0).wait(io.waitScope)->listen();
  DummyService service(headerTable);
  HttpServer server(serverTimer, headerTable, service);
  auto listenTask1 = server.listenHttp(*listener1);
  auto listenTask2 = server.listenHttp(*listener2);

  uint count = 0, addrCount = 0;
  uint tlsCount = 0, tlsAddrCount = 0;
  ConnectionCountingNetwork countingNetwork(io.provider->getNetwork(), count, addrCount);
  ConnectionCountingNetwork countingTlsNetwork(io.provider->getNetwork(), tlsCount, tlsAddrCount);

  HttpClientSettings clientSettings;
  auto client =
      newHttpClient(clientTimer, headerTable, countingNetwork, countingTlsNetwork, clientSettings);

  ZC_EXPECT(count == 0);

  uint i = 0;
  auto doRequest = [&](bool tls, uint port) {
    uint n = i++;
    // We stick a double-slash in the URL to test that it doesn't get coalesced into one slash,
    // which was a bug in the past.
    return client
        ->request(HttpMethod::GET,
                  zc::str((tls ? "https://localhost:" : "http://localhost:"), port, "//", n),
                  HttpHeaders(headerTable))
        .response
        .then([](HttpClient::Response&& response) {
          auto promise = response.body->readAllText();
          return promise.attach(zc::mv(response.body));
        })
        .then([n, port](zc::String body) {
          ZC_EXPECT(body == zc::str("localhost:", port, "://", n), body, port, n);
        });
  };

  uint port1 = listener1->getPort();
  uint port2 = listener2->getPort();

  // We can do several requests in a row to the same host and only have one connection.
  doRequest(false, port1).wait(io.waitScope);
  doRequest(false, port1).wait(io.waitScope);
  doRequest(false, port1).wait(io.waitScope);
  ZC_EXPECT(count == 1);
  ZC_EXPECT(tlsCount == 0);
  ZC_EXPECT(addrCount == 1);
  ZC_EXPECT(tlsAddrCount == 0);

  // Request a different host, and now we have two connections.
  doRequest(false, port2).wait(io.waitScope);
  ZC_EXPECT(count == 2);
  ZC_EXPECT(tlsCount == 0);
  ZC_EXPECT(addrCount == 2);
  ZC_EXPECT(tlsAddrCount == 0);

  // Try TLS.
  doRequest(true, port1).wait(io.waitScope);
  ZC_EXPECT(count == 2);
  ZC_EXPECT(tlsCount == 1);
  ZC_EXPECT(addrCount == 2);
  ZC_EXPECT(tlsAddrCount == 1);

  // Try first host again, no change in connection count.
  doRequest(false, port1).wait(io.waitScope);
  ZC_EXPECT(count == 2);
  ZC_EXPECT(tlsCount == 1);
  ZC_EXPECT(addrCount == 2);
  ZC_EXPECT(tlsAddrCount == 1);

  // Multiple requests in parallel forces more connections to that host.
  auto promise1 = doRequest(false, port1);
  auto promise2 = doRequest(false, port1);
  promise1.wait(io.waitScope);
  promise2.wait(io.waitScope);
  ZC_EXPECT(count == 3);
  ZC_EXPECT(tlsCount == 1);
  ZC_EXPECT(addrCount == 2);
  ZC_EXPECT(tlsAddrCount == 1);

  // Let everything expire.
  clientTimer.advanceTo(clientTimer.now() + clientSettings.idleTimeout * 2);
  io.waitScope.poll();
  ZC_EXPECT(count == 0);
  ZC_EXPECT(tlsCount == 0);
  ZC_EXPECT(addrCount == 0);
  ZC_EXPECT(tlsAddrCount == 0);

  // We can still request those hosts again.
  doRequest(false, port1).wait(io.waitScope);
  ZC_EXPECT(count == 1);
  ZC_EXPECT(tlsCount == 0);
  ZC_EXPECT(addrCount == 1);
  ZC_EXPECT(tlsAddrCount == 0);
}
#endif

// -----------------------------------------------------------------------------

#if ZC_HTTP_TEST_USE_OS_PIPE
// This test only makes sense using the real network.
ZC_TEST("HttpClient to capnproto.org") {
  auto io = zc::setupAsyncIo();

  auto maybeConn =
      io.provider->getNetwork()
          .parseAddress("capnproto.org", 80)
          .then([](zc::Own<zc::NetworkAddress> addr) {
            auto promise = addr->connect();
            return promise.attach(zc::mv(addr));
          })
          .then(
              [](zc::Own<zc::AsyncIoStream>&& connection) -> zc::Maybe<zc::Own<zc::AsyncIoStream>> {
                return zc::mv(connection);
              },
              [](zc::Exception&& e) -> zc::Maybe<zc::Own<zc::AsyncIoStream>> {
                ZC_LOG(WARNING, "skipping test because couldn't connect to capnproto.org");
                return zc::none;
              })
          .wait(io.waitScope);

  ZC_IF_SOME(conn, maybeConn) {
    // Successfully connected to capnproto.org. Try doing GET /. We expect to get a redirect to
    // HTTPS, because what kind of horrible web site would serve in plaintext, really?

    HttpHeaderTable table;
    auto client = newHttpClient(table, *conn);

    HttpHeaders headers(table);
    headers.set(HttpHeaderId::HOST, "capnproto.org");

    auto response = client->request(HttpMethod::GET, "/", headers).response.wait(io.waitScope);
    ZC_EXPECT(response.statusCode / 100 == 3);
    auto location = ZC_ASSERT_NONNULL(response.headers->get(HttpHeaderId::LOCATION));
    ZC_EXPECT(location == "https://capnproto.org/");

    auto body = response.body->readAllText().wait(io.waitScope);
  }
}
#endif

// =======================================================================================
// Misc bugfix tests

class ReadCancelHttpService final : public HttpService {
  // HttpService that tries to read all request data but cancels after 1ms and sends a response.
public:
  ReadCancelHttpService(zc::Timer& timer, HttpHeaderTable& headerTable)
      : timer(timer), headerTable(headerTable) {}

  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& responseSender) override {
    if (method == HttpMethod::POST) {
      // Try to read all content, but cancel after 1ms.

      // Actually, we can't literally cancel mid-read, because this leaves the stream in an
      // unknown state which requires closing the connection. Instead, we know that the sender
      // will send 5 bytes, so we read that, then pause.
      static char junk[5];
      return requestBody.read(junk, 5)
          .then([]() -> zc::Promise<void> { return zc::NEVER_DONE; })
          .exclusiveJoin(timer.afterDelay(1 * zc::MILLISECONDS))
          .then([this, &responseSender]() {
            responseSender.send(408, "Request Timeout", zc::HttpHeaders(headerTable), uint64_t(0));
          });
    } else {
      responseSender.send(200, "OK", zc::HttpHeaders(headerTable), uint64_t(0));
      return zc::READY_NOW;
    }
  }

private:
  zc::Timer& timer;
  HttpHeaderTable& headerTable;
};

ZC_TEST("canceling a length stream mid-read correctly discards rest of request") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  ReadCancelHttpService service(timer, table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  {
    static constexpr zc::StringPtr REQUEST =
        "POST / HTTP/1.1\r\n"
        "Content-Length: 6\r\n"
        "\r\n"
        "fooba"_zc;  // incomplete
    pipe.ends[1]->write(REQUEST.asBytes()).wait(waitScope);

    auto promise = expectRead(*pipe.ends[1],
                              "HTTP/1.1 408 Request Timeout\r\n"
                              "Content-Length: 0\r\n"
                              "\r\n"_zc);

    ZC_EXPECT(!promise.poll(waitScope));

    // Trigger timeout, then response should be sent.
    timer.advanceTo(timer.now() + 1 * zc::MILLISECONDS);
    ZC_ASSERT(promise.poll(waitScope));
    promise.wait(waitScope);
  }

  // We left our request stream hanging. The server will try to read and discard the request body.
  // Let's give it the rest of the data, followed by a second request.
  {
    static constexpr zc::StringPtr REQUEST =
        "r"
        "GET / HTTP/1.1\r\n"
        "\r\n"_zc;
    pipe.ends[1]->write(REQUEST.asBytes()).wait(waitScope);

    auto promise = expectRead(*pipe.ends[1],
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Length: 0\r\n"
                              "\r\n"_zc);
    ZC_ASSERT(promise.poll(waitScope));
    promise.wait(waitScope);
  }
}

ZC_TEST("canceling a chunked stream mid-read correctly discards rest of request") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  ReadCancelHttpService service(timer, table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  {
    static constexpr zc::StringPtr REQUEST =
        "POST / HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "6\r\n"
        "fooba"_zc;  // incomplete chunk
    pipe.ends[1]->write(REQUEST.asBytes()).wait(waitScope);

    auto promise = expectRead(*pipe.ends[1],
                              "HTTP/1.1 408 Request Timeout\r\n"
                              "Content-Length: 0\r\n"
                              "\r\n"_zc);

    ZC_EXPECT(!promise.poll(waitScope));

    // Trigger timeout, then response should be sent.
    timer.advanceTo(timer.now() + 1 * zc::MILLISECONDS);
    ZC_ASSERT(promise.poll(waitScope));
    promise.wait(waitScope);
  }

  // We left our request stream hanging. The server will try to read and discard the request body.
  // Let's give it the rest of the data, followed by a second request.
  {
    static constexpr zc::StringPtr REQUEST =
        "r\r\n"
        "4a\r\n"
        "this is some text that is the body of a chunk and not a valid chunk header\r\n"
        "0\r\n"
        "\r\n"
        "GET / HTTP/1.1\r\n"
        "\r\n"_zc;
    pipe.ends[1]->write(REQUEST.asBytes()).wait(waitScope);

    auto promise = expectRead(*pipe.ends[1],
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Length: 0\r\n"
                              "\r\n"_zc);
    ZC_ASSERT(promise.poll(waitScope));
    promise.wait(waitScope);
  }
}

ZC_TEST("drain() doesn't lose bytes when called at the wrong moment") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  DummyService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttpCleanDrain(*pipe.ends[0]);

  // Do a regular request.
  static constexpr zc::StringPtr REQUEST =
      "GET / HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "\r\n"_zc;
  pipe.ends[1]->write(REQUEST.asBytes()).wait(waitScope);
  expectRead(*pipe.ends[1],
             "HTTP/1.1 200 OK\r\n"
             "Content-Length: 13\r\n"
             "\r\n"
             "example.com:/"_zc)
      .wait(waitScope);

  // Make sure the server is blocked on the next read from the socket.
  zc::Promise<void>(zc::NEVER_DONE).poll(waitScope);

  // Now simultaneously deliver a new request AND drain the socket.
  auto drainPromise = server.drain();
  static constexpr zc::StringPtr REQUEST2 =
      "GET /foo HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "\r\n"_zc;
  pipe.ends[1]->write(REQUEST2.asBytes()).wait(waitScope);

#if ZC_HTTP_TEST_USE_OS_PIPE
  // In the case of an OS pipe, the drain will complete before any data is read from the socket.
  drainPromise.wait(waitScope);

  // The HTTP server should indicate the connection was released but still valid.
  ZC_ASSERT(listenTask.wait(waitScope));

  // The request will not have been read off the socket. We can read it now.
  pipe.ends[1]->shutdownWrite();
  ZC_EXPECT(pipe.ends[0]->readAllText().wait(waitScope) == REQUEST2);

#else
  // In the case of an in-memory pipe, the write() will have delivered bytes directly to the
  // destination buffer synchronously, which means that the server must handle the request
  // before draining.
  ZC_EXPECT(!drainPromise.poll(waitScope));

  // The HTTP request should get a response.
  expectRead(*pipe.ends[1],
             "HTTP/1.1 200 OK\r\n"
             "Content-Length: 16\r\n"
             "\r\n"
             "example.com:/foo"_zc)
      .wait(waitScope);

  // Now the drain completes.
  drainPromise.wait(waitScope);

  // The HTTP server should indicate the connection was released but still valid.
  ZC_ASSERT(listenTask.wait(waitScope));
#endif
}

ZC_TEST("drain() does not cancel the first request on a new connection") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  DummyService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttpCleanDrain(*pipe.ends[0]);

  // Request a drain(). It won't complete, because the newly-connected socket is considered to have
  // an in-flight request.
  auto drainPromise = server.drain();
  ZC_EXPECT(!drainPromise.poll(waitScope));

  // Deliver the request.
  static auto REQUEST2 =
      "GET /foo HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "\r\n"_zcb;
  pipe.ends[1]->write(REQUEST2).wait(waitScope);

  // It should get a response.
  expectRead(*pipe.ends[1],
             "HTTP/1.1 200 OK\r\n"
             "Content-Length: 16\r\n"
             "\r\n"
             "example.com:/foo"_zc)
      .wait(waitScope);

  // Now the drain completes.
  drainPromise.wait(waitScope);

  // The HTTP server should indicate the connection was released but still valid.
  ZC_ASSERT(listenTask.wait(waitScope));
}

ZC_TEST("drain() when NOT using listenHttpCleanDrain() sends Connection: close header") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  DummyService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  // Request a drain(). It won't complete, because the newly-connected socket is considered to have
  // an in-flight request.
  auto drainPromise = server.drain();
  ZC_EXPECT(!drainPromise.poll(waitScope));

  // Deliver the request.
  static auto REQUEST2 =
      "GET /foo HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "\r\n"_zcb;
  pipe.ends[1]->write(REQUEST2).wait(waitScope);

  // It should get a response.
  expectRead(*pipe.ends[1],
             "HTTP/1.1 200 OK\r\n"
             "Connection: close\r\n"
             "Content-Length: 16\r\n"
             "\r\n"
             "example.com:/foo"_zc)
      .wait(waitScope);

  // And then EOF.
  auto rest = pipe.ends[1]->readAllText();
  ZC_ASSERT(rest.poll(waitScope));
  ZC_EXPECT(rest.wait(waitScope) == nullptr);

  // The drain task and listen task are done.
  drainPromise.wait(waitScope);
  listenTask.wait(waitScope);
}

class BrokenConnectionListener final : public zc::ConnectionReceiver {
public:
  void fulfillOne(zc::Own<zc::AsyncIoStream> stream) { fulfiller->fulfill(zc::mv(stream)); }

  zc::Promise<zc::Own<zc::AsyncIoStream>> accept() override {
    auto paf = zc::newPromiseAndFulfiller<zc::Own<zc::AsyncIoStream>>();
    fulfiller = zc::mv(paf.fulfiller);
    return zc::mv(paf.promise);
  }

  uint getPort() override { ZC_UNIMPLEMENTED("not used"); }

private:
  zc::Own<zc::PromiseFulfiller<zc::Own<zc::AsyncIoStream>>> fulfiller;
};

class BrokenConnection final : public zc::AsyncIoStream {
public:
  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return ZC_EXCEPTION(FAILED, "broken");
  }
  Promise<void> write(ArrayPtr<const byte> buffer) override {
    return ZC_EXCEPTION(FAILED, "broken");
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return ZC_EXCEPTION(FAILED, "broken");
  }
  Promise<void> whenWriteDisconnected() override { return zc::NEVER_DONE; }

  void shutdownWrite() override {}
};

ZC_TEST(
    "HttpServer.listenHttp() doesn't prematurely terminate if an accepted connection is broken") {
  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  HttpHeaderTable table;
  DummyService service(table);
  HttpServer server(timer, table, service);

  BrokenConnectionListener listener;
  auto promise = server.listenHttp(listener).eagerlyEvaluate(nullptr);

  // Loop is waiting for a connection.
  ZC_ASSERT(!promise.poll(waitScope));

  ZC_EXPECT_LOG(ERROR, "failed: broken");
  listener.fulfillOne(zc::heap<BrokenConnection>());

  // The loop should not have stopped, even though the connection was broken.
  ZC_ASSERT(!promise.poll(waitScope));
}

ZC_TEST("HttpServer handles disconnected exception for clients disconnecting after headers") {
  // This test case reproduces a race condition where a client could disconnect after the server
  // sent response headers but before it sent the response body, resulting in a broken pipe
  // "disconnected" exception when writing the body.  The default handler for application errors
  // tells the server to ignore "disconnected" exceptions and close the connection, but code
  // after the handler exercised the broken connection, causing the server loop to instead fail
  // with a "failed" exception.

  ZC_HTTP_TEST_SETUP_IO;
  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  class SendErrorHttpService final : public HttpService {
    // HttpService that serves an error page via sendError().
  public:
    SendErrorHttpService(HttpHeaderTable& headerTable) : headerTable(headerTable) {}
    zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                              zc::AsyncInputStream& requestBody,
                              Response& responseSender) override {
      return responseSender.sendError(404, "Not Found", headerTable);
    }

  private:
    HttpHeaderTable& headerTable;
  };

  class DisconnectingAsyncIoStream final : public zc::AsyncIoStream {
  public:
    DisconnectingAsyncIoStream(AsyncIoStream& inner) : inner(inner) {}

    Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
      return inner.read(buffer, minBytes, maxBytes);
    }
    Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
      return inner.tryRead(buffer, minBytes, maxBytes);
    }

    Maybe<uint64_t> tryGetLength() override { return inner.tryGetLength(); }

    Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
      return inner.pumpTo(output, amount);
    }

    Promise<void> write(ArrayPtr<const byte> buffer) override {
      int writeId = writeCount++;
      if (writeId == 0) {
        // Allow first write (headers) to succeed.
        auto promise = inner.write(buffer).then([this]() { inner.shutdownWrite(); });
        return promise;
      } else if (writeId == 1) {
        // Fail subsequent write (body) with a disconnected exception.
        return ZC_EXCEPTION(DISCONNECTED, "a_disconnected_exception");
      } else {
        ZC_FAIL_ASSERT("Unexpected write");
      }
    }
    Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
      return inner.write(pieces);
    }

    Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
      return inner.tryPumpFrom(input, amount);
    }

    Promise<void> whenWriteDisconnected() override { return inner.whenWriteDisconnected(); }

    void shutdownWrite() override { return inner.shutdownWrite(); }

    void abortRead() override { return inner.abortRead(); }

    void getsockopt(int level, int option, void* value, uint* length) override {
      return inner.getsockopt(level, option, value, length);
    }
    void setsockopt(int level, int option, const void* value, uint length) override {
      return inner.setsockopt(level, option, value, length);
    }

    void getsockname(struct sockaddr* addr, uint* length) override {
      return inner.getsockname(addr, length);
    }
    void getpeername(struct sockaddr* addr, uint* length) override {
      return inner.getsockname(addr, length);
    }

    int writeCount = 0;

  private:
    zc::AsyncIoStream& inner;
  };

  class TestErrorHandler : public HttpServerErrorHandler {
  public:
    zc::Promise<void> handleApplicationError(
        zc::Exception exception, zc::Maybe<zc::HttpService::Response&> response) override {
      applicationErrorCount++;
      if (exception.getType() == zc::Exception::Type::DISCONNECTED) {
        // Tell HttpServer to ignore disconnected exceptions (the default behavior).
        return zc::READY_NOW;
      }
      ZC_FAIL_ASSERT("Unexpected application error type", exception.getType());
    }

    int applicationErrorCount = 0;
  };

  TestErrorHandler testErrorHandler;
  HttpServerSettings settings{};
  settings.errorHandler = testErrorHandler;

  HttpHeaderTable table;
  SendErrorHttpService service(table);
  HttpServer server(timer, table, service, settings);

  auto stream = zc::heap<DisconnectingAsyncIoStream>(*pipe.ends[0]);
  auto listenPromise = server.listenHttpCleanDrain(*stream);

  static constexpr auto request = "GET / HTTP/1.1\r\n\r\n"_zc;
  pipe.ends[1]->write(request.asBytes()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  // Client races to read headers but not body, then disconnects.  (Note that the following code
  // doesn't reliably reproduce the race condition by itself -- DisconnectingAsyncIoStream is
  // needed to ensure the disconnected exception throws on the correct write promise.)
  expectRead(*pipe.ends[1],
             "HTTP/1.1 404 Not Found\r\n"
             "Content-Length: 9\r\n"
             "\r\n"_zc)
      .wait(waitScope);
  pipe.ends[1] = nullptr;

  // The race condition failure would manifest as a "previous HTTP message body incomplete"
  // "FAILED" exception here:
  bool canReuse = listenPromise.wait(waitScope);

  ZC_ASSERT(!canReuse);
  ZC_ASSERT(stream->writeCount == 2);
  ZC_ASSERT(testErrorHandler.applicationErrorCount == 1);
}

// =======================================================================================
// CONNECT tests

class ConnectEchoService final : public HttpService {
  // A simple CONNECT echo. It will always accept, and whatever data it
  // receives will be echoed back.
public:
  ConnectEchoService(HttpHeaderTable& headerTable, uint statusCodeToSend = 200)
      : headerTable(headerTable), statusCodeToSend(statusCodeToSend) {
    ZC_ASSERT(statusCodeToSend >= 200 && statusCodeToSend < 300);
  }

  uint connectCount = 0;

  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    ZC_UNIMPLEMENTED("Regular HTTP requests are not implemented here.");
  }

  zc::Promise<void> connect(zc::StringPtr host, const HttpHeaders& headers,
                            zc::AsyncIoStream& connection, ConnectResponse& response,
                            zc::HttpConnectSettings settings) override {
    connectCount++;
    response.accept(statusCodeToSend, "OK", HttpHeaders(headerTable));
    return connection.pumpTo(connection).ignoreResult();
  }

private:
  HttpHeaderTable& headerTable;
  uint statusCodeToSend;
};

class ConnectRejectService final : public HttpService {
  // A simple CONNECT implementation that always rejects.
public:
  ConnectRejectService(HttpHeaderTable& headerTable, uint statusCodeToSend = 400)
      : headerTable(headerTable), statusCodeToSend(statusCodeToSend) {
    ZC_ASSERT(statusCodeToSend >= 300);
  }

  uint connectCount = 0;

  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    ZC_UNIMPLEMENTED("Regular HTTP requests are not implemented here.");
  }

  zc::Promise<void> connect(zc::StringPtr host, const HttpHeaders& headers,
                            zc::AsyncIoStream& connection, ConnectResponse& response,
                            zc::HttpConnectSettings settings) override {
    connectCount++;
    auto out = response.reject(statusCodeToSend, "Failed"_zc, HttpHeaders(headerTable), 4);
    return out->write("boom"_zcb).attach(zc::mv(out));
  }

private:
  HttpHeaderTable& headerTable;
  uint statusCodeToSend;
};

class ConnectCancelReadService final : public HttpService {
  // A simple CONNECT server that will accept a connection then immediately
  // cancel reading from it to test handling of abrupt termination.
public:
  ConnectCancelReadService(HttpHeaderTable& headerTable) : headerTable(headerTable) {}

  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    ZC_UNIMPLEMENTED("Regular HTTP requests are not implemented here.");
  }

  zc::Promise<void> connect(zc::StringPtr host, const HttpHeaders& headers,
                            zc::AsyncIoStream& connection, ConnectResponse& response,
                            zc::HttpConnectSettings settings) override {
    response.accept(200, "OK", HttpHeaders(headerTable));
    // Return an immediately resolved promise and drop the connection
    return zc::READY_NOW;
  }

private:
  HttpHeaderTable& headerTable;
};

class ConnectCancelWriteService final : public HttpService {
  // A simple CONNECT server that will accept a connection then immediately
  // cancel writing to it to test handling of abrupt termination.
public:
  ConnectCancelWriteService(HttpHeaderTable& headerTable) : headerTable(headerTable) {}

  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    ZC_UNIMPLEMENTED("Regular HTTP requests are not implemented here.");
  }

  zc::Promise<void> connect(zc::StringPtr host, const HttpHeaders& headers,
                            zc::AsyncIoStream& connection, ConnectResponse& response,
                            zc::HttpConnectSettings settings) override {
    response.accept(200, "OK", HttpHeaders(headerTable));
    auto promise ZC_UNUSED = connection.write("hello"_zcb);
    // Return an immediately resolved promise and drop the io
    return zc::READY_NOW;
  }

private:
  HttpHeaderTable& headerTable;
};

class ConnectHttpService final : public HttpService {
  // A CONNECT service that tunnels HTTP requests just to verify that, yes, the CONNECT
  // impl can actually tunnel actual protocols.
public:
  ConnectHttpService(HttpHeaderTable& table)
      : timer(zc::origin<zc::TimePoint>()),
        tunneledService(table),
        server(timer, table, tunneledService) {}

private:
  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    ZC_UNIMPLEMENTED("Regular HTTP requests are not implemented here.");
  }

  zc::Promise<void> connect(zc::StringPtr host, const HttpHeaders& headers,
                            zc::AsyncIoStream& connection, ConnectResponse& response,
                            zc::HttpConnectSettings settings) override {
    response.accept(200, "OK", HttpHeaders(tunneledService.table));
    return server.listenHttp(zc::Own<zc::AsyncIoStream>(&connection, zc::NullDisposer::instance));
  }

  class SimpleHttpService final : public HttpService {
  public:
    SimpleHttpService(HttpHeaderTable& table) : table(table) {}
    zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                              zc::AsyncInputStream& requestBody, Response& response) override {
      auto out = response.send(200, "OK"_zc, HttpHeaders(table));
      return out->write("hello there"_zcb).attach(zc::mv(out));
    }

    HttpHeaderTable& table;
  };

  zc::TimerImpl timer;
  SimpleHttpService tunneledService;
  HttpServer server;
};

class ConnectCloseService final : public HttpService {
  // A simple CONNECT server that will accept a connection then immediately
  // shutdown the write side of the AsyncIoStream to simulate socket disconnection.
public:
  ConnectCloseService(HttpHeaderTable& headerTable) : headerTable(headerTable) {}

  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    ZC_UNIMPLEMENTED("Regular HTTP requests are not implemented here.");
  }

  zc::Promise<void> connect(zc::StringPtr host, const HttpHeaders& headers,
                            zc::AsyncIoStream& connection, ConnectResponse& response,
                            zc::HttpConnectSettings settings) override {
    response.accept(200, "OK", HttpHeaders(headerTable));
    connection.shutdownWrite();
    return zc::READY_NOW;
  }

private:
  HttpHeaderTable& headerTable;
};

ZC_TEST("Simple CONNECT Server works") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  HttpHeaderTable table;
  ConnectEchoService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto msg =
      "CONNECT https://example.org HTTP/1.1\r\n"
      "\r\n"
      "hello"_zcb;

  pipe.ends[1]->write(msg).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1],
             "HTTP/1.1 200 OK\r\n"
             "\r\n"
             "hello"_zc)
      .wait(waitScope);

  expectEnd(*pipe.ends[1]).wait(waitScope);

  listenTask.wait(waitScope);

  ZC_ASSERT(service.connectCount == 1);
}

ZC_TEST("Simple CONNECT Client/Server works") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  HttpHeaderTable table;
  ConnectEchoService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto client = newHttpClient(table, *pipe.ends[1]);

  HttpHeaderTable clientHeaders;
  // Initiates a CONNECT with the echo server. Once established, sends a bit of data
  // and waits for it to be echoed back.
  auto request = client->connect("https://example.org"_zc, HttpHeaders(clientHeaders), {});

  request.status
      .then([io = zc::mv(request.connection)](auto status) mutable {
        ZC_ASSERT(status.statusCode == 200);
        ZC_ASSERT(status.statusText == "OK"_zc);

        auto promises = zc::heapArrayBuilder<zc::Promise<void>>(2);
        promises.add(io->write("hello"_zcb));
        promises.add(expectRead(*io, "hello"_zc));
        return zc::joinPromises(promises.finish()).then([io = zc::mv(io)]() mutable {
          io->shutdownWrite();
          return expectEnd(*io).attach(zc::mv(io));
        });
      })
      .wait(waitScope);

  listenTask.wait(waitScope);

  ZC_ASSERT(service.connectCount == 1);
}

ZC_TEST("CONNECT Server (201 status)") {
  ZC_HTTP_TEST_SETUP_IO;

  // Test that CONNECT works with 2xx status codes that typically do
  // not carry a response payload.

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  HttpHeaderTable table;
  ConnectEchoService service(table, 201);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto msg =
      "CONNECT https://example.org HTTP/1.1\r\n"
      "\r\n"
      "hello"_zcb;

  pipe.ends[1]->write(msg).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1],
             "HTTP/1.1 201 OK\r\n"
             "\r\n"
             "hello"_zc)
      .wait(waitScope);

  expectEnd(*pipe.ends[1]).wait(waitScope);

  listenTask.wait(waitScope);

  ZC_ASSERT(service.connectCount == 1);
}

ZC_TEST("CONNECT Client (204 status)") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  // Test that CONNECT works with 2xx status codes that typically do
  // not carry a response payload.

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  HttpHeaderTable table;
  ConnectEchoService service(table, 204);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto client = newHttpClient(table, *pipe.ends[1]);

  HttpHeaderTable clientHeaders;
  // Initiates a CONNECT with the echo server. Once established, sends a bit of data
  // and waits for it to be echoed back.
  auto request = client->connect("https://example.org"_zc, HttpHeaders(clientHeaders), {});

  request.status
      .then([io = zc::mv(request.connection)](auto status) mutable {
        ZC_ASSERT(status.statusCode == 204);
        ZC_ASSERT(status.statusText == "OK"_zc);

        auto promises = zc::heapArrayBuilder<zc::Promise<void>>(2);
        promises.add(io->write("hello"_zcb));
        promises.add(expectRead(*io, "hello"_zc));

        return zc::joinPromises(promises.finish()).then([io = zc::mv(io)]() mutable {
          io->shutdownWrite();
          return expectEnd(*io).attach(zc::mv(io));
        });
      })
      .wait(waitScope);

  listenTask.wait(waitScope);

  ZC_ASSERT(service.connectCount == 1);
}

ZC_TEST("CONNECT Server rejected") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  HttpHeaderTable table;
  ConnectRejectService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto msg =
      "CONNECT https://example.org HTTP/1.1\r\n"
      "\r\n"
      "hello"_zcb;

  pipe.ends[1]->write(msg).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1],
             "HTTP/1.1 400 Failed\r\n"
             "Connection: close\r\n"
             "Content-Length: 4\r\n"
             "\r\n"
             "boom"_zc)
      .wait(waitScope);

  expectEnd(*pipe.ends[1]).wait(waitScope);

  listenTask.wait(waitScope);

  ZC_ASSERT(service.connectCount == 1);
}

#ifndef ZC_HTTP_TEST_USE_OS_PIPE
ZC_TEST("CONNECT Client rejected") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  HttpHeaderTable table;
  ConnectRejectService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto client = newHttpClient(table, *pipe.ends[1]);

  HttpHeaderTable clientHeaders;
  auto request = client->connect("https://example.org"_zc, HttpHeaders(clientHeaders), {});

  request.status
      .then([](auto status) mutable {
        ZC_ASSERT(status.statusCode == 400);
        ZC_ASSERT(status.statusText == "Failed"_zc);

        auto& errorBody = ZC_ASSERT_NONNULL(status.errorBody);

        return expectRead(*errorBody, "boom"_zc)
            .then([&errorBody = *errorBody]() { return expectEnd(errorBody); })
            .attach(zc::mv(errorBody));
      })
      .wait(waitScope);

  listenTask.wait(waitScope);

  ZC_ASSERT(service.connectCount == 1);
}
#endif

ZC_TEST("CONNECT Server cancels read") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  HttpHeaderTable table;
  ConnectCancelReadService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto msg =
      "CONNECT https://example.org HTTP/1.1\r\n"
      "\r\n"
      "hello"_zcb;

  pipe.ends[1]->write(msg).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1],
             "HTTP/1.1 200 OK\r\n"
             "\r\n"_zc)
      .wait(waitScope);

  expectEnd(*pipe.ends[1]).wait(waitScope);

  listenTask.wait(waitScope);
}

#ifndef ZC_HTTP_TEST_USE_OS_PIPE
ZC_TEST("CONNECT Server cancels read w/client") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  HttpHeaderTable table;
  ConnectCancelReadService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto client = newHttpClient(table, *pipe.ends[1]);
  bool failed = false;

  HttpHeaderTable clientHeaders;
  auto request = client->connect("https://example.org"_zc, HttpHeaders(clientHeaders), {});

  request.status
      .then([&failed, io = zc::mv(request.connection)](auto status) mutable {
        ZC_ASSERT(status.statusCode == 200);
        ZC_ASSERT(status.statusText == "OK"_zc);

        return io->write("hello"_zcb)
            .catch_([&](zc::Exception&& ex) {
              ZC_ASSERT(ex.getType() == zc::Exception::Type::DISCONNECTED);
              failed = true;
            })
            .attach(zc::mv(io));
      })
      .wait(waitScope);

  ZC_ASSERT(failed, "the write promise should have failed");

  listenTask.wait(waitScope);
}
#endif

ZC_TEST("CONNECT Server cancels write") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  HttpHeaderTable table;
  ConnectCancelWriteService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto msg =
      "CONNECT https://example.org HTTP/1.1\r\n"
      "\r\n"
      "hello"_zcb;

  pipe.ends[1]->write(msg).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1],
             "HTTP/1.1 200 OK\r\n"
             "\r\n"_zc)
      .wait(waitScope);

  expectEnd(*pipe.ends[1]).wait(waitScope);

  listenTask.wait(waitScope);
}

#ifndef ZC_HTTP_TEST_USE_OS_PIPE
ZC_TEST("CONNECT Server cancels write w/client") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  HttpHeaderTable table;
  ConnectCancelWriteService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto client = newHttpClient(table, *pipe.ends[1]);

  HttpHeaderTable clientHeaders;
  bool failed = false;
  auto request = client->connect("https://example.org"_zc, HttpHeaders(clientHeaders), {});

  request.status
      .then([&failed, io = zc::mv(request.connection)](auto status) mutable {
        ZC_ASSERT(status.statusCode == 200);
        ZC_ASSERT(status.statusText == "OK"_zc);

        return io->write("hello"_zcb)
            .catch_([&failed](zc::Exception&& ex) mutable {
              ZC_ASSERT(ex.getType() == zc::Exception::Type::DISCONNECTED);
              failed = true;
            })
            .attach(zc::mv(io));
      })
      .wait(waitScope);

  ZC_ASSERT(failed, "the write promise should have failed");

  listenTask.wait(waitScope);
}
#endif

ZC_TEST("CONNECT rejects Transfer-Encoding") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  HttpHeaderTable table;
  ConnectEchoService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto msg =
      "CONNECT https://example.org HTTP/1.1\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "5\r\n"
      "hello"
      "0\r\n"_zcb;

  pipe.ends[1]->write(msg).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1],
             "HTTP/1.1 400 Bad Request\r\n"
             "Connection: close\r\n"
             "Content-Length: 18\r\n"
             "Content-Type: text/plain\r\n"
             "\r\n"
             "ERROR: Bad Request"_zc)
      .wait(waitScope);

  expectEnd(*pipe.ends[1]).wait(waitScope);

  listenTask.wait(waitScope);
}

ZC_TEST("CONNECT rejects Content-Length") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());

  HttpHeaderTable table;
  ConnectEchoService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(zc::mv(pipe.ends[0]));

  auto msg =
      "CONNECT https://example.org HTTP/1.1\r\n"
      "Content-Length: 5\r\n"
      "\r\n"
      "hello"_zcb;

  pipe.ends[1]->write(msg).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1],
             "HTTP/1.1 400 Bad Request\r\n"
             "Connection: close\r\n"
             "Content-Length: 18\r\n"
             "Content-Type: text/plain\r\n"
             "\r\n"
             "ERROR: Bad Request"_zc)
      .wait(waitScope);

  expectEnd(*pipe.ends[1]).wait(waitScope);

  listenTask.wait(waitScope);
}

ZC_TEST("CONNECT HTTP-tunneled-over-CONNECT") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  HttpHeaderTable table;
  ConnectHttpService service(table);
  HttpServer server(timer, table, service);

  auto listenTask ZC_UNUSED = server.listenHttp(zc::mv(pipe.ends[0]));

  auto client = newHttpClient(table, *pipe.ends[1]);

  HttpHeaderTable connectHeaderTable;
  HttpHeaderTable tunneledHeaderTable;
  HttpClientSettings settings;

  auto request = client->connect("https://example.org"_zc, HttpHeaders(connectHeaderTable), {});

  auto text = request.status
                  .then([&tunneledHeaderTable, &settings,
                         io = zc::mv(request.connection)](auto status) mutable {
                    ZC_ASSERT(status.statusCode == 200);
                    ZC_ASSERT(status.statusText == "OK"_zc);
                    auto client =
                        newHttpClient(tunneledHeaderTable, *io, settings).attach(zc::mv(io));

                    return client
                        ->request(HttpMethod::GET, "http://example.org"_zc,
                                  HttpHeaders(tunneledHeaderTable))
                        .response
                        .then([](HttpClient::Response&& response) {
                          return response.body->readAllText().attach(zc::mv(response));
                        })
                        .attach(zc::mv(client));
                  })
                  .wait(waitScope);

  ZC_ASSERT(text == "hello there");
}

ZC_TEST("CONNECT HTTP-tunneled-over-pipelined-CONNECT") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  HttpHeaderTable table;
  ConnectHttpService service(table);
  HttpServer server(timer, table, service);

  auto listenTask ZC_UNUSED = server.listenHttp(zc::mv(pipe.ends[0]));

  auto client = newHttpClient(table, *pipe.ends[1]);

  HttpHeaderTable connectHeaderTable;
  HttpHeaderTable tunneledHeaderTable;
  HttpClientSettings settings;

  auto request = client->connect("https://example.org"_zc, HttpHeaders(connectHeaderTable), {});
  auto conn = zc::mv(request.connection);
  auto proxyClient = newHttpClient(tunneledHeaderTable, *conn, settings).attach(zc::mv(conn));

  auto get = proxyClient->request(HttpMethod::GET, "http://example.org"_zc,
                                  HttpHeaders(tunneledHeaderTable));
  auto text = get.response
                  .then([](HttpClient::Response&& response) mutable {
                    return response.body->readAllText().attach(zc::mv(response));
                  })
                  .attach(zc::mv(proxyClient))
                  .wait(waitScope);

  ZC_ASSERT(text == "hello there");
}

ZC_TEST("CONNECT pipelined via an adapter") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  HttpHeaderTable table;
  ConnectHttpService service(table);
  HttpServer server(timer, table, service);

  auto listenTask ZC_UNUSED = server.listenHttp(zc::mv(pipe.ends[0]));

  bool acceptCalled = false;

  auto client = newHttpClient(table, *pipe.ends[1]);
  auto adaptedService = zc::newHttpService(*client).attach(zc::mv(client));

  // adaptedService is an HttpService that wraps an HttpClient that sends
  // a request to server.

  auto clientPipe = newTwoWayPipe();

  struct ResponseImpl final : public HttpService::ConnectResponse {
    bool& acceptCalled;
    ResponseImpl(bool& acceptCalled) : acceptCalled(acceptCalled) {}
    void accept(uint statusCode, zc::StringPtr statusText, const HttpHeaders& headers) override {
      acceptCalled = true;
    }

    zc::Own<zc::AsyncOutputStream> reject(uint statusCode, zc::StringPtr statusText,
                                          const HttpHeaders& headers,
                                          zc::Maybe<uint64_t> expectedBodySize) override {
      ZC_UNREACHABLE;
    }
  };

  ResponseImpl response(acceptCalled);

  HttpHeaderTable connectHeaderTable;
  HttpHeaderTable tunneledHeaderTable;
  HttpClientSettings settings;

  auto promise = adaptedService
                     ->connect("https://example.org"_zc, HttpHeaders(connectHeaderTable),
                               *clientPipe.ends[0], response, {})
                     .attach(zc::mv(clientPipe.ends[0]));

  auto proxyClient = newHttpClient(tunneledHeaderTable, *clientPipe.ends[1], settings)
                         .attach(zc::mv(clientPipe.ends[1]));

  auto text =
      proxyClient
          ->request(HttpMethod::GET, "http://example.org"_zc, HttpHeaders(tunneledHeaderTable))
          .response
          .then([](HttpClient::Response&& response) mutable {
            return response.body->readAllText().attach(zc::mv(response));
          })
          .wait(waitScope);

  ZC_ASSERT(acceptCalled);
  ZC_ASSERT(text == "hello there");
}

ZC_TEST("CONNECT pipelined via an adapter (reject)") {
  ZC_HTTP_TEST_SETUP_IO;

  auto pipe = ZC_HTTP_TEST_CREATE_2PIPE;

  zc::TimerImpl timer(zc::origin<zc::TimePoint>());
  HttpHeaderTable table;
  ConnectRejectService service(table);
  HttpServer server(timer, table, service);

  auto listenTask ZC_UNUSED = server.listenHttp(zc::mv(pipe.ends[0]));

  bool rejectCalled = false;
  bool failedAsExpected = false;

  auto client = newHttpClient(table, *pipe.ends[1]);
  auto adaptedService = zc::newHttpService(*client).attach(zc::mv(client));

  // adaptedService is an HttpService that wraps an HttpClient that sends
  // a request to server.

  auto clientPipe = newTwoWayPipe();

  struct ResponseImpl final : public HttpService::ConnectResponse {
    bool& rejectCalled;
    zc::OneWayPipe pipe;
    ResponseImpl(bool& rejectCalled) : rejectCalled(rejectCalled), pipe(zc::newOneWayPipe()) {}
    void accept(uint statusCode, zc::StringPtr statusText, const HttpHeaders& headers) override {
      ZC_UNREACHABLE;
    }

    zc::Own<zc::AsyncOutputStream> reject(uint statusCode, zc::StringPtr statusText,
                                          const HttpHeaders& headers,
                                          zc::Maybe<uint64_t> expectedBodySize) override {
      rejectCalled = true;
      return zc::mv(pipe.out);
    }

    zc::Own<zc::AsyncInputStream> getRejectStream() { return zc::mv(pipe.in); }
  };

  ResponseImpl response(rejectCalled);

  HttpHeaderTable connectHeaderTable;
  HttpHeaderTable tunneledHeaderTable;
  HttpClientSettings settings;

  auto promise = adaptedService
                     ->connect("https://example.org"_zc, HttpHeaders(connectHeaderTable),
                               *clientPipe.ends[0], response, {})
                     .attach(zc::mv(clientPipe.ends[0]));

  auto proxyClient = newHttpClient(tunneledHeaderTable, *clientPipe.ends[1], settings)
                         .attach(zc::mv(clientPipe.ends[1]));

  auto text =
      proxyClient
          ->request(HttpMethod::GET, "http://example.org"_zc, HttpHeaders(tunneledHeaderTable))
          .response
          .then(
              [](HttpClient::Response&& response) mutable {
                return response.body->readAllText().attach(zc::mv(response));
              },
              [&](zc::Exception&& ex) -> zc::Promise<zc::String> {
                // We fully expect the stream to fail here.
                if (ex.getDescription() == "stream disconnected prematurely") {
                  failedAsExpected = true;
                }
                return zc::str("ok");
              })
          .wait(waitScope);

  auto rejectStream = response.getRejectStream();

#ifndef ZC_HTTP_TEST_USE_OS_PIPE
  expectRead(*rejectStream, "boom"_zc).wait(waitScope);
#endif

  ZC_ASSERT(rejectCalled);
  ZC_ASSERT(failedAsExpected);
  ZC_ASSERT(text == "ok");
}

struct HttpRangeTestCase {
  zc::StringPtr value;
  uint64_t contentLength;
  zc::OneOf<InitializeableArray<HttpByteRange>, HttpEverythingRange, HttpUnsatisfiableRange>
      expected;

  HttpRangeTestCase(zc::StringPtr value, uint64_t contentLength)
      : value(value), contentLength(contentLength), expected(HttpUnsatisfiableRange{}) {}
  HttpRangeTestCase(zc::StringPtr value, uint64_t contentLength, HttpEverythingRange expected)
      : value(value), contentLength(contentLength), expected(expected) {}
  HttpRangeTestCase(zc::StringPtr value, uint64_t contentLength,
                    InitializeableArray<HttpByteRange> expected)
      : value(value), contentLength(contentLength), expected(zc::mv(expected)) {}
};

ZC_TEST("Range header parsing") {
  static const HttpRangeTestCase RANGE_TEST_CASES[]{
      // ===== Unit =====
      // Check case-insensitive unit must be "bytes" and ignores whitespace
      {"bytes=0-1"_zcc, 2, HttpEverythingRange{}},
      {"BYTES    =0-1"_zcc, 2, HttpEverythingRange{}},
      {"     bYtEs=0-1"_zcc, 4, {{0, 1}}},
      {"    Bytes        =0-1"_zcc, 2, HttpEverythingRange{}},
      // Check fails with other units
      {"nibbles=0-1"_zcc, 2},

      // ===== Interval =====
      // Check valid ranges accepted
      {"bytes=0-1"_zcc, 8, {{0, 1}}},
      {"bytes=  2 -   7   "_zcc, 8, {{2, 7}}},
      {"bytes=5-5"_zcc, 8, {{5, 5}}},
      // Check start after end rejected
      {"bytes=1-0"_zcc, 2},
      // Check start after content rejected
      {"bytes=2-3"_zcc, 2},
      {"bytes=5-7"_zcc, 2},
      // Check end after content clamped
      {"bytes=0-2"_zcc, 2, HttpEverythingRange{}},
      {"bytes=1-5"_zcc, 3, {{1, 2}}},
      // Check multiple valid ranges accepted
      {"bytes=  1-3  , 6-7,10-11"_zcc, 12, {{1, 3}, {6, 7}, {10, 11}}},
      // Check overlapping ranges accepted
      {"bytes=0-2,1-3"_zcc, 5, {{0, 2}, {1, 3}}},
      // Check unsatisfiable ranges ignored
      {"bytes=1-2,7-8"_zcc, 5, {{1, 2}}},

      // ===== Prefix =====
      // Check valid ranges accepted
      {"bytes=2-"_zcc, 8, {{2, 7}}},
      {"bytes=5-"_zcc, 6, {{5, 5}}},
      // Check start after content rejected
      {"bytes=2-"_zcc, 2},
      {"bytes=5-"_zcc, 2},
      // Check multiple valid ranges accepted
      {"bytes=  1-  ,6-, 10-11 "_zcc, 12, {{1, 11}, {6, 11}, {10, 11}}},

      // ===== Suffix =====
      // Check valid ranges accepted
      {"bytes=-2"_zcc, 8, {{6, 7}}},
      {"bytes=-6"_zcc, 7, {{1, 6}}},
      // Check start after content truncated and entire response response
      {"bytes=-7"_zcc, 7, HttpEverythingRange{}},
      {"bytes=-10"_zcc, 5, HttpEverythingRange{}},
      // Check if any range returns entire response, other ranges ignored
      {"bytes=0-1,-5,2-3"_zcc, 5, HttpEverythingRange{}},
      // Check unsatisfiable empty range ignored
      {"bytes=-0"_zcc, 2},
      {"bytes=0-1,-0,2-3"_zcc, 4, {{0, 1}, {2, 3}}},

      // ===== Invalid =====
      // Check range with no start or end rejected
      {"bytes=-"_zcc, 2},
      // Check range with no dash rejected
      {"bytes=0"_zcc, 2},
      // Check empty range rejected
      {"bytes=0-1,"_zcc, 2},
      // Check no ranges rejected
      {"bytes="_zcc, 2},
      {"bytes"_zcc, 2},
  };

  for (auto& testCase : RANGE_TEST_CASES) {
    auto ranges = tryParseHttpRangeHeader(testCase.value, testCase.contentLength);
    ZC_SWITCH_ONEOF(testCase.expected) {
      ZC_CASE_ONEOF(expectedArray, InitializeableArray<HttpByteRange>) {
        ZC_SWITCH_ONEOF(ranges) {
          ZC_CASE_ONEOF(array, zc::Array<HttpByteRange>) { ZC_ASSERT(array == expectedArray); }
          ZC_CASE_ONEOF_DEFAULT {
            ZC_FAIL_ASSERT("Expected ", testCase.value, testCase.contentLength, "to return ranges");
          }
        }
      }
      ZC_CASE_ONEOF(_, HttpEverythingRange) {
        ZC_SWITCH_ONEOF(ranges) {
          ZC_CASE_ONEOF(_, HttpEverythingRange) {}
          ZC_CASE_ONEOF_DEFAULT {
            ZC_FAIL_ASSERT("Expected ", testCase.value, testCase.contentLength,
                           "to return everything");
          }
        }
      }
      ZC_CASE_ONEOF(_, HttpUnsatisfiableRange) {
        ZC_SWITCH_ONEOF(ranges) {
          ZC_CASE_ONEOF(_, HttpUnsatisfiableRange) {}
          ZC_CASE_ONEOF_DEFAULT {
            ZC_FAIL_ASSERT("Expected ", testCase.value, testCase.contentLength,
                           "to be unsatisfiable");
          }
        }
      }
    }
  }
}

}  // namespace
}  // namespace zc
