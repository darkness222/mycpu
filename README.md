# myCPU - 从 0 实现一个可运行程序的 RISC-V 指令集模拟器

## 项目概述

myCPU 是一个基于 RISC-V 指令集架构的 5 级流水线 CPU 模拟器，对标 Bochs 模拟器。项目采用前后端分离架构：

- **C++ 后端**：高性能 CPU 模拟器核心
- **React 前端**：交互式可视化界面（参考 Ripes 设计风格）

## 技术架构

### C++ 后端 (src/)

```
src/
├── cpu/           # CPU 核心模块
│   ├── CPU.h/cpp       # 处理器主体、流水线控制
│   ├── Decoder.h/cpp   # 指令解码器
│   └── RegisterFile.h/cpp # 寄存器文件
├── memory/        # 内存模块
│   └── Memory.h/cpp    # 物理内存模拟
├── bus/           # 总线模块
│   └── Bus.h/cpp       # 地址空间与设备连接
├── devices/       # 外设模块
│   └── Device.h/cpp    # UART、定时器等
├── assembler/     # 汇编器模块
│   └── Assembler.h/cpp # RISC-V 汇编器
├── rpc/           # 远程过程调用
│   └── RpcServer.h/cpp # RPC 服务
└── main.cpp       # 入口文件
```

### React 前端 (web/)

```
web/
├── src/
│   ├── App.jsx      # 主应用组件
│   ├── main.jsx     # 入口文件
│   └── index.css    # 样式文件
├── index.html
├── package.json
├── vite.config.js
└── tailwind.config.js
```

## 功能特性

### CPU 核心功能

- 32 个通用寄存器 (x0-x31)
- 完整的 RISC-V RV32I 指令集支持
- 5 级流水线 (IF-ID-EX-MEM-WB)
- 冒险检测与转发机制
- Load-Use 冒险处理
- 分支预测与 flush

### 支持的指令

| 类型 | 指令 |
|------|------|
| 算术运算 | add, sub, addi |
| 逻辑运算 | and, or, xor, andi, ori, xori |
| 移位运算 | sll, srl, sra, slli, srli, srai |
| 比较运算 | slt, sltu, slti, sltiu |
| 内存访问 | lw, sw, lb, lbu, lh, lhu, sb, sh |
| 分支跳转 | beq, bne, blt, bge, bltu, bgeu, jal, jalr |
| 特殊指令 | lui, auipc, ecall, ebreak, nop, halt |

### 外设支持

- UART 串口 (MMIO 地址 0x10000000)
- 定时器 (MMIO 地址 0x10000010)
- 中断控制器预留

### 前端可视化

- 流水线数据通路图
- 寄存器文件视图
- 分段内存视图
- 外设状态面板
- 执行轨迹日志
- 冒险检测提示
- Forwarding/Stall/Flush 信号

## 构建说明

### 前置要求

- CMake 3.15+
- C++17 编译器 (g++/clang++/MSVC)
- Node.js 16+ (用于前端)

### 构建 C++ 后端

```bash
cd mycpu
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 运行演示程序（无 HTTP，跑完即退出）

```bash
./myCPU
```

### 启动 RPC 服务（**前端「加载汇编」必须先启动此项**）

后端默认**不会**监听端口；需加 `--server` 才会在 **8080** 提供 `/assemble`、`/get_state`、`/step` 等接口。

```bash
# Windows（可在 mycpu 目录双击）
start_rpc_server.bat

# 或命令行
cd build
./myCPU --server
# 简写: ./myCPU -s
```

看到 `RPC Server listening on port 8080` 后再打开前端页面。

### 构建并运行前端

```bash
cd web
npm install
npm run dev
```

访问 http://localhost:3000 查看可视化界面。

## 快速开始

### 编写 RISC-V 汇编程序

```asm
; myCPU 示例程序
li x1, 5          ; x1 = 5
li x2, 7          ; x2 = 7
add x3, x1, x2    ; x3 = x1 + x2 = 12
sw x3, 0x100     ; MEM[0x100] = x3
lw x4, 0x100     ; x4 = MEM[0x100]
addi x5, x4, 3   ; x5 = x4 + 3
halt             ; 停止执行
```

### 调试技巧

1. **单步执行**：使用单步阶段按钮观察每个流水线阶段
2. **寄存器监控**：观察寄存器值变化
3. **内存视图**：查看数据段和栈段
4. **冒险检测**：注意 RAW 冒险和 Load-Use 冒险提示

## 项目路线图

- [x] 基础 CPU 核心 (RV32I)
- [x] 5 级流水线
- [x] 冒险检测与转发
- [x] 外设接口 (UART, Timer)
- [x] 汇编器
- [x] React 前端可视化
- [x] RPC 服务器
- [x] CSR 寄存器与异常处理
- [x] ELF 文件加载 (32-bit RISC-V)
- [x] Trap / Interrupt 机制
- [ ] RISC-V M 扩展 (乘法/除法)
- [ ] Cache 模拟
- [ ] 虚拟内存 / MMU
- [ ] 真实 OS 支持 (xv6 / Linux)

## 课程关联

本项目是《计算机系统结构》项目制课程改革方案的核心成果：

- 造一台"虚拟计算机"
- 以指令集架构 + CPU 微结构为核心
- 输出：可独立运行的虚拟机/myCPU 模拟器

## 参考资料

- [Ripes - RISC-V 可视化模拟器](https://github.com/mortbopet/Ripes)
- [RISC-V 官方规范](https://riscv.org/technical/specifications/)
- [《计算机系统结构》项目制课程改革方案](《计算机系统结构》项目制课程改革方案.pdf)

## 许可证

MIT License
