; UART demo prints Hello
li x10, 0x10000000
li x11, 72
sw x11, 0(x10)
li x11, 101
sw x11, 0(x10)
li x11, 108
sw x11, 0(x10)
sw x11, 0(x10)
li x11, 111
sw x11, 0(x10)
halt
