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

#include "zomlang/compiler/basic/string-escape.h"

#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang {
namespace compiler {
namespace basic {

zc::String escapeJsonString(zc::StringPtr str) {
  zc::Vector<char> result;

  for (char c : str) {
    switch (c) {
      case '"':
        result.addAll("\\\""_zc.asArray());
        break;
      case '\\':
        result.addAll("\\\\"_zc.asArray());
        break;
      case '\b':
        result.addAll("\\b"_zc.asArray());
        break;
      case '\f':
        result.addAll("\\f"_zc.asArray());
        break;
      case '\n':
        result.addAll("\\n"_zc.asArray());
        break;
      case '\r':
        result.addAll("\\r"_zc.asArray());
        break;
      case '\t':
        result.addAll("\\t"_zc.asArray());
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          // Control characters need to be escaped as \uXXXX
          result.addAll("\\u00"_zc.asArray());
          result.addAll(zc::hex(static_cast<unsigned char>(c)));
        } else {
          result.add(c);
        }
        break;
    }
  }

  return zc::str(result.asPtr());
}

zc::String escapeXmlString(zc::StringPtr str) {
  zc::Vector<char> result;

  for (char c : str) {
    switch (c) {
      case '&':
        result.addAll("&amp;"_zc.asArray());
        break;
      case '<':
        result.addAll("&lt;"_zc.asArray());
        break;
      case '>':
        result.addAll("&gt;"_zc.asArray());
        break;
      case '"':
        result.addAll("&quot;"_zc.asArray());
        break;
      case '\'':
        result.addAll("&apos;"_zc.asArray());
        break;
      default:
        result.add(c);
        break;
    }
  }

  return zc::str(result.asPtr());
}

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang
