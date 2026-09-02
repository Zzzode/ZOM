add_subdirectory(basic)
add_subdirectory(query)
add_subdirectory(diagnostics)
add_subdirectory(driver)
add_subdirectory(identity)
add_subdirectory(lexer)
add_subdirectory(parser)
add_subdirectory(source)
add_subdirectory(checker)
add_subdirectory(ast)
add_subdirectory(trace)
add_subdirectory(binder)
add_subdirectory(type)
add_subdirectory(cst)
add_subdirectory(format)
add_subdirectory(ir)
add_subdirectory(hir)
add_subdirectory(mir)
add_subdirectory(lir)
add_subdirectory(ownership)
add_subdirectory(ide)
add_subdirectory(lsp)

# RFC 0023: the production parser now emits the recoverable CST event stream,
# and the CST verifier replays it through the AST schema authority.
target_link_libraries(cst PUBLIC ast diagnostics identity lexer source)
target_link_libraries(parser PUBLIC cst)

# RFC 0021 LLVM backend translation shim. Built and linked ONLY when the
# fail-closed LLVM discovery gate is enabled; the frontend build is unaffected.
if(ZOM_ENABLE_LLVM_BACKEND)
  add_subdirectory(backend)
endif()

add_library(
  frontend STATIC
  $<TARGET_OBJECTS:ast>
  $<TARGET_OBJECTS:binder>
  $<TARGET_OBJECTS:basic>
  $<TARGET_OBJECTS:checker>
  $<TARGET_OBJECTS:cst>
  $<TARGET_OBJECTS:diagnostics>
  $<TARGET_OBJECTS:driver>
  $<TARGET_OBJECTS:format>
  $<TARGET_OBJECTS:hir>
  $<TARGET_OBJECTS:identity>
  $<TARGET_OBJECTS:ide>
  $<TARGET_OBJECTS:ir>
  $<TARGET_OBJECTS:lexer>
  $<TARGET_OBJECTS:mir>
  $<TARGET_OBJECTS:lir>
  $<TARGET_OBJECTS:lsp>
  $<TARGET_OBJECTS:ownership>
  $<TARGET_OBJECTS:parser>
  $<TARGET_OBJECTS:source>
  $<TARGET_OBJECTS:trace>
  $<TARGET_OBJECTS:type>)
target_link_libraries(
  frontend
  PUBLIC zc
         query
         yyjson
         zom_vendor_archive
         zom_vendor_sodium
         zom_vendor_tomlplusplus
         zom_vendor_zstd)

set_target_include_directories(
  "${INCLUDE_DIRS}"
  ast
  basic
  binder
  checker
  cst
  diagnostics
  driver
  format
  hir
  identity
  ide
  ir
  lexer
  mir
  lir
  lsp
  ownership
  parser
  query
  source
  trace
  type)
