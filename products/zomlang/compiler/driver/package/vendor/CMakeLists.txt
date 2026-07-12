set(ZOM_VENDOR_ZSTD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/zstd)

add_library(zom_vendor_tomlplusplus INTERFACE)
target_compile_definitions(zom_vendor_tomlplusplus INTERFACE TOML_EXCEPTIONS=0)
target_include_directories(
  zom_vendor_tomlplusplus
  SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/tomlplusplus/include)

set(ZOM_VENDOR_ARCHIVE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/libarchive/libarchive)
set(ZOM_VENDOR_ARCHIVE_SOURCES
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_acl.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_check_magic.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_entry.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_entry_copy_stat.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_entry_link_resolver.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_entry_sparse.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_entry_stat.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_entry_strmode.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_entry_xattr.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_options.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_pack_dev.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_pathmatch.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_random.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_read.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_read_support_filter_none.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_read_support_format_tar.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_string.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_string_sprintf.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_time.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_util.c
  ${ZOM_VENDOR_ARCHIVE_ROOT}/archive_virtual.c)

add_library(zom_vendor_archive STATIC ${ZOM_VENDOR_ARCHIVE_SOURCES})
set_target_properties(
  zom_vendor_archive
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF
             C_VISIBILITY_PRESET hidden)
target_compile_definitions(
  zom_vendor_archive
  PRIVATE PLATFORM_CONFIG_H="zomlang/compiler/driver/package/libarchive-config.h")
target_include_directories(
  zom_vendor_archive
  PUBLIC ${ZOM_VENDOR_ARCHIVE_ROOT}
  PRIVATE ${CMAKE_SOURCE_DIR}/products)

set(ZOM_VENDOR_ZSTD_SOURCES
  ${ZOM_VENDOR_ZSTD_ROOT}/lib/common/debug.c
  ${ZOM_VENDOR_ZSTD_ROOT}/lib/common/entropy_common.c
  ${ZOM_VENDOR_ZSTD_ROOT}/lib/common/error_private.c
  ${ZOM_VENDOR_ZSTD_ROOT}/lib/common/fse_decompress.c
  ${ZOM_VENDOR_ZSTD_ROOT}/lib/common/pool.c
  ${ZOM_VENDOR_ZSTD_ROOT}/lib/common/threading.c
  ${ZOM_VENDOR_ZSTD_ROOT}/lib/common/xxhash.c
  ${ZOM_VENDOR_ZSTD_ROOT}/lib/common/zstd_common.c
  ${ZOM_VENDOR_ZSTD_ROOT}/lib/decompress/huf_decompress.c
  ${ZOM_VENDOR_ZSTD_ROOT}/lib/decompress/zstd_ddict.c
  ${ZOM_VENDOR_ZSTD_ROOT}/lib/decompress/zstd_decompress.c
  ${ZOM_VENDOR_ZSTD_ROOT}/lib/decompress/zstd_decompress_block.c)

add_library(zom_vendor_zstd STATIC ${ZOM_VENDOR_ZSTD_SOURCES})
set_target_properties(
  zom_vendor_zstd
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)
target_compile_definitions(
  zom_vendor_zstd
  PRIVATE ZSTD_DISABLE_ASM=1 ZSTD_LEGACY_SUPPORT=0 ZSTD_MULTITHREAD=0)
target_include_directories(
  zom_vendor_zstd
  PUBLIC ${ZOM_VENDOR_ZSTD_ROOT}/lib
  PRIVATE ${ZOM_VENDOR_ZSTD_ROOT}/lib/common
          ${ZOM_VENDOR_ZSTD_ROOT}/lib/decompress)

set(ZOM_VENDOR_SODIUM_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/libsodium/libsodium)

# This is the portable link closure reached by sodium_init(), SHA-256, and
# Ed25519 verification. The generic signing facade, key generation, signing,
# every other public primitive, assembly, and architecture-tuned dispatch are
# intentionally absent.
set(ZOM_VENDOR_SODIUM_SOURCES
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_aead/aegis128l/aead_aegis128l.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_aead/aegis128l/aegis128l_soft.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_aead/aegis256/aead_aegis256.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_aead/aegis256/aegis256_soft.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_core/ed25519/ref10/ed25519_ref10.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_core/salsa/ref/core_salsa_ref.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_core/softaes/softaes.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_generichash/blake2b/ref/blake2b-compress-ref.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_generichash/blake2b/ref/blake2b-ref.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_generichash/blake2b/ref/generichash_blake2b.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_hash/sha256/cp/hash_sha256_cp.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_hash/sha512/cp/hash_sha512_cp.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_onetimeauth/poly1305/donna/poly1305_donna.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_onetimeauth/poly1305/onetimeauth_poly1305.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_pwhash/argon2/argon2-core.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_pwhash/argon2/argon2-fill-block-neon.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_pwhash/argon2/argon2-fill-block-ref.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_pwhash/argon2/blake2b-long.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_scalarmult/curve25519/ref10/x25519_ref10.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_scalarmult/curve25519/scalarmult_curve25519.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_sign/ed25519/ref10/open.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_sign/ed25519/ref10/sign.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_stream/chacha20/ref/chacha20_ref.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_stream/chacha20/stream_chacha20.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_stream/salsa20/ref/salsa20_ref.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_stream/salsa20/stream_salsa20.c
  ${ZOM_VENDOR_SODIUM_ROOT}/crypto_verify/verify.c
  ${ZOM_VENDOR_SODIUM_ROOT}/randombytes/randombytes.c
  ${ZOM_VENDOR_SODIUM_ROOT}/randombytes/sysrandom/randombytes_sysrandom.c
  ${ZOM_VENDOR_SODIUM_ROOT}/sodium/core.c
  ${ZOM_VENDOR_SODIUM_ROOT}/sodium/runtime.c
  ${ZOM_VENDOR_SODIUM_ROOT}/sodium/utils.c)

add_library(zom_vendor_sodium STATIC ${ZOM_VENDOR_SODIUM_SOURCES})
set_target_properties(
  zom_vendor_sodium
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF
             C_VISIBILITY_PRESET hidden)
target_compile_definitions(
  zom_vendor_sodium
  PRIVATE CONFIGURED=1 MINIMAL=1 SODIUM_LIBRARY_MINIMAL=1 SODIUM_STATIC=1)
target_include_directories(
  zom_vendor_sodium
  PUBLIC ${ZOM_VENDOR_SODIUM_ROOT}/include
  PRIVATE ${ZOM_VENDOR_SODIUM_ROOT}/include/sodium)

if(UNIX)
  target_compile_definitions(
    zom_vendor_sodium
    PRIVATE HAVE_POSIX_MEMALIGN=1 HAVE_PTHREAD=1 HAVE_SYSCONF=1)
  target_compile_options(zom_vendor_sodium PRIVATE -pthread)
  target_link_options(zom_vendor_sodium INTERFACE -pthread)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_compile_definitions(
    zom_vendor_sodium PRIVATE _GNU_SOURCE=1 _POSIX_C_SOURCE=200809L)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  # Match the upstream configured portable build: 64-bit field arithmetic is
  # selected when the compiler provides __int128, and upstream Argon2 sources
  # intentionally carry MSVC-only warning pragmas.
  target_compile_definitions(zom_vendor_sodium PRIVATE HAVE_TI_MODE=1)
  target_compile_options(
    zom_vendor_sodium
    PRIVATE -Wno-unknown-pragmas -Wno-unused-function)
endif()
