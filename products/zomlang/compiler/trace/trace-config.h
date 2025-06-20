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

#include "zc/core/string.h"

/// Compile-time trace configuration
/// These macros can be overridden via CMake or compiler flags

#ifndef ZOM_TRACE_ENABLED
#ifdef NDEBUG
#define ZOM_TRACE_ENABLED 0  // Disabled in release builds by default
#else
#define ZOM_TRACE_ENABLED 1  // Enabled in debug builds by default
#endif
#endif

#ifndef ZOM_TRACE_MAX_DEPTH
#define ZOM_TRACE_MAX_DEPTH 1000  // Maximum call stack depth to trace
#endif

#ifndef ZOM_TRACE_BUFFER_SIZE
#define ZOM_TRACE_BUFFER_SIZE 1000000  // Default maximum events
#endif

/// Performance optimization: completely remove trace code in release builds
#if !ZOM_TRACE_ENABLED

#undef ZOM_TRACE_EVENT
#undef ZOM_TRACE_SCOPE
#undef ZOM_TRACE_FUNCTION
#undef ZOM_TRACE_COUNTER

#define ZOM_TRACE_EVENT(category, name, ...) \
  do {                                       \
  } while (0)
#define ZOM_TRACE_SCOPE(category, name, ...) \
  do {                                       \
  } while (0)
#define ZOM_TRACE_FUNCTION(category) \
  do {                               \
  } while (0)
#define ZOM_TRACE_COUNTER(category, name, value) \
  do {                                           \
  } while (0)

#endif  // !ZOM_TRACE_ENABLED

namespace zomlang {
namespace compiler {
namespace trace {

/// Runtime trace configuration helpers
struct RuntimeConfig {
  /// Check if tracing should be enabled based on environment variables
  static bool shouldEnableFromEnvironment();

  /// Get trace output file from environment
  static const char* getOutputFileFromEnvironment();

  /// Get trace category mask from environment variable
  static uint32_t getCategoryMaskFromEnvironment();

private:
  /// Helper to process individual category name
  static void processCategoryName(const zc::StringPtr category, uint32_t& mask);
};

}  // namespace trace
}  // namespace compiler
}  // namespace zomlang
