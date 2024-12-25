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

#if ZC_HAS_OPENSSL

#include "src/zc/tls/tls.h"

#include <openssl/bio.h>
#include <openssl/conf.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <src/zc/async/async-queue.h>
#include <src/zc/core/debug.h>
#include <src/zc/core/vector.h>

#include "src/zc/tls/readiness-io.h"

#ifdef _WIN32
#include <cryptuiapi.h>
#include <src/zc/core/win32-api-version.h>
#include <wincrypt.h>
#include <windows.h>
#undef CONST
#undef X509_NAME
#include <src/zc/core/windows-sanity.h>
#endif

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define BIO_set_init(x, v) (x->init = v)
#define BIO_get_data(x) (x->ptr)
#define BIO_set_data(x, v) (x->ptr = v)
#endif

namespace zc {

// =======================================================================================
// misc helpers

namespace {

zc::Exception getOpensslError() {
  // Call when an OpenSSL function returns an error code to convert that into an exception.

  zc::Vector<zc::String> lines;
  while (unsigned long long error = ERR_get_error()) {
#ifdef SSL_R_UNEXPECTED_EOF_WHILE_READING
    // OpenSSL 3.0+ reports unexpected disconnects this way.
    if (ERR_GET_REASON(error) == SSL_R_UNEXPECTED_EOF_WHILE_READING) {
      return ZC_EXCEPTION(DISCONNECTED, "peer disconnected without gracefully ending TLS session");
    }
#endif

    char message[1024]{};
    ERR_error_string_n(error, message, sizeof(message));
    lines.add(zc::heapString(message));
  }
  zc::String message = zc::strArray(lines, "\n");
  return ZC_EXCEPTION(FAILED, "OpenSSL error", message);
}

ZC_NORETURN(void throwOpensslError());
void throwOpensslError() {
  // Call when an OpenSSL function returns an error code to convert that into an exception and
  // throw it.

  zc::throwFatalException(getOpensslError());
}

#ifdef _WIN32
// Adds windows CA store certificates to OpenSSL's ca store
// Taken from
// https://stackoverflow.com/questions/9507184/can-openssl-on-windows-use-the-system-certificate-store
void updateOpenSSLCAStoreWithWindowsCertificates(SSL_CTX* ctx) {
  X509_STORE* store = SSL_CTX_get_cert_store(ctx);
  if (store == nullptr) { throwOpensslError(); }
  HCERTSTORE hStore;
  ZC_WIN32(hStore = CertOpenSystemStoreA(NULL, "ROOT"));
  ZC_DEFER(ZC_WIN32(CertCloseStore(hStore, 0)));
  PCCERT_CONTEXT pContext = nullptr;
  ZC_DEFER(CertFreeCertificateContext(pContext));
  while ((pContext = CertEnumCertificatesInStore(hStore, pContext))) {
    X509* x509 =
        d2i_X509(nullptr, (const unsigned char**)&pContext->pbCertEncoded, pContext->cbCertEncoded);
    if (x509) {
      ZC_DEFER(X509_free(x509));
      if (!X509_STORE_add_cert(store, x509)) { throwOpensslError(); }
    }
  }
}
#endif

#if OPENSSL_VERSION_NUMBER < 0x10100000L && !defined(OPENSSL_IS_BORINGSSL)
// Older versions of OpenSSL don't define _up_ref() functions.

void EVP_PKEY_up_ref(EVP_PKEY* pkey) { CRYPTO_add(&pkey->references, 1, CRYPTO_LOCK_EVP_PKEY); }

void X509_up_ref(X509* x509) { CRYPTO_add(&x509->references, 1, CRYPTO_LOCK_X509); }

#endif

#if OPENSSL_VERSION_NUMBER < 0x10100000L
class OpenSslInit {
  // Initializes the OpenSSL library.
public:
  OpenSslInit() {
    SSL_library_init();
    SSL_load_error_strings();
    OPENSSL_config(nullptr);
  }
};

void ensureOpenSslInitialized() {
  // Initializes the OpenSSL library the first time it is called.
  static OpenSslInit init;
}
#else
inline void ensureOpenSslInitialized() {
  // As of 1.1.0, no initialization is needed.
}
#endif

bool isIpAddress(zc::StringPtr addr) {
  bool isPossiblyIp6 = true;
  bool isPossiblyIp4 = true;
  uint colonCount = 0;
  uint dotCount = 0;
  for (auto c : addr) {
    if (c == ':') {
      isPossiblyIp4 = false;
      ++colonCount;
    } else if (c == '.') {
      isPossiblyIp6 = false;
      ++dotCount;
    } else if ('0' <= c && c <= '9') {
      // Digit is valid for ipv4 or ipv6.
    } else if (('a' <= c && c <= 'f') || ('A' <= c && c <= 'F')) {
      // Hex digit could be ipv6 but not ipv4.
      isPossiblyIp4 = false;
    } else {
      // Nope.
      return false;
    }
  }

  // An IPv4 address has 3 dots. (Yes, I'm aware that technically IPv4 addresses can be formatted
  // with fewer dots, but it's not clear that we actually want to support TLS authentication of
  // non-canonical address formats, so for now I'm not. File a bug if you care.) An IPv6 address
  // has at least 2 and as many as 7 colons.
  return (isPossiblyIp4 && dotCount == 3) || (isPossiblyIp6 && colonCount >= 2 && colonCount <= 7);
}

}  // namespace

// =======================================================================================
// Implementation of zc::AsyncIoStream that applies TLS on top of some other AsyncIoStream.
//
// TODO(perf): OpenSSL's I/O abstraction layer, "BIO", is readiness-based, but AsyncIoStream is
//   completion-based. This forces us to use an intermediate buffer which wastes memory and incurs
//   redundant copies. We could improve the situation by creating a way to detect if the underlying
//   AsyncIoStream is simply wrapping a file descriptor (or other readiness-based stream?) and use
//   that directly if so.

class TlsConnection final : public zc::AsyncIoStream {
public:
  TlsConnection(zc::Own<zc::AsyncIoStream> stream, SSL_CTX* ctx) : TlsConnection(*stream, ctx) {
    ownInner = zc::mv(stream);
  }

  TlsConnection(zc::AsyncIoStream& stream, SSL_CTX* ctx)
      : inner(stream), readBuffer(stream), writeBuffer(stream) {
    ssl = SSL_new(ctx);
    if (ssl == nullptr) { throwOpensslError(); }

    BIO* bio = BIO_new(const_cast<BIO_METHOD*>(getBioVtable()));
    if (bio == nullptr) {
      SSL_free(ssl);
      throwOpensslError();
    }

    BIO_set_data(bio, this);
    BIO_set_init(bio, 1);
    SSL_set_bio(ssl, bio, bio);
  }

  zc::Promise<void> connect(zc::StringPtr expectedServerHostname) {
    if (!SSL_set_tlsext_host_name(ssl, expectedServerHostname.cStr())) { return getOpensslError(); }

    X509_VERIFY_PARAM* verify = SSL_get0_param(ssl);
    if (verify == nullptr) { return getOpensslError(); }

    if (isIpAddress(expectedServerHostname)) {
      if (X509_VERIFY_PARAM_set1_ip_asc(verify, expectedServerHostname.cStr()) <= 0) {
        return getOpensslError();
      }
    } else {
      if (X509_VERIFY_PARAM_set1_host(verify, expectedServerHostname.cStr(),
                                      expectedServerHostname.size()) <= 0) {
        return getOpensslError();
      }
    }

    // As of OpenSSL 1.1.0, X509_V_FLAG_TRUSTED_FIRST is on by default. Turning it on for older
    // versions -- as well as certain OpenSSL-compatible libraries -- fixes the problem described
    // here:
    // https://community.letsencrypt.org/t/openssl-client-compatibility-changes-for-let-s-encrypt-certificates/143816
    //
    // Otherwise, certificates issued by Let's Encrypt won't work as of September 30, 2021:
    // https://letsencrypt.org/docs/dst-root-ca-x3-expiration-september-2021/
    X509_VERIFY_PARAM_set_flags(verify, X509_V_FLAG_TRUSTED_FIRST);

    return sslCall([this]() { return SSL_connect(ssl); }).then([this](size_t) {
      X509* cert = SSL_get_peer_certificate(ssl);
      ZC_REQUIRE(cert != nullptr, "TLS peer provided no certificate") { return; }
      X509_free(cert);

      auto result = SSL_get_verify_result(ssl);
      if (result != X509_V_OK) {
        const char* reason = X509_verify_cert_error_string(result);
        ZC_FAIL_REQUIRE("TLS peer's certificate is not trusted", reason) { break; }
      }
    });
  }

  zc::Promise<void> accept() {
    // We are the server. Set SSL options to prefer server's cipher choice.
    SSL_set_options(ssl, SSL_OP_CIPHER_SERVER_PREFERENCE);

    auto acceptPromise = sslCall([this]() { return SSL_accept(ssl); });
    return acceptPromise.then([](size_t ret) {
      if (ret == 0) {
        zc::throwRecoverableException(
            ZC_EXCEPTION(DISCONNECTED, "Client disconnected during SSL_accept()"));
      }
    });
  }

  zc::Own<TlsPeerIdentity> getIdentity(zc::Own<zc::PeerIdentity> inner) {
    return zc::heap<TlsPeerIdentity>(SSL_get_peer_certificate(ssl), zc::mv(inner),
                                     zc::Badge<TlsConnection>());
  }

  ~TlsConnection() noexcept(false) { SSL_free(ssl); }

  zc::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return tryReadInternal(buffer, minBytes, maxBytes, 0);
  }

  Promise<void> write(ArrayPtr<const byte> buffer) override {
    return writeInternal(buffer, nullptr);
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    auto cork = writeBuffer.cork();
    return writeInternal(pieces[0], pieces.slice(1, pieces.size())).attach(zc::mv(cork));
  }

  Promise<void> whenWriteDisconnected() override { return inner.whenWriteDisconnected(); }

  void shutdownWrite() override {
    ZC_REQUIRE(shutdownTask == zc::none, "already called shutdownWrite()");

    // TODO(2.0): shutdownWrite() is problematic because it doesn't return a promise. It was
    //   designed to assume that it would only be called after all writes are finished and that
    //   there was no reason to block at that point, but SSL sessions don't fit this since they
    //   actually have to send a shutdown message.
    shutdownTask = sslCall([this]() {
                     // The first SSL_shutdown() call is expected to return 0 and may flag a
                     // misleading error.
                     int result = SSL_shutdown(ssl);
                     return result == 0 ? 1 : result;
                   })
                       .ignoreResult()
                       .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });
  }

  void abortRead() override { inner.abortRead(); }

  void getsockopt(int level, int option, void* value, uint* length) override {
    inner.getsockopt(level, option, value, length);
  }
  void setsockopt(int level, int option, const void* value, uint length) override {
    inner.setsockopt(level, option, value, length);
  }

  void getsockname(struct sockaddr* addr, uint* length) override {
    inner.getsockname(addr, length);
  }
  void getpeername(struct sockaddr* addr, uint* length) override {
    inner.getpeername(addr, length);
  }

  zc::Maybe<int> getFd() const override { return inner.getFd(); }

private:
  SSL* ssl;
  zc::AsyncIoStream& inner;
  zc::Own<zc::AsyncIoStream> ownInner;

  zc::Maybe<zc::Promise<void>> shutdownTask;

  ReadyInputStreamWrapper readBuffer;
  ReadyOutputStreamWrapper writeBuffer;

  zc::Promise<size_t> tryReadInternal(void* buffer, size_t minBytes, size_t maxBytes,
                                      size_t alreadyDone) {
    return sslCall([this, buffer, maxBytes]() { return SSL_read(ssl, buffer, maxBytes); })
        .then([this, buffer, minBytes, maxBytes, alreadyDone](size_t n) -> zc::Promise<size_t> {
          if (n >= minBytes || n == 0) {
            return alreadyDone + n;
          } else {
            return tryReadInternal(reinterpret_cast<byte*>(buffer) + n, minBytes - n, maxBytes - n,
                                   alreadyDone + n);
          }
        });
  }

  Promise<void> writeInternal(zc::ArrayPtr<const byte> first,
                              zc::ArrayPtr<const zc::ArrayPtr<const byte>> rest) {
    ZC_REQUIRE(shutdownTask == zc::none, "already called shutdownWrite()");

    // SSL_write() with a zero-sized input returns 0, but a 0 return is documented as indicating
    // an error. So, we need to avoid zero-sized writes entirely.
    while (first.size() == 0) {
      if (rest.size() == 0) { return zc::READY_NOW; }
      first = rest.front();
      rest = rest.slice(1, rest.size());
    }

    return sslCall([this, first]() { return SSL_write(ssl, first.begin(), first.size()); })
        .then([this, first, rest](size_t n) -> zc::Promise<void> {
          if (n == 0) {
            return ZC_EXCEPTION(DISCONNECTED, "ssl connection ended during write");
          } else if (n < first.size()) {
            return writeInternal(first.slice(n, first.size()), rest);
          } else if (rest.size() > 0) {
            return writeInternal(rest[0], rest.slice(1, rest.size()));
          } else {
            return zc::READY_NOW;
          }
        });
  }

  template <typename Func>
  zc::Promise<size_t> sslCall(Func&& func) {
    auto result = func();

    if (result > 0) {
      return result;
    } else {
      int error = SSL_get_error(ssl, result);
      switch (error) {
        case SSL_ERROR_ZERO_RETURN:
          return constPromise<size_t, 0>();
        case SSL_ERROR_WANT_READ:
          return readBuffer.whenReady().then(
              [this, func = zc::mv(func)]() mutable { return sslCall(zc::fwd<Func>(func)); });
        case SSL_ERROR_WANT_WRITE:
          return writeBuffer.whenReady().then(
              [this, func = zc::mv(func)]() mutable { return sslCall(zc::fwd<Func>(func)); });
        case SSL_ERROR_SSL:
          return getOpensslError();
        case SSL_ERROR_SYSCALL:
          if (result == 0) {
            // OpenSSL pre-3.0 reports unexpected disconnects this way. Note that 3.0+ report it
            // as SSL_ERROR_SSL with the reason SSL_R_UNEXPECTED_EOF_WHILE_READING, which is
            // handled in throwOpensslError().
            return ZC_EXCEPTION(DISCONNECTED,
                                "peer disconnected without gracefully ending TLS session");
          } else {
            // According to documentation we shouldn't get here, because our BIO never returns an
            // "error". But in practice we do get here sometimes when the peer disconnects
            // prematurely.
            return ZC_EXCEPTION(DISCONNECTED, "SSL unable to continue I/O");
          }
        default:
          ZC_FAIL_ASSERT("unexpected SSL error code", error);
      }
    }
  }

  static int bioRead(BIO* b, char* out, int outl) {
    BIO_clear_retry_flags(b);
    ZC_IF_SOME(n, reinterpret_cast<TlsConnection*>(BIO_get_data(b))
                      ->readBuffer.read(zc::arrayPtr(out, outl).asBytes())) {
      return n;
    }
    else {
      BIO_set_retry_read(b);
      return -1;
    }
  }

  static int bioWrite(BIO* b, const char* in, int inl) {
    BIO_clear_retry_flags(b);
    ZC_IF_SOME(n, reinterpret_cast<TlsConnection*>(BIO_get_data(b))
                      ->writeBuffer.write(zc::arrayPtr(in, inl).asBytes())) {
      return n;
    }
    else {
      BIO_set_retry_write(b);
      return -1;
    }
  }

  static long bioCtrl(BIO* b, int cmd, long num, void* ptr) {
    switch (cmd) {
      case BIO_CTRL_EOF:
        return reinterpret_cast<TlsConnection*>(BIO_get_data(b))->readBuffer.isAtEnd();
      case BIO_CTRL_FLUSH:
        return 1;
      case BIO_CTRL_PUSH:
      case BIO_CTRL_POP:
        // Informational?
        return 0;
#ifdef BIO_CTRL_GET_KTLS_SEND
      case BIO_CTRL_GET_KTLS_SEND:
      case BIO_CTRL_GET_KTLS_RECV:
        // TODO(someday): Support kTLS if the underlying stream is a raw socket.
        return 0;
#endif
      default:
        ZC_LOG(WARNING, "unimplemented bio_ctrl", cmd);
        return 0;
    }
  }

  static int bioCreate(BIO* b) {
    BIO_set_data(b, nullptr);
    return 1;
  }

  static int bioDestroy(BIO* b) {
    // The BIO does NOT own the TlsConnection.
    return 1;
  }

#if OPENSSL_VERSION_NUMBER < 0x10100000L
  static const BIO_METHOD* getBioVtable() {
    static const BIO_METHOD VTABLE{BIO_TYPE_SOURCE_SINK,
                                   "ZC stream",
                                   TlsConnection::bioWrite,
                                   TlsConnection::bioRead,
                                   nullptr,  // puts
                                   nullptr,  // gets
                                   TlsConnection::bioCtrl,
                                   TlsConnection::bioCreate,
                                   TlsConnection::bioDestroy,
                                   nullptr};
    return &VTABLE;
  }
#else
  static const BIO_METHOD* getBioVtable() {
    static const BIO_METHOD* const vtable = makeBioVtable();
    return vtable;
  }
  static const BIO_METHOD* makeBioVtable() {
    BIO_METHOD* vtable = BIO_meth_new(BIO_TYPE_SOURCE_SINK, "ZC stream");
    BIO_meth_set_write(vtable, TlsConnection::bioWrite);
    BIO_meth_set_read(vtable, TlsConnection::bioRead);
    BIO_meth_set_ctrl(vtable, TlsConnection::bioCtrl);
    BIO_meth_set_create(vtable, TlsConnection::bioCreate);
    BIO_meth_set_destroy(vtable, TlsConnection::bioDestroy);
    return vtable;
  }
#endif
};

// =======================================================================================
// Implementations of ConnectionReceiver, NetworkAddress, and Network as wrappers adding TLS.

class TlsConnectionReceiver final : public ConnectionReceiver, public TaskSet::ErrorHandler {
public:
  TlsConnectionReceiver(TlsContext& tls, Own<ConnectionReceiver> inner,
                        zc::Maybe<TlsErrorHandler> acceptErrorHandler)
      : tls(tls),
        inner(zc::mv(inner)),
        acceptLoopTask(
            acceptLoop().eagerlyEvaluate([this](Exception&& e) { onAcceptFailure(zc::mv(e)); })),
        acceptErrorHandler(zc::mv(acceptErrorHandler)),
        tasks(*this) {}

  void taskFailed(Exception&& e) override {
    ZC_IF_SOME(handler, acceptErrorHandler) { handler(zc::mv(e)); }
    else if (e.getType() != Exception::Type::DISCONNECTED) {
      ZC_LOG(ERROR, "error accepting tls connection", zc::mv(e));
    }
  };

  Promise<Own<AsyncIoStream>> accept() override {
    return acceptAuthenticated().then(
        [](AuthenticatedStream&& stream) { return zc::mv(stream.stream); });
  }

  Promise<AuthenticatedStream> acceptAuthenticated() override {
    ZC_IF_SOME(e, maybeInnerException) {
      // We've experienced an exception from the inner receiver, we consider this unrecoverable.
      return Exception(e);
    }

    return queue.pop();
  }

  uint getPort() override { return inner->getPort(); }

  void getsockopt(int level, int option, void* value, uint* length) override {
    return inner->getsockopt(level, option, value, length);
  }

  void setsockopt(int level, int option, const void* value, uint length) override {
    return inner->setsockopt(level, option, value, length);
  }

private:
  void onAcceptSuccess(AuthenticatedStream&& stream) {
    // Queue this stream to go through SSL_accept.

    auto acceptPromise = zc::evalNow([&] {
      // Do the SSL acceptance procedure.
      return tls.wrapServer(zc::mv(stream));
    });

    auto sslPromise = acceptPromise.then([this](auto&& stream) -> Promise<void> {
      // This is only attached to the success path, thus the error handler will catch if our
      // promise fails.
      queue.push(zc::mv(stream));
      return zc::READY_NOW;
    });
    tasks.add(zc::mv(sslPromise));
  }

  void onAcceptFailure(Exception&& e) {
    // Store this exception to reject all future calls to accept() and reject any unfulfilled
    // promises from the queue.
    maybeInnerException = zc::mv(e);
    queue.rejectAll(Exception(ZC_REQUIRE_NONNULL(maybeInnerException)));
  }

  Promise<void> acceptLoop() {
    // Accept one connection and queue up the next accept on our TaskSet.

    return inner->acceptAuthenticated().then([this](AuthenticatedStream&& stream) {
      onAcceptSuccess(zc::mv(stream));

      // Queue up the next accept loop immediately without waiting for SSL_accept()/wrapServer().
      return acceptLoop();
    });
  }

  TlsContext& tls;
  Own<ConnectionReceiver> inner;

  Promise<void> acceptLoopTask;
  ProducerConsumerQueue<AuthenticatedStream> queue;
  zc::Maybe<TlsErrorHandler> acceptErrorHandler;
  TaskSet tasks;

  Maybe<Exception> maybeInnerException;
};

class TlsNetworkAddress final : public zc::NetworkAddress {
public:
  TlsNetworkAddress(TlsContext& tls, zc::String hostname, zc::Own<zc::NetworkAddress>&& inner)
      : tls(tls), hostname(zc::mv(hostname)), inner(zc::mv(inner)) {}

  Promise<Own<AsyncIoStream>> connect() override {
    // Note: It's unfortunately pretty common for people to assume they can drop the NetworkAddress
    //   as soon as connect() returns, and this works with the native network implementation.
    //   So, we make some copies here.
    auto& tlsRef = tls;
    auto hostnameCopy = zc::str(hostname);
    return inner->connect().then(
        [&tlsRef, hostname = zc::mv(hostnameCopy)](Own<AsyncIoStream>&& stream) {
          return tlsRef.wrapClient(zc::mv(stream), hostname);
        });
  }

  Promise<zc::AuthenticatedStream> connectAuthenticated() override {
    // Note: It's unfortunately pretty common for people to assume they can drop the NetworkAddress
    //   as soon as connect() returns, and this works with the native network implementation.
    //   So, we make some copies here.
    auto& tlsRef = tls;
    auto hostnameCopy = zc::str(hostname);
    return inner->connectAuthenticated().then(
        [&tlsRef, hostname = zc::mv(hostnameCopy)](zc::AuthenticatedStream stream) {
          return tlsRef.wrapClient(zc::mv(stream), hostname);
        });
  }

  Own<ConnectionReceiver> listen() override { return tls.wrapPort(inner->listen()); }

  Own<NetworkAddress> clone() override {
    return zc::heap<TlsNetworkAddress>(tls, zc::str(hostname), inner->clone());
  }

  String toString() override { return zc::str("tls:", inner->toString()); }

private:
  TlsContext& tls;
  zc::String hostname;
  zc::Own<zc::NetworkAddress> inner;
};

class TlsNetwork final : public zc::Network {
public:
  TlsNetwork(TlsContext& tls, zc::Network& inner) : tls(tls), inner(inner) {}
  TlsNetwork(TlsContext& tls, zc::Own<zc::Network> inner)
      : tls(tls), inner(*inner), ownInner(zc::mv(inner)) {}

  Promise<Own<NetworkAddress>> parseAddress(StringPtr addr, uint portHint) override {
    // We want to parse the hostname or IP address out of `addr`. This is a bit complicated as
    // ZC's default network implementation has a fairly featureful grammar for these things.
    // In particular, we cannot just split on ':' because the address might be IPv6.

    zc::String hostname;

    if (addr.startsWith("[")) {
      // IPv6, like "[1234:5678::abcd]:123". Take the part between the brackets.
      ZC_IF_SOME(pos, addr.findFirst(']')) { hostname = zc::str(addr.slice(1, pos)); }
      else {
        // Uhh??? Just take the whole thing, cert will fail later.
        hostname = zc::heapString(addr);
      }
    } else if (addr.startsWith("unix:") || addr.startsWith("unix-abstract:")) {
      // Unfortunately, `unix:123` is ambiguous (maybe there is a host named "unix"?), but the
      // default ZC network implementation will interpret it as a Unix domain socket address.
      // We don't want TLS to then try to authenticate that as a host named "unix".
      ZC_FAIL_REQUIRE("can't authenticate Unix domain socket with TLS", addr);
    } else {
      uint colons = 0;
      for (auto c : addr) {
        if (c == ':') { ++colons; }
      }

      if (colons >= 2) {
        // Must be an IPv6 address. If it had a port, it would have been wrapped in []. So don't
        // strip the port.
        hostname = zc::heapString(addr);
      } else {
        // Assume host:port or ipv4:port. This is a shaky assumption, as the above hacks
        // demonstrate.
        //
        // In theory it might make sense to extend the NetworkAddress interface so that it can tell
        // us what the actual parser decided the hostname is. However, when I tried this it proved
        // rather cumbersome and actually broke code in the Workers Runtime that does complicated
        // stacking of zc::Network implementations.
        ZC_IF_SOME(pos, addr.findFirst(':')) { hostname = zc::heapString(addr.first(pos)); }
        else { hostname = zc::heapString(addr); }
      }
    }

    return inner.parseAddress(addr, portHint)
        .then([this, hostname = zc::mv(hostname)](
                  zc::Own<NetworkAddress>&& addr) mutable -> zc::Own<zc::NetworkAddress> {
          return zc::heap<TlsNetworkAddress>(tls, zc::mv(hostname), zc::mv(addr));
        });
  }

  Own<NetworkAddress> getSockaddr(const void* sockaddr, uint len) override {
    ZC_UNIMPLEMENTED("TLS does not implement getSockaddr() because it needs to know hostnames");
  }

  Own<Network> restrictPeers(zc::ArrayPtr<const zc::StringPtr> allow,
                             zc::ArrayPtr<const zc::StringPtr> deny = nullptr) override {
    // TODO(someday): Maybe we could implement the ability to specify CA or hostname restrictions?
    //   Or is it better to let people do that via the TlsContext? A neat thing about
    //   restrictPeers() is that it's easy to make user-configurable.
    return zc::heap<TlsNetwork>(tls, inner.restrictPeers(allow, deny));
  }

private:
  TlsContext& tls;
  zc::Network& inner;
  zc::Own<zc::Network> ownInner;
};

// =======================================================================================
// class TlsContext

TlsContext::Options::Options()
    : useSystemTrustStore(true),
      verifyClients(false),
      minVersion(TlsVersion::TLS_1_2),
      cipherList(
          "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:"
          "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305") {
}
// Cipher list is Mozilla's "intermediate" list, except with classic DH removed since we don't
// currently support setting dhparams. See:
//     https://mozilla.github.io/server-side-tls/ssl-config-generator/
//
// Classic DH is arguably obsolete and will only become more so as time passes, so perhaps we'll
// never bother.

struct TlsContext::SniCallback {
  // struct SniCallback exists only so that callback() can be declared in the .c++ file, since it
  // references OpenSSL types.

  static int callback(SSL* ssl, int* ad, void* arg);
};

TlsContext::TlsContext(Options options) {
  ensureOpenSslInitialized();

#if OPENSSL_VERSION_NUMBER >= 0x10100000L || defined(OPENSSL_IS_BORINGSSL)
  SSL_CTX* ctx = SSL_CTX_new(TLS_method());
#else
  SSL_CTX* ctx = SSL_CTX_new(SSLv23_method());
#endif

  if (ctx == nullptr) { throwOpensslError(); }
  ZC_ON_SCOPE_FAILURE(SSL_CTX_free(ctx));

  // honor options.useSystemTrustStore
  if (options.useSystemTrustStore) {
    if (!SSL_CTX_set_default_verify_paths(ctx)) { throwOpensslError(); }
#ifdef _WIN32
    updateOpenSSLCAStoreWithWindowsCertificates(ctx);
#endif
  }

  // honor options.trustedCertificates
  if (options.trustedCertificates.size() > 0) {
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    if (store == nullptr) { throwOpensslError(); }
    for (auto& cert : options.trustedCertificates) {
      if (!X509_STORE_add_cert(store, reinterpret_cast<X509*>(cert.chain[0]))) {
        throwOpensslError();
      }
    }
  }

  if (options.verifyClients) {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
  }

  // honor options.minVersion
  long optionFlags = 0;
  if (options.minVersion > TlsVersion::SSL_3) { optionFlags |= SSL_OP_NO_SSLv3; }
  if (options.minVersion > TlsVersion::TLS_1_0) { optionFlags |= SSL_OP_NO_TLSv1; }
  if (options.minVersion > TlsVersion::TLS_1_1) { optionFlags |= SSL_OP_NO_TLSv1_1; }
  if (options.minVersion > TlsVersion::TLS_1_2) { optionFlags |= SSL_OP_NO_TLSv1_2; }
  if (options.minVersion > TlsVersion::TLS_1_3) {
#ifdef SSL_OP_NO_TLSv1_3
    optionFlags |= SSL_OP_NO_TLSv1_3;
#else
    ZC_FAIL_REQUIRE("OpenSSL headers don't support TLS 1.3");
#endif
  }
  SSL_CTX_set_options(ctx, optionFlags);  // note: never fails; returns new options bitmask

  // honor options.cipherList
  if (!SSL_CTX_set_cipher_list(ctx, options.cipherList.cStr())) { throwOpensslError(); }

  ZC_IF_SOME(curves, options.curveList) {
    if (!SSL_CTX_set1_curves_list(ctx, curves.cStr())) { throwOpensslError(); }
  }

  // honor options.defaultKeypair
  ZC_IF_SOME(kp, options.defaultKeypair) {
    if (!SSL_CTX_use_PrivateKey(ctx, reinterpret_cast<EVP_PKEY*>(kp.privateKey.pkey))) {
      throwOpensslError();
    }

    if (!SSL_CTX_use_certificate(ctx, reinterpret_cast<X509*>(kp.certificate.chain[0]))) {
      throwOpensslError();
    }

    for (size_t i = 1; i < zc::size(kp.certificate.chain); i++) {
      X509* x509 = reinterpret_cast<X509*>(kp.certificate.chain[i]);
      if (x509 == nullptr) break;  // end of chain

      if (!SSL_CTX_add_extra_chain_cert(ctx, x509)) { throwOpensslError(); }

      // SSL_CTX_add_extra_chain_cert() does NOT up the refcount itself.
      X509_up_ref(x509);
    }
  }

  // honor options.sniCallback
  ZC_IF_SOME(sni, options.sniCallback) {
    SSL_CTX_set_tlsext_servername_callback(ctx, &SniCallback::callback);
    SSL_CTX_set_tlsext_servername_arg(ctx, &sni);
  }

  ZC_IF_SOME(timeout, options.acceptTimeout) {
    this->timer = ZC_REQUIRE_NONNULL(options.timer,
                                     "acceptTimeout option requires that a timer is also provided");
    this->acceptTimeout = timeout;
  }

  this->acceptErrorHandler = zc::mv(options.acceptErrorHandler);

  this->ctx = ctx;
}

int TlsContext::SniCallback::callback(SSL* ssl, int* ad, void* arg) {
  // The third parameter is actually type TlsSniCallback*.

  ZC_IF_SOME(
      exception, zc::runCatchingExceptions([&]() {
        TlsSniCallback& sni = *reinterpret_cast<TlsSniCallback*>(arg);

        const char* name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
        if (name != nullptr) {
          ZC_IF_SOME(kp, sni.getKey(name)) {
            if (!SSL_use_PrivateKey(ssl, reinterpret_cast<EVP_PKEY*>(kp.privateKey.pkey))) {
              throwOpensslError();
            }

            if (!SSL_use_certificate(ssl, reinterpret_cast<X509*>(kp.certificate.chain[0]))) {
              throwOpensslError();
            }

            if (!SSL_clear_chain_certs(ssl)) { throwOpensslError(); }

            for (size_t i = 1; i < zc::size(kp.certificate.chain); i++) {
              X509* x509 = reinterpret_cast<X509*>(kp.certificate.chain[i]);
              if (x509 == nullptr) break;  // end of chain

              if (!SSL_add0_chain_cert(ssl, x509)) { throwOpensslError(); }

              // SSL_add0_chain_cert() does NOT up the refcount itself.
              X509_up_ref(x509);
            }
          }
        }
      })) {
    ZC_LOG(ERROR, "exception when invoking SNI callback", exception);
    *ad = SSL_AD_INTERNAL_ERROR;
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  return SSL_TLSEXT_ERR_OK;
}

TlsContext::~TlsContext() noexcept(false) { SSL_CTX_free(reinterpret_cast<SSL_CTX*>(ctx)); }

zc::Promise<zc::Own<zc::AsyncIoStream>> TlsContext::wrapClient(
    zc::Own<zc::AsyncIoStream> stream, zc::StringPtr expectedServerHostname) {
  auto conn = zc::heap<TlsConnection>(zc::mv(stream), reinterpret_cast<SSL_CTX*>(ctx));
  auto promise = conn->connect(expectedServerHostname);
  return promise.then(
      [conn = zc::mv(conn)]() mutable -> zc::Own<zc::AsyncIoStream> { return zc::mv(conn); });
}

zc::Promise<zc::Own<zc::AsyncIoStream>> TlsContext::wrapServer(zc::Own<zc::AsyncIoStream> stream) {
  auto conn = zc::heap<TlsConnection>(zc::mv(stream), reinterpret_cast<SSL_CTX*>(ctx));
  auto promise = conn->accept();
  ZC_IF_SOME(timeout, acceptTimeout) {
    promise = ZC_REQUIRE_NONNULL(timer)
                  .afterDelay(timeout)
                  .then([]() -> zc::Promise<void> {
                    return ZC_EXCEPTION(DISCONNECTED,
                                        "timed out waiting for client during TLS handshake");
                  })
                  .exclusiveJoin(zc::mv(promise));
  }
  return promise.then(
      [conn = zc::mv(conn)]() mutable -> zc::Own<zc::AsyncIoStream> { return zc::mv(conn); });
}

zc::Promise<zc::AuthenticatedStream> TlsContext::wrapClient(zc::AuthenticatedStream stream,
                                                            zc::StringPtr expectedServerHostname) {
  auto conn = zc::heap<TlsConnection>(zc::mv(stream.stream), reinterpret_cast<SSL_CTX*>(ctx));
  auto promise = conn->connect(expectedServerHostname);
  return promise.then([conn = zc::mv(conn), innerId = zc::mv(stream.peerIdentity)]() mutable {
    auto id = conn->getIdentity(zc::mv(innerId));
    return zc::AuthenticatedStream{zc::mv(conn), zc::mv(id)};
  });
}

zc::Promise<zc::AuthenticatedStream> TlsContext::wrapServer(zc::AuthenticatedStream stream) {
  auto conn = zc::heap<TlsConnection>(zc::mv(stream.stream), reinterpret_cast<SSL_CTX*>(ctx));
  auto promise = conn->accept();
  ZC_IF_SOME(timeout, acceptTimeout) {
    promise = ZC_REQUIRE_NONNULL(timer)
                  .afterDelay(timeout)
                  .then([]() -> zc::Promise<void> {
                    return ZC_EXCEPTION(DISCONNECTED,
                                        "timed out waiting for client during TLS handshake");
                  })
                  .exclusiveJoin(zc::mv(promise));
  }
  auto peerId = stream.peerIdentity->toString();
  return promise
      .then([conn = zc::mv(conn), innerId = zc::mv(stream.peerIdentity)]() mutable {
        auto id = conn->getIdentity(zc::mv(innerId));
        return zc::AuthenticatedStream{zc::mv(conn), zc::mv(id)};
      })
      .catch_([peerId = zc::mv(peerId)](zc::Exception&& e) -> zc::Promise<zc::AuthenticatedStream> {
        e.setDescription(zc::str(e.getDescription(), "; clientId = ", peerId));
        zc::throwFatalException(zc::mv(e));
      });
}

zc::Own<zc::ConnectionReceiver> TlsContext::wrapPort(zc::Own<zc::ConnectionReceiver> port) {
  auto handler =
      acceptErrorHandler.map([](TlsErrorHandler& handler) { return handler.reference(); });
  return zc::heap<TlsConnectionReceiver>(*this, zc::mv(port), zc::mv(handler));
}

zc::Own<zc::NetworkAddress> TlsContext::wrapAddress(zc::Own<zc::NetworkAddress> address,
                                                    zc::StringPtr expectedServerHostname) {
  return zc::heap<TlsNetworkAddress>(*this, zc::str(expectedServerHostname), zc::mv(address));
}

zc::Own<zc::Network> TlsContext::wrapNetwork(zc::Network& network) {
  return zc::heap<TlsNetwork>(*this, network);
}

// =======================================================================================
// class TlsPrivateKey

TlsPrivateKey::TlsPrivateKey(zc::ArrayPtr<const byte> asn1) {
  ensureOpenSslInitialized();

  const byte* ptr = asn1.begin();
  pkey = d2i_AutoPrivateKey(nullptr, &ptr, asn1.size());
  if (pkey == nullptr) { throwOpensslError(); }
}

TlsPrivateKey::TlsPrivateKey(zc::StringPtr pem, zc::Maybe<zc::StringPtr> password) {
  ensureOpenSslInitialized();

  // const_cast apparently needed for older versions of OpenSSL.
  BIO* bio = BIO_new_mem_buf(const_cast<char*>(pem.begin()), pem.size());
  ZC_DEFER(BIO_free(bio));

  pkey = PEM_read_bio_PrivateKey(bio, nullptr, &passwordCallback, &password);
  if (pkey == nullptr) { throwOpensslError(); }
}

TlsPrivateKey::TlsPrivateKey(const TlsPrivateKey& other) : pkey(other.pkey) {
  if (pkey != nullptr) EVP_PKEY_up_ref(reinterpret_cast<EVP_PKEY*>(pkey));
}

TlsPrivateKey& TlsPrivateKey::operator=(const TlsPrivateKey& other) {
  if (pkey != other.pkey) {
    EVP_PKEY_free(reinterpret_cast<EVP_PKEY*>(pkey));
    pkey = other.pkey;
    if (pkey != nullptr) EVP_PKEY_up_ref(reinterpret_cast<EVP_PKEY*>(pkey));
  }
  return *this;
}

TlsPrivateKey::~TlsPrivateKey() noexcept(false) {
  EVP_PKEY_free(reinterpret_cast<EVP_PKEY*>(pkey));
}

int TlsPrivateKey::passwordCallback(char* buf, int size, int rwflag, void* u) {
  auto& password = *reinterpret_cast<zc::Maybe<zc::StringPtr>*>(u);

  ZC_IF_SOME(p, password) {
    int result = zc::min(p.size(), size);
    memcpy(buf, p.begin(), result);
    return result;
  }
  else { return 0; }
}

// =======================================================================================
// class TlsCertificate

TlsCertificate::TlsCertificate(zc::ArrayPtr<const zc::ArrayPtr<const byte>> asn1) {
  ensureOpenSslInitialized();

  ZC_REQUIRE(asn1.size() > 0, "must provide at least one certificate in chain");
  ZC_REQUIRE(asn1.size() <= zc::size(chain), "exceeded maximum certificate chain length of 10");

  memset(chain, 0, sizeof(chain));

  for (auto i : zc::indices(asn1)) {
    auto p = asn1[i].begin();

    // "_AUX" apparently refers to some auxiliary information that can be appended to the
    // certificate, but should only be trusted for your own certificate, not the whole chain??
    // I don't really know, I'm just cargo-culting.
    chain[i] =
        i == 0 ? d2i_X509_AUX(nullptr, &p, asn1[i].size()) : d2i_X509(nullptr, &p, asn1[i].size());

    if (chain[i] == nullptr) {
      for (size_t j = 0; j < i; j++) { X509_free(reinterpret_cast<X509*>(chain[j])); }
      throwOpensslError();
    }
  }
}

TlsCertificate::TlsCertificate(zc::ArrayPtr<const byte> asn1)
    : TlsCertificate(zc::arrayPtr(&asn1, 1)) {}

TlsCertificate::TlsCertificate(zc::StringPtr pem) {
  ensureOpenSslInitialized();

  memset(chain, 0, sizeof(chain));

  // const_cast apparently needed for older versions of OpenSSL.
  BIO* bio = BIO_new_mem_buf(const_cast<char*>(pem.begin()), pem.size());
  ZC_DEFER(BIO_free(bio));

  for (auto i : zc::indices(chain)) {
    // "_AUX" apparently refers to some auxiliary information that can be appended to the
    // certificate, but should only be trusted for your own certificate, not the whole chain??
    // I don't really know, I'm just cargo-culting.
    chain[i] = i == 0 ? PEM_read_bio_X509_AUX(bio, nullptr, nullptr, nullptr)
                      : PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);

    if (chain[i] == nullptr) {
      auto error = ERR_peek_last_error();
      if (i > 0 && ERR_GET_LIB(error) == ERR_LIB_PEM &&
          ERR_GET_REASON(error) == PEM_R_NO_START_LINE) {
        // EOF; we're done.
        ERR_clear_error();
        return;
      } else {
        for (size_t j = 0; j < i; j++) { X509_free(reinterpret_cast<X509*>(chain[j])); }
        throwOpensslError();
      }
    }
  }

  // We reached the chain length limit. Try to read one more to verify that the chain ends here.
  X509* dummy = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  if (dummy != nullptr) {
    X509_free(dummy);
    for (auto i : zc::indices(chain)) { X509_free(reinterpret_cast<X509*>(chain[i])); }
    ZC_FAIL_REQUIRE("exceeded maximum certificate chain length of 10");
  }
}

TlsCertificate::TlsCertificate(const TlsCertificate& other) {
  memcpy(chain, other.chain, sizeof(chain));
  for (void* p : chain) {
    if (p == nullptr) break;  // end of chain; quit early
    X509_up_ref(reinterpret_cast<X509*>(p));
  }
}

TlsCertificate& TlsCertificate::operator=(const TlsCertificate& other) {
  for (auto i : zc::indices(chain)) {
    if (chain[i] != other.chain[i]) {
      EVP_PKEY_free(reinterpret_cast<EVP_PKEY*>(chain[i]));
      chain[i] = other.chain[i];
      if (chain[i] != nullptr) X509_up_ref(reinterpret_cast<X509*>(chain[i]));
    } else if (chain[i] == nullptr) {
      // end of both chains; quit early
      break;
    }
  }
  return *this;
}

TlsCertificate::~TlsCertificate() noexcept(false) {
  for (void* p : chain) {
    if (p == nullptr) break;  // end of chain; quit early
    X509_free(reinterpret_cast<X509*>(p));
  }
}

// =======================================================================================
// class TlsPeerIdentity

TlsPeerIdentity::~TlsPeerIdentity() noexcept(false) {
  if (cert != nullptr) { X509_free(reinterpret_cast<X509*>(cert)); }
}

zc::String TlsPeerIdentity::toString() {
  if (hasCertificate()) {
    return getCommonName();
  } else {
    return zc::str("(anonymous client)");
  }
}

zc::String TlsPeerIdentity::getCommonName() {
  if (cert == nullptr) {
    ZC_FAIL_REQUIRE("client did not provide a certificate") { return nullptr; }
  }

  X509_NAME* subj = X509_get_subject_name(reinterpret_cast<X509*>(cert));

  int index = X509_NAME_get_index_by_NID(subj, NID_commonName, -1);
  ZC_ASSERT(index != -1, "certificate has no common name?");
  X509_NAME_ENTRY* entry = X509_NAME_get_entry(subj, index);
  ZC_ASSERT(entry != nullptr);
  ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
  ZC_ASSERT(data != nullptr);

  unsigned char* out = nullptr;
  int len = ASN1_STRING_to_UTF8(&out, data);
  ZC_ASSERT(len >= 0);
  ZC_DEFER(OPENSSL_free(out));

  return zc::heapString(reinterpret_cast<char*>(out), len);
}

}  // namespace zc

#endif  // ZC_HAS_OPENSSL
