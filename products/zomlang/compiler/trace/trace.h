// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"

namespace zomlang {
namespace compiler {
namespace trace {

/// Trace event types
enum class TraceEventType : uint8_t {
  kEnter = 0,  // Function/scope entry
  kExit,       // Function/scope exit
  kInstant,    // Instant event
  kCounter,    // Counter value
  kMetadata,   // Metadata event
};

/// Trace categories for filtering
enum class TraceCategory : uint32_t {
  kNone = 0,
  kLexer = 1 << 0,
  kParser = 1 << 1,
  kChecker = 1 << 2,
  kDriver = 1 << 3,
  kDiagnostics = 1 << 4,
  kMemory = 1 << 5,
  kPerformance = 1 << 6,
  kAll = 0xFFFFFFFF,
};

/// Trace configuration
struct TraceConfig {
  bool enabled = false;
  TraceCategory categoryMask = TraceCategory::kAll;
  size_t maxEvents = 1000000;  // Maximum events to store
  bool enableTimestamps = true;
  bool enableThreadInfo = true;
  zc::StringPtr outputFile = nullptr;
};

/// Individual trace event
struct TraceEvent {
  TraceEventType type;
  TraceCategory category;
  zc::String name;
  zc::String details;
  uint64_t timestamp;  // Nanoseconds since epoch
  uint32_t threadId;
  uint32_t depth;  // Call stack depth

  TraceEvent(TraceEventType t, TraceCategory cat, zc::StringPtr n, zc::StringPtr d = nullptr,
             uint32_t dep = 0);
};

/// Forward declaration
class TraceManager;

/// RAII scope tracer for automatic enter/exit events
class ScopeTracer {
public:
  ScopeTracer(TraceCategory category, zc::StringPtr name, zc::StringPtr details = nullptr);
  ~ScopeTracer();

  ZC_DISALLOW_COPY_AND_MOVE(ScopeTracer);

private:
  TraceCategory category;
  zc::String name;
  bool active;
};

/// Main trace manager - singleton pattern
class TraceManager {
public:
  static TraceManager& getInstance();

  /// Configure tracing
  void configure(const TraceConfig& config);

  /// Check if tracing is enabled for a category
  bool isEnabled(TraceCategory category) const;

  /// Add trace event
  void addEvent(TraceEventType type, TraceCategory category, zc::StringPtr name,
                zc::StringPtr details = nullptr);

  /// Flush events to output
  void flush();

  /// Get current call depth for thread
  uint32_t getCurrentDepth() const;

  /// Increment/decrement call depth
  void incrementDepth();
  void decrementDepth();

  /// Clear all events
  void clear();

  /// Get event count
  size_t getEventCount() const;

private:
  TraceManager() = default;
  ~TraceManager() = default;

  ZC_DISALLOW_COPY_AND_MOVE(TraceManager);

  struct Impl;
  zc::Own<Impl> impl;
};

/// Convenience macros for tracing
#define ZOM_TRACE_CATEGORY_ENABLED(category) \
  (::zomlang::compiler::trace::TraceManager::getInstance().isEnabled(category))

#define ZOM_TRACE_EVENT(category, name, ...)                                                    \
  do {                                                                                          \
    if (ZOM_TRACE_CATEGORY_ENABLED(category)) {                                                 \
      ::zomlang::compiler::trace::TraceManager::getInstance().addEvent(                         \
          ::zomlang::compiler::trace::TraceEventType::kInstant, category, name, ##__VA_ARGS__); \
    }                                                                                           \
  } while (0)

#define ZOM_TRACE_SCOPE(category, name, ...)                                            \
  ::zomlang::compiler::trace::ScopeTracer ZC_UNIQUE_NAME(_trace_scope_)(category, name, \
                                                                        ##__VA_ARGS__)

#define ZOM_TRACE_FUNCTION(category) ZOM_TRACE_SCOPE(category, __FUNCTION__)

#define ZOM_TRACE_COUNTER(category, name, value)                                                 \
  do {                                                                                           \
    if (ZOM_TRACE_CATEGORY_ENABLED(category)) {                                                  \
      ::zomlang::compiler::trace::TraceManager::getInstance().addEvent(                          \
          ::zomlang::compiler::trace::TraceEventType::kCounter, category, name, zc::str(value)); \
    }                                                                                            \
  } while (0)

}  // namespace trace
}  // namespace compiler
}  // namespace zomlang
