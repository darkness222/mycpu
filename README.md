# myCPU

一个面向答辩展示的 RISC-V 模拟 CPU 项目，包含：

- C++ 后端模拟器
- React 前端可视化界面
- HTTP / WebSocket 通信链路
- 小恐龙游戏演示

这个项目的核心原则是“真后端、真状态、真联动”：

- 主站不是前端自己解释执行指令，而是通过后端 `Simulator` 装载程序、单步执行、读取状态。
- 小恐龙游戏也不是纯前端假逻辑，而是把 `game/dino_game.s` 汇编后装入同一个模拟器，再从模拟内存读取游戏状态。

## 当前能力

已实现：

- `RV32I`
- `RV32M`
- 多周期 CPU
- 5 级流水线 CPU
- Cache 统计展示
- 分支预测展示
- MMU / Trap / CSR / UART 展示
- HTTP RPC
- WebSocket 游戏链路

未实现：

- `RV32V`
- SIMD / 向量寄存器
- 真正的向量运算 demo

补充说明：

- `矩阵乘法 RV32I` 是标量重复加法版本。
- `矩阵乘法 RV32IM` 是标量 `mul` 指令版本。
- 这两个 demo 都不是向量运算。

## 项目结构

```text
mycpu/
├─ src/
│  ├─ assembler/      # 汇编器
│  ├─ bus/            # 总线
│  ├─ cpu/            # 多周期 / 流水线 CPU、译码器、CSR、Trap
│  ├─ devices/        # UART / Timer 等设备
│  ├─ elf/            # ELF 加载
│  ├─ memory/         # 内存与分页
│  ├─ rpc/            # HTTP / WebSocket RPC 服务
│  └─ main.cpp
├─ include/
│  ├─ Constants.h
│  └─ Types.h
├─ web/
│  └─ src/
│     ├─ App.jsx
│     └─ DinoGame.jsx
├─ game/
│  └─ dino_game.s
├─ docs/
│  ├─ 技术文档.md
│  └─ 傻瓜式教程-答辩版.md
└─ start.bat
```

## 前后端真实链路

### 主站

前端 [App.jsx](/d:/Code/Project/mycpufinal/mycpu/web/src/App.jsx) 通过这些接口驱动后端：

- `POST /assemble`
- `GET /get_state`
- `GET /get_instructions`
- `POST /step`
- `POST /step_instruction`
- `POST /reset`
- `POST /set_mode`

这些接口最终进入 [RpcServer.cpp](/d:/Code/Project/mycpufinal/mycpu/src/rpc/RpcServer.cpp)，再调用：

- `simulator_->loadProgram(...)`
- `simulator_->step()`
- `simulator_->stepInstruction()`
- `simulator_->reset()`
- `simulator_->setMode(...)`

### 小恐龙游戏

游戏页 [DinoGame.jsx](/d:/Code/Project/mycpufinal/mycpu/web/src/DinoGame.jsx) 优先使用 WebSocket `/ws`，失败时自动回退 HTTP。

相关接口：

- `POST /game/init`
- `POST /game/step`
- `GET /game/get_state`
- `WS /ws`

后端会：

1. 读取 `game/dino_game.s`
2. 汇编成机器码
3. 装入 `Simulator`
4. 每帧调用 `stepInstruction()`
5. 从模拟内存 `0x2000` 一段取出分数、跳跃、障碍物等状态

所以游戏逻辑不是前端自己算的，前端只负责输入、通信和渲染。

## 示例程序怎么加载

- 点击普通示例：会自动加载并重新汇编，不需要再手点一次“重新汇编”。
- 点击“自定义程序”：不会自动覆盖你当前编辑内容，也不会自动重新汇编；修改后需要手动点“重新汇编”。

## 运行方式

### 方式 1：直接用启动脚本

在 Windows 下双击或执行：

```bat
start.bat
```

默认行为：

1. 编译 C++ 后端
2. 启动后端服务 `myCPU.exe --server`
3. 启动前端开发服务器
4. 打开浏览器

默认端口：

- 前端：`3000`
- 后端：`18080`

### 方式 2：手动启动

编译后端：

```powershell
cd mycpu
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
mingw32-make -j4
```

启动后端：

```powershell
cd build
.\myCPU.exe --server
```

启动前端：

```powershell
cd web
npm install
npm run dev
```

## 答辩时建议怎么演示

推荐顺序：

1. 先开主站，展示“后端已连接”
2. 点一个普通 demo，说明“这里会自动重新汇编并加载到后端 CPU”
3. 用“单步阶段”和“单步指令”展示程序视图、执行轨迹、寄存器、Trap / UART / MMU
4. 切到流水线模式，展示时间轴、stall、flush、分支预测
5. 再进入小恐龙，说明游戏逻辑同样跑在虚拟 CPU 上

## 已知限制

- 当前 trace 主要用于演示观察，不是完整调试器日志。
- WebSocket 主要用于游戏链路，主站仍以 HTTP 轮询和按钮操作为主。
- 文档与界面中所有能力说明均以当前仓库代码事实为准，不应把矩阵乘法 demo 说成向量运算。

## 相关文档

- [技术文档](/d:/Code/Project/mycpufinal/mycpu/docs/技术文档.md)
- [傻瓜式教程-答辩版](/d:/Code/Project/mycpufinal/mycpu/docs/傻瓜式教程-答辩版.md)
