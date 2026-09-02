# General-purpose third-party libraries linked by the compiler proper. Unlike the
# RFC 0012 package-manager vendor tree (compiler/driver/package/vendor), these are
# reusable libraries any layer may link. Integrity is verified by
# scripts/check-third-party.py against each library's third-party-manifest.json.
add_subdirectory(yyjson)
