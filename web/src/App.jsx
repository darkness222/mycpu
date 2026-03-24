'use client';

import { Fragment, useCallback, useEffect, useMemo, useState } from 'react';

const API = import.meta.env.VITE_API_BASE_URL || 'http://localhost:18080';
const STAGES = ['FETCH', 'DECODE', 'EXECUTE', 'MEMORY', 'WRITEBACK'];
const SHORT = ['IF', 'ID', 'EX', 'MEM', 'WB'];
const STAGE_COLORS = {
  IF: 'bg-[#fff59d]',
  ID: 'bg-[#d9f99d]',
  EX: 'bg-[#bae6fd]',
  MEM: 'bg-[#c4b5fd]',
  WB: 'bg-[#fdba74]',
};
const REGS = Array.from({ length: 32 }, (_, i) => `x${i}`);
const SEGMENTS = [
  { name: 'TEXT', start: 0x0000, end: 0x00ff },
  { name: 'DATA', start: 0x0100, end: 0x0fff },
  { name: 'STACK', start: 0x2000, end: 0x3fff },
];

const DEMO = `; myCPU 演示程序
li x1, 5
li x3, 12
add x5, x1, x3
addi x2, x2, -4
sw x5, 0(x2)
lw x6, 0(x2)
sw x6, 0x100
lw x9, 0x100
lui x10, 0x10000
li x11, 72
sw x11, 0(x10)
li x11, 105
sw x11, 0(x10)
halt`;

async function fetchJson(path, options) {
  const res = await fetch(`${API}${path}`, options);
  const data = await res.json();
  if (!res.ok || data.error) throw new Error(data.error || `HTTP ${res.status}`);
  return data;
}

const api = {
  assemble: (source) => fetchJson('/assemble', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ source }) }),
  getState: () => fetchJson('/get_state'),
  getInstructions: () => fetchJson('/get_instructions'),
  setMode: (mode) => fetchJson('/set_mode', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ mode }) }),
  step: () => fetchJson('/step', { method: 'POST' }),
  stepInstruction: () => fetchJson('/step_instruction', { method: 'POST' }),
  reset: () => fetchJson('/reset', { method: 'POST' }),
};

const hex = (value, width = 4) => `0x${(Number(value ?? 0) >>> 0).toString(16).toUpperCase().padStart(width, '0')}`;
const signed = (value) => Number(value ?? 0) | 0;
const alignWord = (addr) => Number(addr ?? 0) & ~0x3;

function normalizeLatch(latch) {
  return { valid: Boolean(latch?.valid), pc: Number(latch?.pc ?? 0) >>> 0, text: latch?.text ? String(latch.text) : '' };
}

function compactInstruction(text) {
  if (!text) return '';
  const trimmed = String(text).trim();
  const [mnemonic, ...rest] = trimmed.split(/\s+/);
  const args = rest.join(' ');
  return args ? `${mnemonic} ${args}`.slice(0, 16) : mnemonic.slice(0, 16);
}

function bubbleMatchesStage(bubbleStage, shortStage) {
  if (!bubbleStage) return false;
  const map = { FETCH: 'IF', DECODE: 'ID', EXECUTE: 'EX', MEMORY: 'MEM', WRITEBACK: 'WB' };
  return bubbleStage === shortStage || map[bubbleStage] === shortStage;
}

function emptyState() {
  return {
    pc: 0, pcInstructionIndex: 0, cycle: 0, mode: 'MULTI_CYCLE', modeName: 'Multi-cycle', truePipeline: false,
    modeNote: '', halted: true, stageIndex: 0,
    registers: Object.fromEntries(REGS.map((name) => [name, 0])),
    memory: {},
    pipelineLatches: { fetch: normalizeLatch(null), ifid: normalizeLatch(null), idex: normalizeLatch(null), exmem: normalizeLatch(null), memwb: normalizeLatch(null) },
    stageViews: { if: normalizeLatch(null), id: normalizeLatch(null), ex: normalizeLatch(null), mem: normalizeLatch(null), wb: normalizeLatch(null) },
    flowSignals: { stall: false, flush: false, forwarding: [], notes: [] },
    bubble: { active: false, stage: '', reason: '' },
    trace: [],
    stats: { cycles: 0, instructions: 0, stalls: 0, flushes: 0, branchTaken: 0, branchNotTaken: 0 },
    peripherals: { uart: '空', timer: '0', trap: '无', irq: '无' },
    csr: { privilegeMode: 'M', mtvec: 0, mepc: 0, mcause: 0, mtval: 0 },
    mmu: { pagingEnabled: false, mappedPages: 0, pageMappings: [] },
  };
}

function fromBackendState(state) {
  const registers = Object.fromEntries(REGS.map((name) => [name, 0]));
  if (Array.isArray(state.registers)) state.registers.forEach((value, index) => { registers[`x${index}`] = Number(value ?? 0) | 0; });
  const csr = {
    privilegeMode: String(state.csr?.privilege_mode ?? 'M'),
    mtvec: Number(state.csr?.mtvec ?? 0) >>> 0,
    mepc: Number(state.csr?.mepc ?? 0) >>> 0,
    mcause: Number(state.csr?.mcause ?? 0) >>> 0,
    mtval: Number(state.csr?.mtval ?? 0) >>> 0,
  };
  const backendState = String(state.state ?? '');
  return {
    pc: Number(state.pc ?? 0) >>> 0,
    pcInstructionIndex: Number(state.pcInstructionIndex ?? 0),
    cycle: Number(state.cycle ?? 0),
    mode: String(state.mode ?? 'MULTI_CYCLE'),
    modeName: String(state.modeName ?? 'Multi-cycle'),
    truePipeline: Boolean(state.truePipeline),
    modeNote: String(state.modeNote ?? ''),
    halted: backendState === 'HALTED',
    stageIndex: Number(state.stageIndex ?? 0),
    registers,
    memory: Object.fromEntries(Object.entries(state.memory ?? {}).map(([k, v]) => [k, Number(v)])),
    pipelineLatches: {
      fetch: normalizeLatch(state.pipelineLatches?.fetch),
      ifid: normalizeLatch(state.pipelineLatches?.ifid),
      idex: normalizeLatch(state.pipelineLatches?.idex),
      exmem: normalizeLatch(state.pipelineLatches?.exmem),
      memwb: normalizeLatch(state.pipelineLatches?.memwb),
    },
    stageViews: {
      if: normalizeLatch(state.stageViews?.if),
      id: normalizeLatch(state.stageViews?.id),
      ex: normalizeLatch(state.stageViews?.ex),
      mem: normalizeLatch(state.stageViews?.mem),
      wb: normalizeLatch(state.stageViews?.wb),
    },
    flowSignals: {
      stall: Boolean(state.flowSignals?.stall),
      flush: Boolean(state.flowSignals?.flush),
      forwarding: Array.isArray(state.flowSignals?.forwarding) ? state.flowSignals.forwarding : [],
      notes: Array.isArray(state.flowSignals?.notes) ? state.flowSignals.notes : [],
    },
    bubble: state.bubble ?? { active: false, stage: '', reason: '' },
    trace: Array.isArray(state.trace) ? state.trace : [],
    stats: {
      cycles: Number(state.stats?.cycles ?? 0), instructions: Number(state.stats?.instructions ?? 0), stalls: Number(state.stats?.stalls ?? 0),
      flushes: Number(state.stats?.flushes ?? 0), branchTaken: Number(state.stats?.branchTaken ?? 0), branchNotTaken: Number(state.stats?.branchNotTaken ?? 0),
    },
    peripherals: {
      uart: state.peripherals?.uart_buffer ? String(state.peripherals.uart_buffer) : '空',
      timer: String(state.peripherals?.timer_value ?? 0),
      trap: backendState === 'EXCEPTION' || backendState === 'INTERRUPT' ? `mcause=${hex(csr.mcause, 8)} mepc=${hex(csr.mepc, 8)}` : '无',
      irq: state.peripherals?.timer_interrupt ? 'Timer IRQ' : state.peripherals?.external_interrupt ? 'External IRQ' : state.peripherals?.software_interrupt ? 'Software IRQ' : '无',
    },
    csr,
    mmu: { pagingEnabled: Boolean(state.mmu?.paging_enabled), mappedPages: Number(state.mmu?.mapped_pages ?? 0), pageMappings: Array.isArray(state.mmu?.page_mappings) ? state.mmu.page_mappings : [] },
  };
}

const Panel = ({ title, badge, children }) => (
  <section className="overflow-hidden border-[3px] border-black bg-white">
    <div className="flex items-center justify-between border-b-[3px] border-black bg-[#2d2d2d] px-4 py-3 text-white">
      <div className="text-[0.72rem] font-bold tracking-[0.05em]">{title}</div>
      {badge ? <div className="border border-black bg-[#ffd43b] px-3 py-1 text-[0.65rem] font-bold tracking-[0.04em] text-black">{badge}</div> : null}
    </div>
    <div className="p-4 md:p-5">{children}</div>
  </section>
);

const Stat = ({ label, value, accent = false }) => (
  <div className={`border-2 border-black p-3 ${accent ? 'bg-[#74c0fc]' : 'bg-white'}`}>
    <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">{label}</div>
    <div className="mt-1 text-[0.95rem] font-bold tracking-[0.03em]">{value}</div>
  </div>
);

function activeIndex(sim) {
  if (sim.halted) return sim.stageIndex === 0 ? Math.max(0, sim.pcInstructionIndex) : Math.max(0, sim.pcInstructionIndex - 1);
  return sim.stageIndex === 0 ? sim.pcInstructionIndex : Math.max(0, sim.pcInstructionIndex - 1);
}

function segmentEntries(segment, memory, registers) {
  const entries = new Map();
  Object.entries(memory ?? {}).forEach(([addrText, value]) => {
    const addr = Number(addrText);
    if (addr >= segment.start && addr <= segment.end) entries.set(addr, { addr, value: Number(value), touched: true });
  });
  const watch = segment.name === 'DATA'
    ? [0x0100, 0x0104, 0x0108, 0x010c]
    : segment.name === 'STACK'
      ? [alignWord(registers?.x2 ?? 0), alignWord(registers?.x2 ?? 0) + 4, alignWord(registers?.x2 ?? 0) + 8, alignWord(registers?.x2 ?? 0) + 12].filter((addr) => addr >= 0x2000 && addr <= 0x3fff)
      : [];
  watch.forEach((addr) => { if (!entries.has(addr)) entries.set(addr, { addr, value: 0, touched: false }); });
  return Array.from(entries.values()).sort((a, b) => a.addr - b.addr);
}

export default function App() {
  const [programText, setProgramText] = useState(DEMO);
  const [program, setProgram] = useState([]);
  const [sim, setSim] = useState(emptyState);
  const [connected, setConnected] = useState(false);
  const [isRunning, setIsRunning] = useState(false);
  const [error, setError] = useState('');
  const [modeResults, setModeResults] = useState({});
  const [timeline, setTimeline] = useState([]);

  const clearTimeline = useCallback(() => {
    setTimeline([]);
  }, []);

  const refresh = useCallback(async () => {
    const [stateResult, instrResult] = await Promise.all([api.getState(), api.getInstructions()]);
    setSim(fromBackendState(stateResult));
    setProgram(Array.isArray(instrResult.instructions) ? instrResult.instructions : []);
  }, []);

  useEffect(() => {
    const poll = async () => {
      try { await refresh(); setConnected(true); } catch { setConnected(false); }
    };
    poll();
    const timer = window.setInterval(poll, 5000);
    return () => window.clearInterval(timer);
  }, [refresh]);

  useEffect(() => {
    if (!isRunning) return undefined;
    let cancelled = false;
    let timer = null;
    const tick = async () => {
      try {
        const next = fromBackendState(await api.step());
        if (cancelled) return;
        setSim(next);
        if (next.halted) { setIsRunning(false); return; }
        timer = window.setTimeout(tick, 220);
      } catch (err) {
        if (!cancelled) { setError(`杩愯澶辫触: ${err.message}`); setIsRunning(false); }
      }
    };
    timer = window.setTimeout(tick, 220);
    return () => { cancelled = true; if (timer) window.clearTimeout(timer); };
  }, [isRunning]);

  useEffect(() => {
    setTimeline((prev) => {
      if (!sim.truePipeline || sim.cycle <= 0) {
        return prev;
      }

      const stalledFetch = sim.flowSignals.stall && !sim.stageViews.if.valid && !sim.halted;
      const stalledFetchIndex = Math.floor((sim.pc || 0) / 4) + 1;
      const stalledFetchText = program[Math.floor((sim.pc || 0) / 4)]?.text || '';

      const stages = {
        IF: sim.stageViews.if.valid
          ? { index: Math.floor(sim.stageViews.if.pc / 4) + 1, text: sim.stageViews.if.text || '' }
          : stalledFetch
            ? { index: stalledFetchIndex, text: stalledFetchText }
            : null,
        ID: sim.stageViews.id.valid ? { index: Math.floor(sim.stageViews.id.pc / 4) + 1, text: sim.stageViews.id.text } : null,
        EX: sim.stageViews.ex.valid ? { index: Math.floor(sim.stageViews.ex.pc / 4) + 1, text: sim.stageViews.ex.text } : null,
        MEM: sim.stageViews.mem.valid ? { index: Math.floor(sim.stageViews.mem.pc / 4) + 1, text: sim.stageViews.mem.text } : null,
        WB: sim.stageViews.wb.valid ? { index: Math.floor(sim.stageViews.wb.pc / 4) + 1, text: sim.stageViews.wb.text } : null,
      };

      const last = prev[prev.length - 1];
      if (last?.cycle === sim.cycle) {
        const next = [...prev];
        next[next.length - 1] = {
          cycle: sim.cycle,
          stages,
          stall: sim.flowSignals.stall,
          flush: sim.flowSignals.flush,
          bubble: sim.bubble,
          notes: sim.flowSignals.forwarding ? (sim.flowSignals.notes || []) : [],
        };
        return next;
      }

      return [...prev, {
        cycle: sim.cycle,
        stages,
        stall: sim.flowSignals.stall,
        flush: sim.flowSignals.flush,
        bubble: sim.bubble,
        notes: sim.flowSignals.notes || [],
      }].slice(-24);
    });
  }, [sim, program]);

  useEffect(() => {
    if (!sim.halted || sim.stats.instructions <= 0) return;
    setModeResults((prev) => ({
      ...prev,
      [sim.mode]: {
        cycles: sim.stats.cycles,
        instructions: sim.stats.instructions,
        stalls: sim.stats.stalls,
        flushes: sim.stats.flushes,
        branchTaken: sim.stats.branchTaken,
        branchNotTaken: sim.stats.branchNotTaken,
        cpi: sim.stats.instructions > 0 ? (sim.stats.cycles / sim.stats.instructions).toFixed(2) : '0.00',
        ipc: sim.stats.cycles > 0 ? (sim.stats.instructions / sim.stats.cycles).toFixed(2) : '0.00',
        stallRate: sim.stats.instructions > 0 ? ((sim.stats.stalls / sim.stats.instructions) * 100).toFixed(1) : '0.0',
      },
    }));
  }, [sim]);

  const assemble = useCallback(async () => {
    setError('');
    setIsRunning(false);
    clearTimeline();
    try { await api.assemble(programText); await refresh(); } catch (err) { setError(`鍔犺浇姹囩紪澶辫触: ${err.message}`); }
  }, [programText, refresh, clearTimeline]);

  const reset = useCallback(async () => {
    setIsRunning(false);
    clearTimeline();
    try { setSim(fromBackendState(await api.reset())); setProgram((await api.getInstructions()).instructions || []); setError(''); } catch (err) { setError(`閲嶇疆澶辫触: ${err.message}`); }
  }, [clearTimeline]);

  const stepStage = useCallback(async () => {
    setIsRunning(false);
    try { setSim(fromBackendState(await api.step())); } catch (err) { setError(`鍗曟闃舵澶辫触: ${err.message}`); }
  }, []);

  const stepInstruction = useCallback(async () => {
    setIsRunning(false);
    try { setSim(fromBackendState(await api.stepInstruction())); } catch (err) { setError(`鍗曟鎸囦护澶辫触: ${err.message}`); }
  }, []);

  const switchMode = useCallback(async (mode) => {
    setIsRunning(false);
    clearTimeline();
    try { setSim(fromBackendState(await api.setMode(mode))); setProgram((await api.getInstructions()).instructions || []); setError(''); } catch (err) { setError(`鍒囨崲妯″紡澶辫触: ${err.message}`); }
  }, [clearTimeline]);

  const currentIndex = activeIndex(sim);
  const currentInstruction = program[currentIndex];

  const badgesByIndex = useMemo(() => {
    const map = new Map();
    if (sim.truePipeline) {
      if (!sim.halted) {
        const fetchIndex = Math.floor((sim.pc || 0) / 4);
        map.set(fetchIndex, ['IF']);
      }
      [['ID', sim.pipelineLatches.ifid], ['EX', sim.pipelineLatches.idex], ['MEM', sim.pipelineLatches.exmem], ['WB', sim.pipelineLatches.memwb]].forEach(([label, latch]) => {
        if (!latch.valid) return;
        const index = Math.floor(latch.pc / 4);
        if (!map.has(index)) map.set(index, []);
        map.get(index).push(label);
      });
      return map;
    }
    map.set(currentIndex, [SHORT[sim.stageIndex] || 'IF']);
    return map;
  }, [sim, currentIndex]);

  const summaries = useMemo(() => SEGMENTS.map((segment) => ({ ...segment, count: segmentEntries(segment, sim.memory, sim.registers).length })), [sim.memory, sim.registers]);

  return (
    <div className="min-h-screen bg-[#f4efea] font-mono text-black">
      <div className="mx-auto max-w-7xl px-4 pb-10 pt-8 md:px-8">
        <header className="mb-8 flex flex-col gap-4 md:flex-row md:items-end md:justify-between">
          <div>
            <div className="mb-3 inline-flex items-center gap-2 border border-black bg-[#ffd43b] px-3 py-1 text-[0.68rem] font-bold tracking-[0.05em]">
              <span className={`inline-block h-2 w-2 rounded-full ${connected ? 'bg-green-500' : 'bg-red-500'}`} />
              {connected ? '后端已连接' : '后端未连接'} / 可视化展示台
            </div>
            <h1 className="text-[2rem] font-bold leading-[1.08] tracking-[0.03em] md:text-[3rem]">myCPU 可视化执行实验台</h1>
            <p className="mt-3 max-w-4xl text-[0.9rem] leading-7 opacity-90">前端展示的数据都直接来自后端 CPU 状态，不再靠 PC 位置猜流水线阶段。</p>
          </div>
          <div className="flex flex-wrap gap-3">
            <button onClick={() => switchMode('MULTI_CYCLE')} className={`border-2 border-black px-5 py-3 text-[0.78rem] font-semibold ${sim.mode === 'MULTI_CYCLE' ? 'bg-[#ffd43b]' : 'bg-white'}`}>多周期</button>
            <button onClick={() => switchMode('PIPELINED')} className={`border-2 border-black px-5 py-3 text-[0.78rem] font-semibold ${sim.mode === 'PIPELINED' ? 'bg-[#ffd43b]' : 'bg-white'}`}>流水线</button>
            <button onClick={assemble} className="border-2 border-black bg-[#74c0fc] px-5 py-3 text-[0.78rem] font-semibold">加载汇编</button>
            <button onClick={reset} className="border-2 border-black bg-white px-5 py-3 text-[0.78rem] font-semibold">重置</button>
            <button onClick={stepStage} className="border-2 border-black bg-[#74c0fc] px-5 py-3 text-[0.78rem] font-semibold">单步阶段</button>
            <button onClick={stepInstruction} className="border-2 border-black bg-white px-5 py-3 text-[0.78rem] font-semibold">单步指令</button>
            <button onClick={() => setIsRunning((v) => !v)} className="border-2 border-black bg-[#74c0fc] px-5 py-3 text-[0.78rem] font-semibold">{isRunning ? '暂停' : '运行'}</button>
          </div>
        </header>

        <div className={`mb-6 grid gap-4 ${sim.truePipeline ? 'md:grid-cols-7' : 'md:grid-cols-5'}`}>
          <Stat label="模式" value={sim.modeName} accent />
          <Stat label="PC" value={hex(sim.pc, 8)} accent />
          <Stat label="指令序号" value={String(sim.pcInstructionIndex)} />
          <Stat label="周期" value={String(sim.cycle)} />
          <Stat label="状态" value={sim.halted ? 'HALTED' : STAGES[sim.stageIndex] || 'RUNNING'} />
          {sim.truePipeline ? <Stat label="STALL" value={sim.flowSignals.stall ? 'ON' : 'OFF'} /> : null}
          {sim.truePipeline ? <Stat label="FLUSH" value={sim.flowSignals.flush ? 'ON' : 'OFF'} /> : null}
        </div>

        <div className="mb-6 border-2 border-black bg-white px-4 py-3 text-[0.78rem] leading-6">
          <div className="font-bold tracking-[0.04em]">当前模式说明</div>
          <div className="mt-2">{sim.modeNote || '后端未提供模式说明。'}</div>
        </div>

        <div className="mb-6 grid gap-6 xl:grid-cols-[1.02fr_1.15fr_0.95fr]">
          <Panel title="汇编编辑器" badge="输入程序">
            <textarea value={programText} onChange={(e) => setProgramText(e.target.value)} spellCheck={false} className="min-h-[430px] w-full resize-none border-2 border-black bg-[#f4efea] p-4 text-[0.78rem] leading-6 outline-none" />
            {error ? <div className="mt-4 border-2 border-black bg-[#ffd43b] px-3 py-3 text-[0.74rem] font-semibold">{error}</div> : null}
          </Panel>

          <Panel title="数据通路" badge={sim.truePipeline ? '真实锁存器状态' : '多周期阶段执行'}>
            <div className="mb-4 border-2 border-black bg-[#f4efea] p-3 text-[0.72rem] leading-6">
              <div className="font-bold tracking-[0.04em]">当前参考指令</div>
              <div className="mt-2 text-[0.92rem] font-bold tracking-[0.03em]">{currentInstruction?.text || '暂无'}</div>
            </div>
            <div className="grid grid-cols-5 gap-2">
              {SHORT.map((label, index) => <div key={label} className={`border-2 border-black p-3 text-center ${sim.truePipeline || sim.stageIndex === index ? 'bg-[#74c0fc]' : 'bg-white'}`}><div className="text-[0.58rem] opacity-80">阶段</div><div className="mt-1 text-[0.78rem] font-bold">{label}</div></div>)}
            </div>
            <div className="mt-4 grid gap-3 md:grid-cols-2 xl:grid-cols-5">
              {[
                ['PC / 指令存储', currentInstruction?.text || '空闲'],
                ['译码', sim.pipelineLatches.ifid.text || '等待'],
                ['执行 / ALU', sim.pipelineLatches.idex.text || '等待'],
                ['访存', sim.pipelineLatches.exmem.text || '等待'],
                ['写回', sim.pipelineLatches.memwb.text || '等待'],
              ].map(([title, body]) => <div key={title} className="border-2 border-black bg-white p-3 text-[0.72rem]"><div className="font-bold tracking-[0.04em]">{title}</div><div className="mt-2 min-h-[2.4rem] leading-6">{body}</div></div>)}
            </div>
            {sim.truePipeline ? (
              <div className="mt-4 grid gap-3 md:grid-cols-4 text-[0.72rem]">
                <div className={`border-2 border-black p-3 ${sim.flowSignals.forwarding.length ? 'bg-[#74c0fc]' : 'bg-white'}`}>{sim.flowSignals.forwarding.length ? sim.flowSignals.forwarding.join(' / ') : '当前无旁路'}</div>
                <div className={`border-2 border-black p-3 ${sim.flowSignals.stall ? 'bg-[#ffd43b]' : 'bg-white'}`}>Stall: {sim.flowSignals.stall ? 'ON' : 'OFF'}</div>
                <div className={`border-2 border-black p-3 ${sim.flowSignals.flush ? 'bg-[#ffd43b]' : 'bg-white'}`}>Flush: {sim.flowSignals.flush ? 'ON' : 'OFF'}</div>
                <div className={`border-2 border-black p-3 ${sim.bubble.active ? 'bg-[#ffd43b]' : 'bg-white'}`}>{sim.bubble.active ? `${sim.bubble.stage} / ${sim.bubble.reason}` : '当前没有气泡'}</div>
              </div>
            ) : null}
          </Panel>

          <Panel title="程序视图" badge={`${program.length} 条指令`}>
            <div className="mb-3 border-2 border-black bg-[#f4efea] px-3 py-2 text-[0.72rem] leading-6">{sim.truePipeline ? '按后端真实锁存器显示 IF/ID/EX/MEM。' : '多周期模式下显示当前阶段所在指令。'}</div>
            <div className="max-h-[620px] space-y-2 overflow-auto pr-1">
              {program.map((instruction, index) => {
                const badges = badgesByIndex.get(index) || [];
                const active = badges.length > 0;
                const done = !sim.truePipeline && (index < currentIndex || (sim.halted && index <= currentIndex));
                return <div key={`${instruction.address}-${index}`} className={`border-2 border-black px-3 py-3 text-[0.74rem] ${active ? 'bg-[#74c0fc]' : done ? 'bg-white' : 'bg-[#f4efea]'}`}><div className="flex items-center justify-between gap-3"><div className="flex min-w-0 items-center gap-3"><span className="w-16 shrink-0 text-[0.65rem] font-bold opacity-80">{hex(instruction.address ?? index * 4, 4)}</span><span className="truncate font-semibold">{instruction.text}</span></div><div className="flex flex-wrap gap-1">{badges.length ? badges.map((badge) => <span key={`${index}-${badge}`} className="border border-black bg-white px-2 py-0.5 text-[0.58rem] font-bold">{badge}</span>) : <span className="border border-black bg-white px-2 py-0.5 text-[0.58rem] font-bold">{sim.truePipeline ? '空闲' : done ? '已执行' : '等待'}</span>}</div></div></div>;
              })}
            </div>
          </Panel>
        </div>

        <div className="mb-6">
          <Panel title="流水线时间轴" badge="按周期展开">
            <div className="mb-3 border-2 border-black bg-[#f4efea] px-3 py-2 text-[0.72rem] leading-6">
              {sim.truePipeline
                ? '横轴是时间周期，纵轴是 IF/ID/EX/MEM/WB。格子里的数字表示当前处在该阶段的指令序号。'
                : '当前是多周期模式，切到流水线模式后这里会按时间轴展示重叠执行。'}
            </div>
            {sim.truePipeline ? (
              <div className="overflow-x-auto">
                <div className="inline-grid min-w-full" style={{ gridTemplateColumns: `72px repeat(${Math.max(timeline.length, 1)}, minmax(48px, 1fr))` }}>
                  <div className="border-2 border-black bg-[#2d2d2d] px-2 py-2 text-center text-[0.68rem] font-bold text-white">阶段</div>
                  {timeline.length ? timeline.map((item) => (
                    <div key={`head-${item.cycle}`} className="border-2 border-l-0 border-black bg-[#ffd43b] px-2 py-2 text-center text-[0.68rem] font-bold">
                      {item.cycle}
                    </div>
                  )) : (
                    <div className="border-2 border-l-0 border-black bg-white px-2 py-2 text-center text-[0.68rem]">-</div>
                  )}

                  {SHORT.map((stage) => (
                    <Fragment key={stage}>
                      <div className={`border-2 border-t-0 border-black px-2 py-3 text-center text-[0.74rem] font-bold ${STAGE_COLORS[stage]}`}>
                        {stage}
                      </div>
                      {timeline.length ? timeline.map((item) => (
                        <div
                          key={`${stage}-${item.cycle}`}
                          className={`border-2 border-l-0 border-t-0 border-black px-1 py-2 text-center ${
                            item.flush
                              ? 'bg-[#ff6b6b] text-black'
                              : item.stall && (stage === 'IF' || stage === 'ID')
                                ? 'bg-[#ff6b6b] text-black'
                                : item.bubble?.active && bubbleMatchesStage(item.bubble?.stage, stage)
                                  ? 'bg-[#ff6b6b] text-black'
                                  : item.stages[stage]
                                    ? STAGE_COLORS[stage]
                                    : 'bg-[#f4efea]'
                          }`}
                        >
                          {item.stages[stage] ? (
                            <div className="leading-tight">
                              {item.stall && (stage === 'IF' || stage === 'ID') ? (
                                <div className="mb-1 text-[0.5rem] font-extrabold uppercase tracking-[0.05em]">STALL</div>
                              ) : null}
                              {item.flush ? (
                                <div className="mb-1 text-[0.5rem] font-extrabold uppercase tracking-[0.05em]">FLUSH</div>
                              ) : null}
                              {item.bubble?.active && bubbleMatchesStage(item.bubble?.stage, stage) ? (
                                <div className="mb-1 text-[0.5rem] font-extrabold uppercase tracking-[0.05em]">BUBBLE</div>
                              ) : null}
                              <div className="text-[0.8rem] font-bold">{item.stages[stage].index}</div>
                              <div className="mt-1 text-[0.52rem] opacity-80">{compactInstruction(item.stages[stage].text)}</div>
                            </div>
                          ) : item.flush ? (
                            <div className="text-[0.56rem] font-bold">FLUSH</div>
                          ) : item.stall && (stage === 'IF' || stage === 'ID') ? (
                            <div className="text-[0.56rem] font-bold">STALL</div>
                          ) : item.bubble?.active && bubbleMatchesStage(item.bubble?.stage, stage) ? (
                            <div className="text-[0.56rem] font-bold">BUBBLE</div>
                          ) : (
                            ''
                          )}
                        </div>
                      )) : (
                        <div key={`${stage}-empty`} className="border-2 border-l-0 border-t-0 border-black bg-white px-2 py-3 text-center text-[0.78rem]">
                          {' '}
                        </div>
                      )}
                    </Fragment>
                  ))}
                </div>
              </div>
            ) : (
              <div className="border-2 border-black bg-white px-3 py-4 text-[0.74rem]">
                暂无时间轴。请切换到“流水线”模式并开始单步或运行。
              </div>
            )}
          </Panel>
        </div>

        <div className="mb-6 grid gap-6 xl:grid-cols-[1fr_1fr]">
          <Panel title="寄存器" badge="GPR 视图">
            <div className="mb-3 border-2 border-black bg-[#f4efea] px-3 py-2 text-[0.72rem] leading-6">上方蓝条只表示数值的大致相对大小，真正的寄存器值请看右侧的 HEX 和 DEC。</div>
            <div className="grid gap-2 md:grid-cols-2">
              {REGS.map((name) => {
                const value = name === 'x0' ? 0 : sim.registers[name];
                const width = Math.min(100, Math.max(value === 0 ? 0 : 10, Math.abs(Number(value)) / 64));
                return (
                  <div key={name} className="border-2 border-black bg-white px-3 py-3 text-[0.72rem]">
                    <div className="mb-2 flex items-center justify-between gap-3">
                      <span className="font-bold">{name}</span>
                      <div className="min-w-0 flex-1">
                        <div className="h-3 overflow-hidden border border-black bg-[#f4efea]">
                          <div className="h-full bg-[#74c0fc]" style={{ width: `${width}%` }} />
                        </div>
                      </div>
                    </div>
                    <div className="grid grid-cols-[1fr_auto] items-center gap-x-4 gap-y-1 text-[0.68rem]">
                      <span className="opacity-70">HEX</span>
                      <span className="font-semibold">{hex(value, 8)}</span>
                      <span className="opacity-70">DEC</span>
                      <span className="font-semibold">{signed(value)}</span>
                    </div>
                  </div>
                );
              })}
            </div>
          </Panel>

          <Panel title="分段内存" badge="TEXT / DATA / STACK">
            <div className="mb-3 border-2 border-black bg-[#f4efea] px-3 py-2 text-[0.72rem] leading-6">written 表示后端已有真实内容，watch 表示前端主动观察的关键地址。</div>
            <div className="mb-4 grid gap-2 md:grid-cols-3">{summaries.map((segment) => <div key={segment.name} className="border-2 border-black bg-[#f4efea] px-3 py-2 text-[0.68rem] font-bold">{segment.name} · {segment.count} cells</div>)}</div>
            <div className="space-y-4">
              {SEGMENTS.map((segment) => {
                const entries = segmentEntries(segment, sim.memory, sim.registers);
                return <div key={segment.name}><div className="mb-2 inline-block border-2 border-black bg-[#74c0fc] px-3 py-1 text-[0.68rem] font-bold">{segment.name}</div><div className="space-y-2">{entries.length === 0 ? <div className="border border-black bg-[#f4efea] px-3 py-2 text-[0.68rem] opacity-60">空</div> : entries.map(({ addr, value, touched }) => <div key={addr} className={`grid grid-cols-[90px_1fr_auto_auto_auto] items-center gap-3 border-2 border-black px-3 py-3 text-[0.74rem] ${touched ? 'bg-white' : 'bg-[#f4efea]'}`}><span className="font-bold">{hex(addr, 4)}</span><div className="h-3 overflow-hidden border border-black bg-[#f4efea]"><div className={`h-full ${touched ? 'bg-black' : 'bg-[#74c0fc]'}`} style={{ width: `${Math.min(100, Math.max(Number(value) === 0 ? 0 : 8, Math.abs(Number(value)) / 64))}%` }} /></div><span className="text-[0.62rem] font-bold">{SEGMENTS.find((s) => addr >= s.start && addr <= s.end)?.name || 'OTHER'}</span><span className="font-semibold">{hex(Number(value), 8)}</span><span className="text-[0.58rem] opacity-70">{touched ? 'written' : 'watch'}</span></div>)}</div></div>;
              })}
            </div>
          </Panel>
        </div>

        <div className="space-y-6">
          <Panel title="模式对比" badge="最近一次结果">
            <div className="mb-4 grid gap-3 md:grid-cols-2">
              {['MULTI_CYCLE', 'PIPELINED'].map((modeKey) => {
                const result = modeResults[modeKey];
                const isPipeline = modeKey === 'PIPELINED';
                return (
                  <div key={modeKey} className={`border-2 border-black px-4 py-4 ${isPipeline ? 'bg-[#fff4d6]' : 'bg-white'}`}>
                    <div className="flex items-center justify-between gap-3">
                      <div className="text-[0.7rem] font-bold uppercase tracking-[0.06em] opacity-80">
                        {isPipeline ? 'Pipelined' : 'Multi-cycle'}
                      </div>
                      <div className={`border border-black px-2 py-1 text-[0.58rem] font-bold uppercase tracking-[0.05em] ${isPipeline ? 'bg-[#ff922b]' : 'bg-[#74c0fc]'}`}>
                        {result ? `${result.instructions} instr` : 'no data'}
                      </div>
                    </div>
                    {result ? (
                      <div className="mt-4 space-y-4">
                        <div className="grid grid-cols-2 gap-3">
                          <div className="border border-black bg-white px-3 py-2">
                            <div className="text-[0.58rem] uppercase opacity-70">CPI</div>
                            <div className="mt-1 text-[1.15rem] font-bold">{result.cpi}</div>
                          </div>
                          <div className="border border-black bg-white px-3 py-2">
                            <div className="text-[0.58rem] uppercase opacity-70">IPC</div>
                            <div className="mt-1 text-[1.15rem] font-bold">{result.ipc}</div>
                          </div>
                        </div>
                        <div className="grid grid-cols-2 gap-x-4 gap-y-2 text-[0.72rem]">
                          <div>Cycles: <span className="font-bold">{result.cycles}</span></div>
                          <div>Instr: <span className="font-bold">{result.instructions}</span></div>
                          <div>Stalls: <span className="font-bold">{result.stalls}</span></div>
                          <div>Flushes: <span className="font-bold">{result.flushes}</span></div>
                          <div>Stall Rate: <span className="font-bold">{result.stallRate}%</span></div>
                          <div>Branches: <span className="font-bold">{result.branchTaken + result.branchNotTaken}</span></div>
                        </div>
                      </div>
                    ) : (
                      <div className="mt-3 text-[0.72rem] opacity-60">还没有记录到该模式结果。</div>
                    )}
                  </div>
                );
              })}
            </div>
            {modeResults.MULTI_CYCLE && modeResults.PIPELINED ? (
              <div className="border-2 border-black bg-[#f4efea] px-4 py-3 text-[0.74rem] leading-6">
                <div className="font-bold tracking-[0.04em]">对比结论</div>
                <div className="mt-2">
                  流水线模式相对多周期模式的周期数降低到
                  <span className="mx-1 font-bold">
                    {(modeResults.PIPELINED.cycles / modeResults.MULTI_CYCLE.cycles).toFixed(2)}x
                  </span>
                  ，等价于获得了
                  <span className="mx-1 font-bold">
                    {(modeResults.MULTI_CYCLE.cycles / modeResults.PIPELINED.cycles).toFixed(2)}x
                  </span>
                  的周期级加速比。
                </div>
              </div>
            ) : null}
          </Panel>

          <Panel title="执行轨迹" badge="最近 20 条">
            <div className="grid gap-2 text-[0.74rem] leading-6 sm:grid-cols-2">
              {sim.trace.length ? sim.trace.map((entry, index) => (
                <div
                  key={`${entry}-${index}`}
                  className={`border-2 border-black px-3 py-3 ${index === sim.trace.length - 1 ? 'bg-[#ffd43b]' : 'bg-white'}`}
                >
                  {entry}
                </div>
              )) : <div className="border-2 border-black bg-white px-3 py-3 sm:col-span-2">暂无执行轨迹</div>}
            </div>
          </Panel>

          <div className="grid gap-6 xl:grid-cols-2 xl:items-start">
            <Panel title="外设 / Trap" badge="UART / Timer / IRQ">
              <div className="grid gap-3 sm:grid-cols-2 text-[0.74rem]">
              <div className="border-2 border-black bg-white px-4 py-4">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">UART</div>
                <div className="mt-2 min-h-[1.5rem] font-bold break-all">{sim.peripherals.uart || '空'}</div>
              </div>
              <div className="border-2 border-black bg-white px-4 py-4">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Timer</div>
                <div className="mt-2 text-[1rem] font-bold">{sim.peripherals.timer}</div>
              </div>
              <div className="border-2 border-black bg-white px-4 py-4 sm:col-span-2">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Trap</div>
                <div className="mt-2 min-h-[1.5rem] font-bold break-all">{sim.peripherals.trap}</div>
              </div>
              <div className="border-2 border-black bg-white px-4 py-4 sm:col-span-2">
                <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">IRQ</div>
                <div className="mt-2 min-h-[1.5rem] font-bold break-all">{sim.peripherals.irq}</div>
              </div>
            </div>
          </Panel>

            <Panel title="CSR / MMU" badge="状态快照">
              <div className="space-y-3 text-[0.74rem]">
              <div className="grid gap-3 sm:grid-cols-2">
                <div className="border-2 border-black bg-white px-4 py-4">
                  <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Privilege</div>
                  <div className="mt-2 text-[1rem] font-bold">{sim.csr.privilegeMode}</div>
                </div>
                <div className="border-2 border-black bg-white px-4 py-4">
                  <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Paging</div>
                  <div className="mt-2 text-[1rem] font-bold">{sim.mmu.pagingEnabled ? 'ON' : 'OFF'}</div>
                </div>
              </div>
              <div className="grid gap-3 sm:grid-cols-2">
                <div className="border-2 border-black bg-white px-4 py-4">
                  <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Trap CSR</div>
                  <div className="mt-2 space-y-1">
                    <div>mtvec: <span className="font-bold">{hex(sim.csr.mtvec, 8)}</span></div>
                    <div>mepc: <span className="font-bold">{hex(sim.csr.mepc, 8)}</span></div>
                    <div>mcause: <span className="font-bold">{hex(sim.csr.mcause, 8)}</span></div>
                    <div>mtval: <span className="font-bold">{hex(sim.csr.mtval, 8)}</span></div>
                  </div>
                </div>
                <div className="border-2 border-black bg-white px-4 py-4">
                  <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-80">Page Map</div>
                  <div className="mt-2">Mapped Pages: <span className="font-bold">{sim.mmu.mappedPages}</span></div>
                  <div className="mt-2 space-y-1 text-[0.68rem] opacity-85">
                    {sim.mmu.pageMappings.length ? sim.mmu.pageMappings.slice(0, 4).map((mapping) => <div key={mapping}>{mapping}</div>) : <div>当前没有可展示的页表映射。</div>}
                  </div>
                </div>
              </div>
              </div>
            </Panel>
          </div>
        </div>
      </div>
    </div>
  );
}
