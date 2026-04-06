; RV32M demo and smoke test
li x1, 3
li x2, 5
mul x3, x1, x2
sw x3, 0x0100

li x4, 10
li x5, 3
div x6, x4, x5
rem x7, x4, x5
sw x6, 0x0104
sw x7, 0x0108

li x8, -20
li x9, 6
div x10, x8, x9
rem x11, x8, x9
sw x10, 0x010c
sw x11, 0x0110

li x12, -1
li x13, 2
divu x14, x12, x13
remu x15, x12, x13
sw x14, 0x0114
sw x15, 0x0118

halt
