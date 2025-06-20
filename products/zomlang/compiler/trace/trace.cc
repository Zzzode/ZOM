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
#include "zc/core/map.h"
#include "zc/core/mutex.h"

namespace zomlang {
namespace compiler {
namespace trace {

// ================================================================================
// TraceEvent

TraceEvent::TraceEvent(TraceEventType t, TraceCategory cat, zc::StringPtr n, zc::StringPtr d,
                       uint32_t dep)
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
// ScopeTracer

ScopeTracer::ScopeTracer(TraceCategory cat, zc::StringPtr n, zc::StringPtr details)
    : category(cat), name(zc::str(n)), active(false) {
  TraceManager& manager = TraceManager::getInstance();
  if (manager.isEnabled(category)) {
    active = true;
    manager.addEvent(TraceEventType::kEnter, category, name, details);
    manager.incrementDepth();
  }
}

ScopeTracer::~ScopeTracer() {
  if (active) {
    TraceManager& manager = TraceManager::getInstance();
    manager.decrementDepth();
    manager.addEvent(TraceEventType::kExit, category, name);
  }
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
    // TODO: Implement file output in JSON format for Chrome tracing
    // For now, just output to debug log
    ZC_LOG(INFO, "Trace flush requested - ", lock->size(), " events");
  }
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

}  // namespace trace
}  // namespace compiler
}  // namespace zomlang
