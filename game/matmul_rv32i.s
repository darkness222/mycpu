; RV32I-only repeated addition version
li x1, 6
li x2, 7
li x3, 5
li x4, 4

li x5, 0
li x6, 0
mul_loop_a:
beq x6, x2, mul_done_a
add x5, x5, x1
addi x6, x6, 1
jal x0, mul_loop_a

mul_done_a:
li x7, 0
li x8, 0
mul_loop_b:
beq x8, x4, mul_done_b
add x7, x7, x3
addi x8, x8, 1
jal x0, mul_loop_b

mul_done_b:
add x9, x5, x7
sw x9, 0x0120
halt
