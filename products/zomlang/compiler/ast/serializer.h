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
#include "zc/core/io.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"

namespace zomlang {
namespace compiler {
namespace ast {

// Forward declarations
class Node;

/// Abstract serializer interface - eliminates format-specific switch statements
/// Following Linus's "good taste" principle: no special cases, clean data structure
class Serializer {
public:
  ZC_DISALLOW_COPY_AND_MOVE(Serializer);

  // Core serialization interface - no format-specific logic here
  virtual void writeNodeStart(const zc::StringPtr nodeType) = 0;
  virtual void writeNodeEnd(const zc::StringPtr nodeType) = 0;
  virtual void writeProperty(const zc::StringPtr name, const zc::StringPtr value) = 0;
  virtual void writeChildStart(const zc::StringPtr name) = 0;
  virtual void writeChildEnd(const zc::StringPtr name) = 0;
  virtual void writeArrayStart(const zc::StringPtr name, size_t count) = 0;
  virtual void writeArrayEnd(const zc::StringPtr name) = 0;
  virtual void writeArrayElement() = 0;
  virtual void writeNull() = 0;

  // Indentation management
  virtual void increaseIndent() = 0;
  virtual void decreaseIndent() = 0;

protected:
  Serializer() noexcept = default;
};

/// JSON serializer implementation
class JSONSerializer final : public Serializer {
public:
  explicit JSONSerializer(zc::OutputStream& output) noexcept;
  ~JSONSerializer() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(JSONSerializer);

  void writeNodeStart(const zc::StringPtr nodeType) final;
  void writeNodeEnd(const zc::StringPtr nodeType) final;
  void writeProperty(const zc::StringPtr name, const zc::StringPtr value) final;
  void writeChildStart(const zc::StringPtr name) final;
  void writeChildEnd(const zc::StringPtr name) final;
  void writeArrayStart(const zc::StringPtr name, size_t count) final;
  void writeArrayEnd(const zc::StringPtr name) final;
  void writeArrayElement() final;
  void writeNull() final;
  void increaseIndent() final;
  void decreaseIndent() final;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// XML serializer implementation
class XMLSerializer final : public Serializer {
public:
  explicit XMLSerializer(zc::OutputStream& output) noexcept;
  ~XMLSerializer() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(XMLSerializer);

  void writeNodeStart(const zc::StringPtr nodeType) final;
  void writeNodeEnd(const zc::StringPtr nodeType) final;
  void writeProperty(const zc::StringPtr name, const zc::StringPtr value) final;
  void writeChildStart(const zc::StringPtr name) final;
  void writeChildEnd(const zc::StringPtr name) final;
  void writeArrayStart(const zc::StringPtr name, size_t count) final;
  void writeArrayEnd(const zc::StringPtr name) final;
  void writeArrayElement() final;
  void writeNull() final;
  void increaseIndent() final;
  void decreaseIndent() final;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// Text serializer implementation (human-readable format)
class TextSerializer final : public Serializer {
public:
  explicit TextSerializer(zc::OutputStream& output) noexcept;
  ~TextSerializer() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TextSerializer);

  void writeNodeStart(const zc::StringPtr nodeType) final;
  void writeNodeEnd(const zc::StringPtr nodeType) final;
  void writeProperty(const zc::StringPtr name, const zc::StringPtr value) final;
  void writeChildStart(const zc::StringPtr name) final;
  void writeChildEnd(const zc::StringPtr name) final;
  void writeArrayStart(const zc::StringPtr name, size_t count) final;
  void writeArrayEnd(const zc::StringPtr name) final;
  void writeArrayElement() final;
  void writeNull() final;
  void increaseIndent() final;
  void decreaseIndent() final;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// Factory function to create serializers - eliminates enum-based switching
/// Returns owned serializer instance based on format string
zc::Own<Serializer> createSerializer(const zc::StringPtr format, zc::OutputStream& output);

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
