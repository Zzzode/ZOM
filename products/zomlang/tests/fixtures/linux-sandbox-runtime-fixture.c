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
  kSeccompSetModeFilter = 1,
  kSeccompReturnAllow = 0x7fff0000,
  kBpfReturnConstant = 0x06,
};

#if defined(__x86_64__)
enum { kRead = 0, kWrite = 1, kSeccomp = 317, kExitGroup = 231 };

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
enum { kRead = 63, kWrite = 64, kSeccomp = 277, kExitGroup = 94 };

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

static void exitImmediately(long status) {
  (void)rawSyscall3(kExitGroup, status, 0, 0);
  for (;;) {}
}

void _start(void) {
  struct BpfInstruction allow = {kBpfReturnConstant, 0, 0, kSeccompReturnAllow};
  struct BpfProgram program = {1, &allow};
  if (rawSyscall3(kSeccomp, kSeccompSetModeFilter, 0, (long)&program) != 0) {
    exitImmediately(120);
  }

  u8 request[256];
  for (;;) {
    const long count = rawSyscall3(kRead, kRequestDescriptor, (long)request, sizeof(request));
    if (count == 0) { break; }
    if (count < 0) { exitImmediately(121); }
  }

  static const u8 response[] = {
      0, 0, 0, 0, 0, 0, 0, 17,  // Payload length.
      0, 0, 0, 0, 0, 0, 0, 0,   // Correlation identifier.
      0,                        // Success status.
      0, 0, 0, 0, 0, 0, 0, 0,   // Empty exported environment sequence.
  };
  u64 written = 0;
  while (written < sizeof(response)) {
    const long count = rawSyscall3(kWrite, kResponseDescriptor, (long)(response + written),
                                   (long)(sizeof(response) - written));
    if (count <= 0) { exitImmediately(122); }
    written += (u64)count;
  }
  exitImmediately(0);
}
