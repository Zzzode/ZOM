add_executable(zomc zomc.cc)
target_link_libraries(zomc PRIVATE zc frontend)
target_compile_definitions(zomc PRIVATE "VERSION=\"${VERSION}\"")
set_target_include_directories("${INCLUDE_DIRS}" zomc)
