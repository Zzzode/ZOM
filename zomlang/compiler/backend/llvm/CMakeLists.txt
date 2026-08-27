# RFC 0021 LLVM translation shim. The only target that links LLVM.

add_library(backend_llvm STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/llvm-translator.cc
)

# ZOM public headers (repo root, libraries/) plus the ZOM-native LIR interface.
target_include_directories(backend_llvm PRIVATE ${INCLUDE_DIRS})

# LLVM headers and preprocessor definitions from find_package(LLVM CONFIG),
# validated by the RFC 0016 gate. SYSTEM so LLVM's own warnings do not trip
# -Werror in the ZOM build.
target_include_directories(backend_llvm SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
separate_arguments(ZOM_LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(backend_llvm PRIVATE ${ZOM_LLVM_DEFINITIONS_LIST})

# The provisioned LLVM is built -fno-rtti. This shim never derives from an LLVM
# type (it only calls factory/free functions), so it must be compiled -fno-rtti
# to match the LLVM ABI while the rest of ZOM keeps RTTI.
target_compile_options(backend_llvm PRIVATE -fno-rtti)

# Link the resolved static component libraries (exported by the gate) plus zc
# for the ZOM-native result/string types, and the minimal LLVM system deps.
target_link_libraries(backend_llvm
  PUBLIC zc
  PRIVATE ${ZOM_LLVM_RESOLVED_COMPONENT_LIBS} rt dl m)
