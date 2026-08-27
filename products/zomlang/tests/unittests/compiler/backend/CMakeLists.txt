# RFC 0021 O2/KR2.4 backend translation test. Built and run ONLY when the LLVM
# backend is enabled, since it links the LLVM translation shim. The parent
# unittests CMake auto-includes this directory; guard the contents so the
# frontend-only build (backend OFF) registers no target and no test.

if(ZOM_ENABLE_LLVM_BACKEND)
  add_ztest_unit_test(
    llvm-translation-test
    ${CMAKE_CURRENT_SOURCE_DIR}/llvm-translation-test.cc
    LIBRARIES frontend backend_llvm
  )
endif()
