// api.js — thin HTTP + WebSocket wrappers.
//
// Every REST response follows  { code: number, msg: string, data: object|null }.
// `code !== 0` is thrown as ApiError so pages can catch uniformly.
// `code === 401` triggers the global onUnauthorized hook (see app.js).

export class ApiError extends Error {
  constructor(code, msg) {
    super(msg || `error ${code}`);
    this.code = code;
  }
}

const TOKEN_KEY = 'sgw.token';
const HOOKS = { onUnauthorized: null };

export function getToken() {
  try { return localStorage.getItem(TOKEN_KEY) || ''; } catch { return ''; }
}
export function setToken(tok) {
  try { tok ? localStorage.setItem(TOKEN_KEY, tok) : localStorage.removeItem(TOKEN_KEY); }
  catch { /* private mode etc. — ignore */ }
}
export function setUnauthorizedHandler(fn) { HOOKS.onUnauthorized = fn; }

async function request(method, url, body) {
  const init = { method, headers: {} };
  const tok = getToken();
  if (tok) init.headers['Authorization'] = `Bearer ${tok}`;
  if (body !== undefined) {
    init.headers['Content-Type'] = 'application/json';
    init.body = JSON.stringify(body);
  }

  let resp;
  try { resp = await fetch(url, init); }
  catch { throw new ApiError(-1, '网络不可达'); }

  let payload = null;
  try { payload = await resp.json(); } catch { /* non-json */ }

  if (resp.status === 401 || (payload && payload.code === 401)) {
    setToken('');
    if (HOOKS.onUnauthorized) HOOKS.onUnauthorized();
    throw new ApiError(401, payload?.msg || '未登录');
  }
  if (!resp.ok && !payload) throw new ApiError(resp.status, resp.statusText);
  if (!payload) throw new ApiError(-2, '无响应');
  if (payload.code !== 0) throw new ApiError(payload.code, payload.msg);
  return payload.data ?? {};
}

export const api = {
  // auth
  login:           (user, password) => request('POST', '/api/auth/login',           { user, password }),
  logout:          ()               => request('POST', '/api/auth/logout',          {}),
  me:              ()               => request('GET',  '/api/auth/me'),
  changePassword:  (oldp, newp)     => request('POST', '/api/auth/change_password', { old: oldp, new: newp }),

  // network
  getNetwork:   ()    => request('GET',  '/api/network'),
  setNetwork:   (cfg) => request('POST', '/api/network', cfg),
  scanNetworks: ()    => request('GET',  '/api/network/scan'),

  // serial
  getSerial:    ()    => request('GET',  '/api/serial'),
  setSerial:    (cfg) => request('POST', '/api/serial', cfg),
  sendSerial:   (fmt, data) => request('POST', '/api/serial/send', { fmt, data }),

  // system
  getStatus:    ()    => request('GET',  '/api/system/status'),
  getInfo:      ()    => request('GET',  '/api/system/info'),
  reboot:       ()    => request('POST', '/api/system/reboot', {}),

  // work mode (bridge)
  getWorkmode:        ()    => request('GET',  '/api/workmode'),
  setWorkmode:        (cfg) => request('POST', '/api/workmode', cfg),
  getWorkmodeStatus:  ()    => request('GET',  '/api/workmode/status'),
};

/**
 * SerialSocket — WebSocket with auto-reconnect, token attached as ?token=.
 */
export class SerialSocket {
  constructor({ onopen, onclose, onframe }) {
    this.onopen = onopen || (() => {});
    this.onclose = onclose || (() => {});
    this.onframe = onframe || (() => {});
    this._backoff = 500;
    this._stopped = false;
    this._connect();
  }

  _connect() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const tok   = encodeURIComponent(getToken());
    const ws = new WebSocket(`${proto}//${location.host}/ws/serial?token=${tok}`);
    this._ws = ws;

    ws.onopen = () => { this._backoff = 500; this.onopen(); };
    ws.onmessage = (e) => {
      try { this.onframe(JSON.parse(e.data)); }
      catch { /* ignore malformed */ }
    };
    ws.onerror = () => { /* onclose handles */ };
    ws.onclose = () => {
      this.onclose();
      if (!this._stopped) {
        setTimeout(() => this._connect(), this._backoff);
        this._backoff = Math.min(this._backoff * 2, 8000);
      }
    };
  }

  stop() {
    this._stopped = true;
    if (this._ws) this._ws.close();
  }
}
