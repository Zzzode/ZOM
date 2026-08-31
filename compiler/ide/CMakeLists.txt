set(IDE_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/semantic-snapshot-facade.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/semantic-snapshot-freshness.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/semantic-snapshot-key.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/semantic-snapshot.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/snapshot-diagnostic.cc
)

add_library(ide STATIC ${IDE_SRC})
target_link_libraries(ide PUBLIC identity diagnostics parser query)

# The facade instantiates getCapability<ParseSourceQuery>, whose result decoder
# static_asserts on the generated QueryDescriptorInventoryBinding. Attach the
# generated inventory force-include to this target from here, so query's
# CMakeLists never names ide: the edge is ide -> query-descriptor-inventory (a
# codegen leaf), a build-order dependency with no link edge, so no cycle forms.
add_dependencies(ide query-descriptor-inventory)
target_compile_options(ide PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${CMAKE_BINARY_DIR}/compiler/query/query-descriptor-inventory.generated.h>")
