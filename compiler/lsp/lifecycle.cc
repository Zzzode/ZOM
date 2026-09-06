// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/lsp/lifecycle.h"

#include "zc/core/debug.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::lsp {
namespace {

constexpr zc::StringPtr kJsonRpcVersion = "2.0"_zc;

/// \brief Builds one object member.
JsonMember member(zc::StringPtr key, JsonValue&& value) {
  return JsonMember{zc::heapString(key), zc::mv(value)};
}

/// \brief Builds an object from an owned member list.
JsonValue objectOf(zc::Vector<JsonMember>&& members) {
  return JsonValue::object(members.releaseAsArray());
}

/// \brief Clones the subset of a JSON value that may appear as a request id.
///
/// JSON-RPC allows a string, a number, or null. Anything else is not a valid
/// id, and echoing it back would propagate the client's error.
JsonValue cloneId(zc::Maybe<const JsonValue&> id) {
  ZC_IF_SOME(value, id) {
    if (value.isString()) { return JsonValue::string(zc::heapString(value.asString())); }
    if (value.isNumber()) {
      auto number = JsonValue::number(value.asNumber());
      ZC_IF_SOME(built, number) { return zc::mv(built); }
      return JsonValue::null();
    }
  }
  return JsonValue::null();
}

/// \brief Builds a JSON-RPC error response.
JsonValue errorResponse(JsonValue&& id, JsonRpcErrorCode code, zc::StringPtr message) {
  zc::Vector<JsonMember> error(2);
  auto codeValue = JsonValue::number(static_cast<double>(static_cast<int32_t>(code)));
  error.add(member("code"_zc, ZC_ASSERT_NONNULL(zc::mv(codeValue))));
  error.add(member("message"_zc, JsonValue::string(zc::heapString(message))));

  zc::Vector<JsonMember> response(3);
  response.add(member("jsonrpc"_zc, JsonValue::string(zc::heapString(kJsonRpcVersion))));
  response.add(member("id"_zc, zc::mv(id)));
  response.add(member("error"_zc, objectOf(zc::mv(error))));
  return objectOf(zc::mv(response));
}

/// \brief Builds a JSON-RPC success response.
JsonValue resultResponse(JsonValue&& id, JsonValue&& result) {
  zc::Vector<JsonMember> response(3);
  response.add(member("jsonrpc"_zc, JsonValue::string(zc::heapString(kJsonRpcVersion))));
  response.add(member("id"_zc, zc::mv(id)));
  response.add(member("result"_zc, zc::mv(result)));
  return objectOf(zc::mv(response));
}

/// \brief Builds the `initialize` result.
///
/// Capabilities are deliberately empty: this slice serves the lifecycle only,
/// and advertising a capability the server cannot honor would be a false
/// contract with the client.
JsonValue initializeResult() {
  zc::Vector<JsonMember> serverInfo(2);
  serverInfo.add(member("name"_zc, JsonValue::string(zc::heapString("zomc"_zc))));

  zc::Vector<JsonMember> result(2);
  result.add(member("capabilities"_zc, objectOf(zc::Vector<JsonMember>())));
  result.add(member("serverInfo"_zc, objectOf(zc::mv(serverInfo))));
  return objectOf(zc::mv(result));
}

}  // namespace

struct LifecycleHandler::Impl final {
  LifecycleState state = LifecycleState::Uninitialized;
};

LifecycleHandler::LifecycleHandler() : impl(zc::heap<Impl>()) {}
LifecycleHandler::~LifecycleHandler() noexcept = default;
LifecycleHandler::LifecycleHandler(LifecycleHandler&&) noexcept = default;
LifecycleHandler& LifecycleHandler::operator=(LifecycleHandler&&) noexcept = default;

LifecycleState LifecycleHandler::state() const noexcept { return impl->state; }

JsonValue LifecycleHandler::parseErrorResponse() {
  return errorResponse(JsonValue::null(), JsonRpcErrorCode::ParseError, "parse error"_zc);
}

LifecycleOutcome LifecycleHandler::handle(const JsonValue& message) {
  if (!message.isObject()) {
    return LifecycleOutcome{LifecycleAction::Respond,
                            errorResponse(JsonValue::null(), JsonRpcErrorCode::InvalidRequest,
                                          "expected an object"_zc)};
  }

  auto methodValue = message.find("method"_zc);
  auto idValue = message.find("id"_zc);
  // A message without an id is a notification: the specification forbids
  // answering it, even when it is invalid.
  const bool isNotification = idValue == zc::none;
  auto id = cloneId(idValue);

  if (methodValue == zc::none || !ZC_ASSERT_NONNULL(methodValue).isString()) {
    if (isNotification) { return LifecycleOutcome{LifecycleAction::Silent, JsonValue::null()}; }
    return LifecycleOutcome{LifecycleAction::Respond,
                            errorResponse(zc::mv(id), JsonRpcErrorCode::InvalidRequest,
                                          "missing or non-string method"_zc)};
  }
  const zc::StringPtr method = ZC_ASSERT_NONNULL(methodValue).asString();

  if (method == "exit"_zc) {
    const bool orderly = impl->state == LifecycleState::ShuttingDown;
    impl->state = LifecycleState::Exited;
    return LifecycleOutcome{orderly ? LifecycleAction::ExitSuccess : LifecycleAction::ExitFailure,
                            JsonValue::null()};
  }

  if (method == "initialized"_zc) {
    if (impl->state == LifecycleState::Initializing) { impl->state = LifecycleState::Running; }
    return LifecycleOutcome{LifecycleAction::Silent, JsonValue::null()};
  }

  if (method == "initialize"_zc) {
    if (isNotification) { return LifecycleOutcome{LifecycleAction::Silent, JsonValue::null()}; }
    if (impl->state != LifecycleState::Uninitialized) {
      return LifecycleOutcome{LifecycleAction::Respond,
                              errorResponse(zc::mv(id), JsonRpcErrorCode::InvalidRequest,
                                            "server is already initialized"_zc)};
    }
    impl->state = LifecycleState::Initializing;
    return LifecycleOutcome{LifecycleAction::Respond,
                            resultResponse(zc::mv(id), initializeResult())};
  }

  if (method == "shutdown"_zc) {
    if (isNotification) { return LifecycleOutcome{LifecycleAction::Silent, JsonValue::null()}; }
    if (impl->state == LifecycleState::Uninitialized) {
      return LifecycleOutcome{LifecycleAction::Respond,
                              errorResponse(zc::mv(id), JsonRpcErrorCode::ServerNotInitialized,
                                            "server is not initialized"_zc)};
    }
    if (impl->state == LifecycleState::ShuttingDown) {
      return LifecycleOutcome{LifecycleAction::Respond,
                              errorResponse(zc::mv(id), JsonRpcErrorCode::InvalidRequest,
                                            "server is already shutting down"_zc)};
    }
    impl->state = LifecycleState::ShuttingDown;
    return LifecycleOutcome{LifecycleAction::Respond,
                            resultResponse(zc::mv(id), JsonValue::null())};
  }

  // Every other method: order first, then unsupported.
  if (isNotification) { return LifecycleOutcome{LifecycleAction::Silent, JsonValue::null()}; }
  if (impl->state == LifecycleState::Uninitialized) {
    return LifecycleOutcome{LifecycleAction::Respond,
                            errorResponse(zc::mv(id), JsonRpcErrorCode::ServerNotInitialized,
                                          "server is not initialized"_zc)};
  }
  if (impl->state == LifecycleState::ShuttingDown) {
    return LifecycleOutcome{
        LifecycleAction::Respond,
        errorResponse(zc::mv(id), JsonRpcErrorCode::InvalidRequest, "server is shutting down"_zc)};
  }
  return LifecycleOutcome{
      LifecycleAction::Respond,
      errorResponse(zc::mv(id), JsonRpcErrorCode::MethodNotFound, "unsupported method"_zc)};
}

}  // namespace zomlang::compiler::lsp
