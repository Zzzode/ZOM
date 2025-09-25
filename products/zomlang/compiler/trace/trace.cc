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

#include "zomlang/compiler/trace/trace.h"

#include <chrono>
#include <thread>

#include "zc/core/debug.h"
#include "zc/core/filesystem.h"
#include "zc/core/map.h"
#include "zc/core/mutex.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/basic/string-escape.h"
#include "zomlang/compiler/trace/trace-config.h"

namespace zomlang {
namespace compiler {
namespace trace {

// ================================================================================
// TraceEvent

TraceEvent::TraceEvent(TraceEventType t, TraceCategory cat, zc::StringPtr n, zc::StringPtr d,
                       uint32_t dep) noexcept
    : type(t), category(cat), name(zc::str(n)), depth(dep) {
  if (d != nullptr) { details = zc::str(d); }

  // Get current timestamp in nanoseconds
  auto now = std::chrono::high_resolution_clock::now();
  auto epoch = now.time_since_epoch();
  timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch).count();

  // Get thread ID
  auto tid = std::this_thread::get_id();
  threadId = std::hash<std::thread::id>{}(tid);
}

// ================================================================================
// TraceManager::Impl

struct TraceManager::Impl {
  TraceConfig config;
  mutable zc::MutexGuarded<zc::Vector<TraceEvent>> events;
  mutable zc::MutexGuarded<zc::HashMap<uint32_t, uint32_t>> threadDepths;

  Impl() {
    // Initialize with default config
    config.enabled = false;
    config.categoryMask = TraceCategory::kAll;
    config.maxEvents = 1000000;
    config.enableTimestamps = true;
    config.enableThreadInfo = true;
  }

  uint32_t getCurrentThreadId() const {
    std::thread::id tid = std::this_thread::get_id();
    return std::hash<std::thread::id>{}(tid);
  }

  uint32_t getDepthForThread(uint32_t threadId) const {
    auto locked = threadDepths.lockShared();
    ZC_IF_SOME(depth, locked->find(threadId)) { return depth; }
    return 0;
  }

  void setDepthForThread(uint32_t threadId, uint32_t depth) {
    auto locked = threadDepths.lockExclusive();
    locked->upsert(threadId, depth);
  }
};

// ================================================================================
// TraceManager

// static
TraceManager& TraceManager::getInstance() {
  static TraceManager instance;
  if (instance.impl.get() == nullptr) { instance.impl = zc::heap<Impl>(); }
  return instance;
}

void TraceManager::configure(const TraceConfig& config) {
  ZC_REQUIRE(impl.get() != nullptr);

  zc::Locked<zc::Vector<TraceEvent>> lock = impl->events.lockExclusive();
  impl->config = config;

  // Clear existing events if disabled
  if (!config.enabled) { lock->clear(); }
}

bool TraceManager::isEnabled(TraceCategory category) const {
  ZC_REQUIRE(impl.get() != nullptr);

  if (!impl->config.enabled) { return false; }

  return (static_cast<uint32_t>(impl->config.categoryMask) & static_cast<uint32_t>(category)) != 0;
}

void TraceManager::addEvent(TraceEventType type, TraceCategory category, zc::StringPtr name,
                            zc::StringPtr details) {
  ZC_REQUIRE(impl.get() != nullptr);

  if (!isEnabled(category)) { return; }

  zc::Locked<zc::Vector<TraceEvent>> lock = impl->events.lockExclusive();

  // Check if we've exceeded the maximum number of events
  if (lock->size() >= impl->config.maxEvents) {
    // Remove oldest events (simple FIFO)
    size_t removeCount = lock->size() / 10;  // Remove 10%
    // Move remaining events to front and truncate
    for (size_t i = removeCount; i < lock->size(); ++i) {
      (*lock)[i - removeCount] = zc::mv((*lock)[i]);
    }
    lock->truncate(lock->size() - removeCount);
  }

  // Get current depth
  uint32_t depth = getCurrentDepth();

  lock->add(TraceEvent(type, category, name, details, depth));
}

void TraceManager::flush() {
  ZC_REQUIRE(impl.get() != nullptr);

  zc::Locked<zc::Vector<TraceEvent>> lock = impl->events.lockExclusive();

  if (impl->config.outputFile != nullptr) {
    ZC_IF_SOME(exception, writeTraceToFile(impl->config.outputFile, *lock)) {
      ZC_LOG(ERROR, "Failed to write trace file", impl->config.outputFile, exception);
    }
  }
}

zc::Maybe<zc::Exception> TraceManager::writeTraceToFile(zc::StringPtr filename,
                                                        const zc::Vector<TraceEvent>& events) {
  // Create Chrome tracing JSON format
  zc::String json = generateChromeTracingJson(events);

  // Write to file using zc::runCatchingExceptions for proper error handling
  return zc::runCatchingExceptions([&]() {
    // Use zc filesystem to write the file
    auto fs = zc::newDiskFilesystem();

    // Use Path::eval to handle both absolute and relative paths correctly
    zc::Path outputPath = zc::Path(nullptr).eval(filename);
    auto file = fs->getCurrent().openFile(
        outputPath, zc::WriteMode::CREATE | zc::WriteMode::MODIFY | zc::WriteMode::CREATE_PARENT);
    file->writeAll(json);
    file->datasync();
  });
}

zc::String TraceManager::generateChromeTracingJson(const zc::Vector<TraceEvent>& events) {
  zc::Vector<char> json;

  // Start JSON array
  json.addAll(zc::StringPtr("{\"traceEvents\":[\n"));

  bool first = true;
  for (const auto& event : events) {
    if (!first) { json.addAll(zc::StringPtr(",\n")); }
    first = false;

    // Convert event to Chrome tracing format
    zc::String eventJson = formatEventAsJson(event);
    json.addAll(eventJson);
  }

  // End JSON
  json.addAll(zc::StringPtr("\n]}"));

  return zc::str(json.releaseAsArray());
}

zc::String TraceManager::formatEventAsJson(const TraceEvent& event) {
  // Map our event types to Chrome tracing format
  const char* phase;
  switch (event.type) {
    case TraceEventType::kEnter:
      phase = "B";  // Begin
      break;
    case TraceEventType::kExit:
      phase = "E";  // End
      break;
    case TraceEventType::kInstant:
      phase = "i";  // Instant
      break;
    case TraceEventType::kCounter:
      phase = "C";  // Counter
      break;
    case TraceEventType::kMetadata:
      phase = "M";  // Metadata
      break;
    default:
      phase = "i";
      break;
  }

  // Convert category to string
  const char* categoryStr;
  switch (event.category) {
    case TraceCategory::kLexer:
      categoryStr = "lexer";
      break;
    case TraceCategory::kParser:
      categoryStr = "parser";
      break;
    case TraceCategory::kChecker:
      categoryStr = "checker";
      break;
    case TraceCategory::kDriver:
      categoryStr = "driver";
      break;
    case TraceCategory::kDiagnostics:
      categoryStr = "diagnostics";
      break;
    case TraceCategory::kMemory:
      categoryStr = "memory";
      break;
    case TraceCategory::kPerformance:
      categoryStr = "performance";
      break;
    default:
      categoryStr = "unknown";
      break;
  }

  // Convert timestamp from nanoseconds to microseconds (Chrome format)
  uint64_t timestampUs = event.timestamp / 1000;

  // Build JSON object
  zc::String baseJson =
      zc::str("  {", "\"name\":\"", basic::escapeJsonString(event.name), "\",", "\"cat\":\"",
              categoryStr, "\",", "\"ph\":\"", phase, "\",", "\"ts\":", timestampUs, ",",
              "\"pid\":1,", "\"tid\":", event.threadId);

  // Add details if present
  if (event.details.size() > 0) {
    return zc::str(baseJson, ",\"args\":{\"details\":\"", basic::escapeJsonString(event.details),
                   "\"}}");
  }

  return zc::str(baseJson, "}");
}

uint32_t TraceManager::getCurrentDepth() const {
  ZC_REQUIRE(impl.get() != nullptr);

  uint32_t threadId = impl->getCurrentThreadId();
  return impl->getDepthForThread(threadId);
}

void TraceManager::incrementDepth() {
  ZC_REQUIRE(impl.get() != nullptr);

  uint32_t threadId = impl->getCurrentThreadId();
  uint32_t currentDepth = impl->getDepthForThread(threadId);
  impl->setDepthForThread(threadId, currentDepth + 1);
}

void TraceManager::decrementDepth() {
  ZC_REQUIRE(impl.get() != nullptr);

  uint32_t threadId = impl->getCurrentThreadId();
  uint32_t currentDepth = impl->getDepthForThread(threadId);
  if (currentDepth > 0) { impl->setDepthForThread(threadId, currentDepth - 1); }
}

void TraceManager::clear() {
  ZC_REQUIRE(impl.get() != nullptr);

  zc::Locked<zc::Vector<TraceEvent>> lock = impl->events.lockExclusive();
  lock->clear();

  auto depthLock = impl->threadDepths.lockExclusive();
  depthLock->clear();
}

size_t TraceManager::getEventCount() const {
  ZC_REQUIRE(impl.get() != nullptr);

  auto lock = impl->events.lockShared();
  return lock->size();
}

uint32_t TraceManager::getMaxRecursionDepth() const {
  ZC_REQUIRE(impl.get() != nullptr);
  return impl->config.maxRecursionDepth;
}

// ================================================================================
// Trace functions

/// Compile-time trace enablement check
constexpr bool kTraceEnabled = ZOM_TRACE_ENABLED;

void traceEvent(TraceCategory category, zc::StringPtr name, zc::StringPtr details) {
  if constexpr (kTraceEnabled) {
    if (TraceManager::getInstance().isEnabled(category)) {
      TraceManager::getInstance().addEvent(TraceEventType::kInstant, category, name, details);
    }
  }
}

void traceCounter(TraceCategory category, zc::StringPtr name, zc::StringPtr details) {
  if constexpr (kTraceEnabled) {
    if (TraceManager::getInstance().isEnabled(category)) {
      TraceManager::getInstance().addEvent(TraceEventType::kCounter, category, name, details);
    }
  }
}

// ================================================================================
// ScopeTracer::Impl

struct ScopeTracer::Impl {
  bool enabled;
  TraceCategory category;
  zc::String name;
  zc::String details;

  Impl(TraceCategory cat, zc::StringPtr n, zc::StringPtr d)
      : enabled(false), category(cat), name(zc::str(n)) {
    if (d != nullptr) { details = zc::str(d); }

    if constexpr (kTraceEnabled) {
      if (TraceManager::getInstance().isEnabled(category)) {
        // Check recursion depth before incrementing
        uint32_t currentDepth = TraceManager::getInstance().getCurrentDepth();
        uint32_t maxDepth = TraceManager::getInstance().getMaxRecursionDepth();

        if (currentDepth >= maxDepth) {
          ZC_FAIL_REQUIRE("Trace recursion depth exceeded maximum limit", currentDepth,
                          ">=", maxDepth, "category:", static_cast<uint32_t>(category),
                          "name:", name);
        }

        enabled = true;
        TraceManager::getInstance().incrementDepth();
        TraceManager::getInstance().addEvent(TraceEventType::kEnter, category, name, details);
      }
    }
  }

  ~Impl() {
    if constexpr (kTraceEnabled) {
      if (enabled) {
        TraceManager::getInstance().addEvent(TraceEventType::kExit, category, name, details);
        TraceManager::getInstance().decrementDepth();
      }
    }
  }
};

// ================================================================================
// ScopeTracer

ScopeTracer::ScopeTracer(TraceCategory category, zc::StringPtr name, zc::StringPtr details) noexcept
    : impl(zc::heap<Impl>(category, name, details)) {}

ScopeTracer::~ScopeTracer() noexcept(false) = default;

// ================================================================================
// FunctionTracer::Impl

struct FunctionTracer::Impl {
  ScopeTracer scopeTracer;

  Impl(TraceCategory category, zc::StringPtr functionName) : scopeTracer(category, functionName) {}
};

// ================================================================================
// FunctionTracer

FunctionTracer::FunctionTracer(TraceCategory category, zc::StringPtr functionName)
    : impl(zc::heap<Impl>(category, functionName)) {}

FunctionTracer::~FunctionTracer() = default;

}  // namespace trace
}  // namespace compiler
}  // namespace zomlang
