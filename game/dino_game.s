; 小恐龙跑酷游戏，简化版
; 内存布局：
; 0x2000: 游戏控制字（bit0=跳跃请求，bit1=游戏结束）
; 0x2004: 小恐龙 y 坐标
; 0x2008: 小恐龙垂直速度
; 0x200C: 游戏初始化标记（0=未初始化，1=已初始化）
; 0x2010: 分数
; 0x2014: 障碍物数量
; 0x2018: 障碍物生成计数器
; 0x2020-0x2080: 障碍物数据（每 8 字节一项：x 坐标、type）

init:
    li x1, 0x2000
    lw x2, 0xC(x1)
    bne x2, x0, skip_full_init

    li x2, 1
    sw x2, 0xC(x1)
    sw x0, 0(x1)
    sw x0, 4(x1)
    sw x0, 8(x1)
    sw x0, 0x10(x1)
    sw x0, 0x14(x1)
    sw x0, 0x18(x1)

skip_full_init:
    lw x2, 0(x1)
    andi x3, x2, 0xFFFFFFFE
    sw x3, 0(x1)

    andi x4, x2, 0x1
    beq x4, x0, no_jump

    lw x5, 4(x1)
    bne x5, x0, no_jump

    li x6, 17
    sw x6, 8(x1)
    jal x0, update_pos

no_jump:
    lw x6, 8(x1)
    addi x6, x6, -2
    sw x6, 8(x1)

update_pos:
    lw x7, 4(x1)
    lw x8, 8(x1)
    add x7, x7, x8
    sw x7, 4(x1)

    bge x7, x0, skip_ground

    sw x0, 4(x1)
    sw x0, 8(x1)

skip_ground:
    lw x9, 0x18(x1)
    addi x9, x9, 1
    sw x9, 0x18(x1)

    li x10, 45
    blt x9, x10, skip_spawn_obstacle

    sw x0, 0x18(x1)
    lw x11, 0x14(x1)
    li x12, 10
    bge x11, x12, skip_spawn_obstacle

    li x13, 800
    slli x14, x11, 3
    add x15, x1, x14
    addi x15, x15, 0x20
    sw x13, 0(x15)
    sw x0, 4(x15)
    addi x11, x11, 1
    sw x11, 0x14(x1)

skip_spawn_obstacle:
    lw x16, 0x14(x1)
    li x18, 0
    beq x16, x0, skip_update_obstacles

    li x17, 0
loop_obstacles:
    bge x17, x16, skip_update_obstacles
    slli x19, x17, 3
    add x19, x1, x19
    addi x19, x19, 0x20
    lw x20, 0(x19)
    lw x21, 4(x19)
    addi x20, x20, -8
    li x22, -40
    blt x20, x22, skip_keep_obstacle
    slli x23, x18, 3
    add x24, x1, x23
    addi x24, x24, 0x20
    sw x20, 0(x24)
    sw x21, 4(x24)
    addi x18, x18, 1

skip_keep_obstacle:
    addi x17, x17, 1
    jal x0, loop_obstacles

skip_update_obstacles:
    sw x18, 0x14(x1)
    lw x22, 0x14(x1)
    beq x22, x0, skip_collision_check

    li x23, 0
collision_loop:
    bge x23, x22, skip_collision_check
    slli x24, x23, 3
    add x25, x1, x24
    addi x25, x25, 0x20
    lw x26, 0(x25)

    li x27, 70
    bge x27, x26, next_collision
    li x28, 130
    bge x26, x28, next_collision

    lw x29, 4(x1)
    li x30, 35
    bge x29, x30, next_collision

    lw x31, 0(x1)
    ori x31, x31, 0x2
    sw x31, 0(x1)
    halt

next_collision:
    addi x23, x23, 1
    jal x0, collision_loop

skip_collision_check:
    lw x21, 0x10(x1)
    addi x21, x21, 1
    sw x21, 0x10(x1)

    halt
