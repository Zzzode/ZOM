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

#include "zomlang/compiler/trace/trace-config.h"

#include <cstdlib>
#include <cstring>

#include "zc/core/string.h"
#include "zomlang/compiler/trace/trace.h"

namespace zomlang {
namespace compiler {
namespace trace {

bool RuntimeConfig::shouldEnableFromEnvironment() {
  const char* env = std::getenv("ZOM_TRACE_ENABLED");
  if (env == nullptr) { return false; }

  // Check for various "true" values
  return (std::strcmp(env, "1") == 0 || std::strcmp(env, "true") == 0 ||
          std::strcmp(env, "TRUE") == 0 || std::strcmp(env, "on") == 0 ||
          std::strcmp(env, "ON") == 0);
}

const char* RuntimeConfig::getOutputFileFromEnvironment() {
  return std::getenv("ZOM_TRACE_OUTPUT");
}

uint32_t RuntimeConfig::getCategoryMaskFromEnvironment() {
  const char* env = std::getenv("ZOM_TRACE_CATEGORIES");
  if (env == nullptr) { return static_cast<uint32_t>(TraceCategory::kAll); }

  uint32_t mask = 0;
  zc::StringPtr categories(env);

  // Parse comma-separated category names
  size_t start = 0;
  while (start < categories.size()) {
    ZC_IF_SOME(commaPos, categories.slice(start).findFirst(',')) {
      // Create a null-terminated string for the category
      zc::String categoryStr = zc::str(categories.slice(start, start + commaPos));
      start = start + commaPos + 1;

      // Process category
      processCategoryName(categoryStr, mask);
    }
    else {
      // Last category (no comma found)
      zc::String categoryStr = zc::str(categories.slice(start));
      processCategoryName(categoryStr, mask);
      break;
    }
  }

  return mask;
}

void RuntimeConfig::processCategoryName(const zc::StringPtr category, uint32_t& mask) {
  // Simple trim by finding start and end positions
  zc::StringPtr categoryPtr = category;
  size_t start = 0;
  size_t end = categoryPtr.size();

  // Trim leading whitespace
  while (start < end && (categoryPtr[start] == ' ' || categoryPtr[start] == '\t')) { start++; }

  // Trim trailing whitespace
  while (end > start && (categoryPtr[end - 1] == ' ' || categoryPtr[end - 1] == '\t')) { end--; }

  // Create trimmed category string
  zc::String trimmedCategory = zc::str(categoryPtr.slice(start, end));
  zc::StringPtr finalCategory = trimmedCategory;

  if (finalCategory == "lexer") {
    mask |= static_cast<uint32_t>(TraceCategory::kLexer);
  } else if (finalCategory == "parser") {
    mask |= static_cast<uint32_t>(TraceCategory::kParser);
  } else if (finalCategory == "checker") {
    mask |= static_cast<uint32_t>(TraceCategory::kChecker);
  } else if (finalCategory == "driver") {
    mask |= static_cast<uint32_t>(TraceCategory::kDriver);
  } else if (finalCategory == "diagnostics") {
    mask |= static_cast<uint32_t>(TraceCategory::kDiagnostics);
  } else if (finalCategory == "memory") {
    mask |= static_cast<uint32_t>(TraceCategory::kMemory);
  } else if (finalCategory == "performance") {
    mask |= static_cast<uint32_t>(TraceCategory::kPerformance);
  } else if (finalCategory == "all") {
    mask = static_cast<uint32_t>(TraceCategory::kAll);
  }

  if (mask == 0) { mask = static_cast<uint32_t>(TraceCategory::kAll); }
}

}  // namespace trace
}  // namespace compiler
}  // namespace zomlang
