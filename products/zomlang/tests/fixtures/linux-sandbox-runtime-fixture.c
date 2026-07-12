// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

enum {
  kRequestDescriptor = 3,
  kResponseDescriptor = 4,
  kInputDescriptor = 5,
  kOutputDescriptor = 6,
  kSeccompSetModeFilter = 1,
  kRuntimeFilterCapacity = 256,
  kScenarioSuccess = 0,
  kScenarioSeccompDenial = 1,
  kScenarioMalformedResponse = 2,
  kScenarioWallLimit = 3,
  kOpenReadOnly = 0,
  kOpenWriteOnly = 1,
  kOpenCreate = 0100,
  kOpenExclusive = 0200,
  kOpenCloseOnExec = 02000000,
  kResolveNoMagicLinks = 0x02,
  kResolveNoSymlinks = 0x04,
  kResolveBeneath = 0x08,
};

#if defined(__x86_64__)
enum { kRead = 0, kWrite = 1, kClose = 3, kExitGroup = 231, kSeccomp = 317, kOpenAt2 = 437 };

static inline long rawSyscall3(long number, long first, long second, long third) {
  register long result __asm__("rax") = number;
  register long argument1 __asm__("rdi") = first;
  register long argument2 __asm__("rsi") = second;
  register long argument3 __asm__("rdx") = third;
  __asm__ volatile("syscall"
                   : "+r"(result)
                   : "r"(argument1), "r"(argument2), "r"(argument3)
                   : "rcx", "r11", "memory");
  return result;
}
#elif defined(__aarch64__)
enum { kClose = 57, kRead = 63, kWrite = 64, kExitGroup = 94, kSeccomp = 277, kOpenAt2 = 437 };

static inline long rawSyscall3(long number, long first, long second, long third) {
  register long argument0 __asm__("x0") = first;
  register long argument1 __asm__("x1") = second;
  register long argument2 __asm__("x2") = third;
  register long syscallNumber __asm__("x8") = number;
  __asm__ volatile("svc 0"
                   : "+r"(argument0)
                   : "r"(argument1), "r"(argument2), "r"(syscallNumber)
                   : "memory");
  return argument0;
}
#else
#error The Linux sandbox fixture supports only x86-64 and AArch64.
#endif

struct BpfInstruction {
  u16 code;
  u8 jumpTrue;
  u8 jumpFalse;
  u32 operand;
};

struct BpfProgram {
  u16 length;
  struct BpfInstruction* instructions;
};

struct RuntimeFilterPatch {
  u8 marker[16];
  u32 instructionCount;
  u32 scenario;
  struct BpfInstruction instructions[kRuntimeFilterCapacity];
};

struct OpenHow {
  u64 flags;
  u64 mode;
  u64 resolve;
};

struct TargetNote {
  u32 nameSize;
  u32 descriptorSize;
  u32 type;
  u8 name[4];
  u8 descriptor[4080];
};

// The test patches the descriptor and section size with the canonical target selection.
__attribute__((section(".note.zom.target"), aligned(4),
               used)) static const struct TargetNote targetNote = {
    4, 4080, 0x5a4f4d01, {'Z', 'O', 'M', 0}, {0}};

// The integration test patches this record with generateLinuxSandboxFilter(Runtime).
__attribute__((section(".rodata.zom.runtime-filter"), aligned(4),
               used)) static volatile const struct RuntimeFilterPatch runtimeFilterPatch = {
    {'Z', 'O', 'M', 'R', 'U', 'N', 'T', 'I', 'M', 'E', 'B', 'P', 'F', '0', '0', '1'},
    0,
    kScenarioSuccess,
    {{0, 0, 0, 0}}};

static void exitImmediately(long status) {
  (void)rawSyscall3(kExitGroup, status, 0, 0);
  for (;;) {}
}

static void requireEqual(const u8* actual, const u8* expected, u64 size, long status) {
  for (u64 index = 0; index < size; ++index) {
    if (actual[index] != expected[index]) { exitImmediately(status); }
  }
}

static long openDeclared(long rootDescriptor, const char* path, u64 flags, u64 mode) {
  const struct OpenHow how = {
      flags,
      mode,
      kResolveBeneath | kResolveNoMagicLinks | kResolveNoSymlinks,
  };
  return rawSyscall3(kOpenAt2, rootDescriptor, (long)path, (long)&how);
}

static void writeAll(long descriptor, const u8* bytes, u64 size, long status) {
  u64 written = 0;
  while (written < size) {
    const long count =
        rawSyscall3(kWrite, descriptor, (long)(bytes + written), (long)(size - written));
    if (count <= 0) { exitImmediately(status); }
    written += (u64)count;
  }
}

void _start(void) {
  if (runtimeFilterPatch.instructionCount == 0 ||
      runtimeFilterPatch.instructionCount > kRuntimeFilterCapacity) {
    exitImmediately(119);
  }
  struct BpfProgram program;
  program.length = (u16)runtimeFilterPatch.instructionCount;
  program.instructions = (struct BpfInstruction*)runtimeFilterPatch.instructions;
  if (rawSyscall3(kSeccomp, kSeccompSetModeFilter, 0, (long)&program) != 0) {
    exitImmediately(120);
  }

  u8 request[256];
  for (;;) {
    const long count = rawSyscall3(kRead, kRequestDescriptor, (long)request, sizeof(request));
    if (count == 0) { break; }
    if (count < 0) { exitImmediately(121); }
  }

  if (runtimeFilterPatch.scenario == kScenarioSeccompDenial) {
    static const u8 forbiddenRuntimeWrite = 0;
    (void)rawSyscall3(kWrite, 8, (long)&forbiddenRuntimeWrite, 1);
    exitImmediately(123);
  }
  if (runtimeFilterPatch.scenario == kScenarioWallLimit) {
    for (;;) {}
  }
  if (runtimeFilterPatch.scenario == kScenarioMalformedResponse) {
    static const u8 malformed = 0xff;
    writeAll(kResponseDescriptor, &malformed, 1, 124);
    exitImmediately(0);
  }

  static const char inputPath[] = "declared-input.txt";
  static const char outputPath[] = "declared-output.txt";
  static const u8 expectedInput[] = "sandbox-input\n";
  static const u8 expectedOutput[] = "sandbox-output\n";
  u8 input[sizeof(expectedInput) - 1];
  const long inputDescriptor =
      openDeclared(kInputDescriptor, inputPath, kOpenReadOnly | kOpenCloseOnExec, 0);
  if (inputDescriptor != 0) { exitImmediately(125); }
  const long inputCount = rawSyscall3(kRead, inputDescriptor, (long)input, sizeof(input));
  if (inputCount != (long)sizeof(input)) { exitImmediately(126); }
  requireEqual(input, expectedInput, sizeof(input), 127);
  if (rawSyscall3(kClose, inputDescriptor, 0, 0) != 0) { exitImmediately(128); }

  const long outputDescriptor =
      openDeclared(kOutputDescriptor, outputPath,
                   kOpenWriteOnly | kOpenCreate | kOpenExclusive | kOpenCloseOnExec, 0600);
  if (outputDescriptor != 0) { exitImmediately(129); }
  writeAll(outputDescriptor, expectedOutput, sizeof(expectedOutput) - 1, 130);
  if (rawSyscall3(kClose, outputDescriptor, 0, 0) != 0) { exitImmediately(131); }

  static const u8 response[] = {
      0, 0, 0, 0, 0, 0, 0, 17,  // Payload length.
      0, 0, 0, 0, 0, 0, 0, 0,   // Correlation identifier.
      0,                        // Success status.
      0, 0, 0, 0, 0, 0, 0, 0,   // Empty exported environment sequence.
  };
  writeAll(kResponseDescriptor, response, sizeof(response), 122);
  exitImmediately(0);
}
