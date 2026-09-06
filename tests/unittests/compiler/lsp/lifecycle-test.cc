// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "compiler/lsp/lifecycle.h"

#include "compiler/lsp/json-parse.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::lsp {
namespace {

JsonValue decode(zc::StringPtr text) {
  auto parsed = parseJson(text.asBytes());
  ZC_REQUIRE(parsed != zc::none);
  return zc::mv(ZC_REQUIRE_NONNULL(parsed));
}

LifecycleOutcome send(LifecycleHandler& handler, zc::StringPtr text) {
  auto message = decode(text);
  return handler.handle(message);
}

/// \brief Returns the numeric `error.code`, or none when the value is not an error.
zc::Maybe<int32_t> errorCode(const JsonValue& response) {
  ZC_IF_SOME(error, response.find("error"_zc)) {
    ZC_IF_SOME(code, error.find("code"_zc)) {
      if (code.isNumber()) { return static_cast<int32_t>(code.asNumber()); }
    }
  }
  return zc::none;
}

bool hasResult(const JsonValue& response) { return response.find("result"_zc) != zc::none; }

int32_t code(JsonRpcErrorCode value) { return static_cast<int32_t>(value); }

/// \brief Drives a handler to `Running`.
LifecycleHandler running() {
  LifecycleHandler handler;
  auto initialize = send(handler, R"({"jsonrpc":"2.0","id":1,"method":"initialize"})"_zc);
  ZC_REQUIRE(initialize.action == LifecycleAction::Respond);
  auto initialized = send(handler, R"({"jsonrpc":"2.0","method":"initialized"})"_zc);
  ZC_REQUIRE(initialized.action == LifecycleAction::Silent);
  ZC_REQUIRE(handler.state() == LifecycleState::Running);
  return handler;
}

}  // namespace

ZC_TEST("Lifecycle answers initialize and advances on initialized") {
  LifecycleHandler handler;
  ZC_EXPECT(handler.state() == LifecycleState::Uninitialized);

  auto outcome = send(handler, R"({"jsonrpc":"2.0","id":1,"method":"initialize"})"_zc);
  ZC_EXPECT(outcome.action == LifecycleAction::Respond);
  ZC_EXPECT(hasResult(outcome.response));
  ZC_EXPECT(handler.state() == LifecycleState::Initializing);

  // The result must advertise capabilities, even though this slice has none.
  ZC_IF_SOME(result, outcome.response.find("result"_zc)) {
    ZC_EXPECT(result.find("capabilities"_zc) != zc::none);
  } else {
    ZC_FAIL_EXPECT("initialize must carry a result");
  }

  auto initialized = send(handler, R"({"jsonrpc":"2.0","method":"initialized"})"_zc);
  ZC_EXPECT(initialized.action == LifecycleAction::Silent);
  ZC_EXPECT(handler.state() == LifecycleState::Running);
}

ZC_TEST("Lifecycle echoes the request id in both spellings") {
  LifecycleHandler numeric;
  auto byNumber = send(numeric, R"({"jsonrpc":"2.0","id":7,"method":"initialize"})"_zc);
  ZC_IF_SOME(id, byNumber.response.find("id"_zc)) {
    ZC_EXPECT(id.asNumber() == 7.0);
  } else {
    ZC_FAIL_EXPECT("response must carry an id");
  }

  LifecycleHandler textual;
  auto byString = send(textual, R"({"jsonrpc":"2.0","id":"a1","method":"initialize"})"_zc);
  ZC_IF_SOME(id, byString.response.find("id"_zc)) {
    ZC_EXPECT(id.asString() == "a1");
  } else {
    ZC_FAIL_EXPECT("response must carry an id");
  }
}

ZC_TEST("Lifecycle rejects a request before initialize") {
  LifecycleHandler handler;
  auto outcome = send(handler, R"({"jsonrpc":"2.0","id":1,"method":"textDocument/hover"})"_zc);
  ZC_EXPECT(outcome.action == LifecycleAction::Respond);
  ZC_EXPECT(errorCode(outcome.response) == code(JsonRpcErrorCode::ServerNotInitialized));
  ZC_EXPECT(handler.state() == LifecycleState::Uninitialized);
}

ZC_TEST("Lifecycle rejects a second initialize") {
  auto handler = running();
  auto outcome = send(handler, R"({"jsonrpc":"2.0","id":2,"method":"initialize"})"_zc);
  ZC_EXPECT(errorCode(outcome.response) == code(JsonRpcErrorCode::InvalidRequest));
  ZC_EXPECT(handler.state() == LifecycleState::Running);
}

ZC_TEST("Lifecycle reports an unsupported method once running") {
  auto handler = running();
  auto outcome = send(handler, R"({"jsonrpc":"2.0","id":3,"method":"textDocument/hover"})"_zc);
  ZC_EXPECT(errorCode(outcome.response) == code(JsonRpcErrorCode::MethodNotFound));
}

ZC_TEST("Lifecycle reports document synchronization as unsupported in this slice") {
  auto handler = running();
  // Document sync needs a resolved CrateKey, so it must not be silently
  // accepted while workspace resolution is absent.
  auto outcome = send(handler, R"({"jsonrpc":"2.0","id":4,"method":"textDocument/didOpen"})"_zc);
  ZC_EXPECT(errorCode(outcome.response) == code(JsonRpcErrorCode::MethodNotFound));
}

ZC_TEST("Lifecycle accepts shutdown then exit") {
  auto handler = running();
  auto shutdown = send(handler, R"({"jsonrpc":"2.0","id":5,"method":"shutdown"})"_zc);
  ZC_EXPECT(shutdown.action == LifecycleAction::Respond);
  ZC_EXPECT(hasResult(shutdown.response));
  ZC_EXPECT(handler.state() == LifecycleState::ShuttingDown);

  auto exit = send(handler, R"({"jsonrpc":"2.0","method":"exit"})"_zc);
  ZC_EXPECT(exit.action == LifecycleAction::ExitSuccess);
  ZC_EXPECT(handler.state() == LifecycleState::Exited);
}

ZC_TEST("Lifecycle treats exit without shutdown as a failure") {
  auto handler = running();
  auto outcome = send(handler, R"({"jsonrpc":"2.0","method":"exit"})"_zc);
  ZC_EXPECT(outcome.action == LifecycleAction::ExitFailure);
  ZC_EXPECT(handler.state() == LifecycleState::Exited);
}

ZC_TEST("Lifecycle rejects shutdown before initialize and a repeated shutdown") {
  LifecycleHandler early;
  auto premature = send(early, R"({"jsonrpc":"2.0","id":1,"method":"shutdown"})"_zc);
  ZC_EXPECT(errorCode(premature.response) == code(JsonRpcErrorCode::ServerNotInitialized));

  auto handler = running();
  auto first = send(handler, R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})"_zc);
  ZC_EXPECT(hasResult(first.response));
  auto second = send(handler, R"({"jsonrpc":"2.0","id":3,"method":"shutdown"})"_zc);
  ZC_EXPECT(errorCode(second.response) == code(JsonRpcErrorCode::InvalidRequest));
}

ZC_TEST("Lifecycle rejects a request after shutdown") {
  auto handler = running();
  ZC_EXPECT(
      hasResult(send(handler, R"({"jsonrpc":"2.0","id":1,"method":"shutdown"})"_zc).response));
  auto outcome = send(handler, R"({"jsonrpc":"2.0","id":2,"method":"textDocument/hover"})"_zc);
  ZC_EXPECT(errorCode(outcome.response) == code(JsonRpcErrorCode::InvalidRequest));
}

ZC_TEST("Lifecycle never answers a notification") {
  auto handler = running();
  // Even an unsupported or malformed notification gets no response, because
  // JSON-RPC forbids replying to a message without an id.
  ZC_EXPECT(send(handler, R"({"jsonrpc":"2.0","method":"$/cancelRequest"})"_zc).action ==
            LifecycleAction::Silent);
  ZC_EXPECT(send(handler, R"({"jsonrpc":"2.0"})"_zc).action == LifecycleAction::Silent);
}

ZC_TEST("Lifecycle rejects a structurally invalid request") {
  LifecycleHandler handler;
  auto notObject = handler.handle(JsonValue::string(zc::heapString("nope"_zc)));
  ZC_EXPECT(errorCode(notObject.response) == code(JsonRpcErrorCode::InvalidRequest));

  auto missingMethod = send(handler, R"({"jsonrpc":"2.0","id":1})"_zc);
  ZC_EXPECT(errorCode(missingMethod.response) == code(JsonRpcErrorCode::InvalidRequest));

  auto numericMethod = send(handler, R"({"jsonrpc":"2.0","id":2,"method":9})"_zc);
  ZC_EXPECT(errorCode(numericMethod.response) == code(JsonRpcErrorCode::InvalidRequest));
}

ZC_TEST("Lifecycle builds a null-id parse error response") {
  auto response = LifecycleHandler::parseErrorResponse();
  ZC_EXPECT(errorCode(response) == code(JsonRpcErrorCode::ParseError));
  ZC_IF_SOME(id, response.find("id"_zc)) {
    ZC_EXPECT(id.isNull());
  } else {
    ZC_FAIL_EXPECT("a parse error response must carry a null id");
  }
}

}  // namespace zomlang::compiler::lsp
