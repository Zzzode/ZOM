add_executable(zomc zomc.cc)
target_link_libraries(zomc PRIVATE zc frontend)
set_target_include_directories("${INCLUDE_DIRS}" zomc)
