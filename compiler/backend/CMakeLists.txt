# RFC 0021 LLVM backend translation library.
#
# This directory is the LLVM isolation wall: it is the only place that includes
# and links `llvm/...`. It is added by compiler/CMakeLists.txt ONLY when
# ZOM_ENABLE_LLVM_BACKEND is ON, so the frontend-only build never sees LLVM.
#
# The RFC 0016 discovery gate (cmake/utils/llvm.cmake) has already run and
# validated the pinned LLVM install; it exported ZOM_LLVM_RESOLVED_COMPONENT_LIBS
# (the mapped static component libraries) and find_package(LLVM CONFIG) set
# LLVM_INCLUDE_DIRS and LLVM_DEFINITIONS.

add_subdirectory(llvm)
