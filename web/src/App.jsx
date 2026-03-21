'use client';

import { useState, useEffect, useCallback, useRef } from 'react';

// ===== API 基础地址 =====
const API = 'http://localhost:8080';

// ===== 后端 API 调用 =====

async function apiAssemble(source) {
  const res = await fetch(`${API}/assemble`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ source }),
  });
  const data = await res.json();
  if (!res.ok || data.error) {
    throw new Error(data.error || `HTTP ${res.status}`);
  }
  return data;
}

async function apiGetState() {
  const res = await fetch(`${API}/get_state`);
  if (!res.ok) throw new Error(await res.text());
  return res.json();
}

async function apiStep() {
  const res = await fetch(`${API}/step`, { method: 'POST' });
  if (!res.ok) throw new Error(await res.text());
  return res.json();
}

async function apiStepInstruction() {
  const res = await fetch(`${API}/step_instruction`, { method: 'POST' });
  if (!res.ok) throw new Error(await res.text());
  return res.json();
}

async function apiReset() {
  const res = await fetch(`${API}/reset`, { method: 'POST' });
  if (!res.ok) throw new Error(await res.text());
  return res.json();
}

async function apiGetInstructions() {
  const res = await fetch(`${API}/get_instructions`);
  if (!res.ok) throw new Error(await res.text());
  return res.json();
}

// ===== 流水线阶段定义 =====
const STAGES = ['FETCH', 'DECODE', 'EXECUTE', 'MEMORY', 'WRITEBACK'];
const STAGE_LABEL_SHORT = ['IF', 'ID', 'EX', 'MEM', 'WB'];

// ===== 寄存器名称 =====
const REGISTER_NAMES = [
  'x0', 'x1', 'x2', 'x3', 'x4', 'x5', 'x6', 'x7',
  'x8', 'x9', 'x10', 'x11', 'x12', 'x13', 'x14', 'x15',
  'x16', 'x17', 'x18', 'x19', 'x20', 'x21', 'x22', 'x23',
  'x24', 'x25', 'x26', 'x27', 'x28', 'x29', 'x30', 'x31'
];

// ===== 内存段定义 =====
const MEMORY_SEGMENTS = [
  { name: 'TEXT', start: 0x00, end: 0xFF },
  { name: 'DATA', start: 0x100, end: 0xFFF },
  { name: 'STACK', start: 0x2000, end: 0x3FFF },
];

// ===== 默认示例程序 =====
const DEFAULT_PROGRAM = `; myCPU RISC-V 模拟器示例程序
; 支持的指令: lui, auipc, jal, jalr, add, sub, addi, and, or, xor, slt, slti
;            beq, bne, blt, bge, lw, sw, li, ecall, ebreak, halt

; ===== 基础运算 =====
li x1, 5          ; x1 = 5
li x2, 7          ; x2 = 7
add x3, x1, x2    ; x3 = x1 + x2 = 12
sub x4, x3, x1    ; x4 = x3 - x1 = 7
addi x5, x4, 3    ; x5 = x4 + 3 = 10

; ===== 逻辑运算 =====
and x6, x1, x2    ; x6 = x1 & x2
or x7, x1, x2     ; x7 = x1 | x2
xor x8, x1, x2    ; x8 = x1 ^ x2

; ===== 内存操作 =====
sw x3, 0x100      ; MEM[0x100] = x3
lw x9, 0x100      ; x9 = MEM[0x100] (=12，演示 load)

; ===== 分支跳转 =====
loop:
beq x10, x5, done ; if (x10 == x5) goto done
addi x10, x10, 1  ; x10++
beq x0, x0, loop  ; goto loop

done:
halt              ; 停止执行`;

// ===== 工具函数 =====

function hex(value, width = 4) {
  const normalized = Number((value ?? 0) >>> 0).toString(16).toUpperCase();
  return '0x' + normalized.padStart(width, '0');
}

function segmentNameForAddress(addr) {
  const found = MEMORY_SEGMENTS.find(seg => addr >= seg.start && addr <= seg.end);
  return found ? found.name : 'OTHER';
}

// ===== 状态转换函数 =====
function fromBackendState(s) {
  const rawPc = Number(s.pc ?? 0) >>> 0;

  // 调试：打印寄存器数据
  if (s.registers && s.registers.length > 0) {
    console.log('[调试] 后端寄存器数据:', s.registers.slice(0, 5));
  }

  // 构建寄存器对象
  const registersObj = {};
  REGISTER_NAMES.forEach((name, i) => {
    registersObj[name] = 0;
  });
  if (s.registers && Array.isArray(s.registers)) {
    s.registers.forEach((v, i) => {
      registersObj[`x${i}`] = v | 0;
    });
  }

  return {
    pc: rawPc,
    pcInstructionIndex: Number(s.pcInstructionIndex ?? Math.floor(rawPc / 4)),
    cycle: s.cycle ?? 0,
    halted: s.state === 'HALTED',
    stageIndex: s.stageIndex ?? 0,
    registers: registersObj,
    memory: Object.fromEntries(
      Object.entries(s.memory ?? {}).map(([k, v]) => [k, v])
    ),
    pipelineLatches: {
      ifid: s.pipelineLatches?.ifid ?? null,
      idex: s.pipelineLatches?.idex ?? null,
      exmem: s.pipelineLatches?.exmem ?? null,
      memwb: s.pipelineLatches?.memwb ?? null,
    },
    flowSignals: {
      stall: s.flowSignals?.stall ?? false,
      flush: s.flowSignals?.flush ?? false,
      forwarding: s.flowSignals?.forwarding ?? [],
      notes: s.flowSignals?.notes ?? [],
    },
    bubble: s.bubble ?? { active: false, stage: '', reason: '' },
    trace: s.trace ?? [],
    stats: s.stats ?? { cycles: 0, instructions: 0, stalls: 0 },
    peripherals: {
      uart: s.peripherals?.uart_buffer ?? '无',
      timer: String(s.peripherals?.timer_value ?? 0),
      trap: '无',
      irq: s.peripherals?.timer_interrupt ? 'Timer IRQ' : '无',
    },
  };
}

// ===== UI 组件 =====

function Stat({ label, value, accent = false }) {
  return (
    <div className={`border-2 border-black p-3 ${accent ? 'bg-[#74c0fc]' : 'bg-white'}`}>
      <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">{label}</div>
      <div className="mt-1 text-[0.95rem] font-bold uppercase tracking-[0.03em]">{value}</div>
    </div>
  );
}

function Panel({ title, badge, children }) {
  return (
    <section className="overflow-hidden border-[3px] border-black bg-white shadow-[8px_8px_0_#000]">
      <div className="flex items-center justify-between border-b-[3px] border-black bg-[#2d2d2d] px-4 py-3 text-white">
        <div className="text-[0.72rem] font-bold uppercase tracking-[0.05em]">{title}</div>
        {badge && (
          <div className="border border-black bg-[#ffd43b] px-3 py-1 text-[0.65rem] font-bold uppercase tracking-[0.04em] text-black">
            {badge}
          </div>
        )}
      </div>
      <div className="p-4 md:p-5">{children}</div>
    </section>
  );
}

function DataPath({ stageIndex, currentText, latches, flowSignals, bubble }) {
  const activeClass = (index) => (stageIndex === index ? 'bg-[#74c0fc]' : 'bg-white');
  const arrowClass = (hasValue) =>
    hasValue ? 'bg-[#74c0fc] text-black border-black' : 'bg-white text-black border-black';

  const datapathSteps = [
    { key: 'pc', title: 'PC / IMEM', body: currentText || '空闲', arrowAfter: 'ifid' },
    { key: 'dec', title: '译码器', body: latches.ifid || '等待', arrowAfter: 'idex' },
    { key: 'alu', title: 'ALU', body: latches.idex || '等待', arrowAfter: 'exmem' },
    { key: 'dm', title: '数据内存', body: latches.exmem || '等待', arrowAfter: 'memwb' },
    { key: 'wb', title: '写回', body: latches.memwb || '等待', arrowAfter: null },
  ];

  return (
    <div className="space-y-4">
      <div className="grid grid-cols-5 gap-2 sm:gap-3">
        {STAGES.map((stage, index) => (
          <div
            key={stage}
            title={stage}
            className={`min-w-0 border-2 border-black p-2 sm:p-3 ${activeClass(index)}`}
          >
            <div className="text-[0.58rem] uppercase tracking-[0.05em] opacity-80 sm:text-[0.62rem]">阶段</div>
            <div
              className="mt-1 text-center text-[0.72rem] font-bold uppercase leading-tight tracking-[0.04em] sm:text-[0.8rem]"
              title={stage}
            >
              <span className="inline-block max-w-full break-words">{STAGE_LABEL_SHORT[index]}</span>
            </div>
            {bubble.active && bubble.stage === stage && (
              <div className="mt-2 border border-black bg-[#ffd43b] px-1 py-0.5 text-center text-[0.52rem] font-bold uppercase tracking-[0.04em] sm:text-[0.58rem]">
                Bubble
              </div>
            )}
          </div>
        ))}
      </div>

      <div>
        <div className="overflow-x-auto overflow-y-visible pb-1 [-webkit-overflow-scrolling:touch]">
          <div className="flex w-max min-w-full items-stretch gap-2 py-0.5 text-[0.65rem] sm:gap-3 sm:text-[0.7rem]">
            {datapathSteps.map((step) => (
              <div key={step.key} className="flex shrink-0 items-stretch gap-2 sm:gap-3">
                <div className="flex w-[min(8rem,26vw)] min-w-[6.25rem] flex-col border-2 border-black bg-white p-2 sm:min-w-[7.25rem] sm:p-3">
                  <div className="shrink-0 font-bold uppercase tracking-[0.04em]">{step.title}</div>
                  <div className="mt-2 min-h-[2.25rem] break-words leading-snug opacity-80">{step.body}</div>
                </div>
                {step.arrowAfter && (
                  <div
                    className={`flex w-9 shrink-0 items-center justify-center self-center border-2 px-1 py-2 text-center text-lg font-bold sm:w-10 sm:text-xl ${arrowClass(
                      Boolean(latches[step.arrowAfter])
                    )}`}
                    title={
                      step.arrowAfter === 'ifid'
                        ? 'IF/ID 锁存'
                        : step.arrowAfter === 'idex'
                          ? 'ID/EX 锁存'
                          : step.arrowAfter === 'exmem'
                            ? 'EX/MEM 锁存'
                            : 'MEM/WB 锁存'
                    }
                  >
                    →
                  </div>
                )}
              </div>
            ))}
          </div>
        </div>
      </div>

      <div className="grid gap-3 md:grid-cols-4">
        <div className={`border-2 border-black p-3 ${flowSignals.forwarding.length ? 'bg-[#74c0fc]' : 'bg-white'}`}>
          <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Forwarding</div>
          <div className="mt-2 space-y-2 text-[0.72rem]">
            {flowSignals.forwarding.length ? flowSignals.forwarding.map((item) => <div key={item}>{item}</div>) : <div>无旁路</div>}
          </div>
        </div>
        <div className={`border-2 border-black p-3 ${flowSignals.stall ? 'bg-[#ffd43b]' : 'bg-white'}`}>
          <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Stall / Bubble</div>
          <div className="mt-2 text-[0.8rem] font-bold uppercase tracking-[0.04em]">{flowSignals.stall ? 'STALL' : 'NONE'}</div>
        </div>
        <div className={`border-2 border-black p-3 ${flowSignals.flush ? 'bg-[#ffd43b]' : 'bg-white'}`}>
          <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Flush</div>
          <div className="mt-2 text-[0.8rem] font-bold uppercase tracking-[0.04em]">{flowSignals.flush ? 'FLUSH' : 'NONE'}</div>
        </div>
        <div className={`border-2 border-black p-3 ${bubble.active ? 'bg-[#ffd43b]' : 'bg-white'}`}>
          <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Bubble Slot</div>
          <div className="mt-2 text-[0.72rem] leading-5">{bubble.active ? `${bubble.stage} · ${bubble.reason}` : '当前无气泡流动'}</div>
        </div>
      </div>
    </div>
  );
}

// ===== 主应用组件 =====

export default function App() {
  const [programText, setProgramText] = useState(DEFAULT_PROGRAM);
  const [program, setProgram] = useState([]);
  const [sim, setSim] = useState({
    pc: 0,
    pcInstructionIndex: 0,
    cycle: 0,
    halted: true,
    stageIndex: 0,
    registers: Object.fromEntries(REGISTER_NAMES.map(name => [name, 0])),
    memory: {},
    pipelineLatches: { ifid: null, idex: null, exmem: null, memwb: null },
    flowSignals: { stall: false, flush: false, forwarding: [], notes: [] },
    bubble: { active: false, stage: '', reason: '' },
    trace: [],
    stats: { cycles: 0, instructions: 0, stalls: 0 },
    peripherals: { uart: '无', timer: '0', trap: '无', irq: '无' },
  });
  const [isRunning, setIsRunning] = useState(false);
  const [assembleError, setAssembleError] = useState('');
  const [connected, setConnected] = useState(false);

  const programRef = useRef(program);
  programRef.current = program;

  // ===== 检查后端连接状态 =====
  useEffect(() => {
    const checkConnection = async () => {
      try {
        await apiGetState();
        setConnected(true);
      } catch {
        setConnected(false);
      }
    };
    checkConnection();
    const interval = setInterval(checkConnection, 5000);
    return () => clearInterval(interval);
  }, []);

  // ===== 后端连续运行 =====
  useEffect(() => {
    if (!isRunning) return undefined;
    let cancelled = false;
    let timeoutId = null;
    const delayMs = 300;

    const tick = async () => {
      if (cancelled) return;
      try {
        const state = await apiStep();
        if (cancelled) return;
        const next = fromBackendState(state);
        setSim(next);
        if (next.halted) {
          setIsRunning(false);
          return;
        }
      } catch {
        if (!cancelled) setIsRunning(false);
        return;
      }
      if (!cancelled) {
        timeoutId = window.setTimeout(tick, delayMs);
      }
    };

    timeoutId = window.setTimeout(tick, delayMs);
    return () => {
      cancelled = true;
      if (timeoutId !== null) window.clearTimeout(timeoutId);
    };
  }, [isRunning]);

  // ===== 汇编并加载程序 =====
  const assembleText = useCallback(async () => {
    setAssembleError('');
    setIsRunning(false);

    try {
      const asmResult = await apiAssemble(programText);
      const stateResult = await apiGetState();
      const instrResult = await apiGetInstructions();

      setSim(fromBackendState(stateResult));
      setProgram(instrResult.instructions || []);
    } catch (err) {
      const msg = err?.message || String(err);
      const hint =
        /fetch|network|Failed to fetch|REFUSED/i.test(msg)
          ? ' 请先启动 RPC 服务器：进入 mycpu/build 运行 myCPU.exe --server'
          : '';
      setAssembleError('后端错误: ' + msg + hint);
    }
  }, [programText]);

  // ===== 重置模拟器 =====
  const resetSim = useCallback(async () => {
    setIsRunning(false);
    try {
      await apiReset();
      const stateResult = await apiGetState();
      setSim(fromBackendState(stateResult));
    } catch {
      // ignore
    }
  }, []);

  // ===== 单步阶段 =====
  const stepStage = useCallback(async () => {
    try {
      const state = await apiStep();
      setSim(fromBackendState(state));
    } catch (err) {
      setAssembleError('错误: ' + err.message);
    }
  }, []);

  // ===== 单步指令 =====
  const stepInstruction = useCallback(async () => {
    try {
      const state = await apiStepInstruction();
      setSim(fromBackendState(state));
    } catch (err) {
      setAssembleError('错误: ' + err.message);
    }
  }, []);

  // ===== 计算当前指令索引 =====
  const currentPcIndex = sim.pcInstructionIndex;
  const currentInstruction = program[currentPcIndex];

  // ===== 内存分段统计 =====
  const segmentSummaries = MEMORY_SEGMENTS.map((segment) => ({
    ...segment,
    count: Object.keys(sim.memory).filter((addr) => {
      const numeric = Number(addr);
      return numeric >= segment.start && numeric <= segment.end;
    }).length,
  }));

  return (
    <div className="min-h-screen bg-[#f4efea] font-mono text-black">
      <div className="mx-auto max-w-7xl px-4 pb-10 pt-8 md:px-8">
        {/* ===== 头部 ===== */}
        <header className="mb-8 flex flex-col gap-4 md:flex-row md:items-end md:justify-between">
          <div>
            <div className="mb-3 inline-flex items-center gap-2 border border-black bg-[#ffd43b] px-3 py-1 text-[0.68rem] font-bold uppercase tracking-[0.05em]">
              <span className={`inline-block h-2 w-2 rounded-full ${connected ? 'bg-green-500' : 'bg-red-500'}`} />
              {connected ? 'C++ 后端已连接' : '后端未连接'}
              {' · 纯可视化模式'}
            </div>
            <h1 className="text-[2rem] font-bold uppercase leading-[1.08] tracking-[0.03em] md:text-[3.2rem]">
              myCPU 可视化执行实验台
            </h1>
            <p className="mt-3 max-w-4xl text-[0.88rem] leading-7 opacity-90 md:text-[0.95rem]">
              基于 RISC-V 指令集的 5 级流水线 CPU 模拟器，前端仅负责可视化展示，所有执行逻辑由后端 C++ 实现。
            </p>
          </div>
          <div className="flex flex-wrap gap-3">
            <button onClick={assembleText} className="border-2 border-black bg-[#74c0fc] px-5 py-3 text-[0.78rem] font-semibold uppercase tracking-[0.04em] hover:brightness-105">加载汇编</button>
            <button onClick={resetSim} className="border-2 border-black bg-white px-5 py-3 text-[0.78rem] font-semibold uppercase tracking-[0.04em] hover:brightness-95">重置</button>
            <button onClick={stepStage} className="border-2 border-black bg-[#74c0fc] px-5 py-3 text-[0.78rem] font-semibold uppercase tracking-[0.04em] hover:brightness-105">单步阶段</button>
            <button onClick={stepInstruction} className="border-2 border-black bg-white px-5 py-3 text-[0.78rem] font-semibold uppercase tracking-[0.04em] hover:brightness-95">单步指令</button>
            <button onClick={() => setIsRunning((v) => !v)} className="border-2 border-black bg-[#74c0fc] px-5 py-3 text-[0.78rem] font-semibold uppercase tracking-[0.04em] hover:brightness-105">{isRunning ? '暂停' : '运行'}</button>
          </div>
        </header>

        {/* ===== 状态统计 ===== */}
        <div className="mb-6 grid gap-4 md:grid-cols-6">
          <Stat label="PC (字节)" value={hex(sim.pc, 8)} accent />
          <Stat label="PC (指令)" value={String(sim.pcInstructionIndex)} />
          <Stat label="周期" value={String(sim.cycle)} />
          <Stat label="状态" value={sim.halted ? 'HALTED' : STAGES[sim.stageIndex]} />
          <Stat label="STALL" value={sim.flowSignals.stall ? 'ON' : 'OFF'} />
          <Stat label="FLUSH" value={sim.flowSignals.flush ? 'ON' : 'OFF'} />
        </div>

        {/* ===== 主布局 ===== */}
        <div className="mb-6 grid gap-6 xl:grid-cols-[1.02fr_1.18fr_0.95fr]">
          {/* 编辑器 */}
          <Panel title="编辑器" badge="汇编输入">
            <textarea
              value={programText}
              onChange={(e) => setProgramText(e.target.value)}
              spellCheck={false}
              className="min-h-[430px] w-full resize-none border-2 border-black bg-[#f4efea] p-4 text-[0.78rem] leading-6 outline-none"
            />
            <div className="mt-4 flex flex-wrap gap-3 text-[0.68rem] uppercase tracking-[0.04em]">
              <div className="border border-black bg-white px-3 py-2">li / add / sub / addi</div>
              <div className="border border-black bg-white px-3 py-2">lw / sw</div>
              <div className="border border-black bg-white px-3 py-2">beq / bne / blt / bge</div>
              <div className="border border-black bg-white px-3 py-2">jal / jalr / lui / ecall</div>
            </div>
            {assembleError ? <div className="mt-4 border-2 border-black bg-[#ffd43b] px-3 py-3 text-[0.74rem] font-semibold">{assembleError}</div> : null}
          </Panel>

          {/* 数据通路 */}
          <Panel title="数据通路" badge="旁路 / Stall / Flush 可视化">
            <div className="mb-4 border-2 border-black bg-[#f4efea] p-3 text-[0.72rem] leading-6">
              <div className="font-bold uppercase tracking-[0.04em]">当前指令</div>
              <div className="mt-2 text-[0.92rem] font-bold uppercase tracking-[0.03em]">{currentInstruction?.text || '无'}</div>
            </div>
            <DataPath
              stageIndex={sim.stageIndex}
              currentText={currentInstruction?.text}
              latches={sim.pipelineLatches}
              flowSignals={sim.flowSignals}
              bubble={sim.bubble}
            />
            <div className="mt-5 grid gap-3 md:grid-cols-2">
              <div className="border-2 border-black bg-white p-3 text-[0.72rem]">
                <div className="font-bold uppercase tracking-[0.04em]">控制信号</div>
                <div className="mt-2 space-y-2">
                  {sim.flowSignals.notes.length ? sim.flowSignals.notes.map((item) => <div key={item} className="border border-black px-2 py-2">{item}</div>) : <div className="border border-black px-2 py-2">当前周期无额外控制动作</div>}
                </div>
              </div>
              <div className="border-2 border-black bg-white p-3 text-[0.72rem]">
                <div className="font-bold uppercase tracking-[0.04em]">流水线寄存器</div>
                <div className="mt-2 space-y-2">
                  <div className="border border-black px-2 py-2">IF/ID: {sim.pipelineLatches.ifid || '空'}</div>
                  <div className="border border-black px-2 py-2">ID/EX: {sim.pipelineLatches.idex || '空'}</div>
                  <div className="border border-black px-2 py-2">EX/MEM: {sim.pipelineLatches.exmem || '空'}</div>
                  <div className="border border-black px-2 py-2">MEM/WB: {sim.pipelineLatches.memwb || '空'}</div>
                </div>
              </div>
            </div>
          </Panel>

          {/* 程序列表 */}
          <Panel title="程序" badge={`${program.length} 条指令`}>
            <div className="max-h-[600px] space-y-2 overflow-auto pr-1">
              {program.map((instruction, index) => {
                const isPc = index === currentPcIndex && !sim.halted;
                const isExecuted = index < currentPcIndex || (sim.halted && index <= currentPcIndex);
                return (
                  <div key={`${instruction.text}-${index}`} className={`border-2 border-black px-3 py-3 text-[0.74rem] ${isPc ? 'bg-[#74c0fc]' : isExecuted ? 'bg-white' : 'bg-[#f4efea]'}`}>
                    <div className="flex items-center justify-between gap-3">
                      <div className="flex items-center gap-3">
                        <span className="w-12 text-[0.65rem] font-bold uppercase tracking-[0.04em] opacity-80">{hex(index * 4, 4)}</span>
                        <span className="font-semibold font-mono">{instruction.text}</span>
                      </div>
                      <span className="text-[0.62rem] font-bold uppercase tracking-[0.05em]">{isPc ? STAGES[sim.stageIndex] : isExecuted ? '完成' : '等待'}</span>
                    </div>
                    {instruction.rd !== undefined && instruction.rd !== 0 && (
                      <div className="mt-2 flex gap-2 text-[0.58rem] uppercase tracking-[0.05em]">
                        <span className="border border-black px-2 py-1">写入 x{instruction.rd}</span>
                      </div>
                    )}
                  </div>
                );
              })}
            </div>
          </Panel>
        </div>

        {/* ===== 寄存器和内存 ===== */}
        <div className="mb-6 grid gap-6 xl:grid-cols-[1fr_1fr]">
          {/* 寄存器 */}
          <Panel title="寄存器" badge="GPR 视图">
            <div className="grid gap-2 md:grid-cols-2">
              {REGISTER_NAMES.map((name) => {
                const value = name === 'x0' ? 0 : sim.registers[name];
                return (
                  <div key={name} className="grid grid-cols-[54px_1fr_auto_auto] items-center gap-3 border-2 border-black px-3 py-2 text-[0.72rem] bg-white">
                    <span className="font-bold uppercase tracking-[0.05em]">{name}</span>
                    <div className="h-3 overflow-hidden border border-black bg-[#f4efea]">
                      <div className="h-full bg-[#74c0fc]" style={{ width: `${Math.min(100, Math.abs(value) * 4)}%` }} />
                    </div>
                    <span className="font-semibold">{hex(value, 4)}</span>
                    <span className="text-[0.6rem] opacity-70">{value}</span>
                  </div>
                );
              })}
            </div>
          </Panel>

          {/* 内存 */}
          <Panel title="分段内存" badge="Text / Data / Stack">
            <div className="mb-4 grid gap-2 md:grid-cols-3">
              {segmentSummaries.map((segment) => (
                <div key={segment.name} className="border-2 border-black bg-[#f4efea] px-3 py-2 text-[0.68rem] font-bold uppercase tracking-[0.04em]">
                  {segment.name} · {segment.count} cells
                </div>
              ))}
            </div>
            <div className="space-y-4">
              {MEMORY_SEGMENTS.map((segment) => {
                const entries = Object.entries(sim.memory).filter(([addr]) => {
                  const numeric = Number(addr);
                  return numeric >= segment.start && numeric <= segment.end;
                });
                return (
                  <div key={segment.name}>
                    <div className="mb-2 inline-block border-2 border-black bg-[#74c0fc] px-3 py-1 text-[0.68rem] font-bold uppercase tracking-[0.04em]">{segment.name}</div>
                    <div className="space-y-2">
                      {entries.length === 0 ? (
                        <div className="border border-black bg-[#f4efea] px-3 py-2 text-[0.68rem] opacity-60">空</div>
                      ) : (
                        entries.map(([addrText, value]) => {
                          const addr = Number(addrText);
                          return (
                            <div key={addrText} className="grid grid-cols-[90px_1fr_auto_auto] items-center gap-3 border-2 border-black bg-white px-3 py-3 text-[0.74rem]">
                              <span className="font-bold uppercase tracking-[0.04em]">{hex(addr)}</span>
                              <div className="h-3 overflow-hidden border border-black bg-[#f4efea]">
                                <div className="h-full bg-black" style={{ width: `${Math.min(100, Math.abs(Number(value)) * 4)}%` }} />
                              </div>
                              <span className="text-[0.62rem] font-bold uppercase tracking-[0.04em]">{segmentNameForAddress(addr)}</span>
                              <span className="font-semibold">{hex(Number(value), 4)}</span>
                            </div>
                          );
                        })
                      )}
                    </div>
                  </div>
                );
              })}
            </div>
          </Panel>
        </div>

        {/* ===== 轨迹、外设、统计 ===== */}
        <div className="grid gap-6 xl:grid-cols-[1fr_1fr_0.9fr]">
          {/* 执行轨迹 */}
          <Panel title="执行轨迹" badge="最近 20 条">
            <div className="space-y-2 text-[0.74rem] leading-6">
              {sim.trace.map((entry, index) => (
                <div key={`${entry}-${index}`} className={`border-2 border-black px-3 py-3 ${index === 0 ? 'bg-[#ffd43b]' : 'bg-white'}`}>{entry}</div>
              ))}
            </div>
          </Panel>

          {/* 外设 / Trap */}
          <Panel title="外设 / Trap" badge="UART · Timer · IRQ">
            <div className="space-y-3 text-[0.74rem]">
              <div className="border-2 border-black bg-white px-3 py-3">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">UART</div>
                <div className="mt-1 font-bold uppercase tracking-[0.03em]">{sim.peripherals.uart}</div>
              </div>
              <div className="border-2 border-black bg-white px-3 py-3">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Timer</div>
                <div className="mt-1 font-bold uppercase tracking-[0.03em]">{sim.peripherals.timer}</div>
              </div>
              <div className="border-2 border-black bg-white px-3 py-3">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Trap</div>
                <div className="mt-1 font-bold uppercase tracking-[0.03em]">{sim.peripherals.trap}</div>
              </div>
              <div className="border-2 border-black bg-white px-3 py-3">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">IRQ</div>
                <div className="mt-1 font-bold uppercase tracking-[0.03em]">{sim.peripherals.irq}</div>
              </div>
            </div>
          </Panel>

          {/* 统计 */}
          <Panel title="性能统计" badge="执行数据">
            <div className="space-y-3 text-[0.74rem]">
              <div className="border-2 border-black bg-white px-3 py-3">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">周期数</div>
                <div className="mt-1 font-bold uppercase tracking-[0.03em]">{sim.stats.cycles}</div>
              </div>
              <div className="border-2 border-black bg-white px-3 py-3">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">指令数</div>
                <div className="mt-1 font-bold uppercase tracking-[0.03em]">{sim.stats.instructions}</div>
              </div>
              <div className="border-2 border-black bg-white px-3 py-3">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">停顿数</div>
                <div className="mt-1 font-bold uppercase tracking-[0.03em]">{sim.stats.stalls}</div>
              </div>
            </div>
          </Panel>
        </div>
      </div>
    </div>
  );
}
