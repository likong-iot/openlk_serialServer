// login.js — fullscreen login + forced/optional change-password views.
//
// Both views are mounted into #auth-view (not the SPA router) because they
// must work without a session (and replace the navigation chrome).

import { api, setToken } from '/modules/api.js';
import { el, field, toast, readForm } from '/modules/ui.js';

function authShell(title, subtitle, body) {
  return el('div', { class: 'auth-shell' }, [
    el('div', { class: 'auth-bg', 'aria-hidden': 'true' }),
    el('section', { class: 'auth-card' }, [
      el('header', { class: 'auth-card__head' }, [
        el('div', { class: 'auth-card__logo' }, [
          el('span', { html: `
            <svg viewBox="0 0 24 24" width="28" height="28" fill="none"
                 stroke="currentColor" stroke-width="2"
                 stroke-linecap="round" stroke-linejoin="round">
              <path d="M4 7h16M4 12h10M4 17h16"/>
              <circle cx="18" cy="12" r="2" fill="currentColor" stroke="none"/>
            </svg>` }),
        ]),
        el('h1', { class: 'auth-card__title' }, title),
        subtitle ? el('p', { class: 'auth-card__sub' }, subtitle) : null,
      ]),
      body,
      el('footer', { class: 'auth-card__foot' },
        '© 立控电子 · Serial Gateway'),
    ]),
  ]);
}

/* ── login page ──────────────────────────────────────────────────────── */

export function loginPage() {
  let unmounted = false;

  return {
    async mount(root, { onSuccess, onMustChange }) {
      const userInput = el('input', {
        name: 'user', type: 'text', value: 'admin', autocomplete: 'username',
        autofocus: true, placeholder: '用户名',
      });
      const passInput = el('input', {
        name: 'password', type: 'password', autocomplete: 'current-password',
        placeholder: '密码',
      });
      const submit = el('button', { type: 'submit', class: 'btn btn--primary btn--block' }, '登录');

      const form = el('form', { class: 'auth-form' }, [
        field('用户名', userInput),
        field('密  码',  passInput),
        el('div', { class: 'auth-hint' },
          '出厂默认账号：admin / admin。首次登录将强制修改密码。'),
        submit,
      ]);

      form.addEventListener('submit', async (e) => {
        e.preventDefault();
        const data = readForm(form);
        submit.disabled = true; submit.textContent = '登录中…';
        try {
          const r = await api.login(data.user, data.password);
          setToken(r.token);
          if (r.must_change) onMustChange();
          else               onSuccess();
        } catch (err) {
          toast(err.code === 401 ? '账号或密码错误' : err.message, 'error');
          submit.disabled = false; submit.textContent = '登录';
          passInput.value = '';
          passInput.focus();
        }
      });

      root.append(authShell('欢迎回来', '请登录以管理串口网关', form));
    },
    async unmount() { unmounted = true; void unmounted; },
  };
}

/* ── change password ─────────────────────────────────────────────────── */

export function changePasswordPage({ forced, onDone, onCancel }) {
  return {
    async mount(root) {
      const oldInput = el('input', {
        name: 'old', type: 'password', autocomplete: 'current-password',
        placeholder: '当前密码', autofocus: true,
      });
      const newInput = el('input', {
        name: 'new', type: 'password', autocomplete: 'new-password',
        placeholder: '至少 4 位',
      });
      const repInput = el('input', {
        name: 'rep', type: 'password', autocomplete: 'new-password',
        placeholder: '再次输入',
      });
      const submit = el('button', { type: 'submit', class: 'btn btn--primary btn--block' }, '保存新密码');
      const cancel = onCancel
        ? el('button', { type: 'button', class: 'btn btn--ghost btn--block' }, '取消')
        : null;
      if (cancel) cancel.addEventListener('click', onCancel);

      const form = el('form', { class: 'auth-form' }, [
        field('当前密码', oldInput),
        field('新密码',   newInput),
        field('确认',     repInput),
        forced
          ? el('div', { class: 'auth-hint auth-hint--warn' },
              '出厂默认密码不安全，请先修改后再使用。')
          : null,
        submit,
        cancel,
      ]);

      form.addEventListener('submit', async (e) => {
        e.preventDefault();
        const d = readForm(form);
        if (d.new !== d.rep) { toast('两次输入的新密码不一致', 'error'); return; }
        if ((d.new || '').length < 4) { toast('新密码至少 4 位', 'error'); return; }
        submit.disabled = true; submit.textContent = '保存中…';
        try {
          await api.changePassword(d.old, d.new);
          toast('密码已更新', 'success');
          onDone && onDone();
        } catch (err) {
          toast(err.message || '修改失败', 'error');
          submit.disabled = false; submit.textContent = '保存新密码';
        }
      });

      root.append(authShell(
        forced ? '请先修改密码' : '修改密码',
        forced ? '为了安全，使用前请把出厂默认密码改掉' : '更新管理员密码',
        form));
    },
    async unmount() {},
  };
}
