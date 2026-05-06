// network.js — 配网页
//
// Two cards: STA / IP (existing) and AP (热点参数, 新增).
// AP password is never returned by the API; "leave empty = keep current".

import { api }              from '/modules/api.js';
import { el, field, toast, readForm } from '/modules/ui.js';

function signalBars(rssi) {
  if (rssi >= -50) return '▮▮▮▮';
  if (rssi >= -65) return '▮▮▮▯';
  if (rssi >= -75) return '▮▮▯▯';
  if (rssi >= -85) return '▮▯▯▯';
  return '▯▯▯▯';
}

function statusCard(status) {
  const net = status.net || {};
  const sys = status.sys || {};
  const up  = net.got_ip;
  const items = [
    ['连接状态', up ? '已连接' : (net.wifi_up ? '获取 IP 中' : '未连接')],
    ['SSID',     net.ssid || '—'],
    ['IP',       net.ip   || '—'],
    ['信号',     net.rssi ? `${signalBars(net.rssi)}  ${net.rssi} dBm` : '—'],
    ['运行',     `${Math.floor((sys.uptime_ms || 0) / 1000)} 秒`],
  ];
  return el('div', { class: 'card' }, [
    el('h2', { class: 'card__title' }, '当前状态'),
    el('dl', { class: 'kv' },
      items.flatMap(([k, v]) => [
        el('dt', {}, k),
        el('dd', {}, String(v)),
      ])),
  ]);
}

function staCard(state, { onScan, onSubmitSta, onReboot }) {
  const ssidInput  = el('input', { name: 'ssid', type: 'text', value: state.ssid || '', autocomplete: 'off' });
  const ssidSelect = el('select', { class: 'scan-list' });
  ssidSelect.append(el('option', { value: '' }, '— 扫描后选择 —'));
  ssidSelect.addEventListener('change', () => {
    if (ssidSelect.value) ssidInput.value = ssidSelect.value;
  });

  const scanBtn   = el('button', { type: 'button', class: 'btn btn--ghost' }, '扫描');
  scanBtn.addEventListener('click', () => onScan(scanBtn, ssidSelect));

  const submitBtn = el('button', { type: 'submit', class: 'btn btn--primary' }, '保存');
  const rebootBtn = el('button', { type: 'button', class: 'btn btn--ghost' }, '重启设备');
  rebootBtn.addEventListener('click', onReboot);

  const form = el('form', { id: 'form-net-sta' }, [
    field('模式',
      el('select', { name: 'mode' }, [
        el('option', { value: 'wifi', selected: state.mode === 'wifi' }, 'WiFi'),
        el('option', { value: 'eth',  selected: state.mode === 'eth'  }, '以太网'),
      ])),

    field('WiFi SSID',
      el('div', { class: 'inline-group' }, [ ssidInput, scanBtn ]),
      '填写或点击扫描选择'),

    field('扫描结果', ssidSelect),

    field('WiFi 密码',
      el('input', { name: 'password', type: 'password', placeholder: '留空保持不变' })),

    field('寻址方式',
      el('label', { class: 'checkbox' }, [
        el('input', { type: 'checkbox', name: 'dhcp', checked: state.dhcp !== false }),
        '自动 (DHCP)',
      ])),

    field('IP',       el('input', { name: 'ip',      type: 'text', value: state.ip      || '' })),
    field('子网掩码',  el('input', { name: 'mask',    type: 'text', value: state.mask    || '' })),
    field('网关',     el('input', { name: 'gateway', type: 'text', value: state.gateway || '' })),

    el('div', { class: 'row row--end' }, [ rebootBtn, el('span', { class: 'spacer' }), submitBtn ]),
  ]);

  form.addEventListener('submit', (e) => { e.preventDefault(); onSubmitSta(form, submitBtn); });
  return el('div', { class: 'card' }, [
    el('h2', { class: 'card__title' }, '上行网络（STA / 以太网）'),
    el('p',  { class: 'card__desc' },  '保存后可能需要重启以切换网络模式。'),
    form,
  ]);
}

function apCard(state, { onSubmitAp }) {
  const submitBtn = el('button', { type: 'submit', class: 'btn btn--primary' }, '保存');
  const channels = Array.from({ length: 13 }, (_, i) => i + 1);

  const form = el('form', { id: 'form-net-ap' }, [
    field('AP 名称',
      el('input', { name: 'ap_ssid', type: 'text',
                    value: state.ap_ssid || '',
                    placeholder: '默认 Gateway-XXXXXX',
                    maxlength: 32 }),
      '设备热点广播的 SSID'),
    field('AP 密码',
      el('input', { name: 'ap_password', type: 'password', placeholder: '留空保持不变（≥8 位 WPA2）' }),
      '留空 = 不修改；填写需 8–63 位'),
    field('信道',
      el('select', { name: 'ap_channel' },
        channels.map((c) => el('option',
          { value: c, selected: (state.ap_channel || 1) === c }, String(c))))),
    el('div', { class: 'row row--end' }, [ submitBtn ]),
  ]);

  form.addEventListener('submit', (e) => { e.preventDefault(); onSubmitAp(form, submitBtn); });

  return el('div', { class: 'card' }, [
    el('h2', { class: 'card__title' }, 'AP 热点'),
    el('p',  { class: 'card__desc' },  '设备无法连接上游时，提供此热点用于配网。'),
    form,
  ]);
}

async function postCfg(form, btn, label, transform) {
  const data = transform(readForm(form));
  btn.disabled = true;
  const prev = btn.textContent;
  btn.textContent = label;
  try {
    const d = await api.setNetwork(data);
    toast(d.reboot_required ? '已保存，可点「重启设备」立即应用' : '已保存', 'success');
  } catch (err) {
    toast(err.message, 'error');
  } finally {
    btn.disabled = false;
    btn.textContent = prev;
  }
}

async function handleSubmitSta(form, btn) {
  return postCfg(form, btn, '保存中…', (d) => {
    if (!d.password) delete d.password;
    return d;
  });
}

async function handleSubmitAp(form, btn) {
  return postCfg(form, btn, '保存中…', (d) => {
    if (!d.ap_password) delete d.ap_password;
    if (d.ap_channel != null) d.ap_channel = Number(d.ap_channel);
    return d;
  });
}

async function handleScan(btn, select) {
  btn.disabled = true;
  const prev = btn.textContent;
  btn.textContent = '扫描中…';
  try {
    const d = await api.scanNetworks();
    select.innerHTML = '';
    select.append(el('option', { value: '' }, `— 共 ${d.count} 个 —`));
    for (const ap of d.aps) {
      select.append(el('option', { value: ap.ssid },
        `${ap.ssid}    ${ap.rssi} dBm    ${ap.auth}`));
    }
    toast(`扫描到 ${d.count} 个网络`, 'success');
  } catch (err) {
    toast('扫描失败: ' + err.message, 'error');
  } finally {
    btn.disabled = false;
    btn.textContent = prev;
  }
}

async function handleReboot() {
  if (!confirm('确定立即重启设备？')) return;
  try {
    await api.reboot();
    toast('设备正在重启…', 'success');
    setTimeout(() => location.reload(), 3000);
  } catch (err) {
    toast(err.message, 'error');
  }
}

export const networkPage = {
  async mount(root) {
    try {
      const [cfg, status] = await Promise.all([
        api.getNetwork(),
        api.getStatus().catch(() => null),
      ]);
      if (status) root.append(statusCard(status));
      root.append(staCard(cfg, {
        onScan:      handleScan,
        onSubmitSta: handleSubmitSta,
        onReboot:    handleReboot,
      }));
      root.append(apCard(cfg, { onSubmitAp: handleSubmitAp }));
    } catch (err) {
      root.append(el('div', { class: 'card' }, [`加载失败: ${err.message}`]));
    }
  },
  unmount() {},
};
