'use strict';

/* ==========================================================================
   T-Display-S3 NAV — 前端控制台
   ========================================================================== */

const $ = (id) => document.getElementById(id);

function fmtBytes(n) {
  if (n === undefined || n === null) return '—';
  if (n >= 1073741824) return (n / 1073741824).toFixed(2) + ' GB';
  if (n >= 1048576) return (n / 1048576).toFixed(2) + ' MB';
  if (n >= 1024) return (n / 1024).toFixed(1) + ' KB';
  return n + ' B';
}

function fmtDuration(sec) {
  sec = Math.max(0, Math.round(sec || 0));
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  if (h > 0) return h + ' 時 ' + String(m).padStart(2, '0') + ' 分';
  if (m > 0) return m + ' 分';
  return sec + ' 秒';
}

function fmtUptime(sec) {
  sec = Math.max(0, Math.round(sec || 0));
  const d = Math.floor(sec / 86400);
  const h = Math.floor((sec % 86400) / 3600);
  const m = Math.floor((sec % 3600) / 60);
  if (d > 0) return d + ' 天 ' + String(h).padStart(2, '0') + ':' + String(m).padStart(2, '0');
  return String(h).padStart(2, '0') + ':' + String(m).padStart(2, '0') + ':' + String(sec % 60).padStart(2, '0');
}

function fmtDist(m) {
  if (m === undefined || m === null) return '—';
  if (m < 1000) return Math.round(m) + ' m';
  if (m < 10000) return (m / 1000).toFixed(2) + ' km';
  return (m / 1000).toFixed(1) + ' km';
}

function setMsg(el, text, kind) {
  if (!el) return;
  el.textContent = text || '';
  el.className = 'msg' + (kind ? ' ' + kind : '');
}

async function api(path, options) {
  const res = await fetch(path, Object.assign({
    headers: { 'Content-Type': 'application/json' },
    cache: 'no-store'
  }, options || {}));
  const text = await res.text();
  let data = null;
  try { data = text ? JSON.parse(text) : null; } catch (_) { data = { ok: false, msg: text }; }
  if (!res.ok) {
    throw new Error((data && data.msg) ? data.msg : ('HTTP ' + res.status));
  }
  return data;
}

/* ------------------------------------------------------------------ tabs */
function switchTab(name) {
  document.querySelectorAll('.tab').forEach((b) => {
    b.classList.toggle('active', b.dataset.tab === name);
  });
  document.querySelectorAll('.panel').forEach((p) => {
    p.classList.toggle('active', p.id === 'tab-' + name);
  });
  if (name === 'nav') {
    initMap();
    setTimeout(() => { if (map) map.invalidateSize(); }, 80);
  }
  if (name === 'status') loadSpiffs();
  if (history.replaceState) history.replaceState(null, '', '#' + name);
}

/* ------------------------------------------------------------------ status */
let statusTimer = null;

async function loadStatus() {
  try {
    const st = await api('/api/status');
    renderStatus(st);
    renderNavLive(st.nav);
    return st;
  } catch (err) {
    $('sideState').textContent = '离线';
    $('sideState').classList.add('off');
    return null;
  }
}

function renderStatus(st) {
  if (!st || !st.ok) return;
  const w = st.wifi || {};
  const bat = st.battery || {};
  const mem = st.heap || {};
  const fs = st.spiffs || {};
  const fw = st.fw || {};

  $('sideState').textContent = w.connected ? 'WiFi 已連線' : (w.apActive ? 'AP 模式' : '未連線');
  $('sideState').classList.toggle('off', !w.connected);
  $('sideIp').textContent = w.connected ? w.ip : (w.apIp || '—');

  $('sWifiState').textContent = w.state || '—';
  $('sSsid').textContent = w.ssid || '—';
  $('sIp').textContent = w.connected ? w.ip : (w.apIp || '—');
  $('sRssi').textContent = w.connected ? (w.rssi + ' dBm') : '—';
  $('sChannel').textContent = w.connected ? (w.channel || '—') : '—';
  $('sMac').textContent = w.mac || '—';
  $('sHost').textContent = w.host || '—';

  const pct = bat.present ? (bat.percent || 0) : 0;
  $('batPct').textContent = bat.present ? pct + '%' : 'USB';
  $('batVolt').textContent = bat.present ? ('電壓 ' + (bat.voltage || 0).toFixed(2) + ' V') : '無電池，USB 供電';
  $('batNote').textContent = bat.present ? '' : '未偵測到電池';
  const fill = $('batFill');
  fill.style.width = pct + '%';
  fill.style.background = pct > 50 ? 'var(--green)' : (pct > 20 ? 'var(--yellow)' : 'var(--red)');

  $('sHeap').textContent = fmtBytes(mem.free);
  $('sHeapMin').textContent = fmtBytes(mem.min);
  $('sPsram').textContent = mem.psram ? fmtBytes(mem.psram) : '無';
  $('sFlash').textContent = fmtBytes(mem.flash);
  $('sChip').textContent = mem.chip || '—';
  $('sCpu').textContent = (mem.cpu || '—') + ' MHz';

  $('sFwName').textContent = fw.name || '—';
  $('sFwVer').textContent = fw.version || '—';
  $('sFwBuild').textContent = fw.build || '—';
  $('sCore').textContent = fw.core || '—';
  $('sUptime').textContent = fmtUptime(st.uptime);
  $('sReset').textContent = mem.reset || '—';
  $('sBright').textContent = (st.brightness || 0) + '%';

  const used = fs.used || 0;
  const total = fs.total || 1;
  $('fsFill').style.width = Math.min(100, used / total * 100).toFixed(1) + '%';
  $('fsText').textContent = fmtBytes(used) + ' / ' + fmtBytes(total) +
    '（剩餘 ' + fmtBytes(total - used) + '）';

  $('wState').textContent = w.state || '—';
  $('wSsid').textContent = w.ssid || '—';
  $('wIp').textContent = w.connected ? w.ip : (w.apIp || '—');
  $('wApSsid').textContent = w.apSsid || '—';
  $('wApPass').textContent = w.apPass || '—';

  $('statusTime').textContent = '更新於 ' + new Date().toLocaleTimeString('zh-TW', { hour12: false });
}

/* ------------------------------------------------------------------ spiffs */
let spiffsTimer = null;

async function loadSpiffs() {
  const body = $('fsList');
  try {
    const data = await api('/api/spiffs');
    const files = (data.files || []).slice().sort((a, b) => a.path.localeCompare(b.path));
    if (!files.length) {
      body.innerHTML = '<tr><td colspan="2" class="mini">SPIFFS 是空的，請上傳 data/。</td></tr>';
      return;
    }
    body.innerHTML = files.map((f) =>
      '<tr><td class="mono">' + escapeHtml(f.path) + '</td>' +
      '<td class="num">' + fmtBytes(f.size) + '</td></tr>').join('');
  } catch (err) {
    body.innerHTML = '<tr><td colspan="2" class="mini">無法讀取：' + escapeHtml(err.message) + '</td></tr>';
  }
}

function escapeHtml(s) {
  return String(s === undefined ? '' : s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

/* ------------------------------------------------------------------ wifi */
function signalBars(rssi) {
  if (rssi >= -55) return '▂▄▆█';
  if (rssi >= -65) return '▂▄▆_';
  if (rssi >= -75) return '▂▄__';
  return '▂___';
}

function renderScan(nets) {
  const box = $('scanList');
  $('scanCount').textContent = nets.length ? '（' + nets.length + ' 個）' : '';
  if (!nets.length) {
    box.innerHTML = '<div class="mini">沒有找到網路。</div>';
    return;
  }
  box.innerHTML = nets.map((n) =>
    '<div class="scan-item" data-ssid="' + escapeHtml(n.ssid) + '">' +
      '<div><div class="nm">' + escapeHtml(n.ssid) + '</div>' +
      '<div class="meta">' + escapeHtml(n.enc) + ' · CH ' + n.channel + ' · ' + n.rssi + ' dBm</div></div>' +
      '<div class="sig">' + signalBars(n.rssi) + '</div>' +
    '</div>').join('');

  box.querySelectorAll('.scan-item').forEach((el) => {
    el.onclick = () => {
      $('ssid').value = el.dataset.ssid;
      $('pass').focus();
      setMsg($('wifiMsg'), '已選擇「' + el.dataset.ssid + '」，請輸入密碼後按連線。');
    };
  });
}

async function scanWifi() {
  const btn = $('btnScan');
  btn.disabled = true;
  setMsg($('wifiMsg'), '掃描中，裝置螢幕同步顯示進度…');
  try {
    const data = await api('/api/wifi/scan');
    renderScan(data.networks || []);
    setMsg($('wifiMsg'), '掃描完成，點選網路即可填入 SSID。', 'ok');
  } catch (err) {
    setMsg($('wifiMsg'), '掃描失敗：' + err.message, 'err');
  } finally {
    btn.disabled = false;
  }
}

async function connectWifi() {
  const ssid = $('ssid').value.trim();
  const pass = $('pass').value;
  if (!ssid) {
    setMsg($('wifiMsg'), '請輸入或選擇 SSID。', 'err');
    return;
  }
  const btn = $('btnConnect');
  btn.disabled = true;
  setMsg($('wifiMsg'), '連線中，最多等待 15 秒（請同時查看裝置螢幕）…');
  try {
    const data = await api('/api/wifi', {
      method: 'POST',
      body: JSON.stringify({ ssid: ssid, pass: pass })
    });
    setMsg($('wifiMsg'), '✓ 已連線 ' + ssid + '（' + (data.ip || '') + '）', 'ok');
    await loadStatus();
  } catch (err) {
    setMsg($('wifiMsg'), '✗ ' + err.message, 'err');
  } finally {
    btn.disabled = false;
    $('pass').value = '';
  }
}

async function forgetWifi() {
  if (!confirm('確定要清除已儲存的 WiFi 設定嗎？裝置會重新啟動並進入 AP 模式。')) return;
  try {
    await api('/api/wifi', { method: 'DELETE' });
    setMsg($('wifiMsg'), '已清除，裝置重新啟動中…（請重新連線 AP）', 'ok');
  } catch (err) {
    setMsg($('wifiMsg'), '✕ ' + err.message, 'err');
  }
}

/* ------------------------------------------------------------------ map */
let map = null;
let mapReady = false;
let mode = 'dest';
let startPt = null, destPt = null, mePt = null;
let startMarker = null, destMarker = null, meMarker = null, routeLine = null;
let destName = '';

const osmLayer = 'https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png';
const TPE = [24.1477, 120.6736];

function initMap() {
  if (mapReady) return;
  mapReady = true;

  if (typeof L === 'undefined') {
    $('map').innerHTML = '<div class="mini" style="padding:18px;line-height:1.8">' +
      '地圖元件未載入（Leaflet CDN 需要網際網路）。<br>' +
      'WiFi 設定、系統狀態與 OTA 更新仍可正常使用；' +
      '請先讓手機 / 電腦連上有網際網路的路由器，再開啟本頁。</div>';
    return;
  }

  map = L.map('map', { zoomControl: true, attributionControl: true }).setView(TPE, 8);

  L.tileLayer(osmLayer, {
    maxZoom: 19,
    attribution: '&copy; OpenStreetMap contributors'
  }).addTo(map);

  map.on('click', (e) => {
    if (mode === 'start') setStart(e.latlng);
    else if (mode === 'me') setMe(e.latlng);
    else setDest(e.latlng);
  });
}

function pointText(p) {
  return p ? (p.lat.toFixed(5) + ', ' + p.lng.toFixed(5)) : '未設定';
}

function markerIcon(color) {
  return L.divIcon({
    className: '',
    html: '<div style="width:14px;height:14px;border-radius:50%;border:2px solid #fff;' +
          'background:' + color + ';box-shadow:0 0 6px rgba(0,0,0,.6)"></div>',
    iconSize: [14, 14],
    iconAnchor: [7, 7]
  });
}

function setStart(p) {
  if (!map) { setMsg($('routeMsg'), '地圖未載入，無法設定座標。', 'err'); return; }
  startPt = p;
  if (startMarker) map.removeLayer(startMarker);
  startMarker = L.marker(p, { icon: markerIcon('#34d399'), draggable: true }).addTo(map);
  startMarker.on('dragend', () => { startPt = startMarker.getLatLng(); $('startText').textContent = pointText(startPt); });
  $('startText').textContent = pointText(p) + '（起點）';
}

function setDest(p, name) {
  if (!map) { setMsg($('routeMsg'), '地圖未載入，無法設定座標。', 'err'); return; }
  destPt = p;
  destName = name || '地圖選點';
  if (destMarker) map.removeLayer(destMarker);
  destMarker = L.marker(p, { icon: markerIcon('#f87171'), draggable: true }).addTo(map);
  destMarker.bindPopup(escapeHtml(destName));
  destMarker.on('dragend', () => {
    destPt = destMarker.getLatLng();
    $('destText').textContent = pointText(destPt);
  });
  $('destText').textContent = pointText(p) + (name ? '（' + name + '）' : '（終點）');
}

async function setMe(p) {
  if (!map) { setMsg($('routeMsg'), '地圖未載入，無法顯示位置。', 'err'); return; }
  mePt = p;
  if (meMarker) map.removeLayer(meMarker);
  meMarker = L.circleMarker(p, {
    radius: 7, color: '#22d3ee', weight: 3, fillColor: '#0e7490', fillOpacity: 0.9
  }).addTo(map);
  meMarker.bindPopup('目前位置');

  try {
    const d = await api('/api/nav/position', {
      method: 'POST',
      body: JSON.stringify({ lat: p.lat, lon: p.lng })
    });
    setMsg($('routeMsg'), d.active ? '位置已送出，裝置已更新導航。' : '位置已送出（尚未規劃路線）。', 'ok');
  } catch (err) {
    setMsg($('routeMsg'), '位置送出失敗：' + err.message, 'err');
  }
}

function decodePolyline(str, precision) {
  const factor = Math.pow(10, precision === undefined ? 5 : precision);
  const coords = [];
  let index = 0, lat = 0, lng = 0;

  while (index < str.length) {
    let shift = 0, result = 0, b;
    do {
      b = str.charCodeAt(index++) - 63;
      result |= (b & 0x1f) << shift;
      shift += 5;
    } while (b >= 0x20);
    lat += (result & 1) ? ~(result >> 1) : (result >> 1);

    shift = 0; result = 0;
    do {
      b = str.charCodeAt(index++) - 63;
      result |= (b & 0x1f) << shift;
      shift += 5;
    } while (b >= 0x20);
    lng += (result & 1) ? ~(result >> 1) : (result >> 1);

    coords.push([lat / factor, lng / factor]);
  }
  return coords;
}

async function buildRoute() {
  initMap();
  if (!startPt || !destPt) {
    setMsg($('routeMsg'), '請先在地圖設定起點與終點（或使用搜尋）。', 'err');
    return;
  }
  const btn = $('btnRoute');
  btn.disabled = true;
  setMsg($('routeMsg'), '向 OSRM 查詢路線中…（裝置會同步顯示）');

  try {
    const r = await api('/api/nav/route', {
      method: 'POST',
      body: JSON.stringify({
        start: { lat: startPt.lat, lon: startPt.lng },
        dest: { lat: destPt.lat, lon: destPt.lng },
        name: destName || '目的地'
      })
    });
    drawRoute(r);
    setMsg($('routeMsg'), '✓ 路線已建立，並已顯示在裝置上。', 'ok');
  } catch (err) {
    setMsg($('routeMsg'), '✕ ' + err.message, 'err');
  } finally {
    btn.disabled = false;
  }
}

function renderSteps(list) {
  const box = $('navSteps');
  if (!list || !list.length) {
    box.innerHTML = '<div class="mini">沒有轉彎指示。</div>';
    return;
  }
  box.innerHTML = list.map((s, i) =>
    '<div class="step-item" data-i="' + i + '">' +
      '<span>' + (i + 1) + '. ' + escapeHtml(s.instruction || '') + '</span>' +
      '<span class="dist">' + fmtDist(s.distance) + '</span>' +
    '</div>').join('');

  box.querySelectorAll('.step-item').forEach((el) => {
    el.onclick = () => {
      const s = list[+el.dataset.i];
      if (s && s.lat && s.lon && map) map.setView([s.lat, s.lon], 16);
    };
  });
}

function drawRoute(r) {
  initMap();
  if (map && routeLine) { map.removeLayer(routeLine); routeLine = null; }

  const coords = map ? decodePolyline(r.geometry || '', 5) : [];
  if (map && coords.length > 1) {
    routeLine = L.polyline(coords, { color: '#22d3ee', weight: 5, opacity: 0.9 }).addTo(map);
    map.fitBounds(routeLine.getBounds(), { padding: [26, 26] });
  }

  $('routeDist').textContent = fmtDist(r.routeDistance);
  $('routeTime').textContent = fmtDuration(r.routeDuration);
  $('routeSummary').classList.remove('hidden');

  if (r.destName) {
    destName = r.destName;
    $('destText').textContent = r.destName;
  }

  renderSteps(r.list || []);
  renderNavLive(r);
}

function renderNavLive(nav) {
  if (!nav) return;
  const arrow = document.querySelector('.live-arrow');
  const live = $('navLive');

  if (nav.active) {
    $('navInstr').textContent = nav.arrived ? '已抵達目的地' : (nav.instruction || '—');
    $('navMeta').textContent = '剩餘 ' + fmtDist(nav.remaining) + ' / ' +
      fmtDuration(nav.remainingTime) + ' · 步驟 ' + (nav.step || 0) + '/' + (nav.steps || 0);
    if (arrow) arrow.style.transform = 'rotate(' + (nav.bearing || 0).toFixed(0) + 'deg)';
    if (live) live.classList.add('moving');
  } else {
    $('navInstr').textContent = '尚未規劃路線';
    $('navMeta').textContent = '設定起點與終點後按「規劃路線」';
    if (arrow) arrow.style.transform = 'rotate(0deg)';
    if (live) live.classList.remove('moving');
  }
}

async function clearRoute() {
  try {
    await api('/api/nav', { method: 'DELETE' });
    if (map && routeLine) { map.removeLayer(routeLine); routeLine = null; }
    $('routeSummary').classList.add('hidden');
    $('navSteps').innerHTML = '';
    renderNavLive({ active: false });
    setMsg($('routeMsg'), '已清除路線。', 'ok');
  } catch (err) {
    setMsg($('routeMsg'), '✕ ' + err.message, 'err');
  }
}

function locateMe() {
  if (!navigator.geolocation) {
    setMsg($('routeMsg'), '此瀏覽器不支援定位。', 'err');
    return;
  }
  setMsg($('routeMsg'), '取得定位中…');
  navigator.geolocation.getCurrentPosition(
    (pos) => {
      initMap();
      if (!map) {
        setMsg($('routeMsg'), '地圖未載入，無法顯示定位。', 'err');
        return;
      }
      const p = L.latLng(pos.coords.latitude, pos.coords.longitude);
      setMe(p);
      map.setView(p, 15);
      if (pos.coords.heading) {
        api('/api/nav/position', {
          method: 'POST',
          body: JSON.stringify({ lat: p.lat, lon: p.lng, heading: pos.coords.heading })
        }).catch(() => {});
      }
    },
    () => {
      setMsg($('routeMsg'), '定位失敗：瀏覽器僅在 HTTPS 或 localhost 允許定位。請改用地圖上的「模擬位置」。', 'err');
    },
    { enableHighAccuracy: true, timeout: 9000, maximumAge: 5000 }
  );
}

async function doSearch() {
  const q = $('search').value.trim();
  if (!q) return;
  const box = $('searchResults');

  if (!map) {
    box.innerHTML = '<div class="mini">地圖未載入（需要網際網路）。</div>';
    return;
  }

  box.innerHTML = '<div class="mini">搜尋中…</div>';

  try {
    const url = 'https://nominatim.openstreetmap.org/search?format=jsonv2&limit=6' +
                '&accept-language=zh-TW&q=' + encodeURIComponent(q);
    const res = await fetch(url, { headers: { 'Accept': 'application/json' } });
    const list = await res.json();

    if (!Array.isArray(list) || !list.length) {
      box.innerHTML = '<div class="mini">找不到地點。</div>';
      return;
    }

    box.innerHTML = list.map((it, i) =>
      '<button class="search-hit" data-i="' + i + '">' + escapeHtml(it.display_name) + '</button>'
    ).join('');

    box.querySelectorAll('.search-hit').forEach((btn) => {
      btn.onclick = () => {
        const it = list[+btn.dataset.i];
        const p = L.latLng(parseFloat(it.lat), parseFloat(it.lon));
        initMap();
        if (mode === 'start') setStart(p);
        else setDest(p, it.display_name.split(',')[0]);
        map.setView(p, 14);
        box.innerHTML = '';
      };
    });
  } catch (err) {
    box.innerHTML = '<div class="mini">搜尋失敗（需要網際網路，AP 模式下通常無法使用）。</div>';
  }
}

/* ------------------------------------------------------------------ OTA */
function uploadBinary(kind, inputEl, barEl, msgEl, btnEl) {
  const file = inputEl.files && inputEl.files[0];
  if (!file) {
    setMsg(msgEl, '請先選擇 .bin 檔案。', 'err');
    return;
  }

  const fd = new FormData();
  fd.append('update', file, file.name);

  btnEl.disabled = true;
  barEl.style.width = '0%';
  setMsg(msgEl, '準備上傳 ' + file.name + '（' + fmtBytes(file.size) + '）…');

  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/ota/' + kind + '?size=' + file.size, true);

  xhr.upload.onprogress = (e) => {
    if (!e.lengthComputable) return;
    const p = e.loaded / e.total * 100;
    barEl.style.width = p.toFixed(1) + '%';
    setMsg(msgEl, '上傳中 ' + p.toFixed(0) + '%（' + fmtBytes(e.loaded) + ' / ' + fmtBytes(e.total) + '）');
  };

  xhr.onload = () => {
    btnEl.disabled = false;
    let data = null;
    try { data = JSON.parse(xhr.responseText); } catch (_) { data = null; }

    if (xhr.status >= 200 && xhr.status < 300 && data && data.ok) {
      barEl.style.width = '100%';
      setMsg(msgEl, '✓ 更新完成，裝置重新啟動中，請稍候再重新整理。', 'ok');
    } else {
      setMsg(msgEl, '✕ 更新失敗：' + ((data && data.msg) || ('HTTP ' + xhr.status)), 'err');
    }
  };

  xhr.onerror = () => {
    btnEl.disabled = false;
    setMsg(msgEl, '✕ 連線中斷（若裝置已重新啟動，通常代表更新成功）。', 'err');
  };

  xhr.send(fd);
}

async function rebootDevice() {
  if (!confirm('確定要重新啟動裝置嗎？')) return;
  try {
    await api('/api/system/reboot', { method: 'POST' });
    $('sideState').textContent = '重新啟動中…';
  } catch (err) {
    alert('重新啟動失敗：' + err.message);
  }
}

/* ------------------------------------------------------------------ init */
function bindTabs() {
  document.querySelectorAll('.tab').forEach((btn) => {
    btn.addEventListener('click', () => switchTab(btn.dataset.tab));
  });

  if (location.hash) {
    const name = location.hash.replace('#', '');
    if (document.getElementById('tab-' + name)) switchTab(name);
  }
}

function bindNav() {
  document.querySelectorAll('.mode').forEach((btn) => {
    btn.addEventListener('click', () => {
      mode = btn.dataset.mode;
      document.querySelectorAll('.mode').forEach((b) => b.classList.toggle('active', b === btn));
      const hints = {
        dest: '在地圖上點一下設定終點，再按「規劃路線」。',
        start: '在地圖上點一下設定起點。',
        me: '在地圖上點一下，模擬目前位置並傳給裝置。'
      };
      $('mapHint').textContent = hints[mode] || '';
    });
  });

  $('btnRoute').addEventListener('click', buildRoute);
  $('btnClearRoute').addEventListener('click', clearRoute);
  $('btnLocate').addEventListener('click', locateMe);
  $('btnSearch').addEventListener('click', doSearch);
  $('search').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); doSearch(); }
  });
}

function bindWifi() {
  $('btnScan').addEventListener('click', scanWifi);
  $('btnConnect').addEventListener('click', connectWifi);
  $('btnForget').addEventListener('click', forgetWifi);
  $('pass').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); connectWifi(); }
  });
}

function bindOta() {
  $('btnFw').addEventListener('click', () =>
    uploadBinary('firmware', $('fwFile'), $('fwBar'), $('fwMsg'), $('btnFw')));
  $('btnFs').addEventListener('click', () =>
    uploadBinary('spiffs', $('fsFile'), $('fsBar'), $('fsMsg'), $('btnFs')));
  $('btnReboot').addEventListener('click', rebootDevice);
}

function startPolling() {
  if (statusTimer) clearInterval(statusTimer);
  statusTimer = setInterval(() => {
    loadStatus();
  }, 4000);

  setInterval(() => {
    if (document.querySelector('#tab-status.active')) loadSpiffs();
  }, 20000);
}

document.addEventListener('DOMContentLoaded', () => {
  bindTabs();
  bindWifi();
  bindNav();
  bindOta();

  $('btnRefresh').addEventListener('click', async () => {
    await loadStatus();
    await loadSpiffs();
  });

  loadStatus().then(() => {
    if (document.querySelector('#tab-nav.active')) initMap();
  });
  loadSpiffs();
  startPolling();
});
