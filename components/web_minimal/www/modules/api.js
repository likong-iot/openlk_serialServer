// api.js — thin HTTP + WebSocket wrappers around the firmware's API.
//
// Every REST response follows  { code: number, msg: string, data: object|null }.
// Non-zero `code` is thrown as an ApiError so pages can catch uniformly.

export class ApiError extends Error {
  constructor(code, msg) {
    super(msg || `error ${code}`);
    this.code = code;
  }
}

async function request(method, url, body) {
  const init = { method, headers: {} };
  if (body !== undefined) {
    init.headers['Content-Type'] = 'application/json';
    init.body = JSON.stringify(body);
  }

  let resp;
  try {
    resp = await fetch(url, init);
  } catch (e) {
    throw new ApiError(-1, '网络不可达');
  }

  let payload = null;
  try { payload = await resp.json(); } catch { /* non-json */ }

  if (!resp.ok && !payload) throw new ApiError(resp.status, resp.statusText);
  if (!payload) throw new ApiError(-2, '无响应');
  if (payload.code !== 0) throw new ApiError(payload.code, payload.msg);
  return payload.data ?? {};
}

export const api = {
  getNetwork:   ()     => request('GET',  '/api/network'),
  setNetwork:   (cfg)  => request('POST', '/api/network', cfg),
  scanNetworks: ()     => request('GET',  '/api/network/scan'),
  getSerial:    ()     => request('GET',  '/api/serial'),
  setSerial:    (cfg)  => request('POST', '/api/serial', cfg),
  sendSerial:   (fmt, data) => request('POST', '/api/serial/send', { fmt, data }),
  getStatus:    ()     => request('GET',  '/api/system/status'),
  reboot:       ()     => request('POST', '/api/system/reboot', {}),
};

/**
 * SerialSocket — WebSocket with auto-reconnect for /ws/serial.
 *
 * Events:
 *   onopen()        — socket opened
 *   onclose()       — socket closed (will auto-reconnect)
 *   onframe(frame)  — received { dir, ts, fmt, data }
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
    const ws = new WebSocket(`${proto}//${location.host}/ws/serial`);
    this._ws = ws;

    ws.onopen = () => { this._backoff = 500; this.onopen(); };
    ws.onmessage = (e) => {
      try { this.onframe(JSON.parse(e.data)); }
      catch { /* ignore malformed */ }
    };
    ws.onerror = () => { /* onclose will handle */ };
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
