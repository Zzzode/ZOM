set(QUERY_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/query-database.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/query-types.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/semantic-context-capability-arena.cc)

set(QUERY_DESCRIPTOR_SCHEMA
  ${CMAKE_CURRENT_SOURCE_DIR}/query-descriptor-schema.def)
set(QUERY_DESCRIPTOR_GENERATOR
  ${CMAKE_SOURCE_DIR}/scripts/generate-query-descriptor-schema.py)
set(QUERY_DESCRIPTOR_GENERATED_DIR
  ${CMAKE_BINARY_DIR}/zomlang/compiler/query)
set(QUERY_DESCRIPTOR_GENERATED_HEADER
  ${QUERY_DESCRIPTOR_GENERATED_DIR}/query-descriptor-inventory.generated.h)

add_custom_command(
  OUTPUT ${QUERY_DESCRIPTOR_GENERATED_HEADER}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${QUERY_DESCRIPTOR_GENERATED_DIR}
  COMMAND
    "${Python3_EXECUTABLE}" ${QUERY_DESCRIPTOR_GENERATOR}
      --production-schema ${QUERY_DESCRIPTOR_SCHEMA}
      --production-output ${QUERY_DESCRIPTOR_GENERATED_HEADER}
  DEPENDS
    ${QUERY_DESCRIPTOR_SCHEMA}
    ${QUERY_DESCRIPTOR_GENERATOR}
  VERBATIM)

add_custom_target(query-descriptor-inventory
  DEPENDS ${QUERY_DESCRIPTOR_GENERATED_HEADER})

add_library(query STATIC ${QUERY_SRC} ${QUERY_DESCRIPTOR_GENERATED_HEADER})
add_dependencies(query query-descriptor-inventory)
target_link_libraries(query PUBLIC zc basic)
target_include_directories(query PUBLIC ${INCLUDE_DIRS})
target_compile_options(query PUBLIC
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${QUERY_DESCRIPTOR_GENERATED_HEADER}>")

function(zom_attach_production_query_descriptor_inventory)
  set(generated_header
    ${CMAKE_BINARY_DIR}/zomlang/compiler/query/query-descriptor-inventory.generated.h)
  foreach(target IN ITEMS identity parser driver binder)
    add_dependencies(${target} query-descriptor-inventory)
    target_compile_options(${target} PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${generated_header}>")
  endforeach()
endfunction()

cmake_language(
  DEFER
  DIRECTORY ${CMAKE_SOURCE_DIR}/zomlang/compiler
  CALL zom_attach_production_query_descriptor_inventory)
