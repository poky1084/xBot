// xBot request proxy: runs in the casino tab, takes requests from xBot over a local
// WebSocket, sends them with fetch() from this page (the browser's own TLS, cookies and
// Cloudflare session) and returns the answers.
//
// Protocol (xBot <-> extension):
//   ext -> app  {"type":"hello","host":"duel.com","version":"2.0.0"}      on connect
//   app -> ext  {"id":"...","payload":{"raw":true,"url":"...","options":{method,headers,body},
//                "timeoutMs":15000}}
//   ext -> app  {"id":"...","status":200,"body":"<response text>"}         any HTTP status
//               {"id":"...","status":0,"error":"..."}                     network error / timeout
//   app -> ext  {"id":"...","payload":{"raw":true,"op":"storage","key":"security:uuid"}}
//   ext -> app  {"id":"...","status":200,"body":"{\"value\":<string|null>}"}  (page localStorage)
//   app -> ext  {"id":"...","payload":{"raw":true,"op":"cookie","key":"session"}}
//   ext -> app  {"id":"...","status":200,"body":"{\"value\":<string|null>}"}  (cookie of this site)
//   Legacy requests (no "raw", sent by older xBot versions for Stake):
//   ext -> app  {"id":"...","response":<JSON>}  or  {"id":"...","error":"..."}
(function () {
  if (window.__xbotRequestProxy)
    return; // already running in this page
  window.__xbotRequestProxy = true;

  const VERSION = '2.1.1';
  const URL_WS = 'ws://localhost:4444';
  const RECONNECT_MS = 5000;
  const STANDBY_MS = 15000;      // another tab of this site is connected already
  const CLOSE_DUPLICATE = 4001;  // sent by xBot for that case

  let ws;

  function send(obj) {
    if (ws && ws.readyState === WebSocket.OPEN)
      ws.send(JSON.stringify(obj));
  }

  function connect() {
    ws = new WebSocket(URL_WS);

    ws.onopen = () => {
      console.log('xBot proxy connected');
      send({ type: 'hello', host: location.hostname.toLowerCase(), version: VERSION });
    };

    ws.onmessage = async (event) => {
      let message;
      try {
        message = JSON.parse(event.data);
      } catch (e) {
        return;
      }
      if (!message || !message.id || !message.payload)
        return;
      const payload = message.payload;
      if (payload.raw && payload.op === 'storage') { // read a value of this page's localStorage
        let value = null;
        try {
          value = localStorage.getItem(String(payload.key));
        } catch (e) {}
        send({ id: message.id, status: 200, body: JSON.stringify({ value: value }) });
        return;
      }
      if (payload.raw && payload.op === 'cookie') { // a cookie of this site (also httpOnly ones)
        let reply;
        try {
          const r = await chrome.runtime.sendMessage({ type: 'xbot-cookie', name: String(payload.key) });
          reply = (r && r.error) ? { status: 0, error: r.error }
                                 : { status: 200, body: JSON.stringify({ value: r ? r.value : null }) };
        } catch (e) {
          // e.g. "Extension context invalidated" after reloading the extension: refresh the tab
          reply = { status: 0, error: String((e && e.message) || e) + ' (refresh the casino tab)' };
        }
        send(Object.assign({ id: message.id }, reply));
        return;
      }
      if (payload.raw) {
        send(Object.assign({ id: message.id }, await rawRequest(payload)));
        return;
      }
      try {
        send({ id: message.id, response: await legacyRequest(payload.url, payload.options || {}) });
      } catch (error) {
        send({ id: message.id, error: String((error && error.message) || error) });
      }
    };

    ws.onerror = () => {};

    ws.onclose = (event) => {
      if (retired)
        return;
      const standby = event && event.code === CLOSE_DUPLICATE;
      console.log(standby ? 'xBot proxy: another tab of this site is connected (standby)'
                          : 'xBot proxy disconnected');
      setTimeout(connect, standby ? STANDBY_MS : RECONNECT_MS);
    };
  }

  // One request, any status, the answer as text. Never retried: a bet must not be sent twice.
  async function rawRequest(payload) {
    const options = Object.assign({}, payload.options || {});
    if (options.body && typeof options.body === 'object')
      options.body = JSON.stringify(options.body);
    if (!options.body)
      delete options.body;
    // Default 'same-origin': the page's cookies go with requests to its own site. (Requests to an
    // API on another host, e.g. apis.goated.com, use their Authorization header instead.)
    options.cache = 'no-store';
    options.redirect = 'follow';
    const timeoutMs = payload.timeoutMs > 0 ? payload.timeoutMs : 15000;
    options.signal = AbortSignal.timeout(timeoutMs);
    try {
      const response = await fetch(payload.url, options);
      return { status: response.status, body: await response.text() };
    } catch (error) {
      const name = error && error.name;
      return {
        status: 0,
        error: (name === 'TimeoutError' || name === 'AbortError')
          ? 'timeout (' + Math.round(timeoutMs / 1000) + ' s)'
          : String((error && error.message) || error)
      };
    }
  }

  // Older xBot requests (Stake): JSON answer, retried on network errors.
  async function legacyRequest(url, options) {
    if (options.body && typeof options.body === 'object')
      options.body = JSON.stringify(options.body);
    const response = await fetchPlus(url, options);
    const text = await response.text();
    try {
      return JSON.parse(text);
    } catch (e) {
      if (!response.ok)
        throw new Error('HTTP error! status: ' + response.status);
      throw new Error('not a JSON answer: ' + text.slice(0, 200));
    }
  }

  // Retries network errors / timeouts; any HTTP answer (also 4xx/5xx with a JSON error body)
  // is returned so the API's error message reaches xBot.
  function fetchPlus(url, options = {}, retries = 5) {
    return fetch(url, Object.assign({ signal: AbortSignal.timeout(5000) }, options, { redirect: 'follow' }))
      .catch(async (error) => {
        if (retries > 0) {
          console.log('xBot proxy: retrying fetch, ' + retries + ' retries left.', error && error.name);
          await new Promise((res) => setTimeout(res, 500));
          return fetchPlus(url, options, retries - 1);
        }
        throw new Error((error && error.message) || String(error));
      });
  }

  // The extension was reloaded / updated: this copy can no longer reach it (cookies), so it
  // steps aside and lets the new copy (injected into this tab) take over.
  let retired = false;
  setInterval(() => {
    let alive = false;
    try {
      alive = !!(chrome.runtime && chrome.runtime.id);
    } catch (e) {}
    if (!alive && !retired) {
      retired = true;
      if (ws) {
        ws.onclose = null;
        ws.close();
      }
    }
  }, 2000);

  connect();
})();
