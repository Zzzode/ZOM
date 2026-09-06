// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "compiler/lsp/json-value.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"

namespace zomlang::compiler::lsp {

/// \brief The server's position in the LSP lifecycle.
enum class LifecycleState : uint8_t {
  /// No `initialize` has been accepted yet.
  Uninitialized = 0x01,
  /// `initialize` succeeded; `initialized` has not arrived.
  Initializing = 0x02,
  /// The session is serving requests.
  Running = 0x03,
  /// `shutdown` succeeded; only `exit` is still accepted.
  ShuttingDown = 0x04,
  /// `exit` was received; the transport loop must stop.
  Exited = 0x05,
};

/// \brief JSON-RPC error codes this server produces.
///
/// The first four are JSON-RPC 2.0; the last two are LSP reservations.
enum class JsonRpcErrorCode : int32_t {
  ParseError = -32700,
  InvalidRequest = -32600,
  MethodNotFound = -32601,
  InvalidParams = -32602,
  InternalError = -32603,
  ServerNotInitialized = -32002,
  RequestFailed = -32803,
};

/// \brief What the transport loop must do after one handled message.
enum class LifecycleAction : uint8_t {
  /// Write `response` and keep reading.
  Respond = 0x01,
  /// Write nothing and keep reading; the message was a notification.
  Silent = 0x02,
  /// Write nothing and stop the loop with a success status.
  ExitSuccess = 0x03,
  /// Write nothing and stop the loop with a failure status.
  ///
  /// `exit` before `shutdown` is a protocol violation the specification
  /// requires the server to report through its process status.
  ExitFailure = 0x04,
};

/// \brief One handled message: what to write, and whether to keep going.
struct LifecycleOutcome final {
  LifecycleOutcome(LifecycleAction action, JsonValue&& response)
      : action(action), response(zc::mv(response)) {}
  LifecycleOutcome(LifecycleOutcome&&) noexcept = default;
  LifecycleOutcome& operator=(LifecycleOutcome&&) noexcept = default;
  ZC_DISALLOW_COPY(LifecycleOutcome);

  LifecycleAction action;
  /// \brief The response to write; meaningful only when `action` is `Respond`.
  JsonValue response;
};

/// \brief Handles the LSP lifecycle over decoded JSON-RPC messages.
///
/// RFC 0023 keeps JSON at the transport boundary, so this type consumes an
/// already-decoded `JsonValue` and returns one, never bytes. It owns only
/// lifecycle state: it answers `initialize`, `initialized`, `shutdown`, and
/// `exit`, and rejects everything else with `MethodNotFound`. Document
/// synchronization is deliberately absent because `EditorDocumentAdapter`
/// requires a resolved `CrateKey`, which needs workspace resolution that is not
/// yet part of this slice.
///
/// Ordering is enforced: a request before `initialize` is
/// `ServerNotInitialized`, a second `initialize` is `InvalidRequest`, and any
/// request after `shutdown` is `InvalidRequest`. `exit` without a preceding
/// `shutdown` stops the loop with a failure status.
class LifecycleHandler final {
public:
  LifecycleHandler();
  ~LifecycleHandler() noexcept;
  LifecycleHandler(LifecycleHandler&&) noexcept;
  LifecycleHandler& operator=(LifecycleHandler&&) noexcept;
  ZC_DISALLOW_COPY(LifecycleHandler);

  /// \brief Returns the current lifecycle state.
  ZC_NODISCARD LifecycleState state() const noexcept;

  /// \brief Handles one decoded JSON-RPC message.
  ///
  /// \param message The decoded message; any JSON value is accepted, and a
  ///        structurally invalid one yields an `InvalidRequest` response.
  /// \return The response to write and whether the loop continues.
  ZC_NODISCARD LifecycleOutcome handle(const JsonValue& message);

  /// \brief Builds a JSON-RPC error response for a payload that failed to parse.
  ///
  /// The id is null because an unparsable payload has no recoverable id.
  ZC_NODISCARD static JsonValue parseErrorResponse();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::lsp
