// workmode.js — 工作模式（透传桥）配置页
//
// One status card (auto-refresh) + a mode selector + a per-mode params form.
// Submitting POSTs { mode, <selected>: {...} } so the backend only restarts
// the bridge with the relevant params block.

import { api } from '/modules/api.js';
import { el, field, toast, readForm } from '/modules/ui.js';

const MODE_LABELS = {
  off:        '关闭',
  tcp_client: 'TCP 客户端',
  tcp_server: 'TCP 服务器',
  udp:        'UDP',
  mqtt:       'MQTT',
  http:       'HTTP 客户端',
};

const STATE_LABELS = {
  stopped:      '未启动',
  starting:     '启动中',
  connected:    '已连接',
  disconnected: '未连接',
  error:        '错误',
};

const STATE_TONES = {
  stopped:      'idle',
  starting:     'warn',
  connected:    'ok',
  disconnected: 'warn',
  error:        'err',
};

function fmtBytes(n) {
  n = Number(n) || 0;
  if (n >= 1024 * 1024) return (n / (1024 * 1024)).toFixed(2) + ' MB';
  if (n >= 1024)        return (n / 1024).toFixed(1) + ' KB';
  return n + ' B';
}

/* ── status card ─────────────────────────────────────────────────────── */

function statusCard(s) {
  const stateKey = s.state || 'stopped';
  const tone     = STATE_TONES[stateKey] || 'idle';
  const showMetrics = s.mode && s.mode !== 'off';

  const metrics = el('div', { class: 'wm-metrics' }, [
    el('div', { class: 'wm-metric' }, [
      el('span', { class: 'wm-metric__label' }, '上行  TX'),
      el('span', { class: 'wm-metric__value' }, fmtBytes(s.tx_bytes)),
      el('span', { class: 'wm-metric__sub'   }, `${s.tx_packets || 0} 包`),
    ]),
    el('div', { class: 'wm-metric' }, [
      el('span', { class: 'wm-metric__label' }, '下行  RX'),
      el('span', { class: 'wm-metric__value' }, fmtBytes(s.rx_bytes)),
      el('span', { class: 'wm-metric__sub'   }, `${s.rx_packets || 0} 包`),
    ]),
  ]);

  return el('div', { class: 'card wm-status' }, [
    el('div', { class: 'wm-status__head' }, [
      el('div', { class: 'wm-status__mode' }, [
        el('span', { class: 'wm-status__mode-label' }, '当前模式'),
        el('strong', {}, MODE_LABELS[s.mode] || '关闭'),
      ]),
      el('span', { class: `state-badge state-badge--${tone}` }, [
        el('span', { class: 'state-badge__dot' }),
        STATE_LABELS[stateKey] || stateKey,
      ]),
    ]),
    showMetrics ? metrics : el('p', { class: 'wm-status__hint' },
      '工作模式当前为「关闭」。选择一种模式并保存后，串口数据将与所选协议双向透传。'),
  ]);
}

/* ── mode selector (segmented control, wraps on mobile) ──────────────── */

function modeSelector(currentMode, onChange) {
  const seg = el('div', { class: 'seg-strip', role: 'tablist' });
  for (const [key, label] of Object.entries(MODE_LABELS)) {
    const btn = el('button', {
      type:        'button',
      role:        'tab',
      class:       'seg-strip__item' + (key === currentMode ? ' is-active' : ''),
      'aria-selected': key === currentMode ? 'true' : 'false',
      'data-mode': key,
    }, label);
    btn.addEventListener('click', () => {
      for (const c of seg.children) c.classList.remove('is-active');
      btn.classList.add('is-active');
      onChange(key);
    });
    seg.append(btn);
  }
  return el('div', { class: 'card' }, [
    el('h2', { class: 'card__title' }, '选择工作模式'),
    el('p',  { class: 'card__desc' },  '保存后立即切换。无需重启设备。'),
    seg,
  ]);
}

/* ── per-mode parameter panels ───────────────────────────────────────── */

function paramsForMode(mode, cfg) {
  if (mode === 'off') {
    return el('div', { class: 'wm-empty' },
      '关闭模式下设备不会建立任何上行连接，串口仍然可在「调试」页中收发。');
  }

  if (mode === 'tcp_client') {
    const c = cfg.tcp_client || {};
    return el('div', { class: 'wm-form' }, [
      field('服务器地址', el('input', { name: 'host', type: 'text', value: c.host || '', placeholder: '例如 192.168.1.100 或 example.com' })),
      field('端口',       el('input', { name: 'port', type: 'number', min: 1, max: 65535, value: c.port || 8080 })),
      field('重连间隔 ms', el('input', { name: 'reconn_ms', type: 'number', min: 200, max: 60000, value: c.reconn_ms || 2000 }),
            '断线后等待此毫秒再重连，建议 1000–5000'),
    ]);
  }

  if (mode === 'tcp_server') {
    const c = cfg.tcp_server || {};
    return el('div', { class: 'wm-form' }, [
      field('监听端口',   el('input', { name: 'port', type: 'number', min: 1, max: 65535, value: c.port || 8080 })),
      field('最大客户端', el('input', { name: 'max_clients', type: 'number', min: 1, max: 16, value: c.max_clients || 4 }),
            '同时接入的 TCP 客户端数量（1–16）'),
    ]);
  }

  if (mode === 'udp') {
    const c = cfg.udp || {};
    return el('div', { class: 'wm-form' }, [
      field('本地端口',     el('input', { name: 'local_port', type: 'number', min: 1, max: 65535, value: c.local_port || 9000 })),
      field('对端地址',     el('input', { name: 'remote_host', type: 'text', value: c.remote_host || '', placeholder: '主动发包的目标 IP / 主机名' })),
      field('对端端口',     el('input', { name: 'remote_port', type: 'number', min: 1, max: 65535, value: c.remote_port || 9000 })),
    ]);
  }

  if (mode === 'mqtt') {
    const c = cfg.mqtt || {};
    return el('div', { class: 'wm-form' }, [
      field('Broker URI', el('input', { name: 'uri', type: 'text', value: c.uri || '', placeholder: 'mqtt://host:1883 或 mqtts://...' })),
      field('Client ID',  el('input', { name: 'client_id', type: 'text', value: c.client_id || '' })),
      field('用户名',     el('input', { name: 'user', type: 'text', value: c.user || '' })),
      field('密码',       el('input', { name: 'password', type: 'password', placeholder: '留空保持不变' })),
      field('发布主题',   el('input', { name: 'pub_topic', type: 'text', value: c.pub_topic || '', placeholder: '串口数据上行到该主题' })),
      field('订阅主题',   el('input', { name: 'sub_topic', type: 'text', value: c.sub_topic || '', placeholder: '该主题数据下行到串口' })),
      field('QoS',
        el('select', { name: 'qos' }, [0, 1, 2].map(v =>
          el('option', { value: v, selected: (c.qos ?? 0) === v }, String(v))))),
    ]);
  }

  if (mode === 'http') {
    const c = cfg.http || {};
    return el('div', { class: 'wm-form' }, [
      field('URL',     el('input', { name: 'url', type: 'text', value: c.url || '', placeholder: 'http://host/path' })),
      field('方法',
        el('select', { name: 'method' },
          ['POST', 'GET'].map(m =>
            el('option', { value: m, selected: (c.method || 'POST') === m }, m)))),
      field('超时 ms', el('input', { name: 'timeout_ms', type: 'number', min: 100, max: 60000, value: c.timeout_ms || 5000 })),
    ]);
  }

  return el('div', { class: 'wm-empty' }, '该模式参数不可配置。');
}

/* ── form submission ─────────────────────────────────────────────────── */

function collectPayload(mode, paramsRoot) {
  const body = { mode };
  if (mode === 'off') return body;

  const raw = readForm(paramsRoot);
  // Strip empty password so backend keeps the existing one.
  if (mode === 'mqtt' && !raw.password) delete raw.password;
  body[mode] = raw;
  return body;
}

async function submit(btn, payload) {
  btn.disabled = true;
  const prev = btn.textContent;
  btn.textContent = '应用中…';
  try {
    await api.setWorkmode(payload);
    toast('已应用', 'success');
    return true;
  } catch (err) {
    toast(err.message, 'error');
    return false;
  } finally {
    btn.disabled = false;
    btn.textContent = prev;
  }
}

/* ── page entry ──────────────────────────────────────────────────────── */

export const workmodePage = {
  async mount(root) {
    let cfg;
    try {
      cfg = await api.getWorkmode();
    } catch (err) {
      root.append(el('div', { class: 'card' }, [`加载失败：${err.message}`]));
      return;
    }

    let selectedMode = cfg.mode || 'off';

    /* status card  */
    const statusHost = el('div');
    statusHost.append(statusCard(cfg.status || { mode: selectedMode, state: 'stopped' }));

    /* params card (dynamic body) */
    const paramsHost = el('div', { class: 'wm-params' });
    const submitBtn  = el('button', { type: 'submit', class: 'btn' }, '保存并应用');
    const paramsForm = el('form', { class: 'card' }, [
      el('h2', { class: 'card__title' }, '参数配置'),
      paramsHost,
      el('div', { class: 'row row--end' }, [submitBtn]),
    ]);

    const renderParams = (mode) => {
      paramsHost.innerHTML = '';
      paramsHost.append(paramsForMode(mode, cfg));
      submitBtn.disabled = false;
    };
    renderParams(selectedMode);

    paramsForm.addEventListener('submit', async (e) => {
      e.preventDefault();
      const payload = collectPayload(selectedMode, paramsHost);
      const ok = await submit(submitBtn, payload);
      if (ok) {
        // refresh from server to reflect normalised values
        try { cfg = await api.getWorkmode(); } catch { /* ignore */ }
        statusHost.innerHTML = '';
        statusHost.append(statusCard(cfg.status || { mode: selectedMode, state: 'stopped' }));
      }
    });

    /* mode selector */
    const selectorCard = modeSelector(selectedMode, (m) => {
      selectedMode = m;
      renderParams(m);
    });

    root.append(statusHost, selectorCard, paramsForm);

    /* live status poll */
    this._timer = setInterval(async () => {
      try {
        const s = await api.getWorkmodeStatus();
        statusHost.innerHTML = '';
        statusHost.append(statusCard(s));
      } catch (err) {
        if (err.code === 401) clearInterval(this._timer);
      }
    }, 2000);
  },

  async unmount() {
    if (this._timer) { clearInterval(this._timer); this._timer = 0; }
  },
};
