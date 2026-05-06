// app.js — bootstraps the SPA, routes between two shells:
//   1. authenticated app shell (#shell)  — main features
//   2. login/setup view       (#auth-view) — login + must-change-password flow
//
// Each page module exports { mount(root), unmount() }.

import { Router }      from '/modules/router.js';
import { api, getToken, setToken, setUnauthorizedHandler } from '/modules/api.js';
import { toast }       from '/modules/ui.js';
import { networkPage  } from '/pages/network.js';
import { serialPage   } from '/pages/serial.js';
import { consolePage  } from '/pages/console.js';
import { infoPage     } from '/pages/info.js';
import { workmodePage } from '/pages/workmode.js';
import { loginPage, changePasswordPage } from '/pages/login.js';

const shell    = document.getElementById('shell');
const authView = document.getElementById('auth-view');
const view     = document.getElementById('view');
const userMenu = document.getElementById('user-menu');
const userName = document.getElementById('user-name');
const userAvatar = document.getElementById('user-avatar');

let router = null;

/* ── Auth view (login / change password) ──────────────────────────────── */

let currentAuthPage = null;
async function showAuth(pageFactory) {
  shell.hidden = true;
  authView.hidden = false;
  if (currentAuthPage?.unmount) await currentAuthPage.unmount();
  authView.innerHTML = '';
  currentAuthPage = pageFactory();
  await currentAuthPage.mount(authView, {
    onSuccess: () => bootShell(),
    onMustChange: () => showAuth(() => changePasswordPage({
      forced: true,
      onDone: () => bootShell(),
    })),
  });
}

/* ── App shell (after login) ─────────────────────────────────────────── */

async function bootShell() {
  if (currentAuthPage?.unmount) await currentAuthPage.unmount();
  currentAuthPage = null;
  authView.hidden = true;
  authView.innerHTML = '';
  shell.hidden = false;

  // Refresh user info into the topbar.
  try {
    const me = await api.me();
    if (me.must_change) {
      // Server requires a password change. Force the flow.
      return showAuth(() => changePasswordPage({
        forced: true,
        onDone: () => bootShell(),
      }));
    }
    userName.textContent = me.user || '—';
    userAvatar.textContent = (me.user || '?').charAt(0).toUpperCase();
    userMenu.hidden = false;
  } catch {
    return showAuth(() => loginPage());
  }

  if (!router) {
    router = new Router(
      {
        info:     infoPage,
        network:  networkPage,
        serial:   serialPage,
        workmode: workmodePage,
        console:  consolePage,
      },
      {
        defaultRoute: 'info',
        onChange(name) {
          for (const t of document.querySelectorAll('.tab')) {
            t.classList.toggle('is-active', t.dataset.route === name);
          }
        },
      },
    );
    router.start(view);
  } else {
    // Re-render current route after re-login.
    router._active = null;
    router._render();
  }
}

/* ── Connection indicator ────────────────────────────────────────────── */

let statusTimer = 0;
function startStatusPoll() {
  if (statusTimer) return;
  const tick = async () => {
    if (shell.hidden) return;  // skip while on auth view
    const s = document.getElementById('conn-status');
    if (!s) return;
    try {
      const st = await api.getStatus();
      const up = st?.net?.got_ip;
      s.classList.toggle('is-up', !!up);
      s.classList.toggle('is-down', !up);
      s.querySelector('.status__text').textContent =
        up ? `STA · ${st.net.ip}` : 'AP 模式';
    } catch (err) {
      if (err.code !== 401) {
        s.classList.remove('is-up');
        s.classList.add('is-down');
        s.querySelector('.status__text').textContent = '无响应';
      }
    }
  };
  statusTimer = setInterval(tick, 4000);
  tick();
}

/* ── User menu ───────────────────────────────────────────────────────── */

userMenu.addEventListener('click', async (e) => {
  const btn = e.target.closest('button');
  if (!btn) return;
  if (btn.classList.contains('user-menu__btn')) {
    userMenu.classList.toggle('is-open');
    return;
  }
  userMenu.classList.remove('is-open');
  const act = btn.dataset.act;
  if (act === 'logout') {
    try { await api.logout(); } catch { /* ignore */ }
    setToken('');
    userMenu.hidden = true;
    showAuth(() => loginPage());
  } else if (act === 'changepw') {
    showAuth(() => changePasswordPage({
      forced: false,
      onDone: () => bootShell(),
      onCancel: () => bootShell(),
    }));
  }
});
document.addEventListener('click', (e) => {
  if (!userMenu.contains(e.target)) userMenu.classList.remove('is-open');
});

/* ── Boot ────────────────────────────────────────────────────────────── */

setUnauthorizedHandler(() => {
  toast('登录已过期，请重新登录', 'error');
  userMenu.hidden = true;
  showAuth(() => loginPage());
});

(async function boot() {
  startStatusPoll();
  if (getToken()) {
    try { await api.me(); return bootShell(); }
    catch { setToken(''); /* fallthrough */ }
  }
  showAuth(() => loginPage());
})();
