// router.js — tiny hash-based router.
//
// Usage:
//   const router = new Router({
//     network: { mount, unmount },
//     serial:  { mount, unmount },
//     console: { mount, unmount },
//   }, { defaultRoute: 'network', onChange(route){} });
//   router.start(mountElement);

export class Router {
  constructor(routes, { defaultRoute = Object.keys(routes)[0], onChange } = {}) {
    this.routes = routes;
    this.default = defaultRoute;
    this.onChange = onChange || (() => {});
    this._active = null;
  }

  start(mount) {
    this.mount = mount;
    window.addEventListener('hashchange', () => this._render());
    this._render();
  }

  _parse() {
    const raw = location.hash.replace(/^#\/?/, '').split('?')[0];
    return this.routes[raw] ? raw : this.default;
  }

  async _render() {
    const name = this._parse();
    if (this._active === name) return;

    const next = this.routes[name];
    const prev = this._active && this.routes[this._active];

    if (prev && prev.unmount) {
      try { await prev.unmount(); } catch (e) { console.error(e); }
    }
    this.mount.innerHTML = '';
    this._active = name;
    this.onChange(name);
    try {
      await next.mount(this.mount);
    } catch (e) {
      console.error('route mount failed:', e);
      this.mount.innerHTML = `<div class="card"><p>页面加载失败：${e.message}</p></div>`;
    }
  }
}
