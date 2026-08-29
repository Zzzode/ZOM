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
 * It does three things the test relies on:
 *   1. Verifies the invocation shape it received: argv must be
 *        argv[0] <driver> -o <out> -e <entry> [--fake-linker-mode:<mode>]
 *        <inputs...>
 *      and the environment must be empty (the Empty env policy). A structural
 *      violation exits with a distinct code so the test proves argv/env, not
 *      merely "a file was written".
 *   2. Records, for every input path argument, "path=<p> size=<n>" into a
 *      sibling "<out>.args" file, plus each of -o/-e. The test reads this back
 *      to prove the argv was rewritten to snapshot paths (paths differ from the
 *      originals) and that those snapshots held the expected bytes at exec time.
 *   3. Emulates the requested outcome: write an ELF-magic output and exit 0,
 *      write a partial output then exit 3, or exit 0 writing no output.
 *
 * The mode is carried as a plan argument token (not an environment variable,
 * because the Empty policy clears the environment).
 */

#include <stdio.h>
#include <string.h>

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
};

/* Writes the four-byte ELF magic to `path`. Returns 0 on success. */
static int writeElfMagic(const char* path) {
  FILE* out = fopen(path, "wb");
  if (out == NULL) { return -1; }
  static const unsigned char kElfMagic[4] = {0x7f, 'E', 'L', 'F'};
  size_t wrote = fwrite(kElfMagic, 1, sizeof(kElfMagic), out);
  fclose(out);
  return wrote == sizeof(kElfMagic) ? 0 : -1;
}

/* Writes partial, non-ELF bytes to `path`. Returns 0 on success. */
static int writePartial(const char* path) {
  FILE* out = fopen(path, "wb");
  if (out == NULL) { return -1; }
  static const char kPartial[] = "partial";
  size_t wrote = fwrite(kPartial, 1, sizeof(kPartial) - 1, out);
  fclose(out);
  return wrote == sizeof(kPartial) - 1 ? 0 : -1;
}

/* Appends "kind=<kind> path=<p> size=<n>\n" to the args-dump file, opening the
 * input to record the byte count the driver actually observed at exec time. A
 * size of -1 means the path could not be opened. */
static void recordArg(FILE* dump, const char* kind, const char* path) {
  long size = -1;
  FILE* in = fopen(path, "rb");
  if (in != NULL) {
    if (fseek(in, 0, SEEK_END) == 0) { size = ftell(in); }
    fclose(in);
  }
  fprintf(dump, "%s path=%s size=%ld\n", kind, path, size);
}

int main(int argc, char** argv) {
  /* The Empty env policy must deliver an empty environment. */
  if (environ != NULL && environ[0] != NULL) { return kExitEnvNotEmpty; }

  /* Minimum shape: argv[0] -o <out> -e <entry>. */
  if (argc < 5) { return kExitBadArgc; }
  if (strcmp(argv[1], "-o") != 0) { return kExitBadOutputFlag; }
  if (strcmp(argv[3], "-e") != 0) { return kExitBadEntryFlag; }

  const char* outputPath = argv[2];
  const char* entrySymbol = argv[4];

  /* Scan the remaining arguments for the mode token and collect input paths. */
  const char* mode = "success";
  static const char kModePrefix[] = "--fake-linker-mode:";
  for (int i = 5; i < argc; ++i) {
    if (strncmp(argv[i], kModePrefix, sizeof(kModePrefix) - 1) == 0) {
      mode = argv[i] + (sizeof(kModePrefix) - 1);
    }
  }

  /* Record the full invocation the driver saw, for the test to verify the argv
   * was rewritten to snapshot paths. Written as a sibling of the output. */
  char argsPath[4096];
  int n = snprintf(argsPath, sizeof(argsPath), "%s.args", outputPath);
  if (n <= 0 || (size_t)n >= sizeof(argsPath)) { return kExitArgsOpenFailed; }
  FILE* dump = fopen(argsPath, "wb");
  if (dump == NULL) { return kExitArgsOpenFailed; }
  fprintf(dump, "output path=%s size=-1\n", outputPath);
  fprintf(dump, "entry symbol=%s size=-1\n", entrySymbol);
  for (int i = 5; i < argc; ++i) {
    if (strncmp(argv[i], kModePrefix, sizeof(kModePrefix) - 1) == 0) {
      fprintf(dump, "mode value=%s size=-1\n", argv[i] + (sizeof(kModePrefix) - 1));
      continue;
    }
    recordArg(dump, "input", argv[i]);
  }
  fclose(dump);

  if (strcmp(mode, "partial") == 0) {
    if (writePartial(outputPath) != 0) { return kExitOutputOpenFailed; }
    return kExitPartial;
  }
  if (strcmp(mode, "clean-no-output") == 0) { return kExitOk; }

  /* Default success: write an ELF-magic output. */
  if (writeElfMagic(outputPath) != 0) { return kExitOutputOpenFailed; }
  return kExitOk;
}
