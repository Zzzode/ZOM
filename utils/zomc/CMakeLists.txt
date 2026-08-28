add_executable(zomc zomc.cc)
target_link_libraries(zomc PRIVATE zc frontend)
target_compile_definitions(zomc PRIVATE "VERSION=\"${VERSION}\"")

# RFC 0021 O5/KR5.1: when the LLVM backend is built, link the isolation-wall
# shim into zomc and expose the flag to C++ so `emitBinary()` can lower
# MIR -> LIR -> a native object. The frontend-only build links no LLVM and
# keeps the BinaryEmissionUnavailable path.
if(ZOM_ENABLE_LLVM_BACKEND)
  target_link_libraries(zomc PRIVATE backend_llvm)
  target_compile_definitions(zomc PRIVATE ZOM_ENABLE_LLVM_BACKEND=1)
endif()

set_target_include_directories("${INCLUDE_DIRS}" zomc)
set_target_properties(
  zomc
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
add_dependencies(zomc zomcore-distribution)
add_dependencies(zomc generate-core-library-inventory)
install(TARGETS zomc RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(
  FILES "${CMAKE_SOURCE_DIR}/tools/gdb/zomlang_gdb.py"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/zom/debuggers/gdb")
install(
  FILES "${CMAKE_SOURCE_DIR}/tools/lldb/zomlang_lldb.py"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/zom/debuggers/lldb")
