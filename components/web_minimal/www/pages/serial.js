// serial.js — "配串口" page.

import { api }                        from '/modules/api.js';
import { el, field, toast, readForm } from '/modules/ui.js';

const BAUDS  = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600];
const PARITY = [['none', '无'], ['even', '偶'], ['odd', '奇']];
const STOPS  = [[1, '1'], [3, '2']];     // serial_stop_bits_t values

function render(state) {
  const form = el('form', { id: 'form-serial' }, [
    field('波特率',
      el('select', { name: 'baud' },
        BAUDS.map(b => el('option', { value: b, selected: b === state.baud }, String(b))))),

    field('数据位',
      el('select', { name: 'data_bits' },
        [5,6,7,8].map(n => el('option', { value: n, selected: n === state.data_bits }, String(n))))),

    field('停止位',
      el('select', { name: 'stop_bits' },
        STOPS.map(([v, label]) =>
          el('option', { value: v, selected: v === state.stop_bits }, label)))),

    field('校验位',
      el('select', { name: 'parity' },
        PARITY.map(([v, label]) =>
          el('option', { value: v, selected: v === state.parity }, label)))),

    field('硬件流控',
      el('label', { class: 'checkbox' }, [
        el('input', { type: 'checkbox', name: 'flow_ctrl', checked: !!state.flow_ctrl }),
        'RTS/CTS',
      ])),

    field('RS-485',
      el('label', { class: 'checkbox' }, [
        el('input', { type: 'checkbox', name: 'rs485', checked: !!state.rs485 }),
        '半双工',
      ])),

    field('帧间隔 (ms)',
      el('input', { name: 'frame_gap_ms', type: 'number', min: 0, max: 1000,
                    value: state.frame_gap_ms ?? 0 }),
      '0 表示按波特率自动估算'),

    el('div', { class: 'row row--end' }, [
      el('button', { type: 'submit', class: 'btn' }, '保存并应用'),
    ]),
  ]);

  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const btn = form.querySelector('button[type="submit"]');
    btn.disabled = true;
    const prev = btn.textContent;
    btn.textContent = '保存中…';
    try {
      await api.setSerial(readForm(form));
      toast('串口参数已更新', 'success');
    } catch (err) {
      toast(err.message, 'error');
    } finally {
      btn.disabled = false;
      btn.textContent = prev;
    }
  });

  return el('section', {}, [
    el('div', { class: 'card' }, [
      el('h2', { class: 'card__title' }, '串口参数'),
      el('p',  { class: 'card__desc' },  '修改后立即生效，设备无需重启。'),
      form,
    ]),
  ]);
}

export const serialPage = {
  async mount(root) {
    try {
      const state = await api.getSerial();
      root.append(render(state));
    } catch (err) {
      root.append(el('div', { class: 'card' }, [`加载失败: ${err.message}`]));
    }
  },
  unmount() {},
};
