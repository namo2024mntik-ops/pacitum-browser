// Pacitum 1.7.1 permission shims (permissions-spec.md "gaps"). Injected into the main world of
// top-level http(s)/file documents at DocumentCreation. Qt WebEngine has no chooser for WebUSB,
// Web Bluetooth, Web Serial or WebHID and no permission type for Web MIDI or persistent storage,
// so these APIs are routed to Pacitum's native, consent-first pickers over QWebChannel.
// Security model: this file only *asks*. The browser derives the origin itself, shows the prompt,
// stores grants per origin + profile, and never grants without a click. A page that bypasses the
// shim and calls the bridge directly gets exactly the same prompts.
(function () {
  'use strict';
  if (window.__pacitumPermShim) return;
  try { Object.defineProperty(window, '__pacitumPermShim', { value: true }); } catch (e) { return; }
  var proto = location.protocol;
  if (proto !== 'https:' && proto !== 'http:' && proto !== 'file:') return;

  var QWC = (function () {
    var module = { exports: {} }; var exports = module.exports;
    /*__QWEBCHANNEL_JS__*/
    return (module.exports && module.exports.QWebChannel) || (typeof QWebChannel !== 'undefined' ? QWebChannel : null);
  })();

  var NONCE = '__PACITUM_NONCE__';   // per page, main frame only: iframes can't use the bridge
  var bridge = null, waiting = [], seq = 1, pending = {}, serialSinks = {}, failed = false;
  var stopHooks = { capture: [], tabshare: {} };
  function withBridge(fn) { if (bridge) fn(bridge); else if (failed) fn(null); else waiting.push(fn); }
  var tries = 0;
  function connect() {
    if (!(window.qt && window.qt.webChannelTransport) || !QWC) {
      if (++tries > 100) { failed = true; var w0 = waiting; waiting = []; w0.forEach(function (f) { f(null); }); return; }
      return setTimeout(connect, 50);
    }
    new QWC(window.qt.webChannelTransport, function (ch) {
      bridge = ch.objects.pacitumPermissions || null;
      if (!bridge) { failed = true; } else {
        bridge.resolved.connect(function (id, json) {
          var p = pending[id]; if (!p) return; delete pending[id];
          var r; try { r = JSON.parse(json); } catch (e) { r = { error: 'NotSupportedError', message: 'Bad reply' }; }
          if (r.error) p.reject(r.error === 'TypeError' ? new TypeError(r.message) : new DOMException(r.message || r.error, r.error));
          else p.resolve(r.value);
        });
        bridge.serialData.connect(function (tok, b64) { var s = serialSinks[tok]; if (s) s(b64); });
        bridge.stopRequested.connect(function (what, tok) {
          if (what === 'capture') stopHooks.capture.slice().forEach(function (f) { f(); });
          if (what === 'tabshare' && stopHooks.tabshare[tok]) stopHooks.tabshare[tok]();
          if (what === 'display') live.forEach(function (t) { if (!t.__pacitumDisplay) return; t.stop(); try { t.dispatchEvent(new Event('ended')); } catch (e) {} });
        });
      }
      var w = waiting; waiting = []; w.forEach(function (f) { f(bridge); });
    });
  }
  connect();

  function call(kind, opts) {
    return new Promise(function (resolve, reject) {
      withBridge(function (b) {
        if (!b) { reject(new DOMException('Not available in this context', 'NotSupportedError')); return; }
        var id = seq++; pending[id] = { resolve: resolve, reject: reject };
        b.request(id, kind, JSON.stringify({ n: NONCE, o: opts || {} }));
      });
    });
  }
  function notify(kind, opts) { withBridge(function (b) { if (b) b.request(0, kind, JSON.stringify({ n: NONCE, o: opts || {} })); }); }
  function gesture() {
    if (navigator.userActivation && !navigator.userActivation.isActive)
      throw new DOMException('Must be handling a user gesture to show a permission request.', 'SecurityError');
  }
  function unsupported(what) {
    return function () {
      return Promise.reject(new DOMException(what + ' I/O is not available in Pacitum: Qt WebEngine ships no ' + what +
        ' backend. The device was shared with this site, but it cannot be opened.', 'NotSupportedError'));
    };
  }
  function defineGetter(name, obj) {
    try { Object.defineProperty(Navigator.prototype, name, { get: function () { return obj; }, configurable: true, enumerable: true }); } catch (e) {}
  }
  function b64ToBytes(b64) { var s = atob(b64), a = new Uint8Array(s.length); for (var i = 0; i < s.length; i++) a[i] = s.charCodeAt(i); return a; }
  function bytesToB64(chunk) {
    var u = chunk instanceof Uint8Array ? chunk : ArrayBuffer.isView(chunk) ? new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength) : new Uint8Array(chunk);
    var s = ''; for (var i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000));
    return btoa(s);
  }
  function forgetter(kind, d) { return function () { return call('forget', { kind: kind, id: d.id }).then(function () {}); }; }

  // ---- WebUSB
  function makeUsb(d) {
    return { vendorId: d.vendorId | 0, productId: d.productId | 0, productName: d.name || '', manufacturerName: d.manufacturer || '',
      serialNumber: d.serial || '', opened: false, configuration: null, configurations: [],
      open: unsupported('USB'), close: function () { return Promise.resolve(); }, selectConfiguration: unsupported('USB'),
      claimInterface: unsupported('USB'), controlTransferIn: unsupported('USB'), controlTransferOut: unsupported('USB'),
      transferIn: unsupported('USB'), transferOut: unsupported('USB'), forget: forgetter('usb', d) };
  }
  var usb = new EventTarget();
  usb.requestDevice = function (o) { try { gesture(); } catch (e) { return Promise.reject(e); }
    if (!o || !Array.isArray(o.filters)) return Promise.reject(new TypeError("Failed to execute 'requestDevice': 'filters' is required"));
    return call('usb', o).then(makeUsb); };
  usb.getDevices = function () { return call('devices', { kind: 'usb' }).then(function (a) { return a.map(makeUsb); }); };
  defineGetter('usb', usb);

  // ---- WebHID
  function makeHid(d) {
    var h = new EventTarget();
    h.vendorId = d.vendorId | 0; h.productId = d.productId | 0; h.productName = d.name || ''; h.opened = false; h.collections = [];
    h.open = unsupported('HID'); h.close = function () { return Promise.resolve(); };
    h.sendReport = unsupported('HID'); h.sendFeatureReport = unsupported('HID'); h.receiveFeatureReport = unsupported('HID');
    h.forget = forgetter('hid', d); return h;
  }
  var hid = new EventTarget();
  hid.requestDevice = function (o) { try { gesture(); } catch (e) { return Promise.reject(e); }
    return call('hid', o || {}).then(function (d) { return d ? [makeHid(d)] : []; }); };
  hid.getDevices = function () { return call('devices', { kind: 'hid' }).then(function (a) { return a.map(makeHid); }); };
  defineGetter('hid', hid);

  // ---- Web Bluetooth (chooser + grant; GATT is not available)
  function makeBt(d) {
    var b = new EventTarget(); b.id = d.id; b.name = d.name || '';
    b.gatt = { device: b, connected: false, connect: unsupported('Bluetooth GATT'), disconnect: function () {} };
    b.watchAdvertisements = unsupported('Bluetooth'); b.forget = forgetter('bluetooth', d); return b;
  }
  var bt = new EventTarget();
  bt.requestDevice = function (o) { try { gesture(); } catch (e) { return Promise.reject(e); }
    return call('bluetooth', o || {}).then(makeBt); };
  bt.getAvailability = function () { return call('bluetooth-available', {}); };
  bt.getDevices = function () { return call('devices', { kind: 'bluetooth' }).then(function (a) { return a.map(makeBt); }); };
  defineGetter('bluetooth', bt);

  // ---- Web Serial (real I/O through the native QSerialPort bridge)
  function makePort(d) {
    var port = new EventTarget(), token = null;
    port.readable = null; port.writable = null; port.connected = true;
    port.getInfo = function () { var i = {}; if (d.vendorId) { i.usbVendorId = d.vendorId; i.usbProductId = d.productId; } return i; };
    port.open = function (o) {
      if (!o || !o.baudRate) return Promise.reject(new TypeError("Failed to execute 'open' on 'SerialPort': required member baudRate is undefined."));
      if (token) return Promise.reject(new DOMException('The port is already open.', 'InvalidStateError'));
      return call('serial-open', { id: d.id, baudRate: o.baudRate, dataBits: o.dataBits || 8, stopBits: o.stopBits || 1,
        parity: o.parity || 'none', flowControl: o.flowControl || 'none' }).then(function (t) {
        token = t;
        port.readable = new ReadableStream({
          start: function (c) { serialSinks[t] = function (b64) { if (b64 === '') { try { c.close(); } catch (e) {} delete serialSinks[t]; return; } c.enqueue(b64ToBytes(b64)); }; },
          cancel: function () { delete serialSinks[t]; }
        });
        port.writable = new WritableStream({ write: function (chunk) { return call('serial-write', { token: t, data: bytesToB64(chunk) }); } });
      });
    };
    port.setSignals = function (s) { return token ? call('serial-signals', { token: token, signals: s || {} }) : Promise.reject(new DOMException('The port is closed.', 'InvalidStateError')); };
    port.close = function () {
      if (!token) return Promise.resolve();
      var t = token; token = null; var s = serialSinks[t]; if (s) s(''); port.readable = null; port.writable = null;
      return call('serial-close', { token: t }).then(function () {});
    };
    port.forget = forgetter('serial', d);
    return port;
  }
  var serial = new EventTarget();
  serial.requestPort = function (o) { try { gesture(); } catch (e) { return Promise.reject(e); } return call('serial', o || {}).then(makePort); };
  serial.getPorts = function () { return call('devices', { kind: 'serial' }).then(function (a) { return a.map(makePort); }); };
  defineGetter('serial', serial);

  // ---- Web MIDI: Pacitum prompt first, then the real Chromium implementation.
  var _midi = Navigator.prototype.requestMIDIAccess;
  try {
    Object.defineProperty(Navigator.prototype, 'requestMIDIAccess', { configurable: true, writable: true, value: function (o) {
      var self = this;
      return call('midi', { sysex: !!(o && o.sysex) }).then(function () {
        if (!_midi) throw new DOMException('Web MIDI is not available in this build.', 'NotSupportedError');
        return _midi.call(self, o);
      });
    } });
  } catch (e) {}

  // ---- Persistent storage
  if (window.StorageManager && StorageManager.prototype.persist) {
    var _persist = StorageManager.prototype.persist, _persisted = StorageManager.prototype.persisted;
    StorageManager.prototype.persist = function () {
      var self = this;
      return call('storage', {}).then(function (ok) { if (!ok) return false; return _persist.call(self).then(function () { return true; }, function () { return true; }); });
    };
    StorageManager.prototype.persisted = function () {
      var self = this;
      return call('storage-query', {}).then(function (v) { return v === true ? true : _persisted.call(self); }, function () { return _persisted.call(self); });
    };
  }

  // ---- Capture bookkeeping (in-use indicators, "Stop using camera & mic") and tab sharing
  var live = new Set();
  var report = function () {
    var a = 0, v = 0, d = 0;
    live.forEach(function (t) { if (t.__pacitumDisplay) d++; else if (t.kind === 'audio') a++; else v++; });
    notify('capture-state', { audio: a, video: v, display: d });
  };
  var track = function (s, display) {
    s.getTracks().forEach(function (t) {
      if (display) t.__pacitumDisplay = true;
      live.add(t); var _stop = t.stop.bind(t);
      t.stop = function () { live.delete(t); _stop(); report(); };
      t.addEventListener('ended', function () { live.delete(t); report(); });
    });
    report(); return s;
  };
  if (window.MediaDevices && MediaDevices.prototype.getUserMedia) {
    var _gum = MediaDevices.prototype.getUserMedia;
    MediaDevices.prototype.getUserMedia = function (c) { return _gum.call(this, c).then(function (s) { return track(s, false); }); };
    stopHooks.capture.push(function () { live.forEach(function (t) { if (t.__pacitumDisplay) return; t.stop(); try { t.dispatchEvent(new Event('ended')); } catch (e) {} }); });
  }
  if (window.MediaDevices && MediaDevices.prototype.getDisplayMedia) {
    var _gdm = MediaDevices.prototype.getDisplayMedia;
    var tabStream = function (tok) {
      var cv = document.createElement('canvas'); cv.width = 1280; cv.height = 720;
      var g = cv.getContext('2d'), s = cv.captureStream(10), tr = s.getVideoTracks()[0], on = true, busy = false, img = new Image();
      img.onload = function () { if (cv.width !== img.naturalWidth || cv.height !== img.naturalHeight) { cv.width = img.naturalWidth; cv.height = img.naturalHeight; } g.drawImage(img, 0, 0); busy = false; };
      img.onerror = function () { busy = false; };
      var timer = setInterval(function () {
        if (!on || busy) return; busy = true;
        call('tab-frame', { token: tok }).then(function (u) { if (!u) { busy = false; return; } img.src = u; }, function () { stop(); });
      }, 200);
      var _s = tr.stop.bind(tr);
      function stop() { if (!on) return; on = false; clearInterval(timer); delete stopHooks.tabshare[tok]; notify('tab-share-stop', { token: tok }); _s(); try { tr.dispatchEvent(new Event('ended')); } catch (e) {} }
      tr.stop = stop; stopHooks.tabshare[tok] = stop;
      return s;
    };
    MediaDevices.prototype.getDisplayMedia = function (c) {
      return _gdm.call(this, c).then(function (s) { return track(s, true); }, function (err) {
        // The Pacitum picker cancels Qt's request when the user picks a *tab* (Qt can't capture
        // tabs); the browser then serves that tab's frames to a canvas stream.
        return call('tab-share-take', {}).then(function (tok) { if (!tok) throw err; return tabStream(tok); }, function () { throw err; });
      });
    };
  }
})();
