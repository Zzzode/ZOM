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

#if _WIN32
// Request Vista-level APIs.
#include "zc/core/win32-api-version.h"
#elif !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <sys/types.h>
#include <zc/core/filesystem.h>
#include <zc/core/time.h>
#include <zc/ztest/gtest.h>

#include "zc/async/async-io-internal.h"
#include "zc/async/async-io.h"
#include "zc/core/cidr.h"
#include "zc/core/debug.h"
#include "zc/core/io.h"
#include "zc/core/miniposix.h"
#if _WIN32
#include <ws2tcpip.h>

#include "zc/core/windows-sanity.h"
#define inet_pton InetPtonA
#define inet_ntop InetNtopA
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace zc {
namespace {

TEST(AsyncIo, SimpleNetwork) {
  auto ioContext = setupAsyncIo();
  auto& network = ioContext.provider->getNetwork();

  Own<ConnectionReceiver> listener;
  Own<AsyncIoStream> server;
  Own<AsyncIoStream> client;

  char receiveBuffer[4]{};

  auto port = newPromiseAndFulfiller<uint>();

  port.promise.then([&](uint portnum) { return network.parseAddress("localhost", portnum); })
      .then([&](Own<NetworkAddress>&& result) { return result->connect(); })
      .then([&](Own<AsyncIoStream>&& result) {
        client = zc::mv(result);
        return client->write("foo"_zcb);
      })
      .detach([](zc::Exception&& exception) { ZC_FAIL_EXPECT(exception); });

  zc::String result = network.parseAddress("*")
                          .then([&](Own<NetworkAddress>&& result) {
                            listener = result->listen();
                            port.fulfiller->fulfill(listener->getPort());
                            return listener->accept();
                          })
                          .then([&](Own<AsyncIoStream>&& result) {
                            server = zc::mv(result);
                            return server->tryRead(receiveBuffer, 3, 4);
                          })
                          .then([&](size_t n) {
                            EXPECT_EQ(3u, n);
                            return heapString(receiveBuffer, n);
                          })
                          .wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
}

#if !_WIN32  // TODO(someday): Implement NetworkPeerIdentity for Win32.
TEST(AsyncIo, SimpleNetworkAuthentication) {
  auto ioContext = setupAsyncIo();
  auto& network = ioContext.provider->getNetwork();

  Own<ConnectionReceiver> listener;
  Own<AsyncIoStream> server;
  Own<AsyncIoStream> client;

  char receiveBuffer[4]{};

  auto port = newPromiseAndFulfiller<uint>();

  port.promise.then([&](uint portnum) { return network.parseAddress("localhost", portnum); })
      .then([&](Own<NetworkAddress>&& addr) {
        auto promise = addr->connectAuthenticated();
        return promise.then([&, addr = zc::mv(addr)](AuthenticatedStream result) mutable {
          auto id = result.peerIdentity.downcast<NetworkPeerIdentity>();

          // `addr` was resolved from `localhost` and may contain multiple addresses, but
          // result.peerIdentity tells us the specific address that was used. So it should be one
          // of the ones on the list, but only one.
          ZC_EXPECT(addr->toString().contains(id->getAddress().toString()));
          ZC_EXPECT(id->getAddress().toString().findFirst(',') == zc::none);

          client = zc::mv(result.stream);

          // `id` should match client->getpeername().
          union {
            struct sockaddr generic;
            struct sockaddr_in ip4;
            struct sockaddr_in6 ip6;
          } rawAddr;
          uint len = sizeof(rawAddr);
          client->getpeername(&rawAddr.generic, &len);
          auto peername = network.getSockaddr(&rawAddr.generic, len);
          ZC_EXPECT(id->toString() == peername->toString());

          return client->write("foo"_zcb);
        });
      })
      .detach([](zc::Exception&& exception) { ZC_FAIL_EXPECT(exception); });

  zc::String result = network.parseAddress("*")
                          .then([&](Own<NetworkAddress>&& result) {
                            listener = result->listen();
                            port.fulfiller->fulfill(listener->getPort());
                            return listener->acceptAuthenticated();
                          })
                          .then([&](AuthenticatedStream result) {
                            auto id = result.peerIdentity.downcast<NetworkPeerIdentity>();
                            server = zc::mv(result.stream);

                            // `id` should match server->getpeername().
                            union {
                              struct sockaddr generic;
                              struct sockaddr_in ip4;
                              struct sockaddr_in6 ip6;
                            } addr;
                            uint len = sizeof(addr);
                            server->getpeername(&addr.generic, &len);
                            auto peername = network.getSockaddr(&addr.generic, len);
                            ZC_EXPECT(id->toString() == peername->toString());

                            return server->tryRead(receiveBuffer, 3, 4);
                          })
                          .then([&](size_t n) {
                            EXPECT_EQ(3u, n);
                            return heapString(receiveBuffer, n);
                          })
                          .wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
}
#endif

#if !_WIN32 && !__CYGWIN__  // TODO(someday): Debug why this deadlocks on Cygwin.

#if __ANDROID__
#define TMPDIR "/data/local/tmp"
#else
#define TMPDIR "/tmp"
#endif

TEST(AsyncIo, UnixSocket) {
  auto ioContext = setupAsyncIo();
  auto& network = ioContext.provider->getNetwork();

  auto path = zc::str(TMPDIR "/zc-async-io-test.", getpid());
  ZC_DEFER(unlink(path.cStr()));

  Own<ConnectionReceiver> listener;
  Own<AsyncIoStream> server;
  Own<AsyncIoStream> client;

  char receiveBuffer[4]{};

  auto ready = newPromiseAndFulfiller<void>();

  ready.promise.then([&]() { return network.parseAddress(zc::str("unix:", path)); })
      .then([&](Own<NetworkAddress>&& addr) {
        auto promise = addr->connectAuthenticated();
        return promise.then([&, addr = zc::mv(addr)](AuthenticatedStream result) mutable {
          auto id = result.peerIdentity.downcast<LocalPeerIdentity>();
          auto creds = id->getCredentials();
          ZC_IF_SOME(p, creds.pid) {
            ZC_EXPECT(p == getpid());
#if __linux__ || __APPLE__
          }
          else {
            ZC_FAIL_EXPECT("LocalPeerIdentity for unix socket had null PID");
#endif
          }
          ZC_IF_SOME(u, creds.uid) { ZC_EXPECT(u == getuid()); }
          else { ZC_FAIL_EXPECT("LocalPeerIdentity for unix socket had null UID"); }

          client = zc::mv(result.stream);
          return client->write("foo"_zcb);
        });
      })
      .detach([](zc::Exception&& exception) { ZC_FAIL_EXPECT(exception); });

  zc::String result = network.parseAddress(zc::str("unix:", path))
                          .then([&](Own<NetworkAddress>&& result) {
                            listener = result->listen();
                            ready.fulfiller->fulfill();
                            return listener->acceptAuthenticated();
                          })
                          .then([&](AuthenticatedStream result) {
                            auto id = result.peerIdentity.downcast<LocalPeerIdentity>();
                            auto creds = id->getCredentials();
                            ZC_IF_SOME(p, creds.pid) {
                              ZC_EXPECT(p == getpid());
#if __linux__ || __APPLE__
                            }
                            else {
                              ZC_FAIL_EXPECT("LocalPeerIdentity for unix socket had null PID");
#endif
                            }
                            ZC_IF_SOME(u, creds.uid) { ZC_EXPECT(u == getuid()); }
                            else {
                              ZC_FAIL_EXPECT("LocalPeerIdentity for unix socket had null UID");
                            }

                            server = zc::mv(result.stream);
                            return server->tryRead(receiveBuffer, 3, 4);
                          })
                          .then([&](size_t n) {
                            EXPECT_EQ(3u, n);
                            return heapString(receiveBuffer, n);
                          })
                          .wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
}

TEST(AsyncIo, AncillaryMessageHandlerNoMsg) {
  auto ioContext = setupAsyncIo();
  auto& network = ioContext.provider->getNetwork();

  Own<ConnectionReceiver> listener;
  Own<AsyncIoStream> server;
  Own<AsyncIoStream> client;

  char receiveBuffer[4]{};

  bool clientHandlerCalled = false;
  zc::Function<void(zc::ArrayPtr<AncillaryMessage>)> clientHandler =
      [&](zc::ArrayPtr<AncillaryMessage>) { clientHandlerCalled = true; };
  bool serverHandlerCalled = false;
  zc::Function<void(zc::ArrayPtr<AncillaryMessage>)> serverHandler =
      [&](zc::ArrayPtr<AncillaryMessage>) { serverHandlerCalled = true; };

  auto port = newPromiseAndFulfiller<uint>();

  port.promise.then([&](uint portnum) { return network.parseAddress("localhost", portnum); })
      .then([&](Own<NetworkAddress>&& addr) {
        auto promise = addr->connectAuthenticated();
        return promise.then([&, addr = zc::mv(addr)](AuthenticatedStream result) mutable {
          client = zc::mv(result.stream);
          client->registerAncillaryMessageHandler(zc::mv(clientHandler));
          return client->write("foo"_zcb);
        });
      })
      .detach([](zc::Exception&& exception) { ZC_FAIL_EXPECT(exception); });

  zc::String result = network.parseAddress("*")
                          .then([&](Own<NetworkAddress>&& result) {
                            listener = result->listen();
                            port.fulfiller->fulfill(listener->getPort());
                            return listener->acceptAuthenticated();
                          })
                          .then([&](AuthenticatedStream result) {
                            server = zc::mv(result.stream);
                            server->registerAncillaryMessageHandler(zc::mv(serverHandler));
                            return server->tryRead(receiveBuffer, 3, 4);
                          })
                          .then([&](size_t n) {
                            EXPECT_EQ(3u, n);
                            return heapString(receiveBuffer, n);
                          })
                          .wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
  EXPECT_FALSE(clientHandlerCalled);
  EXPECT_FALSE(serverHandlerCalled);
}
#endif

// This test uses SO_TIMESTAMP on a SOCK_STREAM, which is only supported by Linux. Ideally we'd
// rewrite the test to use some other message type that is widely supported on streams. But for
// now we just limit the test to Linux. Also, it doesn't work on Android for some reason, and it
// isn't worth investigating, so we skip it there.
#if __linux__ && !__ANDROID__
TEST(AsyncIo, AncillaryMessageHandler) {
  auto ioContext = setupAsyncIo();
  auto& network = ioContext.provider->getNetwork();

  Own<ConnectionReceiver> listener;
  Own<AsyncIoStream> server;
  Own<AsyncIoStream> client;

  char receiveBuffer[4]{};

  bool clientHandlerCalled = false;
  zc::Function<void(zc::ArrayPtr<AncillaryMessage>)> clientHandler =
      [&](zc::ArrayPtr<AncillaryMessage>) { clientHandlerCalled = true; };
  bool serverHandlerCalled = false;
  zc::Function<void(zc::ArrayPtr<AncillaryMessage>)> serverHandler =
      [&](zc::ArrayPtr<AncillaryMessage> msgs) {
        serverHandlerCalled = true;
        EXPECT_EQ(1, msgs.size());
        EXPECT_EQ(SOL_SOCKET, msgs[0].getLevel());
        EXPECT_EQ(SO_TIMESTAMP, msgs[0].getType());
      };

  auto port = newPromiseAndFulfiller<uint>();

  port.promise.then([&](uint portnum) { return network.parseAddress("localhost", portnum); })
      .then([&](Own<NetworkAddress>&& addr) {
        auto promise = addr->connectAuthenticated();
        return promise.then([&, addr = zc::mv(addr)](AuthenticatedStream result) mutable {
          client = zc::mv(result.stream);
          client->registerAncillaryMessageHandler(zc::mv(clientHandler));
          return client->write("foo"_zcb);
        });
      })
      .detach([](zc::Exception&& exception) { ZC_FAIL_EXPECT(exception); });

  zc::String result = network.parseAddress("*")
                          .then([&](Own<NetworkAddress>&& result) {
                            listener = result->listen();
                            // Register interest in having the timestamp delivered via cmsg on each
                            // recvmsg.
                            int yes = 1;
                            listener->setsockopt(SOL_SOCKET, SO_TIMESTAMP, &yes, sizeof(yes));
                            port.fulfiller->fulfill(listener->getPort());
                            return listener->acceptAuthenticated();
                          })
                          .then([&](AuthenticatedStream result) {
                            server = zc::mv(result.stream);
                            server->registerAncillaryMessageHandler(zc::mv(serverHandler));
                            return server->tryRead(receiveBuffer, 3, 4);
                          })
                          .then([&](size_t n) {
                            EXPECT_EQ(3u, n);
                            return heapString(receiveBuffer, n);
                          })
                          .wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
  EXPECT_FALSE(clientHandlerCalled);
  EXPECT_TRUE(serverHandlerCalled);
}
#endif

String tryParse(WaitScope& waitScope, Network& network, StringPtr text, uint portHint = 0) {
  return network.parseAddress(text, portHint).wait(waitScope)->toString();
}

bool systemSupportsAddress(StringPtr addr, StringPtr service = nullptr) {
  // Can getaddrinfo() parse this addresses? This is only true if the address family (e.g., ipv6)
  // is configured on at least one interface. (The loopback interface usually has both ipv4 and
  // ipv6 configured, but not always.)
  struct addrinfo hints;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = 0;
#if !defined(AI_V4MAPPED)
  hints.ai_flags = AI_ADDRCONFIG;
#else
  hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;
#endif
  hints.ai_protocol = 0;
  hints.ai_canonname = nullptr;
  hints.ai_addr = nullptr;
  hints.ai_next = nullptr;
  struct addrinfo* list;
  int status =
      getaddrinfo(addr.cStr(), service == nullptr ? nullptr : service.cStr(), &hints, &list);
  if (status == 0) {
    freeaddrinfo(list);
    return true;
  } else {
    return false;
  }
}

TEST(AsyncIo, AddressParsing) {
  auto ioContext = setupAsyncIo();
  auto& w = ioContext.waitScope;
  auto& network = ioContext.provider->getNetwork();

  EXPECT_EQ("*:0", tryParse(w, network, "*"));
  EXPECT_EQ("*:123", tryParse(w, network, "*:123"));
  EXPECT_EQ("0.0.0.0:0", tryParse(w, network, "0.0.0.0"));
  EXPECT_EQ("1.2.3.4:5678", tryParse(w, network, "1.2.3.4", 5678));

#if !_WIN32
  EXPECT_EQ("unix:foo/bar/baz", tryParse(w, network, "unix:foo/bar/baz"));
  EXPECT_EQ("unix-abstract:foo/bar/baz", tryParse(w, network, "unix-abstract:foo/bar/baz"));
#endif

  // We can parse services by name...
  //
  // For some reason, Android and some various Linux distros do not support service names.
  if (systemSupportsAddress("1.2.3.4", "http")) {
    EXPECT_EQ("1.2.3.4:80", tryParse(w, network, "1.2.3.4:http", 5678));
    EXPECT_EQ("*:80", tryParse(w, network, "*:http", 5678));
  } else {
    ZC_LOG(WARNING, "system does not support resolving service names on ipv4; skipping tests");
  }

  // IPv6 tests. Annoyingly, these don't work on machines that don't have IPv6 configured on any
  // interfaces.
  if (systemSupportsAddress("::")) {
    EXPECT_EQ("[::]:123", tryParse(w, network, "0::0", 123));
    EXPECT_EQ("[12ab:cd::34]:321", tryParse(w, network, "[12ab:cd:0::0:34]:321", 432));
    if (systemSupportsAddress("12ab:cd::34", "http")) {
      EXPECT_EQ("[::]:80", tryParse(w, network, "[::]:http", 5678));
      EXPECT_EQ("[12ab:cd::34]:80", tryParse(w, network, "[12ab:cd::34]:http", 5678));
    } else {
      ZC_LOG(WARNING, "system does not support resolving service names on ipv6; skipping tests");
    }
  } else {
    ZC_LOG(WARNING, "system does not support ipv6; skipping tests");
  }

  // It would be nice to test DNS lookup here but the test would not be very hermetic.  Even
  // localhost can map to different addresses depending on whether IPv6 is enabled.  We do
  // connect to "localhost" in a different test, though.
}

TEST(AsyncIo, OneWayPipe) {
  auto ioContext = setupAsyncIo();

  auto pipe = ioContext.provider->newOneWayPipe();
  char receiveBuffer[4]{};

  pipe.out->write("foo"_zcb).detach([](zc::Exception&& exception) { ZC_FAIL_EXPECT(exception); });

  zc::String result = pipe.in->tryRead(receiveBuffer, 3, 4)
                          .then([&](size_t n) {
                            EXPECT_EQ(3u, n);
                            return heapString(receiveBuffer, n);
                          })
                          .wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
}

TEST(AsyncIo, TwoWayPipe) {
  auto ioContext = setupAsyncIo();

  auto pipe = ioContext.provider->newTwoWayPipe();
  char receiveBuffer1[4]{};
  char receiveBuffer2[4]{};

  auto promise = pipe.ends[0]
                     ->write("foo"_zcb)
                     .then([&]() { return pipe.ends[0]->tryRead(receiveBuffer1, 3, 4); })
                     .then([&](size_t n) {
                       EXPECT_EQ(3u, n);
                       return heapString(receiveBuffer1, n);
                     });

  zc::String result = pipe.ends[1]
                          ->write("bar"_zcb)
                          .then([&]() { return pipe.ends[1]->tryRead(receiveBuffer2, 3, 4); })
                          .then([&](size_t n) {
                            EXPECT_EQ(3u, n);
                            return heapString(receiveBuffer2, n);
                          })
                          .wait(ioContext.waitScope);

  zc::String result2 = promise.wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
  EXPECT_EQ("bar", result2);
}

TEST(AsyncIo, InMemoryCapabilityPipe) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto pipe = newCapabilityPipe();
  auto pipe2 = newCapabilityPipe();
  char receiveBuffer1[4]{};
  char receiveBuffer2[4]{};

  // Expect to receive a stream, then read "foo" from it, then write "bar" to it.
  Own<AsyncCapabilityStream> receivedStream;
  auto promise = pipe2.ends[1]
                     ->receiveStream()
                     .then([&](Own<AsyncCapabilityStream> stream) {
                       receivedStream = zc::mv(stream);
                       return receivedStream->tryRead(receiveBuffer2, 3, 4);
                     })
                     .then([&](size_t n) {
                       EXPECT_EQ(3u, n);
                       return receivedStream->write("bar"_zcb).then(
                           [&receiveBuffer2, n]() { return heapString(receiveBuffer2, n); });
                     });

  // Send a stream, then write "foo" to the other end of the sent stream, then receive "bar"
  // from it.
  zc::String result = pipe2.ends[0]
                          ->sendStream(zc::mv(pipe.ends[1]))
                          .then([&]() { return pipe.ends[0]->write("foo"_zcb); })
                          .then([&]() { return pipe.ends[0]->tryRead(receiveBuffer1, 3, 4); })
                          .then([&](size_t n) {
                            EXPECT_EQ(3u, n);
                            return heapString(receiveBuffer1, n);
                          })
                          .wait(waitScope);

  zc::String result2 = promise.wait(waitScope);

  EXPECT_EQ("bar", result);
  EXPECT_EQ("foo", result2);
}

#if !_WIN32 && !__CYGWIN__
TEST(AsyncIo, CapabilityPipe) {
  auto ioContext = setupAsyncIo();

  auto pipe = ioContext.provider->newCapabilityPipe();
  auto pipe2 = ioContext.provider->newCapabilityPipe();
  char receiveBuffer1[4]{};
  char receiveBuffer2[4]{};

  // Expect to receive a stream, then write "bar" to it, then receive "foo" from it.
  Own<AsyncCapabilityStream> receivedStream;
  auto promise = pipe2.ends[1]
                     ->receiveStream()
                     .then([&](Own<AsyncCapabilityStream> stream) {
                       receivedStream = zc::mv(stream);
                       return receivedStream->write("bar"_zcb);
                     })
                     .then([&]() { return receivedStream->tryRead(receiveBuffer2, 3, 4); })
                     .then([&](size_t n) {
                       EXPECT_EQ(3u, n);
                       return heapString(receiveBuffer2, n);
                     });

  // Send a stream, then write "foo" to the other end of the sent stream, then receive "bar"
  // from it.
  zc::String result = pipe2.ends[0]
                          ->sendStream(zc::mv(pipe.ends[1]))
                          .then([&]() { return pipe.ends[0]->write("foo"_zcb); })
                          .then([&]() { return pipe.ends[0]->tryRead(receiveBuffer1, 3, 4); })
                          .then([&](size_t n) {
                            EXPECT_EQ(3u, n);
                            return heapString(receiveBuffer1, n);
                          })
                          .wait(ioContext.waitScope);

  zc::String result2 = promise.wait(ioContext.waitScope);

  EXPECT_EQ("bar", result);
  EXPECT_EQ("foo", result2);
}

TEST(AsyncIo, CapabilityPipeBlockedSendStream) {
  // Check for a bug that existed at one point where if a sendStream() call couldn't complete
  // immediately, it would fail.

  auto io = setupAsyncIo();

  auto pipe = io.provider->newCapabilityPipe();

  Promise<void> promise = nullptr;
  Own<AsyncIoStream> endpoint1;
  uint nonBlockedCount = 0;
  for (;;) {
    auto pipe2 = io.provider->newCapabilityPipe();
    promise = pipe.ends[0]->sendStream(zc::mv(pipe2.ends[0]));
    if (promise.poll(io.waitScope)) {
      // Send completed immediately, because there was enough space in the stream.
      ++nonBlockedCount;
      promise.wait(io.waitScope);
    } else {
      // Send blocked! Let's continue with this promise then!
      endpoint1 = zc::mv(pipe2.ends[1]);
      break;
    }
  }

  for (uint i ZC_UNUSED : zc::zeroTo(nonBlockedCount)) {
    // Receive and ignore all the streams that were sent without blocking.
    pipe.ends[1]->receiveStream().wait(io.waitScope);
  }

  // Now that write that blocked should have been able to complete.
  promise.wait(io.waitScope);

  // Now get the one that blocked.
  auto endpoint2 = pipe.ends[1]->receiveStream().wait(io.waitScope);

  endpoint1->write("foo"_zcb).wait(io.waitScope);
  endpoint1->shutdownWrite();
  ZC_EXPECT(endpoint2->readAllText().wait(io.waitScope) == "foo");
}

TEST(AsyncIo, CapabilityPipeMultiStreamMessage) {
  auto ioContext = setupAsyncIo();

  auto pipe = ioContext.provider->newCapabilityPipe();
  auto pipe2 = ioContext.provider->newCapabilityPipe();
  auto pipe3 = ioContext.provider->newCapabilityPipe();

  auto streams = heapArrayBuilder<Own<AsyncCapabilityStream>>(2);
  streams.add(zc::mv(pipe2.ends[0]));
  streams.add(zc::mv(pipe3.ends[0]));

  ArrayPtr<const byte> secondBuf = "bar"_zcb;
  pipe.ends[0]
      ->writeWithStreams("foo"_zcb, arrayPtr(&secondBuf, 1), streams.finish())
      .wait(ioContext.waitScope);

  char receiveBuffer[7]{};
  Own<AsyncCapabilityStream> receiveStreams[3];
  auto result = pipe.ends[1]
                    ->tryReadWithStreams(receiveBuffer, 6, 7, receiveStreams, 3)
                    .wait(ioContext.waitScope);

  ZC_EXPECT(result.byteCount == 6);
  receiveBuffer[6] = '\0';
  ZC_EXPECT(zc::StringPtr(receiveBuffer) == "foobar");

  ZC_ASSERT(result.capCount == 2);

  receiveStreams[0]->write("baz"_zcb).wait(ioContext.waitScope);
  receiveStreams[0] = nullptr;
  ZC_EXPECT(pipe2.ends[1]->readAllText().wait(ioContext.waitScope) == "baz");

  pipe3.ends[1]->write("qux"_zcb).wait(ioContext.waitScope);
  pipe3.ends[1] = nullptr;
  ZC_EXPECT(receiveStreams[1]->readAllText().wait(ioContext.waitScope) == "qux");
}

TEST(AsyncIo, ScmRightsTruncatedOdd) {
  // Test that if we send two FDs over a unix socket, but the receiving end only receives one, we
  // don't leak the other FD.

  auto io = setupAsyncIo();

  auto capPipe = io.provider->newCapabilityPipe();

  int pipeFds[2]{};
  ZC_SYSCALL(miniposix::pipe(pipeFds));
  zc::AutoCloseFd in1(pipeFds[0]);
  zc::AutoCloseFd out1(pipeFds[1]);

  ZC_SYSCALL(miniposix::pipe(pipeFds));
  zc::AutoCloseFd in2(pipeFds[0]);
  zc::AutoCloseFd out2(pipeFds[1]);

  {
    AutoCloseFd sendFds[2] = {zc::mv(out1), zc::mv(out2)};
    capPipe.ends[0]->writeWithFds("foo"_zcb, nullptr, sendFds).wait(io.waitScope);
  }

  {
    char buffer[4]{};
    AutoCloseFd fdBuffer[1];
    auto result = capPipe.ends[1]->tryReadWithFds(buffer, 3, 3, fdBuffer, 1).wait(io.waitScope);
    ZC_ASSERT(result.capCount == 1);
    zc::FdOutputStream(fdBuffer[0].get()).write("bar"_zcb);
  }

  // We want to carefully verify that out1 and out2 were closed, without deadlocking if they
  // weren't. So we manually set nonblocking mode and then issue read()s.
  ZC_SYSCALL(fcntl(in1, F_SETFL, O_NONBLOCK));
  ZC_SYSCALL(fcntl(in2, F_SETFL, O_NONBLOCK));

  char buffer[4]{};
  ssize_t n;

  // First we read "bar" from in1.
  ZC_NONBLOCKING_SYSCALL(n = read(in1, buffer, 4));
  ZC_ASSERT(n == 3);
  buffer[3] = '\0';
  ZC_ASSERT(zc::StringPtr(buffer) == "bar");

  // Now it should be EOF.
  ZC_NONBLOCKING_SYSCALL(n = read(in1, buffer, 4));
  if (n < 0) { ZC_FAIL_ASSERT("out1 was not closed"); }
  ZC_ASSERT(n == 0);

  // Second pipe should have been closed implicitly because we didn't provide space to receive it.
  ZC_NONBLOCKING_SYSCALL(n = read(in2, buffer, 4));
  if (n < 0) {
    ZC_FAIL_ASSERT(
        "out2 was not closed. This could indicate that your operating system kernel is "
        "buggy and leaks file descriptors when an SCM_RIGHTS message is truncated. FreeBSD was "
        "known to do this until late 2018, while MacOS still has this bug as of this writing in "
        "2019. However, ZC works around the problem on those platforms. You need to enable the "
        "same work-around for your OS -- search for 'SCM_RIGHTS' in zc/async-io-unix.c++.");
  }
  ZC_ASSERT(n == 0);
}

#if !__aarch64__
// This test fails under qemu-user, probably due to a bug in qemu's syscall emulation rather than
// a bug in the kernel. We don't have a good way to detect qemu so we just skip the test on aarch64
// in general.

TEST(AsyncIo, ScmRightsTruncatedEven) {
  // Test that if we send three FDs over a unix socket, but the receiving end only receives two, we
  // don't leak the third FD. This is different from the send-two-receive-one case in that
  // CMSG_SPACE() on many systems rounds up such that there is always space for an even number of
  // FDs. In that case the other test only verifies that our userspace code to close unwanted FDs
  // is correct, whereas *this* test really verifies that the *kernel* properly closes truncated
  // FDs.

  auto io = setupAsyncIo();

  auto capPipe = io.provider->newCapabilityPipe();

  int pipeFds[2]{};
  ZC_SYSCALL(miniposix::pipe(pipeFds));
  zc::AutoCloseFd in1(pipeFds[0]);
  zc::AutoCloseFd out1(pipeFds[1]);

  ZC_SYSCALL(miniposix::pipe(pipeFds));
  zc::AutoCloseFd in2(pipeFds[0]);
  zc::AutoCloseFd out2(pipeFds[1]);

  ZC_SYSCALL(miniposix::pipe(pipeFds));
  zc::AutoCloseFd in3(pipeFds[0]);
  zc::AutoCloseFd out3(pipeFds[1]);

  {
    AutoCloseFd sendFds[3] = {zc::mv(out1), zc::mv(out2), zc::mv(out3)};
    capPipe.ends[0]->writeWithFds("foo"_zcb, nullptr, sendFds).wait(io.waitScope);
  }

  {
    char buffer[4]{};
    AutoCloseFd fdBuffer[2];
    auto result = capPipe.ends[1]->tryReadWithFds(buffer, 3, 3, fdBuffer, 2).wait(io.waitScope);
    ZC_ASSERT(result.capCount == 2);
    zc::FdOutputStream(fdBuffer[0].get()).write("bar"_zcb);
    zc::FdOutputStream(fdBuffer[1].get()).write("baz"_zcb);
  }

  // We want to carefully verify that out1, out2, and out3 were closed, without deadlocking if they
  // weren't. So we manually set nonblocking mode and then issue read()s.
  ZC_SYSCALL(fcntl(in1, F_SETFL, O_NONBLOCK));
  ZC_SYSCALL(fcntl(in2, F_SETFL, O_NONBLOCK));
  ZC_SYSCALL(fcntl(in3, F_SETFL, O_NONBLOCK));

  char buffer[4]{};
  ssize_t n;

  // First we read "bar" from in1.
  ZC_NONBLOCKING_SYSCALL(n = read(in1, buffer, 4));
  ZC_ASSERT(n == 3);
  buffer[3] = '\0';
  ZC_ASSERT(zc::StringPtr(buffer) == "bar");

  // Now it should be EOF.
  ZC_NONBLOCKING_SYSCALL(n = read(in1, buffer, 4));
  if (n < 0) { ZC_FAIL_ASSERT("out1 was not closed"); }
  ZC_ASSERT(n == 0);

  // Next we read "baz" from in2.
  ZC_NONBLOCKING_SYSCALL(n = read(in2, buffer, 4));
  ZC_ASSERT(n == 3);
  buffer[3] = '\0';
  ZC_ASSERT(zc::StringPtr(buffer) == "baz");

  // Now it should be EOF.
  ZC_NONBLOCKING_SYSCALL(n = read(in2, buffer, 4));
  if (n < 0) { ZC_FAIL_ASSERT("out2 was not closed"); }
  ZC_ASSERT(n == 0);

  // Third pipe should have been closed implicitly because we didn't provide space to receive it.
  ZC_NONBLOCKING_SYSCALL(n = read(in3, buffer, 4));
  if (n < 0) {
    ZC_FAIL_ASSERT(
        "out3 was not closed. This could indicate that your operating system kernel is "
        "buggy and leaks file descriptors when an SCM_RIGHTS message is truncated. FreeBSD was "
        "known to do this until late 2018, while MacOS still has this bug as of this writing in "
        "2019. However, ZC works around the problem on those platforms. You need to enable the "
        "same work-around for your OS -- search for 'SCM_RIGHTS' in zc/async-io-unix.c++.");
  }
  ZC_ASSERT(n == 0);
}

#endif  // !__aarch64__

#endif  // !_WIN32 && !__CYGWIN__

TEST(AsyncIo, PipeThread) {
  auto ioContext = setupAsyncIo();

  auto pipeThread = ioContext.provider->newPipeThread(
      [](AsyncIoProvider& ioProvider, AsyncIoStream& stream, WaitScope& waitScope) {
        char buf[4]{};
        stream.write("foo"_zcb).wait(waitScope);
        EXPECT_EQ(3u, stream.tryRead(buf, 3, 4).wait(waitScope));
        EXPECT_EQ("bar", heapString(buf, 3));

        // Expect disconnect.
        EXPECT_EQ(0, stream.tryRead(buf, 1, 1).wait(waitScope));
      });

  char buf[4]{};
  pipeThread.pipe->write("bar"_zcb).wait(ioContext.waitScope);
  EXPECT_EQ(3u, pipeThread.pipe->tryRead(buf, 3, 4).wait(ioContext.waitScope));
  EXPECT_EQ("foo", heapString(buf, 3));
}

TEST(AsyncIo, PipeThreadDisconnects) {
  // Like above, but in this case we expect the main thread to detect the pipe thread disconnecting.

  auto ioContext = setupAsyncIo();

  auto pipeThread = ioContext.provider->newPipeThread(
      [](AsyncIoProvider& ioProvider, AsyncIoStream& stream, WaitScope& waitScope) {
        char buf[4]{};
        stream.write("foo"_zcb).wait(waitScope);
        EXPECT_EQ(3u, stream.tryRead(buf, 3, 4).wait(waitScope));
        EXPECT_EQ("bar", heapString(buf, 3));
      });

  char buf[4]{};
  EXPECT_EQ(3u, pipeThread.pipe->tryRead(buf, 3, 4).wait(ioContext.waitScope));
  EXPECT_EQ("foo", heapString(buf, 3));

  pipeThread.pipe->write("bar"_zcb).wait(ioContext.waitScope);

  // Expect disconnect.
  EXPECT_EQ(0, pipeThread.pipe->tryRead(buf, 1, 1).wait(ioContext.waitScope));
}

TEST(AsyncIo, Timeouts) {
  auto ioContext = setupAsyncIo();

  Timer& timer = ioContext.provider->getTimer();

  auto promise1 = timer.timeoutAfter(10 * MILLISECONDS, zc::Promise<void>(zc::NEVER_DONE));
  auto promise2 = timer.timeoutAfter(100 * MILLISECONDS, zc::Promise<int>(123));

  EXPECT_TRUE(promise1.then([]() { return false; }, [](zc::Exception&& e) { return true; })
                  .wait(ioContext.waitScope));
  EXPECT_EQ(123, promise2.wait(ioContext.waitScope));
}

#if !_WIN32  // datagrams not implemented on win32 yet

bool isMsgTruncBroken() {
  // Detect if the kernel fails to set MSG_TRUNC on recvmsg(). This seems to be the case at least
  // when running an arm64 binary under qemu.

  int fd;
  ZC_SYSCALL(fd = socket(AF_INET, SOCK_DGRAM, 0));
  ZC_DEFER(close(fd));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(0x7f000001);
  ZC_SYSCALL(bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)));

  // Read back the assigned port.
  socklen_t len = sizeof(addr);
  ZC_SYSCALL(getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len));
  ZC_ASSERT(len == sizeof(addr));

  const char* message = "foobar";
  ZC_SYSCALL(sendto(fd, message, strlen(message), 0, reinterpret_cast<struct sockaddr*>(&addr),
                    sizeof(addr)));

  char buf[4]{};
  struct iovec iov;
  iov.iov_base = buf;
  iov.iov_len = 3;
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  ssize_t n;
  ZC_SYSCALL(n = recvmsg(fd, &msg, 0));
  ZC_ASSERT(n == 3);

  buf[3] = 0;
  ZC_ASSERT(zc::StringPtr(buf) == "foo");

  return (msg.msg_flags & MSG_TRUNC) == 0;
}

TEST(AsyncIo, Udp) {
  bool msgTruncBroken = isMsgTruncBroken();

  auto ioContext = setupAsyncIo();

  auto addr = ioContext.provider->getNetwork().parseAddress("127.0.0.1").wait(ioContext.waitScope);

  auto port1 = addr->bindDatagramPort();
  auto port2 = addr->bindDatagramPort();

  auto addr1 = ioContext.provider->getNetwork()
                   .parseAddress("127.0.0.1", port1->getPort())
                   .wait(ioContext.waitScope);
  auto addr2 = ioContext.provider->getNetwork()
                   .parseAddress("127.0.0.1", port2->getPort())
                   .wait(ioContext.waitScope);

  Own<NetworkAddress> receivedAddr;

  {
    // Send a message and receive it.
    EXPECT_EQ(3, port1->send("foo"_zcb, *addr2).wait(ioContext.waitScope));
    auto receiver = port2->makeReceiver();

    receiver->receive().wait(ioContext.waitScope);
    {
      auto content = receiver->getContent();
      EXPECT_EQ("foo", zc::heapString(content.value.asChars()));
      EXPECT_FALSE(content.isTruncated);
    }
    receivedAddr = receiver->getSource().clone();
    EXPECT_EQ(addr1->toString(), receivedAddr->toString());
    {
      auto ancillary = receiver->getAncillary();
      EXPECT_EQ(0, ancillary.value.size());
      EXPECT_FALSE(ancillary.isTruncated);
    }

    // Receive a second message with the same receiver.
    {
      auto promise = receiver->receive();  // This time, start receiving before sending
      EXPECT_EQ(6, port1->send("barbaz"_zcb, *addr2).wait(ioContext.waitScope));
      promise.wait(ioContext.waitScope);
      auto content = receiver->getContent();
      EXPECT_EQ("barbaz", zc::heapString(content.value.asChars()));
      EXPECT_FALSE(content.isTruncated);
    }
  }

  DatagramReceiver::Capacity capacity;
  capacity.content = 8;
  capacity.ancillary = 1024;

  {
    // Send a reply that will be truncated.
    EXPECT_EQ(16, port2->send("0123456789abcdef"_zcb, *receivedAddr).wait(ioContext.waitScope));
    auto recv1 = port1->makeReceiver(capacity);

    recv1->receive().wait(ioContext.waitScope);
    {
      auto content = recv1->getContent();
      EXPECT_EQ("01234567", zc::heapString(content.value.asChars()));
      EXPECT_TRUE(content.isTruncated || msgTruncBroken);
    }
    EXPECT_EQ(addr2->toString(), recv1->getSource().toString());
    {
      auto ancillary = recv1->getAncillary();
      EXPECT_EQ(0, ancillary.value.size());
      EXPECT_FALSE(ancillary.isTruncated);
    }

#if defined(IP_PKTINFO) && !__CYGWIN__ && !__aarch64__
    // Set IP_PKTINFO header and try to receive it.
    //
    // Doesn't work on Cygwin; see: https://cygwin.com/ml/cygwin/2009-01/msg00350.html
    // TODO(someday): Might work on more-recent Cygwin; I'm still testing against 1.7.
    //
    // Doesn't work when running arm64 binaries under QEMU -- in fact, it crashes QEMU. We don't
    // have a good way to test if we're under QEMU so we just skip this test on aarch64.
    int one = 1;
    port1->setsockopt(IPPROTO_IP, IP_PKTINFO, &one, sizeof(one));

    EXPECT_EQ(3, port2->send("foo"_zcb, *addr1).wait(ioContext.waitScope));

    recv1->receive().wait(ioContext.waitScope);
    {
      auto content = recv1->getContent();
      EXPECT_EQ("foo", zc::heapString(content.value.asChars()));
      EXPECT_FALSE(content.isTruncated);
    }
    EXPECT_EQ(addr2->toString(), recv1->getSource().toString());
    {
      auto ancillary = recv1->getAncillary();
      EXPECT_FALSE(ancillary.isTruncated);
      ASSERT_EQ(1, ancillary.value.size());

      auto message = ancillary.value[0];
      EXPECT_EQ(IPPROTO_IP, message.getLevel());
      EXPECT_EQ(IP_PKTINFO, message.getType());
      EXPECT_EQ(sizeof(struct in_pktinfo), message.asArray<byte>().size());
      auto& pktinfo = ZC_ASSERT_NONNULL(message.as<struct in_pktinfo>());
      EXPECT_EQ(htonl(0x7F000001), pktinfo.ipi_addr.s_addr);  // 127.0.0.1
    }

    // See what happens if there's not quite enough space for in_pktinfo.
    capacity.ancillary = CMSG_SPACE(sizeof(struct in_pktinfo)) - 8;
    recv1 = port1->makeReceiver(capacity);

    EXPECT_EQ(3, port2->send("bar"_zcb, *addr1).wait(ioContext.waitScope));

    recv1->receive().wait(ioContext.waitScope);
    {
      auto content = recv1->getContent();
      EXPECT_EQ("bar", zc::heapString(content.value.asChars()));
      EXPECT_FALSE(content.isTruncated);
    }
    EXPECT_EQ(addr2->toString(), recv1->getSource().toString());
    {
      auto ancillary = recv1->getAncillary();
      EXPECT_TRUE(ancillary.isTruncated || msgTruncBroken);

      // We might get a message, but it will be truncated.
      if (ancillary.value.size() != 0) {
        EXPECT_EQ(1, ancillary.value.size());

        auto message = ancillary.value[0];
        EXPECT_EQ(IPPROTO_IP, message.getLevel());
        EXPECT_EQ(IP_PKTINFO, message.getType());

        EXPECT_TRUE(message.as<struct in_pktinfo>() == zc::none);
        EXPECT_LT(message.asArray<byte>().size(), sizeof(struct in_pktinfo));
      }
    }

#if __APPLE__
// On MacOS, `CMSG_SPACE(0)` triggers a bogus warning.
#pragma GCC diagnostic ignored "-Wnull-pointer-arithmetic"
#endif
    // See what happens if there's not enough space even for the cmsghdr.
    capacity.ancillary = CMSG_SPACE(0) - 8;
    recv1 = port1->makeReceiver(capacity);

    EXPECT_EQ(3, port2->send("baz"_zcb, *addr1).wait(ioContext.waitScope));

    recv1->receive().wait(ioContext.waitScope);
    {
      auto content = recv1->getContent();
      EXPECT_EQ("baz", zc::heapString(content.value.asChars()));
      EXPECT_FALSE(content.isTruncated);
    }
    EXPECT_EQ(addr2->toString(), recv1->getSource().toString());
    {
      auto ancillary = recv1->getAncillary();
      EXPECT_TRUE(ancillary.isTruncated);
      EXPECT_EQ(0, ancillary.value.size());
    }
#endif
  }
}

#endif  // !_WIN32

#ifdef __linux__  // Abstract unix sockets are only supported on Linux

TEST(AsyncIo, AbstractUnixSocket) {
  auto ioContext = setupAsyncIo();
  auto& network = ioContext.provider->getNetwork();
  auto elapsedSinceEpoch = systemPreciseMonotonicClock().now() - zc::origin<TimePoint>();
  auto address = zc::str("unix-abstract:foo", getpid(), elapsedSinceEpoch / zc::NANOSECONDS);

  Own<NetworkAddress> addr = network.parseAddress(address).wait(ioContext.waitScope);

  Own<ConnectionReceiver> listener = addr->listen();
  // chdir proves no filesystem dependence. Test fails for regular unix socket
  // but passes for abstract unix socket.
  int originalDirFd;
  ZC_SYSCALL(originalDirFd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  ZC_DEFER(close(originalDirFd));
  ZC_SYSCALL(chdir("/"));
  ZC_DEFER(ZC_SYSCALL(fchdir(originalDirFd)));

  addr->connect().attach(zc::mv(listener)).wait(ioContext.waitScope);
}

#endif  // __linux__

ZC_TEST("CIDR parsing") {
  ZC_EXPECT(CidrRange("1.2.3.4/16").toString() == "1.2.0.0/16");
  ZC_EXPECT(CidrRange("1.2.255.4/18").toString() == "1.2.192.0/18");
  ZC_EXPECT(CidrRange("1234::abcd:ffff:ffff/98").toString() == "1234::abcd:c000:0/98");

  ZC_EXPECT(CidrRange::inet4({1, 2, 255, 4}, 18).toString() == "1.2.192.0/18");
  ZC_EXPECT(CidrRange::inet6({0x1234, 0x5678}, {0xabcd, 0xffff, 0xffff}, 98).toString() ==
            "1234:5678::abcd:c000:0/98");

  union {
    struct sockaddr addr;
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
  };
  memset(&addr6, 0, sizeof(addr6));

  {
    addr4.sin_family = AF_INET;
    addr4.sin_addr.s_addr = htonl(0x0102dfff);
    ZC_EXPECT(CidrRange("1.2.255.255/18").matches(&addr));
    ZC_EXPECT(!CidrRange("1.2.255.255/19").matches(&addr));
    ZC_EXPECT(CidrRange("1.2.0.0/16").matches(&addr));
    ZC_EXPECT(!CidrRange("1.3.0.0/16").matches(&addr));
    ZC_EXPECT(CidrRange("1.2.223.255/32").matches(&addr));
    ZC_EXPECT(CidrRange("0.0.0.0/0").matches(&addr));
    ZC_EXPECT(!CidrRange("::/0").matches(&addr));
  }

  {
    addr4.sin_family = AF_INET6;
    byte bytes[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    memcpy(addr6.sin6_addr.s6_addr, bytes, 16);
    ZC_EXPECT(CidrRange("0102:03ff::/24").matches(&addr));
    ZC_EXPECT(!CidrRange("0102:02ff::/24").matches(&addr));
    ZC_EXPECT(CidrRange("0102:02ff::/23").matches(&addr));
    ZC_EXPECT(CidrRange("0102:0304:0506:0708:090a:0b0c:0d0e:0f10/128").matches(&addr));
    ZC_EXPECT(CidrRange("::/0").matches(&addr));
    ZC_EXPECT(!CidrRange("0.0.0.0/0").matches(&addr));
  }

  {
    addr4.sin_family = AF_INET6;
    inet_pton(AF_INET6, "::ffff:1.2.223.255", &addr6.sin6_addr);
    ZC_EXPECT(CidrRange("1.2.255.255/18").matches(&addr));
    ZC_EXPECT(!CidrRange("1.2.255.255/19").matches(&addr));
    ZC_EXPECT(CidrRange("1.2.0.0/16").matches(&addr));
    ZC_EXPECT(!CidrRange("1.3.0.0/16").matches(&addr));
    ZC_EXPECT(CidrRange("1.2.223.255/32").matches(&addr));
    ZC_EXPECT(CidrRange("0.0.0.0/0").matches(&addr));
    ZC_EXPECT(CidrRange("::/0").matches(&addr));
  }
}

bool allowed4(_::NetworkFilter& filter, StringPtr addrStr) {
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, addrStr.cStr(), &addr.sin_addr);
  return filter.shouldAllow(reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
}

bool allowed6(_::NetworkFilter& filter, StringPtr addrStr) {
  struct sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  inet_pton(AF_INET6, addrStr.cStr(), &addr.sin6_addr);
  return filter.shouldAllow(reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
}

ZC_TEST("NetworkFilter") {
  _::NetworkFilter base;

  ZC_EXPECT(allowed4(base, "8.8.8.8"));
  ZC_EXPECT(allowed4(base, "240.1.2.3"));

  {
    _::NetworkFilter filter({"public"}, {}, base);

    ZC_EXPECT(allowed4(filter, "8.8.8.8"));
    ZC_EXPECT(!allowed4(filter, "240.1.2.3"));

    ZC_EXPECT(!allowed4(filter, "192.168.0.1"));
    ZC_EXPECT(!allowed4(filter, "10.1.2.3"));
    ZC_EXPECT(!allowed4(filter, "127.0.0.1"));
    ZC_EXPECT(!allowed4(filter, "0.0.0.0"));

    ZC_EXPECT(allowed6(filter, "2400:cb00:2048:1::c629:d7a2"));
    ZC_EXPECT(!allowed6(filter, "fc00::1234"));
    ZC_EXPECT(!allowed6(filter, "::1"));
    ZC_EXPECT(!allowed6(filter, "::"));
  }

  {
    _::NetworkFilter filter({"private"}, {"local"}, base);

    ZC_EXPECT(!allowed4(filter, "8.8.8.8"));
    ZC_EXPECT(!allowed4(filter, "240.1.2.3"));

    ZC_EXPECT(allowed4(filter, "192.168.0.1"));
    ZC_EXPECT(allowed4(filter, "10.1.2.3"));
    ZC_EXPECT(!allowed4(filter, "127.0.0.1"));
    ZC_EXPECT(!allowed4(filter, "0.0.0.0"));

    ZC_EXPECT(!allowed6(filter, "2400:cb00:2048:1::c629:d7a2"));
    ZC_EXPECT(allowed6(filter, "fc00::1234"));
    ZC_EXPECT(!allowed6(filter, "::1"));
    ZC_EXPECT(!allowed6(filter, "::"));
  }

  {
    _::NetworkFilter filter({"1.0.0.0/8", "1.2.3.0/24"}, {"1.2.0.0/16", "1.2.3.4/32"}, base);

    ZC_EXPECT(!allowed4(filter, "8.8.8.8"));
    ZC_EXPECT(!allowed4(filter, "240.1.2.3"));

    ZC_EXPECT(allowed4(filter, "1.0.0.1"));
    ZC_EXPECT(!allowed4(filter, "1.2.2.1"));
    ZC_EXPECT(allowed4(filter, "1.2.3.1"));
    ZC_EXPECT(!allowed4(filter, "1.2.3.4"));
  }

  // Test combinations of public/private/network/local. At one point these were buggy.
  {
    _::NetworkFilter filter({"public", "private"}, {}, base);

    ZC_EXPECT(allowed4(filter, "8.8.8.8"));
    ZC_EXPECT(!allowed4(filter, "240.1.2.3"));

    ZC_EXPECT(allowed4(filter, "192.168.0.1"));
    ZC_EXPECT(allowed4(filter, "10.1.2.3"));
    ZC_EXPECT(allowed4(filter, "127.0.0.1"));
    ZC_EXPECT(allowed4(filter, "0.0.0.0"));

    ZC_EXPECT(allowed6(filter, "2400:cb00:2048:1::c629:d7a2"));
    ZC_EXPECT(allowed6(filter, "fc00::1234"));
    ZC_EXPECT(allowed6(filter, "::1"));
    ZC_EXPECT(allowed6(filter, "::"));
  }

  {
    _::NetworkFilter filter({"network", "local"}, {}, base);

    ZC_EXPECT(allowed4(filter, "8.8.8.8"));
    ZC_EXPECT(!allowed4(filter, "240.1.2.3"));

    ZC_EXPECT(allowed4(filter, "192.168.0.1"));
    ZC_EXPECT(allowed4(filter, "10.1.2.3"));
    ZC_EXPECT(allowed4(filter, "127.0.0.1"));
    ZC_EXPECT(allowed4(filter, "0.0.0.0"));

    ZC_EXPECT(allowed6(filter, "2400:cb00:2048:1::c629:d7a2"));
    ZC_EXPECT(allowed6(filter, "fc00::1234"));
    ZC_EXPECT(allowed6(filter, "::1"));
    ZC_EXPECT(allowed6(filter, "::"));
  }

  {
    _::NetworkFilter filter({"public", "local"}, {}, base);

    ZC_EXPECT(allowed4(filter, "8.8.8.8"));
    ZC_EXPECT(!allowed4(filter, "240.1.2.3"));

    ZC_EXPECT(!allowed4(filter, "192.168.0.1"));
    ZC_EXPECT(!allowed4(filter, "10.1.2.3"));
    ZC_EXPECT(allowed4(filter, "127.0.0.1"));
    ZC_EXPECT(allowed4(filter, "0.0.0.0"));

    ZC_EXPECT(allowed6(filter, "2400:cb00:2048:1::c629:d7a2"));
    ZC_EXPECT(!allowed6(filter, "fc00::1234"));
    ZC_EXPECT(allowed6(filter, "::1"));
    ZC_EXPECT(allowed6(filter, "::"));
  }

  // Reserved ranges can be explicitly allowed.
  {
    _::NetworkFilter filter({"public", "private", "240.0.0.0/4"}, {}, base);

    ZC_EXPECT(allowed4(filter, "8.8.8.8"));
    ZC_EXPECT(allowed4(filter, "240.1.2.3"));

    ZC_EXPECT(allowed4(filter, "192.168.0.1"));
    ZC_EXPECT(allowed4(filter, "10.1.2.3"));
    ZC_EXPECT(allowed4(filter, "127.0.0.1"));
    ZC_EXPECT(allowed4(filter, "0.0.0.0"));

    ZC_EXPECT(allowed6(filter, "2400:cb00:2048:1::c629:d7a2"));
    ZC_EXPECT(allowed6(filter, "fc00::1234"));
    ZC_EXPECT(allowed6(filter, "::1"));
    ZC_EXPECT(allowed6(filter, "::"));
  }
}

ZC_TEST("Network::restrictPeers()") {
  auto ioContext = setupAsyncIo();
  auto& w = ioContext.waitScope;
  auto& network = ioContext.provider->getNetwork();
  auto restrictedNetwork = network.restrictPeers({"public"});

  ZC_EXPECT(tryParse(w, *restrictedNetwork, "8.8.8.8") == "8.8.8.8:0");
#if !_WIN32
  ZC_EXPECT_THROW_MESSAGE("restrictPeers", tryParse(w, *restrictedNetwork, "unix:/foo"));
#endif

  auto addr = restrictedNetwork->parseAddress("127.0.0.1").wait(w);

  auto listener = addr->listen();
  auto acceptTask = listener->accept()
                        .then([](zc::Own<zc::AsyncIoStream>) {
                          ZC_FAIL_EXPECT("should not have received connection");
                        })
                        .eagerlyEvaluate(nullptr);

  ZC_EXPECT_THROW_MESSAGE("restrictPeers", addr->connect().wait(w));

  // We can connect to the listener but the connection will be immediately closed.
  auto addr2 = network.parseAddress("127.0.0.1", listener->getPort()).wait(w);
  auto conn = addr2->connect().wait(w);
  ZC_EXPECT(conn->readAllText().wait(w) == "");
}

zc::Promise<void> expectRead(zc::AsyncInputStream& in, zc::StringPtr expected) {
  if (expected.size() == 0) return zc::READY_NOW;

  auto buffer = zc::heapArray<char>(expected.size());

  auto promise = in.tryRead(buffer.begin(), 1, buffer.size());
  return promise.then([&in, expected, buffer = zc::mv(buffer)](size_t amount) {
    if (amount == 0) { ZC_FAIL_ASSERT("expected data never sent", expected); }

    auto actual = buffer.first(amount);
    if (!expected.asArray().startsWith(actual)) {
      ZC_FAIL_ASSERT("data from stream doesn't match expected", expected, actual);
    }

    return expectRead(in, expected.slice(amount));
  });
}

class MockAsyncInputStream final : public AsyncInputStream {
public:
  MockAsyncInputStream(zc::ArrayPtr<const byte> bytes, size_t blockSize)
      : bytes(bytes), blockSize(blockSize) {}

  zc::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    // Clamp max read to blockSize.
    size_t n = zc::min(blockSize, maxBytes);

    // Unless that's less than minBytes -- in which case, use minBytes.
    n = zc::max(n, minBytes);

    // But also don't read more data than we have.
    n = zc::min(n, bytes.size());

    memcpy(buffer, bytes.begin(), n);
    bytes = bytes.slice(n, bytes.size());
    return n;
  }

private:
  zc::ArrayPtr<const byte> bytes;
  size_t blockSize;
};

ZC_TEST("AsyncInputStream::readAllText() / readAllBytes()") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto bigText = strArray(zc::repeat("foo bar baz"_zc, 12345), ",");
  size_t inputSizes[] = {0, 1, 256, 4096, 8191, 8192, 8193, 10000, bigText.size()};
  size_t blockSizes[] = {1, 4, 256, 4096, 8192, bigText.size()};
  uint64_t limits[] = {0,
                       1,
                       256,
                       bigText.size() / 2,
                       bigText.size() - 1,
                       bigText.size(),
                       bigText.size() + 1,
                       zc::maxValue};

  for (size_t inputSize : inputSizes) {
    for (size_t blockSize : blockSizes) {
      for (uint64_t limit : limits) {
        ZC_CONTEXT(inputSize, blockSize, limit);
        auto textSlice = bigText.asBytes().first(inputSize);
        auto readAllText = [&]() {
          MockAsyncInputStream input(textSlice, blockSize);
          return input.readAllText(limit).wait(ws);
        };
        auto readAllBytes = [&]() {
          MockAsyncInputStream input(textSlice, blockSize);
          return input.readAllBytes(limit).wait(ws);
        };
        if (limit > inputSize) {
          ZC_EXPECT(readAllText().asBytes() == textSlice);
          ZC_EXPECT(readAllBytes() == textSlice);
        } else {
          ZC_EXPECT_THROW_MESSAGE("Reached limit before EOF.", readAllText());
          ZC_EXPECT_THROW_MESSAGE("Reached limit before EOF.", readAllBytes());
        }
      }
    }
  }
}

ZC_TEST("Userland pipe") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  auto promise = pipe.out->write("foo"_zcb);
  ZC_EXPECT(!promise.poll(ws));

  char buf[4]{};
  ZC_EXPECT(pipe.in->tryRead(buf, 1, 4).wait(ws) == 3);
  buf[3] = '\0';
  ZC_EXPECT(buf == "foo"_zc);

  promise.wait(ws);

  auto promise2 = pipe.in->readAllText();
  ZC_EXPECT(!promise2.poll(ws));

  pipe.out = nullptr;
  ZC_EXPECT(promise2.wait(ws) == "");
}

ZC_TEST("Userland pipe cancel write") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  auto promise = pipe.out->write("foobar"_zcb);
  ZC_EXPECT(!promise.poll(ws));

  expectRead(*pipe.in, "foo").wait(ws);
  ZC_EXPECT(!promise.poll(ws));
  promise = nullptr;

  promise = pipe.out->write("baz"_zcb);
  expectRead(*pipe.in, "baz").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  ZC_EXPECT(pipe.in->readAllText().wait(ws) == "");
}

ZC_TEST("Userland pipe cancel read") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  auto writeOp = pipe.out->write("foo"_zcb);
  auto readOp = expectRead(*pipe.in, "foobar");
  writeOp.wait(ws);
  ZC_EXPECT(!readOp.poll(ws));
  readOp = nullptr;

  auto writeOp2 = pipe.out->write("baz"_zcb);
  expectRead(*pipe.in, "baz").wait(ws);
}

ZC_TEST("Userland pipe pumpTo") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out);

  auto promise = pipe.out->write("foo"_zcb);
  ZC_EXPECT(!promise.poll(ws));

  expectRead(*pipe2.in, "foo").wait(ws);

  promise.wait(ws);

  auto promise2 = pipe2.in->readAllText();
  ZC_EXPECT(!promise2.poll(ws));

  pipe.out = nullptr;
  ZC_EXPECT(pumpPromise.wait(ws) == 3);
}

ZC_TEST("Userland pipe tryPumpFrom") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  auto promise = pipe.out->write("foo"_zcb);
  ZC_EXPECT(!promise.poll(ws));

  expectRead(*pipe2.in, "foo").wait(ws);

  promise.wait(ws);

  auto promise2 = pipe2.in->readAllText();
  ZC_EXPECT(!promise2.poll(ws));

  pipe.out = nullptr;
  ZC_EXPECT(!promise2.poll(ws));
  ZC_EXPECT(pumpPromise.wait(ws) == 3);
}

ZC_TEST("Userland pipe tryPumpFrom exception") {
  // Check for a bug where exceptions don't propagate through tryPumpFrom() correctly.

  zc::EventLoop loop;
  WaitScope ws(loop);

  auto [promise, fulfiller] = newPromiseAndFulfiller<Own<AsyncIoStream>>();
  auto promiseStream = newPromisedStream(zc::mv(promise));

  auto pipe = newOneWayPipe();
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe.out->tryPumpFrom(*promiseStream));

  char buffer;
  auto readPromise = pipe.in->tryRead(&buffer, 1, 1);

  ZC_EXPECT(!pumpPromise.poll(ws));
  ZC_EXPECT(!readPromise.poll(ws));

  fulfiller->reject(ZC_EXCEPTION(FAILED, "foobar"));

  ZC_EXPECT_THROW_MESSAGE("foobar", pumpPromise.wait(ws));

  // Before the bugfix, `readPromise` would reject with the exception "disconnected: operation
  // canceled" rather than propagate the original exception.
  ZC_EXPECT_THROW_MESSAGE("foobar", readPromise.wait(ws));
}

ZC_TEST("Userland pipe pumpTo cancel") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out);

  auto promise = pipe.out->write("foobar"_zcb);
  ZC_EXPECT(!promise.poll(ws));

  expectRead(*pipe2.in, "foo").wait(ws);

  // Cancel pump.
  pumpPromise = nullptr;

  auto promise3 = pipe2.out->write("baz"_zcb);
  expectRead(*pipe2.in, "baz").wait(ws);
}

ZC_TEST("Userland pipe tryPumpFrom cancel") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  auto promise = pipe.out->write("foobar"_zcb);
  ZC_EXPECT(!promise.poll(ws));

  expectRead(*pipe2.in, "foo").wait(ws);

  // Cancel pump.
  pumpPromise = nullptr;

  auto promise3 = pipe2.out->write("baz"_zcb);
  expectRead(*pipe2.in, "baz").wait(ws);
}

ZC_TEST("Userland pipe with limit") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe(6);

  {
    auto promise = pipe.out->write("foo"_zcb);
    ZC_EXPECT(!promise.poll(ws));
    expectRead(*pipe.in, "foo").wait(ws);
    promise.wait(ws);
  }

  {
    auto promise = pipe.in->readAllText();
    ZC_EXPECT(!promise.poll(ws));
    auto promise2 = pipe.out->write("barbaz"_zcb);
    ZC_EXPECT(promise.wait(ws) == "bar");
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("read end of pipe was aborted", promise2.wait(ws));
  }

  // Further writes throw and reads return EOF.
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("abortRead() has been called",
                                      pipe.out->write("baz"_zcb).wait(ws));
  ZC_EXPECT(pipe.in->readAllText().wait(ws) == "");
}

ZC_TEST("Userland pipe pumpTo with limit") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe(6);
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out);

  {
    auto promise = pipe.out->write("foo"_zcb);
    ZC_EXPECT(!promise.poll(ws));
    expectRead(*pipe2.in, "foo").wait(ws);
    promise.wait(ws);
  }

  {
    auto promise = expectRead(*pipe2.in, "bar");
    ZC_EXPECT(!promise.poll(ws));
    auto promise2 = pipe.out->write("barbaz"_zcb);
    promise.wait(ws);
    pumpPromise.wait(ws);
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("read end of pipe was aborted", promise2.wait(ws));
  }

  // Further writes throw.
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("abortRead() has been called",
                                      pipe.out->write("baz"_zcb).wait(ws));
}

ZC_TEST("Userland pipe pump into zero-limited pipe, no data to pump") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe(uint64_t(0));
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  expectRead(*pipe2.in, "").wait(ws);
  pipe.out = nullptr;
  ZC_EXPECT(pumpPromise.wait(ws) == 0);
}

ZC_TEST("Userland pipe pump into zero-limited pipe, data is pumped") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe(uint64_t(0));
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  expectRead(*pipe2.in, "").wait(ws);
  auto writePromise = pipe.out->write("foo"_zcb);
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("abortRead() has been called", pumpPromise.wait(ws));
}

ZC_TEST("Userland pipe gather write") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe.in, "foobar").wait(ws);
  promise.wait(ws);

  auto promise2 = pipe.in->readAllText();
  ZC_EXPECT(!promise2.poll(ws));

  pipe.out = nullptr;
  ZC_EXPECT(promise2.wait(ws) == "");
}

ZC_TEST("Userland pipe gather write split on buffer boundary") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe.in, "foo").wait(ws);
  expectRead(*pipe.in, "bar").wait(ws);
  promise.wait(ws);

  auto promise2 = pipe.in->readAllText();
  ZC_EXPECT(!promise2.poll(ws));

  pipe.out = nullptr;
  ZC_EXPECT(promise2.wait(ws) == "");
}

ZC_TEST("Userland pipe gather write split mid-first-buffer") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe.in, "fo").wait(ws);
  expectRead(*pipe.in, "obar").wait(ws);
  promise.wait(ws);

  auto promise2 = pipe.in->readAllText();
  ZC_EXPECT(!promise2.poll(ws));

  pipe.out = nullptr;
  ZC_EXPECT(promise2.wait(ws) == "");
}

ZC_TEST("Userland pipe gather write split mid-second-buffer") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe.in, "foob").wait(ws);
  expectRead(*pipe.in, "ar").wait(ws);
  promise.wait(ws);

  auto promise2 = pipe.in->readAllText();
  ZC_EXPECT(!promise2.poll(ws));

  pipe.out = nullptr;
  ZC_EXPECT(promise2.wait(ws) == "");
}

ZC_TEST("Userland pipe gather write pump") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out);

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  ZC_EXPECT(pumpPromise.wait(ws) == 6);
}

ZC_TEST("Userland pipe gather write pump split on buffer boundary") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out);

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foo").wait(ws);
  expectRead(*pipe2.in, "bar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  ZC_EXPECT(pumpPromise.wait(ws) == 6);
}

ZC_TEST("Userland pipe gather write pump split mid-first-buffer") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out);

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "fo").wait(ws);
  expectRead(*pipe2.in, "obar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  ZC_EXPECT(pumpPromise.wait(ws) == 6);
}

ZC_TEST("Userland pipe gather write pump split mid-second-buffer") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out);

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foob").wait(ws);
  expectRead(*pipe2.in, "ar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  ZC_EXPECT(pumpPromise.wait(ws) == 6);
}

ZC_TEST("Userland pipe gather write split pump on buffer boundary") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out, 3).then([&](uint64_t i) {
    ZC_EXPECT(i == 3);
    return pipe.in->pumpTo(*pipe2.out, 3);
  });

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  ZC_EXPECT(pumpPromise.wait(ws) == 3);
}

ZC_TEST("Userland pipe gather write split pump mid-first-buffer") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out, 2).then([&](uint64_t i) {
    ZC_EXPECT(i == 2);
    return pipe.in->pumpTo(*pipe2.out, 4);
  });

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  ZC_EXPECT(pumpPromise.wait(ws) == 4);
}

ZC_TEST("Userland pipe gather write split pump mid-second-buffer") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out, 4).then([&](uint64_t i) {
    ZC_EXPECT(i == 4);
    return pipe.in->pumpTo(*pipe2.out, 2);
  });

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  ZC_EXPECT(pumpPromise.wait(ws) == 2);
}

ZC_TEST("Userland pipe gather write pumpFrom") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  char c;
  auto eofPromise = pipe2.in->tryRead(&c, 1, 1);
  eofPromise.poll(ws);  // force pump to notice EOF
  ZC_EXPECT(pumpPromise.wait(ws) == 6);
  pipe2.out = nullptr;
  ZC_EXPECT(eofPromise.wait(ws) == 0);
}

ZC_TEST("Userland pipe gather write pumpFrom split on buffer boundary") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foo").wait(ws);
  expectRead(*pipe2.in, "bar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  char c;
  auto eofPromise = pipe2.in->tryRead(&c, 1, 1);
  eofPromise.poll(ws);  // force pump to notice EOF
  ZC_EXPECT(pumpPromise.wait(ws) == 6);
  pipe2.out = nullptr;
  ZC_EXPECT(eofPromise.wait(ws) == 0);
}

ZC_TEST("Userland pipe gather write pumpFrom split mid-first-buffer") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "fo").wait(ws);
  expectRead(*pipe2.in, "obar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  char c;
  auto eofPromise = pipe2.in->tryRead(&c, 1, 1);
  eofPromise.poll(ws);  // force pump to notice EOF
  ZC_EXPECT(pumpPromise.wait(ws) == 6);
  pipe2.out = nullptr;
  ZC_EXPECT(eofPromise.wait(ws) == 0);
}

ZC_TEST("Userland pipe gather write pumpFrom split mid-second-buffer") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foob").wait(ws);
  expectRead(*pipe2.in, "ar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  char c;
  auto eofPromise = pipe2.in->tryRead(&c, 1, 1);
  eofPromise.poll(ws);  // force pump to notice EOF
  ZC_EXPECT(pumpPromise.wait(ws) == 6);
  pipe2.out = nullptr;
  ZC_EXPECT(eofPromise.wait(ws) == 0);
}

ZC_TEST("Userland pipe gather write split pumpFrom on buffer boundary") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in, 3)).then([&](uint64_t i) {
    ZC_EXPECT(i == 3);
    return ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in, 3));
  });

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  ZC_EXPECT(pumpPromise.wait(ws) == 3);
}

ZC_TEST("Userland pipe gather write split pumpFrom mid-first-buffer") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in, 2)).then([&](uint64_t i) {
    ZC_EXPECT(i == 2);
    return ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in, 4));
  });

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  ZC_EXPECT(pumpPromise.wait(ws) == 4);
}

ZC_TEST("Userland pipe gather write split pumpFrom mid-second-buffer") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in, 4)).then([&](uint64_t i) {
    ZC_EXPECT(i == 4);
    return ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in, 2));
  });

  ArrayPtr<const byte> parts[] = {"foo"_zcb, "bar"_zcb};
  auto promise = pipe.out->write(parts);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  ZC_EXPECT(pumpPromise.wait(ws) == 2);
}

ZC_TEST("Userland pipe pumpTo less than write amount") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out, 1);

  auto pieces = zc::heapArray<ArrayPtr<const byte>>(2);
  byte a[1] = {'a'};
  byte b[1] = {'b'};
  pieces[0] = arrayPtr(a, 1);
  pieces[1] = arrayPtr(b, 1);

  auto writePromise = pipe.out->write(pieces);
  ZC_EXPECT(!writePromise.poll(ws));

  expectRead(*pipe2.in, "a").wait(ws);
  ZC_EXPECT(pumpPromise.wait(ws) == 1);
  ZC_EXPECT(!writePromise.poll(ws));

  pumpPromise = pipe.in->pumpTo(*pipe2.out, 1);

  expectRead(*pipe2.in, "b").wait(ws);
  ZC_EXPECT(pumpPromise.wait(ws) == 1);
  writePromise.wait(ws);
}

ZC_TEST("Userland pipe pumpFrom EOF on abortRead()") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  auto promise = pipe.out->write("foobar"_zcb);
  ZC_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  ZC_EXPECT(!pumpPromise.poll(ws));
  pipe.out = nullptr;
  pipe2.in = nullptr;  // force pump to notice EOF
  ZC_EXPECT(pumpPromise.wait(ws) == 6);
  pipe2.out = nullptr;
}

ZC_TEST("Userland pipe EOF fulfills pumpFrom promise") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  auto writePromise = pipe.out->write("foobar"_zcb);
  ZC_EXPECT(!writePromise.poll(ws));
  auto pipe3 = newOneWayPipe();
  auto pumpPromise2 = pipe2.in->pumpTo(*pipe3.out);
  ZC_EXPECT(!pumpPromise2.poll(ws));
  expectRead(*pipe3.in, "foobar").wait(ws);
  writePromise.wait(ws);

  ZC_EXPECT(!pumpPromise.poll(ws));
  pipe.out = nullptr;
  ZC_EXPECT(pumpPromise.wait(ws) == 6);

  ZC_EXPECT(!pumpPromise2.poll(ws));
  pipe2.out = nullptr;
  ZC_EXPECT(pumpPromise2.wait(ws) == 6);
}

ZC_TEST("Userland pipe tryPumpFrom to pumpTo for same amount fulfills simultaneously") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in, 6));

  auto writePromise = pipe.out->write("foobar"_zcb);
  ZC_EXPECT(!writePromise.poll(ws));
  auto pipe3 = newOneWayPipe();
  auto pumpPromise2 = pipe2.in->pumpTo(*pipe3.out, 6);
  ZC_EXPECT(!pumpPromise2.poll(ws));
  expectRead(*pipe3.in, "foobar").wait(ws);
  writePromise.wait(ws);

  ZC_EXPECT(pumpPromise.wait(ws) == 6);
  ZC_EXPECT(pumpPromise2.wait(ws) == 6);
}

ZC_TEST("Userland pipe multi-part write doesn't quit early") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  auto readPromise = expectRead(*pipe.in, "foo");

  zc::ArrayPtr<const byte> pieces[2] = {"foobar"_zcb, "baz"_zcb};
  auto writePromise = pipe.out->write(pieces);

  readPromise.wait(ws);
  ZC_EXPECT(!writePromise.poll(ws));
  expectRead(*pipe.in, "bar").wait(ws);
  ZC_EXPECT(!writePromise.poll(ws));
  expectRead(*pipe.in, "baz").wait(ws);
  writePromise.wait(ws);
}

ZC_TEST("Userland pipe BlockedRead gets empty tryPumpFrom") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();

  // First start a read from the back end.
  char buffer[4]{};
  auto readPromise = pipe2.in->tryRead(buffer, 1, 4);

  // Now arrange a pump between the pipes, using tryPumpFrom().
  auto pumpPromise = ZC_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  // Disconnect the front pipe, causing EOF on the pump.
  pipe.out = nullptr;

  // The pump should have produced zero bytes.
  ZC_EXPECT(pumpPromise.wait(ws) == 0);

  // The read is incomplete.
  ZC_EXPECT(!readPromise.poll(ws));

  // A subsequent write() completes the read.
  pipe2.out->write("foo"_zcb).wait(ws);
  ZC_EXPECT(readPromise.wait(ws) == 3);
  buffer[3] = '\0';
  ZC_EXPECT(zc::StringPtr(buffer, 3) == "foo");
}

constexpr static auto TEE_MAX_CHUNK_SIZE = 1 << 14;
// AsyncTee::MAX_CHUNK_SIZE, 16k as of this writing

ZC_TEST("Userland tee") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto tee = newTee(zc::mv(pipe.in));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  auto writePromise = pipe.out->write("foobar"_zcb);

  expectRead(*left, "foobar").wait(ws);
  writePromise.wait(ws);
  expectRead(*right, "foobar").wait(ws);
}

ZC_TEST("Userland nested tee") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto tee = newTee(zc::mv(pipe.in));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  auto tee2 = newTee(zc::mv(right));
  auto rightLeft = zc::mv(tee2.branches[0]);
  auto rightRight = zc::mv(tee2.branches[1]);

  auto writePromise = pipe.out->write("foobar"_zcb);

  expectRead(*left, "foobar").wait(ws);
  writePromise.wait(ws);
  expectRead(*rightLeft, "foobar").wait(ws);
  expectRead(*rightRight, "foo").wait(ws);

  auto tee3 = newTee(zc::mv(rightRight));
  auto rightRightLeft = zc::mv(tee3.branches[0]);
  auto rightRightRight = zc::mv(tee3.branches[1]);
  expectRead(*rightRightLeft, "bar").wait(ws);
  expectRead(*rightRightRight, "b").wait(ws);

  auto tee4 = newTee(zc::mv(rightRightRight));
  auto rightRightRightLeft = zc::mv(tee4.branches[0]);
  auto rightRightRightRight = zc::mv(tee4.branches[1]);
  expectRead(*rightRightRightLeft, "ar").wait(ws);
  expectRead(*rightRightRightRight, "ar").wait(ws);
}

ZC_TEST("Userland tee concurrent read") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto tee = newTee(zc::mv(pipe.in));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  uint8_t leftBuf[6] = {0};
  uint8_t rightBuf[6] = {0};
  auto leftPromise = left->tryRead(leftBuf, 6, 6);
  auto rightPromise = right->tryRead(rightBuf, 6, 6);
  ZC_EXPECT(!leftPromise.poll(ws));
  ZC_EXPECT(!rightPromise.poll(ws));

  pipe.out->write("foobar"_zcb).wait(ws);

  ZC_EXPECT(leftPromise.wait(ws) == 6);
  ZC_EXPECT(rightPromise.wait(ws) == 6);

  ZC_EXPECT(leftBuf == "foobar"_zcb);
}

ZC_TEST("Userland tee cancel and restart read") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto tee = newTee(zc::mv(pipe.in));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  auto writePromise = pipe.out->write("foobar"_zcb);

  {
    // Initiate a read and immediately cancel it.
    uint8_t buf[6] = {0};
    auto promise = left->tryRead(buf, 6, 6);
  }

  // Subsequent reads still see the full data.
  expectRead(*left, "foobar").wait(ws);
  writePromise.wait(ws);
  expectRead(*right, "foobar").wait(ws);
}

ZC_TEST("Userland tee cancel read and destroy branch then read other branch") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto tee = newTee(zc::mv(pipe.in));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  auto writePromise = pipe.out->write("foobar"_zcb);

  {
    // Initiate a read and immediately cancel it.
    uint8_t buf[6] = {0};
    auto promise = left->tryRead(buf, 6, 6);
  }

  // And destroy the branch for good measure.
  left = nullptr;

  // Subsequent reads on the other branch still see the full data.
  expectRead(*right, "foobar").wait(ws);
  writePromise.wait(ws);
}

ZC_TEST("Userland tee subsequent other-branch reads are READY_NOW") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto tee = newTee(zc::mv(pipe.in));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  uint8_t leftBuf[6] = {0};
  auto leftPromise = left->tryRead(leftBuf, 6, 6);
  // This is the first read, so there should NOT be buffered data.
  ZC_EXPECT(!leftPromise.poll(ws));
  pipe.out->write("foobar"_zcb).wait(ws);
  leftPromise.wait(ws);
  ZC_EXPECT(leftBuf == "foobar"_zcb);

  uint8_t rightBuf[6] = {0};
  auto rightPromise = right->tryRead(rightBuf, 6, 6);
  // The left read promise was fulfilled, so there SHOULD be buffered data.
  ZC_EXPECT(rightPromise.poll(ws));
  rightPromise.wait(ws);
  ZC_EXPECT(rightBuf == "foobar"_zcb);
}

ZC_TEST("Userland tee read EOF propagation") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto writePromise = pipe.out->write("foobar"_zcb);
  auto tee = newTee(mv(pipe.in));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  // Lengthless pipe, so ...
  ZC_EXPECT(left->tryGetLength() == zc::none);
  ZC_EXPECT(right->tryGetLength() == zc::none);

  uint8_t leftBuf[7] = {0};
  auto leftPromise = left->tryRead(leftBuf, size(leftBuf), size(leftBuf));
  writePromise.wait(ws);
  // Destroying the output side should force a short read.
  pipe.out = nullptr;

  ZC_EXPECT(leftPromise.wait(ws) == 6);
  ZC_EXPECT(zc::arrayPtr(leftBuf, 6) == "foobar"_zcb);

  // And we should see a short read here, too.
  uint8_t rightBuf[7] = {0};
  auto rightPromise = right->tryRead(rightBuf, size(rightBuf), size(rightBuf));
  ZC_EXPECT(rightPromise.wait(ws) == 6);
  ZC_EXPECT(zc::arrayPtr(rightBuf, 6) == "foobar"_zcb);

  // Further reads should all be short.
  ZC_EXPECT(left->tryRead(leftBuf, 1, size(leftBuf)).wait(ws) == 0);
  ZC_EXPECT(right->tryRead(rightBuf, 1, size(rightBuf)).wait(ws) == 0);
}

ZC_TEST("Userland tee read exception propagation") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  // Make a pipe expecting to read more than we're actually going to write. This will force a "pipe
  // ended prematurely" exception when we destroy the output side early.
  auto pipe = newOneWayPipe(7);
  auto writePromise = pipe.out->write("foobar"_zcb);
  auto tee = newTee(mv(pipe.in));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  // Test tryGetLength() while we're at it.
  ZC_EXPECT(ZC_ASSERT_NONNULL(left->tryGetLength()) == 7);
  ZC_EXPECT(ZC_ASSERT_NONNULL(right->tryGetLength()) == 7);

  uint8_t leftBuf[7] = {0};
  auto leftPromise = left->tryRead(leftBuf, 6, size(leftBuf));
  writePromise.wait(ws);
  // Destroying the output side should force a fulfillment of the read (since we reached minBytes).
  pipe.out = nullptr;
  ZC_EXPECT(leftPromise.wait(ws) == 6);
  ZC_EXPECT(zc::arrayPtr(leftBuf, 6) == "foobar"_zcb);

  // The next read sees the exception.
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE(
      "pipe ended prematurely", left->tryRead(leftBuf, 1, size(leftBuf)).ignoreResult().wait(ws));

  // Test tryGetLength() here -- the unread branch still sees the original length value.
  ZC_EXPECT(ZC_ASSERT_NONNULL(left->tryGetLength()) == 1);
  ZC_EXPECT(ZC_ASSERT_NONNULL(right->tryGetLength()) == 7);

  // We should see the buffered data on the other side, even though we don't reach our minBytes.
  uint8_t rightBuf[7] = {0};
  auto rightPromise = right->tryRead(rightBuf, size(rightBuf), size(rightBuf));
  ZC_EXPECT(rightPromise.wait(ws) == 6);
  ZC_EXPECT(zc::arrayPtr(rightBuf, 6) == "foobar"_zcb);
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE(
      "pipe ended prematurely", right->tryRead(rightBuf, 1, size(leftBuf)).ignoreResult().wait(ws));

  // Further reads should all see the exception again.
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE(
      "pipe ended prematurely", left->tryRead(leftBuf, 1, size(leftBuf)).ignoreResult().wait(ws));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE(
      "pipe ended prematurely", right->tryRead(rightBuf, 1, size(leftBuf)).ignoreResult().wait(ws));
}

ZC_TEST("Userland tee read exception propagation w/ data loss") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  // Make a pipe expecting to read more than we're actually going to write. This will force a "pipe
  // ended prematurely" exception once the pipe sees a short read.
  auto pipe = newOneWayPipe(7);
  auto writePromise = pipe.out->write("foobar"_zcb);
  auto tee = newTee(mv(pipe.in));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  uint8_t leftBuf[7] = {0};
  auto leftPromise = left->tryRead(leftBuf, 7, 7);
  writePromise.wait(ws);
  // Destroying the output side should force an exception, since we didn't reach our minBytes.
  pipe.out = nullptr;
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("pipe ended prematurely",
                                      leftPromise.ignoreResult().wait(ws));

  // And we should see a short read here, too. In fact, we shouldn't see anything: the short read
  // above read all of the pipe's data, but then failed to buffer it because it encountered an
  // exception. It buffered the exception, instead.
  uint8_t rightBuf[7] = {0};
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("pipe ended prematurely",
                                      right->tryRead(rightBuf, 1, 1).ignoreResult().wait(ws));
}

ZC_TEST("Userland tee read into different buffer sizes") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto tee = newTee(heap<MockAsyncInputStream>("foo bar baz"_zcb, 11));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  uint8_t leftBuf[5] = {0};
  uint8_t rightBuf[11] = {0};

  auto leftPromise = left->tryRead(leftBuf, 5, 5);
  auto rightPromise = right->tryRead(rightBuf, 11, 11);

  ZC_EXPECT(leftPromise.wait(ws) == 5);
  ZC_EXPECT(rightPromise.wait(ws) == 11);
}

ZC_TEST("Userland tee reads see max(minBytes...) and min(maxBytes...)") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto tee = newTee(heap<MockAsyncInputStream>("foo bar baz"_zcb, 11));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  {
    uint8_t leftBuf[5] = {0};
    uint8_t rightBuf[11] = {0};

    // Subrange of another range. The smaller maxBytes should win.
    auto leftPromise = left->tryRead(leftBuf, 3, 5);
    auto rightPromise = right->tryRead(rightBuf, 1, 11);

    ZC_EXPECT(leftPromise.wait(ws) == 5);
    ZC_EXPECT(rightPromise.wait(ws) == 5);
  }

  {
    uint8_t leftBuf[5] = {0};
    uint8_t rightBuf[11] = {0};

    // Disjoint ranges. The larger minBytes should win.
    auto leftPromise = left->tryRead(leftBuf, 3, 5);
    auto rightPromise = right->tryRead(rightBuf, 6, 11);

    ZC_EXPECT(leftPromise.wait(ws) == 5);
    ZC_EXPECT(rightPromise.wait(ws) == 6);

    ZC_EXPECT(left->tryRead(leftBuf, 1, 2).wait(ws) == 1);
  }
}

ZC_TEST("Userland tee read stress test") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto bigText = strArray(zc::repeat("foo bar baz"_zc, 12345), ",");

  auto tee = newTee(heap<MockAsyncInputStream>(bigText.asBytes(), bigText.size()));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  auto leftBuffer = heapArray<byte>(bigText.size());

  {
    auto leftSlice = leftBuffer.first(leftBuffer.size());
    while (leftSlice.size() > 0) {
      for (size_t blockSize : {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59}) {
        if (leftSlice.size() == 0) break;
        auto maxBytes = min(blockSize, leftSlice.size());
        auto amount = left->tryRead(leftSlice.begin(), 1, maxBytes).wait(ws);
        leftSlice = leftSlice.slice(amount, leftSlice.size());
      }
    }
  }

  ZC_EXPECT(leftBuffer == bigText.asBytes());
  ZC_EXPECT(right->readAllText().wait(ws) == bigText);
}

ZC_TEST("Userland tee pump") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto bigText = strArray(zc::repeat("foo bar baz"_zc, 12345), ",");

  auto tee = newTee(heap<MockAsyncInputStream>(bigText.asBytes(), bigText.size()));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  auto leftPipe = newOneWayPipe();
  auto rightPipe = newOneWayPipe();

  auto leftPumpPromise = left->pumpTo(*leftPipe.out, 7);
  ZC_EXPECT(!leftPumpPromise.poll(ws));

  auto rightPumpPromise = right->pumpTo(*rightPipe.out);
  // Neither are ready yet, because the left pump's backpressure has blocked the AsyncTee's pull
  // loop until we read from leftPipe.
  ZC_EXPECT(!leftPumpPromise.poll(ws));
  ZC_EXPECT(!rightPumpPromise.poll(ws));

  expectRead(*leftPipe.in, "foo bar").wait(ws);
  ZC_EXPECT(leftPumpPromise.wait(ws) == 7);
  ZC_EXPECT(!rightPumpPromise.poll(ws));

  // We should be able to read up to how far the left side pumped, and beyond. The left side will
  // now have data in its buffer.
  expectRead(*rightPipe.in, "foo bar baz,foo bar baz,foo").wait(ws);

  // Consume the left side buffer.
  expectRead(*left, " baz,foo bar").wait(ws);

  // We can destroy the left branch entirely and the right branch will still see all data.
  left = nullptr;
  ZC_EXPECT(!rightPumpPromise.poll(ws));
  auto allTextPromise = rightPipe.in->readAllText();
  ZC_EXPECT(rightPumpPromise.wait(ws) == bigText.size());
  // Need to force an EOF in the right pipe to check the result.
  rightPipe.out = nullptr;
  ZC_EXPECT(allTextPromise.wait(ws) == bigText.slice(27));
}

ZC_TEST("Userland tee pump slows down reads") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto bigText = strArray(zc::repeat("foo bar baz"_zc, 12345), ",");

  auto tee = newTee(heap<MockAsyncInputStream>(bigText.asBytes(), bigText.size()));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  auto leftPipe = newOneWayPipe();
  auto leftPumpPromise = left->pumpTo(*leftPipe.out);
  ZC_EXPECT(!leftPumpPromise.poll(ws));

  // The left pump will cause some data to be buffered on the right branch, which we can read.
  auto rightExpectation0 = zc::str(bigText.first(TEE_MAX_CHUNK_SIZE));
  expectRead(*right, rightExpectation0).wait(ws);

  // But the next right branch read is blocked by the left pipe's backpressure.
  auto rightExpectation1 = zc::str(bigText.slice(TEE_MAX_CHUNK_SIZE, TEE_MAX_CHUNK_SIZE + 10));
  auto rightPromise = expectRead(*right, rightExpectation1);
  ZC_EXPECT(!rightPromise.poll(ws));

  // The right branch read finishes when we relieve the pressure in the left pipe.
  auto allTextPromise = leftPipe.in->readAllText();
  rightPromise.wait(ws);
  ZC_EXPECT(leftPumpPromise.wait(ws) == bigText.size());
  leftPipe.out = nullptr;
  ZC_EXPECT(allTextPromise.wait(ws) == bigText);
}

ZC_TEST("Userland tee pump EOF propagation") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  {
    // EOF encountered by two pump operations.
    auto pipe = newOneWayPipe();
    auto writePromise = pipe.out->write("foo bar"_zcb);
    auto tee = newTee(mv(pipe.in));
    auto left = zc::mv(tee.branches[0]);
    auto right = zc::mv(tee.branches[1]);

    auto leftPipe = newOneWayPipe();
    auto rightPipe = newOneWayPipe();

    // Pump the first bit, and block.

    auto leftPumpPromise = left->pumpTo(*leftPipe.out);
    ZC_EXPECT(!leftPumpPromise.poll(ws));
    auto rightPumpPromise = right->pumpTo(*rightPipe.out);
    writePromise.wait(ws);
    ZC_EXPECT(!leftPumpPromise.poll(ws));
    ZC_EXPECT(!rightPumpPromise.poll(ws));

    // Induce an EOF. We should see it propagated to both pump promises.

    pipe.out = nullptr;

    // Relieve backpressure.
    auto leftAllPromise = leftPipe.in->readAllText();
    auto rightAllPromise = rightPipe.in->readAllText();
    ZC_EXPECT(leftPumpPromise.wait(ws) == 7);
    ZC_EXPECT(rightPumpPromise.wait(ws) == 7);

    // Make sure we got the data on the pipes that were being pumped to.
    ZC_EXPECT(!leftAllPromise.poll(ws));
    ZC_EXPECT(!rightAllPromise.poll(ws));
    leftPipe.out = nullptr;
    rightPipe.out = nullptr;
    ZC_EXPECT(leftAllPromise.wait(ws) == "foo bar");
    ZC_EXPECT(rightAllPromise.wait(ws) == "foo bar");
  }

  {
    // EOF encountered by a read and pump operation.
    auto pipe = newOneWayPipe();
    auto writePromise = pipe.out->write("foo bar"_zcb);
    auto tee = newTee(mv(pipe.in));
    auto left = zc::mv(tee.branches[0]);
    auto right = zc::mv(tee.branches[1]);

    auto leftPipe = newOneWayPipe();
    auto rightPipe = newOneWayPipe();

    // Pump one branch, read another.

    auto leftPumpPromise = left->pumpTo(*leftPipe.out);
    ZC_EXPECT(!leftPumpPromise.poll(ws));
    expectRead(*right, "foo bar").wait(ws);
    writePromise.wait(ws);
    uint8_t dummy = 0;
    auto rightReadPromise = right->tryRead(&dummy, 1, 1);

    // Induce an EOF. We should see it propagated to both the read and pump promises.

    pipe.out = nullptr;

    // Relieve backpressure in the tee to see the EOF.
    auto leftAllPromise = leftPipe.in->readAllText();
    ZC_EXPECT(leftPumpPromise.wait(ws) == 7);
    ZC_EXPECT(rightReadPromise.wait(ws) == 0);

    // Make sure we got the data on the pipe that was being pumped to.
    ZC_EXPECT(!leftAllPromise.poll(ws));
    leftPipe.out = nullptr;
    ZC_EXPECT(leftAllPromise.wait(ws) == "foo bar");
  }
}

ZC_TEST("Userland tee pump EOF on chunk boundary") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto bigText = strArray(zc::repeat("foo bar baz"_zc, 12345), ",");

  // Conjure an EOF right on the boundary of the tee's internal chunk.
  auto chunkText = zc::str(bigText.first(TEE_MAX_CHUNK_SIZE));
  auto tee = newTee(heap<MockAsyncInputStream>(chunkText.asBytes(), chunkText.size()));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  auto leftPipe = newOneWayPipe();
  auto rightPipe = newOneWayPipe();

  auto leftPumpPromise = left->pumpTo(*leftPipe.out);
  auto rightPumpPromise = right->pumpTo(*rightPipe.out);
  ZC_EXPECT(!leftPumpPromise.poll(ws));
  ZC_EXPECT(!rightPumpPromise.poll(ws));

  auto leftAllPromise = leftPipe.in->readAllText();
  auto rightAllPromise = rightPipe.in->readAllText();

  // The pumps should see the EOF and stop.
  ZC_EXPECT(leftPumpPromise.wait(ws) == TEE_MAX_CHUNK_SIZE);
  ZC_EXPECT(rightPumpPromise.wait(ws) == TEE_MAX_CHUNK_SIZE);

  // Verify that we saw the data on the other end of the destination pipes.
  leftPipe.out = nullptr;
  rightPipe.out = nullptr;
  ZC_EXPECT(leftAllPromise.wait(ws) == chunkText);
  ZC_EXPECT(rightAllPromise.wait(ws) == chunkText);
}

ZC_TEST("Userland tee pump read exception propagation") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  {
    // Exception encountered by two pump operations.
    auto pipe = newOneWayPipe(14);
    auto writePromise = pipe.out->write("foo bar"_zcb);
    auto tee = newTee(mv(pipe.in));
    auto left = zc::mv(tee.branches[0]);
    auto right = zc::mv(tee.branches[1]);

    auto leftPipe = newOneWayPipe();
    auto rightPipe = newOneWayPipe();

    // Pump the first bit, and block.

    auto leftPumpPromise = left->pumpTo(*leftPipe.out);
    ZC_EXPECT(!leftPumpPromise.poll(ws));
    auto rightPumpPromise = right->pumpTo(*rightPipe.out);
    writePromise.wait(ws);
    ZC_EXPECT(!leftPumpPromise.poll(ws));
    ZC_EXPECT(!rightPumpPromise.poll(ws));

    // Induce a read exception. We should see it propagated to both pump promises.

    pipe.out = nullptr;

    // Both promises must exist before the backpressure in the tee is relieved, and the tee pull
    // loop actually sees the exception.
    auto leftAllPromise = leftPipe.in->readAllText();
    auto rightAllPromise = rightPipe.in->readAllText();
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("pipe ended prematurely",
                                        leftPumpPromise.ignoreResult().wait(ws));
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("pipe ended prematurely",
                                        rightPumpPromise.ignoreResult().wait(ws));

    // Make sure we got the data on the destination pipes.
    ZC_EXPECT(!leftAllPromise.poll(ws));
    ZC_EXPECT(!rightAllPromise.poll(ws));
    leftPipe.out = nullptr;
    rightPipe.out = nullptr;
    ZC_EXPECT(leftAllPromise.wait(ws) == "foo bar");
    ZC_EXPECT(rightAllPromise.wait(ws) == "foo bar");
  }

  {
    // Exception encountered by a read and pump operation.
    auto pipe = newOneWayPipe(14);
    auto writePromise = pipe.out->write("foo bar"_zcb);
    auto tee = newTee(mv(pipe.in));
    auto left = zc::mv(tee.branches[0]);
    auto right = zc::mv(tee.branches[1]);

    auto leftPipe = newOneWayPipe();
    auto rightPipe = newOneWayPipe();

    // Pump one branch, read another.

    auto leftPumpPromise = left->pumpTo(*leftPipe.out);
    ZC_EXPECT(!leftPumpPromise.poll(ws));
    expectRead(*right, "foo bar").wait(ws);
    writePromise.wait(ws);
    uint8_t dummy = 0;
    auto rightReadPromise = right->tryRead(&dummy, 1, 1);

    // Induce a read exception. We should see it propagated to both the read and pump promises.

    pipe.out = nullptr;

    // Relieve backpressure in the tee to see the exceptions.
    auto leftAllPromise = leftPipe.in->readAllText();
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("pipe ended prematurely",
                                        leftPumpPromise.ignoreResult().wait(ws));
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("pipe ended prematurely",
                                        rightReadPromise.ignoreResult().wait(ws));

    // Make sure we got the data on the destination pipe.
    ZC_EXPECT(!leftAllPromise.poll(ws));
    leftPipe.out = nullptr;
    ZC_EXPECT(leftAllPromise.wait(ws) == "foo bar");
  }
}

ZC_TEST("Userland tee pump write exception propagation") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto bigText = strArray(zc::repeat("foo bar baz"_zc, 12345), ",");

  auto tee = newTee(heap<MockAsyncInputStream>(bigText.asBytes(), bigText.size()));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  // Set up two pumps and let them block.
  auto leftPipe = newOneWayPipe();
  auto rightPipe = newOneWayPipe();
  auto leftPumpPromise = left->pumpTo(*leftPipe.out);
  auto rightPumpPromise = right->pumpTo(*rightPipe.out);
  ZC_EXPECT(!leftPumpPromise.poll(ws));
  ZC_EXPECT(!rightPumpPromise.poll(ws));

  // Induce a write exception in the right branch pump. It should propagate to the right pump
  // promise.
  rightPipe.in = nullptr;
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("read end of pipe was aborted",
                                      rightPumpPromise.ignoreResult().wait(ws));

  // The left pump promise does not see the right branch's write exception.
  ZC_EXPECT(!leftPumpPromise.poll(ws));
  auto allTextPromise = leftPipe.in->readAllText();
  ZC_EXPECT(leftPumpPromise.wait(ws) == bigText.size());
  leftPipe.out = nullptr;
  ZC_EXPECT(allTextPromise.wait(ws) == bigText);
}

ZC_TEST("Userland tee pump cancellation implies write cancellation") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto text = "foo bar baz"_zc;

  auto tee = newTee(heap<MockAsyncInputStream>(text.asBytes(), text.size()));
  auto left = zc::mv(tee.branches[0]);
  auto right = zc::mv(tee.branches[1]);

  auto leftPipe = newOneWayPipe();
  auto leftPumpPromise = left->pumpTo(*leftPipe.out);

  // Arrange to block the left pump on its write operation.
  expectRead(*right, "foo ").wait(ws);
  ZC_EXPECT(!leftPumpPromise.poll(ws));

  // Then cancel the pump, while it's still blocked.
  leftPumpPromise = nullptr;
  // It should cancel its write operations, so it should now be safe to destroy the output stream to
  // which it was pumping.
  ZC_IF_SOME(exception, zc::runCatchingExceptions([&]() { leftPipe.out = nullptr; })) {
    ZC_FAIL_EXPECT("write promises were not canceled", exception);
  }
}

ZC_TEST("Userland tee buffer size limit") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto text = "foo bar baz"_zc;

  {
    // We can carefully read data to stay under our ridiculously low limit.

    auto tee = newTee(heap<MockAsyncInputStream>(text.asBytes(), text.size()), 2);
    auto left = zc::mv(tee.branches[0]);
    auto right = zc::mv(tee.branches[1]);

    expectRead(*left, "fo").wait(ws);
    expectRead(*right, "foo ").wait(ws);
    expectRead(*left, "o ba").wait(ws);
    expectRead(*right, "bar ").wait(ws);
    expectRead(*left, "r ba").wait(ws);
    expectRead(*right, "baz").wait(ws);
    expectRead(*left, "z").wait(ws);
  }

  {
    // Exceeding the limit causes both branches to see the exception after exhausting their buffers.

    auto tee = newTee(heap<MockAsyncInputStream>(text.asBytes(), text.size()), 2);
    auto left = zc::mv(tee.branches[0]);
    auto right = zc::mv(tee.branches[1]);

    expectRead(*left, "fo").wait(ws);
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("tee buffer size limit exceeded",
                                        expectRead(*left, "o").wait(ws));
    expectRead(*right, "fo").wait(ws);
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("tee buffer size limit exceeded",
                                        expectRead(*right, "o").wait(ws));
  }

  {
    // We guarantee that two pumps started simultaneously will never exceed our buffer size limit.

    auto tee = newTee(heap<MockAsyncInputStream>(text.asBytes(), text.size()), 2);
    auto left = zc::mv(tee.branches[0]);
    auto right = zc::mv(tee.branches[1]);
    auto leftPipe = zc::newOneWayPipe();
    auto rightPipe = zc::newOneWayPipe();

    auto leftPumpPromise = left->pumpTo(*leftPipe.out);
    auto rightPumpPromise = right->pumpTo(*rightPipe.out);
    ZC_EXPECT(!leftPumpPromise.poll(ws));
    ZC_EXPECT(!rightPumpPromise.poll(ws));

    uint8_t leftBuf[11] = {0};
    uint8_t rightBuf[11] = {0};

    // The first read on the left pipe will succeed.
    auto leftPromise = leftPipe.in->tryRead(leftBuf, 1, 11);
    ZC_EXPECT(leftPromise.wait(ws) == 2);
    ZC_EXPECT(zc::arrayPtr(leftBuf, 2) == text.first(2));

    // But the second will block until we relieve pressure on the right pipe.
    leftPromise = leftPipe.in->tryRead(leftBuf + 2, 1, 9);
    ZC_EXPECT(!leftPromise.poll(ws));

    // Relieve the right pipe pressure ...
    auto rightPromise = rightPipe.in->tryRead(rightBuf, 1, 11);
    ZC_EXPECT(rightPromise.wait(ws) == 2);
    ZC_EXPECT(zc::arrayPtr(rightBuf, 2) == text.first(2));

    // Now the second left pipe read will complete.
    ZC_EXPECT(leftPromise.wait(ws) == 2);
    ZC_EXPECT(zc::arrayPtr(leftBuf, 4) == text.first(4));

    // Leapfrog the left branch with the right. There should be 2 bytes in the buffer, so we can
    // demand a total of 4.
    rightPromise = rightPipe.in->tryRead(rightBuf + 2, 4, 9);
    ZC_EXPECT(rightPromise.wait(ws) == 4);
    ZC_EXPECT(zc::arrayPtr(rightBuf, 6) == text.first(6));

    // Leapfrog the right with the left. We demand the entire rest of the stream, so this should
    // block. Note that a regular read for this amount on one of the tee branches directly would
    // exceed our buffer size limit, but this one does not, because we have the pipe to regulate
    // backpressure for us.
    leftPromise = leftPipe.in->tryRead(leftBuf + 4, 7, 7);
    ZC_EXPECT(!leftPromise.poll(ws));

    // Ask for the entire rest of the stream on the right branch and wrap things up.
    rightPromise = rightPipe.in->tryRead(rightBuf + 6, 5, 5);

    ZC_EXPECT(leftPromise.wait(ws) == 7);
    ZC_EXPECT(zc::arrayPtr(leftBuf, 11) == text.first(11));

    ZC_EXPECT(rightPromise.wait(ws) == 5);
    ZC_EXPECT(zc::arrayPtr(rightBuf, 11) == text.first(11));
  }
}

ZC_TEST("Userspace OneWayPipe whenWriteDisconnected()") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  auto abortedPromise = pipe.out->whenWriteDisconnected();
  ZC_ASSERT(!abortedPromise.poll(ws));

  pipe.in = nullptr;

  ZC_ASSERT(abortedPromise.poll(ws));
  abortedPromise.wait(ws);
}

ZC_TEST("Userspace TwoWayPipe whenWriteDisconnected()") {
  zc::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newTwoWayPipe();

  auto abortedPromise = pipe.ends[0]->whenWriteDisconnected();
  ZC_ASSERT(!abortedPromise.poll(ws));

  pipe.ends[1] = nullptr;

  ZC_ASSERT(abortedPromise.poll(ws));
  abortedPromise.wait(ws);
}

#if !_WIN32      // We don't currently support detecting disconnect with IOCP.
#if !__CYGWIN__  // TODO(someday): Figure out why whenWriteDisconnected() doesn't work on Cygwin.

ZC_TEST("OS OneWayPipe whenWriteDisconnected()") {
  auto io = setupAsyncIo();

  auto pipe = io.provider->newOneWayPipe();

  pipe.out->write("foo"_zcb).wait(io.waitScope);
  auto abortedPromise = pipe.out->whenWriteDisconnected();
  ZC_ASSERT(!abortedPromise.poll(io.waitScope));

  pipe.in = nullptr;

  ZC_ASSERT(abortedPromise.poll(io.waitScope));
  abortedPromise.wait(io.waitScope);
}

ZC_TEST("OS TwoWayPipe whenWriteDisconnected()") {
  auto io = setupAsyncIo();

  auto pipe = io.provider->newTwoWayPipe();

  pipe.ends[0]->write("foo"_zcb).wait(io.waitScope);
  pipe.ends[1]->write("bar"_zcb).wait(io.waitScope);

  auto abortedPromise = pipe.ends[0]->whenWriteDisconnected();
  ZC_ASSERT(!abortedPromise.poll(io.waitScope));

  pipe.ends[1] = nullptr;

  ZC_ASSERT(abortedPromise.poll(io.waitScope));
  abortedPromise.wait(io.waitScope);

  char buffer[4]{};
  ZC_ASSERT(pipe.ends[0]->tryRead(&buffer, 3, 3).wait(io.waitScope) == 3);
  buffer[3] = '\0';
  ZC_EXPECT(buffer == "bar"_zc);

  // Note: Reading any further in pipe.ends[0] would throw "connection reset".
}

ZC_TEST("import socket FD that's already broken") {
  auto io = setupAsyncIo();

  int fds[2]{};
  ZC_SYSCALL(socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  ZC_SYSCALL(write(fds[1], "foo", 3));
  ZC_SYSCALL(close(fds[1]));

  auto stream = io.lowLevelProvider->wrapSocketFd(fds[0], LowLevelAsyncIoProvider::TAKE_OWNERSHIP);

  auto abortedPromise = stream->whenWriteDisconnected();
  ZC_ASSERT(abortedPromise.poll(io.waitScope));
  abortedPromise.wait(io.waitScope);

  char buffer[4]{};
  ZC_ASSERT(stream->tryRead(&buffer, sizeof(buffer), sizeof(buffer)).wait(io.waitScope) == 3);
  buffer[3] = '\0';
  ZC_EXPECT(buffer == "foo"_zc);
}

#endif  // !__CYGWIN__
#endif  // !_WIN32

ZC_TEST("AggregateConnectionReceiver") {
  EventLoop loop;
  WaitScope ws(loop);

  auto pipe1 = newCapabilityPipe();
  auto pipe2 = newCapabilityPipe();

  auto receiversBuilder = zc::heapArrayBuilder<Own<ConnectionReceiver>>(2);
  receiversBuilder.add(zc::heap<CapabilityStreamConnectionReceiver>(*pipe1.ends[0]));
  receiversBuilder.add(zc::heap<CapabilityStreamConnectionReceiver>(*pipe2.ends[0]));

  auto aggregate = newAggregateConnectionReceiver(receiversBuilder.finish());

  CapabilityStreamNetworkAddress connector1(zc::none, *pipe1.ends[1]);
  CapabilityStreamNetworkAddress connector2(zc::none, *pipe2.ends[1]);

  auto connectAndWrite = [&](NetworkAddress& addr, zc::StringPtr text) {
    return addr.connect()
        .then([text](Own<AsyncIoStream> stream) {
          auto promise = stream->write(text.asBytes());
          return promise.attach(zc::mv(stream));
        })
        .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });
  };

  auto acceptAndRead = [&](ConnectionReceiver& socket, zc::StringPtr expected) {
    return socket.accept()
        .then([](Own<AsyncIoStream> stream) {
          auto promise = stream->readAllText();
          return promise.attach(zc::mv(stream));
        })
        .then([expected](zc::String actual) { ZC_EXPECT(actual == expected); })
        .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, e); });
  };

  auto connectPromise1 = connectAndWrite(connector1, "foo");
  ZC_EXPECT(!connectPromise1.poll(ws));
  auto connectPromise2 = connectAndWrite(connector2, "bar");
  ZC_EXPECT(!connectPromise2.poll(ws));

  acceptAndRead(*aggregate, "foo").wait(ws);

  auto connectPromise3 = connectAndWrite(connector1, "baz");
  ZC_EXPECT(!connectPromise3.poll(ws));

  acceptAndRead(*aggregate, "bar").wait(ws);
  acceptAndRead(*aggregate, "baz").wait(ws);

  connectPromise1.wait(ws);
  connectPromise2.wait(ws);
  connectPromise3.wait(ws);

  auto acceptPromise1 = acceptAndRead(*aggregate, "qux");
  auto acceptPromise2 = acceptAndRead(*aggregate, "corge");
  auto acceptPromise3 = acceptAndRead(*aggregate, "grault");

  ZC_EXPECT(!acceptPromise1.poll(ws));
  ZC_EXPECT(!acceptPromise2.poll(ws));
  ZC_EXPECT(!acceptPromise3.poll(ws));

  // Cancel one of the acceptors...
  { auto drop = zc::mv(acceptPromise2); }

  connectAndWrite(connector2, "qux").wait(ws);
  connectAndWrite(connector1, "grault").wait(ws);

  acceptPromise1.wait(ws);
  acceptPromise3.wait(ws);
}

ZC_TEST("AggregateConnectionReceiver empty") {
  auto aggregate = newAggregateConnectionReceiver({});
  ZC_EXPECT(aggregate->getPort() == 0);

  int value;
  uint length = sizeof(value);

  ZC_EXPECT_THROW_MESSAGE("receivers.size() > 0", aggregate->getsockopt(0, 0, &value, &length));
}

// =======================================================================================
// Tests for optimized pumpTo() between OS handles. Note that this is only even optimized on
// some OSes (only Linux as of this writing), but the behavior should still be the same on all
// OSes, so we run the tests regardless.

zc::String bigString(size_t size) {
  auto result = zc::heapString(size);
  for (auto i : zc::zeroTo(size)) { result[i] = 'a' + i % 26; }
  return result;
}

ZC_TEST("OS handle pumpTo") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0]);

  {
    auto readPromise = expectRead(*pipe2.ends[1], "foo");
    pipe1.ends[0]->write("foo"_zcb).wait(ws);
    readPromise.wait(ws);
  }

  {
    auto readPromise = expectRead(*pipe2.ends[1], "bar");
    pipe1.ends[0]->write("bar"_zcb).wait(ws);
    readPromise.wait(ws);
  }

  auto two = bigString(2000);
  auto four = bigString(4000);
  auto eight = bigString(8000);
  auto fiveHundred = bigString(500'000);

  {
    auto readPromise = expectRead(*pipe2.ends[1], two);
    pipe1.ends[0]->write(two.asBytes()).wait(ws);
    readPromise.wait(ws);
  }

  {
    auto readPromise = expectRead(*pipe2.ends[1], four);
    pipe1.ends[0]->write(four.asBytes()).wait(ws);
    readPromise.wait(ws);
  }

  {
    auto readPromise = expectRead(*pipe2.ends[1], eight);
    pipe1.ends[0]->write(eight.asBytes()).wait(ws);
    readPromise.wait(ws);
  }

  {
    auto readPromise = expectRead(*pipe2.ends[1], fiveHundred);
    pipe1.ends[0]->write(fiveHundred.asBytes()).wait(ws);
    readPromise.wait(ws);
  }

  ZC_EXPECT(!pump.poll(ws))
  pipe1.ends[0]->shutdownWrite();
  ZC_EXPECT(pump.wait(ws) == 6 + two.size() + four.size() + eight.size() + fiveHundred.size());
}

ZC_TEST("OS handle pumpTo small limit") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0], 500);

  auto text = bigString(1000);

  auto expected = zc::str(text.first(500));

  auto readPromise = expectRead(*pipe2.ends[1], expected);
  pipe1.ends[0]->write(text.asBytes()).wait(ws);
  auto secondWritePromise = pipe1.ends[0]->write(text.asBytes());
  readPromise.wait(ws);
  ZC_EXPECT(pump.wait(ws) == 500);

  expectRead(*pipe1.ends[1], text.slice(500)).wait(ws);
}

ZC_TEST("OS handle pumpTo small limit -- write first then read") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto text = bigString(1000);

  auto expected = zc::str(text.first(500));

  // Initiate the write first and let it put as much in the buffer as possible.
  auto writePromise = pipe1.ends[0]->write(text.asBytes());
  writePromise.poll(ws);

  // Now start the pump.
  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0], 500);

  auto readPromise = expectRead(*pipe2.ends[1], expected);
  writePromise.wait(ws);
  auto secondWritePromise = pipe1.ends[0]->write(text.asBytes());
  readPromise.wait(ws);
  ZC_EXPECT(pump.wait(ws) == 500);

  expectRead(*pipe1.ends[1], text.slice(500)).wait(ws);
}

ZC_TEST("OS handle pumpTo large limit") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0], 750'000);

  auto text = bigString(500'000);

  auto expected = zc::str(text, text.first(250'000));

  auto readPromise = expectRead(*pipe2.ends[1], expected);
  pipe1.ends[0]->write(text.asBytes()).wait(ws);
  auto secondWritePromise = pipe1.ends[0]->write(text.asBytes());
  readPromise.wait(ws);
  ZC_EXPECT(pump.wait(ws) == 750'000);

  expectRead(*pipe1.ends[1], text.slice(250'000)).wait(ws);
}

ZC_TEST("OS handle pumpTo large limit -- write first then read") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto text = bigString(500'000);

  auto expected = zc::str(text, text.first(250'000));

  // Initiate the write first and let it put as much in the buffer as possible.
  auto writePromise = pipe1.ends[0]->write(text.asBytes());
  writePromise.poll(ws);

  // Now start the pump.
  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0], 750'000);

  auto readPromise = expectRead(*pipe2.ends[1], expected);
  writePromise.wait(ws);
  auto secondWritePromise = pipe1.ends[0]->write(text.asBytes());
  readPromise.wait(ws);
  ZC_EXPECT(pump.wait(ws) == 750'000);

  expectRead(*pipe1.ends[1], text.slice(250'000)).wait(ws);
}

#if !_WIN32
zc::String fillWriteBuffer(int fd) {
  // Fill up the write buffer of the given FD and return the contents written. We need to use the
  // raw syscalls to do this because ZC doesn't have a way to know how many bytes made it into the
  // socket buffer.
  auto huge = bigString(4'200'000);

  size_t pos = 0;
  for (;;) {
    ZC_ASSERT(pos < huge.size(), "whoa, big buffer");
    ssize_t n;
    ZC_NONBLOCKING_SYSCALL(n = ::write(fd, huge.begin() + pos, huge.size() - pos));
    if (n < 0) break;
    pos += n;
  }

  return zc::str(huge.first(pos));
}

ZC_TEST("OS handle pumpTo write buffer is full before pump") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto bufferContent = fillWriteBuffer(ZC_ASSERT_NONNULL(pipe2.ends[0]->getFd()));

  // Also prime the input pipe with some buffered bytes.
  auto writePromise = pipe1.ends[0]->write("foo"_zcb);
  writePromise.poll(ws);

  // Start the pump and let it get blocked.
  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0]);
  ZC_EXPECT(!pump.poll(ws));

  // Queue another write, even.
  writePromise = writePromise.then([&]() { return pipe1.ends[0]->write("bar"_zcb); });
  writePromise.poll(ws);

  // See it all go through.
  expectRead(*pipe2.ends[1], bufferContent).wait(ws);
  expectRead(*pipe2.ends[1], "foobar").wait(ws);

  writePromise.wait(ws);

  pipe1.ends[0]->shutdownWrite();
  ZC_EXPECT(pump.wait(ws) == 6);
  pipe2.ends[0]->shutdownWrite();
  ZC_EXPECT(pipe2.ends[1]->readAllText().wait(ws) == "");
}

ZC_TEST("OS handle pumpTo write buffer is full before pump -- and pump ends early") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto bufferContent = fillWriteBuffer(ZC_ASSERT_NONNULL(pipe2.ends[0]->getFd()));

  // Also prime the input pipe with some buffered bytes followed by EOF.
  auto writePromise =
      pipe1.ends[0]->write("foo"_zcb).then([&]() { pipe1.ends[0]->shutdownWrite(); });
  writePromise.poll(ws);

  // Start the pump and let it get blocked.
  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0]);
  ZC_EXPECT(!pump.poll(ws));

  // See it all go through.
  expectRead(*pipe2.ends[1], bufferContent).wait(ws);
  expectRead(*pipe2.ends[1], "foo").wait(ws);

  writePromise.wait(ws);

  ZC_EXPECT(pump.wait(ws) == 3);
  pipe2.ends[0]->shutdownWrite();
  ZC_EXPECT(pipe2.ends[1]->readAllText().wait(ws) == "");
}

ZC_TEST("OS handle pumpTo write buffer is full before pump -- and pump hits limit early") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto bufferContent = fillWriteBuffer(ZC_ASSERT_NONNULL(pipe2.ends[0]->getFd()));

  // Also prime the input pipe with some buffered bytes followed by EOF.
  auto writePromise = pipe1.ends[0]->write("foo"_zcb);
  writePromise.poll(ws);

  // Start the pump and let it get blocked.
  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0], 3);
  ZC_EXPECT(!pump.poll(ws));

  // See it all go through.
  expectRead(*pipe2.ends[1], bufferContent).wait(ws);
  expectRead(*pipe2.ends[1], "foo").wait(ws);

  writePromise.wait(ws);

  ZC_EXPECT(pump.wait(ws) == 3);
  pipe2.ends[0]->shutdownWrite();
  ZC_EXPECT(pipe2.ends[1]->readAllText().wait(ws) == "");
}

ZC_TEST("OS handle pumpTo write buffer is full before pump -- and a lot of data is pumped") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto bufferContent = fillWriteBuffer(ZC_ASSERT_NONNULL(pipe2.ends[0]->getFd()));

  // Also prime the input pipe with some buffered bytes followed by EOF.
  auto text = bigString(500'000);
  auto writePromise = pipe1.ends[0]->write(text.asBytes());
  writePromise.poll(ws);

  // Start the pump and let it get blocked.
  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0]);
  ZC_EXPECT(!pump.poll(ws));

  // See it all go through.
  expectRead(*pipe2.ends[1], bufferContent).wait(ws);
  expectRead(*pipe2.ends[1], text).wait(ws);

  writePromise.wait(ws);

  pipe1.ends[0]->shutdownWrite();
  ZC_EXPECT(pump.wait(ws) == text.size());
  pipe2.ends[0]->shutdownWrite();
  ZC_EXPECT(pipe2.ends[1]->readAllText().wait(ws) == "");
}
#endif

ZC_TEST("pump file to socket") {
  // Tests sendfile() optimization

  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto doTest = [&](zc::Own<const File> file) {
    file->writeAll("foobar"_zcb);

    {
      FileInputStream input(*file);
      auto pipe = ioContext.provider->newTwoWayPipe();
      auto readPromise = pipe.ends[1]->readAllText();
      input.pumpTo(*pipe.ends[0]).wait(ws);
      pipe.ends[0]->shutdownWrite();
      ZC_EXPECT(readPromise.wait(ws) == "foobar");
      ZC_EXPECT(input.getOffset() == 6);
    }

    {
      FileInputStream input(*file);
      auto pipe = ioContext.provider->newTwoWayPipe();
      auto readPromise = pipe.ends[1]->readAllText();
      input.pumpTo(*pipe.ends[0], 3).wait(ws);
      pipe.ends[0]->shutdownWrite();
      ZC_EXPECT(readPromise.wait(ws) == "foo");
      ZC_EXPECT(input.getOffset() == 3);
    }

    {
      FileInputStream input(*file, 3);
      auto pipe = ioContext.provider->newTwoWayPipe();
      auto readPromise = pipe.ends[1]->readAllText();
      input.pumpTo(*pipe.ends[0]).wait(ws);
      pipe.ends[0]->shutdownWrite();
      ZC_EXPECT(readPromise.wait(ws) == "bar");
      ZC_EXPECT(input.getOffset() == 6);
    }

    auto big = bigString(500'000);
    file->writeAll(big);

    {
      FileInputStream input(*file);
      auto pipe = ioContext.provider->newTwoWayPipe();
      auto readPromise = pipe.ends[1]->readAllText();
      input.pumpTo(*pipe.ends[0]).wait(ws);
      pipe.ends[0]->shutdownWrite();
      // Extra parens here so that we don't write the big string to the console on failure...
      ZC_EXPECT((readPromise.wait(ws) == big));
      ZC_EXPECT(input.getOffset() == big.size());
    }
  };

  // Try with an in-memory file. No optimization is possible.
  doTest(zc::newInMemoryFile(zc::nullClock()));

  // Try with a disk file. Should use sendfile().
  auto fs = zc::newDiskFilesystem();
  doTest(fs->getCurrent().createTemporary());
}

}  // namespace
}  // namespace zc
