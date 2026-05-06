// console.js — "串口调试助手".
//
// Layout:
//   [ toolbar: 清空 / 自动滚动 / 统计 ]
//   [ log area — scroll buffer ]
//   [ input: format switch + textarea + send ]

import { api, SerialSocket } from '/modules/api.js';
import { el, toast }         from '/modules/ui.js';

const MAX_LINES = 500;

function formatTs(ms) {
  const d = new Date(ms);
  const pad = (n, w = 2) => String(n).padStart(w, '0');
  return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}.${pad(d.getMilliseconds(), 3)}`;
}

function renderLine({ dir, ts, fmt, data }) {
  return el('div', { class: `console__line ${dir}` }, [
    el('span', { class: 'console__ts'   }, formatTs(ts)),
    el('span', { class: 'console__dir'  }, dir.toUpperCase()),
    el('span', { class: 'console__data' }, `[${fmt}] ${data}`),
  ]);
}

function makeStatus(stats) {
  return `发送 ${stats.tx} 帧 · 接收 ${stats.rx} 帧`;
}

export const consolePage = {
  mount(root) {
    const state = {
      fmt: 'hex',
      autoscroll: true,
      tx: 0, rx: 0,
    };

    const logArea = el('div', { class: 'console__log', id: 'log' });
    const statusEl = el('span', { class: 'console__stats' }, makeStatus(state));

    const fmtSeg = el('div', { class: 'segmented' }, [
      el('button', { type: 'button', class: 'is-active', dataset: { fmt: 'hex'  } }, 'HEX'),
      el('button', { type: 'button',                    dataset: { fmt: 'text' } }, 'TEXT'),
    ]);
    fmtSeg.addEventListener('click', (e) => {
      const b = e.target.closest('button[data-fmt]');
      if (!b) return;
      state.fmt = b.dataset.fmt;
      for (const x of fmtSeg.children) x.classList.toggle('is-active', x === b);
      input.placeholder = state.fmt === 'hex' ? '例：01 03 00 00 00 0A C5 CD' : '要发送的文本';
    });

    const input = el('textarea', {
      id: 'tx-input',
      placeholder: '例：01 03 00 00 00 0A C5 CD',
      rows: 2,
    });

    const sendBtn  = el('button', { type: 'button', class: 'btn' }, '发送');
    const clearBtn = el('button', { type: 'button', class: 'btn btn--ghost' }, '清空');
    const autoCk   = el('label', { class: 'checkbox' }, [
      el('input', { type: 'checkbox', checked: true }), '自动滚动',
    ]);
    autoCk.querySelector('input').addEventListener('change', (e) => {
      state.autoscroll = e.target.checked;
    });

    const resetBtn = el('button', { type: 'button', class: 'btn btn--ghost' }, '重置计数');
    resetBtn.addEventListener('click', () => {
      state.tx = 0; state.rx = 0;
      statusEl.textContent = makeStatus(state);
    });

    clearBtn.addEventListener('click', () => { logArea.innerHTML = ''; });

    const appendLine = (frame) => {
      const line = renderLine(frame);
      logArea.append(line);
      while (logArea.children.length > MAX_LINES) logArea.firstChild.remove();
      if (state.autoscroll) logArea.scrollTop = logArea.scrollHeight;
    };

    sendBtn.addEventListener('click', async () => {
      const data = input.value.trim();
      if (!data) return;
      try {
        const d = await api.sendSerial(state.fmt, data);
        state.tx += 1;
        statusEl.textContent = makeStatus(state);
        appendLine({ dir: 'tx', ts: Date.now(), fmt: state.fmt, data });
        if (state.fmt === 'text') input.value = '';
      } catch (err) {
        toast('发送失败: ' + err.message, 'error');
      }
    });

    /* Ctrl/⌘-Enter to send. */
    input.addEventListener('keydown', (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
        e.preventDefault();
        sendBtn.click();
      }
    });

    /* WebSocket — rx side. */
    this._ws = new SerialSocket({
      onopen:  () => setConn(true),
      onclose: () => setConn(false),
      onframe: (f) => {
        if (f && f.dir === 'rx') {
          state.rx += 1;
          statusEl.textContent = makeStatus(state);
          appendLine(f);
        }
      },
    });

    const section = el('section', { class: 'console' }, [
      el('div', { class: 'console__toolbar' }, [
        fmtSeg,
        autoCk,
        el('span', { class: 'spacer' }),
        statusEl,
        resetBtn,
        clearBtn,
      ]),
      logArea,
      el('div', { class: 'console__send' }, [
        input,
        el('div', {}, [sendBtn]),
      ]),
      el('p', { class: 'hint' }, '快捷键：Ctrl / ⌘ + Enter 发送。'),
    ]);

    root.append(section);
  },

  unmount() {
    if (this._ws) { this._ws.stop(); this._ws = null; }
  },
};

function setConn(up) {
  const s = document.getElementById('conn-status');
  if (!s) return;
  s.classList.toggle('is-up', up);
  s.classList.toggle('is-down', !up);
  s.querySelector('.status__text').textContent = up ? '已连接' : '断开';
}
