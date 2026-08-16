add_executable(zomc zomc.cc)
target_link_libraries(zomc PRIVATE zc frontend)
target_compile_definitions(zomc PRIVATE "VERSION=\"${VERSION}\"")
set_target_include_directories("${INCLUDE_DIRS}" zomc)
set_target_properties(
  zomc
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
add_dependencies(zomc zomcore-distribution)
add_dependencies(zomc generate-core-library-inventory)
install(TARGETS zomc RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(
  FILES "${CMAKE_SOURCE_DIR}/products/zomlang/tools/gdb/zomlang_gdb.py"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/zom/debuggers/gdb")
install(
  FILES "${CMAKE_SOURCE_DIR}/products/zomlang/tools/lldb/zomlang_lldb.py"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/zom/debuggers/lldb")
