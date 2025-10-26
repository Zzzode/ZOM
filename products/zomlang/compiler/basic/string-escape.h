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

/// \file string-escape.h
/// \brief String escaping utilities for JSON, XML and other formats
///
/// This module provides common string escaping utilities used across the compiler,
/// including functions for JSON and XML string escaping that comply with their
/// respective specifications.

#pragma once

#include "zc/core/string.h"

namespace zomlang {
namespace compiler {
namespace basic {

/// \brief Escape a string for safe inclusion in JSON
///
/// This function escapes special characters in a string to make it safe
/// for inclusion in JSON output. It handles:
/// - Quote characters (")
/// - Backslashes (\)
/// - Control characters (\n, \r, \t, \b, \f)
/// - Other control characters (converted to \uXXXX format)
///
/// \param str The input string to escape
/// \return A new string with all necessary characters escaped
zc::String escapeJsonString(zc::StringPtr str);

/// \brief Escape a string for safe inclusion in XML
///
/// This function escapes special characters in a string to make it safe
/// for inclusion in XML output. It handles:
/// - Ampersand (&)
/// - Less than (<)
/// - Greater than (>)
/// - Quote characters (")
/// - Apostrophe (')
///
/// \param str The input string to escape
/// \return A new string with all necessary characters escaped
zc::String escapeXmlString(zc::StringPtr str);

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang