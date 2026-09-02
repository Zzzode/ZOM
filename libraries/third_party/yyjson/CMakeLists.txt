# yyjson: a high-performance MIT-licensed JSON library (single translation unit).
# Vendored from https://github.com/ibireme/yyjson tag 0.9.0; see
# third-party-manifest.json for the pinned version, source URL, tarball SHA-256,
# license, and per-file digests (verified by scripts/check-third-party.py).
#
# Only the LSP transport layer (compiler/lsp) links this, and only behind its own
# JsonValue adapter; yyjson types never cross that boundary.
add_library(yyjson STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/yyjson.c)
set_target_properties(
  yyjson
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF
             C_VISIBILITY_PRESET hidden)
target_include_directories(yyjson SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
