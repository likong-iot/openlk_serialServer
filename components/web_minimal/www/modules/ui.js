// ui.js — tiny DOM helpers. No framework.

/**
 * Element factory. Usage:
 *   el('div', { class: 'card' }, [ el('h3', {}, 'Title') ])
 */
export function el(tag, attrs = {}, children = []) {
  const n = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v == null || v === false) continue;
    if (k === 'class')        n.className = v;
    else if (k === 'dataset') Object.assign(n.dataset, v);
    else if (k.startsWith('on') && typeof v === 'function') n.addEventListener(k.slice(2), v);
    else if (k === 'html')    n.innerHTML = v;
    else                      n.setAttribute(k, v);
  }
  const arr = Array.isArray(children) ? children : [children];
  for (const c of arr) {
    if (c == null || c === false) continue;
    n.append(c instanceof Node ? c : document.createTextNode(c));
  }
  return n;
}

/**
 * Build a labelled form field.
 *   field('波特率', el('input',{type:'number',value:115200}))
 */
export function field(label, control, hint) {
  return el('div', { class: 'field' }, [
    el('label', {}, label),
    el('div', { class: 'control' }, [
      control,
      hint ? el('span', { class: 'hint' }, hint) : null,
    ]),
  ]);
}

/**
 * Transient notification.
 */
let _toastTimer = 0;
export function toast(msg, kind = 'info') {
  const t = document.getElementById('toast');
  if (!t) return;
  t.textContent = msg;
  t.className = 'toast is-show ' + (kind === 'error'   ? 'is-error'   :
                                    kind === 'success' ? 'is-success' : '');
  clearTimeout(_toastTimer);
  _toastTimer = setTimeout(() => { t.classList.remove('is-show'); }, 2600);
}

/** Read values from a form, returning an object keyed by `name`. */
export function readForm(root) {
  const out = {};
  for (const c of root.querySelectorAll('[name]')) {
    if (!c.name) continue;
    if (c.type === 'checkbox')      out[c.name] = c.checked;
    else if (c.type === 'number')   out[c.name] = c.value === '' ? null : Number(c.value);
    else                            out[c.name] = c.value;
  }
  return out;
}
