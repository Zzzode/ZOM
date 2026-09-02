# General-purpose third-party libraries. yyjson is compiled and linked by the
# compiler; llvm and unicode are lock/reference assets (llvm-lock.json, the
# Unicode data license and readme) that are not part of the build, so only yyjson
# is added here. Integrity of compiled libraries is verified by
# scripts/check-thirdparty.py against each library's third-party-manifest.json.
add_subdirectory(yyjson)
