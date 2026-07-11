target_sources(
  driver
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sodium-runtime.cc
          ${CMAKE_CURRENT_SOURCE_DIR}/zstd-decoder.cc)
target_link_libraries(driver PUBLIC zom_vendor_sodium zom_vendor_zstd)
