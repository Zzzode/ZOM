/* Copyright (c) 2026 Zode.Z. All rights reserved
 * Licensed under the Apache License, Version 2.0.
 */

.text
.globl _start
.type _start,@function
_start:
  call zom.module_init
  mov %eax, %edi
  mov $60, %eax
  syscall
  hlt
.size _start, .-_start

.section .note.GNU-stack,"",@progbits
