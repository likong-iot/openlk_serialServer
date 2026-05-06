// bridge.js — "桥接" page.

import { api } from '/modules/api.js';
import { el, field, toast, readForm } from '/modules/ui.js';

function statusCard(status) {
  const net = status?.net || {};
  const items = [
    ['网络模式', net.mode || 'wifi'],
    ['链路状态', net.got_ip ? '已联网' : '未联网'],
    ['当前 IP',  net.ip || '—'],
    ['网关',     net.gateway || '—'],
  ];
  return el('div', { class: 'card' }, [
    el('h2', { class: 'card__title' }, '桥接运行条件'),
    el('dl', { class: 'kv' },
      items.flatMap(([k, v]) => [
        el('dt', {}, k),
        el('dd', {}, String(v)),
      ])),
  ]);
}

function bridgeFormCard(state) {
  const form = el('form', { id: 'form-bridge' }, [
    field('TCP 主机',
      el('input', {
        name: 'host',
        type: 'text',
        required: 'required',
        maxlength: 63,
        value: state.host || '',
        placeholder: '例如 192.168.1.100 或 example.com',
      }),
      '串口数据将发往该地址'),

    field('TCP 端口',
      el('input', {
        name: 'port',
        type: 'number',
        min: 1,
        max: 65535,
        required: 'required',
        value: state.port ?? 9000,
      })),

    field('重连间隔 (ms)',
      el('input', {
        name: 'reconnect_ms',
        type: 'number',
        min: 200,
        max: 60000,
        required: 'required',
        value: state.reconnect_ms ?? 2000,
      }),
      '建议 1000~5000ms'),

    el('div', { class: 'row row--end' }, [
      el('button', { type: 'button', class: 'btn btn--ghost', id: 'bridge-reboot' }, '重启设备'),
      el('span', { class: 'spacer' }),
      el('button', { type: 'submit', class: 'btn' }, '保存'),
    ]),
  ]);

  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const btn = form.querySelector('button[type="submit"]');
    btn.disabled = true;
    const prev = btn.textContent;
    btn.textContent = '保存中…';
    try {
      const payload = readForm(form);
      payload.host = String(payload.host || '').trim();
      await api.setBridge(payload);
      toast('桥接参数已保存，请重启设备后生效', 'success');
    } catch (err) {
      toast(err.message, 'error');
    } finally {
      btn.disabled = false;
      btn.textContent = prev;
    }
  });

  form.querySelector('#bridge-reboot').addEventListener('click', async () => {
    if (!confirm('确定立即重启设备并应用桥接参数？')) return;
    try {
      await api.reboot();
      toast('设备正在重启…', 'success');
      setTimeout(() => location.reload(), 3000);
    } catch (err) {
      toast(err.message, 'error');
    }
  });

  return el('div', { class: 'card' }, [
    el('h2', { class: 'card__title' }, '串口桥接 TCP 客户端'),
    el('p', { class: 'card__desc' }, '保存后会写入配置；重启设备后桥接任务会按新参数重新连接。'),
    form,
  ]);
}

export const bridgePage = {
  async mount(root) {
    try {
      const [cfg, status] = await Promise.all([api.getBridge(), api.getStatus().catch(() => null)]);
      if (status) root.append(statusCard(status));
      root.append(bridgeFormCard(cfg));
    } catch (err) {
      root.append(el('div', { class: 'card' }, [`加载失败: ${err.message}`]));
    }
  },
  unmount() {},
};
