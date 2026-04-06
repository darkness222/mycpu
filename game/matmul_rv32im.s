; RV32IM version
li x1, 6
li x2, 7
li x3, 5
li x4, 4

mul x5, x1, x2
mul x6, x3, x4
add x7, x5, x6
sw x7, 0x0120
halt
