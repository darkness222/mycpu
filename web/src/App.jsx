'use client';

import { useCallback, useEffect, useMemo, useState } from 'react';

const API = import.meta.env.VITE_API_BASE_URL || 'http://localhost:18080';

const STAGES = ['FETCH', 'DECODE', 'EXECUTE', 'MEMORY', 'WRITEBACK'];
const STAGE_LABEL_SHORT = ['IF', 'ID', 'EX', 'MEM', 'WB'];

const REGISTER_NAMES = [
  'x0', 'x1', 'x2', 'x3', 'x4', 'x5', 'x6', 'x7',
  'x8', 'x9', 'x10', 'x11', 'x12', 'x13', 'x14', 'x15',
  'x16', 'x17', 'x18', 'x19', 'x20', 'x21', 'x22', 'x23',
  'x24', 'x25', 'x26', 'x27', 'x28', 'x29', 'x30', 'x31',
];

const MEMORY_SEGMENTS = [
  { name: 'TEXT', start: 0x0000, end: 0x00ff },
  { name: 'DATA', start: 0x0100, end: 0x0fff },
  { name: 'STACK', start: 0x2000, end: 0x3fff },
];

const DEFAULT_PROGRAM = `; myCPU RISC-V 示例程序
; 支持 lui, auipc, jal, jalr, add, sub, addi, and, or, xor, slt, slti
;      beq, bne, blt, bge, lw, sw, li, ecall, ebreak, halt

; ===== 基础运算 =====
li x1, 5
li x2, 7
add x3, x1, x2
sub x4, x3, x1
addi x5, x4, 3

; ===== 逻辑运算 =====
and x6, x1, x2
or x7, x1, x2
xor x8, x1, x2

; ===== 数据段访问 =====
sw x3, 0x100
lw x9, 0x100

; ===== 分支跳转 =====
loop:
beq x10, x5, done
addi x10, x10, 1
beq x0, x0, loop

done:
halt`;

async function fetchJson(path, options) {
  const res = await fetch(`${API}${path}`, options);
  const data = await res.json();
  if (!res.ok || data.error) {
    throw new Error(data.error || `HTTP ${res.status}`);
  }
  return data;
}

async function apiAssemble(source) {
  return fetchJson('/assemble', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ source }),
  });
}

async function apiGetState() {
  return fetchJson('/get_state');
}

async function apiStep() {
  return fetchJson('/step', { method: 'POST' });
}

async function apiStepInstruction() {
  return fetchJson('/step_instruction', { method: 'POST' });
}

async function apiReset() {
  return fetchJson('/reset', { method: 'POST' });
}

async function apiGetInstructions() {
  return fetchJson('/get_instructions');
}

function hex(value, width = 4) {
  const normalized = Number(value ?? 0) >>> 0;
  return `0x${normalized.toString(16).toUpperCase().padStart(width, '0')}`;
}

function formatSigned(value) {
  return String((Number(value ?? 0) << 0) >> 0);
}

function alignWord(addr) {
  return Number(addr ?? 0) & ~0x3;
}

function segmentNameForAddress(addr) {
  const found = MEMORY_SEGMENTS.find((seg) => addr >= seg.start && addr <= seg.end);
  return found ? found.name : 'OTHER';
}

function getObservedAddresses(segmentName, registers) {
  if (segmentName === 'DATA') {
    return [0x0100, 0x0104, 0x0108, 0x010c];
  }

  if (segmentName === 'STACK') {
    const sp = alignWord(registers?.x2 ?? 0);
    return [sp, sp + 4, sp + 8, sp + 12].filter((addr) => addr >= 0x2000 && addr <= 0x3fff);
  }

  return [];
}

function getSegmentEntries(segment, memory, registers) {
  const entries = new Map();

  Object.entries(memory ?? {}).forEach(([addrText, value]) => {
    const addr = Number(addrText);
    if (addr >= segment.start && addr <= segment.end) {
      entries.set(addr, { addr, value: Number(value), touched: true });
    }
  });

  getObservedAddresses(segment.name, registers).forEach((addr) => {
    if (!entries.has(addr)) {
      entries.set(addr, { addr, value: 0, touched: false });
    }
  });

  return Array.from(entries.values()).sort((a, b) => a.addr - b.addr);
}

function stageStatus(sim) {
  if (sim.halted) return 'HALTED';
  return STAGES[sim.stageIndex] || 'RUNNING';
}

function fromBackendState(state) {
  const registers = Object.fromEntries(REGISTER_NAMES.map((name) => [name, 0]));
  if (Array.isArray(state.registers)) {
    state.registers.forEach((value, index) => {
      registers[`x${index}`] = value | 0;
    });
  }

  return {
    pc: Number(state.pc ?? 0) >>> 0,
    pcInstructionIndex: Number(state.pcInstructionIndex ?? 0),
    cycle: Number(state.cycle ?? 0),
    halted: state.state === 'HALTED',
    stageIndex: Number(state.stageIndex ?? 0),
    registers,
    memory: Object.fromEntries(Object.entries(state.memory ?? {}).map(([k, v]) => [k, Number(v)])),
    pipelineLatches: {
      ifid: state.pipelineLatches?.ifid ?? null,
      idex: state.pipelineLatches?.idex ?? null,
      exmem: state.pipelineLatches?.exmem ?? null,
      memwb: state.pipelineLatches?.memwb ?? null,
    },
    flowSignals: {
      stall: Boolean(state.flowSignals?.stall),
      flush: Boolean(state.flowSignals?.flush),
      forwarding: state.flowSignals?.forwarding ?? [],
      notes: state.flowSignals?.notes ?? [],
    },
    bubble: state.bubble ?? { active: false, stage: '', reason: '' },
    trace: state.trace ?? [],
    stats: {
      cycles: Number(state.stats?.cycles ?? 0),
      instructions: Number(state.stats?.instructions ?? 0),
      stalls: Number(state.stats?.stalls ?? 0),
    },
    peripherals: {
      uart: state.peripherals?.uart_buffer || '无',
      timer: String(state.peripherals?.timer_value ?? 0),
      trap: state.state === 'EXCEPTION' ? '触发异常' : '无',
      irq: state.peripherals?.timer_interrupt ? 'Timer IRQ' : '无',
    },
  };
}

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
        <div className="text-[0.72rem] font-bold tracking-[0.05em]">{title}</div>
        {badge ? (
          <div className="border border-black bg-[#ffd43b] px-3 py-1 text-[0.65rem] font-bold tracking-[0.04em] text-black">
            {badge}
          </div>
        ) : null}
      </div>
      <div className="p-4 md:p-5">{children}</div>
    </section>
  );
}

function DataPath({ stageIndex, currentText, latches, flowSignals, bubble }) {
  const activeClass = (index) => (stageIndex === index ? 'bg-[#74c0fc]' : 'bg-white');
  const arrowClass = (hasValue) => (hasValue ? 'bg-[#74c0fc]' : 'bg-white');

  const datapathSteps = [
    { key: 'pc', title: 'PC / 指令存储器', body: currentText || '空闲', arrowAfter: 'ifid' },
    { key: 'dec', title: '译码器', body: latches.ifid || '等待', arrowAfter: 'idex' },
    { key: 'alu', title: '执行单元 / ALU', body: latches.idex || '等待', arrowAfter: 'exmem' },
    { key: 'dm', title: '数据存储器', body: latches.exmem || '等待', arrowAfter: 'memwb' },
    { key: 'wb', title: '写回阶段', body: latches.memwb || '等待', arrowAfter: null },
  ];

  return (
    <div className="space-y-4">
      <div className="grid grid-cols-5 gap-2 sm:gap-3">
        {STAGES.map((stage, index) => (
          <div key={stage} title={stage} className={`min-w-0 border-2 border-black p-2 sm:p-3 ${activeClass(index)}`}>
            <div className="text-[0.58rem] uppercase tracking-[0.05em] opacity-80 sm:text-[0.62rem]">阶段</div>
            <div className="mt-1 text-center text-[0.72rem] font-bold uppercase leading-tight tracking-[0.04em] sm:text-[0.8rem]">
              {STAGE_LABEL_SHORT[index]}
            </div>
            {bubble.active && bubble.stage === stage ? (
              <div className="mt-2 border border-black bg-[#ffd43b] px-1 py-0.5 text-center text-[0.52rem] font-bold uppercase tracking-[0.04em] sm:text-[0.58rem]">
                Bubble
              </div>
            ) : null}
          </div>
        ))}
      </div>

      <div className="overflow-x-auto overflow-y-visible pb-1 [-webkit-overflow-scrolling:touch]">
        <div className="flex w-max min-w-full items-stretch gap-2 py-0.5 text-[0.65rem] sm:gap-3 sm:text-[0.7rem]">
          {datapathSteps.map((step) => (
            <div key={step.key} className="flex shrink-0 items-stretch gap-2 sm:gap-3">
              <div className="flex w-[min(8rem,26vw)] min-w-[6.25rem] flex-col border-2 border-black bg-white p-2 sm:min-w-[7.25rem] sm:p-3">
                <div className="font-bold tracking-[0.04em]">{step.title}</div>
                <div className="mt-2 min-h-[2.25rem] break-words leading-snug opacity-80">{step.body}</div>
              </div>
              {step.arrowAfter ? (
                <div
                  className={`flex w-9 shrink-0 items-center justify-center self-center border-2 border-black px-1 py-2 text-center text-lg font-bold sm:w-10 sm:text-xl ${arrowClass(Boolean(latches[step.arrowAfter]))}`}
                  title={step.arrowAfter.toUpperCase()}
                >
                  {'->'}
                </div>
              ) : null}
            </div>
          ))}
        </div>
      </div>

      <div className="grid gap-3 md:grid-cols-4">
        <div className={`border-2 border-black p-3 ${flowSignals.forwarding.length ? 'bg-[#74c0fc]' : 'bg-white'}`}>
          <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Forwarding</div>
          <div className="mt-2 space-y-2 text-[0.72rem]">
            {flowSignals.forwarding.length
              ? flowSignals.forwarding.map((item) => <div key={item}>{item}</div>)
              : <div>当前无旁路</div>}
          </div>
        </div>
        <div className={`border-2 border-black p-3 ${flowSignals.stall ? 'bg-[#ffd43b]' : 'bg-white'}`}>
          <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Stall</div>
          <div className="mt-2 text-[0.8rem] font-bold uppercase tracking-[0.04em]">{flowSignals.stall ? 'ON' : 'OFF'}</div>
        </div>
        <div className={`border-2 border-black p-3 ${flowSignals.flush ? 'bg-[#ffd43b]' : 'bg-white'}`}>
          <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Flush</div>
          <div className="mt-2 text-[0.8rem] font-bold uppercase tracking-[0.04em]">{flowSignals.flush ? 'ON' : 'OFF'}</div>
        </div>
        <div className={`border-2 border-black p-3 ${bubble.active ? 'bg-[#ffd43b]' : 'bg-white'}`}>
          <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Bubble</div>
          <div className="mt-2 text-[0.72rem] leading-5">
            {bubble.active ? `${bubble.stage} / ${bubble.reason}` : '当前没有气泡'}
          </div>
        </div>
      </div>
    </div>
  );
}

export default function App() {
  const [programText, setProgramText] = useState(DEFAULT_PROGRAM);
  const [program, setProgram] = useState([]);
  const [sim, setSim] = useState({
    pc: 0,
    pcInstructionIndex: 0,
    cycle: 0,
    halted: true,
    stageIndex: 0,
    registers: Object.fromEntries(REGISTER_NAMES.map((name) => [name, 0])),
    memory: {},
    pipelineLatches: { ifid: null, idex: null, exmem: null, memwb: null },
    flowSignals: { stall: false, flush: false, forwarding: [], notes: [] },
    bubble: { active: false, stage: '', reason: '' },
    trace: [],
    stats: { cycles: 0, instructions: 0, stalls: 0 },
    peripherals: { uart: '无', timer: '0', trap: '无', irq: '无' },
  });
  const [connected, setConnected] = useState(false);
  const [isRunning, setIsRunning] = useState(false);
  const [assembleError, setAssembleError] = useState('');

  useEffect(() => {
    const checkConnection = async () => {
      try {
        const state = await apiGetState();
        setSim(fromBackendState(state));
        setConnected(true);
      } catch {
        setConnected(false);
      }
    };

    checkConnection();
    const interval = window.setInterval(checkConnection, 5000);
    return () => window.clearInterval(interval);
  }, []);

  useEffect(() => {
    if (!isRunning) return undefined;

    let cancelled = false;
    let timeoutId = null;

    const tick = async () => {
      if (cancelled) return;
      try {
        const next = fromBackendState(await apiStep());
        if (cancelled) return;
        setSim(next);
        if (next.halted) {
          setIsRunning(false);
          return;
        }
      } catch (err) {
        if (!cancelled) {
          setIsRunning(false);
          setAssembleError(`运行失败: ${err.message}`);
        }
        return;
      }

      if (!cancelled) {
        timeoutId = window.setTimeout(tick, 300);
      }
    };

    timeoutId = window.setTimeout(tick, 300);
    return () => {
      cancelled = true;
      if (timeoutId !== null) window.clearTimeout(timeoutId);
    };
  }, [isRunning]);

  const assembleText = useCallback(async () => {
    setAssembleError('');
    setIsRunning(false);

    try {
      await apiAssemble(programText);
      const [stateResult, instrResult] = await Promise.all([apiGetState(), apiGetInstructions()]);
      setSim(fromBackendState(stateResult));
      setProgram(instrResult.instructions || []);
    } catch (err) {
      const msg = err?.message || String(err);
      const hint = /fetch|network|Failed to fetch|REFUSED/i.test(msg)
        ? '。请先启动后端服务：myCPU.exe --server'
        : '';
      setAssembleError(`后端错误: ${msg}${hint}`);
    }
  }, [programText]);

  const resetSim = useCallback(async () => {
    setIsRunning(false);
    try {
      const state = await apiReset();
      setSim(fromBackendState(state));
      setAssembleError('');
    } catch (err) {
      setAssembleError(`重置失败: ${err.message}`);
    }
  }, []);

  const stepStage = useCallback(async () => {
    try {
      const state = await apiStep();
      setSim(fromBackendState(state));
    } catch (err) {
      setAssembleError(`单步阶段失败: ${err.message}`);
    }
  }, []);

  const stepInstruction = useCallback(async () => {
    try {
      const state = await apiStepInstruction();
      setSim(fromBackendState(state));
    } catch (err) {
      setAssembleError(`单步指令失败: ${err.message}`);
    }
  }, []);

  const currentPcIndex = sim.pcInstructionIndex;
  const currentInstruction = program[currentPcIndex];

  const segmentSummaries = useMemo(
    () => MEMORY_SEGMENTS.map((segment) => ({
      ...segment,
      count: getSegmentEntries(segment, sim.memory, sim.registers).length,
    })),
    [sim.memory, sim.registers],
  );

  return (
    <div className="min-h-screen bg-[#f4efea] font-mono text-black">
      <div className="mx-auto max-w-7xl px-4 pb-10 pt-8 md:px-8">
        <header className="mb-8 flex flex-col gap-4 md:flex-row md:items-end md:justify-between">
          <div>
            <div className="mb-3 inline-flex items-center gap-2 border border-black bg-[#ffd43b] px-3 py-1 text-[0.68rem] font-bold tracking-[0.05em]">
              <span className={`inline-block h-2 w-2 rounded-full ${connected ? 'bg-green-500' : 'bg-red-500'}`} />
              {connected ? '后端已连接' : '后端未连接'} / 纯可视化展示
            </div>
            <h1 className="text-[2rem] font-bold leading-[1.08] tracking-[0.03em] md:text-[3rem]">
              myCPU 可视化执行实验台
            </h1>
            <p className="mt-3 max-w-4xl text-[0.9rem] leading-7 opacity-90">
              前端只负责展示，CPU 执行、寄存器更新、内存读写、流水线状态都由后端 C++ 模拟器提供。
            </p>
          </div>
          <div className="flex flex-wrap gap-3">
            <button onClick={assembleText} className="border-2 border-black bg-[#74c0fc] px-5 py-3 text-[0.78rem] font-semibold tracking-[0.04em] hover:brightness-105">加载汇编</button>
            <button onClick={resetSim} className="border-2 border-black bg-white px-5 py-3 text-[0.78rem] font-semibold tracking-[0.04em] hover:brightness-95">重置</button>
            <button onClick={stepStage} className="border-2 border-black bg-[#74c0fc] px-5 py-3 text-[0.78rem] font-semibold tracking-[0.04em] hover:brightness-105">单步阶段</button>
            <button onClick={stepInstruction} className="border-2 border-black bg-white px-5 py-3 text-[0.78rem] font-semibold tracking-[0.04em] hover:brightness-95">单步指令</button>
            <button onClick={() => setIsRunning((value) => !value)} className="border-2 border-black bg-[#74c0fc] px-5 py-3 text-[0.78rem] font-semibold tracking-[0.04em] hover:brightness-105">
              {isRunning ? '暂停' : '运行'}
            </button>
          </div>
        </header>

        <div className="mb-6 grid gap-4 md:grid-cols-6">
          <Stat label="PC（字节）" value={hex(sim.pc, 8)} accent />
          <Stat label="PC（指令序号）" value={String(sim.pcInstructionIndex)} />
          <Stat label="周期" value={String(sim.cycle)} />
          <Stat label="状态" value={stageStatus(sim)} />
          <Stat label="STALL" value={sim.flowSignals.stall ? 'ON' : 'OFF'} />
          <Stat label="FLUSH" value={sim.flowSignals.flush ? 'ON' : 'OFF'} />
        </div>

        <div className="mb-6 grid gap-6 xl:grid-cols-[1.02fr_1.18fr_0.95fr]">
          <Panel title="汇编编辑器" badge="输入程序">
            <textarea
              value={programText}
              onChange={(e) => setProgramText(e.target.value)}
              spellCheck={false}
              className="min-h-[430px] w-full resize-none border-2 border-black bg-[#f4efea] p-4 text-[0.78rem] leading-6 outline-none"
            />
            <div className="mt-4 flex flex-wrap gap-3 text-[0.68rem] tracking-[0.04em]">
              <div className="border border-black bg-white px-3 py-2">li / add / sub / addi</div>
              <div className="border border-black bg-white px-3 py-2">lw / sw</div>
              <div className="border border-black bg-white px-3 py-2">beq / bne / blt / bge</div>
              <div className="border border-black bg-white px-3 py-2">jal / jalr / lui / ecall</div>
            </div>
            {assembleError ? (
              <div className="mt-4 border-2 border-black bg-[#ffd43b] px-3 py-3 text-[0.74rem] font-semibold">
                {assembleError}
              </div>
            ) : null}
          </Panel>

          <Panel title="数据通路" badge="旁路 / Stall / Flush">
            <div className="mb-4 border-2 border-black bg-[#f4efea] p-3 text-[0.72rem] leading-6">
              <div className="font-bold tracking-[0.04em]">当前指令</div>
              <div className="mt-2 text-[0.92rem] font-bold tracking-[0.03em]">{currentInstruction?.text || '暂无'}</div>
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
                <div className="font-bold tracking-[0.04em]">控制信号</div>
                <div className="mt-2 space-y-2">
                  {sim.flowSignals.notes.length
                    ? sim.flowSignals.notes.map((item) => <div key={item} className="border border-black px-2 py-2">{item}</div>)
                    : <div className="border border-black px-2 py-2">当前周期没有额外控制提示</div>}
                </div>
              </div>
              <div className="border-2 border-black bg-white p-3 text-[0.72rem]">
                <div className="font-bold tracking-[0.04em]">流水线锁存器</div>
                <div className="mt-2 space-y-2">
                  <div className="border border-black px-2 py-2">IF/ID: {sim.pipelineLatches.ifid || '空'}</div>
                  <div className="border border-black px-2 py-2">ID/EX: {sim.pipelineLatches.idex || '空'}</div>
                  <div className="border border-black px-2 py-2">EX/MEM: {sim.pipelineLatches.exmem || '空'}</div>
                  <div className="border border-black px-2 py-2">MEM/WB: {sim.pipelineLatches.memwb || '空'}</div>
                </div>
              </div>
            </div>
          </Panel>

          <Panel title="程序视图" badge={`${program.length} 条指令`}>
            <div className="max-h-[600px] space-y-2 overflow-auto pr-1">
              {program.map((instruction, index) => {
                const isPc = index === currentPcIndex && !sim.halted;
                const isExecuted = index < currentPcIndex || (sim.halted && index <= currentPcIndex);
                return (
                  <div key={`${instruction.text}-${index}`} className={`border-2 border-black px-3 py-3 text-[0.74rem] ${isPc ? 'bg-[#74c0fc]' : isExecuted ? 'bg-white' : 'bg-[#f4efea]'}`}>
                    <div className="flex items-center justify-between gap-3">
                      <div className="flex items-center gap-3">
                        <span className="w-12 text-[0.65rem] font-bold tracking-[0.04em] opacity-80">{hex(index * 4, 4)}</span>
                        <span className="font-semibold">{instruction.text}</span>
                      </div>
                      <span className="text-[0.62rem] font-bold tracking-[0.05em]">
                        {isPc ? STAGES[sim.stageIndex] : isExecuted ? '已执行' : '等待'}
                      </span>
                    </div>
                  </div>
                );
              })}
            </div>
          </Panel>
        </div>

        <div className="mb-6 grid gap-6 xl:grid-cols-[1fr_1fr]">
          <Panel title="寄存器" badge="GPR 视图">
            <div className="mb-3 border-2 border-black bg-[#f4efea] px-3 py-2 text-[0.72rem] leading-6">
              复位后只有 `x2(sp)` 会被初始化为栈顶地址，其它通用寄存器默认都是 0。
            </div>
            <div className="grid gap-2 md:grid-cols-2">
              {REGISTER_NAMES.map((name) => {
                const value = name === 'x0' ? 0 : sim.registers[name];
                const width = value === 0 ? 0 : Math.max(6, Math.min(100, Math.abs(value) * 4));
                return (
                  <div key={name} className="grid grid-cols-[44px_minmax(90px,1fr)_112px_44px] items-center gap-2 border-2 border-black bg-white px-2 py-2 text-[0.72rem]">
                    <span className="font-bold tracking-[0.05em]">{name}</span>
                    <div className="h-3 overflow-hidden border border-black bg-[#f4efea] min-w-0">
                      <div className="h-full bg-[#74c0fc]" style={{ width: `${width}%` }} />
                    </div>
                    <span className="overflow-hidden text-ellipsis whitespace-nowrap text-[0.68rem] font-semibold">{hex(value, 8)}</span>
                    <span className="overflow-hidden text-right text-[0.58rem] opacity-70">{formatSigned(value)}</span>
                  </div>
                );
              })}
            </div>
          </Panel>

          <Panel title="分段内存" badge="Text / Data / Stack">
            <div className="mb-3 border-2 border-black bg-[#f4efea] px-3 py-2 text-[0.72rem] leading-6">
              `written` 表示后端快照里已经有实际内容，`watch` 表示前端主动观察的关键地址，便于看出 `sw` 和栈访问何时生效。
            </div>
            <div className="mb-4 grid gap-2 md:grid-cols-3">
              {segmentSummaries.map((segment) => (
                <div key={segment.name} className="border-2 border-black bg-[#f4efea] px-3 py-2 text-[0.68rem] font-bold tracking-[0.04em]">
                  {segment.name} · {segment.count} cells
                </div>
              ))}
            </div>
            <div className="space-y-4">
              {MEMORY_SEGMENTS.map((segment) => {
                const entries = getSegmentEntries(segment, sim.memory, sim.registers);
                return (
                  <div key={segment.name}>
                    <div className="mb-2 inline-block border-2 border-black bg-[#74c0fc] px-3 py-1 text-[0.68rem] font-bold tracking-[0.04em]">
                      {segment.name}
                    </div>
                    <div className="space-y-2">
                      {entries.length === 0 ? (
                        <div className="border border-black bg-[#f4efea] px-3 py-2 text-[0.68rem] opacity-60">空</div>
                      ) : (
                        entries.map(({ addr, value, touched }) => (
                          <div key={addr} className={`grid grid-cols-[90px_1fr_auto_auto_auto] items-center gap-3 border-2 border-black px-3 py-3 text-[0.74rem] ${touched ? 'bg-white' : 'bg-[#f4efea]'}`}>
                            <span className="font-bold tracking-[0.04em]">{hex(addr, 4)}</span>
                            <div className="h-3 overflow-hidden border border-black bg-[#f4efea]">
                              <div className={`h-full ${touched ? 'bg-black' : 'bg-[#74c0fc]'}`} style={{ width: `${Math.min(100, Math.abs(Number(value)) * 4)}%` }} />
                            </div>
                            <span className="text-[0.62rem] font-bold tracking-[0.04em]">{segmentNameForAddress(addr)}</span>
                            <span className="font-semibold">{hex(Number(value), 8)}</span>
                            <span className="text-[0.58rem] tracking-[0.04em] opacity-70">{touched ? 'written' : 'watch'}</span>
                          </div>
                        ))
                      )}
                    </div>
                  </div>
                );
              })}
            </div>
          </Panel>
        </div>

        <div className="grid gap-6 xl:grid-cols-[1fr_1fr_0.9fr]">
          <Panel title="执行轨迹" badge="最近 20 条">
            <div className="space-y-2 text-[0.74rem] leading-6">
              {sim.trace.length
                ? sim.trace.map((entry, index) => (
                  <div key={`${entry}-${index}`} className={`border-2 border-black px-3 py-3 ${index === sim.trace.length - 1 ? 'bg-[#ffd43b]' : 'bg-white'}`}>
                    {entry}
                  </div>
                ))
                : <div className="border-2 border-black bg-white px-3 py-3">暂无执行轨迹</div>}
            </div>
          </Panel>

          <Panel title="外设 / Trap" badge="UART / Timer / IRQ">
            <div className="space-y-3 text-[0.74rem]">
              <div className="border-2 border-black bg-white px-3 py-3">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">UART</div>
                <div className="mt-1 font-bold tracking-[0.03em]">{sim.peripherals.uart}</div>
              </div>
              <div className="border-2 border-black bg-white px-3 py-3">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Timer</div>
                <div className="mt-1 font-bold tracking-[0.03em]">{sim.peripherals.timer}</div>
              </div>
              <div className="border-2 border-black bg-white px-3 py-3">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Trap</div>
                <div className="mt-1 font-bold tracking-[0.03em]">{sim.peripherals.trap}</div>
              </div>
              <div className="border-2 border-black bg-white px-3 py-3">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">IRQ</div>
                <div className="mt-1 font-bold tracking-[0.03em]">{sim.peripherals.irq}</div>
              </div>
            </div>
          </Panel>

          <Panel title="性能统计" badge="执行数据">
            <div className="space-y-3 text-[0.74rem]">
              <div className="border-2 border-black bg-white px-3 py-3">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">周期数</div>
                <div className="mt-1 font-bold tracking-[0.03em]">{sim.stats.cycles}</div>
              </div>
              <div className="border-2 border-black bg-white px-3 py-3">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">指令数</div>
                <div className="mt-1 font-bold tracking-[0.03em]">{sim.stats.instructions}</div>
              </div>
              <div className="border-2 border-black bg-white px-3 py-3">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">停顿数</div>
                <div className="mt-1 font-bold tracking-[0.03em]">{sim.stats.stalls}</div>
              </div>
            </div>
          </Panel>
        </div>
      </div>
    </div>
  );
}
