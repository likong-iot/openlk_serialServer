// app.js — bootstraps the SPA.
// Each page module exports { mount(root), unmount() }.

import { Router }     from '/modules/router.js';
import { api }         from '/modules/api.js';
import { networkPage } from '/pages/network.js';
import { bridgePage }  from '/pages/bridge.js';
import { serialPage  } from '/pages/serial.js';
import { consolePage } from '/pages/console.js';

const mount = document.getElementById('view');

const router = new Router(
  {
    network: networkPage,
    bridge:  bridgePage,
    serial:  serialPage,
    console: consolePage,
  },
  {
    defaultRoute: 'network',
    onChange(name) {
      for (const t of document.querySelectorAll('.tab')) {
        t.classList.toggle('is-active', t.dataset.route === name);
      }
    },
  },
);

router.start(mount);

// Topbar connectivity indicator — periodic probe of /api/system/status.
// If the HTTP probe fails, the WS will also be down, and vice versa.
setInterval(async () => {
  const s = document.getElementById('conn-status');
  if (!s) return;
  try {
    const st = await api.getStatus();
    const up = st?.net?.got_ip;
    s.classList.toggle('is-up', !!up);
    s.classList.toggle('is-down', !up);
    s.querySelector('.status__text').textContent = up ? `STA ${st.net.ip}` : 'AP mode';
  } catch {
    s.classList.remove('is-up');
    s.classList.add('is-down');
    s.querySelector('.status__text').textContent = '无响应';
  }
}, 4000);
