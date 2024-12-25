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

#include "src/zc/http/http.h"

#include <cstdlib>
#include <deque>
#include <map>
#include <queue>
#include <unordered_map>

#include "src/zc/core/debug.h"
#include "src/zc/core/encoding.h"
#include "src/zc/core/exception.h"
#include "src/zc/core/string.h"
#include "src/zc/http/url.h"
#include "src/zc/parse/char.h"
#if ZC_HAS_ZLIB
#include <src/zc/zip/zlib.h>
#endif  // ZC_HAS_ZLIB

namespace zc {

// =======================================================================================
// SHA-1 implementation from https://github.com/clibs/sha1
//
// The WebSocket standard depends on SHA-1. ARRRGGGHHHHH.
//
// Any old checksum would have served the purpose, or hell, even just returning the header
// verbatim. But NO, they decided to throw a whole complicated hash algorithm in there, AND
// THEY CHOSE A BROKEN ONE THAT WE OTHERWISE WOULDN'T NEED ANYMORE.
//
// TODO(cleanup): Move this to a shared hashing library. Maybe. Or maybe don't, because no one
//   should be using SHA-1 anymore.
//
// THIS USAGE IS NOT SECURITY SENSITIVE. IF YOU REPORT A SECURITY ISSUE BECAUSE YOU SAW SHA1 IN THE
// SOURCE CODE I WILL MAKE FUN OF YOU.

/*
SHA-1 in C
By Steve Reid <steve@edmweb.com>
100% Public Domain
Test Vectors (from FIPS PUB 180-1)
"abc"
  A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D
"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
  84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1
A million repetitions of "a"
  34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F
*/

/* #define LITTLE_ENDIAN * This should be #define'd already, if true. */
/* #define SHA1HANDSOFF * Copies data before messing with it. */

#define SHA1HANDSOFF

typedef struct {
  uint32_t state[5];
  uint32_t count[2];
  unsigned char buffer[64];
} SHA1_CTX;

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

/* blk0() and blk() perform the initial expand. */
/* I got the idea of expanding during the round function from SSLeay */
#if BYTE_ORDER == LITTLE_ENDIAN
#define blk0(i) \
  (block->l[i] = (rol(block->l[i], 24) & 0xFF00FF00) | (rol(block->l[i], 8) & 0x00FF00FF))
#elif BYTE_ORDER == BIG_ENDIAN
#define blk0(i) block->l[i]
#else
#error "Endianness not defined!"
#endif
#define blk(i)                                                               \
  (block->l[i & 15] = rol(block->l[(i + 13) & 15] ^ block->l[(i + 8) & 15] ^ \
                              block->l[(i + 2) & 15] ^ block->l[i & 15],     \
                          1))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define R0(v, w, x, y, z, i)                                   \
  z += ((w & (x ^ y)) ^ y) + blk0(i) + 0x5A827999 + rol(v, 5); \
  w = rol(w, 30);
#define R1(v, w, x, y, z, i)                                  \
  z += ((w & (x ^ y)) ^ y) + blk(i) + 0x5A827999 + rol(v, 5); \
  w = rol(w, 30);
#define R2(v, w, x, y, z, i)                          \
  z += (w ^ x ^ y) + blk(i) + 0x6ED9EBA1 + rol(v, 5); \
  w = rol(w, 30);
#define R3(v, w, x, y, z, i)                                        \
  z += (((w | x) & y) | (w & x)) + blk(i) + 0x8F1BBCDC + rol(v, 5); \
  w = rol(w, 30);
#define R4(v, w, x, y, z, i)                          \
  z += (w ^ x ^ y) + blk(i) + 0xCA62C1D6 + rol(v, 5); \
  w = rol(w, 30);

/* Hash a single 512-bit block. This is the core of the algorithm. */

void SHA1Transform(uint32_t state[5], const unsigned char buffer[64]) {
  uint32_t a, b, c, d, e;

  typedef union {
    unsigned char c[64];
    uint32_t l[16];
  } CHAR64LONG16;

#ifdef SHA1HANDSOFF
  CHAR64LONG16 block[1]; /* use array to appear as a pointer */

  memcpy(block, buffer, 64);
#else
  /* The following had better never be used because it causes the
   * pointer-to-const buffer to be cast into a pointer to non-const.
   * And the result is written through.  I threw a "const" in, hoping
   * this will cause a diagnostic.
   */
  CHAR64LONG16* block = (const CHAR64LONG16*)buffer;
#endif
  /* Copy context->state[] to working vars */
  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];
  e = state[4];
  /* 4 rounds of 20 operations each. Loop unrolled. */
  R0(a, b, c, d, e, 0);
  R0(e, a, b, c, d, 1);
  R0(d, e, a, b, c, 2);
  R0(c, d, e, a, b, 3);
  R0(b, c, d, e, a, 4);
  R0(a, b, c, d, e, 5);
  R0(e, a, b, c, d, 6);
  R0(d, e, a, b, c, 7);
  R0(c, d, e, a, b, 8);
  R0(b, c, d, e, a, 9);
  R0(a, b, c, d, e, 10);
  R0(e, a, b, c, d, 11);
  R0(d, e, a, b, c, 12);
  R0(c, d, e, a, b, 13);
  R0(b, c, d, e, a, 14);
  R0(a, b, c, d, e, 15);
  R1(e, a, b, c, d, 16);
  R1(d, e, a, b, c, 17);
  R1(c, d, e, a, b, 18);
  R1(b, c, d, e, a, 19);
  R2(a, b, c, d, e, 20);
  R2(e, a, b, c, d, 21);
  R2(d, e, a, b, c, 22);
  R2(c, d, e, a, b, 23);
  R2(b, c, d, e, a, 24);
  R2(a, b, c, d, e, 25);
  R2(e, a, b, c, d, 26);
  R2(d, e, a, b, c, 27);
  R2(c, d, e, a, b, 28);
  R2(b, c, d, e, a, 29);
  R2(a, b, c, d, e, 30);
  R2(e, a, b, c, d, 31);
  R2(d, e, a, b, c, 32);
  R2(c, d, e, a, b, 33);
  R2(b, c, d, e, a, 34);
  R2(a, b, c, d, e, 35);
  R2(e, a, b, c, d, 36);
  R2(d, e, a, b, c, 37);
  R2(c, d, e, a, b, 38);
  R2(b, c, d, e, a, 39);
  R3(a, b, c, d, e, 40);
  R3(e, a, b, c, d, 41);
  R3(d, e, a, b, c, 42);
  R3(c, d, e, a, b, 43);
  R3(b, c, d, e, a, 44);
  R3(a, b, c, d, e, 45);
  R3(e, a, b, c, d, 46);
  R3(d, e, a, b, c, 47);
  R3(c, d, e, a, b, 48);
  R3(b, c, d, e, a, 49);
  R3(a, b, c, d, e, 50);
  R3(e, a, b, c, d, 51);
  R3(d, e, a, b, c, 52);
  R3(c, d, e, a, b, 53);
  R3(b, c, d, e, a, 54);
  R3(a, b, c, d, e, 55);
  R3(e, a, b, c, d, 56);
  R3(d, e, a, b, c, 57);
  R3(c, d, e, a, b, 58);
  R3(b, c, d, e, a, 59);
  R4(a, b, c, d, e, 60);
  R4(e, a, b, c, d, 61);
  R4(d, e, a, b, c, 62);
  R4(c, d, e, a, b, 63);
  R4(b, c, d, e, a, 64);
  R4(a, b, c, d, e, 65);
  R4(e, a, b, c, d, 66);
  R4(d, e, a, b, c, 67);
  R4(c, d, e, a, b, 68);
  R4(b, c, d, e, a, 69);
  R4(a, b, c, d, e, 70);
  R4(e, a, b, c, d, 71);
  R4(d, e, a, b, c, 72);
  R4(c, d, e, a, b, 73);
  R4(b, c, d, e, a, 74);
  R4(a, b, c, d, e, 75);
  R4(e, a, b, c, d, 76);
  R4(d, e, a, b, c, 77);
  R4(c, d, e, a, b, 78);
  R4(b, c, d, e, a, 79);
  /* Add the working vars back into context.state[] */
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  /* Wipe variables */
  a = b = c = d = e = 0;
#ifdef SHA1HANDSOFF
  memset(block, '\0', sizeof(block));
#endif
}

/* SHA1Init - Initialize new context */

void SHA1Init(SHA1_CTX* context) {
  /* SHA1 initialization constants */
  context->state[0] = 0x67452301;
  context->state[1] = 0xEFCDAB89;
  context->state[2] = 0x98BADCFE;
  context->state[3] = 0x10325476;
  context->state[4] = 0xC3D2E1F0;
  context->count[0] = context->count[1] = 0;
}

/* Run your data through this. */

void SHA1Update(SHA1_CTX* context, const unsigned char* data, uint32_t len) {
  uint32_t i;

  uint32_t j;

  j = context->count[0];
  if ((context->count[0] += len << 3) < j) context->count[1]++;
  context->count[1] += (len >> 29);
  j = (j >> 3) & 63;
  if ((j + len) > 63) {
    memcpy(&context->buffer[j], data, (i = 64 - j));
    SHA1Transform(context->state, context->buffer);
    for (; i + 63 < len; i += 64) { SHA1Transform(context->state, &data[i]); }
    j = 0;
  } else
    i = 0;
  memcpy(&context->buffer[j], &data[i], len - i);
}

/* Add padding and return the message digest. */

void SHA1Final(unsigned char digest[20], SHA1_CTX* context) {
  unsigned i;

  unsigned char finalcount[8]{};

  unsigned char c;

#if 0 /* untested "improvement" by DHR */
    /* Convert context->count to a sequence of bytes
     * in finalcount.  Second element first, but
     * big-endian order within element.
     * But we do it all backwards.
     */
    unsigned char *fcp = &finalcount[8];
    for (i = 0; i < 2; i++)
    {
        uint32_t t = context->count[i];
        int j;
        for (j = 0; j < 4; t >>= 8, j++)
            *--fcp = (unsigned char) t}
#else
  for (i = 0; i < 8; i++) {
    finalcount[i] = (unsigned char)((context->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) &
                                    255); /* Endian independent */
  }
#endif
  c = 0200;
  SHA1Update(context, &c, 1);
  while ((context->count[0] & 504) != 448) {
    c = 0000;
    SHA1Update(context, &c, 1);
  }
  SHA1Update(context, finalcount, 8); /* Should cause a SHA1Transform() */
  for (i = 0; i < 20; i++) {
    digest[i] = (unsigned char)((context->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
  }
  /* Wipe variables */
  memset(context, '\0', sizeof(*context));
  memset(&finalcount, '\0', sizeof(finalcount));
}

// End SHA-1 implementation.
// =======================================================================================

static const char* METHOD_NAMES[] = {
#define METHOD_NAME(id) #id,
    ZC_HTTP_FOR_EACH_METHOD(METHOD_NAME)
#undef METHOD_NAME
};

zc::StringPtr ZC_STRINGIFY(HttpMethod method) {
  auto index = static_cast<uint>(method);
  ZC_ASSERT(index < size(METHOD_NAMES), "invalid HTTP method");

  return METHOD_NAMES[index];
}

zc::StringPtr ZC_STRINGIFY(HttpConnectMethod method) { return "CONNECT"_zc; }

static zc::Maybe<zc::OneOf<HttpMethod, HttpConnectMethod>> consumeHttpMethod(char*& ptr) {
  char* p = ptr;

#define EXPECT_REST(prefix, suffix)                                                         \
  if (strncmp(p, #suffix, sizeof(#suffix) - 1) == 0) {                                      \
    ptr = p + (sizeof(#suffix) - 1);                                                        \
    return zc::Maybe<zc::OneOf<HttpMethod, HttpConnectMethod>>(HttpMethod::prefix##suffix); \
  } else {                                                                                  \
    return zc::none;                                                                        \
  }

  switch (*p++) {
    case 'A':
      EXPECT_REST(A, CL)
    case 'C':
      switch (*p++) {
        case 'H':
          EXPECT_REST(CH, ECKOUT)
        case 'O':
          switch (*p++) {
            case 'P':
              EXPECT_REST(COP, Y)
            case 'N':
              if (strncmp(p, "NECT", 4) == 0) {
                ptr = p + 4;
                return zc::Maybe<zc::OneOf<HttpMethod, HttpConnectMethod>>(HttpConnectMethod());
              } else {
                return zc::none;
              }
            default:
              return zc::none;
          }
        default:
          return zc::none;
      }
    case 'D':
      EXPECT_REST(D, ELETE)
    case 'G':
      EXPECT_REST(G, ET)
    case 'H':
      EXPECT_REST(H, EAD)
    case 'L':
      EXPECT_REST(L, OCK)
    case 'M':
      switch (*p++) {
        case 'E':
          EXPECT_REST(ME, RGE)
        case 'K':
          switch (*p++) {
            case 'A':
              EXPECT_REST(MKA, CTIVITY)
            case 'C':
              EXPECT_REST(MKC, OL)
            default:
              return zc::none;
          }
        case 'O':
          EXPECT_REST(MO, VE)
        case 'S':
          EXPECT_REST(MS, EARCH)
        default:
          return zc::none;
      }
    case 'N':
      EXPECT_REST(N, OTIFY)
    case 'O':
      EXPECT_REST(O, PTIONS)
    case 'P':
      switch (*p++) {
        case 'A':
          EXPECT_REST(PA, TCH)
        case 'O':
          EXPECT_REST(PO, ST)
        case 'R':
          if (*p++ != 'O' || *p++ != 'P') return zc::none;
          switch (*p++) {
            case 'F':
              EXPECT_REST(PROPF, IND)
            case 'P':
              EXPECT_REST(PROPP, ATCH)
            default:
              return zc::none;
          }
        case 'U':
          switch (*p++) {
            case 'R':
              EXPECT_REST(PUR, GE)
            case 'T':
              EXPECT_REST(PUT, )
            default:
              return zc::none;
          }
        default:
          return zc::none;
      }
    case 'R':
      EXPECT_REST(R, EPORT)
    case 'S':
      switch (*p++) {
        case 'E':
          EXPECT_REST(SE, ARCH)
        case 'U':
          EXPECT_REST(SU, BSCRIBE)
        default:
          return zc::none;
      }
    case 'T':
      EXPECT_REST(T, RACE)
    case 'U':
      if (*p++ != 'N') return zc::none;
      switch (*p++) {
        case 'L':
          EXPECT_REST(UNL, OCK)
        case 'S':
          EXPECT_REST(UNS, UBSCRIBE)
        default:
          return zc::none;
      }
    default:
      return zc::none;
  }
#undef EXPECT_REST
}

zc::Maybe<HttpMethod> tryParseHttpMethod(zc::StringPtr name) {
  ZC_IF_SOME(method, tryParseHttpMethodAllowingConnect(name)) {
    ZC_SWITCH_ONEOF(method) {
      ZC_CASE_ONEOF(m, HttpMethod) { return m; }
      ZC_CASE_ONEOF(m, HttpConnectMethod) { return zc::none; }
    }
    ZC_UNREACHABLE;
  }
  else { return zc::none; }
}

zc::Maybe<zc::OneOf<HttpMethod, HttpConnectMethod>> tryParseHttpMethodAllowingConnect(
    zc::StringPtr name) {
  // const_cast OK because we don't actually access it. consumeHttpMethod() is also called by some
  // code later than explicitly needs to use a non-const pointer.
  char* ptr = const_cast<char*>(name.begin());
  auto result = consumeHttpMethod(ptr);
  if (*ptr == '\0') {
    return result;
  } else {
    return zc::none;
  }
}

// =======================================================================================

namespace {

constexpr char WEBSOCKET_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
// From RFC6455.

static zc::String generateWebSocketAccept(zc::StringPtr key) {
  // WebSocket demands we do a SHA-1 here. ARRGHH WHY SHA-1 WHYYYYYY?
  SHA1_CTX ctx;
  byte digest[20]{};
  SHA1Init(&ctx);
  SHA1Update(&ctx, key.asBytes().begin(), key.size());
  SHA1Update(&ctx, reinterpret_cast<const byte*>(WEBSOCKET_GUID), strlen(WEBSOCKET_GUID));
  SHA1Final(digest, &ctx);
  return zc::encodeBase64(digest);
}

constexpr auto HTTP_SEPARATOR_CHARS = zc::parse::anyOfChars("()<>@,;:\\\"/[]?={} \t");
// RFC2616 section 2.2: https://www.w3.org/Protocols/rfc2616/rfc2616-sec2.html#sec2.2

constexpr auto HTTP_TOKEN_CHARS = zc::parse::controlChar.orChar('\x7f')
                                      .orGroup(zc::parse::whitespaceChar)
                                      .orGroup(HTTP_SEPARATOR_CHARS)
                                      .invert();
// RFC2616 section 2.2: https://www.w3.org/Protocols/rfc2616/rfc2616-sec2.html#sec2.2

constexpr auto HTTP_HEADER_NAME_CHARS = HTTP_TOKEN_CHARS;
// RFC2616 section 4.2: https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2

static void requireValidHeaderName(zc::StringPtr name) {
  for (char c : name) {
    ZC_REQUIRE(HTTP_HEADER_NAME_CHARS.contains(c), "invalid header name", name);
  }
}

static void requireValidHeaderValue(zc::StringPtr value) {
  ZC_REQUIRE(HttpHeaders::isValidHeaderValue(value), "invalid header value",
             zc::encodeCEscape(value));
}

static const char* BUILTIN_HEADER_NAMES[] = {
// Indexed by header ID, which includes connection headers, so we include those names too.
#define HEADER_NAME(id, name) name,
    ZC_HTTP_FOR_EACH_BUILTIN_HEADER(HEADER_NAME)
#undef HEADER_NAME
};

}  // namespace

#define HEADER_ID(id, name) constexpr uint HttpHeaders::BuiltinIndices::id;
ZC_HTTP_FOR_EACH_BUILTIN_HEADER(HEADER_ID)
#undef HEADER_ID

#define DEFINE_HEADER(id, name) \
  const HttpHeaderId HttpHeaderId::id(nullptr, HttpHeaders::BuiltinIndices::id);
ZC_HTTP_FOR_EACH_BUILTIN_HEADER(DEFINE_HEADER)
#undef DEFINE_HEADER

zc::StringPtr HttpHeaderId::toString() const {
  if (table == nullptr) {
    ZC_ASSERT(id < zc::size(BUILTIN_HEADER_NAMES));
    return BUILTIN_HEADER_NAMES[id];
  } else {
    return table->idToString(*this);
  }
}

namespace {

struct HeaderNameHash {
  size_t operator()(zc::StringPtr s) const {
    size_t result = 5381;
    for (byte b : s.asBytes()) {
      // Masking bit 0x20 makes our hash case-insensitive while conveniently avoiding any
      // collisions that would matter for header names.
      result = ((result << 5) + result) ^ (b & ~0x20);
    }
    return result;
  }

  bool operator()(zc::StringPtr a, zc::StringPtr b) const {
    // TODO(perf): I wonder if we can beat strcasecmp() by masking bit 0x20 from each byte. We'd
    //   need to prohibit one of the technically-legal characters '^' or '~' from header names
    //   since they'd otherwise be ambiguous, but otherwise there is no ambiguity.
#if _MSC_VER
    return _stricmp(a.cStr(), b.cStr()) == 0;
#else
    return strcasecmp(a.cStr(), b.cStr()) == 0;
#endif
  }
};

}  // namespace

struct HttpHeaderTable::IdsByNameMap {
  // TODO(perf): If we were cool we could maybe use a perfect hash here, since our hashtable is
  //   static once built.

  std::unordered_map<zc::StringPtr, uint, HeaderNameHash, HeaderNameHash> map;
};

HttpHeaderTable::Builder::Builder() : table(zc::heap<HttpHeaderTable>()) {
  table->buildStatus = BuildStatus::BUILDING;
}

HttpHeaderId HttpHeaderTable::Builder::add(zc::StringPtr name) {
  requireValidHeaderName(name);

  auto insertResult = table->idsByName->map.insert(std::make_pair(name, table->namesById.size()));
  if (insertResult.second) { table->namesById.add(name); }
  return HttpHeaderId(table, insertResult.first->second);
}

HttpHeaderTable::HttpHeaderTable() : idsByName(zc::heap<IdsByNameMap>()) {
#define ADD_HEADER(id, name) \
  namesById.add(name);       \
  idsByName->map.insert(std::make_pair(name, HttpHeaders::BuiltinIndices::id));
  ZC_HTTP_FOR_EACH_BUILTIN_HEADER(ADD_HEADER);
#undef ADD_HEADER
}
HttpHeaderTable::~HttpHeaderTable() noexcept(false) {}

zc::Maybe<HttpHeaderId> HttpHeaderTable::stringToId(zc::StringPtr name) const {
  auto iter = idsByName->map.find(name);
  if (iter == idsByName->map.end()) {
    return zc::none;
  } else {
    return HttpHeaderId(this, iter->second);
  }
}

// =======================================================================================

bool HttpHeaders::isValidHeaderValue(zc::StringPtr value) {
  for (char c : value) {
    // While the HTTP spec suggests that only printable ASCII characters are allowed in header
    // values, reality has a different opinion. See: https://github.com/httpwg/http11bis/issues/19
    // We follow the browsers' lead.
    if (c == '\0' || c == '\r' || c == '\n') { return false; }
  }

  return true;
}

HttpHeaders::HttpHeaders(const HttpHeaderTable& table)
    : table(&table), indexedHeaders(zc::heapArray<zc::StringPtr>(table.idCount())) {
  ZC_ASSERT(table.isReady(),
            "HttpHeaders object was constructed from "
            "HttpHeaderTable that wasn't fully built yet at the time of construction");
}

void HttpHeaders::clear() {
  for (auto& header : indexedHeaders) { header = nullptr; }

  unindexedHeaders.clear();
}

size_t HttpHeaders::size() const {
  size_t result = unindexedHeaders.size();
  for (auto i : zc::indices(indexedHeaders)) {
    if (indexedHeaders[i] != nullptr) { ++result; }
  }
  return result;
}

HttpHeaders HttpHeaders::clone() const {
  HttpHeaders result(*table);

  for (auto i : zc::indices(indexedHeaders)) {
    if (indexedHeaders[i] != nullptr) {
      result.indexedHeaders[i] = result.cloneToOwn(indexedHeaders[i]);
    }
  }

  result.unindexedHeaders.resize(unindexedHeaders.size());
  for (auto i : zc::indices(unindexedHeaders)) {
    result.unindexedHeaders[i].name = result.cloneToOwn(unindexedHeaders[i].name);
    result.unindexedHeaders[i].value = result.cloneToOwn(unindexedHeaders[i].value);
  }

  return result;
}

HttpHeaders HttpHeaders::cloneShallow() const {
  HttpHeaders result(*table);

  for (auto i : zc::indices(indexedHeaders)) {
    if (indexedHeaders[i] != nullptr) { result.indexedHeaders[i] = indexedHeaders[i]; }
  }

  result.unindexedHeaders.resize(unindexedHeaders.size());
  for (auto i : zc::indices(unindexedHeaders)) { result.unindexedHeaders[i] = unindexedHeaders[i]; }

  return result;
}

zc::StringPtr HttpHeaders::cloneToOwn(zc::StringPtr str) {
  auto copy = zc::heapString(str);
  zc::StringPtr result = copy;
  ownedStrings.add(copy.releaseArray());
  return result;
}

namespace {

template <char... chars>
constexpr bool fastCaseCmp(const char* actual);

}  // namespace

bool HttpHeaders::isWebSocket() const {
  return fastCaseCmp<'w', 'e', 'b', 's', 'o', 'c', 'k', 'e', 't'>(
      get(HttpHeaderId::UPGRADE).orDefault(nullptr).cStr());
}

void HttpHeaders::set(HttpHeaderId id, zc::StringPtr value) {
  id.requireFrom(*table);
  requireValidHeaderValue(value);

  indexedHeaders[id.id] = value;
}

void HttpHeaders::set(HttpHeaderId id, zc::String&& value) {
  set(id, zc::StringPtr(value));
  takeOwnership(zc::mv(value));
}

void HttpHeaders::add(zc::StringPtr name, zc::StringPtr value) {
  requireValidHeaderName(name);
  requireValidHeaderValue(value);

  addNoCheck(name, value);
}

void HttpHeaders::add(zc::StringPtr name, zc::String&& value) {
  add(name, zc::StringPtr(value));
  takeOwnership(zc::mv(value));
}

void HttpHeaders::add(zc::String&& name, zc::String&& value) {
  add(zc::StringPtr(name), zc::StringPtr(value));
  takeOwnership(zc::mv(name));
  takeOwnership(zc::mv(value));
}

void HttpHeaders::addNoCheck(zc::StringPtr name, zc::StringPtr value) {
  ZC_IF_SOME(id, table->stringToId(name)) {
    if (indexedHeaders[id.id] == nullptr) {
      indexedHeaders[id.id] = value;
    } else {
      // Duplicate HTTP headers are equivalent to the values being separated by a comma.

#if _MSC_VER
      if (_stricmp(name.cStr(), "set-cookie") == 0) {
#else
      if (strcasecmp(name.cStr(), "set-cookie") == 0) {
#endif
        // Uh-oh, Set-Cookie will be corrupted if we try to concatenate it. We'll make it an
        // unindexed header, which is weird, but the alternative is guaranteed corruption, so...
        // TODO(cleanup): Maybe HttpHeaders should just special-case set-cookie in general?
        unindexedHeaders.add(Header{name, value});
      } else {
        auto concat = zc::str(indexedHeaders[id.id], ", ", value);
        indexedHeaders[id.id] = concat;
        ownedStrings.add(concat.releaseArray());
      }
    }
  }
  else { unindexedHeaders.add(Header{name, value}); }
}

void HttpHeaders::takeOwnership(zc::String&& string) { ownedStrings.add(string.releaseArray()); }
void HttpHeaders::takeOwnership(zc::Array<char>&& chars) { ownedStrings.add(zc::mv(chars)); }
void HttpHeaders::takeOwnership(HttpHeaders&& otherHeaders) {
  for (auto& str : otherHeaders.ownedStrings) { ownedStrings.add(zc::mv(str)); }
  otherHeaders.ownedStrings.clear();
}

// -----------------------------------------------------------------------------

static inline const char* skipSpace(const char* p) {
  for (;;) {
    switch (*p) {
      case '\t':
      case ' ':
        ++p;
        break;
      default:
        return p;
    }
  }
}
static inline char* skipSpace(char* p) {
  return const_cast<char*>(skipSpace(const_cast<const char*>(p)));
}

static zc::Maybe<zc::StringPtr> consumeWord(char*& ptr) {
  char* start = skipSpace(ptr);
  char* p = start;

  for (;;) {
    switch (*p) {
      case '\0':
        ptr = p;
        return zc::StringPtr(start, p);

      case '\t':
      case ' ': {
        char* end = p++;
        ptr = p;
        *end = '\0';
        return zc::StringPtr(start, end);
      }

      case '\n':
      case '\r':
        // Not expecting EOL!
        return zc::none;

      default:
        ++p;
        break;
    }
  }
}

static zc::Maybe<uint> consumeNumber(const char*& ptr) {
  const char* start = skipSpace(ptr);
  const char* p = start;

  uint result = 0;

  for (;;) {
    const char c = *p;
    if ('0' <= c && c <= '9') {
      result = result * 10 + (c - '0');
      ++p;
    } else {
      if (p == start) return zc::none;
      ptr = p;
      return result;
    }
  }
}
static zc::Maybe<uint> consumeNumber(char*& ptr) {
  const char* constPtr = ptr;
  auto result = consumeNumber(constPtr);
  ptr = const_cast<char*>(constPtr);
  return result;
}

static zc::StringPtr consumeLine(char*& ptr) {
  char* start = skipSpace(ptr);
  char* p = start;

  for (;;) {
    switch (*p) {
      case '\0':
        ptr = p;
        return zc::StringPtr(start, p);

      case '\r': {
        char* end = p++;
        if (*p == '\n') ++p;

        if (*p == ' ' || *p == '\t') {
          // Whoa, continuation line. These are deprecated, but historically a line starting with
          // a space was treated as a continuation of the previous line. The behavior should be
          // the same as if the \r\n were replaced with spaces, so let's do that here to prevent
          // confusion later.
          *end = ' ';
          p[-1] = ' ';
          break;
        }

        ptr = p;
        *end = '\0';
        return zc::StringPtr(start, end);
      }

      case '\n': {
        char* end = p++;

        if (*p == ' ' || *p == '\t') {
          // Whoa, continuation line. These are deprecated, but historically a line starting with
          // a space was treated as a continuation of the previous line. The behavior should be
          // the same as if the \n were replaced with spaces, so let's do that here to prevent
          // confusion later.
          *end = ' ';
          break;
        }

        ptr = p;
        *end = '\0';
        return zc::StringPtr(start, end);
      }

      default:
        ++p;
        break;
    }
  }
}

static zc::Maybe<zc::StringPtr> consumeHeaderName(char*& ptr) {
  // Do NOT skip spaces before the header name. Leading spaces indicate a continuation line; they
  // should have been handled in consumeLine().
  char* p = ptr;

  char* start = p;
  while (HTTP_HEADER_NAME_CHARS.contains(*p)) ++p;
  char* end = p;

  p = skipSpace(p);

  if (end == start || *p != ':') return zc::none;
  ++p;

  p = skipSpace(p);

  *end = '\0';
  ptr = p;
  return zc::StringPtr(start, end);
}

static char* trimHeaderEnding(zc::ArrayPtr<char> content) {
  // Trim off the trailing \r\n from a header blob.

  if (content.size() < 2) return nullptr;

  // Remove trailing \r\n\r\n and replace with \0 sentinel char.
  char* end = content.end();

  if (end[-1] != '\n') return nullptr;
  --end;
  if (end[-1] == '\r') --end;
  *end = '\0';

  return end;
}

HttpHeaders::RequestOrProtocolError HttpHeaders::tryParseRequest(zc::ArrayPtr<char> content) {
  ZC_SWITCH_ONEOF(tryParseRequestOrConnect(content)) {
    ZC_CASE_ONEOF(request, Request) { return zc::mv(request); }
    ZC_CASE_ONEOF(error, ProtocolError) { return zc::mv(error); }
    ZC_CASE_ONEOF(connect, ConnectRequest) {
      return ProtocolError{501, "Not Implemented", "Unrecognized request method.", content};
    }
  }
  ZC_UNREACHABLE;
}

HttpHeaders::RequestConnectOrProtocolError HttpHeaders::tryParseRequestOrConnect(
    zc::ArrayPtr<char> content) {
  char* end = trimHeaderEnding(content);
  if (end == nullptr) {
    return ProtocolError{400, "Bad Request", "Request headers have no terminal newline.", content};
  }

  char* ptr = content.begin();

  HttpHeaders::RequestConnectOrProtocolError result;

  ZC_IF_SOME(method, consumeHttpMethod(ptr)) {
    if (*ptr != ' ' && *ptr != '\t') {
      return ProtocolError{501, "Not Implemented", "Unrecognized request method.", content};
    }
    ++ptr;

    zc::Maybe<StringPtr> path;
    ZC_IF_SOME(p, consumeWord(ptr)) { path = p; }
    else { return ProtocolError{400, "Bad Request", "Invalid request line.", content}; }

    ZC_SWITCH_ONEOF(method) {
      ZC_CASE_ONEOF(m, HttpMethod) { result = HttpHeaders::Request{m, ZC_ASSERT_NONNULL(path)}; }
      ZC_CASE_ONEOF(m, HttpConnectMethod) {
        result = HttpHeaders::ConnectRequest{ZC_ASSERT_NONNULL(path)};
      }
    }
  }
  else { return ProtocolError{501, "Not Implemented", "Unrecognized request method.", content}; }

  // Ignore rest of line. Don't care about "HTTP/1.1" or whatever.
  consumeLine(ptr);

  if (!parseHeaders(ptr, end)) {
    return ProtocolError{400, "Bad Request", "The headers sent by your client are not valid.",
                         content};
  }

  return result;
}

HttpHeaders::ResponseOrProtocolError HttpHeaders::tryParseResponse(zc::ArrayPtr<char> content) {
  char* end = trimHeaderEnding(content);
  if (end == nullptr) {
    return ProtocolError{502, "Bad Gateway", "Response headers have no terminal newline.", content};
  }

  char* ptr = content.begin();

  HttpHeaders::Response response;

  ZC_IF_SOME(version, consumeWord(ptr)) {
    if (!version.startsWith("HTTP/")) {
      return ProtocolError{502, "Bad Gateway", "Invalid response status line (invalid protocol).",
                           content};
    }
  }
  else {
    return ProtocolError{502, "Bad Gateway", "Invalid response status line (no spaces).", content};
  }

  ZC_IF_SOME(code, consumeNumber(ptr)) { response.statusCode = code; }
  else {
    return ProtocolError{502, "Bad Gateway", "Invalid response status line (invalid status code).",
                         content};
  }

  response.statusText = consumeLine(ptr);

  if (!parseHeaders(ptr, end)) {
    return ProtocolError{502, "Bad Gateway", "The headers sent by the server are not valid.",
                         content};
  }

  return response;
}

bool HttpHeaders::tryParse(zc::ArrayPtr<char> content) {
  char* end = trimHeaderEnding(content);
  if (end == nullptr) return false;

  char* ptr = content.begin();
  return parseHeaders(ptr, end);
}

bool HttpHeaders::parseHeaders(char* ptr, char* end) {
  while (*ptr != '\0') {
    ZC_IF_SOME(name, consumeHeaderName(ptr)) {
      zc::StringPtr line = consumeLine(ptr);
      addNoCheck(name, line);
    }
    else { return false; }
  }

  return ptr == end;
}

// -----------------------------------------------------------------------------

zc::String HttpHeaders::serializeRequest(
    HttpMethod method, zc::StringPtr url,
    zc::ArrayPtr<const zc::StringPtr> connectionHeaders) const {
  return serialize(zc::toCharSequence(method), url, zc::StringPtr("HTTP/1.1"), connectionHeaders);
}

zc::String HttpHeaders::serializeConnectRequest(
    zc::StringPtr authority, zc::ArrayPtr<const zc::StringPtr> connectionHeaders) const {
  return serialize("CONNECT"_zc, authority, zc::StringPtr("HTTP/1.1"), connectionHeaders);
}

zc::String HttpHeaders::serializeResponse(
    uint statusCode, zc::StringPtr statusText,
    zc::ArrayPtr<const zc::StringPtr> connectionHeaders) const {
  auto statusCodeStr = zc::toCharSequence(statusCode);

  return serialize(zc::StringPtr("HTTP/1.1"), statusCodeStr, statusText, connectionHeaders);
}

zc::String HttpHeaders::serialize(zc::ArrayPtr<const char> word1, zc::ArrayPtr<const char> word2,
                                  zc::ArrayPtr<const char> word3,
                                  zc::ArrayPtr<const zc::StringPtr> connectionHeaders) const {
  const zc::StringPtr space = " ";
  const zc::StringPtr newline = "\r\n";
  const zc::StringPtr colon = ": ";

  size_t size = 2;  // final \r\n
  if (word1 != nullptr) { size += word1.size() + word2.size() + word3.size() + 4; }
  ZC_ASSERT(connectionHeaders.size() <= indexedHeaders.size());
  for (auto i : zc::indices(indexedHeaders)) {
    zc::StringPtr value = i < connectionHeaders.size() ? connectionHeaders[i] : indexedHeaders[i];
    if (value != nullptr) {
      size += table->idToString(HttpHeaderId(table, i)).size() + value.size() + 4;
    }
  }
  for (auto& header : unindexedHeaders) { size += header.name.size() + header.value.size() + 4; }

  String result = heapString(size);
  char* ptr = result.begin();

  if (word1 != nullptr) { ptr = zc::_::fill(ptr, word1, space, word2, space, word3, newline); }
  for (auto i : zc::indices(indexedHeaders)) {
    zc::StringPtr value = i < connectionHeaders.size() ? connectionHeaders[i] : indexedHeaders[i];
    if (value != nullptr) {
      ptr = zc::_::fill(ptr, table->idToString(HttpHeaderId(table, i)), colon, value, newline);
    }
  }
  for (auto& header : unindexedHeaders) {
    ptr = zc::_::fill(ptr, header.name, colon, header.value, newline);
  }
  ptr = zc::_::fill(ptr, newline);

  ZC_ASSERT(ptr == result.end());
  return result;
}

zc::String HttpHeaders::toString() const { return serialize(nullptr, nullptr, nullptr, nullptr); }

// -----------------------------------------------------------------------------

namespace {

// The functions below parse HTTP "ranges specifiers" set in `Range` headers and defined by
// RFC9110 section 14.1: https://www.rfc-editor.org/rfc/rfc9110#section-14.1.
//
// Ranges specifiers consist of a case-insensitive "range unit", followed by an '=', followed by a
// comma separated list of "range specs". We currently only support byte ranges, with a range unit
// of "bytes". A byte range spec can either be:
//
// - An "int range" consisting of an inclusive start index, followed by a '-', and optionally an
//   inclusive end index (e.g. "2-5", "7-7", "9-"). Satisfiable if the start index is less than
//   the content length. Note the end index defaults to, and is clamped to the content length.
// - A "suffix range" consisting of a '-', followed by a suffix length (e.g. "-5"). Satisfiable
//   if the suffix length is not 0. Note the suffix length is clamped to the content length.
//
// A full ranges specifier might look something like "bytes=2-4,-1", which requests bytes 2 through
// 4, and the last byte.
//
// A range spec is invalid if it doesn't match the above structure, or if it is an int range
// with an end index > start index. A ranges specifier is invalid if any of its range specs are.
// A byte ranges specifier is satisfiable if at least one of its range specs are.
//
// `tryParseHttpRangeHeader()` will return an array of satisfiable ranges, unless the ranges
// specifier is invalid.

static bool consumeByteRangeUnit(const char*& ptr) {
  const char* p = ptr;
  p = skipSpace(p);

  // Match case-insensitive "bytes"
  if (*p != 'b' && *p != 'B') return false;
  if (*(++p) != 'y' && *p != 'Y') return false;
  if (*(++p) != 't' && *p != 'T') return false;
  if (*(++p) != 'e' && *p != 'E') return false;
  if (*(++p) != 's' && *p != 'S') return false;
  ++p;

  p = skipSpace(p);
  ptr = p;
  return true;
}

static zc::Maybe<HttpByteRange> consumeIntRange(const char*& ptr, uint64_t contentLength) {
  const char* p = ptr;
  p = skipSpace(p);
  uint firstPos;
  ZC_IF_SOME(n, consumeNumber(p)) { firstPos = n; }
  else { return zc::none; }
  p = skipSpace(p);
  if (*(p++) != '-') return zc::none;
  p = skipSpace(p);
  auto maybeLastPos = consumeNumber(p);
  p = skipSpace(p);

  ZC_IF_SOME(lastPos, maybeLastPos) {
    // "An int-range is invalid if the last-pos value is present and less than the first-pos"
    if (firstPos > lastPos) return zc::none;
    // "if the value is greater than or equal to the current length of the representation data
    // ... interpreted as the remainder of the representation"
    if (lastPos >= contentLength) lastPos = contentLength - 1;
    ptr = p;
    return HttpByteRange{firstPos, lastPos};
  }
  else {
    // "if the last-pos value is absent ... interpreted as the remainder of the representation"
    ptr = p;
    return HttpByteRange{firstPos, contentLength - 1};
  }
}

static zc::Maybe<HttpByteRange> consumeSuffixRange(const char*& ptr, uint64_t contentLength) {
  const char* p = ptr;
  p = skipSpace(p);
  if (*(p++) != '-') return zc::none;
  p = skipSpace(p);
  uint suffixLength;
  ZC_IF_SOME(n, consumeNumber(p)) { suffixLength = n; }
  else { return zc::none; }
  p = skipSpace(p);

  ptr = p;
  if (suffixLength >= contentLength) {
    // "if the selected representation is shorter than the specified suffix-length, the entire
    // representation is used"
    return HttpByteRange{0, contentLength - 1};
  } else {
    return HttpByteRange{contentLength - suffixLength, contentLength - 1};
  }
}

static zc::Maybe<HttpByteRange> consumeRangeSpec(const char*& ptr, uint64_t contentLength) {
  ZC_IF_SOME(range, consumeIntRange(ptr, contentLength)) { return range; }
  else {
    // If we failed to consume an int range, try consume a suffix range instead
    return consumeSuffixRange(ptr, contentLength);
  }
}

}  // namespace

zc::String ZC_STRINGIFY(HttpByteRange range) { return zc::str(range.start, "-", range.end); }

HttpRanges tryParseHttpRangeHeader(zc::ArrayPtr<const char> value, uint64_t contentLength) {
  const char* p = value.begin();
  if (!consumeByteRangeUnit(p)) return HttpUnsatisfiableRange{};
  if (*(p++) != '=') return HttpUnsatisfiableRange{};

  auto fullRange = false;
  zc::Vector<HttpByteRange> satisfiableRanges;
  do {
    ZC_IF_SOME(range, consumeRangeSpec(p, contentLength)) {
      // Don't record more ranges if we've already recorded a full range
      if (!fullRange && range.start <= range.end) {
        if (range.start == 0 && range.end == contentLength - 1) {
          // A range evaluated to the full range, but still need to check rest are valid
          fullRange = true;
        } else {
          // "a valid bytes range-spec is satisfiable if it is either:
          // - an int-range with a first-pos that is less than the current length of the selected
          //   representation or
          // - a suffix-range with a non-zero suffix-length"
          satisfiableRanges.add(range);
        }
      }
    }
    else {
      // If we failed to parse a range, the whole range specification is invalid
      return HttpUnsatisfiableRange{};
    }
  } while (*(p++) == ',');

  if ((--p) != value.end()) return HttpUnsatisfiableRange{};
  if (fullRange) return HttpEverythingRange{};
  // "A valid ranges-specifier is "satisfiable" if it contains at least one range-spec that is
  // satisfiable"
  if (satisfiableRanges.size() == 0) return HttpUnsatisfiableRange{};
  return satisfiableRanges.releaseAsArray();
}

// =======================================================================================

namespace {

template <typename Subclass>
class WrappableStreamMixin {
  // Both HttpInputStreamImpl and HttpOutputStream are commonly wrapped by a class that implements
  // a particular type of body stream, such as a chunked body or a fixed-length body. That wrapper
  // stream is passed back to the application to represent the specific request/response body, but
  // the inner stream is associated with the connection and can be reused several times.
  //
  // It's easy for applications to screw up and hold on to a body stream beyond the lifetime of the
  // underlying connection stream. This used to lead to UAF. This mixin class implements behavior
  // that detached the wrapper if it outlives the wrapped stream, so that we log errors and

public:
  WrappableStreamMixin() = default;
  WrappableStreamMixin(WrappableStreamMixin&& other) {
    // This constructor is only needed by HttpServer::Connection::makeHttpInput() which constructs
    // a new stream and returns it. Technically the constructor will always be elided anyway.
    ZC_REQUIRE(other.currentWrapper == nullptr, "can't move a wrappable object that has wrappers!");
  }
  ZC_DISALLOW_COPY(WrappableStreamMixin);

  ~WrappableStreamMixin() noexcept(false) {
    ZC_IF_SOME(w, currentWrapper) {
      ZC_LOG(ERROR, "HTTP connection destroyed while HTTP body streams still exist",
             zc::getStackTrace());
      w = zc::none;
    }
  }

  void setCurrentWrapper(zc::Maybe<Subclass&>& weakRef) {
    // Tracks the current `HttpEntityBodyReader` instance which is wrapping this stream. There can
    // be only one wrapper at a time, and the wrapper must be destroyed before the underlying HTTP
    // connection is torn down. The purpose of tracking the wrapper here is to detect when these
    // rules are violated by apps, and log an error instead of going UB.
    //
    // `weakRef` is the wrapper's pointer to this object. If the underlying stream is destroyed
    // before the wrapper, then `weakRef` will be nulled out.

    // The API should prevent an app from obtaining multiple wrappers with the same backing stream.
    ZC_ASSERT(currentWrapper == zc::none,
              "bug in ZC HTTP: only one HTTP stream wrapper can exist at a time");

    currentWrapper = weakRef;
    weakRef = static_cast<Subclass&>(*this);
  }

  void unsetCurrentWrapper(zc::Maybe<Subclass&>& weakRef) {
    auto& current = ZC_ASSERT_NONNULL(currentWrapper);
    ZC_ASSERT(&current == &weakRef,
              "bug in ZC HTTP: unsetCurrentWrapper() passed the wrong wrapper");
    weakRef = zc::none;
    currentWrapper = zc::none;
  }

private:
  zc::Maybe<zc::Maybe<Subclass&>&> currentWrapper;
};

// =======================================================================================

static constexpr size_t MIN_BUFFER = 4096;
static constexpr size_t MAX_BUFFER = 128 * 1024;
static constexpr size_t MAX_CHUNK_HEADER_SIZE = 32;

class HttpInputStreamImpl final : public HttpInputStream,
                                  public WrappableStreamMixin<HttpInputStreamImpl> {
private:
  static zc::OneOf<HttpHeaders::Request, HttpHeaders::ConnectRequest> getResumingRequest(
      zc::OneOf<HttpMethod, HttpConnectMethod> method, zc::StringPtr url) {
    ZC_SWITCH_ONEOF(method) {
      ZC_CASE_ONEOF(m, HttpMethod) { return HttpHeaders::Request{m, url}; }
      ZC_CASE_ONEOF(m, HttpConnectMethod) { return HttpHeaders::ConnectRequest{url}; }
    }
    ZC_UNREACHABLE;
  }

public:
  explicit HttpInputStreamImpl(AsyncInputStream& inner, const HttpHeaderTable& table)
      : inner(inner), headerBuffer(zc::heapArray<char>(MIN_BUFFER)), headers(table) {}

  explicit HttpInputStreamImpl(AsyncInputStream& inner, zc::Array<char> headerBufferParam,
                               zc::ArrayPtr<char> leftoverParam,
                               zc::OneOf<HttpMethod, HttpConnectMethod> method, zc::StringPtr url,
                               HttpHeaders headers)
      : inner(inner),
        headerBuffer(zc::mv(headerBufferParam)),
        // Initialize `messageHeaderEnd` to a safe value, we'll adjust it below.
        messageHeaderEnd(leftoverParam.begin() - headerBuffer.begin()),
        leftover(leftoverParam),
        headers(zc::mv(headers)),
        resumingRequest(getResumingRequest(method, url)) {
    // Constructor used for resuming a SuspendedRequest.

    // We expect headerBuffer to look like this:
    //   <method> <url> <headers> [CR] LF <leftover>
    // We initialized `messageHeaderEnd` to the beginning of `leftover`, but we want to point it at
    // the CR (or LF if there's no CR).
    ZC_REQUIRE(messageHeaderEnd >= 2 && leftover.end() <= headerBuffer.end(),
               "invalid SuspendedRequest - leftover buffer not where it should be");
    ZC_REQUIRE(leftover.begin()[-1] == '\n', "invalid SuspendedRequest - missing LF");
    messageHeaderEnd -= 1 + (leftover.begin()[-2] == '\r');

    // We're in the middle of a message, so set up our state as such. Note that the only way to
    // resume a SuspendedRequest is via an HttpServer, but HttpServers never call
    // `awaitNextMessage()` before fully reading request bodies, meaning we expect that
    // `messageReadQueue` will never be used.
    ++pendingMessageCount;
    auto paf = zc::newPromiseAndFulfiller<void>();
    onMessageDone = zc::mv(paf.fulfiller);
    messageReadQueue = zc::mv(paf.promise);
  }

  bool canReuse() { return !broken && pendingMessageCount == 0; }

  bool canSuspend() {
    // We are at a suspendable point if we've parsed the headers, but haven't consumed anything
    // beyond that.
    //
    // TODO(cleanup): This is a silly check; we need a more defined way to track the state of the
    //   stream.
    bool messageHeaderEndLooksRight =
        (leftover.begin() - (headerBuffer.begin() + messageHeaderEnd) == 2 &&
         leftover.begin()[-1] == '\n' && leftover.begin()[-2] == '\r') ||
        (leftover.begin() - (headerBuffer.begin() + messageHeaderEnd) == 1 &&
         leftover.begin()[-1] == '\n');

    return !broken && headerBuffer.size() > 0 && messageHeaderEndLooksRight;
  }

  // ---------------------------------------------------------------------------
  // public interface

  zc::Promise<Request> readRequest() override {
    auto requestOrProtocolError = co_await readRequestHeaders();
    auto request =
        ZC_REQUIRE_NONNULL(requestOrProtocolError.tryGet<HttpHeaders::Request>(), "bad request");
    auto body = getEntityBody(HttpInputStreamImpl::REQUEST, request.method, 0, headers);

    co_return {request.method, request.url, headers, zc::mv(body)};
  }

  zc::Promise<zc::OneOf<Request, Connect>> readRequestAllowingConnect() override {
    auto requestOrProtocolError = co_await readRequestHeaders();
    ZC_SWITCH_ONEOF(requestOrProtocolError) {
      ZC_CASE_ONEOF(request, HttpHeaders::Request) {
        auto body = getEntityBody(HttpInputStreamImpl::REQUEST, request.method, 0, headers);
        co_return HttpInputStream::Request{request.method, request.url, headers, zc::mv(body)};
      }
      ZC_CASE_ONEOF(request, HttpHeaders::ConnectRequest) {
        auto body = getEntityBody(HttpInputStreamImpl::REQUEST, HttpConnectMethod(), 0, headers);
        co_return HttpInputStream::Connect{request.authority, headers, zc::mv(body)};
      }
      ZC_CASE_ONEOF(error, HttpHeaders::ProtocolError) { ZC_FAIL_REQUIRE("bad request"); }
    }
    ZC_UNREACHABLE;
  }

  zc::Promise<Response> readResponse(HttpMethod requestMethod) override {
    auto responseOrProtocolError = co_await readResponseHeaders();
    auto response =
        ZC_REQUIRE_NONNULL(responseOrProtocolError.tryGet<HttpHeaders::Response>(), "bad response");
    auto body =
        getEntityBody(HttpInputStreamImpl::RESPONSE, requestMethod, response.statusCode, headers);

    co_return {response.statusCode, response.statusText, headers, zc::mv(body)};
  }

  zc::Promise<Message> readMessage() override {
    auto textOrError = co_await readMessageHeaders();
    ZC_REQUIRE(textOrError.is<zc::ArrayPtr<char>>(), "bad message");
    auto text = textOrError.get<zc::ArrayPtr<char>>();
    headers.clear();
    ZC_REQUIRE(headers.tryParse(text), "bad message");
    auto body = getEntityBody(HttpInputStreamImpl::RESPONSE, HttpMethod::GET, 0, headers);

    co_return {headers, zc::mv(body)};
  }

  // ---------------------------------------------------------------------------
  // Stream locking: While an entity-body is being read, the body stream "locks" the underlying
  // HTTP stream. Once the entity-body is complete, we can read the next pipelined message.

  void finishRead() {
    // Called when entire request has been read.

    ZC_REQUIRE_NONNULL(onMessageDone)->fulfill();
    onMessageDone = zc::none;
    --pendingMessageCount;
  }

  void abortRead() {
    // Called when a body input stream was destroyed without reading to the end.

    ZC_REQUIRE_NONNULL(onMessageDone)
        ->reject(ZC_EXCEPTION(FAILED,
                              "application did not finish reading previous HTTP response body",
                              "can't read next pipelined request/response"));
    onMessageDone = zc::none;
    broken = true;
  }

  // ---------------------------------------------------------------------------

  zc::Promise<bool> awaitNextMessage() override {
    // Waits until more data is available, but doesn't consume it. Returns false on EOF.
    //
    // Used on the server after a request is handled, to check for pipelined requests.
    //
    // Used on the client to detect when idle connections are closed from the server end. (In this
    // case, the promise always returns false or is canceled.)

    if (resumingRequest != zc::none) {
      // We're resuming a request, so report that we have a message.
      co_return true;
    }

    if (onMessageDone != zc::none) {
      // We're still working on reading the previous body.
      auto fork = messageReadQueue.fork();
      messageReadQueue = fork.addBranch();
      co_await fork;
    }

    for (;;) {
      snarfBufferedLineBreak();

      if (!lineBreakBeforeNextHeader && leftover != nullptr) { co_return true; }

      auto amount = co_await inner.tryRead(headerBuffer.begin(), 1, headerBuffer.size());
      if (amount == 0) { co_return false; }

      leftover = headerBuffer.first(amount);
    }
  }

  bool isCleanDrain() {
    // Returns whether we can cleanly drain the stream at this point.
    if (onMessageDone != zc::none) return false;
    snarfBufferedLineBreak();
    return !lineBreakBeforeNextHeader && leftover == nullptr;
  }

  zc::Promise<zc::OneOf<zc::ArrayPtr<char>, HttpHeaders::ProtocolError>> readMessageHeaders() {
    ++pendingMessageCount;
    auto paf = zc::newPromiseAndFulfiller<void>();

    auto nextMessageReady = zc::mv(messageReadQueue);
    messageReadQueue = zc::mv(paf.promise);

    co_await nextMessageReady;
    onMessageDone = zc::mv(paf.fulfiller);

    co_return co_await readHeader(HeaderType::MESSAGE, 0, 0);
  }

  zc::Promise<zc::OneOf<uint64_t, HttpHeaders::ProtocolError>> readChunkHeader() {
    ZC_REQUIRE(onMessageDone != zc::none);

    // We use the portion of the header after the end of message headers.
    auto textOrError = co_await readHeader(HeaderType::CHUNK, messageHeaderEnd, messageHeaderEnd);

    ZC_SWITCH_ONEOF(textOrError) {
      ZC_CASE_ONEOF(protocolError, HttpHeaders::ProtocolError) { co_return protocolError; }
      ZC_CASE_ONEOF(text, zc::ArrayPtr<char>) {
        ZC_REQUIRE(text.size() > 0) { break; }

        uint64_t value = 0;
        for (char c : text) {
          if ('0' <= c && c <= '9') {
            value = value * 16 + (c - '0');
          } else if ('a' <= c && c <= 'f') {
            value = value * 16 + (c - 'a' + 10);
          } else if ('A' <= c && c <= 'F') {
            value = value * 16 + (c - 'A' + 10);
          } else {
            ZC_FAIL_REQUIRE("invalid HTTP chunk size", text, text.asBytes()) { break; }
            co_return value;
          }
        }

        co_return value;
      }
    }

    ZC_UNREACHABLE;
  }

  inline zc::Promise<HttpHeaders::RequestConnectOrProtocolError> readRequestHeaders() {
    ZC_IF_SOME(resuming, resumingRequest) {
      ZC_DEFER(resumingRequest = zc::none);
      co_return HttpHeaders::RequestConnectOrProtocolError(resuming);
    }

    auto textOrError = co_await readMessageHeaders();
    ZC_SWITCH_ONEOF(textOrError) {
      ZC_CASE_ONEOF(protocolError, HttpHeaders::ProtocolError) { co_return protocolError; }
      ZC_CASE_ONEOF(text, zc::ArrayPtr<char>) {
        headers.clear();
        co_return headers.tryParseRequestOrConnect(text);
      }
    }

    ZC_UNREACHABLE;
  }

  inline zc::Promise<HttpHeaders::ResponseOrProtocolError> readResponseHeaders() {
    // Note: readResponseHeaders() could be called multiple times concurrently when pipelining
    //   requests. readMessageHeaders() will serialize these, but it's important not to mess with
    //   state (like calling headers.clear()) before said serialization has taken place.
    auto headersOrError = co_await readMessageHeaders();
    ZC_SWITCH_ONEOF(headersOrError) {
      ZC_CASE_ONEOF(protocolError, HttpHeaders::ProtocolError) { co_return protocolError; }
      ZC_CASE_ONEOF(text, zc::ArrayPtr<char>) {
        headers.clear();
        co_return headers.tryParseResponse(text);
      }
    }

    ZC_UNREACHABLE;
  }

  inline const HttpHeaders& getHeaders() const { return headers; }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) {
    // Read message body data.

    ZC_REQUIRE(onMessageDone != zc::none);

    if (leftover == nullptr) {
      // No leftovers. Forward directly to inner stream.
      co_return co_await inner.tryRead(buffer, minBytes, maxBytes);
    } else if (leftover.size() >= maxBytes) {
      // Didn't even read the entire leftover buffer.
      memcpy(buffer, leftover.begin(), maxBytes);
      leftover = leftover.slice(maxBytes, leftover.size());
      co_return maxBytes;
    } else {
      // Read the entire leftover buffer, plus some.
      memcpy(buffer, leftover.begin(), leftover.size());
      size_t copied = leftover.size();
      leftover = nullptr;
      if (copied >= minBytes) {
        // Got enough to stop here.
        co_return copied;
      } else {
        // Read the rest from the underlying stream.
        auto n = co_await inner.tryRead(reinterpret_cast<byte*>(buffer) + copied, minBytes - copied,
                                        maxBytes - copied);
        co_return n + copied;
      }
    }
  }

  enum RequestOrResponse { REQUEST, RESPONSE };

  zc::Own<zc::AsyncInputStream> getEntityBody(RequestOrResponse type,
                                              zc::OneOf<HttpMethod, HttpConnectMethod> method,
                                              uint statusCode, const zc::HttpHeaders& headers);

  struct ReleasedBuffer {
    zc::Array<byte> buffer;
    zc::ArrayPtr<byte> leftover;
  };

  ReleasedBuffer releaseBuffer() { return {headerBuffer.releaseAsBytes(), leftover.asBytes()}; }

  zc::Promise<void> discard(AsyncOutputStream& output, size_t maxBytes) {
    // Used to read and discard the input during error handling.
    return inner.pumpTo(output, maxBytes).ignoreResult();
  }

private:
  AsyncInputStream& inner;
  zc::Array<char> headerBuffer;

  size_t messageHeaderEnd = 0;
  // Position in headerBuffer where the message headers end -- further buffer space can
  // be used for chunk headers.

  zc::ArrayPtr<char> leftover;
  // Data in headerBuffer that comes immediately after the header content, if any.

  HttpHeaders headers;
  // Parsed headers, after a call to parseAwaited*().

  zc::Maybe<zc::OneOf<HttpHeaders::Request, HttpHeaders::ConnectRequest>> resumingRequest;
  // Non-null if we're resuming a SuspendedRequest.

  bool lineBreakBeforeNextHeader = false;
  // If true, the next await should expect to start with a spurious '\n' or '\r\n'. This happens
  // as a side-effect of HTTP chunked encoding, where such a newline is added to the end of each
  // chunk, for no good reason.

  bool broken = false;
  // Becomes true if the caller failed to read the whole entity-body before closing the stream.

  uint pendingMessageCount = 0;
  // Number of reads we have queued up.

  zc::Maybe<zc::Own<zc::PromiseFulfiller<void>>> onMessageDone;
  // Fulfill once the current message has been completely read. Unblocks reading of the next
  // message headers.
  //
  // Note this should be declared before `messageReadQueue`, because the promise in
  // `messageReadQueue` may be waiting for `onMessageDone` to be fulfilled. If the whole object
  // is torn down early, then the fulfiller ends up being deleted while a listener still exists,
  // which causes various stack tracing for exception-handling purposes to be performed, only to
  // be thrown away as the listener is immediately canceled thereafter. To avoid this wasted work,
  // we want the listener to be canceled first.

  zc::Promise<void> messageReadQueue = zc::READY_NOW;
  // Resolves when all previous HTTP messages have completed, allowing the next pipelined message
  // to be read.

  enum class HeaderType { MESSAGE, CHUNK };

  zc::Promise<zc::OneOf<zc::ArrayPtr<char>, HttpHeaders::ProtocolError>> readHeader(
      HeaderType type, size_t bufferStart, size_t bufferEnd) {
    // Reads the HTTP message header or a chunk header (as in transfer-encoding chunked) and
    // returns the buffer slice containing it.
    //
    // The main source of complication here is that we want to end up with one continuous buffer
    // containing the result, and that the input is delimited by newlines rather than by an upfront
    // length.

    for (;;) {
      zc::Promise<size_t> readPromise = nullptr;

      // Figure out where we're reading from.
      if (leftover != nullptr) {
        // Some data is still left over from the previous message, so start with that.

        // This can only happen if this is the initial run through the loop.
        ZC_ASSERT(bufferStart == bufferEnd);

        // OK, set bufferStart and bufferEnd to both point to the start of the leftover, and then
        // fake a read promise as if we read the bytes from the leftover.
        bufferStart = leftover.begin() - headerBuffer.begin();
        bufferEnd = bufferStart;
        readPromise = leftover.size();
        leftover = nullptr;
      } else {
        // Need to read more data from the underlying stream.

        if (bufferEnd == headerBuffer.size()) {
          // Out of buffer space.

          // Maybe we can move bufferStart backwards to make more space at the end?
          size_t minStart = type == HeaderType::MESSAGE ? 0 : messageHeaderEnd;

          if (bufferStart > minStart) {
            // Move to make space.
            memmove(headerBuffer.begin() + minStart, headerBuffer.begin() + bufferStart,
                    bufferEnd - bufferStart);
            bufferEnd = bufferEnd - bufferStart + minStart;
            bufferStart = minStart;
          } else {
            // Really out of buffer space. Grow the buffer.
            if (type != HeaderType::MESSAGE) {
              // Can't grow because we'd invalidate the HTTP headers.
              zc::throwFatalException(ZC_EXCEPTION(FAILED, "invalid HTTP chunk size"));
            }
            if (headerBuffer.size() >= MAX_BUFFER) {
              co_return HttpHeaders::ProtocolError{
                  .statusCode = 431,
                  .statusMessage = "Request Header Fields Too Large",
                  .description = "header too large.",
                  .rawContent = nullptr};
            }
            auto newBuffer = zc::heapArray<char>(headerBuffer.size() * 2);
            memcpy(newBuffer.begin(), headerBuffer.begin(), headerBuffer.size());
            headerBuffer = zc::mv(newBuffer);
          }
        }

        // How many bytes will we read?
        size_t maxBytes = headerBuffer.size() - bufferEnd;

        if (type == HeaderType::CHUNK) {
          // Roughly limit the amount of data we read to MAX_CHUNK_HEADER_SIZE.
          // TODO(perf): This is mainly to avoid copying a lot of body data into our buffer just to
          //   copy it again when it is read. But maybe the copy would be cheaper than overhead of
          //   extra event loop turns?
          ZC_REQUIRE(bufferEnd - bufferStart <= MAX_CHUNK_HEADER_SIZE, "invalid HTTP chunk size");
          maxBytes = zc::min(maxBytes, MAX_CHUNK_HEADER_SIZE);
        }

        readPromise = inner.read(headerBuffer.begin() + bufferEnd, 1, maxBytes);
      }

      auto amount = co_await readPromise;

      if (lineBreakBeforeNextHeader) {
        // Hackily deal with expected leading line break.
        if (bufferEnd == bufferStart && headerBuffer[bufferEnd] == '\r') {
          ++bufferEnd;
          --amount;
        }

        if (amount > 0 && headerBuffer[bufferEnd] == '\n') {
          lineBreakBeforeNextHeader = false;
          ++bufferEnd;
          --amount;

          // Cut the leading line break out of the buffer entirely.
          bufferStart = bufferEnd;
        }

        if (amount == 0) { continue; }
      }

      size_t pos = bufferEnd;
      size_t newEnd = pos + amount;

      for (;;) {
        // Search for next newline.
        char* nl = reinterpret_cast<char*>(memchr(headerBuffer.begin() + pos, '\n', newEnd - pos));
        if (nl == nullptr) {
          // No newline found. Wait for more data.
          bufferEnd = newEnd;
          break;
        }

        // Is this newline which we found the last of the header? For a chunk header, always. For
        // a message header, we search for two newlines in a row. We accept either "\r\n" or just
        // "\n" as a newline sequence (though the standard requires "\r\n").
        if (type == HeaderType::CHUNK ||
            (nl - headerBuffer.begin() >= 4 &&
             ((nl[-1] == '\r' && nl[-2] == '\n') || (nl[-1] == '\n')))) {
          // OK, we've got all the data!

          size_t endIndex = nl + 1 - headerBuffer.begin();
          size_t leftoverStart = endIndex;

          // Strip off the last newline from end.
          endIndex -= 1 + (nl[-1] == '\r');

          if (type == HeaderType::MESSAGE) {
            if (headerBuffer.size() - newEnd < MAX_CHUNK_HEADER_SIZE) {
              // Ugh, there's not enough space for the secondary await buffer. Grow once more.
              auto newBuffer = zc::heapArray<char>(headerBuffer.size() * 2);
              memcpy(newBuffer.begin(), headerBuffer.begin(), headerBuffer.size());
              headerBuffer = zc::mv(newBuffer);
            }
            messageHeaderEnd = endIndex;
          } else {
            // For some reason, HTTP specifies that there will be a line break after each chunk.
            lineBreakBeforeNextHeader = true;
          }

          auto result = headerBuffer.slice(bufferStart, endIndex);
          leftover = headerBuffer.slice(leftoverStart, newEnd);
          co_return result;
        } else {
          pos = nl - headerBuffer.begin() + 1;
        }
      }

      // If we're here, we broke out of the inner loop because we need to read more data.
    }
  }

  void snarfBufferedLineBreak() {
    // Slightly-crappy code to snarf the expected line break. This will actually eat the leading
    // regex /\r*\n?/.
    while (lineBreakBeforeNextHeader && leftover.size() > 0) {
      if (leftover[0] == '\r') {
        leftover = leftover.slice(1, leftover.size());
      } else if (leftover[0] == '\n') {
        leftover = leftover.slice(1, leftover.size());
        lineBreakBeforeNextHeader = false;
      } else {
        // Err, missing line break, whatever.
        lineBreakBeforeNextHeader = false;
      }
    }
  }
};

// -----------------------------------------------------------------------------

class HttpEntityBodyReader : public zc::AsyncInputStream {
public:
  HttpEntityBodyReader(HttpInputStreamImpl& inner) { inner.setCurrentWrapper(weakInner); }
  ~HttpEntityBodyReader() noexcept(false) {
    if (!finished) {
      ZC_IF_SOME(inner, weakInner) {
        inner.unsetCurrentWrapper(weakInner);
        inner.abortRead();
      }
      else {
        // Since we're in a destructor, log an error instead of throwing.
        ZC_LOG(ERROR, "HTTP body input stream outlived underlying connection", zc::getStackTrace());
      }
    }
  }

protected:
  HttpInputStreamImpl& getInner() {
    ZC_IF_SOME(i, weakInner) { return i; }
    else if (finished) {
      // This is a bug in the implementations in this file, not the app.
      ZC_FAIL_ASSERT("bug in ZC HTTP: tried to access inner stream after it had been released");
    }
    else { ZC_FAIL_REQUIRE("HTTP body input stream outlived underlying connection"); }
  }

  void doneReading() {
    auto& inner = getInner();
    inner.unsetCurrentWrapper(weakInner);
    finished = true;
    inner.finishRead();
  }

  inline bool alreadyDone() { return weakInner == zc::none; }

private:
  zc::Maybe<HttpInputStreamImpl&> weakInner;
  bool finished = false;
};

class HttpNullEntityReader final : public HttpEntityBodyReader {
  // Stream for an entity-body which is not present. Always returns EOF on read, but tryGetLength()
  // may indicate non-zero in the special case of a response to a HEAD request.

public:
  HttpNullEntityReader(HttpInputStreamImpl& inner, zc::Maybe<uint64_t> length)
      : HttpEntityBodyReader(inner), length(length) {
    // `length` is what to return from tryGetLength(). For a response to a HEAD request, this may
    // be non-zero.
    doneReading();
  }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return constPromise<size_t, 0>();
  }

  Maybe<uint64_t> tryGetLength() override { return length; }

private:
  zc::Maybe<uint64_t> length;
};

class HttpConnectionCloseEntityReader final : public HttpEntityBodyReader {
  // Stream which reads until EOF.

public:
  HttpConnectionCloseEntityReader(HttpInputStreamImpl& inner) : HttpEntityBodyReader(inner) {}

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    if (alreadyDone()) co_return 0;

    auto amount = co_await getInner().tryRead(buffer, minBytes, maxBytes);
    if (amount < minBytes) { doneReading(); }
    co_return amount;
  }
};

class HttpFixedLengthEntityReader final : public HttpEntityBodyReader {
  // Stream which reads only up to a fixed length from the underlying stream, then emulates EOF.

public:
  HttpFixedLengthEntityReader(HttpInputStreamImpl& inner, size_t length)
      : HttpEntityBodyReader(inner), length(length) {
    if (length == 0) doneReading();
  }

  Maybe<uint64_t> tryGetLength() override { return length; }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    ZC_REQUIRE(clean, "can't read more data after a previous read didn't complete");
    clean = false;

    size_t alreadyRead = 0;

    for (;;) {
      if (length == 0) {
        clean = true;
        co_return 0;
      }

      // We have to set minBytes to 1 here so that if we read any data at all, we update our
      // counter immediately, so that we still know where we are in case of cancellation.
      auto amount = co_await getInner().tryRead(buffer, 1, zc::min(maxBytes, length));

      length -= amount;
      if (length > 0) {
        // We haven't reached the end of the entity body yet.
        if (amount == 0) {
          size_t expectedLength = length + alreadyRead;
          zc::throwRecoverableException(ZC_EXCEPTION(
              DISCONNECTED, "premature EOF in HTTP entity body; did not reach Content-Length",
              expectedLength, alreadyRead));
        } else if (amount < minBytes) {
          // We requested a minimum 1 byte above, but our own caller actually set a larger minimum
          // which has not yet been reached. Keep trying until we reach it.
          buffer = reinterpret_cast<byte*>(buffer) + amount;
          minBytes -= amount;
          maxBytes -= amount;
          alreadyRead += amount;
          continue;
        }
      } else if (length == 0) {
        doneReading();
      }
      clean = true;
      co_return amount + alreadyRead;
    }
  }

private:
  size_t length;
  bool clean = true;
};

class HttpChunkedEntityReader final : public HttpEntityBodyReader {
  // Stream which reads a Transfer-Encoding: Chunked stream.

public:
  HttpChunkedEntityReader(HttpInputStreamImpl& inner) : HttpEntityBodyReader(inner) {}

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    ZC_REQUIRE(clean, "can't read more data after a previous read didn't complete");
    clean = false;

    size_t alreadyRead = 0;

    for (;;) {
      if (alreadyDone()) {
        clean = true;
        co_return alreadyRead;
      } else if (chunkSize == 0) {
        // Read next chunk header.
        auto nextChunkSizeOrError = co_await getInner().readChunkHeader();
        ZC_REQUIRE(nextChunkSizeOrError.is<uint64_t>(), "bad header");
        auto nextChunkSize = nextChunkSizeOrError.get<uint64_t>();
        if (nextChunkSize == 0) { doneReading(); }

        chunkSize = nextChunkSize;
        continue;
      } else {
        // Read current chunk.
        // We have to set minBytes to 1 here so that if we read any data at all, we update our
        // counter immediately, so that we still know where we are in case of cancellation.
        auto amount = co_await getInner().tryRead(buffer, 1, zc::min(maxBytes, chunkSize));

        chunkSize -= amount;
        if (amount == 0) {
          zc::throwRecoverableException(ZC_EXCEPTION(DISCONNECTED, "premature EOF in HTTP chunk"));
        } else if (amount < minBytes) {
          // We requested a minimum 1 byte above, but our own caller actually set a larger minimum
          // which has not yet been reached. Keep trying until we reach it.
          buffer = reinterpret_cast<byte*>(buffer) + amount;
          minBytes -= amount;
          maxBytes -= amount;
          alreadyRead += amount;
          continue;
        }
        clean = true;
        co_return alreadyRead + amount;
      }
    }
  }

private:
  size_t chunkSize = 0;
  bool clean = true;
};

template <char...>
struct FastCaseCmp;

template <char first, char... rest>
struct FastCaseCmp<first, rest...> {
  static constexpr bool apply(const char* actual) {
    return ('a' <= first && first <= 'z') || ('A' <= first && first <= 'Z')
               ? (*actual | 0x20) == (first | 0x20) && FastCaseCmp<rest...>::apply(actual + 1)
               : *actual == first && FastCaseCmp<rest...>::apply(actual + 1);
  }
};

template <>
struct FastCaseCmp<> {
  static constexpr bool apply(const char* actual) { return *actual == '\0'; }
};

template <char... chars>
constexpr bool fastCaseCmp(const char* actual) {
  return FastCaseCmp<chars...>::apply(actual);
}

// Tests
static_assert(fastCaseCmp<'f', 'O', 'o', 'B', '1'>("FooB1"), "");
static_assert(!fastCaseCmp<'f', 'O', 'o', 'B', '2'>("FooB1"), "");
static_assert(!fastCaseCmp<'n', 'O', 'o', 'B', '1'>("FooB1"), "");
static_assert(!fastCaseCmp<'f', 'O', 'o', 'B'>("FooB1"), "");
static_assert(!fastCaseCmp<'f', 'O', 'o', 'B', '1', 'a'>("FooB1"), "");

zc::Own<zc::AsyncInputStream> HttpInputStreamImpl::getEntityBody(
    RequestOrResponse type, zc::OneOf<HttpMethod, HttpConnectMethod> method, uint statusCode,
    const zc::HttpHeaders& headers) {
  ZC_REQUIRE(headerBuffer.size() > 0, "Cannot get entity body after header buffer release.");

  auto isHeadRequest = method.tryGet<HttpMethod>()
                           .map([](auto& m) { return m == HttpMethod::HEAD; })
                           .orDefault(false);

  auto isConnectRequest = method.is<HttpConnectMethod>();

  // Rules to determine how HTTP entity-body is delimited:
  //   https://tools.ietf.org/html/rfc7230#section-3.3.3
  // #1
  if (type == RESPONSE) {
    if (isHeadRequest) {
      // Body elided.
      zc::Maybe<uint64_t> length;
      ZC_IF_SOME(cl, headers.get(HttpHeaderId::CONTENT_LENGTH)) {
        length = strtoull(cl.cStr(), nullptr, 10);
      }
      else if (headers.get(HttpHeaderId::TRANSFER_ENCODING) == zc::none) {
        // HACK: Neither Content-Length nor Transfer-Encoding header in response to HEAD
        //   request. Propagate this fact with a 0 expected body length.
        length = uint64_t(0);
      }
      return zc::heap<HttpNullEntityReader>(*this, length);
    } else if (isConnectRequest && statusCode >= 200 && statusCode < 300) {
      ZC_FAIL_ASSERT("a CONNECT response with a 2xx status does not have an entity body to get");
    } else if (statusCode == 204 || statusCode == 304) {
      // No body.
      return zc::heap<HttpNullEntityReader>(*this, uint64_t(0));
    }
  }

  // For CONNECT requests messages, we let the rest of the logic play out.
  // We already check before here to ensure that Transfer-Encoding and
  // Content-Length headers are not present in which case the code below
  // does the right thing.

  // #3
  ZC_IF_SOME(te, headers.get(HttpHeaderId::TRANSFER_ENCODING)) {
    // TODO(someday): Support pluggable transfer encodings? Or at least zip?
    // TODO(someday): Support stacked transfer encodings, e.g. "zip, chunked".

    // NOTE: #3¶3 is ambiguous about what should happen if Transfer-Encoding and Content-Length are
    //   both present. It says that Transfer-Encoding takes precedence, but also that the request
    //   "ought to be handled as an error", and that proxies "MUST" drop the Content-Length before
    //   forwarding. We ignore the vague "ought to" part and implement the other two. (The
    //   dropping of Content-Length will happen naturally if/when the message is sent back out to
    //   the network.)
    if (fastCaseCmp<'c', 'h', 'u', 'n', 'k', 'e', 'd'>(te.cStr())) {
      // #3¶1
      return zc::heap<HttpChunkedEntityReader>(*this);
    } else if (fastCaseCmp<'i', 'd', 'e', 'n', 't', 'i', 't', 'y'>(te.cStr())) {
      // #3¶2
      ZC_REQUIRE(type != REQUEST, "request body cannot have Transfer-Encoding other than chunked");
      return zc::heap<HttpConnectionCloseEntityReader>(*this);
    }

    ZC_FAIL_REQUIRE("unknown transfer encoding", te) { break; };
  }

  // #4 and #5
  ZC_IF_SOME(cl, headers.get(HttpHeaderId::CONTENT_LENGTH)) {
    // NOTE: By spec, multiple Content-Length values are allowed as long as they are the same, e.g.
    //   "Content-Length: 5, 5, 5". Hopefully no one actually does that...
    char* end;
    uint64_t length = strtoull(cl.cStr(), &end, 10);
    if (end > cl.begin() && *end == '\0') {
      // #5
      return zc::heap<HttpFixedLengthEntityReader>(*this, length);
    } else {
      // #4 (bad content-length)
      ZC_FAIL_REQUIRE("invalid Content-Length header value", cl);
    }
  }

  // #6
  if (type == REQUEST) {
    // Lack of a Content-Length or Transfer-Encoding means no body for requests.
    return zc::heap<HttpNullEntityReader>(*this, uint64_t(0));
  }

  // RFC 2616 permitted "multipart/byteranges" responses to be self-delimiting, but this was
  // mercifully removed in RFC 7230, and new exceptions of this type are disallowed:
  //   https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
  //   https://tools.ietf.org/html/rfc7230#page-81
  // To be extra-safe, we'll reject a multipart/byteranges response that lacks transfer-encoding
  // and content-length.
  ZC_IF_SOME(type, headers.get(HttpHeaderId::CONTENT_TYPE)) {
    if (type.startsWith("multipart/byteranges")) {
      ZC_FAIL_REQUIRE(
          "refusing to handle multipart/byteranges response without transfer-encoding nor "
          "content-length due to ambiguity between RFC 2616 vs RFC 7230.");
    }
  }

  // #7
  return zc::heap<HttpConnectionCloseEntityReader>(*this);
}

}  // namespace

zc::Own<HttpInputStream> newHttpInputStream(zc::AsyncInputStream& input,
                                            const HttpHeaderTable& table) {
  return zc::heap<HttpInputStreamImpl>(input, table);
}

// =======================================================================================

namespace {

class HttpOutputStream : public WrappableStreamMixin<HttpOutputStream> {
public:
  HttpOutputStream(AsyncOutputStream& inner) : inner(inner) {}

  bool isInBody() { return inBody; }

  bool canReuse() { return !inBody && !broken && !writeInProgress; }

  bool canWriteBodyData() { return !writeInProgress && inBody; }

  bool isBroken() { return broken; }

  void writeHeaders(String content) {
    // Writes some header content and begins a new entity body.

    ZC_REQUIRE(!writeInProgress, "concurrent write()s not allowed") { return; }
    ZC_REQUIRE(!inBody, "previous HTTP message body incomplete; can't write more messages");
    inBody = true;

    queueWrite(zc::mv(content));
  }

  void writeBodyData(zc::String content) {
    ZC_REQUIRE(!writeInProgress, "concurrent write()s not allowed") { return; }
    ZC_REQUIRE(inBody) { return; }

    queueWrite(zc::mv(content));
  }

  zc::Promise<void> writeBodyData(ArrayPtr<const byte> buffer) {
    ZC_REQUIRE(!writeInProgress, "concurrent write()s not allowed");
    ZC_REQUIRE(inBody);

    writeInProgress = true;
    auto fork = writeQueue.fork();
    writeQueue = fork.addBranch();

    co_await fork;
    co_await inner.write(buffer);

    // We intentionally don't use ZC_DEFER to clean this up because if an exception is thrown, we
    // want to block further writes.
    writeInProgress = false;
  }

  zc::Promise<void> writeBodyData(zc::ArrayPtr<const zc::ArrayPtr<const byte>> pieces) {
    ZC_REQUIRE(!writeInProgress, "concurrent write()s not allowed");
    ZC_REQUIRE(inBody);

    writeInProgress = true;
    auto fork = writeQueue.fork();
    writeQueue = fork.addBranch();

    co_await fork;
    co_await inner.write(pieces);

    // We intentionally don't use ZC_DEFER to clean this up because if an exception is thrown, we
    // want to block further writes.
    writeInProgress = false;
  }

  Promise<uint64_t> pumpBodyFrom(AsyncInputStream& input, uint64_t amount) {
    ZC_REQUIRE(!writeInProgress, "concurrent write()s not allowed");
    ZC_REQUIRE(inBody);

    writeInProgress = true;
    auto fork = writeQueue.fork();
    writeQueue = fork.addBranch();

    co_await fork;
    auto actual = co_await input.pumpTo(inner, amount);

    // We intentionally don't use ZC_DEFER to clean this up because if an exception is thrown, we
    // want to block further writes.
    writeInProgress = false;
    co_return actual;
  }

  void finishBody() {
    // Called when entire body was written.

    ZC_REQUIRE(inBody) { return; }
    inBody = false;

    if (writeInProgress) {
      // It looks like the last write never completed -- possibly because it was canceled or threw
      // an exception. We must treat this equivalent to abortBody().
      broken = true;

      // Cancel any writes that are still queued.
      writeQueue =
          ZC_EXCEPTION(FAILED, "previous HTTP message body incomplete; can't write more messages");
    }
  }

  void abortBody() {
    // Called if the application failed to write all expected body bytes.
    ZC_REQUIRE(inBody) { return; }
    inBody = false;
    broken = true;

    // Cancel any writes that are still queued.
    writeQueue =
        ZC_EXCEPTION(FAILED, "previous HTTP message body incomplete; can't write more messages");
  }

  zc::Promise<void> flush() {
    auto fork = writeQueue.fork();
    writeQueue = fork.addBranch();
    return fork.addBranch();
  }

  Promise<void> whenWriteDisconnected() { return inner.whenWriteDisconnected(); }

  bool isWriteInProgress() { return writeInProgress; }

private:
  AsyncOutputStream& inner;
  zc::Promise<void> writeQueue = zc::READY_NOW;
  bool inBody = false;
  bool broken = false;

  bool writeInProgress = false;
  // True if a write method has been called and has not completed successfully. In the case that
  // a write throws an exception or is canceled, this remains true forever. In these cases, the
  // underlying stream is in an inconsistent state and cannot be reused.

  void queueWrite(zc::String content) {
    // We only use queueWrite() in cases where we can take ownership of the write buffer, and where
    // it is convenient if we can return `void` rather than a promise.  In particular, this is used
    // to write headers and chunk boundaries. Writes of application data do not go into
    // `writeQueue` because this would prevent cancellation. Instead, they wait until `writeQueue`
    // is empty, then they make the write directly, using `writeInProgress` to detect and block
    // concurrent writes.

    writeQueue = writeQueue.then([this, content = zc::mv(content)]() mutable {
      auto promise = inner.write(content.asBytes());
      return promise.attach(zc::mv(content));
    });
  }
};

class HttpEntityBodyWriter : public zc::AsyncOutputStream {
public:
  HttpEntityBodyWriter(HttpOutputStream& inner) { inner.setCurrentWrapper(weakInner); }
  ~HttpEntityBodyWriter() noexcept(false) {
    if (!finished) {
      ZC_IF_SOME(inner, weakInner) {
        inner.unsetCurrentWrapper(weakInner);
        inner.abortBody();
      }
      else {
        // Since we're in a destructor, log an error instead of throwing.
        ZC_LOG(ERROR, "HTTP body output stream outlived underlying connection",
               zc::getStackTrace());
      }
    }
  }

protected:
  HttpOutputStream& getInner() {
    ZC_IF_SOME(i, weakInner) { return i; }
    else if (finished) {
      // This is a bug in the implementations in this file, not the app.
      ZC_FAIL_ASSERT("bug in ZC HTTP: tried to access inner stream after it had been released");
    }
    else { ZC_FAIL_REQUIRE("HTTP body output stream outlived underlying connection"); }
  }

  void doneWriting() {
    auto& inner = getInner();
    inner.unsetCurrentWrapper(weakInner);
    finished = true;
    inner.finishBody();
  }

  inline bool alreadyDone() { return weakInner == zc::none; }

private:
  zc::Maybe<HttpOutputStream&> weakInner;
  bool finished = false;
};

class HttpNullEntityWriter final : public zc::AsyncOutputStream {
  // Does not inherit HttpEntityBodyWriter because it doesn't actually write anything.
public:
  Promise<void> write(ArrayPtr<const byte> buffer) override {
    return ZC_EXCEPTION(FAILED, "HTTP message has no entity-body; can't write()");
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return ZC_EXCEPTION(FAILED, "HTTP message has no entity-body; can't write()");
  }
  Promise<void> whenWriteDisconnected() override { return zc::NEVER_DONE; }
};

class HttpDiscardingEntityWriter final : public zc::AsyncOutputStream {
  // Does not inherit HttpEntityBodyWriter because it doesn't actually write anything.
public:
  Promise<void> write(ArrayPtr<const byte> buffer) override { return zc::READY_NOW; }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return zc::READY_NOW;
  }
  Promise<void> whenWriteDisconnected() override { return zc::NEVER_DONE; }
};

class HttpFixedLengthEntityWriter final : public HttpEntityBodyWriter {
public:
  HttpFixedLengthEntityWriter(HttpOutputStream& inner, uint64_t length)
      : HttpEntityBodyWriter(inner), length(length) {
    if (length == 0) doneWriting();
  }

  Promise<void> write(ArrayPtr<const byte> buffer) override {
    if (buffer == nullptr) co_return;
    ZC_REQUIRE(buffer.size() <= length, "overwrote Content-Length");
    length -= buffer.size();

    co_await getInner().writeBodyData(buffer);
    if (length == 0) doneWriting();
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    uint64_t size = 0;
    for (auto& piece : pieces) size += piece.size();

    if (size == 0) co_return;
    ZC_REQUIRE(size <= length, "overwrote Content-Length");
    length -= size;

    co_await getInner().writeBodyData(pieces);
    if (length == 0) doneWriting();
  }

  Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
    return pumpFrom(input, amount);
  }

  Promise<uint64_t> pumpFrom(AsyncInputStream& input, uint64_t amount) {
    if (amount == 0) co_return 0;

    bool overshot = amount > length;
    if (overshot) {
      // Hmm, the requested amount was too large, but it's common to specify zc::max as the amount
      // to pump, in which case we pump to EOF. Let's try to verify whether EOF is where we
      // expect it to be.
      ZC_IF_SOME(available, input.tryGetLength()) {
        // Great, the stream knows how large it is. If it's indeed larger than the space available
        // then let's abort.
        ZC_REQUIRE(available <= length, "overwrote Content-Length");
      }
      else {
        // OK, we have no idea how large the input is, so we'll have to check later.
      }
    }

    amount = zc::min(amount, length);
    length -= amount;
    uint64_t actual = amount;

    if (amount != 0) {
      actual = co_await getInner().pumpBodyFrom(input, amount);
      length += amount - actual;
      if (length == 0) doneWriting();
    }

    if (overshot) {
      if (actual == amount) {
        // We read exactly the amount expected. In order to detect an overshoot, we have to
        // try reading one more byte. Ugh.
        static byte junk;
        auto extra = co_await input.tryRead(&junk, 1, 1);
        ZC_REQUIRE(extra == 0, "overwrote Content-Length");
      } else {
        // We actually read less data than requested so we couldn't have overshot. In fact, we
        // undershot.
      }
    }

    co_return actual;
  }

  Promise<void> whenWriteDisconnected() override { return getInner().whenWriteDisconnected(); }

private:
  uint64_t length;
};

class HttpChunkedEntityWriter final : public HttpEntityBodyWriter {
public:
  HttpChunkedEntityWriter(HttpOutputStream& inner) : HttpEntityBodyWriter(inner) {}
  ~HttpChunkedEntityWriter() noexcept(false) {
    if (!alreadyDone()) {
      auto& inner = getInner();
      if (inner.canWriteBodyData()) {
        inner.writeBodyData(zc::str("0\r\n\r\n"));
        doneWriting();
      }
    }
  }

  Promise<void> write(ArrayPtr<const byte> buffer) override {
    if (buffer == nullptr)
      return zc::READY_NOW;  // can't encode zero-size chunk since it indicates EOF.

    auto header = zc::str(zc::hex(buffer.size()), "\r\n");
    auto parts = zc::heapArray<ArrayPtr<const byte>>(3);
    parts[0] = header.asBytes();
    parts[1] = buffer;
    parts[2] = zc::StringPtr("\r\n").asBytes();

    auto promise = getInner().writeBodyData(parts.asPtr());
    return promise.attach(zc::mv(header), zc::mv(parts));
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    uint64_t size = 0;
    for (auto& piece : pieces) size += piece.size();

    if (size == 0) return zc::READY_NOW;  // can't encode zero-size chunk since it indicates EOF.

    auto header = zc::str(zc::hex(size), "\r\n");
    auto partsBuilder = zc::heapArrayBuilder<ArrayPtr<const byte>>(pieces.size() + 2);
    partsBuilder.add(header.asBytes());
    for (auto& piece : pieces) { partsBuilder.add(piece); }
    partsBuilder.add(zc::StringPtr("\r\n").asBytes());

    auto parts = partsBuilder.finish();
    auto promise = getInner().writeBodyData(parts.asPtr());
    return promise.attach(zc::mv(header), zc::mv(parts));
  }

  Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
    ZC_IF_SOME(l, input.tryGetLength()) { return pumpImpl(input, zc::min(amount, l)); }
    else {
      // Need to use naive read/write loop.
      return zc::none;
    }
  }

  Promise<uint64_t> pumpImpl(AsyncInputStream& input, uint64_t length) {
    // Hey, we know exactly how large the input is, so we can write just one chunk.
    getInner().writeBodyData(zc::str(zc::hex(length), "\r\n"));
    auto actual = co_await getInner().pumpBodyFrom(input, length);

    if (actual < length) {
      getInner().abortBody();
      ZC_FAIL_REQUIRE(
          "value returned by input.tryGetLength() was greater than actual bytes transferred") {
        break;
      }
    }

    getInner().writeBodyData(zc::str("\r\n"));
    co_return actual;
  }

  Promise<void> whenWriteDisconnected() override { return getInner().whenWriteDisconnected(); }
};

// =======================================================================================

class WebSocketImpl final : public WebSocket, private WebSocketErrorHandler {
public:
  WebSocketImpl(zc::Own<zc::AsyncIoStream> stream, zc::Maybe<EntropySource&> maskKeyGenerator,
                zc::Maybe<CompressionParameters> compressionConfigParam = zc::none,
                zc::Maybe<WebSocketErrorHandler&> errorHandler = zc::none,
                zc::Array<byte> buffer = zc::heapArray<byte>(4096),
                zc::ArrayPtr<byte> leftover = nullptr,
                zc::Maybe<zc::Promise<void>> waitBeforeSend = zc::none)
      : stream(zc::mv(stream)),
        maskKeyGenerator(maskKeyGenerator),
        compressionConfig(zc::mv(compressionConfigParam)),
        errorHandler(errorHandler.orDefault(*this)),
        sendingControlMessage(zc::mv(waitBeforeSend)),
        recvBuffer(zc::mv(buffer)),
        recvData(leftover) {
#if ZC_HAS_ZLIB
    ZC_IF_SOME(config, compressionConfig) {
      compressionContext.emplace(ZlibContext::Mode::COMPRESS, config);
      decompressionContext.emplace(ZlibContext::Mode::DECOMPRESS, config);
    }
#else
    ZC_REQUIRE(compressionConfig == zc::none,
               "WebSocket compression is only supported if ZC is compiled with Zlib.");
#endif  // ZC_HAS_ZLIB
  }

  zc::Promise<void> send(zc::ArrayPtr<const byte> message) override {
    return sendImpl(OPCODE_BINARY, message);
  }

  zc::Promise<void> send(zc::ArrayPtr<const char> message) override {
    return sendImpl(OPCODE_TEXT, message.asBytes());
  }

  zc::Promise<void> close(uint16_t code, zc::StringPtr reason) override {
    zc::Array<byte> payload = serializeClose(code, reason);
    auto promise = sendImpl(OPCODE_CLOSE, payload);
    return promise.attach(zc::mv(payload));
  }

  void disconnect() override {
    // NOTE: While it's true that disconnect() is UB if called while a send is still in progress,
    // it would be inappropriate for us to assert !currentlySending here because currentlySending
    // remains true after a send is canceled, and it is OK to call disconnect() after canceling
    // a send.

    // If we're sending a control message (e.g. a PONG), cancel it.
    sendingControlMessage = zc::none;

    disconnected = true;
    stream->shutdownWrite();
  }

  void abort() override {
    queuedControlMessage = zc::none;
    sendingControlMessage = zc::none;
    disconnected = true;
    stream->abortRead();
    stream->shutdownWrite();
  }

  zc::Promise<void> whenAborted() override { return stream->whenWriteDisconnected(); }

  zc::Promise<Message> receive(size_t maxSize) override {
    ZC_IF_SOME(ex, receiveException) { return zc::cp(ex); }

    size_t headerSize = Header::headerSize(recvData.begin(), recvData.size());

    if (headerSize > recvData.size()) {
      if (recvData.begin() != recvBuffer.begin()) {
        // Move existing data to front of buffer.
        if (recvData.size() > 0) { memmove(recvBuffer.begin(), recvData.begin(), recvData.size()); }
        recvData = recvBuffer.first(recvData.size());
      }

      return stream->tryRead(recvData.end(), 1, recvBuffer.end() - recvData.end())
          .then([this, maxSize](size_t actual) -> zc::Promise<Message> {
            receivedBytes += actual;
            if (actual == 0) {
              if (recvData.size() > 0) {
                return ZC_EXCEPTION(DISCONNECTED, "WebSocket EOF in frame header");
              } else {
                // It's incorrect for the WebSocket to disconnect without sending `Close`.
                return ZC_EXCEPTION(
                    DISCONNECTED, "WebSocket disconnected between frames without sending `Close`.");
              }
            }

            recvData = recvBuffer.first(recvData.size() + actual);
            return receive(maxSize);
          });
    }

    auto& recvHeader = *reinterpret_cast<Header*>(recvData.begin());
    if (recvHeader.hasRsv2or3()) {
      return sendCloseDueToError(1002, "Received frame had RSV bits 2 or 3 set");
    }

    recvData = recvData.slice(headerSize, recvData.size());

    size_t payloadLen = recvHeader.getPayloadLen();
    if (payloadLen > maxSize) {
      auto description = zc::str("Message is too large: ", payloadLen, " > ", maxSize);
      return sendCloseDueToError(1009, description.asPtr()).attach(zc::mv(description));
    }

    auto opcode = recvHeader.getOpcode();
    bool isData = opcode < OPCODE_FIRST_CONTROL;
    if (opcode == OPCODE_CONTINUATION) {
      if (fragments.empty()) { return sendCloseDueToError(1002, "Unexpected continuation frame"); }

      opcode = fragmentOpcode;
    } else if (isData) {
      if (!fragments.empty()) { return sendCloseDueToError(1002, "Missing continuation frame"); }
    }

    bool isFin = recvHeader.isFin();
    bool isCompressed = false;

    zc::Array<byte> message;            // space to allocate
    byte* payloadTarget;                // location into which to read payload (size is payloadLen)
    zc::Maybe<size_t> originalMaxSize;  // maxSize from first `receive()` call
    if (isFin) {
      size_t amountToAllocate;
      if (recvHeader.isCompressed() || fragmentCompressed) {
        // Add 4 since we append 0x00 0x00 0xFF 0xFF to the tail of the payload.
        // See: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.2
        amountToAllocate = payloadLen + 4;
        isCompressed = true;
      } else {
        // Add space for NUL terminator when allocating text message.
        amountToAllocate = payloadLen + (opcode == OPCODE_TEXT && isFin);
      }

      if (isData && !fragments.empty()) {
        // Final frame of a fragmented message. Gather the fragments.
        size_t offset = 0;
        for (auto& fragment : fragments) offset += fragment.size();
        message = zc::heapArray<byte>(offset + amountToAllocate);
        originalMaxSize = offset + maxSize;  // gives us back the original maximum message size.

        offset = 0;
        for (auto& fragment : fragments) {
          memcpy(message.begin() + offset, fragment.begin(), fragment.size());
          offset += fragment.size();
        }
        payloadTarget = message.begin() + offset;

        fragments.clear();
        fragmentOpcode = 0;
        fragmentCompressed = false;
      } else {
        // Single-frame message.
        message = zc::heapArray<byte>(amountToAllocate);
        originalMaxSize = maxSize;  // gives us back the original maximum message size.
        payloadTarget = message.begin();
      }
    } else {
      // Fragmented message, and this isn't the final fragment.
      if (!isData) { return sendCloseDueToError(1002, "Received fragmented control frame"); }

      message = zc::heapArray<byte>(payloadLen);
      payloadTarget = message.begin();
      if (fragments.empty()) {
        // This is the first fragment, so set the opcode.
        fragmentOpcode = opcode;
        fragmentCompressed = recvHeader.isCompressed();
      }
    }

    Mask mask = recvHeader.getMask();

    auto handleMessage = [this, opcode, payloadTarget, payloadLen, mask, isFin, maxSize,
                          originalMaxSize, isCompressed,
                          message = zc::mv(message)]() mutable -> zc::Promise<Message> {
      if (!mask.isZero()) { mask.apply(zc::arrayPtr(payloadTarget, payloadLen)); }

      if (!isFin) {
        // Add fragment to the list and loop.
        auto newMax = maxSize - message.size();
        fragments.add(zc::mv(message));
        return receive(newMax);
      }

      // Provide a reasonable error if a compressed frame is received without compression enabled.
      if (isCompressed && compressionConfig == zc::none) {
        return sendCloseDueToError(
            1002,
            "Received a WebSocket frame whose compression bit was set, but the compression "
            "extension was not negotiated for this connection.");
      }

      switch (opcode) {
        case OPCODE_CONTINUATION:
          // Shouldn't get here; handled above.
          ZC_UNREACHABLE;
        case OPCODE_TEXT:
#if ZC_HAS_ZLIB
          if (isCompressed) {
            auto& config = ZC_ASSERT_NONNULL(compressionConfig);
            auto& decompressor = ZC_ASSERT_NONNULL(decompressionContext);
            ZC_ASSERT(message.size() >= 4);
            auto tail = message.slice(message.size() - 4, message.size());
            // Note that we added an additional 4 bytes to `message`s capacity to account for these
            // extra bytes. See `amountToAllocate` in the if(recvHeader.isCompressed()) block above.
            const byte tailBytes[] = {0x00, 0x00, 0xFF, 0xFF};
            memcpy(tail.begin(), tailBytes, sizeof(tailBytes));
            // We have to append 0x00 0x00 0xFF 0xFF to the message before inflating.
            // See: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.2
            if (config.inboundNoContextTakeover) {
              // We must reset context on each message.
              decompressor.reset();
            }
            bool addNullTerminator = true;
            // We want to add the null terminator when receiving a TEXT message.
            auto decompressedOrError =
                decompressor.processMessage(message, originalMaxSize, addNullTerminator);
            ZC_SWITCH_ONEOF(decompressedOrError) {
              ZC_CASE_ONEOF(protocolError, ProtocolError) {
                return sendCloseDueToError(protocolError.statusCode, protocolError.description)
                    .attach(zc::mv(decompressedOrError));
              }
              ZC_CASE_ONEOF(decompressed, zc::Array<byte>) {
                return Message(zc::String(decompressed.releaseAsChars()));
              }
            }
          }
#endif  // ZC_HAS_ZLIB
          message.back() = '\0';
          return Message(zc::String(message.releaseAsChars()));
        case OPCODE_BINARY:
#if ZC_HAS_ZLIB
          if (isCompressed) {
            auto& config = ZC_ASSERT_NONNULL(compressionConfig);
            auto& decompressor = ZC_ASSERT_NONNULL(decompressionContext);
            ZC_ASSERT(message.size() >= 4);
            auto tail = message.slice(message.size() - 4, message.size());
            // Note that we added an additional 4 bytes to `message`s capacity to account for these
            // extra bytes. See `amountToAllocate` in the if(recvHeader.isCompressed()) block above.
            const byte tailBytes[] = {0x00, 0x00, 0xFF, 0xFF};
            memcpy(tail.begin(), tailBytes, sizeof(tailBytes));
            // We have to append 0x00 0x00 0xFF 0xFF to the message before inflating.
            // See: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.2
            if (config.inboundNoContextTakeover) {
              // We must reset context on each message.
              decompressor.reset();
            }

            auto decompressedOrError = decompressor.processMessage(message, originalMaxSize);
            ZC_SWITCH_ONEOF(decompressedOrError) {
              ZC_CASE_ONEOF(protocolError, ProtocolError) {
                return sendCloseDueToError(protocolError.statusCode, protocolError.description)
                    .attach(zc::mv(decompressedOrError));
              }
              ZC_CASE_ONEOF(decompressed, zc::Array<byte>) {
                return Message(decompressed.releaseAsBytes());
              }
            }
          }
#endif  // ZC_HAS_ZLIB
          return Message(message.releaseAsBytes());
        case OPCODE_CLOSE:
          if (message.size() < 2) {
            return Message(Close{1005, nullptr});
          } else {
            uint16_t status =
                (static_cast<uint16_t>(message[0]) << 8) | (static_cast<uint16_t>(message[1]));
            return Message(
                Close{status, zc::heapString(message.slice(2, message.size()).asChars())});
          }
        case OPCODE_PING:
          // Send back a pong.
          queuePong(zc::mv(message));
          return receive(maxSize);
        case OPCODE_PONG:
          // Unsolicited pong. Ignore.
          return receive(maxSize);
        default: {
          auto description = zc::str("Unknown opcode ", opcode);
          return sendCloseDueToError(1002, description.asPtr()).attach(zc::mv(description));
        }
      }
    };

    if (payloadLen <= recvData.size()) {
      // All data already received.
      memcpy(payloadTarget, recvData.begin(), payloadLen);
      recvData = recvData.slice(payloadLen, recvData.size());
      return handleMessage();
    } else {
      // Need to read more data.
      memcpy(payloadTarget, recvData.begin(), recvData.size());
      size_t remaining = payloadLen - recvData.size();
      auto promise = stream->tryRead(payloadTarget + recvData.size(), remaining, remaining)
                         .then([this, remaining](size_t amount) {
                           receivedBytes += amount;
                           if (amount < remaining) {
                             zc::throwRecoverableException(
                                 ZC_EXCEPTION(DISCONNECTED, "WebSocket EOF in message"));
                           }
                         });
      recvData = nullptr;
      return promise.then(zc::mv(handleMessage));
    }
  }

  zc::Maybe<zc::Promise<void>> tryPumpFrom(WebSocket& other) override {
    ZC_IF_SOME(optOther, zc::dynamicDowncastIfAvailable<WebSocketImpl>(other)) {
      // Both WebSockets are raw WebSockets, so we can pump the streams directly rather than read
      // whole messages.

      if ((maskKeyGenerator == zc::none) == (optOther.maskKeyGenerator == zc::none)) {
        // Oops, it appears that we either believe we are the client side of both sockets, or we
        // are the server side of both sockets. Since clients must "mask" their outgoing frames but
        // servers must *not* do so, we can't direct-pump. Sad.
        return zc::none;
      }

      ZC_IF_SOME(config, compressionConfig) {
        ZC_IF_SOME(otherConfig, optOther.compressionConfig) {
          if (config.outboundMaxWindowBits != otherConfig.inboundMaxWindowBits ||
              config.inboundMaxWindowBits != otherConfig.outboundMaxWindowBits ||
              config.inboundNoContextTakeover != otherConfig.outboundNoContextTakeover ||
              config.outboundNoContextTakeover != otherConfig.inboundNoContextTakeover) {
            // Compression configurations differ.
            return zc::none;
          }
        }
        else {
          // Only one websocket uses compression.
          return zc::none;
        }
      }
      else {
        if (optOther.compressionConfig != zc::none) {
          // Only one websocket uses compression.
          return zc::none;
        }
      }
      // Both websockets use compatible compression configurations so we can pump directly.

      // Check same error conditions as with sendImpl().
      ZC_REQUIRE(!disconnected, "WebSocket can't send after disconnect()");
      ZC_REQUIRE(!currentlySending, "another message send is already in progress");
      currentlySending = true;

      // If the application chooses to pump messages out, but receives incoming messages normally
      // with `receive()`, then we will receive pings and attempt to send pongs. But we can't
      // safely insert a pong in the middle of a pumped stream. We kind of don't have a choice
      // except to drop them on the floor, which is what will happen if we set `hasSentClose` true.
      // Hopefully most apps that set up a pump do so in both directions at once, and so pings will
      // flow through and pongs will flow back.
      hasSentClose = true;

      return optOther.optimizedPumpTo(*this);
    }

    return zc::none;
  }

  uint64_t sentByteCount() override { return sentBytes; }

  uint64_t receivedByteCount() override { return receivedBytes; }

  zc::Maybe<zc::String> getPreferredExtensions(ExtensionsContext ctx) override {
    if (maskKeyGenerator == zc::none) {
      // `this` is the server side of a websocket.
      if (ctx == ExtensionsContext::REQUEST) {
        // The other WebSocket is (going to be) the client side of a WebSocket, i.e. this is a
        // proxying pass-through scenario. Optimization is possible. Confusingly, we have to use
        // generateExtensionResponse() (even though we're generating headers to be passed in a
        // request) because this is the function that correctly maps our config's inbound/outbound
        // to client/server.
        ZC_IF_SOME(c, compressionConfig) { return _::generateExtensionResponse(c); }
        else {
          return zc::String(nullptr);  // recommend no compression
        }
      } else {
        // We're apparently arranging to pump from the server side of one WebSocket to the server
        // side of another; i.e., we are a server, we have two clients, and we're trying to pump
        // between them. We cannot optimize this case, because the masking requirements are
        // different for client->server vs. server->client messages. Since we have to parse out
        // the messages anyway there's no point in trying to match extensions, so return null.
        return zc::none;
      }
    } else {
      // `this` is the client side of a websocket.
      if (ctx == ExtensionsContext::RESPONSE) {
        // The other WebSocket is (going to be) the server side of a WebSocket, i.e. this is a
        // proxying pass-through scenario. Optimization is possible. Confusingly, we have to use
        // generateExtensionRequest() (even though we're generating headers to be passed in a
        // response) because this is the function that correctly maps our config's inbound/outbound
        // to server/client.
        ZC_IF_SOME(c, compressionConfig) {
          CompressionParameters arr[1]{c};
          return _::generateExtensionRequest(arr);
        }
        else {
          return zc::String(nullptr);  // recommend no compression
        }
      } else {
        // We're apparently arranging to pump from the client side of one WebSocket to the client
        // side of another; i.e., we are a client, we are connected to two servers, and we're
        // trying to pump between them. We cannot optimize this case, because the masking
        // requirements are different for client->server vs. server->client messages. Since we have
        // to parse out the messages anyway there's no point in trying to match extensions, so
        // return null.
        return zc::none;
      }
    }
  }

private:
  class Mask {
  public:
    Mask() : maskBytes{0, 0, 0, 0} {}
    Mask(const byte* ptr) { memcpy(maskBytes, ptr, 4); }

    Mask(zc::Maybe<EntropySource&> generator) {
      ZC_IF_SOME(g, generator) { g.generate(maskBytes); }
      else { memset(maskBytes, 0, 4); }
    }

    void apply(zc::ArrayPtr<byte> bytes) const { apply(bytes.begin(), bytes.size()); }

    void copyTo(byte* output) const { memcpy(output, maskBytes, 4); }

    bool isZero() const { return (maskBytes[0] | maskBytes[1] | maskBytes[2] | maskBytes[3]) == 0; }

  private:
    byte maskBytes[4];

    void apply(byte* __restrict__ bytes, size_t size) const {
      for (size_t i = 0; i < size; i++) { bytes[i] ^= maskBytes[i % 4]; }
    }
  };

  class Header {
  public:
    zc::ArrayPtr<const byte> compose(bool fin, bool compressed, byte opcode, uint64_t payloadLen,
                                     Mask mask) {
      bytes[0] = (fin ? FIN_MASK : 0) | (compressed ? RSV1_MASK : 0) | opcode;
      // Note that we can only set the compressed bit on DATA frames.
      bool hasMask = !mask.isZero();

      size_t fill;

      if (payloadLen < 126) {
        bytes[1] = (hasMask ? USE_MASK_MASK : 0) | payloadLen;
        if (hasMask) {
          mask.copyTo(bytes + 2);
          fill = 6;
        } else {
          fill = 2;
        }
      } else if (payloadLen < 65536) {
        bytes[1] = (hasMask ? USE_MASK_MASK : 0) | 126;
        bytes[2] = static_cast<byte>(payloadLen >> 8);
        bytes[3] = static_cast<byte>(payloadLen);
        if (hasMask) {
          mask.copyTo(bytes + 4);
          fill = 8;
        } else {
          fill = 4;
        }
      } else {
        bytes[1] = (hasMask ? USE_MASK_MASK : 0) | 127;
        bytes[2] = static_cast<byte>(payloadLen >> 56);
        bytes[3] = static_cast<byte>(payloadLen >> 48);
        bytes[4] = static_cast<byte>(payloadLen >> 40);
        bytes[5] = static_cast<byte>(payloadLen >> 42);
        bytes[6] = static_cast<byte>(payloadLen >> 24);
        bytes[7] = static_cast<byte>(payloadLen >> 16);
        bytes[8] = static_cast<byte>(payloadLen >> 8);
        bytes[9] = static_cast<byte>(payloadLen);
        if (hasMask) {
          mask.copyTo(bytes + 10);
          fill = 14;
        } else {
          fill = 10;
        }
      }

      return arrayPtr(bytes, fill);
    }

    bool isFin() const { return bytes[0] & FIN_MASK; }

    bool isCompressed() const { return bytes[0] & RSV1_MASK; }

    bool hasRsv2or3() const { return bytes[0] & RSV2_3_MASK; }

    byte getOpcode() const { return bytes[0] & OPCODE_MASK; }

    uint64_t getPayloadLen() const {
      byte payloadLen = bytes[1] & PAYLOAD_LEN_MASK;
      if (payloadLen == 127) {
        return (static_cast<uint64_t>(bytes[2]) << 56) | (static_cast<uint64_t>(bytes[3]) << 48) |
               (static_cast<uint64_t>(bytes[4]) << 40) | (static_cast<uint64_t>(bytes[5]) << 32) |
               (static_cast<uint64_t>(bytes[6]) << 24) | (static_cast<uint64_t>(bytes[7]) << 16) |
               (static_cast<uint64_t>(bytes[8]) << 8) | (static_cast<uint64_t>(bytes[9]));
      } else if (payloadLen == 126) {
        return (static_cast<uint64_t>(bytes[2]) << 8) | (static_cast<uint64_t>(bytes[3]));
      } else {
        return payloadLen;
      }
    }

    Mask getMask() const {
      if (bytes[1] & USE_MASK_MASK) {
        byte payloadLen = bytes[1] & PAYLOAD_LEN_MASK;
        if (payloadLen == 127) {
          return Mask(bytes + 10);
        } else if (payloadLen == 126) {
          return Mask(bytes + 4);
        } else {
          return Mask(bytes + 2);
        }
      } else {
        return Mask();
      }
    }

    static size_t headerSize(byte const* bytes, size_t sizeSoFar) {
      if (sizeSoFar < 2) return 2;

      size_t required = 2;

      if (bytes[1] & USE_MASK_MASK) { required += 4; }

      byte payloadLen = bytes[1] & PAYLOAD_LEN_MASK;
      if (payloadLen == 127) {
        required += 8;
      } else if (payloadLen == 126) {
        required += 2;
      }

      return required;
    }

  private:
    byte bytes[14];

    static constexpr byte FIN_MASK = 0x80;
    static constexpr byte RSV2_3_MASK = 0x30;
    static constexpr byte RSV1_MASK = 0x40;
    static constexpr byte OPCODE_MASK = 0x0f;

    static constexpr byte USE_MASK_MASK = 0x80;
    static constexpr byte PAYLOAD_LEN_MASK = 0x7f;
  };

#if ZC_HAS_ZLIB
  class ZlibContext {
    // `ZlibContext` is the WebSocket's interface to Zlib's compression/decompression functions.
    // Depending on the `mode`, `ZlibContext` will act as a compressor or a decompressor.
  public:
    enum class Mode{
        COMPRESS,
        DECOMPRESS,
    };

    struct Result {
      int processResult = 0;
      zc::Array<const byte> buffer;
      size_t size = 0;  // Number of bytes used; size <= buffer.size().
    };

    ZlibContext(Mode mode, const CompressionParameters& config) : mode(mode) {
      switch (mode) {
        case Mode::COMPRESS: {
          int windowBits = -config.outboundMaxWindowBits.orDefault(15);
          // We use negative values because we want to use raw deflate.
          if (windowBits == -8) {
            // Zlib cannot accept `windowBits` of 8 for the deflater. However, due to an
            // implementation quirk, `windowBits` of 8 and 9 would both use 250 bytes.
            // Therefore, a decompressor using `windowBits` of 8 could safely inflate a message
            // that a zlib client compressed using `windowBits` = 9.
            // https://bugs.chromium.org/p/chromium/issues/detail?id=691074
            windowBits = -9;
          }
          int result = deflateInit2(&ctx, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits,
                                    8,  // memLevel = 8 is the default
                                    Z_DEFAULT_STRATEGY);
          ZC_REQUIRE(result == Z_OK, "Failed to initialize compression context (deflate).");
          break;
        }
        case Mode::DECOMPRESS: {
          int windowBits = -config.inboundMaxWindowBits.orDefault(15);
          // We use negative values because we want to use raw inflate.
          int result = inflateInit2(&ctx, windowBits);
          ZC_REQUIRE(result == Z_OK, "Failed to initialize decompression context (inflate).");
          break;
        }
      }
    }

    ~ZlibContext() noexcept(false) {
      switch (mode) {
        case Mode::COMPRESS:
          deflateEnd(&ctx);
          break;
        case Mode::DECOMPRESS:
          inflateEnd(&ctx);
          break;
      }
    }

    ZC_DISALLOW_COPY_AND_MOVE(ZlibContext);

    zc::OneOf<zc::Array<zc::byte>, ProtocolError> processMessage(
        zc::ArrayPtr<const byte> message, zc::Maybe<size_t> maxSize = zc::none,
        bool addNullTerminator = false) {
      // If `this` is the compressor, calling `processMessage()` will compress the `message`.
      // Likewise, if `this` is the decompressor, `processMessage()` will decompress the `message`.
      //
      // `maxSize` is only passed in when decompressing, since we want to ensure the decompressed
      // message is smaller than the `maxSize` passed to `receive()`.
      //
      // If (de)compression is successful, the result is returned as a Vector, otherwise,
      // an Exception is thrown.

      ctx.next_in = const_cast<byte*>(reinterpret_cast<const byte*>(message.begin()));
      ctx.avail_in = message.size();

      zc::Vector<Result> parts;
      ZC_SWITCH_ONEOF(processLoop(maxSize)) {
        ZC_CASE_ONEOF(protocolError, ProtocolError) { return zc::mv(protocolError); }
        ZC_CASE_ONEOF(dataParts, zc::Vector<Result>) { parts = zc::mv(dataParts); }
      }

      size_t amountToAllocate = 0;
      for (const auto& part : parts) { amountToAllocate += part.size; }

      if (addNullTerminator) {
        // Add space for the null-terminator.
        amountToAllocate += 1;
      }

      zc::Array<zc::byte> processedMessage = zc::heapArray<zc::byte>(amountToAllocate);
      size_t currentIndex = 0;  // Current index into processedMessage.
      for (const auto& part : parts) {
        memcpy(&processedMessage[currentIndex], part.buffer.begin(), part.size);
        // We need to use `part.size` to determine the number of useful bytes, since data after
        // `part.size` is unused (and probably junk).
        currentIndex += part.size;
      }

      if (addNullTerminator) { processedMessage[currentIndex++] = '\0'; }

      ZC_ASSERT(currentIndex == processedMessage.size());

      return zc::mv(processedMessage);
    }

    void reset() {
      // Resets the (de)compression context. This should only be called when the (de)compressor uses
      // client/server_no_context_takeover.
      switch (mode) {
        case Mode::COMPRESS: {
          ZC_ASSERT(deflateReset(&ctx) == Z_OK, "deflateReset() failed.");
          break;
        }
        case Mode::DECOMPRESS: {
          ZC_ASSERT(inflateReset(&ctx) == Z_OK, "inflateReset failed.");
          break;
        }
      }
    }

  private:
    Result pumpOnce() {
      // Prepares Zlib's internal state for a call to deflate/inflate, then calls the relevant
      // function to process the input buffer. It is assumed that the caller has already set up
      // Zlib's input buffer.
      //
      // Since calls to deflate/inflate will process data until the input is empty, or until the
      // output is full, multiple calls to `pumpOnce()` may be required to process the entire
      // message. We're done processing once either `result` is `Z_STREAM_END`, or we get
      // `Z_BUF_ERROR` and did not write any more output.
      size_t bufSize = 4096;
      Array<zc::byte> buffer = zc::heapArray<zc::byte>(bufSize);
      ctx.next_out = buffer.begin();
      ctx.avail_out = bufSize;

      int result = Z_OK;

      switch (mode) {
        case Mode::COMPRESS:
          result = deflate(&ctx, Z_SYNC_FLUSH);
          ZC_REQUIRE(result == Z_OK || result == Z_BUF_ERROR || result == Z_STREAM_END,
                     "Compression failed", result);
          break;
        case Mode::DECOMPRESS:
          result = inflate(&ctx, Z_SYNC_FLUSH);
          ZC_REQUIRE(result == Z_OK || result == Z_BUF_ERROR || result == Z_STREAM_END,
                     "Decompression failed", result, " with reason", ctx.msg);
          break;
      }

      return Result{
          result,
          zc::mv(buffer),
          bufSize - ctx.avail_out,
      };
    }

    zc::OneOf<zc::Vector<Result>, ProtocolError> processLoop(zc::Maybe<size_t> maxSize) {
      // Since Zlib buffers the writes, we want to continue processing until there's nothing left.
      zc::Vector<Result> output;
      size_t totalBytesProcessed = 0;
      for (;;) {
        Result result = pumpOnce();

        auto status = result.processResult;
        auto bytesProcessed = result.size;
        if (bytesProcessed > 0) {
          output.add(zc::mv(result));
          totalBytesProcessed += bytesProcessed;
          ZC_IF_SOME(m, maxSize) {
            // This is only non-null for `receive` calls, so we must be decompressing. We don't want
            // the decompressed message to OOM us, so let's make sure it's not too big.
            if (totalBytesProcessed > m) {
              return ProtocolError{.statusCode = 1009, .description = "Message is too large"};
            }
          }
        }

        if ((ctx.avail_in == 0 && ctx.avail_out != 0) || status == Z_STREAM_END) {
          // If we're out of input to consume, and we have space in the output buffer, then we must
          // have flushed the remaining message, so we're done pumping. Alternatively, if we found a
          // BFINAL deflate block, then we know the stream is completely finished.
          if (status == Z_STREAM_END) { reset(); }
          return zc::mv(output);
        }
      }
    }

    Mode mode;
    z_stream ctx = {};
  };
#endif  // ZC_HAS_ZLIB

  static constexpr byte OPCODE_CONTINUATION = 0;
  static constexpr byte OPCODE_TEXT = 1;
  static constexpr byte OPCODE_BINARY = 2;
  static constexpr byte OPCODE_CLOSE = 8;
  static constexpr byte OPCODE_PING = 9;
  static constexpr byte OPCODE_PONG = 10;

  static constexpr byte OPCODE_FIRST_CONTROL = 8;
  static constexpr byte OPCODE_MAX = 15;

  // ---------------------------------------------------------------------------

  zc::Own<zc::AsyncIoStream> stream;
  zc::Maybe<EntropySource&> maskKeyGenerator;
  zc::Maybe<CompressionParameters> compressionConfig;
  WebSocketErrorHandler& errorHandler;
#if ZC_HAS_ZLIB
  zc::Maybe<ZlibContext> compressionContext;
  zc::Maybe<ZlibContext> decompressionContext;
#endif  // ZC_HAS_ZLIB

  bool hasSentClose = false;
  bool disconnected = false;
  bool currentlySending = false;
  Header sendHeader;

  struct ControlMessage {
    byte opcode;
    zc::Array<byte> payload;
    zc::Maybe<zc::Own<zc::PromiseFulfiller<void>>> fulfiller;

    ControlMessage(byte opcodeParam, zc::Array<byte> payloadParam,
                   zc::Maybe<zc::Own<zc::PromiseFulfiller<void>>> fulfillerParam)
        : opcode(opcodeParam), payload(zc::mv(payloadParam)), fulfiller(zc::mv(fulfillerParam)) {
      ZC_REQUIRE(opcode <= OPCODE_MAX);
    }
  };

  zc::Maybe<zc::Exception> receiveException;
  // If set, all future calls to receive() will throw this exception.

  zc::Maybe<ControlMessage> queuedControlMessage;
  // queuedControlMessage holds the body of the next control message to write; it is cleared when
  // the message is written.
  //
  // It may be overwritten; for example, if a more recent ping arrives before the pong is actually
  // written, we can update this value to instead respond to the more recent ping. If a bad frame
  // shows up, we can overwrite any queued pong with a Close message.
  //
  // Currently, this holds either a Close or a Pong.

  zc::Maybe<zc::Promise<void>> sendingControlMessage;
  // If a control message is being sent asynchronously (e.g., a Pong in response to a Ping), this is
  // a promise for the completion of that send.
  //
  // Additionally, this member is used if we need to block our first send on WebSocket startup,
  // e.g. because we need to wait for HTTP handshake writes to flush before we can start sending
  // WebSocket data. `sendingControlMessage` was overloaded for this use case because the logic is
  // the same. Perhaps it should be renamed to `blockSend` or `writeQueue`.

  uint fragmentOpcode = 0;
  bool fragmentCompressed = false;
  // For fragmented messages, was the first frame compressed?
  // Note that subsequent frames of a compressed message will not set the RSV1 bit.
  zc::Vector<zc::Array<byte>> fragments;
  // If `fragments` is non-empty, we've already received some fragments of a message.
  // `fragmentOpcode` is the original opcode.

  zc::Array<byte> recvBuffer;
  zc::ArrayPtr<byte> recvData;

  uint64_t sentBytes = 0;
  uint64_t receivedBytes = 0;

  zc::Promise<void> sendImpl(byte opcode, zc::ArrayPtr<const byte> message) {
    ZC_REQUIRE(!disconnected, "WebSocket can't send after disconnect()");
    ZC_REQUIRE(!currentlySending, "another message send is already in progress");

    currentlySending = true;

    for (;;) {
      ZC_IF_SOME(p, sendingControlMessage) {
        // Re-check in case of disconnect on a previous loop iteration.
        ZC_REQUIRE(!disconnected, "WebSocket can't send after disconnect()");

        // We recently sent a control message; make sure it's finished before proceeding.
        auto localPromise = zc::mv(p);
        sendingControlMessage = zc::none;
        co_await localPromise;
      }
      else { break; }
    }

    // We don't stop the application from sending further messages after close() -- this is the
    // application's error to make. But, we do want to make sure we don't send any PONGs after a
    // close, since that would be our error. So we stack whether we closed for that reason.
    hasSentClose = hasSentClose || opcode == OPCODE_CLOSE;

    Mask mask(maskKeyGenerator);

    bool useCompression = false;
    zc::Maybe<zc::Array<byte>> compressedMessage;
    if (opcode == OPCODE_BINARY || opcode == OPCODE_TEXT) {
      // We can only compress data frames.
#if ZC_HAS_ZLIB
      ZC_IF_SOME(config, compressionConfig) {
        useCompression = true;
        // Compress `message` according to `compressionConfig`s outbound parameters.
        auto& compressor = ZC_ASSERT_NONNULL(compressionContext);
        if (config.outboundNoContextTakeover) {
          // We must reset context on each message.
          compressor.reset();
        }

        ZC_SWITCH_ONEOF(compressor.processMessage(message)) {
          ZC_CASE_ONEOF(error, ProtocolError) {
            ZC_FAIL_REQUIRE("Error compressing websocket message: ", error.description);
          }
          ZC_CASE_ONEOF(compressed, zc::Array<byte>) {
            auto& innerMessage = compressedMessage.emplace(zc::mv(compressed));
            if (message.size() > 0) {
              ZC_ASSERT(innerMessage.asPtr().endsWith({0x00, 0x00, 0xFF, 0xFF}));
              message = innerMessage.first(innerMessage.size() - 4);
              // Strip 0x00 0x00 0xFF 0xFF off the tail.
              // See: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.1
            } else {
              // RFC 7692 (7.2.3.6) specifies that an empty uncompressed DEFLATE block (0x00) should
              // be built if the compression library doesn't generate data when the input is empty.
              message = compressedMessage.emplace(zc::heapArray<byte>({0x00}));
            }
          }
        }
      }
#endif  // ZC_HAS_ZLIB
    }

    zc::Array<byte> ownMessage;
    if (!mask.isZero()) {
      // Sadness, we have to make a copy to apply the mask.
      ownMessage = zc::heapArray(message);
      mask.apply(ownMessage);
      message = ownMessage;
    }

    zc::ArrayPtr<const byte> sendParts[2];
    sendParts[0] = sendHeader.compose(true, useCompression, opcode, message.size(), mask);
    sendParts[1] = message;
    ZC_ASSERT(!sendHeader.hasRsv2or3(),
              "RSV bits 2 and 3 must be 0, as we do not currently "
              "support an extension that would set these bits");

    co_await stream->write(sendParts);
    currentlySending = false;

    // Send queued control message if needed.
    if (queuedControlMessage != zc::none) { setUpSendingControlMessage(); };
    sentBytes += sendParts[0].size() + sendParts[1].size();
    ;
  }

  void queueClose(uint16_t code, zc::StringPtr reason,
                  zc::Own<zc::PromiseFulfiller<void>> fulfiller) {
    bool alreadyWaiting = (queuedControlMessage != zc::none);

    // Overwrite any previously-queued message. If there is one, it's just a Pong, and this Close
    // supersedes it.
    auto payload = serializeClose(code, reason);
    queuedControlMessage = ControlMessage(OPCODE_CLOSE, zc::mv(payload), zc::mv(fulfiller));

    if (!alreadyWaiting) { setUpSendingControlMessage(); }
  }

  zc::Array<byte> serializeClose(uint16_t code, zc::StringPtr reason) {
    zc::Array<byte> payload;
    if (code == 1005) {
      ZC_REQUIRE(reason.size() == 0, "WebSocket close code 1005 cannot have a reason");

      // code 1005 -- leave payload empty
    } else {
      payload = heapArray<byte>(reason.size() + 2);
      payload[0] = code >> 8;
      payload[1] = code;
      memcpy(payload.begin() + 2, reason.begin(), reason.size());
    }
    return zc::mv(payload);
  }

  zc::Promise<Message> sendCloseDueToError(uint16_t code, zc::StringPtr reason) {
    auto paf = newPromiseAndFulfiller<void>();
    queueClose(code, reason, zc::mv(paf.fulfiller));

    return paf.promise.then([this, code, reason]() -> zc::Promise<Message> {
      return errorHandler.handleWebSocketProtocolError({code, reason});
    });
  }

  void queuePong(zc::Array<byte> payload) {
    bool alreadyWaitingForPongWrite = false;

    ZC_IF_SOME(controlMessage, queuedControlMessage) {
      if (controlMessage.opcode == OPCODE_CLOSE) {
        // We're currently sending a Close message, which we only do (at least via
        // queuedControlMessage) when we're closing the connection due to error. There's no point
        // queueing a Pong that'll never be sent.
        return;
      } else {
        ZC_ASSERT(controlMessage.opcode == OPCODE_PONG);
        alreadyWaitingForPongWrite = true;
      }
    }

    // Note: According to spec, if the server receives a second ping before responding to the
    //   previous one, it can opt to respond only to the last ping. So we don't have to check if
    //   queuedControlMessage is already non-null.
    queuedControlMessage = ControlMessage(OPCODE_PONG, zc::mv(payload), zc::none);

    if (currentlySending) {
      // There is a message-send in progress, so we cannot write to the stream now.  We will set
      // up the control message write at the end of the message-send.
      return;
    }
    if (alreadyWaitingForPongWrite) {
      // We were already waiting for a pong to be written; don't need to queue another write.
      return;
    }
    setUpSendingControlMessage();
  }

  void setUpSendingControlMessage() {
    ZC_IF_SOME(promise, sendingControlMessage) {
      sendingControlMessage =
          promise.then([this]() mutable { return writeQueuedControlMessage(); });
    }
    else { sendingControlMessage = writeQueuedControlMessage(); }
  }

  zc::Promise<void> writeQueuedControlMessage() {
    ZC_IF_SOME(q, queuedControlMessage) {
      byte opcode = q.opcode;
      zc::Array<byte> payload = zc::mv(q.payload);
      auto maybeFulfiller = zc::mv(q.fulfiller);
      queuedControlMessage = zc::none;

      if (hasSentClose || disconnected) {
        ZC_IF_SOME(fulfiller, maybeFulfiller) { fulfiller->fulfill(); }
        co_return;
      }

      Mask mask(maskKeyGenerator);
      if (!mask.isZero()) { mask.apply(payload); }

      zc::ArrayPtr<const byte> sendParts[2];
      sendParts[0] = sendHeader.compose(true, false, opcode, payload.size(), mask);
      sendParts[1] = payload;
      co_await stream->write(sendParts);
      ZC_IF_SOME(fulfiller, maybeFulfiller) { fulfiller->fulfill(); }
    }
  }

  zc::Promise<void> optimizedPumpTo(WebSocketImpl& other) {
    ZC_IF_SOME(p, other.sendingControlMessage) {
      // We recently sent a control message; make sure it's finished before proceeding.
      auto promise = p.then([this, &other]() { return optimizedPumpTo(other); });
      other.sendingControlMessage = zc::none;
      return promise;
    }

    if (recvData.size() > 0) {
      // We have some data buffered. Write it first.
      return other.stream->write(recvData).then([this, &other, size = recvData.size()]() {
        recvData = nullptr;
        other.sentBytes += size;
        return optimizedPumpTo(other);
      });
    }

    auto cancelPromise = other.stream->whenWriteDisconnected().then([this]() -> zc::Promise<void> {
      this->abort();
      return ZC_EXCEPTION(DISCONNECTED, "destination of WebSocket pump disconnected prematurely");
    });

    // There's no buffered incoming data, so start pumping stream now.
    return stream->pumpTo(*other.stream)
        .then(
            [this, &other](size_t s) -> zc::Promise<void> {
              // WebSocket pumps are expected to include end-of-stream.
              other.disconnected = true;
              other.stream->shutdownWrite();
              receivedBytes += s;
              other.sentBytes += s;
              return zc::READY_NOW;
            },
            [&other](zc::Exception&& e) -> zc::Promise<void> {
              // We don't know if it was a read or a write that threw. If it was a read that threw,
              // we need to send a disconnect on the destination. If it was the destination that
              // threw, it shouldn't hurt to disconnect() it again, but we'll catch and squelch any
              // exceptions.
              other.disconnected = true;
              zc::runCatchingExceptions([&other]() { other.stream->shutdownWrite(); });
              return zc::mv(e);
            })
        .exclusiveJoin(zc::mv(cancelPromise));
  }
};

zc::Own<WebSocket> upgradeToWebSocket(zc::Own<zc::AsyncIoStream> stream,
                                      HttpInputStreamImpl& httpInput, HttpOutputStream& httpOutput,
                                      zc::Maybe<EntropySource&> maskKeyGenerator,
                                      zc::Maybe<CompressionParameters> compressionConfig = zc::none,
                                      zc::Maybe<WebSocketErrorHandler&> errorHandler = zc::none) {
  // Create a WebSocket upgraded from an HTTP stream.
  auto releasedBuffer = httpInput.releaseBuffer();
  return zc::heap<WebSocketImpl>(zc::mv(stream), maskKeyGenerator, zc::mv(compressionConfig),
                                 errorHandler, zc::mv(releasedBuffer.buffer),
                                 releasedBuffer.leftover, httpOutput.flush());
}

}  // namespace

zc::Own<WebSocket> newWebSocket(zc::Own<zc::AsyncIoStream> stream,
                                zc::Maybe<EntropySource&> maskKeyGenerator,
                                zc::Maybe<CompressionParameters> compressionConfig,
                                zc::Maybe<WebSocketErrorHandler&> errorHandler) {
  return zc::heap<WebSocketImpl>(zc::mv(stream), maskKeyGenerator, zc::mv(compressionConfig),
                                 errorHandler);
}

static zc::Promise<void> pumpWebSocketLoop(WebSocket& from, WebSocket& to) {
  try {
    while (true) {
      auto message = co_await from.receive();
      ZC_SWITCH_ONEOF(message) {
        ZC_CASE_ONEOF(text, zc::String) { co_await to.send(text); }
        ZC_CASE_ONEOF(data, zc::Array<byte>) { co_await to.send(data); }
        ZC_CASE_ONEOF(close, WebSocket::Close) {
          // Once a close has passed through, the pump is complete.
          co_await to.close(close.code, close.reason);
          co_return;
        }
      }
      // continue the loop
    }
  } catch (...) {
    // We don't know if it was a read or a write that threw. If it was a read that threw, we need
    // to send a disconnect on the destination. If it was the destination that threw, it
    // shouldn't hurt to disconnect() it again, but we'll catch and squelch any exceptions.
    zc::runCatchingExceptions([&to]() { to.disconnect(); });

    // In any case, this error broke the pump. We should propagate it out as the pump result.
    throw;
  }
}

zc::Promise<void> WebSocket::pumpTo(WebSocket& other) {
  ZC_IF_SOME(p, other.tryPumpFrom(*this)) {
    // Yay, optimized pump!
    return zc::mv(p);
  }
  else {
    // Fall back to default implementation.
    return zc::evalNow([&]() {
      auto cancelPromise = other.whenAborted().then([this]() -> zc::Promise<void> {
        this->abort();
        return ZC_EXCEPTION(DISCONNECTED, "destination of WebSocket pump disconnected prematurely");
      });
      return pumpWebSocketLoop(*this, other).exclusiveJoin(zc::mv(cancelPromise));
    });
  }
}

zc::Maybe<zc::Promise<void>> WebSocket::tryPumpFrom(WebSocket& other) { return zc::none; }

namespace {

class WebSocketPipeImpl final : public WebSocket, public zc::Refcounted {
  // Represents one direction of a WebSocket pipe.
  //
  // This class behaves as a "loopback" WebSocket: a message sent using send() is received using
  // receive(), on the same object. This is *not* how WebSocket implementations usually behave.
  // But, this object is actually used to implement only one direction of a bidirectional pipe. At
  // another layer above this, the pipe is actually composed of two WebSocketPipeEnd instances,
  // which layer on top of two WebSocketPipeImpl instances representing the two directions. So,
  // send() calls on a WebSocketPipeImpl instance always come from one of the two WebSocketPipeEnds
  // while receive() calls come from the other end.

public:
  ~WebSocketPipeImpl() noexcept(false) {
    ZC_REQUIRE(
        state == zc::none || ownState.get() != nullptr,
        "destroying WebSocketPipe with operation still in-progress; probably going to segfault") {
      // Don't std::terminate().
      break;
    }
  }

  void abort() override {
    ZC_IF_SOME(s, state) { s.abort(); }
    else {
      ownState = heap<Aborted>();
      state = *ownState;

      aborted = true;
      ZC_IF_SOME(f, abortedFulfiller) {
        f->fulfill();
        abortedFulfiller = zc::none;
      }
    }
  }

  zc::Promise<void> send(zc::ArrayPtr<const byte> message) override {
    ZC_IF_SOME(s, state) { co_await s.send(message); }
    else { co_await newAdaptedPromise<void, BlockedSend>(*this, MessagePtr(message)); }
    transferredBytes += message.size();
  }

  zc::Promise<void> send(zc::ArrayPtr<const char> message) override {
    ZC_IF_SOME(s, state) { co_await s.send(message); }
    else { co_await newAdaptedPromise<void, BlockedSend>(*this, MessagePtr(message)); }
    transferredBytes += message.size();
  }

  zc::Promise<void> close(uint16_t code, zc::StringPtr reason) override {
    ZC_IF_SOME(s, state) { co_await s.close(code, reason); }
    else {
      co_await newAdaptedPromise<void, BlockedSend>(*this, MessagePtr(ClosePtr{code, reason}));
    }
    transferredBytes += reason.size() + 2;
  }

  void disconnect() override {
    ZC_IF_SOME(s, state) { s.disconnect(); }
    else {
      ownState = heap<Disconnected>();
      state = *ownState;
    }
  }
  zc::Promise<void> whenAborted() override {
    if (aborted) {
      return zc::READY_NOW;
    } else
      ZC_IF_SOME(p, abortedPromise) { return p.addBranch(); }
    else {
      auto paf = newPromiseAndFulfiller<void>();
      abortedFulfiller = zc::mv(paf.fulfiller);
      auto fork = paf.promise.fork();
      auto result = fork.addBranch();
      abortedPromise = zc::mv(fork);
      return result;
    }
  }
  zc::Maybe<zc::Promise<void>> tryPumpFrom(WebSocket& other) override {
    ZC_IF_SOME(s, state) { return s.tryPumpFrom(other); }
    else { return newAdaptedPromise<void, BlockedPumpFrom>(*this, other); }
  }

  zc::Promise<Message> receive(size_t maxSize) override {
    ZC_IF_SOME(s, state) { return s.receive(maxSize); }
    else { return newAdaptedPromise<Message, BlockedReceive>(*this, maxSize); }
  }
  zc::Promise<void> pumpTo(WebSocket& other) override {
    auto onAbort = other.whenAborted().then(
        []() -> zc::Promise<void> { return ZC_EXCEPTION(DISCONNECTED, "WebSocket was aborted"); });

    return pumpToNoAbort(other).exclusiveJoin(zc::mv(onAbort));
  }

  zc::Promise<void> pumpToNoAbort(WebSocket& other) {
    ZC_IF_SOME(s, state) {
      auto before = other.receivedByteCount();
      ZC_DEFER(transferredBytes += other.receivedByteCount() - before);
      co_await s.pumpTo(other);
    }
    else { co_await newAdaptedPromise<void, BlockedPumpTo>(*this, other); }
  }

  uint64_t sentByteCount() override { return transferredBytes; }
  uint64_t receivedByteCount() override { return transferredBytes; }

  zc::Maybe<zc::String> getPreferredExtensions(ExtensionsContext ctx) override { ZC_UNREACHABLE; };

  zc::Maybe<WebSocket&> destinationPumpingTo;
  zc::Maybe<WebSocket&> destinationPumpingFrom;
  // Tracks the outstanding pumpTo() and tryPumpFrom() calls currently running on the
  // WebSocketPipeEnd, which is the destination side of this WebSocketPipeImpl. This is used by
  // the source end to implement getPreferredExtensions().
  //
  // getPreferredExtensions() does not fit into the model used by all the other methods because it
  // is not directional (not a read nor a write call).

private:
  zc::Maybe<WebSocket&> state;
  // Object-oriented state! If any method call is blocked waiting on activity from the other end,
  // then `state` is non-null and method calls should be forwarded to it. If no calls are
  // outstanding, `state` is null.

  zc::Own<WebSocket> ownState;

  uint64_t transferredBytes = 0;

  bool aborted = false;
  Maybe<Own<PromiseFulfiller<void>>> abortedFulfiller = zc::none;
  Maybe<ForkedPromise<void>> abortedPromise = zc::none;

  void endState(WebSocket& obj) {
    ZC_IF_SOME(s, state) {
      if (&s == &obj) { state = zc::none; }
    }
  }

  struct ClosePtr {
    uint16_t code;
    zc::StringPtr reason;
  };
  typedef zc::OneOf<zc::ArrayPtr<const char>, zc::ArrayPtr<const byte>, ClosePtr> MessagePtr;

  class BlockedSend final : public WebSocket {
  public:
    BlockedSend(zc::PromiseFulfiller<void>& fulfiller, WebSocketPipeImpl& pipe, MessagePtr message)
        : fulfiller(fulfiller), pipe(pipe), message(zc::mv(message)) {
      ZC_REQUIRE(pipe.state == zc::none);
      pipe.state = *this;
    }
    ~BlockedSend() noexcept(false) { pipe.endState(*this); }

    void abort() override {
      canceler.cancel("other end of WebSocketPipe was destroyed");
      fulfiller.reject(ZC_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed"));
      pipe.endState(*this);
      pipe.abort();
    }
    zc::Promise<void> whenAborted() override {
      ZC_FAIL_ASSERT("can't get here -- implemented by WebSocketPipeImpl");
    }

    zc::Promise<void> send(zc::ArrayPtr<const byte> message) override {
      ZC_FAIL_ASSERT("another message send is already in progress");
    }
    zc::Promise<void> send(zc::ArrayPtr<const char> message) override {
      ZC_FAIL_ASSERT("another message send is already in progress");
    }
    zc::Promise<void> close(uint16_t code, zc::StringPtr reason) override {
      ZC_FAIL_ASSERT("another message send is already in progress");
    }
    void disconnect() override { ZC_FAIL_ASSERT("another message send is already in progress"); }
    zc::Maybe<zc::Promise<void>> tryPumpFrom(WebSocket& other) override {
      ZC_FAIL_ASSERT("another message send is already in progress");
    }

    zc::Promise<Message> receive(size_t maxSize) override {
      ZC_REQUIRE(canceler.isEmpty(), "already pumping");
      fulfiller.fulfill();
      pipe.endState(*this);
      ZC_SWITCH_ONEOF(message) {
        ZC_CASE_ONEOF(arr, zc::ArrayPtr<const char>) { return Message(zc::str(arr)); }
        ZC_CASE_ONEOF(arr, zc::ArrayPtr<const byte>) {
          auto copy = zc::heapArray<byte>(arr.size());
          memcpy(copy.begin(), arr.begin(), arr.size());
          return Message(zc::mv(copy));
        }
        ZC_CASE_ONEOF(close, ClosePtr) { return Message(Close{close.code, zc::str(close.reason)}); }
      }
      ZC_UNREACHABLE;
    }
    zc::Promise<void> pumpTo(WebSocket& other) override {
      ZC_REQUIRE(canceler.isEmpty(), "already pumping");
      zc::Promise<void> promise = nullptr;
      ZC_SWITCH_ONEOF(message) {
        ZC_CASE_ONEOF(arr, zc::ArrayPtr<const char>) { promise = other.send(arr); }
        ZC_CASE_ONEOF(arr, zc::ArrayPtr<const byte>) { promise = other.send(arr); }
        ZC_CASE_ONEOF(close, ClosePtr) { promise = other.close(close.code, close.reason); }
      }
      return canceler.wrap(promise.then(
          [this, &other]() {
            canceler.release();
            fulfiller.fulfill();
            pipe.endState(*this);
            return pipe.pumpTo(other);
          },
          [this](zc::Exception&& e) -> zc::Promise<void> {
            canceler.release();
            fulfiller.reject(zc::cp(e));
            pipe.endState(*this);
            return zc::mv(e);
          }));
    }

    uint64_t sentByteCount() override {
      ZC_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }
    uint64_t receivedByteCount() override {
      ZC_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }

    zc::Maybe<zc::String> getPreferredExtensions(ExtensionsContext ctx) override {
      ZC_UNREACHABLE;
    };

  private:
    zc::PromiseFulfiller<void>& fulfiller;
    WebSocketPipeImpl& pipe;
    MessagePtr message;
    Canceler canceler;
  };

  class BlockedPumpFrom final : public WebSocket {
  public:
    BlockedPumpFrom(zc::PromiseFulfiller<void>& fulfiller, WebSocketPipeImpl& pipe,
                    WebSocket& input)
        : fulfiller(fulfiller), pipe(pipe), input(input) {
      ZC_REQUIRE(pipe.state == zc::none);
      pipe.state = *this;
    }
    ~BlockedPumpFrom() noexcept(false) { pipe.endState(*this); }

    void abort() override {
      canceler.cancel("other end of WebSocketPipe was destroyed");
      fulfiller.reject(ZC_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed"));
      pipe.endState(*this);
      pipe.abort();
    }
    zc::Promise<void> whenAborted() override {
      ZC_FAIL_ASSERT("can't get here -- implemented by WebSocketPipeImpl");
    }

    zc::Promise<void> send(zc::ArrayPtr<const byte> message) override {
      ZC_FAIL_ASSERT("another message send is already in progress");
    }
    zc::Promise<void> send(zc::ArrayPtr<const char> message) override {
      ZC_FAIL_ASSERT("another message send is already in progress");
    }
    zc::Promise<void> close(uint16_t code, zc::StringPtr reason) override {
      ZC_FAIL_ASSERT("another message send is already in progress");
    }
    void disconnect() override { ZC_FAIL_ASSERT("another message send is already in progress"); }
    zc::Maybe<zc::Promise<void>> tryPumpFrom(WebSocket& other) override {
      ZC_FAIL_ASSERT("another message send is already in progress");
    }

    zc::Promise<Message> receive(size_t maxSize) override {
      ZC_REQUIRE(canceler.isEmpty(), "another message receive is already in progress");
      return canceler.wrap(input.receive(maxSize).then(
          [this](Message message) {
            if (message.is<Close>()) {
              canceler.release();
              fulfiller.fulfill();
              pipe.endState(*this);
            }
            return zc::mv(message);
          },
          [this](zc::Exception&& e) -> Message {
            canceler.release();
            fulfiller.reject(zc::cp(e));
            pipe.endState(*this);
            zc::throwRecoverableException(zc::mv(e));
            return Message(zc::String());
          }));
    }
    zc::Promise<void> pumpTo(WebSocket& other) override {
      ZC_REQUIRE(canceler.isEmpty(), "another message receive is already in progress");
      return canceler.wrap(input.pumpTo(other).then(
          [this]() {
            canceler.release();
            fulfiller.fulfill();
            pipe.endState(*this);
          },
          [this](zc::Exception&& e) {
            canceler.release();
            fulfiller.reject(zc::cp(e));
            pipe.endState(*this);
            zc::throwRecoverableException(zc::mv(e));
          }));
    }

    uint64_t sentByteCount() override {
      ZC_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }
    uint64_t receivedByteCount() override {
      ZC_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }

    zc::Maybe<zc::String> getPreferredExtensions(ExtensionsContext ctx) override {
      ZC_UNREACHABLE;
    };

  private:
    zc::PromiseFulfiller<void>& fulfiller;
    WebSocketPipeImpl& pipe;
    WebSocket& input;
    Canceler canceler;
  };

  class BlockedReceive final : public WebSocket {
  public:
    BlockedReceive(zc::PromiseFulfiller<Message>& fulfiller, WebSocketPipeImpl& pipe,
                   size_t maxSize)
        : fulfiller(fulfiller), pipe(pipe), maxSize(maxSize) {
      ZC_REQUIRE(pipe.state == zc::none);
      pipe.state = *this;
    }
    ~BlockedReceive() noexcept(false) { pipe.endState(*this); }

    void abort() override {
      canceler.cancel("other end of WebSocketPipe was destroyed");
      fulfiller.reject(ZC_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed"));
      pipe.endState(*this);
      pipe.abort();
    }
    zc::Promise<void> whenAborted() override {
      ZC_FAIL_ASSERT("can't get here -- implemented by WebSocketPipeImpl");
    }

    zc::Promise<void> send(zc::ArrayPtr<const byte> message) override {
      ZC_REQUIRE(canceler.isEmpty(), "already pumping");
      auto copy = zc::heapArray<byte>(message.size());
      memcpy(copy.begin(), message.begin(), message.size());
      fulfiller.fulfill(Message(zc::mv(copy)));
      pipe.endState(*this);
      return zc::READY_NOW;
    }
    zc::Promise<void> send(zc::ArrayPtr<const char> message) override {
      ZC_REQUIRE(canceler.isEmpty(), "already pumping");
      fulfiller.fulfill(Message(zc::str(message)));
      pipe.endState(*this);
      return zc::READY_NOW;
    }
    zc::Promise<void> close(uint16_t code, zc::StringPtr reason) override {
      ZC_REQUIRE(canceler.isEmpty(), "already pumping");
      fulfiller.fulfill(Message(Close{code, zc::str(reason)}));
      pipe.endState(*this);
      return zc::READY_NOW;
    }
    void disconnect() override {
      ZC_REQUIRE(canceler.isEmpty(), "already pumping");
      fulfiller.reject(ZC_EXCEPTION(DISCONNECTED, "WebSocket disconnected"));
      pipe.endState(*this);
      pipe.disconnect();
    }
    zc::Maybe<zc::Promise<void>> tryPumpFrom(WebSocket& other) override {
      ZC_REQUIRE(canceler.isEmpty(), "already pumping");
      return canceler.wrap(other.receive(maxSize).then(
          [this, &other](Message message) {
            canceler.release();
            fulfiller.fulfill(zc::mv(message));
            pipe.endState(*this);
            return other.pumpTo(pipe);
          },
          [this](zc::Exception&& e) -> zc::Promise<void> {
            canceler.release();
            fulfiller.reject(zc::cp(e));
            pipe.endState(*this);
            return zc::mv(e);
          }));
    }

    zc::Promise<Message> receive(size_t maxSize) override {
      ZC_FAIL_ASSERT("another message receive is already in progress");
    }
    zc::Promise<void> pumpTo(WebSocket& other) override {
      ZC_FAIL_ASSERT("another message receive is already in progress");
    }

    uint64_t sentByteCount() override {
      ZC_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }
    uint64_t receivedByteCount() override {
      ZC_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }

    zc::Maybe<zc::String> getPreferredExtensions(ExtensionsContext ctx) override {
      ZC_UNREACHABLE;
    };

  private:
    zc::PromiseFulfiller<Message>& fulfiller;
    WebSocketPipeImpl& pipe;
    size_t maxSize;
    Canceler canceler;
  };

  class BlockedPumpTo final : public WebSocket {
  public:
    BlockedPumpTo(zc::PromiseFulfiller<void>& fulfiller, WebSocketPipeImpl& pipe, WebSocket& output)
        : fulfiller(fulfiller), pipe(pipe), output(output) {
      ZC_REQUIRE(pipe.state == zc::none);
      pipe.state = *this;
    }
    ~BlockedPumpTo() noexcept(false) { pipe.endState(*this); }

    void abort() override {
      canceler.cancel("other end of WebSocketPipe was destroyed");

      // abort() is called when the pipe end is dropped. This should be treated as disconnecting,
      // so pumpTo() should complete normally.
      fulfiller.fulfill();

      pipe.endState(*this);
      pipe.abort();
    }
    zc::Promise<void> whenAborted() override {
      ZC_FAIL_ASSERT("can't get here -- implemented by WebSocketPipeImpl");
    }

    zc::Promise<void> send(zc::ArrayPtr<const byte> message) override {
      ZC_REQUIRE(canceler.isEmpty(), "another message send is already in progress");
      return canceler.wrap(output.send(message));
    }
    zc::Promise<void> send(zc::ArrayPtr<const char> message) override {
      ZC_REQUIRE(canceler.isEmpty(), "another message send is already in progress");
      return canceler.wrap(output.send(message));
    }
    zc::Promise<void> close(uint16_t code, zc::StringPtr reason) override {
      ZC_REQUIRE(canceler.isEmpty(), "another message send is already in progress");
      return canceler.wrap(output.close(code, reason)
                               .then(
                                   [this]() {
                                     // A pump is expected to end upon seeing a Close message.
                                     canceler.release();
                                     pipe.endState(*this);
                                     fulfiller.fulfill();
                                   },
                                   [this](zc::Exception&& e) {
                                     canceler.release();
                                     pipe.endState(*this);
                                     fulfiller.reject(zc::cp(e));
                                     zc::throwRecoverableException(zc::mv(e));
                                   }));
    }
    void disconnect() override {
      ZC_REQUIRE(canceler.isEmpty(), "another message send is already in progress");

      output.disconnect();
      pipe.endState(*this);
      fulfiller.reject(ZC_EXCEPTION(DISCONNECTED, "WebSocket::disconnect() ended the pump"));
      pipe.disconnect();
    }
    zc::Maybe<zc::Promise<void>> tryPumpFrom(WebSocket& other) override {
      ZC_REQUIRE(canceler.isEmpty(), "another message send is already in progress");
      return canceler.wrap(other.pumpTo(output).then(
          [this]() {
            canceler.release();
            pipe.endState(*this);
            fulfiller.fulfill();
          },
          [this](zc::Exception&& e) {
            canceler.release();
            pipe.endState(*this);
            fulfiller.reject(zc::cp(e));
            zc::throwRecoverableException(zc::mv(e));
          }));
    }

    zc::Promise<Message> receive(size_t maxSize) override {
      ZC_FAIL_ASSERT("another message receive is already in progress");
    }
    zc::Promise<void> pumpTo(WebSocket& other) override {
      ZC_FAIL_ASSERT("another message receive is already in progress");
    }

    uint64_t sentByteCount() override {
      ZC_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }
    uint64_t receivedByteCount() override {
      ZC_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }

    zc::Maybe<zc::String> getPreferredExtensions(ExtensionsContext ctx) override {
      ZC_UNREACHABLE;
    };

  private:
    zc::PromiseFulfiller<void>& fulfiller;
    WebSocketPipeImpl& pipe;
    WebSocket& output;
    Canceler canceler;
  };

  class Disconnected final : public WebSocket {
  public:
    void abort() override {
      // can ignore
    }
    zc::Promise<void> whenAborted() override {
      ZC_FAIL_ASSERT("can't get here -- implemented by WebSocketPipeImpl");
    }

    zc::Promise<void> send(zc::ArrayPtr<const byte> message) override {
      ZC_FAIL_REQUIRE("can't send() after disconnect()");
    }
    zc::Promise<void> send(zc::ArrayPtr<const char> message) override {
      ZC_FAIL_REQUIRE("can't send() after disconnect()");
    }
    zc::Promise<void> close(uint16_t code, zc::StringPtr reason) override {
      ZC_FAIL_REQUIRE("can't close() after disconnect()");
    }
    void disconnect() override {
      // redundant; ignore
    }
    zc::Maybe<zc::Promise<void>> tryPumpFrom(WebSocket& other) override {
      ZC_FAIL_REQUIRE("can't tryPumpFrom() after disconnect()");
    }

    zc::Promise<Message> receive(size_t maxSize) override {
      return ZC_EXCEPTION(DISCONNECTED, "WebSocket disconnected");
    }
    zc::Promise<void> pumpTo(WebSocket& other) override { return zc::READY_NOW; }

    uint64_t sentByteCount() override {
      ZC_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }
    uint64_t receivedByteCount() override {
      ZC_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }

    zc::Maybe<zc::String> getPreferredExtensions(ExtensionsContext ctx) override {
      ZC_UNREACHABLE;
    };
  };

  class Aborted final : public WebSocket {
  public:
    void abort() override {
      // can ignore
    }
    zc::Promise<void> whenAborted() override {
      ZC_FAIL_ASSERT("can't get here -- implemented by WebSocketPipeImpl");
    }

    zc::Promise<void> send(zc::ArrayPtr<const byte> message) override {
      return ZC_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed");
    }
    zc::Promise<void> send(zc::ArrayPtr<const char> message) override {
      return ZC_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed");
    }
    zc::Promise<void> close(uint16_t code, zc::StringPtr reason) override {
      return ZC_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed");
    }
    void disconnect() override {
      // redundant; ignore
    }
    zc::Maybe<zc::Promise<void>> tryPumpFrom(WebSocket& other) override {
      return zc::Promise<void>(
          ZC_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed"));
    }

    zc::Promise<Message> receive(size_t maxSize) override {
      return ZC_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed");
    }
    zc::Promise<void> pumpTo(WebSocket& other) override {
      return ZC_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed");
    }

    uint64_t sentByteCount() override {
      ZC_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }
    uint64_t receivedByteCount() override {
      ZC_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }
    zc::Maybe<zc::String> getPreferredExtensions(ExtensionsContext ctx) override {
      ZC_UNREACHABLE;
    };
  };
};

class WebSocketPipeEnd final : public WebSocket {
public:
  WebSocketPipeEnd(zc::Rc<WebSocketPipeImpl>&& in, zc::Rc<WebSocketPipeImpl>&& out)
      : in(zc::mv(in)), out(zc::mv(out)) {}
  ~WebSocketPipeEnd() noexcept(false) {
    in->abort();
    out->abort();
  }

  zc::Promise<void> send(zc::ArrayPtr<const byte> message) override { return out->send(message); }
  zc::Promise<void> send(zc::ArrayPtr<const char> message) override { return out->send(message); }
  zc::Promise<void> close(uint16_t code, zc::StringPtr reason) override {
    return out->close(code, reason);
  }
  void disconnect() override { out->disconnect(); }
  void abort() override {
    in->abort();
    out->abort();
  }
  zc::Promise<void> whenAborted() override { return out->whenAborted(); }
  zc::Maybe<zc::Promise<void>> tryPumpFrom(WebSocket& other) override {
    ZC_REQUIRE(in->destinationPumpingFrom == zc::none,
               "can only call tryPumpFrom() once at a time");
    // By convention, we store the WebSocket reference on `in`.
    in->destinationPumpingFrom = other;
    auto deferredUnregister = zc::defer([this]() { in->destinationPumpingFrom = zc::none; });
    ZC_IF_SOME(p, out->tryPumpFrom(other)) { return p.attach(zc::mv(deferredUnregister)); }
    else { return zc::none; }
  }

  zc::Promise<Message> receive(size_t maxSize) override { return in->receive(maxSize); }
  zc::Promise<void> pumpTo(WebSocket& other) override {
    ZC_REQUIRE(in->destinationPumpingTo == zc::none, "can only call pumpTo() once at a time");
    // By convention, we store the WebSocket reference on `in`.
    in->destinationPumpingTo = other;
    auto deferredUnregister = zc::defer([this]() { in->destinationPumpingTo = zc::none; });
    return in->pumpTo(other).attach(zc::mv(deferredUnregister));
  }

  uint64_t sentByteCount() override { return out->sentByteCount(); }
  uint64_t receivedByteCount() override { return in->sentByteCount(); }

  zc::Maybe<zc::String> getPreferredExtensions(ExtensionsContext ctx) override {
    // We want to forward this call to whatever WebSocket the other end of the pipe is pumping
    // to/from, if any. We'll check them in an arbitrary order and take the first one we see.
    // But really, the hope is that both destinationPumpingTo and destinationPumpingFrom are in fact
    // the same object. If they aren't the same, then it's not really clear whose extensions we
    // should prefer; the choice here is arbitrary.
    ZC_IF_SOME(ws, out->destinationPumpingTo) {
      ZC_IF_SOME(result, ws.getPreferredExtensions(ctx)) { return zc::mv(result); }
    }
    ZC_IF_SOME(ws, out->destinationPumpingFrom) {
      ZC_IF_SOME(result, ws.getPreferredExtensions(ctx)) { return zc::mv(result); }
    }
    return zc::none;
  };

private:
  zc::Rc<WebSocketPipeImpl> in;
  zc::Rc<WebSocketPipeImpl> out;
};

}  // namespace

WebSocketPipe newWebSocketPipe() {
  auto pipe1 = zc::rc<WebSocketPipeImpl>();
  auto pipe2 = zc::rc<WebSocketPipeImpl>();

  auto end1 = zc::heap<WebSocketPipeEnd>(pipe1.addRef(), pipe2.addRef());
  auto end2 = zc::heap<WebSocketPipeEnd>(zc::mv(pipe2), zc::mv(pipe1));

  return {{zc::mv(end1), zc::mv(end2)}};
}

// =======================================================================================
class AsyncIoStreamWithInitialBuffer final : public zc::AsyncIoStream {
  // An AsyncIoStream implementation that accepts an initial buffer of data
  // to be read out first, and is optionally capable of deferring writes
  // until a given waitBeforeSend promise is fulfilled.
  //
  // Instances are created with a leftoverBackingBuffer (a zc::Array<byte>)
  // and a leftover zc::ArrayPtr<byte> that provides a view into the backing
  // buffer representing the queued data that is pending to be read. Calling
  // tryRead will consume the data from the leftover first. Once leftover has
  // been fully consumed, reads will defer to the underlying stream.
public:
  AsyncIoStreamWithInitialBuffer(zc::Own<zc::AsyncIoStream> stream,
                                 zc::Array<byte> leftoverBackingBuffer, zc::ArrayPtr<byte> leftover)
      : stream(zc::mv(stream)),
        leftoverBackingBuffer(zc::mv(leftoverBackingBuffer)),
        leftover(leftover) {}

  void shutdownWrite() override { stream->shutdownWrite(); }

  // AsyncInputStream
  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    ZC_REQUIRE(maxBytes >= minBytes);
    auto destination = static_cast<byte*>(buffer);

    // If there are at least minBytes available in the leftover buffer...
    if (leftover.size() >= minBytes) {
      // We are going to immediately read up to maxBytes from the leftover buffer...
      auto bytesToCopy = zc::min(maxBytes, leftover.size());
      memcpy(destination, leftover.begin(), bytesToCopy);
      leftover = leftover.slice(bytesToCopy, leftover.size());

      // If we've consumed all of the data in the leftover buffer, go ahead and free it.
      if (leftover.size() == 0) { leftoverBackingBuffer = nullptr; }

      return bytesToCopy;
    } else {
      // We know here that leftover.size() is less than minBytes, but it might not
      // be zero. Copy everything from leftover into the destination buffer then read
      // the rest from the underlying stream.
      auto bytesToCopy = leftover.size();
      ZC_DASSERT(bytesToCopy < minBytes);

      if (bytesToCopy > 0) {
        memcpy(destination, leftover.begin(), bytesToCopy);
        leftover = nullptr;
        leftoverBackingBuffer = nullptr;
        minBytes -= bytesToCopy;
        maxBytes -= bytesToCopy;
        ZC_DASSERT(minBytes >= 1);
        ZC_DASSERT(maxBytes >= minBytes);
      }

      return stream->tryRead(destination + bytesToCopy, minBytes, maxBytes)
          .then([bytesToCopy](size_t amount) { return amount + bytesToCopy; });
    }
  }

  Maybe<uint64_t> tryGetLength() override {
    // For a CONNECT pipe, we have no idea how much data there is going to be.
    return zc::none;
  }

  zc::Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount = zc::maxValue) override {
    return pumpLoop(output, amount, 0);
  }

  zc::Maybe<zc::Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input,
                                               uint64_t amount = zc::maxValue) override {
    return input.pumpTo(*stream, amount);
  }

  // AsyncOutputStream
  Promise<void> write(ArrayPtr<const byte> buffer) override { return stream->write(buffer); }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return stream->write(pieces);
  }

  Promise<void> whenWriteDisconnected() override { return stream->whenWriteDisconnected(); }

private:
  zc::Promise<uint64_t> pumpLoop(zc::AsyncOutputStream& output, uint64_t remaining,
                                 uint64_t total) {
    // If there is any data remaining in the leftover queue, we'll write it out first to output.
    if (leftover.size() > 0) {
      auto bytesToWrite = zc::min(leftover.size(), remaining);
      return output.write(leftover.first(bytesToWrite))
          .then([this, &output, remaining, total, bytesToWrite]() mutable -> zc::Promise<uint64_t> {
            leftover = leftover.slice(bytesToWrite, leftover.size());
            // If the leftover buffer has been fully consumed, go ahead and free it now.
            if (leftover.size() == 0) { leftoverBackingBuffer = nullptr; }
            remaining -= bytesToWrite;
            total += bytesToWrite;

            if (remaining == 0) { return total; }
            return pumpLoop(output, remaining, total);
          });
    } else {
      // Otherwise, we are just going to defer to stream's pumpTo, making sure to
      // account for the total amount we've already written from the leftover queue.
      return stream->pumpTo(output, remaining).then([total](auto read) { return total + read; });
    }
  };

  zc::Own<zc::AsyncIoStream> stream;
  zc::Array<byte> leftoverBackingBuffer;
  zc::ArrayPtr<byte> leftover;
};

class AsyncIoStreamWithGuards final : public zc::AsyncIoStream, private zc::TaskSet::ErrorHandler {
  // This AsyncIoStream adds separate zc::Promise guards to both the input and output,
  // delaying reads and writes until each relevant guard is resolved.
  //
  // When the read guard promise resolves, it may provide a released buffer that will
  // be read out first.
  // The primary use case for this impl is to support pipelined CONNECT calls which
  // optimistically allow outbound writes to happen while establishing the CONNECT
  // tunnel has not yet been completed. If the guard promise rejects, the stream
  // is permanently errored and existing pending calls (reads and writes) are canceled.
public:
  AsyncIoStreamWithGuards(zc::Own<zc::AsyncIoStream> inner,
                          zc::Promise<zc::Maybe<HttpInputStreamImpl::ReleasedBuffer>> readGuard,
                          zc::Promise<void> writeGuard)
      : inner(zc::mv(inner)),
        readGuard(handleReadGuard(zc::mv(readGuard))),
        writeGuard(handleWriteGuard(zc::mv(writeGuard))),
        tasks(*this) {}

  // AsyncInputStream
  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    if (readGuardReleased) { return inner->tryRead(buffer, minBytes, maxBytes); }
    return readGuard.addBranch().then(
        [this, buffer, minBytes, maxBytes] { return inner->tryRead(buffer, minBytes, maxBytes); });
  }

  Maybe<uint64_t> tryGetLength() override { return zc::none; }

  zc::Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount = zc::maxValue) override {
    if (readGuardReleased) { return inner->pumpTo(output, amount); }
    return readGuard.addBranch().then(
        [this, &output, amount] { return inner->pumpTo(output, amount); });
  }

  // AsyncOutputStream

  void shutdownWrite() override {
    if (writeGuardReleased) {
      inner->shutdownWrite();
    } else {
      tasks.add(writeGuard.addBranch().then([this]() { inner->shutdownWrite(); }));
    }
  }

  zc::Maybe<zc::Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input,
                                               uint64_t amount = zc::maxValue) override {
    if (writeGuardReleased) {
      return input.pumpTo(*inner, amount);
    } else {
      return writeGuard.addBranch().then(
          [this, &input, amount]() { return input.pumpTo(*inner, amount); });
    }
  }

  Promise<void> write(ArrayPtr<const byte> buffer) override {
    if (writeGuardReleased) {
      return inner->write(buffer);
    } else {
      return writeGuard.addBranch().then([this, buffer]() { return inner->write(buffer); });
    }
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    if (writeGuardReleased) {
      return inner->write(pieces);
    } else {
      return writeGuard.addBranch().then([this, pieces]() { return inner->write(pieces); });
    }
  }

  Promise<void> whenWriteDisconnected() override {
    if (writeGuardReleased) {
      return inner->whenWriteDisconnected();
    } else {
      return writeGuard.addBranch().then([this]() { return inner->whenWriteDisconnected(); },
                                         [](zc::Exception&& e) mutable -> zc::Promise<void> {
                                           if (e.getType() == zc::Exception::Type::DISCONNECTED) {
                                             return zc::READY_NOW;
                                           } else {
                                             return zc::mv(e);
                                           }
                                         });
    }
  }

private:
  zc::Own<zc::AsyncIoStream> inner;
  zc::ForkedPromise<void> readGuard;
  zc::ForkedPromise<void> writeGuard;
  bool readGuardReleased = false;
  bool writeGuardReleased = false;
  zc::TaskSet tasks;
  // Set of tasks used to call `shutdownWrite` after write guard is released.

  void taskFailed(zc::Exception&& exception) override {
    // This `taskFailed` callback is only used when `shutdownWrite` is being called. Because we
    // don't care about DISCONNECTED exceptions when `shutdownWrite` is called we ignore this
    // class of exceptions here.
    if (exception.getType() != zc::Exception::Type::DISCONNECTED) { ZC_LOG(ERROR, exception); }
  }

  zc::ForkedPromise<void> handleWriteGuard(zc::Promise<void> guard) {
    return guard.then([this]() { writeGuardReleased = true; }).fork();
  }

  zc::ForkedPromise<void> handleReadGuard(
      zc::Promise<zc::Maybe<HttpInputStreamImpl::ReleasedBuffer>> guard) {
    return guard
        .then([this](zc::Maybe<HttpInputStreamImpl::ReleasedBuffer> buffer) mutable {
          readGuardReleased = true;
          ZC_IF_SOME(b, buffer) {
            if (b.leftover.size() > 0) {
              // We only need to replace the inner stream if a non-empty buffer is provided.
              inner =
                  heap<AsyncIoStreamWithInitialBuffer>(zc::mv(inner), zc::mv(b.buffer), b.leftover);
            }
          }
        })
        .fork();
  }
};

// =======================================================================================

namespace _ {  // private implementation details

zc::ArrayPtr<const char> splitNext(zc::ArrayPtr<const char>& cursor, char delimiter) {
  // Consumes and returns the next item in a delimited list.
  //
  // If a delimiter is found:
  //  - `cursor` is updated to point to the rest of the string after the delimiter.
  //  - The text before the delimiter is returned.
  // If no delimiter is found:
  //  - `cursor` is updated to an empty string.
  //  - The text that had been in `cursor` is returned.
  //
  // (It's up to the caller to stop the loop once `cursor` is empty.)
  ZC_IF_SOME(index, cursor.findFirst(delimiter)) {
    auto part = cursor.first(index);
    cursor = cursor.slice(index + 1, cursor.size());
    return part;
  }
  zc::ArrayPtr<const char> result(zc::mv(cursor));
  cursor = nullptr;

  return result;
}

void stripLeadingAndTrailingSpace(ArrayPtr<const char>& str) {
  // Remove any leading/trailing spaces from `str`, modifying it in-place.
  while (str.size() > 0 && (str[0] == ' ' || str[0] == '\t')) { str = str.slice(1, str.size()); }
  while (str.size() > 0 && (str.back() == ' ' || str.back() == '\t')) {
    str = str.first(str.size() - 1);
  }
}

zc::Vector<zc::ArrayPtr<const char>> splitParts(zc::ArrayPtr<const char> input, char delim) {
  // Given a string `input` and a delimiter `delim`, split the string into a vector of substrings,
  // separated by the delimiter. Note that leading/trailing whitespace is stripped from each
  // element.
  zc::Vector<zc::ArrayPtr<const char>> parts;

  while (input.size() != 0) {
    auto part = splitNext(input, delim);
    stripLeadingAndTrailingSpace(part);
    parts.add(zc::mv(part));
  }

  return parts;
}

zc::Array<KeyMaybeVal> toKeysAndVals(const zc::ArrayPtr<zc::ArrayPtr<const char>>& params) {
  // Given a collection of parameters (a single offer), parse the parameters into <key, MaybeValue>
  // pairs. If the parameter contains an `=`, we set the `key` to everything before, and the `value`
  // to everything after. Otherwise, we set the `key` to be the entire parameter.
  // Either way, both the key and value (if it exists) are stripped of leading & trailing
  // whitespace.
  auto result = zc::heapArray<KeyMaybeVal>(params.size());
  size_t count = 0;
  for (const auto& param : params) {
    zc::ArrayPtr<const char> key;
    zc::Maybe<zc::ArrayPtr<const char>> value;

    ZC_IF_SOME(index, param.findFirst('=')) {
      // Found '=' so we have a value.
      key = param.first(index);
      stripLeadingAndTrailingSpace(key);
      value = param.slice(index + 1, param.size());
      ZC_IF_SOME(v, value) { stripLeadingAndTrailingSpace(v); }
    }
    else { key = zc::mv(param); }

    result[count].key = zc::mv(key);
    result[count].val = zc::mv(value);
    ++count;
  }
  return zc::mv(result);
}

struct ParamType {
  enum { CLIENT, SERVER } side;
  enum { NO_CONTEXT_TAKEOVER, MAX_WINDOW_BITS } property;
};

inline zc::Maybe<ParamType> parseKeyName(zc::ArrayPtr<const char>& key) {
  // Returns a `ParamType` struct if the `key` is valid and zc::none if invalid.

  if (key == "client_no_context_takeover"_zc) {
    return ParamType{ParamType::CLIENT, ParamType::NO_CONTEXT_TAKEOVER};
  } else if (key == "server_no_context_takeover"_zc) {
    return ParamType{ParamType::SERVER, ParamType::NO_CONTEXT_TAKEOVER};
  } else if (key == "client_max_window_bits"_zc) {
    return ParamType{ParamType::CLIENT, ParamType::MAX_WINDOW_BITS};
  } else if (key == "server_max_window_bits"_zc) {
    return ParamType{ParamType::SERVER, ParamType::MAX_WINDOW_BITS};
  }
  return zc::none;
}

zc::Maybe<UnverifiedConfig> populateUnverifiedConfig(zc::Array<KeyMaybeVal>& params) {
  // Given a collection of <key, MaybeValue> pairs, attempt to populate an `UnverifiedConfig`
  // struct. If the struct cannot be populated, we return null.
  //
  // This function populates the struct with what it finds, it does not perform bounds checking or
  // concern itself with valid `Value`s (so long as the `Value` is non-empty).
  //
  // The following issues would prevent a struct from being populated:
  //  Key issues:
  //    - `Key` is invalid (see `parseKeyName()`).
  //    - `Key` is repeated.
  //  Value issues:
  //    - Got a `Value` when none was expected (only the `max_window_bits` parameters expect
  //    values).
  //    - Got an empty `Value` (0 characters, or all whitespace characters).

  if (params.size() > 4) {
    // We expect 4 `Key`s at most, having more implies repeats/invalid keys are present.
    return zc::none;
  }

  UnverifiedConfig config;

  for (auto& param : params) {
    ZC_IF_SOME(paramType, parseKeyName(param.key)) {
      // `Key` is valid, but we still want to check for repeats.
      const auto& side = paramType.side;
      const auto& property = paramType.property;

      if (property == ParamType::NO_CONTEXT_TAKEOVER) {
        auto& takeOverSetting = (side == ParamType::CLIENT) ? config.clientNoContextTakeover
                                                            : config.serverNoContextTakeover;

        if (takeOverSetting == true) {
          // This `Key` is a repeat; invalid config.
          return zc::none;
        }

        if (param.val != zc::none) {
          // The `x_no_context_takeover` parameter shouldn't have a value; invalid config.
          return zc::none;
        }

        takeOverSetting = true;
      } else if (property == ParamType::MAX_WINDOW_BITS) {
        auto& maxBitsSetting =
            (side == ParamType::CLIENT) ? config.clientMaxWindowBits : config.serverMaxWindowBits;

        if (maxBitsSetting != zc::none) {
          // This `Key` is a repeat; invalid config.
          return zc::none;
        }

        ZC_IF_SOME(value, param.val) {
          if (value.size() == 0) {
            // This is equivalent to `x_max_window_bits=`, since we got an "=" we expected a token
            // to follow.
            return zc::none;
          }
          maxBitsSetting = param.val;
        }
        else {
          // We know we got this `max_window_bits` parameter in a Request/Response, and we also know
          // that it didn't include an "=" (otherwise the value wouldn't be null).
          // It's important to retain the information that the parameter was received *without* a
          // corresponding value, as this may determine whether the offer is valid or not.
          //
          // To retain this information, we'll set `maxBitsSetting` to be an empty ArrayPtr so this
          // can be dealt with properly later.
          maxBitsSetting = ArrayPtr<const char>();
        }
      }
    }
    else {
      // Invalid parameter.
      return zc::none;
    }
  }
  return zc::mv(config);
}

zc::Maybe<CompressionParameters> validateCompressionConfig(UnverifiedConfig&& config,
                                                           bool isAgreement) {
  // Verifies that the `config` is valid depending on whether we're validating a Request (offer) or
  // a Response (agreement). This essentially consumes the `UnverifiedConfig` and converts it into a
  // `CompressionParameters` struct.
  CompressionParameters result;

  ZC_IF_SOME(serverBits, config.serverMaxWindowBits) {
    if (serverBits.size() == 0) {
      // This means `server_max_window_bits` was passed without a value. Since a value is required,
      // this config is invalid.
      return zc::none;
    } else {
      ZC_IF_SOME(bits, zc::str(serverBits).tryParseAs<size_t>()) {
        if (bits < 8 || 15 < bits) {
          // Out of range -- invalid.
          return zc::none;
        }
        if (isAgreement) {
          result.inboundMaxWindowBits = bits;
        } else {
          result.outboundMaxWindowBits = bits;
        }
      }
      else {
        // Invalid ABNF, expected 1*DIGIT.
        return zc::none;
      }
    }
  }

  ZC_IF_SOME(clientBits, config.clientMaxWindowBits) {
    if (clientBits.size() == 0) {
      if (!isAgreement) {
        // `client_max_window_bits` does not need to have a value in an offer, let's set it to 15
        // to get the best level of compression.
        result.inboundMaxWindowBits = 15;
      } else {
        // `client_max_window_bits` must have a value in a Response.
        return zc::none;
      }
    } else {
      ZC_IF_SOME(bits, zc::str(clientBits).tryParseAs<size_t>()) {
        if (bits < 8 || 15 < bits) {
          // Out of range -- invalid.
          return zc::none;
        }
        if (isAgreement) {
          result.outboundMaxWindowBits = bits;
        } else {
          result.inboundMaxWindowBits = bits;
        }
      }
      else {
        // Invalid ABNF, expected 1*DIGIT.
        return zc::none;
      }
    }
  }

  if (isAgreement) {
    result.outboundNoContextTakeover = config.clientNoContextTakeover;
    result.inboundNoContextTakeover = config.serverNoContextTakeover;
  } else {
    result.inboundNoContextTakeover = config.clientNoContextTakeover;
    result.outboundNoContextTakeover = config.serverNoContextTakeover;
  }
  return zc::mv(result);
}

inline zc::Maybe<CompressionParameters> tryExtractParameters(
    zc::Vector<zc::ArrayPtr<const char>>& configuration, bool isAgreement) {
  // If the `configuration` is structured correctly and has no invalid parameters/values, we will
  // return a populated `CompressionParameters` struct.
  if (configuration.size() == 1) {
    // Plain `permessage-deflate`.
    return CompressionParameters{};
  }
  auto params = configuration.slice(1, configuration.size());
  auto keyMaybeValuePairs = toKeysAndVals(params);
  // Parse parameter strings into parameter[=value] pairs.
  auto maybeUnverified = populateUnverifiedConfig(keyMaybeValuePairs);
  ZC_IF_SOME(unverified, maybeUnverified) {
    // Parsing succeeded, i.e. the parameter (`key`) names are valid and we don't have
    // values for `x_no_context_takeover` parameters (the configuration is structured correctly).
    // All that's left is to check the `x_max_window_bits` values (if any are present).
    ZC_IF_SOME(validConfig, validateCompressionConfig(zc::mv(unverified), isAgreement)) {
      return zc::mv(validConfig);
    }
  }
  return zc::none;
}

zc::Vector<CompressionParameters> findValidExtensionOffers(StringPtr offers) {
  // A function to be called by the client that wants to offer extensions through
  // `Sec-WebSocket-Extensions`. This function takes the value of the header (a string) and
  // populates a Vector of all the valid offers.
  zc::Vector<CompressionParameters> result;

  auto extensions = splitParts(offers, ',');

  for (const auto& offer : extensions) {
    auto splitOffer = splitParts(offer, ';');
    if (splitOffer.front() != "permessage-deflate"_zc) { continue; }
    ZC_IF_SOME(validated, tryExtractParameters(splitOffer, false)) {
      // We need to swap the inbound/outbound properties since `tryExtractParameters` thinks we're
      // parsing as the server (`isAgreement` is false).
      auto tempCtx = validated.inboundNoContextTakeover;
      validated.inboundNoContextTakeover = validated.outboundNoContextTakeover;
      validated.outboundNoContextTakeover = tempCtx;
      auto tempWindow = validated.inboundMaxWindowBits;
      validated.inboundMaxWindowBits = validated.outboundMaxWindowBits;
      validated.outboundMaxWindowBits = tempWindow;
      result.add(zc::mv(validated));
    }
  }

  return zc::mv(result);
}

zc::String generateExtensionRequest(const ArrayPtr<CompressionParameters>& extensions) {
  // Build the `Sec-WebSocket-Extensions` request from the validated parameters.
  constexpr auto EXT = "permessage-deflate"_zc;
  auto offers = zc::heapArray<String>(extensions.size());
  size_t i = 0;
  for (const auto& offer : extensions) {
    offers[i] = zc::str(EXT);
    if (offer.outboundNoContextTakeover) {
      offers[i] = zc::str(offers[i], "; client_no_context_takeover");
    }
    if (offer.inboundNoContextTakeover) {
      offers[i] = zc::str(offers[i], "; server_no_context_takeover");
    }
    if (offer.outboundMaxWindowBits != zc::none) {
      auto w = ZC_ASSERT_NONNULL(offer.outboundMaxWindowBits);
      offers[i] = zc::str(offers[i], "; client_max_window_bits=", w);
    }
    if (offer.inboundMaxWindowBits != zc::none) {
      auto w = ZC_ASSERT_NONNULL(offer.inboundMaxWindowBits);
      offers[i] = zc::str(offers[i], "; server_max_window_bits=", w);
    }
    ++i;
  }
  return zc::strArray(offers, ", ");
}

zc::Maybe<CompressionParameters> tryParseExtensionOffers(StringPtr offers) {
  // Given a string of offers, accept the first valid offer by returning a `CompressionParameters`
  // struct. If there are no valid offers, return `zc::none`.
  auto splitOffers = splitParts(offers, ',');

  for (const auto& offer : splitOffers) {
    auto splitOffer = splitParts(offer, ';');

    if (splitOffer.front() != "permessage-deflate"_zc) {
      // Extension token was invalid.
      continue;
    }
    ZC_IF_SOME(config, tryExtractParameters(splitOffer, false)) { return zc::mv(config); }
  }
  return zc::none;
}

zc::Maybe<CompressionParameters> tryParseAllExtensionOffers(StringPtr offers,
                                                            CompressionParameters manualConfig) {
  // Similar to `tryParseExtensionOffers()`, however, this function is called when parsing in
  // `MANUAL_COMPRESSION` mode. In some cases, the server's configuration might not support the
  // `server_no_context_takeover` or `server_max_window_bits` parameters. Essentially, this function
  // will look at all the client's offers, and accept the first one that it can support.
  //
  // We differentiate these functions because in `AUTOMATIC_COMPRESSION` mode, ZC can support these
  // server restricting compression parameters.
  auto splitOffers = splitParts(offers, ',');

  for (const auto& offer : splitOffers) {
    auto splitOffer = splitParts(offer, ';');

    if (splitOffer.front() != "permessage-deflate"_zc) {
      // Extension token was invalid.
      continue;
    }
    ZC_IF_SOME(config, tryExtractParameters(splitOffer, false)) {
      ZC_IF_SOME(finalConfig, compareClientAndServerConfigs(config, manualConfig)) {
        // Found a compatible configuration between the server's config and client's offer.
        return zc::mv(finalConfig);
      }
    }
  }
  return zc::none;
}

zc::Maybe<CompressionParameters> compareClientAndServerConfigs(CompressionParameters requestConfig,
                                                               CompressionParameters manualConfig) {
  // We start from the `manualConfig` and go through a series of filters to get a compression
  // configuration that both the client and the server can agree upon. If no agreement can be made,
  // we return null.

  CompressionParameters acceptedParameters = manualConfig;

  // We only need to modify `client_no_context_takeover` and `server_no_context_takeover` when
  // `manualConfig` doesn't include them.
  if (manualConfig.inboundNoContextTakeover == false) {
    acceptedParameters.inboundNoContextTakeover = false;
  }

  if (manualConfig.outboundNoContextTakeover == false) {
    acceptedParameters.outboundNoContextTakeover = false;
    if (requestConfig.outboundNoContextTakeover == true) {
      // The client has told the server to not use context takeover. This is not a "hint",
      // rather it is a restriction on the server's configuration. If the server does not support
      // the configuration, it must reject the offer.
      return zc::none;
    }
  }

  // client_max_window_bits
  if (requestConfig.inboundMaxWindowBits != zc::none &&
      manualConfig.inboundMaxWindowBits != zc::none) {
    // We want `min(requestConfig, manualConfig)` in this case.
    auto reqBits = ZC_ASSERT_NONNULL(requestConfig.inboundMaxWindowBits);
    auto manualBits = ZC_ASSERT_NONNULL(manualConfig.inboundMaxWindowBits);
    if (reqBits < manualBits) { acceptedParameters.inboundMaxWindowBits = reqBits; }
  } else {
    // We will not reply with `client_max_window_bits`.
    acceptedParameters.inboundMaxWindowBits = zc::none;
  }

  // server_max_window_bits
  if (manualConfig.outboundMaxWindowBits != zc::none) {
    auto manualBits = ZC_ASSERT_NONNULL(manualConfig.outboundMaxWindowBits);
    if (requestConfig.outboundMaxWindowBits != zc::none) {
      // We want `min(requestConfig, manualConfig)` in this case.
      auto reqBits = ZC_ASSERT_NONNULL(requestConfig.outboundMaxWindowBits);
      if (reqBits < manualBits) { acceptedParameters.outboundMaxWindowBits = reqBits; }
    }
  } else {
    acceptedParameters.outboundMaxWindowBits = zc::none;
    if (requestConfig.outboundMaxWindowBits != zc::none) {
      // The client has told the server to use `server_max_window_bits`. This is not a "hint",
      // rather it is a restriction on the server's configuration. If the server does not support
      // the configuration, it must reject the offer.
      return zc::none;
    }
  }
  return acceptedParameters;
}

zc::String generateExtensionResponse(const CompressionParameters& parameters) {
  // Build the `Sec-WebSocket-Extensions` response from the agreed parameters.
  zc::String response = zc::str("permessage-deflate");
  if (parameters.inboundNoContextTakeover) {
    response = zc::str(response, "; client_no_context_takeover");
  }
  if (parameters.outboundNoContextTakeover) {
    response = zc::str(response, "; server_no_context_takeover");
  }
  if (parameters.inboundMaxWindowBits != zc::none) {
    auto w = ZC_REQUIRE_NONNULL(parameters.inboundMaxWindowBits);
    response = zc::str(response, "; client_max_window_bits=", w);
  }
  if (parameters.outboundMaxWindowBits != zc::none) {
    auto w = ZC_REQUIRE_NONNULL(parameters.outboundMaxWindowBits);
    response = zc::str(response, "; server_max_window_bits=", w);
  }
  return zc::mv(response);
}

zc::OneOf<CompressionParameters, zc::Exception> tryParseExtensionAgreement(
    const Maybe<CompressionParameters>& clientOffer, StringPtr agreedParameters) {
  // Like `tryParseExtensionOffers`, but called by the client when parsing the server's Response.
  // If the client must decline the agreement, we want to provide some details about what went wrong
  // (since the client has to fail the connection).
  constexpr auto FAILURE = "Server failed WebSocket handshake: "_zc;
  auto e = ZC_EXCEPTION(FAILED);

  if (clientOffer == zc::none) {
    // We've received extensions when we did not send any in the first place.
    e.setDescription(
        zc::str(FAILURE, "added Sec-WebSocket-Extensions when client did not offer any."));
    return zc::mv(e);
  }

  auto offers = splitParts(agreedParameters, ',');
  if (offers.size() != 1) {
    constexpr auto EXPECT =
        "expected exactly one extension (permessage-deflate) but received "
        "more than one."_zc;
    e.setDescription(zc::str(FAILURE, EXPECT));
    return zc::mv(e);
  }
  auto splitOffer = splitParts(offers.front(), ';');

  if (splitOffer.front() != "permessage-deflate"_zc) {
    e.setDescription(zc::str(FAILURE,
                             "response included a Sec-WebSocket-Extensions value that was "
                             "not permessage-deflate."));
    return zc::mv(e);
  }

  // Verify the parameters of our single extension, and compare it with the clients original offer.
  ZC_IF_SOME(config, tryExtractParameters(splitOffer, true)) {
    const auto& client = ZC_ASSERT_NONNULL(clientOffer);
    // The server might have ignored the client's hints regarding its compressor's configuration.
    // That's fine, but as the client, we still want to use those outbound compression parameters.
    if (config.outboundMaxWindowBits == zc::none) {
      config.outboundMaxWindowBits = client.outboundMaxWindowBits;
    } else
      ZC_IF_SOME(value, client.outboundMaxWindowBits) {
        if (value < ZC_ASSERT_NONNULL(config.outboundMaxWindowBits)) {
          // If the client asked for a value smaller than what the server responded with, use the
          // value that the client originally specified.
          config.outboundMaxWindowBits = value;
        }
      }
    if (config.outboundNoContextTakeover == false) {
      config.outboundNoContextTakeover = client.outboundNoContextTakeover;
    }
    return zc::mv(config);
  }

  // There was a problem parsing the server's `Sec-WebSocket-Extensions` response.
  e.setDescription(zc::str(FAILURE,
                           "the Sec-WebSocket-Extensions header in the Response included "
                           "an invalid value."));
  return zc::mv(e);
}

}  // namespace _

namespace {

class HeadResponseStream final : public zc::AsyncInputStream {
  // An input stream which returns no data, but `tryGetLength()` returns a specified value. Used
  // for HEAD responses, where the size is known but the body content is not sent.
public:
  HeadResponseStream(zc::Maybe<size_t> expectedLength) : expectedLength(expectedLength) {}

  zc::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    // TODO(someday): Maybe this should throw? We should not be trying to read the body of a
    // HEAD response.
    return constPromise<size_t, 0>();
  }

  zc::Maybe<uint64_t> tryGetLength() override { return expectedLength; }

  zc::Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
    return constPromise<uint64_t, 0>();
  }

private:
  zc::Maybe<size_t> expectedLength;
};

class HttpClientImpl final : public HttpClient, private HttpClientErrorHandler {
public:
  HttpClientImpl(const HttpHeaderTable& responseHeaderTable, zc::Own<zc::AsyncIoStream> rawStream,
                 HttpClientSettings settings)
      : httpInput(*rawStream, responseHeaderTable),
        httpOutput(*rawStream),
        ownStream(zc::mv(rawStream)),
        settings(zc::mv(settings)) {}

  bool canReuse() {
    // Returns true if we can immediately reuse this HttpClient for another message (so all
    // previous messages have been fully read).

    return !upgraded && !closed && httpInput.canReuse() && httpOutput.canReuse();
  }

  Request request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                  zc::Maybe<uint64_t> expectedBodySize = zc::none) override {
    ZC_REQUIRE(
        !upgraded,
        "can't make further requests on this HttpClient because it has been or is in the process "
        "of being upgraded");
    ZC_REQUIRE(!closed,
               "this HttpClient's connection has been closed by the server or due to an error");
    ZC_REQUIRE(httpOutput.canReuse(),
               "can't start new request until previous request body has been fully written");
    closeWatcherTask = zc::none;

    zc::StringPtr connectionHeaders[HttpHeaders::CONNECTION_HEADERS_COUNT];
    zc::String lengthStr;

    bool isGet = method == HttpMethod::GET || method == HttpMethod::HEAD;
    bool hasBody;

    ZC_IF_SOME(s, expectedBodySize) {
      if (isGet && s == 0) {
        // GET with empty body; don't send any Content-Length.
        hasBody = false;
      } else {
        lengthStr = zc::str(s);
        connectionHeaders[HttpHeaders::BuiltinIndices::CONTENT_LENGTH] = lengthStr;
        hasBody = true;
      }
    }
    else {
      if (isGet && headers.get(HttpHeaderId::TRANSFER_ENCODING) == zc::none) {
        // GET with empty body; don't send any Transfer-Encoding.
        hasBody = false;
      } else {
        // HACK: Normally GET requests shouldn't have bodies. But, if the caller set a
        //   Transfer-Encoding header on a GET, we use this as a special signal that it might
        //   actually want to send a body. This allows pass-through of a GET request with a chunked
        //   body to "just work". We strongly discourage writing any new code that sends
        //   full-bodied GETs.
        connectionHeaders[HttpHeaders::BuiltinIndices::TRANSFER_ENCODING] = "chunked";
        hasBody = true;
      }
    }

    httpOutput.writeHeaders(headers.serializeRequest(method, url, connectionHeaders));

    zc::Own<zc::AsyncOutputStream> bodyStream;
    if (!hasBody) {
      // No entity-body.
      httpOutput.finishBody();
      bodyStream = heap<HttpNullEntityWriter>();
    } else
      ZC_IF_SOME(s, expectedBodySize) {
        bodyStream = heap<HttpFixedLengthEntityWriter>(httpOutput, s);
      }
    else { bodyStream = heap<HttpChunkedEntityWriter>(httpOutput); }

    auto id = ++counter;

    auto responsePromise = httpInput.readResponseHeaders().then(
        [this, method, id](HttpHeaders::ResponseOrProtocolError&& responseOrProtocolError)
            -> HttpClient::Response {
          ZC_SWITCH_ONEOF(responseOrProtocolError) {
            ZC_CASE_ONEOF(response, HttpHeaders::Response) {
              auto& responseHeaders = httpInput.getHeaders();
              HttpClient::Response result{
                  response.statusCode, response.statusText, &responseHeaders,
                  httpInput.getEntityBody(HttpInputStreamImpl::RESPONSE, method,
                                          response.statusCode, responseHeaders)};

              if (fastCaseCmp<'c', 'l', 'o', 's', 'e'>(
                      responseHeaders.get(HttpHeaderId::CONNECTION).orDefault(nullptr).cStr())) {
                closed = true;
              } else if (counter == id) {
                watchForClose();
              } else {
                // Another request was already queued after this one, so we don't want to watch for
                // stream closure because we're fully expecting another response.
              }
              return result;
            }
            ZC_CASE_ONEOF(protocolError, HttpHeaders::ProtocolError) {
              closed = true;
              return settings.errorHandler.orDefault(*this).handleProtocolError(
                  zc::mv(protocolError));
            }
          }

          ZC_UNREACHABLE;
        });

    return {zc::mv(bodyStream), zc::mv(responsePromise)};
  }

  zc::Promise<WebSocketResponse> openWebSocket(zc::StringPtr url,
                                               const HttpHeaders& headers) override {
    ZC_REQUIRE(
        !upgraded,
        "can't make further requests on this HttpClient because it has been or is in the process "
        "of being upgraded");
    ZC_REQUIRE(!closed,
               "this HttpClient's connection has been closed by the server or due to an error");
    closeWatcherTask = zc::none;

    // Mark upgraded for now, even though the upgrade could fail, because we can't allow pipelined
    // requests in the meantime.
    upgraded = true;

    byte keyBytes[16]{};
    ZC_ASSERT_NONNULL(
        settings.entropySource,
        "can't use openWebSocket() because no EntropySource was provided when creating the "
        "HttpClient")
        .generate(keyBytes);
    auto keyBase64 = zc::encodeBase64(keyBytes);

    zc::StringPtr connectionHeaders[HttpHeaders::WEBSOCKET_CONNECTION_HEADERS_COUNT];
    connectionHeaders[HttpHeaders::BuiltinIndices::CONNECTION] = "Upgrade";
    connectionHeaders[HttpHeaders::BuiltinIndices::UPGRADE] = "websocket";
    connectionHeaders[HttpHeaders::BuiltinIndices::SEC_WEBSOCKET_VERSION] = "13";
    connectionHeaders[HttpHeaders::BuiltinIndices::SEC_WEBSOCKET_KEY] = keyBase64;

    zc::Maybe<zc::String> offeredExtensions;
    zc::Maybe<CompressionParameters> clientOffer;
    zc::Vector<CompressionParameters> extensions;
    auto compressionMode = settings.webSocketCompressionMode;

    if (compressionMode == HttpClientSettings::MANUAL_COMPRESSION) {
      ZC_IF_SOME(value, headers.get(HttpHeaderId::SEC_WEBSOCKET_EXTENSIONS)) {
        // Strip all `Sec-WebSocket-Extensions` except for `permessage-deflate`.
        extensions = _::findValidExtensionOffers(value);
      }
    } else if (compressionMode == HttpClientSettings::AUTOMATIC_COMPRESSION) {
      // If AUTOMATIC_COMPRESSION is enabled, we send `Sec-WebSocket-Extensions: permessage-deflate`
      // to the server and ignore the `headers` provided by the caller.
      extensions.add(CompressionParameters());
    }

    if (extensions.size() > 0) {
      clientOffer = extensions.front();
      // We hold on to a copy of the client's most preferred offer so even if the server
      // ignores `client_no_context_takeover` or `client_max_window_bits`, we can still refer to
      // the original offer made by the client (thereby allowing the client to use these
      // parameters).
      //
      // It's safe to ignore the remaining offers because:
      //  1. Offers are ordered by preference.
      //  2. `client_x` parameters are hints to the server and do not result in rejections, so the
      //     client is likely to put them in every offer anyways.
      connectionHeaders[HttpHeaders::BuiltinIndices::SEC_WEBSOCKET_EXTENSIONS] =
          offeredExtensions.emplace(_::generateExtensionRequest(extensions.asPtr()));
    }

    httpOutput.writeHeaders(headers.serializeRequest(HttpMethod::GET, url, connectionHeaders));

    // No entity-body.
    httpOutput.finishBody();

    auto id = ++counter;

    return httpInput.readResponseHeaders().then(
        [this, id, keyBase64 = zc::mv(keyBase64), clientOffer = zc::mv(clientOffer)](
            HttpHeaders::ResponseOrProtocolError&& responseOrProtocolError)
            -> HttpClient::WebSocketResponse {
          ZC_SWITCH_ONEOF(responseOrProtocolError) {
            ZC_CASE_ONEOF(response, HttpHeaders::Response) {
              auto& responseHeaders = httpInput.getHeaders();
              if (response.statusCode == 101) {
                if (!fastCaseCmp<'w', 'e', 'b', 's', 'o', 'c', 'k', 'e', 't'>(
                        responseHeaders.get(HttpHeaderId::UPGRADE).orDefault(nullptr).cStr())) {
                  zc::String ownMessage;
                  zc::StringPtr message;
                  ZC_IF_SOME(actual, responseHeaders.get(HttpHeaderId::UPGRADE)) {
                    ownMessage = zc::str(
                        "Server failed WebSocket handshake: incorrect Upgrade header: "
                        "expected 'websocket', got '",
                        actual, "'.");
                    message = ownMessage;
                  }
                  else { message = "Server failed WebSocket handshake: missing Upgrade header."; }
                  return settings.errorHandler.orDefault(*this).handleWebSocketProtocolError(
                      {502, "Bad Gateway", message, nullptr});
                }

                auto expectedAccept = generateWebSocketAccept(keyBase64);
                if (responseHeaders.get(HttpHeaderId::SEC_WEBSOCKET_ACCEPT).orDefault(nullptr) !=
                    expectedAccept) {
                  zc::String ownMessage;
                  zc::StringPtr message;
                  ZC_IF_SOME(actual, responseHeaders.get(HttpHeaderId::SEC_WEBSOCKET_ACCEPT)) {
                    ownMessage = zc::str(
                        "Server failed WebSocket handshake: incorrect Sec-WebSocket-Accept header: "
                        "expected '",
                        expectedAccept, "', got '", actual, "'.");
                    message = ownMessage;
                  }
                  else { message = "Server failed WebSocket handshake: missing Upgrade header."; }
                  return settings.errorHandler.orDefault(*this).handleWebSocketProtocolError(
                      {502, "Bad Gateway", message, nullptr});
                }

                zc::Maybe<CompressionParameters> compressionParameters;
                if (settings.webSocketCompressionMode != HttpClientSettings::NO_COMPRESSION) {
                  ZC_IF_SOME(agreedParameters,
                             responseHeaders.get(HttpHeaderId::SEC_WEBSOCKET_EXTENSIONS)) {
                    auto parseResult = _::tryParseExtensionAgreement(clientOffer, agreedParameters);
                    if (parseResult.is<zc::Exception>()) {
                      return settings.errorHandler.orDefault(*this).handleWebSocketProtocolError(
                          {502, "Bad Gateway", parseResult.get<zc::Exception>().getDescription(),
                           nullptr});
                    }
                    compressionParameters.emplace(zc::mv(parseResult.get<CompressionParameters>()));
                  }
                }

                return {
                    response.statusCode,
                    response.statusText,
                    &httpInput.getHeaders(),
                    upgradeToWebSocket(zc::mv(ownStream), httpInput, httpOutput,
                                       settings.entropySource, zc::mv(compressionParameters),
                                       settings.webSocketErrorHandler),
                };
              } else {
                upgraded = false;
                HttpClient::WebSocketResponse result{
                    response.statusCode, response.statusText, &responseHeaders,
                    httpInput.getEntityBody(HttpInputStreamImpl::RESPONSE, HttpMethod::GET,
                                            response.statusCode, responseHeaders)};
                if (fastCaseCmp<'c', 'l', 'o', 's', 'e'>(
                        responseHeaders.get(HttpHeaderId::CONNECTION).orDefault(nullptr).cStr())) {
                  closed = true;
                } else if (counter == id) {
                  watchForClose();
                } else {
                  // Another request was already queued after this one, so we don't want to watch
                  // for stream closure because we're fully expecting another response.
                }
                return result;
              }
            }
            ZC_CASE_ONEOF(protocolError, HttpHeaders::ProtocolError) {
              return settings.errorHandler.orDefault(*this).handleWebSocketProtocolError(
                  zc::mv(protocolError));
            }
          }

          ZC_UNREACHABLE;
        });
  }

  ConnectRequest connect(zc::StringPtr host, const HttpHeaders& headers,
                         HttpConnectSettings settings) override {
    ZC_REQUIRE(
        !upgraded,
        "can't make further requests on this HttpClient because it has been or is in the process "
        "of being upgraded");
    ZC_REQUIRE(!closed,
               "this HttpClient's connection has been closed by the server or due to an error");
    ZC_REQUIRE(httpOutput.canReuse(),
               "can't start new request until previous request body has been fully written");

    if (settings.useTls) { ZC_UNIMPLEMENTED("This HttpClient does not support TLS."); }

    closeWatcherTask = zc::none;

    // Mark upgraded for now even though the tunnel could fail, because we can't allow pipelined
    // requests in the meantime.
    upgraded = true;

    zc::StringPtr connectionHeaders[HttpHeaders::CONNECTION_HEADERS_COUNT];

    httpOutput.writeHeaders(headers.serializeConnectRequest(host, connectionHeaders));

    auto id = ++counter;

    auto split =
        httpInput.readResponseHeaders()
            .then([this, id](HttpHeaders::ResponseOrProtocolError&& responseOrProtocolError) mutable
                  -> zc::Tuple<zc::Promise<ConnectRequest::Status>,
                               zc::Promise<zc::Maybe<HttpInputStreamImpl::ReleasedBuffer>>> {
              ZC_SWITCH_ONEOF(responseOrProtocolError) {
                ZC_CASE_ONEOF(response, HttpHeaders::Response) {
                  auto& responseHeaders = httpInput.getHeaders();
                  if (response.statusCode < 200 || response.statusCode >= 300) {
                    // Any statusCode that is not in the 2xx range in interpreted
                    // as an HTTP response. Any status code in the 2xx range is
                    // interpreted as a successful CONNECT response.
                    closed = true;
                    return zc::tuple(
                        ConnectRequest::Status(
                            response.statusCode, zc::str(response.statusText),
                            zc::heap(responseHeaders.clone()),
                            httpInput.getEntityBody(HttpInputStreamImpl::RESPONSE,
                                                    HttpConnectMethod(), response.statusCode,
                                                    responseHeaders)),
                        ZC_EXCEPTION(DISCONNECTED, "the connect request was rejected"));
                  }
                  ZC_ASSERT(counter == id);
                  return zc::tuple(
                      ConnectRequest::Status(response.statusCode, zc::str(response.statusText),
                                             zc::heap(responseHeaders.clone())),
                      zc::Maybe<HttpInputStreamImpl::ReleasedBuffer>(httpInput.releaseBuffer()));
                }
                ZC_CASE_ONEOF(protocolError, HttpHeaders::ProtocolError) {
                  closed = true;
                  auto response = handleProtocolError(protocolError);
                  return zc::tuple(ConnectRequest::Status(
                                       response.statusCode, zc::str(response.statusText),
                                       zc::heap(response.headers->clone()), zc::mv(response.body)),
                                   ZC_EXCEPTION(DISCONNECTED, "the connect request errored"));
                }
              }
              ZC_UNREACHABLE;
            })
            .split();

    return ConnectRequest{
        zc::mv(zc::get<0>(split)),  // Promise for the result
        heap<AsyncIoStreamWithGuards>(
            zc::mv(ownStream),
            zc::mv(zc::get<1>(split)) /* read guard (Promise for the ReleasedBuffer) */,
            httpOutput.flush() /* write guard (void Promise) */)};
  }

private:
  HttpInputStreamImpl httpInput;
  HttpOutputStream httpOutput;
  zc::Own<AsyncIoStream> ownStream;
  HttpClientSettings settings;
  zc::Maybe<zc::Promise<void>> closeWatcherTask;
  bool upgraded = false;
  bool closed = false;

  uint counter = 0;
  // Counts requests for the sole purpose of detecting if more requests have been made after some
  // point in history.

  void watchForClose() {
    closeWatcherTask = httpInput.awaitNextMessage()
                           .then([this](bool hasData) -> zc::Promise<void> {
                             if (hasData) {
                               // Uhh... The server sent some data before we asked for anything.
                               // Perhaps due to properties of this application, the server somehow
                               // already knows what the next request will be, and it is trying to
                               // optimize. Or maybe this is some sort of test and the server is
                               // just replaying a script. In any case, we will humor it -- leave
                               // the data in the buffer and let it become the response to the next
                               // request.
                               return zc::READY_NOW;
                             } else {
                               // EOF -- server disconnected.
                               closed = true;
                               if (httpOutput.isInBody()) {
                                 // Huh, the application is still sending a request. We should let
                                 // it finish. We do not need to proactively free the socket in this
                                 // case because we know that we're not sitting in a reusable
                                 // connection pool, because we know the application is still
                                 // actively using the connection.
                                 return zc::READY_NOW;
                               } else {
                                 return httpOutput.flush().then([this]() {
                                   // We might be sitting in NetworkAddressHttpClient's
                                   // `availableClients` pool. We don't have a way to notify it to
                                   // remove this client from the pool; instead, when it tries to
                                   // pull this client from the pool later, it will notice the
                                   // client is dead and will discard it then. But, we would like to
                                   // avoid holding on to a socket forever. So, destroy the socket
                                   // now.
                                   // TODO(cleanup): Maybe we should arrange to proactively remove
                                   // ourselves? Seems
                                   //   like the code will be awkward.
                                   ownStream = nullptr;
                                 });
                               }
                             }
                           })
                           .eagerlyEvaluate(nullptr);
  }
};

}  // namespace

zc::Promise<HttpClient::WebSocketResponse> HttpClient::openWebSocket(zc::StringPtr url,
                                                                     const HttpHeaders& headers) {
  return request(HttpMethod::GET, url, headers, zc::none)
      .response.then([](HttpClient::Response&& response) -> WebSocketResponse {
        zc::OneOf<zc::Own<zc::AsyncInputStream>, zc::Own<WebSocket>> body;
        body.init<zc::Own<zc::AsyncInputStream>>(zc::mv(response.body));

        return {response.statusCode, response.statusText, response.headers, zc::mv(body)};
      });
}

HttpClient::ConnectRequest HttpClient::connect(zc::StringPtr host, const HttpHeaders& headers,
                                               HttpConnectSettings settings) {
  ZC_UNIMPLEMENTED("CONNECT is not implemented by this HttpClient");
}

zc::Own<HttpClient> newHttpClient(const HttpHeaderTable& responseHeaderTable,
                                  zc::AsyncIoStream& stream, HttpClientSettings settings) {
  return zc::heap<HttpClientImpl>(responseHeaderTable,
                                  zc::Own<zc::AsyncIoStream>(&stream, zc::NullDisposer::instance),
                                  zc::mv(settings));
}

HttpClient::Response HttpClientErrorHandler::handleProtocolError(
    HttpHeaders::ProtocolError protocolError) {
  ZC_FAIL_REQUIRE(protocolError.description) { break; }
  return HttpClient::Response();
}

HttpClient::WebSocketResponse HttpClientErrorHandler::handleWebSocketProtocolError(
    HttpHeaders::ProtocolError protocolError) {
  auto response = handleProtocolError(protocolError);
  return HttpClient::WebSocketResponse{response.statusCode, response.statusText, response.headers,
                                       zc::mv(response.body)};
}

zc::Exception WebSocketErrorHandler::handleWebSocketProtocolError(
    WebSocket::ProtocolError protocolError) {
  return ZC_EXCEPTION(FAILED, "WebSocket protocol error", protocolError.statusCode,
                      protocolError.description);
}

class PausableReadAsyncIoStream::PausableRead {
public:
  PausableRead(zc::PromiseFulfiller<size_t>& fulfiller, PausableReadAsyncIoStream& parent,
               void* buffer, size_t minBytes, size_t maxBytes)
      : fulfiller(fulfiller),
        parent(parent),
        operationBuffer(buffer),
        operationMinBytes(minBytes),
        operationMaxBytes(maxBytes),
        innerRead(parent.tryReadImpl(operationBuffer, operationMinBytes, operationMaxBytes)
                      .then(
                          [&fulfiller](size_t size) mutable -> zc::Promise<void> {
                            fulfiller.fulfill(zc::mv(size));
                            return zc::READY_NOW;
                          },
                          [&fulfiller](zc::Exception&& err) { fulfiller.reject(zc::mv(err)); })) {
    ZC_ASSERT(parent.maybePausableRead == zc::none);
    parent.maybePausableRead = *this;
  }

  ~PausableRead() noexcept(false) { parent.maybePausableRead = zc::none; }

  void pause() { innerRead = nullptr; }

  void unpause() {
    innerRead = parent.tryReadImpl(operationBuffer, operationMinBytes, operationMaxBytes)
                    .then(
                        [this](size_t size) -> zc::Promise<void> {
                          fulfiller.fulfill(zc::mv(size));
                          return zc::READY_NOW;
                        },
                        [this](zc::Exception&& err) { fulfiller.reject(zc::mv(err)); });
  }

  void reject(zc::Exception&& exc) { fulfiller.reject(zc::mv(exc)); }

private:
  zc::PromiseFulfiller<size_t>& fulfiller;
  PausableReadAsyncIoStream& parent;

  void* operationBuffer;
  size_t operationMinBytes;
  size_t operationMaxBytes;
  // The parameters of the current tryRead call. Used to unpause a paused read.

  zc::Promise<void> innerRead;
  // The current pending read.
};

_::Deferred<zc::Function<void()>> PausableReadAsyncIoStream::trackRead() {
  ZC_REQUIRE(!currentlyReading, "only one read is allowed at any one time");
  currentlyReading = true;
  return zc::defer<zc::Function<void()>>([this]() { currentlyReading = false; });
}

_::Deferred<zc::Function<void()>> PausableReadAsyncIoStream::trackWrite() {
  ZC_REQUIRE(!currentlyWriting, "only one write is allowed at any one time");
  currentlyWriting = true;
  return zc::defer<zc::Function<void()>>([this]() { currentlyWriting = false; });
}

zc::Promise<size_t> PausableReadAsyncIoStream::tryRead(void* buffer, size_t minBytes,
                                                       size_t maxBytes) {
  return zc::newAdaptedPromise<size_t, PausableRead>(*this, buffer, minBytes, maxBytes);
}

zc::Promise<size_t> PausableReadAsyncIoStream::tryReadImpl(void* buffer, size_t minBytes,
                                                           size_t maxBytes) {
  // Hack: evalNow used here because `newAdaptedPromise` has a bug. We may need to change
  // `PromiseDisposer::alloc` to not be `noexcept` but in order to do so we'll need to benchmark
  // its performance.
  return zc::evalNow([&]() -> zc::Promise<size_t> {
    return inner->tryRead(buffer, minBytes, maxBytes).attach(trackRead());
  });
}

zc::Maybe<uint64_t> PausableReadAsyncIoStream::tryGetLength() { return inner->tryGetLength(); }

zc::Promise<uint64_t> PausableReadAsyncIoStream::pumpTo(zc::AsyncOutputStream& output,
                                                        uint64_t amount) {
  return zc::unoptimizedPumpTo(*this, output, amount);
}

zc::Promise<void> PausableReadAsyncIoStream::write(ArrayPtr<const byte> buffer) {
  return inner->write(buffer).attach(trackWrite());
}

zc::Promise<void> PausableReadAsyncIoStream::write(
    zc::ArrayPtr<const zc::ArrayPtr<const byte>> pieces) {
  return inner->write(pieces).attach(trackWrite());
}

zc::Maybe<zc::Promise<uint64_t>> PausableReadAsyncIoStream::tryPumpFrom(zc::AsyncInputStream& input,
                                                                        uint64_t amount) {
  auto result = inner->tryPumpFrom(input, amount);
  ZC_IF_SOME(r, result) { return r.attach(trackWrite()); }
  else { return zc::none; }
}

zc::Promise<void> PausableReadAsyncIoStream::whenWriteDisconnected() {
  return inner->whenWriteDisconnected();
}

void PausableReadAsyncIoStream::shutdownWrite() { inner->shutdownWrite(); }

void PausableReadAsyncIoStream::abortRead() { inner->abortRead(); }

zc::Maybe<int> PausableReadAsyncIoStream::getFd() const { return inner->getFd(); }

void PausableReadAsyncIoStream::pause() {
  ZC_IF_SOME(pausable, maybePausableRead) { pausable.pause(); }
}

void PausableReadAsyncIoStream::unpause() {
  ZC_IF_SOME(pausable, maybePausableRead) { pausable.unpause(); }
}

bool PausableReadAsyncIoStream::getCurrentlyReading() { return currentlyReading; }

bool PausableReadAsyncIoStream::getCurrentlyWriting() { return currentlyWriting; }

zc::Own<zc::AsyncIoStream> PausableReadAsyncIoStream::takeStream() { return zc::mv(inner); }

void PausableReadAsyncIoStream::replaceStream(zc::Own<zc::AsyncIoStream> stream) {
  inner = zc::mv(stream);
}

void PausableReadAsyncIoStream::reject(zc::Exception&& exc) {
  ZC_IF_SOME(pausable, maybePausableRead) { pausable.reject(zc::mv(exc)); }
}

// =======================================================================================

namespace {

class NetworkAddressHttpClient final : public HttpClient {
public:
  NetworkAddressHttpClient(zc::Timer& timer, const HttpHeaderTable& responseHeaderTable,
                           zc::Own<zc::NetworkAddress> address, HttpClientSettings settings)
      : timer(timer),
        responseHeaderTable(responseHeaderTable),
        address(zc::mv(address)),
        settings(zc::mv(settings)) {}

  bool isDrained() {
    // Returns true if there are no open connections.
    return activeConnectionCount == 0 && availableClients.empty();
  }

  zc::Promise<void> onDrained() {
    // Returns a promise which resolves the next time isDrained() transitions from false to true.
    auto paf = zc::newPromiseAndFulfiller<void>();
    drainedFulfiller = zc::mv(paf.fulfiller);
    return zc::mv(paf.promise);
  }

  Request request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                  zc::Maybe<uint64_t> expectedBodySize = zc::none) override {
    auto refcounted = getClient();
    auto result = refcounted->client->request(method, url, headers, expectedBodySize);
    result.body = result.body.attach(zc::addRef(*refcounted));
    result.response =
        result.response.then([refcounted = zc::mv(refcounted)](Response&& response) mutable {
          response.body = response.body.attach(zc::mv(refcounted));
          return zc::mv(response);
        });
    return result;
  }

  zc::Promise<WebSocketResponse> openWebSocket(zc::StringPtr url,
                                               const HttpHeaders& headers) override {
    auto refcounted = getClient();
    auto result = refcounted->client->openWebSocket(url, headers);
    return result.then([refcounted = zc::mv(refcounted)](WebSocketResponse&& response) mutable {
      ZC_SWITCH_ONEOF(response.webSocketOrBody) {
        ZC_CASE_ONEOF(body, zc::Own<zc::AsyncInputStream>) {
          response.webSocketOrBody = body.attach(zc::mv(refcounted));
        }
        ZC_CASE_ONEOF(ws, zc::Own<WebSocket>) {
          // The only reason we need to attach the client to the WebSocket is because otherwise
          // the response headers will be deleted prematurely. Otherwise, the WebSocket has taken
          // ownership of the connection.
          //
          // TODO(perf): Maybe we could transfer ownership of the response headers specifically?
          response.webSocketOrBody = ws.attach(zc::mv(refcounted));
        }
      }
      return zc::mv(response);
    });
  }

  ConnectRequest connect(zc::StringPtr host, const HttpHeaders& headers,
                         HttpConnectSettings settings) override {
    auto refcounted = getClient();
    auto request = refcounted->client->connect(host, headers, settings);
    return ConnectRequest{request.status.attach(zc::addRef(*refcounted)),
                          request.connection.attach(zc::mv(refcounted))};
  }

private:
  zc::Timer& timer;
  const HttpHeaderTable& responseHeaderTable;
  zc::Own<zc::NetworkAddress> address;
  HttpClientSettings settings;

  zc::Maybe<zc::Own<zc::PromiseFulfiller<void>>> drainedFulfiller;
  uint activeConnectionCount = 0;

  bool timeoutsScheduled = false;
  zc::Promise<void> timeoutTask = nullptr;

  struct AvailableClient {
    zc::Own<HttpClientImpl> client;
    zc::TimePoint expires;
  };

  std::deque<AvailableClient> availableClients;

  struct RefcountedClient final : public zc::Refcounted {
    RefcountedClient(NetworkAddressHttpClient& parent, zc::Own<HttpClientImpl> client)
        : parent(parent), client(zc::mv(client)) {
      ++parent.activeConnectionCount;
    }
    ~RefcountedClient() noexcept(false) {
      --parent.activeConnectionCount;
      ZC_IF_SOME(exception, zc::runCatchingExceptions(
                                [&]() { parent.returnClientToAvailable(zc::mv(client)); })) {
        ZC_LOG(ERROR, exception);
      }
    }

    NetworkAddressHttpClient& parent;
    zc::Own<HttpClientImpl> client;
  };

  zc::Own<RefcountedClient> getClient() {
    for (;;) {
      if (availableClients.empty()) {
        auto stream = newPromisedStream(address->connect());
        return zc::refcounted<RefcountedClient>(
            *this, zc::heap<HttpClientImpl>(responseHeaderTable, zc::mv(stream), settings));
      } else {
        auto client = zc::mv(availableClients.back().client);
        availableClients.pop_back();
        if (client->canReuse()) { return zc::refcounted<RefcountedClient>(*this, zc::mv(client)); }
        // Whoops, this client's connection was closed by the server at some point. Discard.
      }
    }
  }

  void returnClientToAvailable(zc::Own<HttpClientImpl> client) {
    // Only return the connection to the pool if it is reusable and if our settings indicate we
    // should reuse connections.
    if (client->canReuse() && settings.idleTimeout > 0 * zc::SECONDS) {
      availableClients.push_back(
          AvailableClient{zc::mv(client), timer.now() + settings.idleTimeout});
    }

    // Call this either way because it also signals onDrained().
    if (!timeoutsScheduled) {
      timeoutsScheduled = true;
      timeoutTask = applyTimeouts();
    }
  }

  zc::Promise<void> applyTimeouts() {
    if (availableClients.empty()) {
      timeoutsScheduled = false;
      if (activeConnectionCount == 0) {
        ZC_IF_SOME(f, drainedFulfiller) {
          f->fulfill();
          drainedFulfiller = zc::none;
        }
      }
      return zc::READY_NOW;
    } else {
      auto time = availableClients.front().expires;
      return timer.atTime(time).then([this, time]() {
        while (!availableClients.empty() && availableClients.front().expires <= time) {
          availableClients.pop_front();
        }
        return applyTimeouts();
      });
    }
  }
};

class TransitionaryAsyncIoStream final : public zc::AsyncIoStream {
  // This specialised AsyncIoStream is used by NetworkHttpClient to support startTls.
public:
  TransitionaryAsyncIoStream(zc::Own<zc::AsyncIoStream> unencryptedStream)
      : inner(zc::heap<zc::PausableReadAsyncIoStream>(zc::mv(unencryptedStream))) {}

  zc::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return inner->tryRead(buffer, minBytes, maxBytes);
  }

  zc::Maybe<uint64_t> tryGetLength() override { return inner->tryGetLength(); }

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

  zc::Promise<void> whenWriteDisconnected() override { return inner->whenWriteDisconnected(); }

  void shutdownWrite() override { inner->shutdownWrite(); }

  void abortRead() override { inner->abortRead(); }

  zc::Maybe<int> getFd() const override { return inner->getFd(); }

  void startTls(zc::SecureNetworkWrapper* wrapper, zc::StringPtr expectedServerHostname) {
    // Pause any potential pending reads.
    inner->pause();

    ZC_ON_SCOPE_FAILURE({ inner->reject(ZC_EXCEPTION(FAILED, "StartTls failed.")); });

    ZC_ASSERT(!inner->getCurrentlyReading() && !inner->getCurrentlyWriting(),
              "Cannot call startTls while reads/writes are outstanding");
    zc::Promise<zc::Own<zc::AsyncIoStream>> secureStream =
        wrapper->wrapClient(inner->takeStream(), expectedServerHostname);
    inner->replaceStream(zc::newPromisedStream(zc::mv(secureStream)));
    // Resume any previous pending reads.
    inner->unpause();
  }

private:
  zc::Own<zc::PausableReadAsyncIoStream> inner;
};

class PromiseNetworkAddressHttpClient final : public HttpClient {
  // An HttpClient which waits for a promise to resolve then forwards all calls to the promised
  // client.

public:
  PromiseNetworkAddressHttpClient(zc::Promise<zc::Own<NetworkAddressHttpClient>> promise)
      : promise(promise
                    .then([this](zc::Own<NetworkAddressHttpClient>&& client) {
                      this->client = zc::mv(client);
                    })
                    .fork()) {}

  bool isDrained() {
    ZC_IF_SOME(c, client) { return c->isDrained(); }
    else { return failed; }
  }

  zc::Promise<void> onDrained() {
    ZC_IF_SOME(c, client) { return c->onDrained(); }
    else {
      return promise.addBranch().then([this]() { return ZC_ASSERT_NONNULL(client)->onDrained(); },
                                      [this](zc::Exception&& e) {
                                        // Connecting failed. Treat as immediately drained.
                                        failed = true;
                                        return zc::READY_NOW;
                                      });
    }
  }

  Request request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                  zc::Maybe<uint64_t> expectedBodySize = zc::none) override {
    ZC_IF_SOME(c, client) { return c->request(method, url, headers, expectedBodySize); }
    else {
      // This gets complicated since request() returns a pair of a stream and a promise.
      auto urlCopy = zc::str(url);
      auto headersCopy = headers.clone();
      auto combined = promise.addBranch().then(
          [this, method, expectedBodySize, url = zc::mv(urlCopy), headers = zc::mv(headersCopy)]()
              -> zc::Tuple<zc::Own<zc::AsyncOutputStream>, zc::Promise<Response>> {
            auto req = ZC_ASSERT_NONNULL(client)->request(method, url, headers, expectedBodySize);
            return zc::tuple(zc::mv(req.body), zc::mv(req.response));
          });

      auto split = combined.split();
      return {newPromisedStream(zc::mv(zc::get<0>(split))), zc::mv(zc::get<1>(split))};
    }
  }

  zc::Promise<WebSocketResponse> openWebSocket(zc::StringPtr url,
                                               const HttpHeaders& headers) override {
    ZC_IF_SOME(c, client) { return c->openWebSocket(url, headers); }
    else {
      auto urlCopy = zc::str(url);
      auto headersCopy = headers.clone();
      return promise.addBranch().then(
          [this, url = zc::mv(urlCopy), headers = zc::mv(headersCopy)]() {
            return ZC_ASSERT_NONNULL(client)->openWebSocket(url, headers);
          });
    }
  }

  ConnectRequest connect(zc::StringPtr host, const HttpHeaders& headers,
                         HttpConnectSettings settings) override {
    ZC_IF_SOME(c, client) { return c->connect(host, headers, settings); }
    else {
      auto split =
          promise.addBranch()
              .then([this, host = zc::str(host), headers = headers.clone(),
                     settings]() mutable -> zc::Tuple<zc::Promise<ConnectRequest::Status>,
                                                      zc::Promise<zc::Own<zc::AsyncIoStream>>> {
                auto request = ZC_ASSERT_NONNULL(client)->connect(host, headers, zc::mv(settings));
                return zc::tuple(zc::mv(request.status), zc::mv(request.connection));
              })
              .split();

      return ConnectRequest{zc::mv(zc::get<0>(split)),
                            zc::newPromisedStream(zc::mv(zc::get<1>(split)))};
    }
  }

private:
  zc::ForkedPromise<void> promise;
  zc::Maybe<zc::Own<NetworkAddressHttpClient>> client;
  bool failed = false;
};

class NetworkHttpClient final : public HttpClient, private zc::TaskSet::ErrorHandler {
public:
  NetworkHttpClient(zc::Timer& timer, const HttpHeaderTable& responseHeaderTable,
                    zc::Network& network, zc::Maybe<zc::Network&> tlsNetwork,
                    HttpClientSettings settings)
      : timer(timer),
        responseHeaderTable(responseHeaderTable),
        network(network),
        tlsNetwork(tlsNetwork),
        settings(zc::mv(settings)),
        tasks(*this) {}

  Request request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                  zc::Maybe<uint64_t> expectedBodySize = zc::none) override {
    // We need to parse the proxy-style URL to convert it to host-style.
    // Use URL parsing options that avoid unnecessary rewrites.
    Url::Options urlOptions;
    urlOptions.allowEmpty = true;
    urlOptions.percentDecode = false;

    auto parsed = Url::parse(url, Url::HTTP_PROXY_REQUEST, urlOptions);
    auto path = parsed.toString(Url::HTTP_REQUEST);
    auto headersCopy = headers.clone();
    headersCopy.set(HttpHeaderId::HOST, parsed.host);
    return getClient(parsed).request(method, path, headersCopy, expectedBodySize);
  }

  zc::Promise<WebSocketResponse> openWebSocket(zc::StringPtr url,
                                               const HttpHeaders& headers) override {
    // We need to parse the proxy-style URL to convert it to origin-form.
    // https://www.rfc-editor.org/rfc/rfc9112.html#name-origin-form
    // Use URL parsing options that avoid unnecessary rewrites.
    Url::Options urlOptions;
    urlOptions.allowEmpty = true;
    urlOptions.percentDecode = false;

    auto parsed = Url::parse(url, Url::HTTP_PROXY_REQUEST, urlOptions);
    auto path = parsed.toString(Url::HTTP_REQUEST);
    auto headersCopy = headers.clone();
    headersCopy.set(HttpHeaderId::HOST, parsed.host);
    return getClient(parsed).openWebSocket(path, headersCopy);
  }

  ConnectRequest connect(zc::StringPtr host, const HttpHeaders& headers,
                         HttpConnectSettings connectSettings) override {
    // We want to connect directly instead of going through a proxy here.
    // https://github.com/capnproto/capnproto/pull/1454#discussion_r900414879
    zc::Maybe<zc::Promise<zc::Own<zc::NetworkAddress>>> addr;
    if (connectSettings.useTls) {
      zc::Network& tlsNet = ZC_REQUIRE_NONNULL(tlsNetwork, "this HttpClient doesn't support TLS");
      addr = tlsNet.parseAddress(host);
    } else {
      addr = network.parseAddress(host);
    }

    auto split =
        ZC_ASSERT_NONNULL(addr)
            .then([this](auto address) {
              return address->connect()
                  .then([this](
                            auto connection) -> zc::Tuple<zc::Promise<ConnectRequest::Status>,
                                                          zc::Promise<zc::Own<zc::AsyncIoStream>>> {
                    return zc::tuple(
                        ConnectRequest::Status(
                            200, zc::str("OK"),
                            zc::heap<zc::HttpHeaders>(responseHeaderTable)  // Empty headers
                            ),
                        zc::mv(connection));
                  })
                  .attach(zc::mv(address));
            })
            .split();

    auto connection = zc::newPromisedStream(zc::mv(zc::get<1>(split)));

    if (!connectSettings.useTls) {
      ZC_IF_SOME(wrapper, settings.tlsContext) {
        ZC_IF_SOME(tlsStarter, connectSettings.tlsStarter) {
          auto transitConnectionRef =
              zc::refcountedWrapper(zc::heap<TransitionaryAsyncIoStream>(zc::mv(connection)));
          Function<zc::Promise<void>(zc::StringPtr)> cb =
              [&wrapper, ref1 = transitConnectionRef->addWrappedRef()](
                  zc::StringPtr expectedServerHostname) mutable {
                ref1->startTls(&wrapper, expectedServerHostname);
                return zc::READY_NOW;
              };
          connection = transitConnectionRef->addWrappedRef();
          tlsStarter = zc::mv(cb);
        }
      }
    }

    return ConnectRequest{zc::mv(zc::get<0>(split)), zc::mv(connection)};
  }

private:
  zc::Timer& timer;
  const HttpHeaderTable& responseHeaderTable;
  zc::Network& network;
  zc::Maybe<zc::Network&> tlsNetwork;
  HttpClientSettings settings;

  struct Host {
    zc::String name;  // including port, if non-default
    zc::Own<PromiseNetworkAddressHttpClient> client;
  };

  std::map<zc::StringPtr, Host> httpHosts;
  std::map<zc::StringPtr, Host> httpsHosts;

  struct RequestInfo {
    HttpMethod method;
    zc::String hostname;
    zc::String path;
    HttpHeaders headers;
    zc::Maybe<uint64_t> expectedBodySize;
  };

  zc::TaskSet tasks;

  HttpClient& getClient(zc::Url& parsed) {
    bool isHttps = parsed.scheme == "https";
    bool isHttp = parsed.scheme == "http";
    ZC_REQUIRE(isHttp || isHttps);

    auto& hosts = isHttps ? httpsHosts : httpHosts;

    // Look for a cached client for this host.
    // TODO(perf): It would be nice to recognize when different hosts have the same address and
    //   reuse the same connection pool, but:
    //   - We'd need a reliable way to compare NetworkAddresses, e.g. .equals() and .hashCode().
    //     It's very Java... ick.
    //   - Correctly handling TLS would be tricky: we'd need to verify that the new hostname is
    //     on the certificate. When SNI is in use we might have to request an additional
    //     certificate (is that possible?).
    auto iter = hosts.find(parsed.host);

    if (iter == hosts.end()) {
      // Need to open a new connection.
      zc::Network* networkToUse = &network;
      if (isHttps) {
        networkToUse = &ZC_REQUIRE_NONNULL(tlsNetwork, "this HttpClient doesn't support HTTPS");
      }

      auto promise = networkToUse->parseAddress(parsed.host, isHttps ? 443 : 80)
                         .then([this](zc::Own<zc::NetworkAddress> addr) {
                           return zc::heap<NetworkAddressHttpClient>(timer, responseHeaderTable,
                                                                     zc::mv(addr), settings);
                         });

      Host host{zc::mv(parsed.host), zc::heap<PromiseNetworkAddressHttpClient>(zc::mv(promise))};
      zc::StringPtr nameRef = host.name;

      auto insertResult = hosts.insert(std::make_pair(nameRef, zc::mv(host)));
      ZC_ASSERT(insertResult.second);
      iter = insertResult.first;

      tasks.add(handleCleanup(hosts, iter));
    }

    return *iter->second.client;
  }

  zc::Promise<void> handleCleanup(std::map<zc::StringPtr, Host>& hosts,
                                  std::map<zc::StringPtr, Host>::iterator iter) {
    return iter->second.client->onDrained().then([this, &hosts, iter]() -> zc::Promise<void> {
      // Double-check that it's really drained to avoid race conditions.
      if (iter->second.client->isDrained()) {
        hosts.erase(iter);
        return zc::READY_NOW;
      } else {
        return handleCleanup(hosts, iter);
      }
    });
  }

  void taskFailed(zc::Exception&& exception) override { ZC_LOG(ERROR, exception); }
};

}  // namespace

zc::Own<HttpClient> newHttpClient(zc::Timer& timer, const HttpHeaderTable& responseHeaderTable,
                                  zc::NetworkAddress& addr, HttpClientSettings settings) {
  return zc::heap<NetworkAddressHttpClient>(
      timer, responseHeaderTable, zc::Own<zc::NetworkAddress>(&addr, zc::NullDisposer::instance),
      zc::mv(settings));
}

zc::Own<HttpClient> newHttpClient(zc::Timer& timer, const HttpHeaderTable& responseHeaderTable,
                                  zc::Network& network, zc::Maybe<zc::Network&> tlsNetwork,
                                  HttpClientSettings settings) {
  return zc::heap<NetworkHttpClient>(timer, responseHeaderTable, network, tlsNetwork,
                                     zc::mv(settings));
}

// =======================================================================================

namespace {

class ConcurrencyLimitingHttpClient final : public HttpClient {
public:
  ZC_DISALLOW_COPY_AND_MOVE(ConcurrencyLimitingHttpClient);
  ConcurrencyLimitingHttpClient(
      zc::HttpClient& inner, uint maxConcurrentRequests,
      zc::Function<void(uint runningCount, uint pendingCount)> countChangedCallback)
      : inner(inner),
        maxConcurrentRequests(maxConcurrentRequests),
        countChangedCallback(zc::mv(countChangedCallback)) {}

  ~ConcurrencyLimitingHttpClient() noexcept {
    // Crash in this case because otherwise we'll have UAF later on.
    ZC_ASSERT(concurrentRequests == 0,
              "ConcurrencyLimitingHttpClient getting destroyed when concurrent requests "
              "are still active");
  }

  Request request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                  zc::Maybe<uint64_t> expectedBodySize = zc::none) override {
    if (concurrentRequests < maxConcurrentRequests) {
      auto counter = ConnectionCounter(*this);
      auto request = inner.request(method, url, headers, expectedBodySize);
      fireCountChanged();
      auto promise = attachCounter(zc::mv(request.response), zc::mv(counter));
      return {zc::mv(request.body), zc::mv(promise)};
    }

    auto paf = zc::newPromiseAndFulfiller<ConnectionCounter>();
    auto urlCopy = zc::str(url);
    auto headersCopy = headers.clone();

    auto combined = paf.promise.then([this, method, urlCopy = zc::mv(urlCopy),
                                      headersCopy = zc::mv(headersCopy),
                                      expectedBodySize](ConnectionCounter&& counter) mutable {
      auto req = inner.request(method, urlCopy, headersCopy, expectedBodySize);
      return zc::tuple(zc::mv(req.body), attachCounter(zc::mv(req.response), zc::mv(counter)));
    });
    auto split = combined.split();
    pendingRequests.push(zc::mv(paf.fulfiller));
    fireCountChanged();
    return {newPromisedStream(zc::mv(zc::get<0>(split))), zc::mv(zc::get<1>(split))};
  }

  zc::Promise<WebSocketResponse> openWebSocket(zc::StringPtr url,
                                               const zc::HttpHeaders& headers) override {
    if (concurrentRequests < maxConcurrentRequests) {
      auto counter = ConnectionCounter(*this);
      auto response = inner.openWebSocket(url, headers);
      fireCountChanged();
      return attachCounter(zc::mv(response), zc::mv(counter));
    }

    auto paf = zc::newPromiseAndFulfiller<ConnectionCounter>();
    auto urlCopy = zc::str(url);
    auto headersCopy = headers.clone();

    auto promise =
        paf.promise.then([this, urlCopy = zc::mv(urlCopy),
                          headersCopy = zc::mv(headersCopy)](ConnectionCounter&& counter) mutable {
          return attachCounter(inner.openWebSocket(urlCopy, headersCopy), zc::mv(counter));
        });

    pendingRequests.push(zc::mv(paf.fulfiller));
    fireCountChanged();
    return zc::mv(promise);
  }

  ConnectRequest connect(zc::StringPtr host, const zc::HttpHeaders& headers,
                         HttpConnectSettings settings) override {
    if (concurrentRequests < maxConcurrentRequests) {
      auto counter = ConnectionCounter(*this);
      auto response = inner.connect(host, headers, settings);
      fireCountChanged();
      return attachCounter(zc::mv(response), zc::mv(counter));
    }

    auto paf = zc::newPromiseAndFulfiller<ConnectionCounter>();

    auto split = paf.promise
                     .then([this, host = zc::str(host), headers = headers.clone(),
                            settings](ConnectionCounter&& counter) mutable
                           -> zc::Tuple<zc::Promise<ConnectRequest::Status>,
                                        zc::Promise<zc::Own<zc::AsyncIoStream>>> {
                       auto request =
                           attachCounter(inner.connect(host, headers, settings), zc::mv(counter));
                       return zc::tuple(zc::mv(request.status), zc::mv(request.connection));
                     })
                     .split();

    pendingRequests.push(zc::mv(paf.fulfiller));
    fireCountChanged();

    return ConnectRequest{zc::mv(zc::get<0>(split)),
                          zc::newPromisedStream(zc::mv(zc::get<1>(split)))};
  }

private:
  struct ConnectionCounter;

  zc::HttpClient& inner;
  uint maxConcurrentRequests;
  uint concurrentRequests = 0;
  zc::Function<void(uint runningCount, uint pendingCount)> countChangedCallback;

  std::queue<zc::Own<zc::PromiseFulfiller<ConnectionCounter>>> pendingRequests;
  // TODO(someday): want maximum cap on queue size?

  struct ConnectionCounter final {
    ConnectionCounter(ConcurrencyLimitingHttpClient& client) : parent(&client) {
      ++parent->concurrentRequests;
    }
    ZC_DISALLOW_COPY(ConnectionCounter);
    ~ConnectionCounter() noexcept(false) {
      if (parent != nullptr) {
        --parent->concurrentRequests;
        parent->serviceQueue();
        parent->fireCountChanged();
      }
    }
    ConnectionCounter(ConnectionCounter&& other) : parent(other.parent) { other.parent = nullptr; }
    ConnectionCounter& operator=(ConnectionCounter&& other) {
      if (this != &other) {
        this->parent = other.parent;
        other.parent = nullptr;
      }
      return *this;
    }

    ConcurrencyLimitingHttpClient* parent;
  };

  void serviceQueue() {
    while (concurrentRequests < maxConcurrentRequests && !pendingRequests.empty()) {
      auto fulfiller = zc::mv(pendingRequests.front());
      pendingRequests.pop();
      // ConnectionCounter's destructor calls this function, so we can avoid unnecessary recursion
      // if we only create a ConnectionCounter when we find a waiting fulfiller.
      if (fulfiller->isWaiting()) { fulfiller->fulfill(ConnectionCounter(*this)); }
    }
  }

  void fireCountChanged() { countChangedCallback(concurrentRequests, pendingRequests.size()); }

  using WebSocketOrBody = zc::OneOf<zc::Own<zc::AsyncInputStream>, zc::Own<WebSocket>>;
  static WebSocketOrBody attachCounter(WebSocketOrBody&& webSocketOrBody,
                                       ConnectionCounter&& counter) {
    ZC_SWITCH_ONEOF(webSocketOrBody) {
      ZC_CASE_ONEOF(ws, zc::Own<WebSocket>) { return ws.attach(zc::mv(counter)); }
      ZC_CASE_ONEOF(body, zc::Own<zc::AsyncInputStream>) { return body.attach(zc::mv(counter)); }
    }
    ZC_UNREACHABLE;
  }

  static zc::Promise<WebSocketResponse> attachCounter(zc::Promise<WebSocketResponse>&& promise,
                                                      ConnectionCounter&& counter) {
    return promise.then([counter = zc::mv(counter)](WebSocketResponse&& response) mutable {
      return WebSocketResponse{response.statusCode, response.statusText, response.headers,
                               attachCounter(zc::mv(response.webSocketOrBody), zc::mv(counter))};
    });
  }

  static zc::Promise<Response> attachCounter(zc::Promise<Response>&& promise,
                                             ConnectionCounter&& counter) {
    return promise.then([counter = zc::mv(counter)](Response&& response) mutable {
      return Response{response.statusCode, response.statusText, response.headers,
                      response.body.attach(zc::mv(counter))};
    });
  }

  static ConnectRequest attachCounter(ConnectRequest&& request, ConnectionCounter&& counter) {
    // Notice here that we are only attaching the counter to the connection stream. In the case
    // where the connect tunnel request is rejected and the status promise resolves with an
    // errorBody, there is a possibility that the consuming code might drop the connection stream
    // and the counter while the error body stream is still be consumed. Technically speaking that
    // means we could potentially exceed our concurrency limit temporarily but we consider that
    // acceptable here since the error body is an exception path (plus not requiring that we
    // attach to the errorBody keeps ConnectionCounter from having to be a refcounted heap
    // allocation).
    request.connection = request.connection.attach(zc::mv(counter));
    return zc::mv(request);
  }
};

}  // namespace

zc::Own<HttpClient> newConcurrencyLimitingHttpClient(
    HttpClient& inner, uint maxConcurrentRequests,
    zc::Function<void(uint runningCount, uint pendingCount)> countChangedCallback) {
  return zc::heap<ConcurrencyLimitingHttpClient>(inner, maxConcurrentRequests,
                                                 zc::mv(countChangedCallback));
}

// =======================================================================================

namespace {

class HttpClientAdapter final : public HttpClient {
public:
  HttpClientAdapter(HttpService& service) : service(service) {}

  Request request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                  zc::Maybe<uint64_t> expectedBodySize = zc::none) override {
    // We have to clone the URL and headers because HttpService implementation are allowed to
    // assume that they remain valid until the service handler completes whereas HttpClient callers
    // are allowed to destroy them immediately after the call.
    auto urlCopy = zc::str(url);
    auto headersCopy = zc::heap(headers.clone());

    auto pipe = newOneWayPipe(expectedBodySize);

    // TODO(cleanup): The ownership relationships here are a mess. Can we do something better
    //   involving a PromiseAdapter, maybe?
    auto paf = zc::newPromiseAndFulfiller<Response>();
    auto responder = zc::refcounted<ResponseImpl>(method, zc::mv(paf.fulfiller));

    auto requestPaf = zc::newPromiseAndFulfiller<zc::Promise<void>>();
    responder->setPromise(zc::mv(requestPaf.promise));

    auto promise = service.request(method, urlCopy, *headersCopy, *pipe.in, *responder)
                       .attach(zc::mv(pipe.in), zc::mv(urlCopy), zc::mv(headersCopy));
    requestPaf.fulfiller->fulfill(zc::mv(promise));

    return {zc::mv(pipe.out), paf.promise.attach(zc::mv(responder))};
  }

  zc::Promise<WebSocketResponse> openWebSocket(zc::StringPtr url,
                                               const HttpHeaders& headers) override {
    // We have to clone the URL and headers because HttpService implementation are allowed to
    // assume that they remain valid until the service handler completes whereas HttpClient callers
    // are allowed to destroy them immediately after the call. Also we need to add
    // `Upgrade: websocket` so that headers.isWebSocket() returns true on the service side.
    auto urlCopy = zc::str(url);
    auto headersCopy = zc::heap(headers.clone());
    headersCopy->set(HttpHeaderId::UPGRADE, "websocket");
    ZC_DASSERT(headersCopy->isWebSocket());

    auto paf = zc::newPromiseAndFulfiller<WebSocketResponse>();
    auto responder = zc::refcounted<WebSocketResponseImpl>(zc::mv(paf.fulfiller));

    auto requestPaf = zc::newPromiseAndFulfiller<zc::Promise<void>>();
    responder->setPromise(zc::mv(requestPaf.promise));

    auto in = zc::heap<zc::NullStream>();
    auto promise = service.request(HttpMethod::GET, urlCopy, *headersCopy, *in, *responder)
                       .attach(zc::mv(in), zc::mv(urlCopy), zc::mv(headersCopy));
    requestPaf.fulfiller->fulfill(zc::mv(promise));

    return paf.promise.attach(zc::mv(responder));
  }

  ConnectRequest connect(zc::StringPtr host, const HttpHeaders& headers,
                         HttpConnectSettings settings) override {
    // We have to clone the host and the headers because HttpServer implementation are allowed to
    // assusme that they remain valid until the service handler completes whereas HttpClient callers
    // are allowed to destroy them immediately after the call.
    auto hostCopy = zc::str(host);
    auto headersCopy = zc::heap(headers.clone());

    // 1. Create a new TwoWayPipe, one will be returned with the ConnectRequest,
    //    the other will be held by the ConnectResponseImpl.
    auto pipe = zc::newTwoWayPipe();

    // 2. Create a promise/fulfiller pair for the status. The promise will be
    //    returned with the ConnectResponse, the fulfiller will be held by the
    //    ConnectResponseImpl.
    auto paf = zc::newPromiseAndFulfiller<ConnectRequest::Status>();

    // 3. Create the ConnectResponseImpl
    auto response =
        zc::refcounted<ConnectResponseImpl>(zc::mv(paf.fulfiller), zc::mv(pipe.ends[0]));

    // 5. Call service.connect, passing in the tunnel.
    //    The call to tunnel->getConnectStream() returns a guarded stream that will buffer
    //    writes until the status is indicated by calling accept/reject.
    auto connectStream = response->getConnectStream();
    auto promise =
        service.connect(hostCopy, *headersCopy, *connectStream, *response, settings)
            .eagerlyEvaluate([response = zc::mv(response), host = zc::mv(hostCopy),
                              headers = zc::mv(headersCopy),
                              connectStream = zc::mv(connectStream)](zc::Exception&& ex) mutable {
              // A few things need to happen here.
              //   1. We'll log the exception.
              //   2. We'll break the pipe.
              //   3. We'll reject the status promise if it is still pending.
              //
              // We'll do all of this within the ConnectResponseImpl, however, since it
              // maintains the state necessary here.
              response->handleException(zc::mv(ex), zc::mv(connectStream));
            });

    // TODO(bug): There's a challenge with attaching the service.connect promise to the
    // connection stream below in that the client will likely drop the connection as soon
    // as it reads EOF, but the promise representing the service connect() call may still
    // be running and want to do some cleanup after it has sent EOF. That cleanup will be
    // canceled. For regular HTTP calls, DelayedEofInputStream was created to address this
    // exact issue but with connect() being bidirectional it's rather more difficult. We
    // want a delay similar to what DelayedEofInputStream adds but only when both directions
    // have been closed. That currently is not possible until we have an alternative to
    // shutdownWrite() that returns a Promise (e.g. Promise<void> end()). For now, we can
    // live with the current limitation.
    return ConnectRequest{
        zc::mv(paf.promise),
        pipe.ends[1].attach(zc::mv(promise)),
    };
  }

private:
  HttpService& service;

  class DelayedEofInputStream final : public zc::AsyncInputStream {
    // An AsyncInputStream wrapper that, when it reaches EOF, delays the final read until some
    // promise completes.

  public:
    DelayedEofInputStream(zc::Own<zc::AsyncInputStream> inner, zc::Promise<void> completionTask)
        : inner(zc::mv(inner)), completionTask(zc::mv(completionTask)) {}

    zc::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
      return wrap(minBytes, inner->tryRead(buffer, minBytes, maxBytes));
    }

    zc::Maybe<uint64_t> tryGetLength() override { return inner->tryGetLength(); }

    zc::Promise<uint64_t> pumpTo(zc::AsyncOutputStream& output, uint64_t amount) override {
      return wrap(amount, inner->pumpTo(output, amount));
    }

  private:
    zc::Own<zc::AsyncInputStream> inner;
    zc::Maybe<zc::Promise<void>> completionTask;

    template <typename T>
    zc::Promise<T> wrap(T requested, zc::Promise<T> innerPromise) {
      return innerPromise.then(
          [this, requested](T actual) -> zc::Promise<T> {
            if (actual < requested) {
              // Must have reached EOF.
              ZC_IF_SOME(t, completionTask) {
                // Delay until completion.
                auto result = t.then([actual]() { return actual; });
                completionTask = zc::none;
                return result;
              }
              else {
                // Must have called tryRead() again after we already signaled EOF. Fine.
                return actual;
              }
            } else {
              return actual;
            }
          },
          [this](zc::Exception&& e) -> zc::Promise<T> {
            // The stream threw an exception, but this exception is almost certainly just
            // complaining that the other end of the stream was dropped. In all likelihood, the
            // HttpService request() call itself will throw a much more interesting error -- we'd
            // rather propagate that one, if so.
            ZC_IF_SOME(t, completionTask) {
              auto result = t.then([e = zc::mv(e)]() mutable -> zc::Promise<T> {
                // Looks like the service didn't throw. I guess we should propagate the stream error
                // after all.
                return zc::mv(e);
              });
              completionTask = zc::none;
              return result;
            }
            else {
              // Must have called tryRead() again after we already signaled EOF or threw. Fine.
              return zc::mv(e);
            }
          });
    }
  };

  class ResponseImpl final : public HttpService::Response, public zc::Refcounted {
  public:
    ResponseImpl(zc::HttpMethod method,
                 zc::Own<zc::PromiseFulfiller<HttpClient::Response>> fulfiller)
        : method(method), fulfiller(zc::mv(fulfiller)) {}

    void setPromise(zc::Promise<void> promise) {
      task = promise.eagerlyEvaluate([this](zc::Exception&& exception) {
        if (fulfiller->isWaiting()) {
          fulfiller->reject(zc::mv(exception));
        } else {
          // We need to cause the response stream's read() to throw this, so we should propagate it.
          zc::throwRecoverableException(zc::mv(exception));
        }
      });
    }

    zc::Own<zc::AsyncOutputStream> send(uint statusCode, zc::StringPtr statusText,
                                        const HttpHeaders& headers,
                                        zc::Maybe<uint64_t> expectedBodySize = zc::none) override {
      // The caller of HttpClient is allowed to assume that the statusText and headers remain
      // valid until the body stream is dropped, but the HttpService implementation is allowed to
      // send values that are only valid until send() returns, so we have to copy.
      auto statusTextCopy = zc::str(statusText);
      auto headersCopy = zc::heap(headers.clone());

      if (method == zc::HttpMethod::HEAD || expectedBodySize.orDefault(1) == 0) {
        // We're not expecting any body. We need to delay reporting completion to the client until
        // the server side has actually returned from the service method, otherwise we may
        // prematurely cancel it.

        task = task.then([this, statusCode, statusTextCopy = zc::mv(statusTextCopy),
                          headersCopy = zc::mv(headersCopy), expectedBodySize]() mutable {
                     fulfiller->fulfill({statusCode, statusTextCopy, headersCopy.get(),
                                         zc::heap<HeadResponseStream>(expectedBodySize)
                                             .attach(zc::mv(statusTextCopy), zc::mv(headersCopy))});
                   })
                   .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });
        return zc::heap<zc::NullStream>();
      } else {
        auto pipe = newOneWayPipe(expectedBodySize);

        // Wrap the stream in a wrapper that delays the last read (the one that signals EOF) until
        // the service's request promise has finished.
        auto wrapper =
            zc::heap<DelayedEofInputStream>(zc::mv(pipe.in), task.attach(zc::addRef(*this)));

        fulfiller->fulfill({statusCode, statusTextCopy, headersCopy.get(),
                            wrapper.attach(zc::mv(statusTextCopy), zc::mv(headersCopy))});
        return zc::mv(pipe.out);
      }
    }

    zc::Own<WebSocket> acceptWebSocket(const HttpHeaders& headers) override {
      ZC_FAIL_REQUIRE("a WebSocket was not requested");
    }

  private:
    zc::HttpMethod method;
    zc::Own<zc::PromiseFulfiller<HttpClient::Response>> fulfiller;
    zc::Promise<void> task = nullptr;
  };

  class DelayedCloseWebSocket final : public WebSocket {
    // A WebSocket wrapper that, when it reaches Close (in both directions), delays the final close
    // operation until some promise completes.

  public:
    DelayedCloseWebSocket(zc::Own<zc::WebSocket> inner, zc::Promise<void> completionTask)
        : inner(zc::mv(inner)), completionTask(zc::mv(completionTask)) {}

    zc::Promise<void> send(zc::ArrayPtr<const byte> message) override {
      return inner->send(message);
    }
    zc::Promise<void> send(zc::ArrayPtr<const char> message) override {
      return inner->send(message);
    }
    zc::Promise<void> close(uint16_t code, zc::StringPtr reason) override {
      co_await inner->close(code, reason);
      co_await afterSendClosed();
    }
    void disconnect() override { inner->disconnect(); }
    void abort() override {
      // Don't need to worry about completion task in this case -- cancelling it is reasonable.
      inner->abort();
    }
    zc::Promise<void> whenAborted() override { return inner->whenAborted(); }
    zc::Promise<Message> receive(size_t maxSize) override {
      auto message = co_await inner->receive(maxSize);
      if (message.is<WebSocket::Close>()) { co_await afterReceiveClosed(); }
      co_return message;
    }
    zc::Promise<void> pumpTo(WebSocket& other) override {
      co_await inner->pumpTo(other);
      co_await afterReceiveClosed();
    }
    zc::Maybe<zc::Promise<void>> tryPumpFrom(WebSocket& other) override {
      return other.pumpTo(*inner).then([this]() { return afterSendClosed(); });
    }

    uint64_t sentByteCount() override { return inner->sentByteCount(); }
    uint64_t receivedByteCount() override { return inner->receivedByteCount(); }

    zc::Maybe<zc::String> getPreferredExtensions(ExtensionsContext ctx) override {
      return inner->getPreferredExtensions(ctx);
    };

  private:
    zc::Own<zc::WebSocket> inner;
    zc::Maybe<zc::Promise<void>> completionTask;

    bool sentClose = false;
    bool receivedClose = false;

    zc::Promise<void> afterSendClosed() {
      sentClose = true;
      if (receivedClose) {
        ZC_IF_SOME(t, completionTask) {
          auto result = zc::mv(t);
          completionTask = zc::none;
          co_await result;
        }
      }
    }

    zc::Promise<void> afterReceiveClosed() {
      receivedClose = true;
      if (sentClose) {
        ZC_IF_SOME(t, completionTask) {
          auto result = zc::mv(t);
          completionTask = zc::none;
          co_await result;
        }
      }
    }
  };

  class WebSocketResponseImpl final : public HttpService::Response, public zc::Refcounted {
  public:
    WebSocketResponseImpl(zc::Own<zc::PromiseFulfiller<HttpClient::WebSocketResponse>> fulfiller)
        : fulfiller(zc::mv(fulfiller)) {}

    void setPromise(zc::Promise<void> promise) {
      task = promise.eagerlyEvaluate([this](zc::Exception&& exception) {
        if (fulfiller->isWaiting()) {
          fulfiller->reject(zc::mv(exception));
        } else {
          // We need to cause the client-side WebSocket to throw on close, so propagate the
          // exception.
          zc::throwRecoverableException(zc::mv(exception));
        }
      });
    }

    zc::Own<zc::AsyncOutputStream> send(uint statusCode, zc::StringPtr statusText,
                                        const HttpHeaders& headers,
                                        zc::Maybe<uint64_t> expectedBodySize = zc::none) override {
      // The caller of HttpClient is allowed to assume that the statusText and headers remain
      // valid until the body stream is dropped, but the HttpService implementation is allowed to
      // send values that are only valid until send() returns, so we have to copy.
      auto statusTextCopy = zc::str(statusText);
      auto headersCopy = zc::heap(headers.clone());

      if (expectedBodySize.orDefault(1) == 0) {
        // We're not expecting any body. We need to delay reporting completion to the client until
        // the server side has actually returned from the service method, otherwise we may
        // prematurely cancel it.

        task = task.then([this, statusCode, statusTextCopy = zc::mv(statusTextCopy),
                          headersCopy = zc::mv(headersCopy), expectedBodySize]() mutable {
                     fulfiller->fulfill(
                         {statusCode, statusTextCopy, headersCopy.get(),
                          zc::Own<AsyncInputStream>(
                              zc::heap<HeadResponseStream>(expectedBodySize)
                                  .attach(zc::mv(statusTextCopy), zc::mv(headersCopy)))});
                   })
                   .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });
        return zc::heap<zc::NullStream>();
      } else {
        auto pipe = newOneWayPipe(expectedBodySize);

        // Wrap the stream in a wrapper that delays the last read (the one that signals EOF) until
        // the service's request promise has finished.
        zc::Own<AsyncInputStream> wrapper =
            zc::heap<DelayedEofInputStream>(zc::mv(pipe.in), task.attach(zc::addRef(*this)));

        fulfiller->fulfill({statusCode, statusTextCopy, headersCopy.get(),
                            wrapper.attach(zc::mv(statusTextCopy), zc::mv(headersCopy))});
        return zc::mv(pipe.out);
      }
    }

    zc::Own<WebSocket> acceptWebSocket(const HttpHeaders& headers) override {
      // The caller of HttpClient is allowed to assume that the headers remain valid until the body
      // stream is dropped, but the HttpService implementation is allowed to send headers that are
      // only valid until acceptWebSocket() returns, so we have to copy.
      auto headersCopy = zc::heap(headers.clone());

      auto pipe = newWebSocketPipe();

      // Wrap the client-side WebSocket in a wrapper that delays clean close of the WebSocket until
      // the service's request promise has finished.
      zc::Own<WebSocket> wrapper =
          zc::heap<DelayedCloseWebSocket>(zc::mv(pipe.ends[0]), task.attach(zc::addRef(*this)));
      fulfiller->fulfill(
          {101, "Switching Protocols", headersCopy.get(), wrapper.attach(zc::mv(headersCopy))});
      return zc::mv(pipe.ends[1]);
    }

  private:
    zc::Own<zc::PromiseFulfiller<HttpClient::WebSocketResponse>> fulfiller;
    zc::Promise<void> task = nullptr;
  };

  class ConnectResponseImpl final : public HttpService::ConnectResponse, public zc::Refcounted {
  public:
    ConnectResponseImpl(zc::Own<zc::PromiseFulfiller<HttpClient::ConnectRequest::Status>> fulfiller,
                        zc::Own<zc::AsyncIoStream> stream)
        : fulfiller(zc::mv(fulfiller)),
          streamAndFulfiller(initStreamsAndFulfiller(zc::mv(stream))) {}

    ~ConnectResponseImpl() noexcept(false) {
      if (fulfiller->isWaiting() || streamAndFulfiller.fulfiller->isWaiting()) {
        auto ex = ZC_EXCEPTION(
            FAILED, "service's connect() implementation never called accept() nor reject()");
        if (fulfiller->isWaiting()) { fulfiller->reject(zc::cp(ex)); }
        if (streamAndFulfiller.fulfiller->isWaiting()) {
          streamAndFulfiller.fulfiller->reject(zc::mv(ex));
        }
      }
    }

    void accept(uint statusCode, zc::StringPtr statusText, const HttpHeaders& headers) override {
      ZC_REQUIRE(statusCode >= 200 && statusCode < 300, "the statusCode must be 2xx for accept");
      respond(statusCode, statusText, headers);
    }

    zc::Own<zc::AsyncOutputStream> reject(
        uint statusCode, zc::StringPtr statusText, const HttpHeaders& headers,
        zc::Maybe<uint64_t> expectedBodySize = zc::none) override {
      ZC_REQUIRE(statusCode < 200 || statusCode >= 300,
                 "the statusCode must not be 2xx for reject.");
      auto pipe = zc::newOneWayPipe();
      respond(statusCode, statusText, headers, zc::mv(pipe.in));
      return zc::mv(pipe.out);
    }

  private:
    struct StreamsAndFulfiller {
      // guarded is the wrapped/guarded stream that wraps a reference to
      // the underlying stream but blocks reads until the connection is accepted
      // or rejected.
      // This will be handed off when getConnectStream() is called.
      // The fulfiller is used to resolve the guard for the second stream. This will
      // be fulfilled or rejected when accept/reject is called.
      zc::Own<zc::AsyncIoStream> guarded;
      zc::Own<zc::PromiseFulfiller<void>> fulfiller;
    };

    zc::Own<zc::PromiseFulfiller<HttpClient::ConnectRequest::Status>> fulfiller;
    StreamsAndFulfiller streamAndFulfiller;
    bool connectStreamDetached = false;

    StreamsAndFulfiller initStreamsAndFulfiller(zc::Own<zc::AsyncIoStream> stream) {
      auto paf = zc::newPromiseAndFulfiller<void>();
      auto guarded = zc::heap<AsyncIoStreamWithGuards>(
          zc::mv(stream), zc::Maybe<HttpInputStreamImpl::ReleasedBuffer>(zc::none),
          zc::mv(paf.promise));
      return StreamsAndFulfiller{zc::mv(guarded), zc::mv(paf.fulfiller)};
    }

    void handleException(zc::Exception&& ex, zc::Own<zc::AsyncIoStream> connectStream) {
      // Reject the status promise if it is still pending...
      if (fulfiller->isWaiting()) { fulfiller->reject(zc::cp(ex)); }
      if (streamAndFulfiller.fulfiller->isWaiting()) {
        // If the guard hasn't yet ben released, we can fail the pending reads by
        // rejecting the fulfiller here.
        streamAndFulfiller.fulfiller->reject(zc::mv(ex));
      } else {
        // The guard has already been released at this point.
        // TODO(connect): How to properly propagate the actual exception to the
        // connect stream? Here we "simply" shut it down.
        connectStream->abortRead();
        connectStream->shutdownWrite();
      }
    }

    zc::Own<zc::AsyncIoStream> getConnectStream() {
      ZC_ASSERT(!connectStreamDetached, "the connect stream was already detached");
      connectStreamDetached = true;
      return streamAndFulfiller.guarded.attach(zc::addRef(*this));
    }

    void respond(uint statusCode, zc::StringPtr statusText, const HttpHeaders& headers,
                 zc::Maybe<zc::Own<zc::AsyncInputStream>> errorBody = zc::none) {
      if (errorBody == zc::none) {
        streamAndFulfiller.fulfiller->fulfill();
      } else {
        streamAndFulfiller.fulfiller->reject(
            ZC_EXCEPTION(DISCONNECTED, "the connect request was rejected"));
      }
      fulfiller->fulfill(HttpClient::ConnectRequest::Status(
          statusCode, zc::str(statusText), zc::heap(headers.clone()), zc::mv(errorBody)));
    }

    friend class HttpClientAdapter;
  };
};

}  // namespace

zc::Own<HttpClient> newHttpClient(HttpService& service) {
  return zc::heap<HttpClientAdapter>(service);
}

// =======================================================================================

namespace {

class HttpServiceAdapter final : public HttpService {
public:
  HttpServiceAdapter(HttpClient& client) : client(client) {}

  zc::Promise<void> request(HttpMethod method, zc::StringPtr url, const HttpHeaders& headers,
                            zc::AsyncInputStream& requestBody, Response& response) override {
    if (!headers.isWebSocket()) {
      auto innerReq = client.request(method, url, headers, requestBody.tryGetLength());

      auto promises = zc::heapArrayBuilder<zc::Promise<void>>(2);
      promises.add(requestBody.pumpTo(*innerReq.body)
                       .ignoreResult()
                       .attach(zc::mv(innerReq.body))
                       .eagerlyEvaluate(nullptr));

      promises.add(innerReq.response.then([&response](HttpClient::Response&& innerResponse) {
        auto out = response.send(innerResponse.statusCode, innerResponse.statusText,
                                 *innerResponse.headers, innerResponse.body->tryGetLength());
        auto promise = innerResponse.body->pumpTo(*out);
        return promise.ignoreResult().attach(zc::mv(out), zc::mv(innerResponse.body));
      }));

      return zc::joinPromisesFailFast(promises.finish());
    } else {
      return client.openWebSocket(url, headers)
          .then([&response](HttpClient::WebSocketResponse&& innerResponse) -> zc::Promise<void> {
            ZC_SWITCH_ONEOF(innerResponse.webSocketOrBody) {
              ZC_CASE_ONEOF(ws, zc::Own<WebSocket>) {
                auto ws2 = response.acceptWebSocket(*innerResponse.headers);
                auto promises = zc::heapArrayBuilder<zc::Promise<void>>(2);
                promises.add(ws->pumpTo(*ws2));
                promises.add(ws2->pumpTo(*ws));
                return zc::joinPromisesFailFast(promises.finish()).attach(zc::mv(ws), zc::mv(ws2));
              }
              ZC_CASE_ONEOF(body, zc::Own<zc::AsyncInputStream>) {
                auto out = response.send(innerResponse.statusCode, innerResponse.statusText,
                                         *innerResponse.headers, body->tryGetLength());
                auto promise = body->pumpTo(*out);
                return promise.ignoreResult().attach(zc::mv(out), zc::mv(body));
              }
            }
            ZC_UNREACHABLE;
          });
    }
  }

  zc::Promise<void> connect(zc::StringPtr host, const HttpHeaders& headers,
                            zc::AsyncIoStream& connection, ConnectResponse& response,
                            HttpConnectSettings settings) override {
    ZC_REQUIRE(!headers.isWebSocket(), "WebSocket upgrade headers are not permitted in a connect.");

    auto request = client.connect(host, headers, settings);

    // This operates optimistically. In order to support pipelining, we connect the
    // input and outputs streams immediately, even if we're not yet certain that the
    // tunnel can actually be established.
    auto promises = zc::heapArrayBuilder<zc::Promise<void>>(2);

    // For the inbound pipe (from the clients stream to the passed in stream)
    // We want to guard reads pending the acceptance of the tunnel. If the
    // tunnel is not accepted, the guard will be rejected, causing pending
    // reads to fail.
    auto paf = zc::newPromiseAndFulfiller<zc::Maybe<HttpInputStreamImpl::ReleasedBuffer>>();
    auto io = zc::heap<AsyncIoStreamWithGuards>(zc::mv(request.connection),
                                                zc::mv(paf.promise) /* read guard */,
                                                zc::READY_NOW /* write guard */);

    // Writing from connection to io is unguarded and allowed immediately.
    promises.add(connection.pumpTo(*io).then([&io = *io](uint64_t size) { io.shutdownWrite(); }));

    promises.add(
        io->pumpTo(connection).then([&connection](uint64_t size) { connection.shutdownWrite(); }));

    auto pumpPromise = zc::joinPromisesFailFast(promises.finish());

    return request.status
        .then([&response, &connection, fulfiller = zc::mv(paf.fulfiller),
               pumpPromise = zc::mv(pumpPromise)](
                  HttpClient::ConnectRequest::Status status) mutable -> zc::Promise<void> {
          if (status.statusCode >= 200 && status.statusCode < 300) {
            // Release the read guard!
            fulfiller->fulfill(zc::Maybe<HttpInputStreamImpl::ReleasedBuffer>(zc::none));
            response.accept(status.statusCode, status.statusText, *status.headers);
            return zc::mv(pumpPromise);
          } else {
            // If the connect request is rejected, we want to shutdown the tunnel
            // and pipeline the status.errorBody to the AsyncOutputStream returned by
            // reject if it exists.
            pumpPromise = nullptr;
            connection.shutdownWrite();
            fulfiller->reject(ZC_EXCEPTION(DISCONNECTED, "the connect request was rejected"));
            ZC_IF_SOME(errorBody, status.errorBody) {
              auto out = response.reject(status.statusCode, status.statusText, *status.headers,
                                         errorBody->tryGetLength());
              return errorBody->pumpTo(*out)
                  .then([](uint64_t) -> zc::Promise<void> { return zc::READY_NOW; })
                  .attach(zc::mv(out), zc::mv(errorBody));
            }
            else {
              response.reject(status.statusCode, status.statusText, *status.headers, (uint64_t)0);
              return zc::READY_NOW;
            }
          }
        })
        .attach(zc::mv(io));
  }

private:
  HttpClient& client;
};

}  // namespace

zc::Own<HttpService> newHttpService(HttpClient& client) {
  return zc::heap<HttpServiceAdapter>(client);
}

// =======================================================================================

zc::Promise<void> HttpService::Response::sendError(uint statusCode, zc::StringPtr statusText,
                                                   const HttpHeaders& headers) {
  auto stream = send(statusCode, statusText, headers, statusText.size());
  auto promise = stream->write(statusText.asBytes());
  return promise.attach(zc::mv(stream));
}

zc::Promise<void> HttpService::Response::sendError(uint statusCode, zc::StringPtr statusText,
                                                   const HttpHeaderTable& headerTable) {
  return sendError(statusCode, statusText, HttpHeaders(headerTable));
}

zc::Promise<void> HttpService::connect(zc::StringPtr host, const HttpHeaders& headers,
                                       zc::AsyncIoStream& connection, ConnectResponse& response,
                                       zc::HttpConnectSettings settings) {
  ZC_UNIMPLEMENTED("CONNECT is not implemented by this HttpService");
}

class HttpServer::Connection final : private HttpService::Response,
                                     private HttpService::ConnectResponse,
                                     private HttpServerErrorHandler {
public:
  Connection(HttpServer& server, zc::AsyncIoStream& stream, SuspendableHttpServiceFactory factory,
             zc::Maybe<SuspendedRequest> suspendedRequest, bool wantCleanDrain)
      : server(server),
        stream(stream),
        factory(zc::mv(factory)),
        httpInput(makeHttpInput(stream, server.requestHeaderTable, zc::mv(suspendedRequest))),
        httpOutput(stream),
        wantCleanDrain(wantCleanDrain) {
    ++server.connectionCount;
  }
  ~Connection() noexcept(false) {
    if (--server.connectionCount == 0) {
      ZC_IF_SOME(f, server.zeroConnectionsFulfiller) { f->fulfill(); }
    }
  }

public:
  // Each iteration of the loop decides if it wants to continue, or break the loop and return.
  enum LoopResult {
    CONTINUE_LOOP,
    BREAK_LOOP_CONN_OK,
    BREAK_LOOP_CONN_ERR,
  };

  zc::Promise<bool> startLoop() {
    auto result = co_await startLoopImpl();
    ZC_ASSERT(result != CONTINUE_LOOP);
    co_return result == BREAK_LOOP_CONN_OK ? true : false;
  }

  zc::Promise<LoopResult> startLoopImpl() {
    return loop().catch_([this](zc::Exception&& e) {
      // Exception; report 5xx.

      ZC_IF_SOME(p, webSocketError) {
        // sendWebSocketError() was called. Finish sending and close the connection. Don't log
        // the exception because it's probably a side-effect of this.
        auto promise = zc::mv(p);
        webSocketError = zc::none;
        return zc::mv(promise);
      }

      ZC_IF_SOME(p, tunnelRejected) {
        // reject() was called to reject a CONNECT request. Finish sending and close the connection.
        // Don't log the exception because it's probably a side-effect of this.
        auto promise = zc::mv(p);
        tunnelRejected = zc::none;
        return zc::mv(promise);
      }

      return sendError(zc::mv(e));
    });
  }

  SuspendedRequest suspend(SuspendableRequest& suspendable) {
    ZC_REQUIRE(httpInput.canSuspend(),
               "suspend() may only be called before the request body is consumed");
    ZC_DEFER(suspended = true);
    auto released = httpInput.releaseBuffer();
    return {
        zc::mv(released.buffer),
        released.leftover,
        suspendable.method,
        suspendable.url,
        suspendable.headers.cloneShallow(),
    };
  }

private:
  HttpServer& server;
  zc::AsyncIoStream& stream;

  SuspendableHttpServiceFactory factory;
  // Creates a new zc::Own<HttpService> for each request we handle on this connection.

  HttpInputStreamImpl httpInput;
  HttpOutputStream httpOutput;
  zc::Maybe<zc::OneOf<HttpMethod, HttpConnectMethod>> currentMethod;
  bool timedOut = false;
  bool closed = false;
  bool upgraded = false;
  bool webSocketOrConnectClosed = false;
  bool closeAfterSend = false;  // True if send() should set Connection: close.
  bool wantCleanDrain = false;
  bool suspended = false;
  zc::Maybe<zc::Promise<LoopResult>> webSocketError;
  zc::Maybe<zc::Promise<LoopResult>> tunnelRejected;
  zc::Maybe<zc::Own<zc::PromiseFulfiller<void>>> tunnelWriteGuard;

  static HttpInputStreamImpl makeHttpInput(zc::AsyncIoStream& stream,
                                           const zc::HttpHeaderTable& table,
                                           zc::Maybe<SuspendedRequest> suspendedRequest) {
    // Constructor helper function to create our HttpInputStreamImpl.

    ZC_IF_SOME(sr, suspendedRequest) {
      return HttpInputStreamImpl(stream, sr.buffer.releaseAsChars(), sr.leftover.asChars(),
                                 sr.method, sr.url, zc::mv(sr.headers));
    }
    return HttpInputStreamImpl(stream, table);
  }

  zc::Promise<LoopResult> loop() {
    bool firstRequest = true;

    while (true) {
      if (!firstRequest && server.draining && httpInput.isCleanDrain()) {
        // Don't call awaitNextMessage() in this case because that will initiate a read() which will
        // immediately be canceled, losing data.
        co_return BREAK_LOOP_CONN_OK;
      }

      auto firstByte = httpInput.awaitNextMessage();

      if (!firstRequest) {
        // For requests after the first, require that the first byte arrive before the pipeline
        // timeout, otherwise treat it like the connection was simply closed.
        auto timeoutPromise = server.timer.afterDelay(server.settings.pipelineTimeout);

        if (httpInput.isCleanDrain()) {
          // If we haven't buffered any data, then we can safely drain here, so allow the wait to
          // be canceled by the onDrain promise.
          auto cleanDrainPromise = server.onDrain.addBranch().then([this]() -> zc::Promise<void> {
            // This is a little tricky... drain() has been called, BUT we could have read some data
            // into the buffer in the meantime, and we don't want to lose that. If any data has
            // arrived, then we have no choice but to read the rest of the request and respond to
            // it.
            if (!httpInput.isCleanDrain()) { return zc::NEVER_DONE; }

            // OK... As far as we know, no data has arrived in the buffer. However, unfortunately,
            // we don't *really* know that, because read() is asynchronous. It may have already
            // delivered some bytes, but we just haven't received the notification yet, because it's
            // still queued on the event loop. As a horrible hack, we use evalLast(), so that any
            // such pending notifications get a chance to be delivered.
            // TODO(someday): Does this actually work on Windows, where the notification could also
            //   be queued on the IOCP?
            return zc::evalLast([this]() -> zc::Promise<void> {
              if (httpInput.isCleanDrain()) {
                return zc::READY_NOW;
              } else {
                return zc::NEVER_DONE;
              }
            });
          });
          timeoutPromise = timeoutPromise.exclusiveJoin(zc::mv(cleanDrainPromise));
        }

        firstByte = firstByte.exclusiveJoin(timeoutPromise.then([this]() -> bool {
          timedOut = true;
          return false;
        }));
      }

      auto receivedHeaders = firstByte.then(
          [this,
           firstRequest](bool hasData) -> zc::Promise<HttpHeaders::RequestConnectOrProtocolError> {
            if (hasData) {
              auto readHeaders = httpInput.readRequestHeaders();
              if (!firstRequest) {
                // On requests other than the first, the header timeout starts ticking when we
                // receive the first byte of a pipeline response.
                readHeaders = readHeaders.exclusiveJoin(
                    server.timer.afterDelay(server.settings.headerTimeout)
                        .then([this]() -> HttpHeaders::RequestConnectOrProtocolError {
                          timedOut = true;
                          return HttpHeaders::ProtocolError{
                              408, "Request Timeout", "Timed out waiting for next request headers.",
                              nullptr};
                        }));
              }
              return zc::mv(readHeaders);
            } else {
              // Client closed connection or pipeline timed out with no bytes received. This is not
              // an error, so don't report one.
              this->closed = true;
              return HttpHeaders::RequestConnectOrProtocolError(
                  HttpHeaders::ProtocolError{408, "Request Timeout",
                                             "Client closed connection or connection timeout "
                                             "while waiting for request headers.",
                                             nullptr});
            }
          });

      if (firstRequest) {
        // On the first request, the header timeout starts ticking immediately upon request opening.
        // NOTE: Since we assume that the client wouldn't have formed a connection if they did not
        //   intend to send a request, we immediately treat this connection as having an active
        //   request, i.e. we do NOT cancel it if drain() is called.
        auto timeoutPromise = server.timer.afterDelay(server.settings.headerTimeout)
                                  .then([this]() -> HttpHeaders::RequestConnectOrProtocolError {
                                    timedOut = true;
                                    return HttpHeaders::ProtocolError{
                                        408, "Request Timeout",
                                        "Timed out waiting for initial request headers.", nullptr};
                                  });
        receivedHeaders = receivedHeaders.exclusiveJoin(zc::mv(timeoutPromise));
      }

      auto requestOrProtocolError = co_await receivedHeaders;
      auto loopResult = co_await onHeaders(zc::mv(requestOrProtocolError));

      switch (loopResult) {
        case BREAK_LOOP_CONN_ERR:
        case BREAK_LOOP_CONN_OK:
          co_return loopResult;
        case CONTINUE_LOOP: {
          firstRequest = false;
        }
      }
    }
  }

  zc::Promise<LoopResult> onHeaders(
      HttpHeaders::RequestConnectOrProtocolError&& requestOrProtocolError) {
    if (timedOut) {
      // Client took too long to send anything, so we're going to close the connection. In
      // theory, we should send back an HTTP 408 error -- it is designed exactly for this
      // purpose. Alas, in practice, Google Chrome does not have any special handling for 408
      // errors -- it will assume the error is a response to the next request it tries to send,
      // and will happily serve the error to the user. OTOH, if we simply close the connection,
      // Chrome does the "right thing", apparently. (Though I'm not sure what happens if a
      // request is in-flight when we close... if it's a GET, the browser should retry. But if
      // it's a POST, retrying may be dangerous. This is why 408 exists -- it unambiguously
      // tells the client that it should retry.)
      //
      // Also note that if we ever decide to send 408 again, we might want to send some other
      // error in the case that the server is draining, which also sets timedOut = true; see
      // above.

      co_await httpOutput.flush();
      co_return (server.draining && httpInput.isCleanDrain()) ? BREAK_LOOP_CONN_OK
                                                              : BREAK_LOOP_CONN_ERR;
    }

    if (closed) {
      // Client closed connection. Close our end too.
      co_await httpOutput.flush();
      co_return BREAK_LOOP_CONN_ERR;
    }

    ZC_SWITCH_ONEOF(requestOrProtocolError) {
      ZC_CASE_ONEOF(request, HttpHeaders::ConnectRequest) { co_return co_await onConnect(request); }
      ZC_CASE_ONEOF(request, HttpHeaders::Request) { co_return co_await onRequest(request); }
      ZC_CASE_ONEOF(protocolError, HttpHeaders::ProtocolError) {
        // Bad request.

        auto needClientGrace = protocolError.statusCode == 431;
        if (needClientGrace) {
          // We're going to reply with an error and close the connection.
          // The client might not be able to read the error back. Read some data and wait
          // a bit to give client a chance to finish writing.

          auto dummy = zc::heap<HttpDiscardingEntityWriter>();
          auto lengthGrace =
              zc::evalNow([&]() {
                return httpInput.discard(*dummy, server.settings.canceledUploadGraceBytes);
              }).catch_([](zc::Exception&& e) -> void {
                }).attach(zc::mv(dummy));
          auto timeGrace = server.timer.afterDelay(server.settings.canceledUploadGracePeriod);
          co_await lengthGrace.exclusiveJoin(zc::mv(timeGrace));
        }

        // sendError() uses Response::send(), which requires that we have a currentMethod, but we
        // never read one. GET seems like the correct choice here.
        currentMethod = HttpMethod::GET;
        co_return co_await sendError(zc::mv(protocolError));
      }
    }

    ZC_UNREACHABLE;
  }

  zc::Promise<LoopResult> onConnect(HttpHeaders::ConnectRequest& request) {
    auto& headers = httpInput.getHeaders();

    currentMethod = HttpConnectMethod();

    // The HTTP specification says that CONNECT requests have no meaningful payload
    // but stops short of saying that CONNECT *cannot* have a payload. Implementations
    // can choose to either accept payloads or reject them. We choose to reject it.
    // Specifically, if there are Content-Length or Transfer-Encoding headers in the
    // request headers, we'll automatically reject the CONNECT request.
    //
    // The key implication here is that any data that immediately follows the headers
    // block of the CONNECT request is considered to be part of the tunnel if it is
    // established.

    if (headers.get(HttpHeaderId::CONTENT_LENGTH) != zc::none) {
      co_return co_await sendError(HttpHeaders::ProtocolError{
          400,
          "Bad Request"_zc,
          "Bad Request"_zc,
          nullptr,
      });
    }
    if (headers.get(HttpHeaderId::TRANSFER_ENCODING) != zc::none) {
      co_return co_await sendError(HttpHeaders::ProtocolError{
          400,
          "Bad Request"_zc,
          "Bad Request"_zc,
          nullptr,
      });
    }

    SuspendableRequest suspendable(*this, HttpConnectMethod(), request.authority, headers);
    auto maybeService = factory(suspendable);

    if (suspended) { co_return BREAK_LOOP_CONN_ERR; }

    auto service =
        ZC_ASSERT_NONNULL(zc::mv(maybeService),
                          "SuspendableHttpServiceFactory did not suspend, but returned zc::none.");
    auto connectStream = getConnectStream();
    co_await service->connect(request.authority, headers, *connectStream, *this, {})
        .attach(zc::mv(service), zc::mv(connectStream));

    ZC_IF_SOME(p, tunnelRejected) {
      // reject() was called to reject a CONNECT attempt.
      // Finish sending and close the connection.
      auto promise = zc::mv(p);
      tunnelRejected = zc::none;
      co_return co_await promise;
    }

    if (httpOutput.isBroken()) { co_return BREAK_LOOP_CONN_ERR; }

    co_await httpOutput.flush();
    co_return BREAK_LOOP_CONN_ERR;
  }

  zc::Promise<LoopResult> onRequest(HttpHeaders::Request& request) {
    auto& headers = httpInput.getHeaders();

    currentMethod = request.method;

    SuspendableRequest suspendable(*this, request.method, request.url, headers);
    auto maybeService = factory(suspendable);

    if (suspended) { co_return BREAK_LOOP_CONN_ERR; }

    auto service =
        ZC_ASSERT_NONNULL(zc::mv(maybeService),
                          "SuspendableHttpServiceFactory did not suspend, but returned zc::none.");

    // TODO(perf): If the client disconnects, should we cancel the response? Probably, to
    //   prevent permanent deadlock. It's slightly weird in that arguably the client should
    //   be able to shutdown the upstream but still wait on the downstream, but I believe many
    //   other HTTP servers do similar things.

    auto body = httpInput.getEntityBody(HttpInputStreamImpl::REQUEST, request.method, 0, headers);

    co_await service->request(request.method, request.url, headers, *body, *this)
        .attach(zc::mv(service));
    // Response done. Await next request.

    ZC_IF_SOME(p, webSocketError) {
      // sendWebSocketError() was called. Finish sending and close the connection.
      auto promise = zc::mv(p);
      webSocketError = zc::none;
      co_return co_await promise;
    }

    if (upgraded) {
      // We've upgraded to WebSocket, and by now we should have closed the WebSocket.
      if (!webSocketOrConnectClosed) {
        // This is gonna segfault later so abort now instead.
        ZC_LOG(FATAL,
               "Accepted WebSocket object must be destroyed before HttpService "
               "request handler completes.");
        abort();
      }

      // Once we start a WebSocket there's no going back to HTTP.
      co_return BREAK_LOOP_CONN_ERR;
    }

    if (currentMethod != zc::none) { co_return co_await sendError(); }

    if (httpOutput.isBroken()) {
      // We started a response but didn't finish it. But HttpService returns success?
      // Perhaps it decided that it doesn't want to finish this response. We'll have to
      // disconnect here. If the response body is not complete (e.g. Content-Length not
      // reached), the client should notice. We don't want to log an error because this
      // condition might be intentional on the service's part.
      co_return BREAK_LOOP_CONN_ERR;
    }

    co_await httpOutput.flush();

    if (httpInput.canReuse()) {
      // Things look clean. Go ahead and accept the next request.

      if (closeAfterSend) {
        // We sent Connection: close, so drop the connection now.
        co_return BREAK_LOOP_CONN_ERR;
      } else {
        // Note that we don't have to handle server.draining here because we'll take care
        // of it the next time around the loop.
        co_return CONTINUE_LOOP;
      }
    } else {
      // Apparently, the application did not read the request body. Maybe this is a bug,
      // or maybe not: maybe the client tried to upload too much data and the application
      // legitimately wants to cancel the upload without reading all it it.
      //
      // We have a problem, though: We did send a response, and we didn't send
      // `Connection: close`, so the client may expect that it can send another request.
      // Perhaps the client has even finished sending the previous request's body, in
      // which case the moment it finishes receiving the response, it could be completely
      // within its rights to start a new request. If we close the socket now, we might
      // interrupt that new request.
      //
      // Or maybe we did send `Connection: close`, as indicated by `closeAfterSend` being
      // true. Even in that case, we should still try to read and ignore the request,
      // otherwise when we close the connection the client may get a "connection reset"
      // error before they get a chance to actually read the response body that we sent
      // them.
      //
      // There's no way we can get out of this perfectly cleanly. HTTP just isn't good
      // enough at connection management. The best we can do is give the client some grace
      // period and then abort the connection.

      auto dummy = zc::heap<HttpDiscardingEntityWriter>();
      auto lengthGrace =
          zc::evalNow(
              [&]() { return body->pumpTo(*dummy, server.settings.canceledUploadGraceBytes); })
              .catch_([](zc::Exception&& e) -> uint64_t {
                // Reading from the input failed in some way. This may actually be the whole
                // reason we got here in the first place so don't propagate this error, just
                // give up on discarding the input.
                return 0;  // This zero is ignored but `canReuse()` will return false below.
              })
              .then([this](uint64_t amount) {
                if (httpInput.canReuse()) {
                  // Success, we can continue.
                  return true;
                } else {
                  // Still more data. Give up.
                  return false;
                }
              });
      lengthGrace = lengthGrace.attach(zc::mv(dummy), zc::mv(body));

      auto timeGrace =
          server.timer.afterDelay(server.settings.canceledUploadGracePeriod).then([]() {
            return false;
          });

      auto clean = co_await lengthGrace.exclusiveJoin(zc::mv(timeGrace));
      if (clean && !closeAfterSend) {
        // We recovered. Continue loop.
        co_return CONTINUE_LOOP;
      } else {
        // Client still not done, or we sent Connection: close and so want to drop the
        // connection anyway. Return broken.
        co_return BREAK_LOOP_CONN_ERR;
      }
    }
  }

  zc::Own<zc::AsyncOutputStream> send(uint statusCode, zc::StringPtr statusText,
                                      const HttpHeaders& headers,
                                      zc::Maybe<uint64_t> expectedBodySize) override {
    auto method = ZC_REQUIRE_NONNULL(currentMethod, "already called send()");
    currentMethod = zc::none;

    zc::StringPtr connectionHeaders[HttpHeaders::CONNECTION_HEADERS_COUNT];
    zc::String lengthStr;

    if (!closeAfterSend) {
      // Check if application wants us to close connections.
      //
      // If the application used listenHttpCleanDrain() to listen, then it expects that after a
      // clean drain, the connection is still open and can receive more requests. Otherwise, after
      // receiving drain(), we will close the connection, so we should send a `Connection: close`
      // header.
      if (server.draining && !wantCleanDrain) {
        closeAfterSend = true;
      } else
        ZC_IF_SOME(c, server.settings.callbacks) {
          // The application has registered its own callback to decide whether to send
          // `Connection: close`.
          if (c.shouldClose()) { closeAfterSend = true; }
        }
    }

    if (closeAfterSend) { connectionHeaders[HttpHeaders::BuiltinIndices::CONNECTION] = "close"; }

    bool isHeadRequest = method.tryGet<HttpMethod>()
                             .map([](auto& m) { return m == HttpMethod::HEAD; })
                             .orDefault(false);

    if (statusCode == 204 || statusCode == 304) {
      // No entity-body.
    } else if (statusCode == 205) {
      // Status code 205 also has no body, but unlike 204 and 304, it must explicitly encode an
      // empty body, e.g. using content-length: 0. I'm guessing this is one of those things,
      // where some early clients expected an explicit body while others assumed an empty body,
      // and so the standard had to choose the common denominator.
      //
      // Spec: https://tools.ietf.org/html/rfc7231#section-6.3.6
      connectionHeaders[HttpHeaders::BuiltinIndices::CONTENT_LENGTH] = "0";
    } else
      ZC_IF_SOME(s, expectedBodySize) {
        // HACK: We interpret a zero-length expected body length on responses to HEAD requests to
        //   mean "don't set a Content-Length header at all." This provides a way to omit a body
        //   header on HEAD responses with non-null-body status codes. This is a hack that *only*
        //   makes sense for HEAD responses.
        if (!isHeadRequest || s > 0) {
          lengthStr = zc::str(s);
          connectionHeaders[HttpHeaders::BuiltinIndices::CONTENT_LENGTH] = lengthStr;
        }
      }
    else { connectionHeaders[HttpHeaders::BuiltinIndices::TRANSFER_ENCODING] = "chunked"; }

    // For HEAD requests, if the application specified a Content-Length or Transfer-Encoding
    // header, use that instead of whatever we decided above.
    zc::ArrayPtr<zc::StringPtr> connectionHeadersArray = connectionHeaders;
    if (isHeadRequest) {
      if (headers.get(HttpHeaderId::CONTENT_LENGTH) != zc::none ||
          headers.get(HttpHeaderId::TRANSFER_ENCODING) != zc::none) {
        connectionHeadersArray =
            connectionHeadersArray.first(HttpHeaders::HEAD_RESPONSE_CONNECTION_HEADERS_COUNT);
      }
    }

    httpOutput.writeHeaders(
        headers.serializeResponse(statusCode, statusText, connectionHeadersArray));

    if (isHeadRequest) {
      // Ignore entity-body.
      httpOutput.finishBody();
      return heap<HttpDiscardingEntityWriter>();
    } else if (statusCode == 204 || statusCode == 205 || statusCode == 304) {
      // No entity-body.
      httpOutput.finishBody();
      return heap<HttpNullEntityWriter>();
    } else
      ZC_IF_SOME(s, expectedBodySize) { return heap<HttpFixedLengthEntityWriter>(httpOutput, s); }
    else { return heap<HttpChunkedEntityWriter>(httpOutput); }
  }

  zc::Own<WebSocket> acceptWebSocket(const HttpHeaders& headers) override {
    auto& requestHeaders = httpInput.getHeaders();
    ZC_REQUIRE(
        requestHeaders.isWebSocket(),
        "can't call acceptWebSocket() if the request headers didn't have Upgrade: WebSocket");

    auto method = ZC_REQUIRE_NONNULL(currentMethod, "already called send()");
    ZC_REQUIRE(method.tryGet<HttpMethod>()
                   .map([](auto& m) { return m == HttpMethod::GET; })
                   .orDefault(false),
               "WebSocket must be initiated with a GET request.");

    if (requestHeaders.get(HttpHeaderId::SEC_WEBSOCKET_VERSION).orDefault(nullptr) != "13") {
      return sendWebSocketError("The requested WebSocket version is not supported.");
    }

    zc::String key;
    ZC_IF_SOME(k, requestHeaders.get(HttpHeaderId::SEC_WEBSOCKET_KEY)) { key = zc::str(k); }
    else { return sendWebSocketError("Missing Sec-WebSocket-Key"); }

    zc::Maybe<CompressionParameters> acceptedParameters;
    zc::String agreedParameters;
    auto compressionMode = server.settings.webSocketCompressionMode;
    if (compressionMode == HttpServerSettings::AUTOMATIC_COMPRESSION) {
      // If AUTOMATIC_COMPRESSION is enabled, we ignore the `headers` passed by the application and
      // strictly refer to the `requestHeaders` from the client.
      ZC_IF_SOME(value, requestHeaders.get(HttpHeaderId::SEC_WEBSOCKET_EXTENSIONS)) {
        // Perform compression parameter negotiation.
        ZC_IF_SOME(config, _::tryParseExtensionOffers(value)) {
          acceptedParameters = zc::mv(config);
        }
      }
    } else if (compressionMode == HttpServerSettings::MANUAL_COMPRESSION) {
      // If MANUAL_COMPRESSION is enabled, we use the `headers` passed in by the application, and
      // try to find a configuration that respects both the server's preferred configuration,
      // as well as the client's requested configuration.
      ZC_IF_SOME(value, headers.get(HttpHeaderId::SEC_WEBSOCKET_EXTENSIONS)) {
        // First, we get the manual configuration using `headers`.
        ZC_IF_SOME(manualConfig, _::tryParseExtensionOffers(value)) {
          ZC_IF_SOME(requestOffers, requestHeaders.get(HttpHeaderId::SEC_WEBSOCKET_EXTENSIONS)) {
            // Next, we to find a configuration that both the client and server can accept.
            acceptedParameters = _::tryParseAllExtensionOffers(requestOffers, manualConfig);
          }
        }
      }
    }

    auto websocketAccept = generateWebSocketAccept(key);

    zc::StringPtr connectionHeaders[HttpHeaders::WEBSOCKET_CONNECTION_HEADERS_COUNT];
    connectionHeaders[HttpHeaders::BuiltinIndices::SEC_WEBSOCKET_ACCEPT] = websocketAccept;
    connectionHeaders[HttpHeaders::BuiltinIndices::UPGRADE] = "websocket";
    connectionHeaders[HttpHeaders::BuiltinIndices::CONNECTION] = "Upgrade";
    ZC_IF_SOME(parameters, acceptedParameters) {
      agreedParameters = _::generateExtensionResponse(parameters);
      connectionHeaders[HttpHeaders::BuiltinIndices::SEC_WEBSOCKET_EXTENSIONS] = agreedParameters;
    }

    // Since we're about to write headers, we should nullify `currentMethod`. This tells
    // `sendError(zc::Exception)` (called from `HttpServer::Connection::startLoop()`) not to expose
    // the `HttpService::Response&` reference to the HttpServer's error `handleApplicationError()`
    // callback. This prevents the error handler from inadvertently trying to send another error on
    // the connection.
    currentMethod = zc::none;

    httpOutput.writeHeaders(
        headers.serializeResponse(101, "Switching Protocols", connectionHeaders));

    upgraded = true;
    // We need to give the WebSocket an Own<AsyncIoStream>, but we only have a reference. This is
    // safe because the application is expected to drop the WebSocket object before returning
    // from the request handler. For some extra safety, we check that webSocketOrConnectClosed has
    // been set true when the handler returns.
    auto deferNoteClosed = zc::defer([this]() { webSocketOrConnectClosed = true; });
    zc::Own<zc::AsyncIoStream> ownStream(&stream, zc::NullDisposer::instance);
    return upgradeToWebSocket(ownStream.attach(zc::mv(deferNoteClosed)), httpInput, httpOutput,
                              zc::none, zc::mv(acceptedParameters),
                              server.settings.webSocketErrorHandler);
  }

  zc::Promise<LoopResult> sendError(HttpHeaders::ProtocolError protocolError) {
    closeAfterSend = true;

    // Client protocol errors always happen on request headers parsing, before we call into the
    // HttpService, meaning no response has been sent and we can provide a Response object.
    auto promise = server.settings.errorHandler.orDefault(*this).handleClientProtocolError(
        zc::mv(protocolError), *this);
    return finishSendingError(zc::mv(promise));
  }

  zc::Promise<LoopResult> sendError(zc::Exception&& exception) {
    closeAfterSend = true;

    // We only provide the Response object if we know we haven't already sent a response.
    auto promise = server.settings.errorHandler.orDefault(*this).handleApplicationError(
        zc::mv(exception), currentMethod.map([this](auto&&) -> Response& { return *this; }));
    return finishSendingError(zc::mv(promise));
  }

  zc::Promise<LoopResult> sendError() {
    closeAfterSend = true;

    // We can provide a Response object, since none has already been sent.
    auto promise = server.settings.errorHandler.orDefault(*this).handleNoResponse(*this);
    return finishSendingError(zc::mv(promise));
  }

  zc::Promise<LoopResult> finishSendingError(zc::Promise<void> promise) {
    co_await promise;
    if (!httpOutput.isBroken()) {
      // Skip flush for broken streams, since it will throw an exception that may be worse than
      // the one we just handled.
      co_await httpOutput.flush();
    }
    co_return BREAK_LOOP_CONN_ERR;
  }

  zc::Own<WebSocket> sendWebSocketError(StringPtr errorMessage) {
    // The client committed a protocol error during a WebSocket handshake. We will send an error
    // response back to them, and throw an exception from `acceptWebSocket()` to our app. We'll
    // label this as a DISCONNECTED exception, as if the client had simply closed the connection
    // rather than committing a protocol error. This is intended to let the server know that it
    // wasn't an error on the server's part. (This is a big of a hack...)
    zc::Exception exception =
        ZC_EXCEPTION(DISCONNECTED, "received bad WebSocket handshake", errorMessage);
    webSocketError =
        sendError(HttpHeaders::ProtocolError{400, "Bad Request", errorMessage, nullptr});
    zc::throwFatalException(zc::mv(exception));
  }

  zc::Own<zc::AsyncIoStream> getConnectStream() {
    // Returns an AsyncIoStream over the internal stream but that waits for a Promise to be
    // resolved to allow writes after either accept or reject are called. Reads are allowed
    // immediately.
    ZC_REQUIRE(tunnelWriteGuard == zc::none, "the tunnel stream was already retrieved");
    auto paf = zc::newPromiseAndFulfiller<void>();
    tunnelWriteGuard = zc::mv(paf.fulfiller);

    zc::Own<zc::AsyncIoStream> ownStream(&stream, zc::NullDisposer::instance);
    auto releasedBuffer = httpInput.releaseBuffer();
    auto deferNoteClosed = zc::defer([this]() { webSocketOrConnectClosed = true; });
    return zc::heap<AsyncIoStreamWithGuards>(
        zc::heap<AsyncIoStreamWithInitialBuffer>(zc::mv(ownStream), zc::mv(releasedBuffer.buffer),
                                                 releasedBuffer.leftover)
            .attach(zc::mv(deferNoteClosed)),
        zc::Maybe<HttpInputStreamImpl::ReleasedBuffer>(zc::none), zc::mv(paf.promise));
  }

  void accept(uint statusCode, zc::StringPtr statusText, const HttpHeaders& headers) override {
    auto method = ZC_REQUIRE_NONNULL(currentMethod, "already called send()");
    currentMethod = zc::none;
    ZC_ASSERT(method.is<HttpConnectMethod>(), "only use accept() with CONNECT requests");
    ZC_REQUIRE(statusCode >= 200 && statusCode < 300, "the statusCode must be 2xx for accept");
    tunnelRejected = zc::none;

    auto& fulfiller = ZC_ASSERT_NONNULL(tunnelWriteGuard, "the tunnel stream was not initialized");
    httpOutput.writeHeaders(headers.serializeResponse(statusCode, statusText));
    auto promise =
        httpOutput.flush().then([&fulfiller]() { fulfiller->fulfill(); }).eagerlyEvaluate(nullptr);
    fulfiller = fulfiller.attach(zc::mv(promise));
  }

  zc::Own<zc::AsyncOutputStream> reject(uint statusCode, zc::StringPtr statusText,
                                        const HttpHeaders& headers,
                                        zc::Maybe<uint64_t> expectedBodySize) override {
    auto method = ZC_REQUIRE_NONNULL(currentMethod, "already called send()");
    ZC_REQUIRE(method.is<HttpConnectMethod>(), "Only use reject() with CONNECT requests.");
    ZC_REQUIRE(statusCode < 200 || statusCode >= 300, "the statusCode must not be 2xx for reject.");
    tunnelRejected = Maybe<zc::Promise<LoopResult>>(BREAK_LOOP_CONN_OK);

    auto& fulfiller = ZC_ASSERT_NONNULL(tunnelWriteGuard, "the tunnel stream was not initialized");
    fulfiller->reject(ZC_EXCEPTION(DISCONNECTED, "the tunnel request was rejected"));
    closeAfterSend = true;
    return send(statusCode, statusText, headers, expectedBodySize);
  }
};

HttpServer::HttpServer(zc::Timer& timer, const HttpHeaderTable& requestHeaderTable,
                       HttpService& service, Settings settings)
    : HttpServer(timer, requestHeaderTable, &service, settings,
                 zc::newPromiseAndFulfiller<void>()) {}

HttpServer::HttpServer(zc::Timer& timer, const HttpHeaderTable& requestHeaderTable,
                       HttpServiceFactory serviceFactory, Settings settings)
    : HttpServer(timer, requestHeaderTable, zc::mv(serviceFactory), settings,
                 zc::newPromiseAndFulfiller<void>()) {}

HttpServer::HttpServer(zc::Timer& timer, const HttpHeaderTable& requestHeaderTable,
                       zc::OneOf<HttpService*, HttpServiceFactory> service, Settings settings,
                       zc::PromiseFulfillerPair<void> paf)
    : timer(timer),
      requestHeaderTable(requestHeaderTable),
      service(zc::mv(service)),
      settings(settings),
      onDrain(paf.promise.fork()),
      drainFulfiller(zc::mv(paf.fulfiller)),
      tasks(*this) {}

zc::Promise<void> HttpServer::drain() {
  ZC_REQUIRE(!draining, "you can only call drain() once");

  draining = true;
  drainFulfiller->fulfill();

  if (connectionCount == 0) {
    return zc::READY_NOW;
  } else {
    auto paf = zc::newPromiseAndFulfiller<void>();
    zeroConnectionsFulfiller = zc::mv(paf.fulfiller);
    return zc::mv(paf.promise);
  }
}

zc::Promise<void> HttpServer::listenHttp(zc::ConnectionReceiver& port) {
  return listenLoop(port).exclusiveJoin(onDrain.addBranch());
}

zc::Promise<void> HttpServer::listenLoop(zc::ConnectionReceiver& port) {
  for (;;) {
    auto connection = co_await port.accept();
    tasks.add(zc::evalNow([&]() { return listenHttp(zc::mv(connection)); }));
  }
}

zc::Promise<void> HttpServer::listenHttp(zc::Own<zc::AsyncIoStream> connection) {
  auto promise = listenHttpImpl(*connection, false /* wantCleanDrain */).ignoreResult();

  // eagerlyEvaluate() to maintain historical guarantee that this method eagerly closes the
  // connection when done.
  return promise.attach(zc::mv(connection)).eagerlyEvaluate(nullptr);
}

zc::Promise<bool> HttpServer::listenHttpCleanDrain(zc::AsyncIoStream& connection) {
  return listenHttpImpl(connection, true /* wantCleanDrain */);
}

zc::Promise<bool> HttpServer::listenHttpImpl(zc::AsyncIoStream& connection, bool wantCleanDrain) {
  zc::Own<HttpService> srv;

  ZC_SWITCH_ONEOF(service) {
    ZC_CASE_ONEOF(ptr, HttpService*) {
      // Fake Own okay because we can assume the HttpService outlives this HttpServer, and we can
      // assume `this` HttpServer outlives the returned `listenHttpCleanDrain()` promise, which will
      // own the fake Own.
      srv = zc::Own<HttpService>(ptr, zc::NullDisposer::instance);
    }
    ZC_CASE_ONEOF(func, HttpServiceFactory) { srv = func(connection); }
  }

  ZC_ASSERT(srv.get() != nullptr);

  return listenHttpImpl(
      connection,
      [srv = zc::mv(srv)](SuspendableRequest&) mutable {
        // This factory function will be owned by the Connection object, meaning the Connection
        // object will own the HttpService. We also know that the Connection object outlives all
        // service.request() promises (service.request() is called from a Connection member
        // function). The Owns we return from this function are attached to the service.request()
        // promises, meaning this factory function will outlive all Owns we return. So, it's safe to
        // return a fake Own.
        return zc::Own<HttpService>(srv.get(), zc::NullDisposer::instance);
      },
      zc::none /* suspendedRequest */, wantCleanDrain);
}

zc::Promise<bool> HttpServer::listenHttpCleanDrain(zc::AsyncIoStream& connection,
                                                   SuspendableHttpServiceFactory factory,
                                                   zc::Maybe<SuspendedRequest> suspendedRequest) {
  // Don't close on drain, because a "clean drain" means we return the connection to the
  // application still-open between requests so that it can continue serving future HTTP requests
  // on it.
  return listenHttpImpl(connection, zc::mv(factory), zc::mv(suspendedRequest),
                        true /* wantCleanDrain */);
}

zc::Promise<bool> HttpServer::listenHttpImpl(zc::AsyncIoStream& connection,
                                             SuspendableHttpServiceFactory factory,
                                             zc::Maybe<SuspendedRequest> suspendedRequest,
                                             bool wantCleanDrain) {
  Connection obj(*this, connection, zc::mv(factory), zc::mv(suspendedRequest), wantCleanDrain);

  // Start reading requests and responding to them, but immediately cancel processing if the client
  // disconnects.
  co_return co_await obj.startLoop()
      .exclusiveJoin(connection.whenWriteDisconnected().then([]() { return false; }))
      // Eagerly evaluate so that we drop the connection when the promise resolves, even if the
      // caller doesn't eagerly evaluate.
      .eagerlyEvaluate(nullptr);
}

namespace {
void defaultHandleListenLoopException(zc::Exception&& exception) {
  ZC_LOG(ERROR, "unhandled exception in HTTP server", exception);
}
}  // namespace

void HttpServer::taskFailed(zc::Exception&& exception) {
  ZC_IF_SOME(handler, settings.errorHandler) {
    handler.handleListenLoopException(zc::mv(exception));
  }
  else { defaultHandleListenLoopException(zc::mv(exception)); }
}

HttpServer::SuspendedRequest::SuspendedRequest(zc::Array<byte> bufferParam,
                                               zc::ArrayPtr<byte> leftoverParam,
                                               zc::OneOf<HttpMethod, HttpConnectMethod> method,
                                               zc::StringPtr url, HttpHeaders headers)
    : buffer(zc::mv(bufferParam)),
      leftover(leftoverParam),
      method(method),
      url(url),
      headers(zc::mv(headers)) {
  if (leftover.size() > 0) {
    // We have a `leftover`; make sure it is a slice of `buffer`.
    ZC_ASSERT(leftover.begin() >= buffer.begin() && leftover.begin() <= buffer.end());
    ZC_ASSERT(leftover.end() >= buffer.begin() && leftover.end() <= buffer.end());
  } else {
    // We have no `leftover`, but we still expect it to point into `buffer` somewhere. This is
    // important so that `messageHeaderEnd` is initialized correctly in HttpInputStreamImpl's
    // constructor.
    ZC_ASSERT(leftover.begin() >= buffer.begin() && leftover.begin() <= buffer.end());
  }
}

HttpServer::SuspendedRequest HttpServer::SuspendableRequest::suspend() {
  return connection.suspend(*this);
}

zc::Promise<void> HttpServerErrorHandler::handleClientProtocolError(
    HttpHeaders::ProtocolError protocolError, zc::HttpService::Response& response) {
  // Default error handler implementation.

  HttpHeaderTable headerTable{};
  HttpHeaders headers(headerTable);
  headers.set(HttpHeaderId::CONTENT_TYPE, "text/plain");

  auto errorMessage = zc::str("ERROR: ", protocolError.description);
  auto body = response.send(protocolError.statusCode, protocolError.statusMessage, headers,
                            errorMessage.size());

  return body->write(errorMessage.asBytes()).attach(zc::mv(errorMessage), zc::mv(body));
}

zc::Promise<void> HttpServerErrorHandler::handleApplicationError(
    zc::Exception exception, zc::Maybe<zc::HttpService::Response&> response) {
  // Default error handler implementation.

  if (exception.getType() == zc::Exception::Type::DISCONNECTED) {
    // How do we tell an HTTP client that there was a transient network error, and it should
    // try again immediately? There's no HTTP status code for this (503 is meant for "try
    // again later, not now"). Here's an idea: Don't send any response; just close the
    // connection, so that it looks like the connection between the HTTP client and server
    // was dropped. A good client should treat this exactly the way we want.
    //
    // We also bail here to avoid logging the disconnection, which isn't very interesting.
    return zc::READY_NOW;
  }

  ZC_IF_SOME(r, response) {
    ZC_LOG(INFO, "threw exception while serving HTTP response", exception);

    HttpHeaderTable headerTable{};
    HttpHeaders headers(headerTable);
    headers.set(HttpHeaderId::CONTENT_TYPE, "text/plain");

    zc::String errorMessage;
    zc::Own<AsyncOutputStream> body;

    if (exception.getType() == zc::Exception::Type::OVERLOADED) {
      errorMessage =
          zc::str("ERROR: The server is temporarily unable to handle your request. Details:\n\n",
                  exception);
      body = r.send(503, "Service Unavailable", headers, errorMessage.size());
    } else if (exception.getType() == zc::Exception::Type::UNIMPLEMENTED) {
      errorMessage =
          zc::str("ERROR: The server does not implement this operation. Details:\n\n", exception);
      body = r.send(501, "Not Implemented", headers, errorMessage.size());
    } else {
      errorMessage = zc::str("ERROR: The server threw an exception. Details:\n\n", exception);
      body = r.send(500, "Internal Server Error", headers, errorMessage.size());
    }

    return body->write(errorMessage.asBytes()).attach(zc::mv(errorMessage), zc::mv(body));
  }

  ZC_LOG(ERROR, "HttpService threw exception after generating a partial response",
         "too late to report error to client", exception);
  return zc::READY_NOW;
}

void HttpServerErrorHandler::handleListenLoopException(zc::Exception&& exception) {
  defaultHandleListenLoopException(zc::mv(exception));
}

zc::Promise<void> HttpServerErrorHandler::handleNoResponse(zc::HttpService::Response& response) {
  HttpHeaderTable headerTable{};
  HttpHeaders headers(headerTable);
  headers.set(HttpHeaderId::CONTENT_TYPE, "text/plain");

  constexpr auto errorMessage = "ERROR: The HttpService did not generate a response."_zc;
  auto body = response.send(500, "Internal Server Error", headers, errorMessage.size());

  return body->write(errorMessage.asBytes()).attach(zc::mv(body));
}

}  // namespace zc
