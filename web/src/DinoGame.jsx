import { useCallback, useEffect, useRef, useState } from 'react';

const API = import.meta.env.VITE_API_BASE_URL || 'http://localhost:18080';
const WS_URL = API.replace(/^http/, 'ws') + '/ws';

const initialGameState = {
  dino_y: 0,
  dino_vy: 0,
  score: 0,
  executed_instructions: 0,
  max_instructions_per_step: 1000,
  game_over: false,
  obstacles: [],
};

async function fetchJson(path, options) {
  const res = await fetch(`${API}${path}`, options);
  const data = await res.json();
  if (!res.ok || data.error) {
    throw new Error(data.error || `HTTP ${res.status}`);
  }
  return data;
}

function normalizeState(state) {
  return {
    ...initialGameState,
    ...state,
    obstacles: Array.isArray(state?.obstacles) ? state.obstacles : [],
  };
}

class GameTransport {
  constructor() {
    this.mode = 'http';
    this.ws = null;
    this.pending = null;
    this.pendingReject = null;
  }

  async connectWebSocket() {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.mode = 'websocket';
      return;
    }

    await new Promise((resolve, reject) => {
      const ws = new WebSocket(WS_URL);
      const cleanup = () => {
        ws.onopen = null;
        ws.onerror = null;
      };
      ws.onopen = () => {
        cleanup();
        this.ws = ws;
        this.mode = 'websocket';
        resolve();
      };
      ws.onerror = () => {
        cleanup();
        reject(new Error('WebSocket connection failed'));
      };
      ws.onmessage = (event) => {
        if (!this.pending) {
          return;
        }
        try {
          const payload = JSON.parse(event.data);
          if (payload?.error) {
            this.pendingReject?.(new Error(payload.error));
          } else {
            this.pending(payload);
          }
        } catch (error) {
          this.pendingReject?.(error);
        } finally {
          this.pending = null;
          this.pendingReject = null;
        }
      };
      ws.onclose = () => {
        this.ws = null;
        this.mode = 'http';
      };
    });
  }

  async send(type, payload = {}) {
    if (this.mode === 'websocket' && this.ws?.readyState === WebSocket.OPEN) {
      return new Promise((resolve, reject) => {
        this.pending = resolve;
        this.pendingReject = reject;
        this.ws.send(JSON.stringify({ type, ...payload }));
      });
    }

    if (type === 'game_init') {
      return fetchJson('/game/init', { method: 'POST' });
    }
    if (type === 'game_step') {
      return fetchJson('/game/step', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ jump: Boolean(payload.jump) }),
      });
    }
    return fetchJson('/game/get_state');
  }

  close() {
    if (this.ws) {
      this.ws.close();
    }
    this.ws = null;
    this.mode = 'http';
  }
}

export default function DinoGame({ onBack }) {
  const canvasRef = useRef(null);
  const transportRef = useRef(new GameTransport());
  const jumpRequestedRef = useRef(false);
  const timerRef = useRef(null);

  const [gameState, setGameState] = useState(initialGameState);
  const [gameStarted, setGameStarted] = useState(false);
  const [isRunning, setIsRunning] = useState(false);
  const [isStarting, setIsStarting] = useState(false);
  const [errorMessage, setErrorMessage] = useState('');
  const [transportMode, setTransportMode] = useState('HTTP');

  const stopLoop = useCallback(() => {
    if (timerRef.current) {
      clearTimeout(timerRef.current);
      timerRef.current = null;
    }
  }, []);

  const sendStep = useCallback(async (jump) => {
    const data = await transportRef.current.send('game_step', { jump });
    setTransportMode(transportRef.current.mode === 'websocket' ? 'WebSocket' : 'HTTP');
    return normalizeState(data);
  }, []);

  const bootGame = useCallback(async () => {
    setIsStarting(true);
    setErrorMessage('');
    stopLoop();

    try {
      try {
        await transportRef.current.connectWebSocket();
      } catch {
        transportRef.current.mode = 'http';
      }

      await transportRef.current.send('game_init');
      const state = await sendStep(false);
      setGameState(state);
      setGameStarted(true);
      setIsRunning(true);
    } catch (error) {
      setGameStarted(false);
      setIsRunning(false);
      setGameState(initialGameState);
      setErrorMessage(error.message || '游戏启动失败');
    } finally {
      setTransportMode(transportRef.current.mode === 'websocket' ? 'WebSocket' : 'HTTP');
      setIsStarting(false);
    }
  }, [sendStep, stopLoop]);

  const initGame = useCallback(async () => {
    await bootGame();
  }, [bootGame]);

  const gameLoop = useCallback(async () => {
    if (!isRunning) {
      return;
    }

    try {
      const jump = jumpRequestedRef.current;
      jumpRequestedRef.current = false;
      const nextState = await sendStep(jump);
      setGameState(nextState);
      if (!nextState.game_over) {
        timerRef.current = setTimeout(gameLoop, 35);
      } else {
        setIsRunning(false);
      }
    } catch (error) {
      transportRef.current.mode = 'http';
      setTransportMode('HTTP');
      setIsRunning(false);
      setErrorMessage(error.message || '游戏循环失败');
    }
  }, [isRunning, sendStep]);

  useEffect(() => {
    if (!isRunning) {
      return undefined;
    }
    timerRef.current = setTimeout(gameLoop, 35);
    return () => stopLoop();
  }, [isRunning, gameLoop, stopLoop]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) {
      return;
    }

    const ctx = canvas.getContext('2d');
    const width = canvas.width;
    const height = canvas.height;

    ctx.fillStyle = '#f7f7f7';
    ctx.fillRect(0, 0, width, height);

    ctx.strokeStyle = '#535353';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(0, height - 50);
    ctx.lineTo(width, height - 50);
    ctx.stroke();

    const dinoX = 100;
    const dinoY = height - 50 - 40 - gameState.dino_y;
    ctx.fillStyle = '#535353';
    ctx.fillRect(dinoX, dinoY + 15, 30, 25);
    ctx.fillRect(dinoX + 10, dinoY, 20, 20);
    ctx.fillRect(dinoX + 25, dinoY + 5, 8, 8);
    ctx.fillRect(dinoX + 5, dinoY + 38, 10, 10);
    ctx.fillRect(dinoX + 20, dinoY + 38, 10, 10);

    ctx.fillStyle = '#ffffff';
    ctx.fillRect(dinoX + 22, dinoY + 8, 4, 4);
    ctx.fillStyle = '#000000';
    ctx.fillRect(dinoX + 24, dinoY + 10, 2, 2);

    ctx.fillStyle = '#228B22';
    gameState.obstacles.forEach((obs) => {
      const obsX = Number(obs.x ?? 0);
      const obsY = height - 50 - 40;
      ctx.fillRect(obsX + 8, obsY + 10, 14, 30);
      ctx.fillRect(obsX, obsY + 15, 10, 8);
      ctx.fillRect(obsX, obsY + 5, 8, 18);
      ctx.fillRect(obsX + 20, obsY + 20, 10, 8);
      ctx.fillRect(obsX + 22, obsY + 10, 8, 18);
    });

    ctx.fillStyle = '#535353';
    ctx.font = 'bold 24px monospace';
    ctx.fillText(`Score: ${gameState.score}`, 20, 40);

    if (gameState.game_over) {
      ctx.fillStyle = 'rgba(0, 0, 0, 0.5)';
      ctx.fillRect(0, 0, width, height);
      ctx.fillStyle = '#ff6b6b';
      ctx.font = 'bold 48px monospace';
      ctx.textAlign = 'center';
      ctx.fillText('GAME OVER', width / 2, height / 2 - 30);
      ctx.fillStyle = '#ffffff';
      ctx.font = 'bold 24px monospace';
      ctx.fillText(`Final Score: ${gameState.score}`, width / 2, height / 2 + 20);
      ctx.textAlign = 'left';
    }
  }, [gameState]);

  useEffect(() => {
    const onKeyDown = (event) => {
      if (event.code !== 'Space') {
        return;
      }
      event.preventDefault();
      if (!gameStarted) {
        bootGame();
      } else if (gameState.game_over) {
        initGame();
      } else {
        jumpRequestedRef.current = true;
      }
    };

    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [bootGame, gameStarted, gameState.game_over, initGame]);

  useEffect(() => () => {
    stopLoop();
    transportRef.current.close();
  }, [stopLoop]);

  return (
    <div className="min-h-screen bg-[#f4efea] font-mono text-black flex flex-col items-center justify-center p-4">
      <div className="mb-6">
        <button onClick={onBack} className="border-2 border-black bg-white px-6 py-3 text-[0.78rem] font-semibold hover:bg-[#ffd43b]">
          ← 返回主界面
        </button>
      </div>

      <div className="mb-4 text-center">
        <h2 className="text-[2rem] font-bold mb-2">小恐龙跑酷</h2>
        <p className="text-[0.9rem] opacity-80">按空格键跳跃，避开障碍物。</p>
        <p className="mt-2 max-w-[40rem] text-[0.78rem] leading-6 opacity-75">游戏逻辑由虚拟 CPU 执行，前端只负责输入、传输和画面渲染。</p>
        <p className="max-w-[40rem] text-[0.78rem] leading-6 opacity-75">为保证状态稳定，游戏初始化时会固定切回多周期模式，不继承主站当前的 CPU 模式。</p>
      </div>

      {errorMessage ? <div className="mb-4 max-w-[32rem] border-2 border-black bg-[#ffe3e3] px-4 py-3 text-[0.78rem] font-semibold text-black">{errorMessage}</div> : null}

      {gameStarted ? (
        <div className="mb-4 flex flex-wrap justify-center gap-2 text-[0.76rem]">
          <div className="border-2 border-black bg-white px-3 py-2">传输模式: {transportMode}</div>
          <div className="border-2 border-black bg-white px-3 py-2">分数: {gameState.score}</div>
          <div className="border-2 border-black bg-white px-3 py-2">本帧指令: {gameState.executed_instructions || 0}</div>
          <div className="border-2 border-black bg-white px-3 py-2">每帧上限: {gameState.max_instructions_per_step || 0}</div>
          <div className="border-2 border-black bg-white px-3 py-2">障碍物: {gameState.obstacles.length}</div>
        </div>
      ) : null}

      {!gameStarted ? (
        <div className="flex flex-col items-center gap-4">
          <button onClick={bootGame} disabled={isStarting} className="border-2 border-black bg-[#74c0fc] px-8 py-4 text-[1.2rem] font-bold hover:bg-[#ffd43b] disabled:cursor-not-allowed disabled:bg-[#ced4da]">
            {isStarting ? '正在启动...' : '开始游戏'}
          </button>
          <p className="text-[0.8rem] opacity-70">{isStarting ? '正在连接后端并初始化游戏...' : '点击按钮开始，或按空格键启动'}</p>
        </div>
      ) : (
        <>
          <canvas
            ref={canvasRef}
            width={800}
            height={300}
            className="border-[3px] border-black bg-white cursor-pointer"
            onClick={() => {
              if (gameState.game_over) {
                initGame();
              } else {
                jumpRequestedRef.current = true;
              }
            }}
          />
          <div className="mt-6 text-center">
            <p className="text-[0.8rem] opacity-70">{gameState.game_over ? '按空格键或点击画布重新开始' : '点击画布也可以跳跃'}</p>
          </div>
        </>
      )}
    </div>
  );
}
