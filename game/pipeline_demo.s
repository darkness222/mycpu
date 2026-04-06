; Pipeline demo: forwarding, load-use stall, branch flush
li x2, 0x0100
li x3, 7
li x4, 5

add x5, x3, x4
add x6, x5, x3

sw x6, 0(x2)
lw x7, 0(x2)
add x8, x7, x4

beq x8, x8, taken
addi x9, x0, 111

taken:
addi x10, x8, 1
halt
