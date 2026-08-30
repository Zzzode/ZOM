/* Copyright (c) 2026 Zode.Z. All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* A real compiled-ELF fake linker driver for the RFC 0043 D3b InvokeLinker test.
 *
 * The D3b path execs the linker driver by a snapshot file descriptor
 * (execveat AT_EMPTY_PATH), which a `#!`-script fd cannot satisfy, so the fake
 * driver must be a real ELF. This program is a normal, dynamically linked libc
 * program (execveat on such an ELF works, as /bin/true demonstrates); it needs
 * no freestanding tricks because it runs as an ordinary child, not inside a
 * sandbox.
 *
 * The behavior is selected at COMPILE TIME via -DZOM_FAKE_LINKER_MODE=<n>, so the
 * production link plan carries no mode token and there is no free-text argument
 * surface at all. The test builds three ELF variants (success / partial / no
 * output) from this one source and picks the fixture path per case.
 *
 * It does three things the test relies on:
 *   1. Verifies the invocation shape it received: argv must be
 *        argv[0] <driver> -o <out> -e <entry> <inputs...>
 *      and the environment must be empty (the Empty env policy). A structural
 *      violation exits with a distinct code so the test proves argv/env, not
 *      merely "a file was written".
 *   2. Records the full invocation into a sibling "<out>.args" file: an "argc="
 *      line, then every raw token as "arg[<i>]=<token>", then every input path
 *      argument again as "input[<i>] path=<p> size=<n>" (the byte count observed
 *      at exec time). The test rebuilds the exact expected 11-token vector and
 *      compares it index by index, and reads the input sizes to prove the
 *      snapshots held the expected bytes.
 *   3. Emulates the compile-time-selected outcome: write an ELF-magic output and
 *      exit 0, write a partial output then exit 3, or exit 0 writing no output.
 *
 * Before any structural validation, as soon as the output path (argv[2]) is
 * available it creates a sibling "<out>.started" marker. The test asserts this
 * marker's ABSENCE to prove the driver was never spawned on the input-revision-
 * mismatch paths (absence of "<out>.args" alone is insufficient, since a process
 * that started but failed a later env/flag check would also leave no ".args").
 * The marker is written before the environment/argc/flag/entry checks precisely
 * so an env or flag regression cannot manufacture a false "never spawned"
 * reading.
 */

/* symlink()/mkdir()/link() (used by the D4 output-invariant variants, modes 3,
 * 5, 6) are POSIX, not ISO C11; request the POSIX surface before any system
 * header. */
#if ZOM_FAKE_LINKER_MODE == 3 || ZOM_FAKE_LINKER_MODE == 5 || ZOM_FAKE_LINKER_MODE == 6
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include <stdio.h>
#include <string.h>
/* Modes 3/5/6 exercise the D4 output structural invariants (symlink / directory /
 * hardlink) and need POSIX calls (symlink/mkdir/link) that are not ISO C11. */
#if ZOM_FAKE_LINKER_MODE == 3 || ZOM_FAKE_LINKER_MODE == 5 || ZOM_FAKE_LINKER_MODE == 6
#include <sys/stat.h>
#include <unistd.h>
#endif

/* Compile-time behavior selector. 0 = success, 1 = partial-then-exit-3,
 * 2 = clean exit with no output, 3 = symlink at the output path, 4 = empty
 * regular file, 5 = directory at the output path, 6 = hardlinked regular file
 * (st_nlink > 1). Modes 3-6 exercise the D4 no-follow / regular / non-empty /
 * single-link output invariants. */
#ifndef ZOM_FAKE_LINKER_MODE
#error \
    "ZOM_FAKE_LINKER_MODE must be defined (0=success,1=partial,2=no-output,3=symlink,4=empty,5=dir,6=hardlink,7=invalid-image)"
#endif

#define ZOM_FAKE_LINKER_MODE_SUCCESS 0
#define ZOM_FAKE_LINKER_MODE_PARTIAL 1
#define ZOM_FAKE_LINKER_MODE_NO_OUTPUT 2
#define ZOM_FAKE_LINKER_MODE_SYMLINK 3
#define ZOM_FAKE_LINKER_MODE_EMPTY 4
#define ZOM_FAKE_LINKER_MODE_DIRECTORY 5
#define ZOM_FAKE_LINKER_MODE_HARDLINK 6
#define ZOM_FAKE_LINKER_MODE_INVALID_IMAGE 7

/* The process environment, empty under the Empty subprocess env policy. */
extern char** environ;

/* Distinct, greppable exit codes for structural violations. */
enum {
  kExitOk = 0,
  kExitPartial = 3,
  kExitBadArgc = 40,
  kExitEnvNotEmpty = 41,
  kExitBadOutputFlag = 42,
  kExitBadEntryFlag = 43,
  kExitOutputOpenFailed = 44,
  kExitArgsOpenFailed = 45,
  kExitStartedOpenFailed = 46,
};

/* Creates the empty "<path>.started" spawn-evidence marker. Returns 0 on
 * success. Written as early as possible so its mere existence proves the driver
 * process ran, independent of whether it got far enough to write ".args". */
static int writeStartedMarker(const char* outputPath) {
  char startedPath[4096];
  int n = snprintf(startedPath, sizeof(startedPath), "%s.started", outputPath);
  if (n <= 0 || (size_t)n >= sizeof(startedPath)) { return -1; }
  FILE* marker = fopen(startedPath, "wb");
  if (marker == NULL) { return -1; }
  fclose(marker);
  return 0;
}

/* Keeps the plan's `zom` entry symbol in the success fixture's ELF symbol table. */
#if ZOM_FAKE_LINKER_MODE == ZOM_FAKE_LINKER_MODE_SUCCESS
#if defined(__GNUC__) || defined(__clang__)
__attribute__((used, visibility("default")))
#endif
void zom(void) {
}

/* Copies the running descriptor-executed ELF image to `path`, producing a real
 * bounded executable image whose machine and symbol tables D5 can inspect. */
static int copySelf(const char* path) {
  FILE* in = fopen("/proc/self/exe", "rb");
  if (in == NULL) { return -1; }
  FILE* out = fopen(path, "wb");
  if (out == NULL) {
    fclose(in);
    return -1;
  }
  unsigned char buffer[16384];
  int result = 0;
  for (;;) {
    size_t count = fread(buffer, 1, sizeof(buffer), in);
    if (count > 0 && fwrite(buffer, 1, count, out) != count) {
      result = -1;
      break;
    }
    if (count < sizeof(buffer)) {
      if (ferror(in)) { result = -1; }
      break;
    }
  }
  fclose(out);
  fclose(in);
  return result;
}
#endif

/* Writes the four-byte ELF magic used by the D4 hardlink-only rejection case. */
#if ZOM_FAKE_LINKER_MODE == ZOM_FAKE_LINKER_MODE_HARDLINK
static int writeElfMagic(const char* path) {
  FILE* out = fopen(path, "wb");
  if (out == NULL) { return -1; }
  static const unsigned char kElfMagic[4] = {0x7f, 'E', 'L', 'F'};
  size_t wrote = fwrite(kElfMagic, 1, sizeof(kElfMagic), out);
  fclose(out);
  return wrote == sizeof(kElfMagic) ? 0 : -1;
}
#endif

/* Writes partial, non-ELF bytes to `path`. Returns 0 on success. Only the
 * partial variant links this. */
#if ZOM_FAKE_LINKER_MODE == ZOM_FAKE_LINKER_MODE_PARTIAL || \
    ZOM_FAKE_LINKER_MODE == ZOM_FAKE_LINKER_MODE_INVALID_IMAGE
static int writePartial(const char* path) {
  FILE* out = fopen(path, "wb");
  if (out == NULL) { return -1; }
  static const char kPartial[] = "partial";
  size_t wrote = fwrite(kPartial, 1, sizeof(kPartial) - 1, out);
  fclose(out);
  return wrote == sizeof(kPartial) - 1 ? 0 : -1;
}
#endif

/* Appends "arg[<i>]=<token>\n" for a raw argv token (the test rebuilds the full
 * expected vector and compares by index). */
static void recordToken(FILE* dump, int index, const char* token) {
  fprintf(dump, "arg[%d]=%s\n", index, token);
}

/* Appends "input[<i>] path=<p> size=<n>\n" for an input path argument, opening
 * it to record the byte count the driver observed at exec time. A size of -1
 * means the path could not be opened. */
static void recordInput(FILE* dump, int index, const char* path) {
  long size = -1;
  FILE* in = fopen(path, "rb");
  if (in != NULL) {
    if (fseek(in, 0, SEEK_END) == 0) { size = ftell(in); }
    fclose(in);
  }
  fprintf(dump, "input[%d] path=%s size=%ld\n", index, path, size);
}

int main(int argc, char** argv) {
  /* Only the minimal check needed to obtain the output path (argv[2]) runs before
   * the ".started" marker: a valid production invocation always has at least
   * argv[0] -o <out>, so once the process is entered with that shape the marker
   * is always created. Environment/flag/entry regressions are validated AFTER the
   * marker, so they can never manufacture a false "never spawned" reading. */
  if (argc < 3) { return kExitBadArgc; }
  if (strcmp(argv[1], "-o") != 0) { return kExitBadOutputFlag; }
  const char* outputPath = argv[2];

  /* Earliest spawn evidence available once the output path is known: create
   * "<out>.started". The test asserts this marker's absence to prove the driver
   * was never spawned on input-revision-mismatch paths, and its presence on the
   * success path to anchor that meaning. */
  if (writeStartedMarker(outputPath) != 0) { return kExitStartedOpenFailed; }

  /* Now the full structural validation, after the marker exists. */
  if (environ != NULL && environ[0] != NULL) { return kExitEnvNotEmpty; }
  if (argc < 5) { return kExitBadArgc; }
  if (strcmp(argv[3], "-e") != 0) { return kExitBadEntryFlag; }

  const char* entrySymbol = argv[4];

  /* Record the full invocation the driver saw, for the test to verify the exact
   * argv. Every token is recorded by index (arg[0]..arg[argc-1]); the input path
   * arguments (argv[5..]) additionally get an "input[<i>]" line carrying the byte
   * count observed at exec time. Written as a sibling of the output. */
  char argsPath[4096];
  int n = snprintf(argsPath, sizeof(argsPath), "%s.args", outputPath);
  if (n <= 0 || (size_t)n >= sizeof(argsPath)) { return kExitArgsOpenFailed; }
  FILE* dump = fopen(argsPath, "wb");
  if (dump == NULL) { return kExitArgsOpenFailed; }
  fprintf(dump, "argc=%d\n", argc);
  for (int i = 0; i < argc; ++i) { recordToken(dump, i, argv[i]); }
  for (int i = 5; i < argc; ++i) { recordInput(dump, i - 5, argv[i]); }
  fclose(dump);

  (void)entrySymbol;

#if ZOM_FAKE_LINKER_MODE == ZOM_FAKE_LINKER_MODE_PARTIAL
  if (writePartial(outputPath) != 0) { return kExitOutputOpenFailed; }
  return kExitPartial;
#elif ZOM_FAKE_LINKER_MODE == ZOM_FAKE_LINKER_MODE_NO_OUTPUT
  return kExitOk;
#elif ZOM_FAKE_LINKER_MODE == ZOM_FAKE_LINKER_MODE_INVALID_IMAGE
  if (writePartial(outputPath) != 0) { return kExitOutputOpenFailed; }
  return kExitOk;
#elif ZOM_FAKE_LINKER_MODE == ZOM_FAKE_LINKER_MODE_SYMLINK
  /* Exit zero but make the output path a SYMLINK to a sibling regular file, so
   * the D4 no-follow output capture must refuse it (a symlink is not a regular
   * transaction-owned output). The link target is written first so a
   * symlink-following open would spuriously succeed; the no-follow open must
   * still reject. */
  {
    char targetPath[4096];
    int t = snprintf(targetPath, sizeof(targetPath), "%s.real", outputPath);
    if (t <= 0 || (size_t)t >= sizeof(targetPath)) { return kExitOutputOpenFailed; }
    FILE* target = fopen(targetPath, "wb");
    if (target == NULL) { return kExitOutputOpenFailed; }
    static const unsigned char kElfMagic[4] = {0x7f, 'E', 'L', 'F'};
    (void)fwrite(kElfMagic, 1, sizeof(kElfMagic), target);
    fclose(target);
    if (symlink(targetPath, outputPath) != 0) { return kExitOutputOpenFailed; }
    return kExitOk;
  }
#elif ZOM_FAKE_LINKER_MODE == ZOM_FAKE_LINKER_MODE_EMPTY
  /* Exit zero but create an EMPTY regular file at the output path, so the D4
   * non-empty invariant must refuse it. (Distinct from mode 2, which writes no
   * output at all - here the entry exists but has zero bytes.) */
  {
    FILE* out = fopen(outputPath, "wb");
    if (out == NULL) { return kExitOutputOpenFailed; }
    fclose(out);
    return kExitOk;
  }
#elif ZOM_FAKE_LINKER_MODE == ZOM_FAKE_LINKER_MODE_DIRECTORY
  /* Exit zero but create a DIRECTORY at the output path, so the D4 regular-file
   * invariant must refuse it. */
  if (mkdir(outputPath, 0700) != 0) { return kExitOutputOpenFailed; }
  return kExitOk;
#elif ZOM_FAKE_LINKER_MODE == ZOM_FAKE_LINKER_MODE_HARDLINK
  /* Exit zero, write a regular ELF output, then create a second hard link to it
   * from a sibling path, so the output inode has st_nlink == 2. The D4 sole-link
   * invariant (st_nlink == 1) must refuse it: a multiply-linked inode could be
   * rewritten in place through the external link. */
  {
    if (writeElfMagic(outputPath) != 0) { return kExitOutputOpenFailed; }
    char linkPath[4096];
    int t = snprintf(linkPath, sizeof(linkPath), "%s.hardlink", outputPath);
    if (t <= 0 || (size_t)t >= sizeof(linkPath)) { return kExitOutputOpenFailed; }
    if (link(outputPath, linkPath) != 0) { return kExitOutputOpenFailed; }
    return kExitOk;
  }
#else
  /* Default success: copy this real ELF image, including its `zom` symbol. */
  if (copySelf(outputPath) != 0) { return kExitOutputOpenFailed; }
  return kExitOk;
#endif
}
