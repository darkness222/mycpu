'use client';

import { useCallback, useEffect, useMemo, useState } from 'react';
import DinoGame from './DinoGame';

const API = import.meta.env.VITE_API_BASE_URL || 'http://localhost:18080';
const REGS = Array.from({ length: 32 }, (_, i) => `x${i}`);
const PIPELINE_STAGES = ['IF', 'ID', 'EX', 'MEM', 'WB'];
const STAGE_COLORS = {
  IF: 'bg-[#fff3bf]',
  ID: 'bg-[#d8f5a2]',
  EX: 'bg-[#b5e3ff]',
  MEM: 'bg-[#d0bfff]',
  WB: 'bg-[#ffc078]',
  EVENT: 'bg-[#ffd8a8]',
};
const CUSTOM_HINT = `; 自定义程序
; 这里可以输入当前已支持的汇编指令，然后点击“重新汇编”
; 如果你想观察 stall / flush / bubble，建议切到流水线模式
; 如果你想按阶段一步一步讲解，也可以留在多周期模式

li x1, 1
li x2, 2
add x3, x1, x2
halt`;

const DEMO = `; 默认演示
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

const DEMO_PROGRAMS = [
  {
    key: 'default',
    label: '默认程序',
    source: DEMO,
    note: '覆盖寄存器、访存和 UART。',
    guide: '看程序视图、寄存器摘要、UART 输出。',
    recommendedMode: 'MULTI_CYCLE',
    recommendedModeLabel: '多周期',
  },
  {
    key: 'pipeline',
    label: '流水线冒险',
    source: `; Pipeline demo
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
halt`,
    note: '展示 forwarding、stall 和 flush。',
    guide: '看流水线摘要、时间轴和分支统计。',
    recommendedMode: 'PIPELINED',
    recommendedModeLabel: '流水线',
  },
  {
    key: 'fault',
    label: '地址越界',
    source: `; Fault demo
li x1, 0xffffffff
lw x2, 0(x1)
halt`,
    note: '触发访存异常。',
    guide: '看 Trap 面板里的 mcause、mepc、mtval。',
    recommendedMode: 'MULTI_CYCLE',
    recommendedModeLabel: '多周期',
  },
  {
    key: 'uart',
    label: 'UART',
    source: `; UART demo
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
halt`,
    note: '向 UART MMIO 地址输出 Hello。',
    guide: '看外设面板中的 UART 缓冲区。',
    recommendedMode: 'MULTI_CYCLE',
    recommendedModeLabel: '多周期',
  },
  {
    key: 'mext',
    label: 'RV32M',
    source: `; RV32M demo
li x1, 3
li x2, 5
mul x3, x1, x2
sw x3, 0x0100
li x4, 10
li x5, 3
div x6, x4, x5
rem x7, x4, x5
sw x6, 0x0104
sw x7, 0x0108
li x8, -20
li x9, 6
div x10, x8, x9
rem x11, x8, x9
sw x10, 0x010c
sw x11, 0x0110
halt`,
    note: '验证乘除法与结果写回。',
    guide: '看 DATA 摘要、周期统计和模式对比。',
    recommendedMode: 'MULTI_CYCLE',
    recommendedModeLabel: '多周期',
  },
  {
    key: 'matmul-rv32i',
    label: '矩阵乘法 RV32I',
    source: `; RV32I matrix demo
li x1, 6
li x2, 7
li x3, 5
li x4, 4
li x5, 0
li x6, 0
loop_a:
beq x6, x2, done_a
add x5, x5, x1
addi x6, x6, 1
jal x0, loop_a
done_a:
li x7, 0
li x8, 0
loop_b:
beq x8, x4, done_b
add x7, x7, x3
addi x8, x8, 1
jal x0, loop_b
done_b:
add x9, x5, x7
sw x9, 0x0120
halt`,
    note: '标量重复加法版本，不是向量运算。',
    guide: '和 RV32IM 的标量 mul 版本对比周期、IPC、stall。',
    recommendedMode: 'MULTI_CYCLE',
    recommendedModeLabel: '多周期',
  },
  {
    key: 'matmul-rv32im',
    label: '矩阵乘法 RV32IM',
    source: `; RV32IM matrix demo
li x1, 6
li x2, 7
li x3, 5
li x4, 4
mul x5, x1, x2
mul x6, x3, x4
add x7, x5, x6
sw x7, 0x0120
halt`,
    note: '标量 mul 指令版本，不是向量运算。',
    guide: '重点看它与 RV32I 重复加法版本的周期差异。',
    recommendedMode: 'MULTI_CYCLE',
    recommendedModeLabel: '多周期',
  },
  {
    key: 'mmu',
    label: 'MMU / 页表',
    source: `; MMU observe
li x1, 0x100
lw x2, 0(x1)
halt`,
    note: '观察分页开启状态与页表映射。',
    guide: '看 CSR / MMU 面板中的 paging_enabled 和 page_mappings。',
    recommendedMode: 'MULTI_CYCLE',
    recommendedModeLabel: '多周期',
  },
  {
    key: 'interrupt',
    label: '中断演示',
    source: `; Interrupt observe
li x1, 1
addi x1, x1, 1
addi x1, x1, 1
addi x1, x1, 1
halt`,
    note: '结合主界面的 IRQ / Trap / CSR 区观察联动。',
    guide: '看 IRQ、Trap、mepc、mcause 是否变化。',
    recommendedMode: 'MULTI_CYCLE',
    recommendedModeLabel: '多周期',
  },
  {
    key: 'custom',
    label: '自定义程序',
    source: CUSTOM_HINT,
    note: '输入当前已支持的汇编指令，自由演示。',
    guide: '你可以输入已支持的汇编指令并重新汇编；看阶梯图建议切流水线，看逐阶段执行可用多周期。',
    recommendedMode: '',
    recommendedModeLabel: '按用途选择',
  },
];

const SEGMENTS = [
  { key: 'TEXT', start: 0x0000, end: 0x00ff },
  { key: 'DATA', start: 0x0100, end: 0x0fff },
  { key: 'STACK', start: 0x2000, end: 0x3fff },
];
const TRACE_VISIBLE_COUNT = 10;
const MULTI_CYCLE_STAGE_KEYS = ['if', 'id', 'ex', 'mem', 'wb'];
const MULTI_CYCLE_STAGE_LABELS = ['取指 IF', '译码 ID', '执行 EX', '访存 MEM', '写回 WB'];

async function fetchJson(path, options) {
  const res = await fetch(`${API}${path}`, options);
  const data = await res.json();
  if (!res.ok || data.error) {
    throw new Error(data.error || `HTTP ${res.status}`);
  }
  return data;
}

const api = {
  assemble: (source) => fetchJson('/assemble', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ source }),
  }),
  getState: () => fetchJson('/get_state'),
  getInstructions: () => fetchJson('/get_instructions'),
  setMode: (mode) => fetchJson('/set_mode', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ mode }),
  }),
  stepStage: () => fetchJson('/step', { method: 'POST' }),
  stepInstruction: () => fetchJson('/step_instruction', { method: 'POST' }),
  reset: () => fetchJson('/reset', { method: 'POST' }),
};

const hex = (value, width = 8) => `0x${(Number(value ?? 0) >>> 0).toString(16).toUpperCase().padStart(width, '0')}`;
const signed = (value) => Number(value ?? 0) | 0;
const alignWord = (addr) => Number(addr ?? 0) & ~0x3;
const formatModeName = (mode) => (mode === 'PIPELINED' ? '\u6d41\u6c34\u7ebf' : '\u591a\u5468\u671f');
const TRACE_MNEMONIC_RE = /(lui|auipc|jalr|jal|beq|bne|blt|bge|bltu|bgeu|lb|lh|lw|lbu|lhu|sb|sh|sw|addi|slti|sltiu|xori|ori|andi|slli|srli|srai|add|sub|sll|slt|sltu|xor|srl|sra|or|and|mul|mulh|mulhsu|mulhu|div|divu|rem|remu|ecall|ebreak|mret|csrrw|csrrs|csrrc|csrrwi|csrrsi|csrrci|fence\.i|fence|halt|\.word)\b.*$/i;

function extractInstructionSuffix(text) {
  const value = String(text ?? '').trim();
  const match = value.match(TRACE_MNEMONIC_RE);
  return match ? match[0] : value;
}

function translateFlowNote(note) {
  const value = String(note ?? '').trim();
  if (!value) return '';
  if (value === 'Load-use hazard detected, pipeline stalled' || value === 'Load-use hazard') {
    return '\u68c0\u6d4b\u5230 load-use hazard\uff0c\u6d41\u6c34\u7ebf\u5df2\u6682\u505c\u4e00\u4e2a\u5468\u671f';
  }
  if (value === 'Multi-cycle divide in EX') {
    return '\u9664\u6cd5\u6307\u4ee4\u6b63\u5728 EX \u9636\u6bb5\u5206\u591a\u5468\u671f\u6267\u884c';
  }
  if (value === 'Divide completed') {
    return '\u9664\u6cd5\u6267\u884c\u5b8c\u6210';
  }
  if (value === 'BHT predicts branch taken') {
    return '\u5206\u652f\u9884\u6d4b\u5668\u9884\u6d4b\u672c\u6b21\u8df3\u8f6c\u6210\u7acb';
  }
  return value;
}

function formatTraceEntry(entry) {
  const raw = String(entry ?? '').trim();
  if (!raw) return '\u7a7a';

  if (raw === '[CPU] reset complete, ready to execute') return '[CPU] \u590d\u4f4d\u5b8c\u6210\uff0c\u53ef\u4ee5\u5f00\u59cb\u6267\u884c';
  if (raw === '[PIPE] reset - ready to execute') return '[\u6d41\u6c34\u7ebf] \u590d\u4f4d\u5b8c\u6210\uff0c\u53ef\u4ee5\u5f00\u59cb\u6267\u884c';
  if (raw === '[PIPE] binary loaded') return '[\u6d41\u6c34\u7ebf] \u4e8c\u8fdb\u5236\u7a0b\u5e8f\u5df2\u52a0\u8f7d';
  if (raw === '[PIPE] failed to parse ELF') return '[\u6d41\u6c34\u7ebf] ELF \u89e3\u6790\u5931\u8d25';
  if (raw === '[PIPE] failed to load ELF segments') return '[\u6d41\u6c34\u7ebf] ELF \u6bb5\u88c5\u8f7d\u5931\u8d25';
  if (raw === '[PIPE] halt - memory not connected') return '[\u6d41\u6c34\u7ebf] \u5df2\u505c\u673a\uff1a\u5185\u5b58\u5c1a\u672a\u8fde\u63a5';
  if (raw === '[PIPE] halt - pipeline drained') return '[\u6d41\u6c34\u7ebf] \u5df2\u505c\u673a\uff1a\u6d41\u6c34\u7ebf\u5df2\u5b8c\u5168\u6392\u7a7a';
  if (raw === '[SYSTEM] mret') return '[\u7cfb\u7edf] \u6267\u884c mret\uff0c\u51c6\u5907\u4ece trap \u8fd4\u56de';
  if (raw === '[SYSTEM] ecall') return '[\u7cfb\u7edf] \u6267\u884c ecall\uff0c\u89e6\u53d1 trap';
  if (raw === '[SYSTEM] ebreak') return '[\u7cfb\u7edf] \u6267\u884c ebreak\uff0c\u89e6\u53d1\u65ad\u70b9 trap';
  if (raw === '[SYSTEM] ecall - delegated to official trap vector') return '[\u7cfb\u7edf] ecall \u5df2\u59d4\u6258\u5230\u6807\u51c6 trap \u5411\u91cf';
  if (raw === '[SYSTEM] ecall - entering trap handler') return '[\u7cfb\u7edf] ecall \u6b63\u5728\u8fdb\u5165 trap \u5904\u7406\u6d41\u7a0b';
  if (raw === '[SYSTEM] ebreak - entering trap handler') return '[\u7cfb\u7edf] ebreak \u6b63\u5728\u8fdb\u5165 trap \u5904\u7406\u6d41\u7a0b';
  if (raw === '[SBI] shutdown request') return '[SBI] \u6536\u5230\u5173\u673a\u8bf7\u6c42';

  let match = raw.match(/^\[CPU\] program loaded at (0x[0-9a-fA-F]+) \((\d+) instructions\)$/);
  if (match) return `[CPU] \u7a0b\u5e8f\u5df2\u52a0\u8f7d\u5230 ${match[1]}\uff0c\u5171 ${match[2]} \u6761\u6307\u4ee4`;
  match = raw.match(/^\[PIPE\] program loaded at (0x[0-9a-fA-F]+) \((\d+) instructions\)$/);
  if (match) return `[\u6d41\u6c34\u7ebf] \u7a0b\u5e8f\u5df2\u52a0\u8f7d\u5230 ${match[1]}\uff0c\u5171 ${match[2]} \u6761\u6307\u4ee4`;
  match = raw.match(/^\[PIPE\] ELF loaded: entry=(0x[0-9a-fA-F]+)$/);
  if (match) return `[\u6d41\u6c34\u7ebf] ELF \u5df2\u52a0\u8f7d\uff0c\u5165\u53e3\u5730\u5740 ${match[1]}`;
  match = raw.match(/^\[WARN\] PC dropped below official ELF base: (0x[0-9a-fA-F]+)$/);
  if (match) return `[\u8b66\u544a] PC \u4f4e\u4e8e ELF \u5b98\u65b9\u57fa\u5740\uff1a${match[1]}`;
  if (raw === '[PIPE][WARN] PC dropped below official ELF base') return '[\u6d41\u6c34\u7ebf][\u8b66\u544a] PC \u4f4e\u4e8e ELF \u5b98\u65b9\u57fa\u5740';
  match = raw.match(/^\[IF\] PC=(0x[0-9a-fA-F]+).*$/);
  if (match) return `[\u53d6\u6307] PC=${match[1]}\uff0c\u51c6\u5907\u83b7\u53d6 ${extractInstructionSuffix(raw)}`;
  match = raw.match(/^\[ID\].*?((?:rs1=x\d+=-?\d+)\s+(?:rs2=x\d+=-?\d+))$/);
  if (match) return `[\u8bd1\u7801] ${extractInstructionSuffix(raw.replace(/\s+\|\s+rs1=.*$/, ''))} | ${match[1]}`;
  match = raw.match(/^\[EX\] execute: (.+) => ALU result=(-?\d+)$/);
  if (match) return `[\u6267\u884c] ${match[1]}\uff0cALU \u7ed3\u679c = ${match[2]}`;
  match = raw.match(/^\[EX\] (.+) -> branch (0x[0-9a-fA-F]+)$/);
  if (match) return `[\u6267\u884c] ${match[1]}\uff0c\u5206\u652f\u8df3\u8f6c\u5230 ${match[2]}`;
  match = raw.match(/^\[EX\] (.+) -> addr=(0x[0-9a-fA-F]+)$/);
  if (match) return `[\u6267\u884c] ${match[1]}\uff0c\u8ba1\u7b97\u5730\u5740 ${match[2]}`;
  match = raw.match(/^\[EX\] (.+) -> alu=(-?\d+)$/);
  if (match) return `[\u6267\u884c] ${match[1]}\uff0cALU \u7ed3\u679c = ${match[2]}`;
  match = raw.match(/^\[MEM\] access: (.+?) => load=(-?\d+)$/);
  if (match) return `[\u8bbf\u5b58] ${match[1]}\uff0c\u8bfb\u53d6\u5230 ${match[2]}`;
  match = raw.match(/^\[MEM\] access: (.+?) => store committed$/);
  if (match) return `[\u8bbf\u5b58] ${match[1]}\uff0c\u5199\u5165\u5df2\u63d0\u4ea4`;
  match = raw.match(/^\[MEM\] access: (.+)$/);
  if (match) return `[\u8bbf\u5b58] ${match[1]}`;
  match = raw.match(/^\[WB\] writeback: x(\d+) = (-?\d+)$/);
  if (match) return `[\u56de\u5199] x${match[1]} = ${match[2]}`;
  match = raw.match(/^\[WB\] x(\d+) <- (-?\d+)$/);
  if (match) return `[\u56de\u5199] x${match[1]} = ${match[2]}`;
  match = raw.match(/^\[TRAP\] Interrupt cause=(0x[0-9a-fA-F]+) epc=(0x[0-9a-fA-F]+)$/);
  if (match) return `[Trap] \u4e2d\u65ad cause=${match[1]}\uff0cepc=${match[2]}`;
  match = raw.match(/^\[TRAP\] Exception cause=(0x[0-9a-fA-F]+) epc=(0x[0-9a-fA-F]+)$/);
  if (match) return `[Trap] \u5f02\u5e38 cause=${match[1]}\uff0cepc=${match[2]}`;
  match = raw.match(/^\[TEST\] tohost=(0x[0-9a-fA-F]+) (PASS|FAIL)$/);
  if (match) return `[\u6d4b\u8bd5] tohost=${match[1]}\uff0c\u7ed3\u679c ${match[2] === 'PASS' ? 'PASS' : 'FAIL'}`;

  return raw;
}

function translateModeNote(mode, note, truePipeline) {
  if (!note) {
    return truePipeline
      ? '\u5f53\u524d\u662f\u771f\u6b63\u91cd\u53e0\u6267\u884c\u7684 5 \u7ea7\u6d41\u6c34\u7ebf\uff0c\u53ef\u89c2\u5bdf stall\u3001flush\u3001bubble \u4e0e\u8f6c\u53d1\u3002'
      : '\u5f53\u524d\u662f\u6559\u5b66\u7528\u591a\u5468\u671f\u6267\u884c\u6a21\u578b\uff0c\u9002\u5408\u6309\u9636\u6bb5\u89c2\u5bdf\u6bcf\u6761\u6307\u4ee4\u5982\u4f55\u63a8\u8fdb\u3002';
  }

  const normalized = String(note).trim();
  if (normalized === 'Sequential 5-stage teaching model. Instructions advance stage by stage, but they are not overlapped yet.') {
    return '\u5f53\u524d\u6309 IF / ID / EX / MEM / WB \u4e94\u4e2a\u9636\u6bb5\u987a\u5e8f\u63a8\u8fdb\uff0c\u4f46\u6307\u4ee4\u4e4b\u95f4\u8fd8\u4e0d\u4f1a\u771f\u6b63\u91cd\u53e0\u3002';
  }
  if (normalized === 'True overlapped 5-stage pipeline with forwarding, stalls, flushes, branch prediction, and cache effects.') {
    return '\u5f53\u524d\u662f\u771f\u6b63\u91cd\u53e0\u7684 5 \u7ea7\u6d41\u6c34\u7ebf\uff0c\u652f\u6301\u8f6c\u53d1\u3001stall\u3001flush\u3001\u5206\u652f\u9884\u6d4b\u548c Cache \u5f71\u54cd\u3002';
  }
  return mode === 'PIPELINED'
    ? '\u5f53\u524d\u5904\u4e8e\u6d41\u6c34\u7ebf\u6a21\u5f0f\uff0c\u53ef\u91cd\u70b9\u89c2\u5bdf\u5e76\u53d1\u9636\u6bb5\u534f\u540c\u3002'
    : '\u5f53\u524d\u5904\u4e8e\u591a\u5468\u671f\u6a21\u5f0f\uff0c\u53ef\u91cd\u70b9\u89c2\u5bdf\u5355\u6761\u6307\u4ee4\u9010\u9636\u6bb5\u6267\u884c\u3002';
}

function compactInstruction(text) {
  const cleaned = String(text ?? '').replace(/\s+/g, ' ').trim();
  if (!cleaned) return '\u7a7a';
  return cleaned.length > 22 ? `${cleaned.slice(0, 22)}\u2026` : cleaned;
}

function stageStatusCards(sim) {
  if (sim.truePipeline) {
    return [
      ['取指 IF', sim.stageViews.if.text || '等待', Boolean(sim.stageViews.if.valid)],
      ['译码 ID', sim.stageViews.id.text || '等待', Boolean(sim.stageViews.id.valid)],
      ['执行 EX', sim.stageViews.ex.text || '等待', Boolean(sim.stageViews.ex.valid)],
      ['访存 MEM', sim.stageViews.mem.text || '等待', Boolean(sim.stageViews.mem.valid)],
      ['写回 WB', sim.stageViews.wb.text || '等待', Boolean(sim.stageViews.wb.valid)],
    ];
  }

  return MULTI_CYCLE_STAGE_LABELS.map((label, index) => {
    const key = MULTI_CYCLE_STAGE_KEYS[index];
    const isActive = sim.stageIndex === index;
    const currentText = sim.stageViews[key]?.text || (key === 'if' ? '准备取下一条指令' : '本周期未执行');
    return [label, isActive ? currentText : '本周期未执行', isActive];
  });
}

function bubbleMatchesStage(bubble, stage) {
  if (!bubble?.active || !bubble.stage) return false;
  return String(bubble.stage).toUpperCase().includes(stage);
}

function normalizeLatch(latch) {
  return {
    valid: Boolean(latch?.valid),
    pc: Number(latch?.pc ?? 0) >>> 0,
    text: latch?.text ? String(latch.text) : '',
  };
}

function emptyState() {
  return {
    pc: 0,
    mode: 'MULTI_CYCLE',
    modeName: 'Multi-cycle',
    truePipeline: false,
    modeNote: '',
    halted: true,
    stageIndex: 0,
    registers: Object.fromEntries(REGS.map((name) => [name, 0])),
    memory: {},
    trace: [],
    pipelineLatches: {
      fetch: normalizeLatch(null),
      ifid: normalizeLatch(null),
      idex: normalizeLatch(null),
      exmem: normalizeLatch(null),
      memwb: normalizeLatch(null),
    },
    stageViews: {
      if: normalizeLatch(null),
      id: normalizeLatch(null),
      ex: normalizeLatch(null),
      mem: normalizeLatch(null),
      wb: normalizeLatch(null),
    },
    flowSignals: { stall: false, flush: false, forwarding: [], notes: [] },
    bubble: { active: false, stage: '', reason: '' },
    stats: {
      cycles: 0,
      instructions: 0,
      stalls: 0,
      flushes: 0,
      branchTaken: 0,
      branchNotTaken: 0,
      branchMispredicts: 0,
      cacheHits: 0,
      cacheMisses: 0,
      instructionCacheHits: 0,
      instructionCacheMisses: 0,
      dataCacheHits: 0,
      dataCacheMisses: 0,
    },
    peripherals: { uart: '空', timer: '0', trap: '无', irq: '无' },
    csr: { privilegeMode: 'M', mtvec: 0, mepc: 0, mcause: 0, mtval: 0 },
    mmu: { pagingEnabled: false, mappedPages: 0, pageMappings: [] },
  };
}

function fromBackendState(state) {
  const registers = Object.fromEntries(REGS.map((name) => [name, 0]));
  if (Array.isArray(state.registers)) {
    state.registers.forEach((value, index) => {
      registers[`x${index}`] = Number(value ?? 0) | 0;
    });
  }
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
    mode: String(state.mode ?? 'MULTI_CYCLE'),
    modeName: formatModeName(String(state.mode ?? 'MULTI_CYCLE')),
    truePipeline: Boolean(state.truePipeline),
    modeNote: translateModeNote(String(state.mode ?? 'MULTI_CYCLE'), state.modeNote ?? '', Boolean(state.truePipeline)),
    halted: backendState === 'HALTED',
    stageIndex: Number(state.stageIndex ?? 0),
    registers,
    memory: Object.fromEntries(Object.entries(state.memory ?? {}).map(([k, v]) => [k, Number(v)])),
    trace: Array.isArray(state.trace) ? state.trace : [],
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
      notes: Array.isArray(state.flowSignals?.notes) ? state.flowSignals.notes.map((item) => translateFlowNote(item)) : [],
    },
    bubble: state.bubble ?? { active: false, stage: '', reason: '' },
    stats: {
      cycles: Number(state.stats?.cycles ?? 0),
      instructions: Number(state.stats?.instructions ?? 0),
      stalls: Number(state.stats?.stalls ?? 0),
      flushes: Number(state.stats?.flushes ?? 0),
      branchTaken: Number(state.stats?.branchTaken ?? 0),
      branchNotTaken: Number(state.stats?.branchNotTaken ?? 0),
      branchMispredicts: Number(state.stats?.branchMispredicts ?? 0),
      cacheHits: Number(state.stats?.cacheHits ?? 0),
      cacheMisses: Number(state.stats?.cacheMisses ?? 0),
      instructionCacheHits: Number(state.stats?.instructionCacheHits ?? 0),
      instructionCacheMisses: Number(state.stats?.instructionCacheMisses ?? 0),
      dataCacheHits: Number(state.stats?.dataCacheHits ?? 0),
      dataCacheMisses: Number(state.stats?.dataCacheMisses ?? 0),
    },
    peripherals: {
      uart: state.peripherals?.uart_buffer ? String(state.peripherals.uart_buffer) : '\u7a7a',
      timer: String(state.peripherals?.timer_value ?? 0),
      trap: backendState === 'EXCEPTION' || backendState === 'INTERRUPT'
        ? `mcause=${hex(csr.mcause)} mepc=${hex(csr.mepc)}`
        : '\u65e0',
      irq: state.peripherals?.timer_interrupt
        ? 'Timer IRQ'
        : state.peripherals?.external_interrupt
          ? 'External IRQ'
          : state.peripherals?.software_interrupt
            ? 'Software IRQ'
            : '\u65e0',
    },
    csr,
    mmu: {
      pagingEnabled: Boolean(state.mmu?.paging_enabled),
      mappedPages: Number(state.mmu?.mapped_pages ?? 0),
      pageMappings: Array.isArray(state.mmu?.page_mappings) ? state.mmu.page_mappings : [],
    },
  };
}

function segmentEntries(segment, memory, registers) {
  const values = [];
  Object.entries(memory ?? {}).forEach(([addrText, value]) => {
    const addr = Number(addrText);
    if (addr >= segment.start && addr <= segment.end) {
      values.push({ addr, value: Number(value), touched: true });
    }
  });

  if (segment.key === 'DATA') {
    [0x0100, 0x0104, 0x0108, 0x010c, 0x0110, 0x0120].forEach((addr) => {
      if (!values.some((item) => item.addr === addr)) {
        values.push({ addr, value: 0, touched: false });
      }
    });
  }

  if (segment.key === 'STACK') {
    [alignWord(registers?.x2 ?? 0), alignWord(registers?.x2 ?? 0) + 4].forEach((addr) => {
      if (addr >= segment.start && addr <= segment.end && !values.some((item) => item.addr === addr)) {
        values.push({ addr, value: 0, touched: false });
      }
    });
  }

  return values.sort((a, b) => a.addr - b.addr);
}

function Panel({ title, badge, children }) {
  return (
    <section className="overflow-hidden border-[3px] border-black bg-white">
      <div className="flex items-center justify-between border-b-[3px] border-black bg-[#222] px-4 py-3 text-white">
        <h2 className="text-[0.78rem] font-bold tracking-[0.05em]">{title}</h2>
        {badge ? (
          <div className="border border-black bg-[#ffd43b] px-3 py-1 text-[0.62rem] font-bold text-black">
            {badge}
          </div>
        ) : null}
      </div>
      <div className="p-4">{children}</div>
    </section>
  );
}

function StatCard({ label, value, accent = false }) {
  return (
    <div className={`border-2 border-black p-3 ${accent ? 'bg-[#74c0fc]' : 'bg-white'}`}>
      <div className="text-[0.62rem] uppercase tracking-[0.05em] opacity-70">{label}</div>
      <div className="mt-1 text-[1rem] font-bold break-all">{value}</div>
    </div>
  );
}

function DemoHint({ title, text }) {
  return (
    <div className="border-2 border-black bg-[#f4efea] px-3 py-3 text-[0.74rem]">
      <div className="font-bold">{title}</div>
      <div className="mt-1 leading-6 opacity-85">{text}</div>
    </div>
  );
}

function PipelineTimeline({ timeline, truePipeline }) {
  if (!truePipeline) {
    return (
      <div className="border-2 border-black bg-[#f4efea] px-4 py-4 text-[0.74rem] leading-6">
        多周期模式下不会显示阶梯状流水线图。切到“流水线”模式后，这里会自动展示 IF / ID / EX / MEM / WB 的按周期推进情况。
      </div>
    );
  }

  if (!timeline.length) {
    return (
      <div className="border-2 border-black bg-[#f4efea] px-4 py-4 text-[0.74rem] leading-6">
        先运行、单步阶段或单步指令，让流水线至少推进几个周期，这里就会出现阶梯状时间轴。
      </div>
    );
  }

  return (
    <div className="overflow-x-auto">
      <div className="mb-3 border-2 border-black bg-[#f4efea] px-3 py-2 text-[0.7rem] leading-6">
        横轴是时间周期，纵轴是 IF / ID / EX / MEM / WB。格子里的数字表示当前这条指令的顺序号，遇到 `stall`、`flush`、`bubble` 会直接标在对应周期。
      </div>
      <table className="min-w-full border-collapse text-[0.68rem]">
        <thead>
          <tr>
            <th className="border-2 border-black bg-[#222] px-3 py-2 text-white">阶段</th>
            {timeline.map((column) => (
              <th key={column.cycle} className="border-2 border-black bg-[#ffd43b] px-3 py-2 text-center">
                {column.cycle}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {PIPELINE_STAGES.map((stage) => (
            <tr key={stage}>
              <td className="border-2 border-black bg-[#f4efea] px-3 py-3 font-bold">{stage}</td>
              {timeline.map((column) => {
                const cell = column.cells[stage];
                const bg = STAGE_COLORS[stage];
                return (
                  <td key={`${stage}-${column.cycle}`} className={`h-20 min-w-[110px] border-2 border-black px-2 py-2 align-top ${cell ? bg : 'bg-white'}`}>
                    {cell ? (
                      <div className="leading-5">
                        <div className="font-bold">{cell.indexLabel}</div>
                        <div className="mt-1 break-all">{cell.text}</div>
                        {cell.badges.length ? (
                          <div className="mt-1 text-[0.6rem] font-bold text-[#c92a2a]">{cell.badges.join(' / ')}</div>
                        ) : null}
                      </div>
                    ) : null}
                  </td>
                );
              })}
            </tr>
          ))}
          <tr>
            <td className="border-2 border-black bg-[#f4efea] px-3 py-3 font-bold">事件</td>
            {timeline.map((column) => (
              <td key={`event-${column.cycle}`} className={`h-16 min-w-[110px] border-2 border-black px-2 py-2 align-top ${column.events.length ? STAGE_COLORS.EVENT : 'bg-white'}`}>
                {column.events.length ? <div className="text-[0.62rem] font-bold leading-5">{column.events.join(' / ')}</div> : null}
              </td>
            ))}
          </tr>
        </tbody>
      </table>
    </div>
  );
}

export default function App() {
  const [showGame, setShowGame] = useState(false);
  const [programText, setProgramText] = useState(DEMO);
  const [program, setProgram] = useState([]);
  const [sim, setSim] = useState(emptyState());
  const [connected, setConnected] = useState(false);
  const [isRunning, setIsRunning] = useState(false);
  const [error, setError] = useState('');
  const [modeResults, setModeResults] = useState({});
  const [selectedDemo, setSelectedDemo] = useState('default');
  const [showDetails, setShowDetails] = useState(false);
  const [timeline, setTimeline] = useState([]);

  const refresh = useCallback(async () => {
    const [stateResult, instrResult] = await Promise.all([api.getState(), api.getInstructions()]);
    setSim(fromBackendState(stateResult));
    setProgram(Array.isArray(instrResult.instructions) ? instrResult.instructions : []);
  }, []);

  useEffect(() => {
    const poll = async () => {
      try {
        await refresh();
        setConnected(true);
      } catch {
        setConnected(false);
      }
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
        const next = fromBackendState(await api.stepStage());
        if (cancelled) return;
        setSim(next);
        if (next.halted) {
          setIsRunning(false);
          return;
        }
        timer = window.setTimeout(tick, 220);
      } catch (err) {
        if (!cancelled) {
          setError(`运行失败：${err.message}`);
          setIsRunning(false);
        }
      }
    };

    timer = window.setTimeout(tick, 220);
    return () => {
      cancelled = true;
      if (timer) window.clearTimeout(timer);
    };
  }, [isRunning]);

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
        branchMispredicts: sim.stats.branchMispredicts,
        cacheHits: sim.stats.cacheHits,
        cacheMisses: sim.stats.cacheMisses,
        cpi: sim.stats.instructions > 0 ? (sim.stats.cycles / sim.stats.instructions).toFixed(2) : '0.00',
        ipc: sim.stats.cycles > 0 ? (sim.stats.instructions / sim.stats.cycles).toFixed(2) : '0.00',
      },
    }));
  }, [sim]);

  const currentDemo = useMemo(() => DEMO_PROGRAMS.find((item) => item.key === selectedDemo) || DEMO_PROGRAMS[0], [selectedDemo]);
  const visibleTrace = useMemo(() => sim.trace.slice(-TRACE_VISIBLE_COUNT), [sim.trace]);
  const currentInstruction = useMemo(() => {
    const currentIndex = Math.floor((sim.pc || 0) / 4);
    return program[currentIndex] || null;
  }, [sim.pc, program]);
  const segmentSummaries = useMemo(() => SEGMENTS.map((segment) => ({
    ...segment,
    entries: segmentEntries(segment, sim.memory, sim.registers).slice(0, 4),
    total: segmentEntries(segment, sim.memory, sim.registers).length,
  })), [sim.memory, sim.registers]);

  useEffect(() => {
    if (!sim.truePipeline || sim.stats.cycles <= 0) {
      setTimeline([]);
      return;
    }

    const stageViews = sim.stageViews ?? {};
    const cycle = sim.stats.cycles;
    const nextColumn = {
      cycle,
      cells: {},
      events: [],
    };

    PIPELINE_STAGES.forEach((stage) => {
      const key = stage.toLowerCase();
      const view = stageViews[key];
      if (!view?.valid && !(stage === 'IF' && sim.flowSignals.stall && currentInstruction)) {
        return;
      }

      const text = view?.text || (stage === 'IF' ? currentInstruction?.text : '') || '等待';
      const pc = Number(view?.pc ?? (stage === 'IF' ? sim.pc : 0)) >>> 0;
      const index = Math.floor(pc / 4) + 1;
      const badges = [];
      if (sim.flowSignals.stall && stage === 'IF') badges.push('STALL');
      if (sim.flowSignals.flush && (stage === 'IF' || stage === 'ID')) badges.push('FLUSH');
      if (bubbleMatchesStage(sim.bubble, stage)) badges.push('BUBBLE');

      nextColumn.cells[stage] = {
        indexLabel: Number.isFinite(index) && index > 0 ? String(index) : '-',
        text: compactInstruction(text),
        badges,
      };
    });

    if (sim.flowSignals.forwarding.length) {
      nextColumn.events.push(`旁路: ${sim.flowSignals.forwarding.join(' / ')}`);
    }
    if (sim.flowSignals.notes?.length) {
      nextColumn.events.push(...sim.flowSignals.notes.map((item) => compactInstruction(item)));
    }
    if (sim.bubble?.active) {
      nextColumn.events.push(`气泡: ${sim.bubble.stage || '未知'} / ${sim.bubble.reason || '等待'}`);
    }

    setTimeline((prev) => {
      if (prev.length && prev[prev.length - 1].cycle === cycle) {
        const updated = [...prev];
        updated[updated.length - 1] = nextColumn;
        return updated;
      }
      return [...prev, nextColumn].slice(-18);
    });
  }, [sim, currentInstruction]);

  const assemble = useCallback(async (sourceOverride) => {
    const source = sourceOverride ?? programText;
    setError('');
    setIsRunning(false);
    try {
      await api.assemble(source);
      await refresh();
      setProgramText(source);
    } catch (err) {
      setError(`加载汇编失败：${err.message}`);
    }
  }, [programText, refresh]);

  const loadDemo = useCallback(async (demo) => {
    setSelectedDemo(demo.key);
    setError('');
    setIsRunning(false);

    try {
      if (demo.recommendedMode) {
        setSim(fromBackendState(await api.setMode(demo.recommendedMode)));
        setProgram((await api.getInstructions()).instructions || []);
      }

      if (demo.key === 'custom') {
        setProgramText((prev) => prev.trim() ? prev : demo.source);
        await refresh();
        return;
      }

      await assemble(demo.source);
    } catch (err) {
      setError(`加载演示失败：${err.message}`);
    }
  }, [assemble, refresh]);

  const reset = useCallback(async () => {
    setIsRunning(false);
    try {
      setSim(fromBackendState(await api.reset()));
      setProgram((await api.getInstructions()).instructions || []);
      setError('');
    } catch (err) {
      setError(`重置失败：${err.message}`);
    }
  }, []);

  const switchMode = useCallback(async (mode) => {
    setIsRunning(false);
    try {
      setSim(fromBackendState(await api.setMode(mode)));
      setProgram((await api.getInstructions()).instructions || []);
      setError('');
    } catch (err) {
      setError(`切换模式失败：${err.message}`);
    }
  }, []);

  const stepStage = useCallback(async () => {
    setIsRunning(false);
    try {
      setSim(fromBackendState(await api.stepStage()));
    } catch (err) {
      setError(`单步阶段失败：${err.message}`);
    }
  }, []);

  const stepInstruction = useCallback(async () => {
    setIsRunning(false);
    try {
      setSim(fromBackendState(await api.stepInstruction()));
    } catch (err) {
      setError(`单步指令失败：${err.message}`);
    }
  }, []);

  if (showGame) {
    return <DinoGame onBack={() => setShowGame(false)} />;
  }

  return (
    <div className="min-h-screen bg-[#f4efea] font-mono text-black">
      <div className="mx-auto max-w-7xl px-4 pb-10 pt-8 md:px-8">
        <header className="mb-6 flex flex-col gap-4 lg:flex-row lg:items-end lg:justify-between">
          <div>
            <div className="mb-3 inline-flex items-center gap-2 border border-black bg-[#ffd43b] px-3 py-1 text-[0.68rem] font-bold tracking-[0.05em]">
              <span className={`inline-block h-2 w-2 rounded-full ${connected ? 'bg-green-500' : 'bg-red-500'}`} />
              {connected ? '后端已连接' : '后端未连接'}
            </div>
            <h1 className="text-[2.1rem] font-bold leading-[1.05] md:text-[3rem]">myCPU 可视化答辩展示台</h1>
            <p className="mt-3 max-w-4xl text-[0.9rem] leading-7 opacity-90">
              这一版聚焦可演示、可讲解、可验证。首屏只展示核心执行状态，深入信息放到下方观察区。
            </p>
            <p className="mt-2 max-w-4xl text-[0.78rem] leading-6 opacity-80">
              点击某个演示项时会自动切到推荐模式；“自定义程序”不会强制改模式，方便你按用途自由切换。
            </p>
            <p className="mt-2 max-w-4xl text-[0.78rem] leading-6 opacity-80">
              点击普通示例会自动加载并重新汇编；只有“自定义程序”需要你修改后手动点一次“重新汇编”。
            </p>
          </div>
          <div className="flex flex-wrap gap-3">
            <button onClick={() => setShowGame(true)} className="border-2 border-black bg-[#74c0fc] px-5 py-3 text-[0.78rem] font-semibold">小恐龙游戏</button>
            <button onClick={() => switchMode('MULTI_CYCLE')} className={`border-2 border-black px-5 py-3 text-[0.78rem] font-semibold ${sim.mode === 'MULTI_CYCLE' ? 'bg-[#ffd43b]' : 'bg-white'}`}>多周期</button>
            <button onClick={() => switchMode('PIPELINED')} className={`border-2 border-black px-5 py-3 text-[0.78rem] font-semibold ${sim.mode === 'PIPELINED' ? 'bg-[#ffd43b]' : 'bg-white'}`}>流水线</button>
          </div>
        </header>

        {error ? <div className="mb-6 border-[3px] border-black bg-[#ffe3e3] px-4 py-3 text-[0.8rem] font-semibold">{error}</div> : null}

        <div className="mb-6 grid gap-6 xl:grid-cols-[1.1fr_0.9fr]">
          <Panel title="演示选择" badge={currentDemo.label}>
            <div className="grid gap-2 md:grid-cols-3 xl:grid-cols-2">
              {DEMO_PROGRAMS.map((demo) => (
                <button key={demo.key} onClick={() => loadDemo(demo)} className={`border-2 border-black px-3 py-3 text-left text-[0.74rem] ${selectedDemo === demo.key ? 'bg-[#74c0fc]' : 'bg-white'}`}>
                  <div className="flex items-start justify-between gap-3">
                    <div className="font-bold">{demo.label}</div>
                    <div className="shrink-0 border border-black bg-[#f4efea] px-2 py-1 text-[0.6rem] font-bold">
                      推荐：{demo.recommendedModeLabel}
                    </div>
                  </div>
                  <div className="mt-1 opacity-80">{demo.note}</div>
                </button>
              ))}
            </div>
            <div className="mt-4 grid gap-3 md:grid-cols-2">
              <DemoHint title="这段程序在做什么" text={currentDemo.note} />
              <DemoHint title="演示时看哪里" text={currentDemo.guide} />
            </div>
            <textarea value={programText} onChange={(event) => setProgramText(event.target.value)} className="mt-4 h-44 w-full resize-none border-2 border-black bg-[#fffdf8] p-3 text-[0.74rem] outline-none" />
            <div className="mt-4 flex flex-wrap gap-3">
              <button onClick={() => assemble()} className="border-2 border-black bg-[#ffd43b] px-4 py-2 text-[0.74rem] font-bold">重新汇编</button>
              <button onClick={reset} className="border-2 border-black bg-white px-4 py-2 text-[0.74rem] font-bold">重置</button>
              <button onClick={stepStage} className="border-2 border-black bg-white px-4 py-2 text-[0.74rem] font-bold">单步阶段</button>
              <button onClick={stepInstruction} className="border-2 border-black bg-white px-4 py-2 text-[0.74rem] font-bold">单步指令</button>
              <button onClick={() => setIsRunning((value) => !value)} className="border-2 border-black bg-[#c3fae8] px-4 py-2 text-[0.74rem] font-bold">{isRunning ? '停止运行' : '连续运行'}</button>
            </div>
          </Panel>

          <Panel title="当前执行态" badge={sim.modeName}>
            <div className="grid gap-3 md:grid-cols-2 xl:grid-cols-3">
              <StatCard label="PC" value={hex(sim.pc)} accent />
              <StatCard label="周期" value={sim.stats.cycles} />
              <StatCard label="指令数" value={sim.stats.instructions} />
              <StatCard label="Stall" value={sim.stats.stalls} />
              <StatCard label="Flush" value={sim.stats.flushes} />
              <StatCard label="分支预测失败" value={sim.stats.branchMispredicts} />
            </div>
            <div className="mt-4 grid gap-3 md:grid-cols-2 xl:grid-cols-4">
              <StatCard label="Cache 命中" value={sim.stats.cacheHits} />
              <StatCard label="Cache 未命中" value={sim.stats.cacheMisses} />
              <StatCard label="I-Cache" value={`${sim.stats.instructionCacheHits} / ${sim.stats.instructionCacheMisses}`} />
              <StatCard label="D-Cache" value={`${sim.stats.dataCacheHits} / ${sim.stats.dataCacheMisses}`} />
            </div>
            <div className="mt-4 border-2 border-black bg-[#f4efea] px-3 py-3 text-[0.74rem] leading-6">
              <div><span className="font-bold">当前模式：</span>{sim.modeName}</div>
              <div><span className="font-bold">模式说明：</span>{sim.modeNote || '暂无补充说明。'}</div>
              <div><span className="font-bold">当前指令：</span>{currentInstruction?.text || '暂无'}</div>
              <div><span className="font-bold">流水线提示：</span>{sim.flowSignals.notes?.join(' / ') || '当前无附加提示'}</div>
            </div>
          </Panel>
        </div>

        <div className="mb-6">
          <Panel title="流水线时间轴" badge={sim.truePipeline ? '按周期展开' : '切换到流水线后自动显示'}>
            <PipelineTimeline timeline={timeline} truePipeline={sim.truePipeline} />
          </Panel>
        </div>

        <div className="mb-6 grid gap-6 xl:grid-cols-[1.05fr_0.95fr]">
          <Panel title={sim.truePipeline ? '\u6d41\u6c34\u7ebf\u6458\u8981' : '\u9636\u6bb5\u6458\u8981\uff08\u591a\u5468\u671f\uff09'} badge={sim.truePipeline ? '5 \u4e2a\u9636\u6bb5\u5e76\u884c\u89c2\u5bdf' : '\u5f53\u524d\u9636\u6bb5\u63a8\u8fdb\u5230\u54ea\u4e00\u6b65'}>
            <div className="grid gap-3 md:grid-cols-2 xl:grid-cols-5">
              {stageStatusCards(sim).map(([title, body, active]) => (
                <div key={title} className={`border-2 border-black p-3 text-[0.72rem] ${active ? 'bg-[#ffd43b]' : 'bg-white'}`}>
                  <div className="font-bold">{title}</div>
                  <div className="mt-1 text-[0.62rem] font-bold opacity-75">{active ? (sim.truePipeline ? '\u5f53\u524d\u6709\u6307\u4ee4\u5728\u8be5\u9636\u6bb5' : '\u5f53\u524d\u6d3b\u8dc3\u9636\u6bb5') : (sim.truePipeline ? '\u5f53\u524d\u4e3a\u7a7a' : '\u672c\u5468\u671f\u672a\u6267\u884c')}</div>
                  <div className="mt-2 min-h-[3rem] leading-6">{body}</div>
                </div>
              ))}
            </div>
            <div className="mt-4 border-2 border-black bg-white p-3 text-[0.72rem] leading-6">
              {sim.truePipeline
                ? '\u6d41\u6c34\u7ebf\u6a21\u5f0f\u4e0b\uff0c\u8fd9 5 \u4e2a\u9636\u6bb5\u4f1a\u540c\u65f6\u663e\u793a\u5f53\u524d\u5404\u81ea\u6b63\u5728\u5904\u7406\u7684\u6307\u4ee4\u3002'
                : `\u591a\u5468\u671f\u6a21\u5f0f\u4e0b\uff0c\u540c\u4e00\u65f6\u523b\u53ea\u4f1a\u63a8\u8fdb\u4e00\u4e2a\u9636\u6bb5\uff1b\u5f53\u524d\u9636\u6bb5\u662f ${MULTI_CYCLE_STAGE_LABELS[sim.stageIndex] || '\u53d6\u6307 IF'}\u3002`}
            </div>
            <div className="mt-4 grid gap-3 md:grid-cols-4 text-[0.72rem]">
              <div className={`border-2 border-black p-3 ${sim.flowSignals.stall ? 'bg-[#ffd43b]' : 'bg-white'}`}>Stall: {sim.flowSignals.stall ? '开启' : '关闭'}</div>
              <div className={`border-2 border-black p-3 ${sim.flowSignals.flush ? 'bg-[#ffd43b]' : 'bg-white'}`}>Flush: {sim.flowSignals.flush ? '开启' : '关闭'}</div>
              <div className={`border-2 border-black p-3 ${sim.flowSignals.forwarding.length ? 'bg-[#74c0fc]' : 'bg-white'}`}>{sim.flowSignals.forwarding.length ? sim.flowSignals.forwarding.join(' / ') : '当前无旁路'}</div>
              <div className={`border-2 border-black p-3 ${sim.bubble.active ? 'bg-[#ffd43b]' : 'bg-white'}`}>{sim.bubble.active ? `${sim.bubble.stage} / ${sim.bubble.reason}` : '当前没有气泡'}</div>
            </div>
          </Panel>

          <Panel title="外设 / Trap / CSR" badge="答辩重点">
            <div className="grid gap-3 md:grid-cols-2 text-[0.74rem]">
              <StatCard label="UART" value={sim.peripherals.uart || '空'} />
              <StatCard label="IRQ" value={sim.peripherals.irq} />
              <StatCard label="Trap" value={sim.peripherals.trap} />
              <StatCard label="Paging" value={sim.mmu.pagingEnabled ? 'ON' : 'OFF'} />
            </div>
            <div className="mt-4 grid gap-3 md:grid-cols-2 text-[0.72rem]">
              <div className="border-2 border-black bg-white p-3">
                <div className="font-bold">Trap CSR</div>
                <div className="mt-2 space-y-1">
                  <div>mtvec: <span className="font-semibold">{hex(sim.csr.mtvec)}</span></div>
                  <div>mepc: <span className="font-semibold">{hex(sim.csr.mepc)}</span></div>
                  <div>mcause: <span className="font-semibold">{hex(sim.csr.mcause)}</span></div>
                  <div>mtval: <span className="font-semibold">{hex(sim.csr.mtval)}</span></div>
                </div>
              </div>
              <div className="border-2 border-black bg-white p-3">
                <div className="font-bold">MMU 摘要</div>
                <div className="mt-2 space-y-1">
                  <div>特权级: <span className="font-semibold">{sim.csr.privilegeMode}</span></div>
                  <div>映射页数: <span className="font-semibold">{sim.mmu.mappedPages}</span></div>
                  <div>页表展示:</div>
                  <div className="space-y-1 text-[0.68rem] opacity-85">
                    {sim.mmu.pageMappings.length ? sim.mmu.pageMappings.slice(0, 3).map((item) => <div key={item}>{item}</div>) : <div>当前没有可展示的页表映射。</div>}
                  </div>
                </div>
              </div>
            </div>
          </Panel>
        </div>

        <div className="mb-6 grid gap-6 xl:grid-cols-[0.95fr_1.05fr]">
          <Panel title="分段内存" badge="压缩摘要视图">
            <div className="mb-3 border-2 border-black bg-[#f4efea] px-3 py-2 text-[0.72rem] leading-6">这里只展示 TEXT / DATA / STACK 的摘要和少量关键地址，避免占满整页。完整细节放到下方深入观察区。</div>
            <div className="grid gap-3 lg:grid-cols-3">
              {segmentSummaries.map((segment) => (
                <div key={segment.key} className="border-2 border-black bg-white p-3 text-[0.72rem]">
                  <div className="flex items-center justify-between gap-3">
                    <div className="font-bold">{segment.key}</div>
                    <div className="text-[0.62rem] opacity-70">{hex(segment.start, 4)} - {hex(segment.end, 4)}</div>
                  </div>
                  <div className="mt-2 text-[0.68rem] opacity-80">关键观测地址: {segment.total}</div>
                  <div className="mt-3 space-y-2">
                    {segment.entries.length ? segment.entries.map((entry) => (
                      <div key={entry.addr} className="border border-black bg-[#f4efea] px-2 py-2">
                        <div className="font-semibold">{hex(entry.addr, 4)}</div>
                        <div className="mt-1 text-[0.66rem]">{hex(entry.value)} / {signed(entry.value)}</div>
                      </div>
                    )) : <div className="border border-black bg-[#f4efea] px-2 py-2 opacity-60">当前无内容</div>}
                  </div>
                </div>
              ))}
            </div>
          </Panel>

          <Panel title="模式对比" badge="工程完成态">
            <div className="grid gap-3 md:grid-cols-2">
              {['MULTI_CYCLE', 'PIPELINED'].map((modeKey) => {
                const result = modeResults[modeKey];
                const label = modeKey === 'PIPELINED' ? '流水线' : '多周期';
                return (
                  <div key={modeKey} className="border-2 border-black bg-white p-4 text-[0.74rem]">
                    <div className="flex items-center justify-between">
                      <div className="font-bold">{label}</div>
                      <div className="text-[0.66rem] opacity-70">{result ? `${result.instructions} 条指令` : '暂无数据'}</div>
                    </div>
                    {result ? (
                        <div className="mt-3 grid gap-2">
                        <div>周期: <span className="font-semibold">{result.cycles}</span></div>
                        <div>IPC: <span className="font-semibold">{result.ipc}</span></div>
                        <div>CPI: <span className="font-semibold">{result.cpi}</span></div>
                        <div>Stall / Flush: <span className="font-semibold">{result.stalls} / {result.flushes}</span></div>
                        <div>分支预测失败: <span className="font-semibold">{result.branchMispredicts}</span></div>
                        <div>Cache 命中 / 未命中: <span className="font-semibold">{result.cacheHits} / {result.cacheMisses}</span></div>
                      </div>
                    ) : <div className="mt-3 opacity-60">请运行或单步后记录结果。</div>}
                  </div>
                );
              })}
            </div>
            <div className="mt-4 border-2 border-black bg-[#f4efea] px-3 py-3 text-[0.74rem] leading-6">
              当前程序由后端 Simulator 执行，前端只负责装载、单步、取状态和可视化。已实现 RV32I、RV32M、Cache、分支预测、MMU / Trap / UART 展示；RV32V / SIMD 当前未实现。
            </div>
            <div className="mt-4 grid gap-3 md:grid-cols-2">
              <DemoHint title="RV32I vs RV32IM" text="建议先分别加载“矩阵乘法 RV32I”和“矩阵乘法 RV32IM”，它们都是标量实现，可运行后比较周期、IPC 和 stall。" />
              <DemoHint title="HTTP vs WebSocket" text="在小恐龙页中观察“传输模式”“本帧指令数”“每帧上限”，断开 WebSocket 时应自动回退 HTTP。" />
            </div>
          </Panel>
        </div>

        <div className="space-y-6">
          <div className="flex items-center justify-between">
            <h2 className="text-[1rem] font-bold tracking-[0.05em]">深入观察区</h2>
            <button onClick={() => setShowDetails((value) => !value)} className="border-2 border-black bg-white px-4 py-2 text-[0.74rem] font-bold">{showDetails ? '收起' : '展开'}</button>
          </div>

          {showDetails ? (
            <div className="grid gap-6 xl:grid-cols-[1.05fr_0.95fr]">
              <Panel title="程序视图" badge={`${program.length} 条指令`}>
                <div className="max-h-[420px] space-y-2 overflow-auto pr-1">
                  {program.length ? program.map((instruction, index) => {
                    const active = (instruction.address ?? index * 4) === sim.pc;
                    return (
                      <div key={`${instruction.address}-${index}`} className={`border-2 border-black px-3 py-3 text-[0.74rem] ${active ? 'bg-[#74c0fc]' : 'bg-white'}`}>
                        <div className="flex items-center gap-3">
                          <span className="w-20 shrink-0 font-bold opacity-75">{hex(instruction.address ?? index * 4, 4)}</span>
                          <span className="min-w-0 flex-1 break-all">{instruction.text}</span>
                        </div>
                      </div>
                    );
                  }) : <div className="border-2 border-black bg-white px-3 py-3 text-[0.74rem]">当前没有指令。</div>}
                </div>
              </Panel>

              <Panel title="执行轨迹" badge={`最近 ${TRACE_VISIBLE_COUNT} 条可视`}>
                <div className="max-h-[420px] space-y-2 overflow-auto pr-1 text-[0.74rem] leading-6">
                  {visibleTrace.length ? visibleTrace.map((entry, index) => <div key={`${entry}-${index}`} className={`border-2 border-black px-3 py-3 break-all ${index === visibleTrace.length - 1 ? 'bg-[#ffd43b]' : 'bg-white'}`}>{formatTraceEntry(entry)}</div>) : <div className="border-2 border-black bg-white px-3 py-3">暂无执行轨迹</div>}
                </div>
              </Panel>

              <Panel title="寄存器摘要" badge="精选寄存器">
                <div className="grid gap-2 md:grid-cols-2 xl:grid-cols-3">
                  {[0, 1, 2, 5, 10, 11, 17, 28].map((index) => (
                    <div key={index} className="border-2 border-black bg-white px-3 py-3 text-[0.72rem]">
                      <div className="font-bold">{`x${index}`}</div>
                      <div className="mt-2">HEX: <span className="font-semibold">{hex(sim.registers[`x${index}`])}</span></div>
                      <div className="mt-1">DEC: <span className="font-semibold">{signed(sim.registers[`x${index}`])}</span></div>
                    </div>
                  ))}
                </div>
              </Panel>

              <Panel title="页表详情" badge="MMU 深入信息">
                <div className="space-y-2 text-[0.72rem]">
                  {sim.mmu.pageMappings.length ? sim.mmu.pageMappings.map((mapping) => <div key={mapping} className="border-2 border-black bg-white px-3 py-3">{mapping}</div>) : <div className="border-2 border-black bg-white px-3 py-3">当前没有页表映射。</div>}
                </div>
              </Panel>
            </div>
          ) : null}
        </div>
      </div>
    </div>
  );
}
