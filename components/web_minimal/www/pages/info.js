// info.js — 基本信息页：固件 / 硬件 / 网络 / 资源 + 重启入口
//
// All data comes from /api/system/info (see web_api_system.c). The page
// auto-refreshes resources every 5s while mounted.

import { api } from '/modules/api.js';
import { el, toast } from '/modules/ui.js';

function fmtUptime(ms) {
  let s = Math.floor(ms / 1000);
  const d = Math.floor(s / 86400); s %= 86400;
  const h = Math.floor(s / 3600);  s %= 3600;
  const m = Math.floor(s / 60);    s %= 60;
  const pad = (n) => String(n).padStart(2, '0');
  return d > 0 ? `${d}天 ${pad(h)}:${pad(m)}:${pad(s)}`
               : `${pad(h)}:${pad(m)}:${pad(s)}`;
}

function fmtBytes(n) {
  if (!n && n !== 0) return '—';
  if (n >= 1024 * 1024) return (n / (1024 * 1024)).toFixed(2) + ' MB';
  if (n >= 1024)        return (n / 1024).toFixed(1) + ' KB';
  return n + ' B';
}

function statTile(icon, label, value, accent) {
  return el('div', { class: 'stat-tile' + (accent ? ' stat-tile--' + accent : '') }, [
    el('div', { class: 'stat-tile__icon', html: icon }),
    el('div', { class: 'stat-tile__body' }, [
      el('div', { class: 'stat-tile__label' }, label),
      el('div', { class: 'stat-tile__value' }, value),
    ]),
  ]);
}

const ICON_CHIP = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
  stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 1v3M15 1v3M9 20v3M15 20v3M1 9h3M1 15h3M20 9h3M20 15h3"/></svg>`;
const ICON_NET  = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
  stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M5 12.55a11 11 0 0 1 14 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/>
  <path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><line x1="12" y1="20" x2="12.01" y2="20"/></svg>`;
const ICON_CLOCK= `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
  stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>`;
const ICON_RAM  = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
  stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <rect x="2" y="6" width="20" height="12" rx="2"/><line x1="6" y1="10" x2="6" y2="14"/>
  <line x1="10" y1="10" x2="10" y2="14"/><line x1="14" y1="10" x2="14" y2="14"/>
  <line x1="18" y1="10" x2="18" y2="14"/></svg>`;
const ICON_FW   = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
  stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/>
  <line x1="12" y1="15" x2="12" y2="3"/></svg>`;
const ICON_REBOOT=`<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
  stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polyline points="23 4 23 10 17 10"/><polyline points="1 20 1 14 7 14"/>
  <path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"/></svg>`;

function kvList(rows) {
  return el('dl', { class: 'kv kv--info' },
    rows.flatMap(([k, v]) => [
      el('dt', {}, k),
      el('dd', {}, v == null || v === '' ? '—' : String(v)),
    ]));
}

async function handleReboot() {
  if (!confirm('确认立即重启设备？所有连接将断开。')) return;
  try {
    await api.reboot();
    toast('设备正在重启…', 'success');
    setTimeout(() => location.reload(), 4000);
  } catch (err) {
    toast(err.message, 'error');
  }
}

export const infoPage = {
  async mount(root) {
    const grid = el('div', { class: 'tile-grid' });
    const fwCard  = el('div', { class: 'card' });
    const hwCard  = el('div', { class: 'card' });
    const netCard = el('div', { class: 'card' });
    root.append(grid, el('div', { class: 'col-2' }, [fwCard, hwCard]), netCard);

    async function refresh() {
      try {
        const d = await api.getInfo();
        const fw = d.fw || {}, hw = d.hw || {}, sys = d.sys || {}, net = d.net || {};

        grid.innerHTML = '';
        grid.append(
          statTile(ICON_FW,    '固件版本',  fw.version || '—'),
          statTile(ICON_CHIP,  '芯片',      `${hw.model || '?'} · ${hw.cores || 1} 核`),
          statTile(ICON_NET,   '网络',      net.got_ip ? net.ip : (net.mode === 'eth' ? '以太网未连接' : 'AP 模式')),
          statTile(ICON_CLOCK, '运行时间',  fmtUptime(sys.uptime_ms || 0)),
          statTile(ICON_RAM,   '可用内存',  fmtBytes(sys.free_heap), 'success'),
          statTile(ICON_REBOOT,'复位原因',  sys.reset_reason || '—', 'muted'),
        );

        fwCard.innerHTML = '';
        fwCard.append(
          el('h2', { class: 'card__title' }, '固件'),
          kvList([
            ['项目',     fw.project],
            ['版本',     fw.version],
            ['IDF 版本', fw.idf_version],
            ['编译时间', `${fw.compile_date || ''} ${fw.compile_time || ''}`.trim()],
          ]),
        );

        hwCard.innerHTML = '';
        hwCard.append(
          el('h2', { class: 'card__title' }, '硬件'),
          kvList([
            ['芯片',         `${hw.model} rev.${hw.revision}`],
            ['核心数',        hw.cores],
            ['Flash',        hw.flash_mb ? `${hw.flash_mb} MB` : '—'],
            ['STA MAC',      hw.mac_sta],
            ['AP MAC',       hw.mac_ap],
            ['PSRAM',        sys.psram_total ? `${fmtBytes(sys.psram_free)} / ${fmtBytes(sys.psram_total)}` : '不可用'],
            ['最低空闲堆',    fmtBytes(sys.min_free_heap)],
          ]),
        );

        netCard.innerHTML = '';
        netCard.append(
          el('h2', { class: 'card__title' }, '网络'),
          kvList([
            ['模式',      net.mode === 'eth' ? '以太网' : 'WiFi'],
            ['SSID',      net.ssid],
            ['信号',      net.rssi ? `${net.rssi} dBm` : '—'],
            ['IP',        net.ip],
            ['子网掩码',   net.mask],
            ['网关',      net.gateway],
          ]),
          el('div', { class: 'row row--end' }, [
            el('button', { class: 'btn btn--danger', onclick: handleReboot }, '重启设备'),
          ]),
        );
      } catch (err) {
        if (err.code === 401) return;
        toast('加载失败：' + err.message, 'error');
      }
    }

    await refresh();
    this._timer = setInterval(refresh, 5000);
  },

  async unmount() {
    if (this._timer) { clearInterval(this._timer); this._timer = 0; }
  },
};
