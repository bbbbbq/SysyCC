.arch armv8-a

.text
.globl main
.p2align 2
.type main, %function
main:
  stp x29, x30, [sp, #-16]!
  mov x29, sp
  sub sp, sp, #160
.Lmain_entry:
  sub x11, x29, #32
  mov x10, x11
  movz w9, #1, lsl #0
  str w9, [x10]
  mov x10, x11
  add x10, x10, #4
  movz w9, #2, lsl #0
  str w9, [x10]
  mov x10, x11
  add x10, x10, #8
  movz w9, #3, lsl #0
  str w9, [x10]
  mov x10, x11
  add x10, x10, #8
  add x10, x10, #4
  movz w9, #4, lsl #0
  str w9, [x10]
  mov x9, x11
  add x9, x9, #16
  str xzr, [x9, #0]
  mov x10, x11
  movz w9, #7, lsl #0
  str w9, [x10]
  mov x10, x11
  add x10, x10, #8
  movz w9, #0, lsl #0
  str w9, [x10]
  sub x9, x29, #64
  str xzr, [x9, #0]
  str xzr, [x9, #8]
  str xzr, [x9, #16]
  str xzr, [x9, #24]
  sub x11, x29, #96
  mov x10, x11
  movz w9, #7, lsl #0
  str w9, [x10]
  mov x10, x11
  add x10, x10, #8
  movz w9, #8, lsl #0
  str w9, [x10]
  sub x11, x29, #128
  mov x10, x11
  movz w9, #1, lsl #0
  str w9, [x10]
  mov x10, x11
  add x10, x10, #8
  movz w9, #2, lsl #0
  str w9, [x10]
  mov x10, x11
  add x10, x10, #8
  movz w9, #3, lsl #0
  str w9, [x10]
  mov x10, x11
  add x10, x10, #8
  add x10, x10, #4
  movz w9, #0, lsl #0
  str w9, [x10]
  mov x10, x11
  add x10, x10, #16
  movz w9, #5, lsl #0
  str w9, [x10]
  mov x10, x11
  add x10, x10, #16
  add x10, x10, #4
  movz w9, #0, lsl #0
  str w9, [x10]
  mov x10, x11
  sub x9, x29, #32
  mov x9, x9
  add x9, x9, #24
  ldr w9, [x9]
  str w9, [x10]
  mov x10, x11
  add x10, x10, #8
  movz w9, #8, lsl #0
  str w9, [x10]
  sub x12, x29, #160
  mov x9, x12
  mov x10, x9
  movz w9, #0, lsl #0
  str w9, [x10]
  mov x11, x12
  add x11, x11, #4
  sub x9, x29, #96
  mov x9, x9
  add x9, x9, #16
  add x9, x9, #4
  ldr w10, [x9]
  str w10, [x11]
  mov x9, x12
  add x9, x9, #8
  mov x11, x9
  movz w9, #4, lsl #0
  str w9, [x11]
  mov x9, x12
  add x9, x9, #16
  mov x11, x9
  movz w9, #6, lsl #0
  str w9, [x11]
  mov x9, x12
  add x9, x9, #24
  mov x11, x9
  movz w9, #8, lsl #0
  str w9, [x11]
  sub x9, x29, #160
  mov x9, x9
  add x9, x9, #24
  add x9, x9, #4
  ldr w11, [x9]
  sub x9, x29, #160
  mov x9, x9
  ldr w9, [x9]
  add w9, w11, w9
  add w10, w9, w10
  sub x9, x29, #128
  mov x9, x9
  add x9, x9, #24
  ldr w9, [x9]
  add w9, w10, w9
  mov w0, w9
  b .Lmain_epilogue
.Lmain_epilogue:
  add sp, sp, #160
  ldp x29, x30, [sp], #16
  ret
.size main, .-main
