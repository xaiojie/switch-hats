const { listenForBrowser } = require('./browser-test-helpers.cjs');
const test = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
let chromium;
try { ({ chromium } = require('playwright')); } catch {}

async function harness() {
  const devices = ['A', 'B'].map(id => ({ id, name: '主机 ' + id, ip: '192.0.2.' + (id === 'A' ? 1 : 2), connected: true, can_install: true }));
  const fixture = { selected: devices[0], devices, operations: [], downloads: [], listRequests: [], delayedList: null, responseDevice: null, onOperation: null };
  const server = http.createServer(async (req, res) => {
    const url = new URL(req.url, 'http://localhost');
    const json = value => { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(value)); };
    if (url.pathname === '/api/status') return json({ device: fixture.selected, jobs: [], discovery: { running: false } });
    if (url.pathname === '/api/devices') return json({ devices, scanning: false });
    if (url.pathname === '/api/files' && req.method === 'GET') {
      const deviceId = fixture.selected.id;
      fixture.listRequests.push({ expected_device: url.searchParams.get('expected_device'), selected: deviceId });
      const responseDevice = fixture.responseDevice || deviceId;
      const name = fixture.responseDevice ? 'wrong-device.txt' : 'same.txt';
      const reply = () => json({ path: '/', device_id: responseDevice, entries: fixture.empty ? [] : [{ name, path: '/' + name, type: 'file', size: 5 }] });
      if (fixture.delayedList) { fixture.delayedList(reply); fixture.delayedList = null; } else reply();
      return;
    }
    if (url.pathname === '/api/files/download') {
      fixture.downloads.push(Object.fromEntries(url.searchParams));
      res.setHeader('Content-Disposition', 'attachment; filename="same.txt"'); return res.end('hello');
    }
    if (req.method !== 'GET' && url.pathname.startsWith('/api/')) {
      let raw = ''; for await (const chunk of req) raw += chunk;
      const body = req.headers['content-type']?.includes('application/json') ? JSON.parse(raw || '{}') : raw;
      fixture.operations.push({ path: url.pathname, method: req.method, query: Object.fromEntries(url.searchParams), body, selected: fixture.selected.id });
      fixture.onOperation?.();
      if (url.pathname === '/api/files/upload' || url.pathname === '/api/install') { res.statusCode = 202; return json({ ok: true, job: { id: String(fixture.operations.length), kind: url.pathname === '/api/install' ? 'install' : 'upload', state: 'completed', name: url.searchParams.get('name') } }); }
      return json({ ok: true });
    }
    const file = path.join(__dirname, url.pathname === '/' ? 'index.html' : url.pathname);
    if (!file.startsWith(__dirname) || !fs.existsSync(file)) { res.statusCode = 404; return res.end(); }
    res.setHeader('Content-Type', file.endsWith('.css') ? 'text/css' : file.endsWith('.js') ? 'application/javascript' : 'text/html'); res.end(fs.readFileSync(file));
  });
  await listenForBrowser(server);
  const browser = await chromium.launch({ channel: 'msedge', headless: true });
  const page = await browser.newPage();
  const origin = 'http://127.0.0.1:' + server.address().port;
  const reset = async () => { fixture.selected = devices[0]; fixture.operations = []; fixture.downloads = []; fixture.responseDevice = null; fixture.onOperation = null; fixture.empty = false; await page.goto(origin, { waitUntil: 'networkidle' }); await page.locator('.file-row').waitFor(); };
  const switchAndPoll = async () => { fixture.selected = devices[1]; await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))); await page.waitForFunction(() => document.getElementById('device-name').textContent === '主机 B'); };
  return { ...fixture, fixture, page, reset, switchAndPoll, close: async () => { await browser.close(); await new Promise(resolve => server.close(resolve)); } };
}

test('changing the selected device cancels pending delete, rename, mkdir and overwrite confirmations before mutation', { skip: !chromium, timeout: 35000 }, async () => {
  const h = await harness();
  try {
    for (const action of ['delete', 'rename', 'mkdir', 'overwrite']) {
      await h.reset();
      if (action === 'delete' || action === 'rename') await h.page.getByRole('checkbox', { name: '选择 same.txt' }).check();
      if (action === 'overwrite') await h.page.locator('#upload-input').setInputFiles({ name: 'same.txt', mimeType: 'text/plain', buffer: Buffer.from('hello') });
      else await h.page.locator('#' + (action === 'mkdir' ? 'mkdir' : action) + '-button').click();
      await h.page.locator('#action-dialog[open]').waitFor();
      await h.switchAndPoll();
      assert.equal(await h.page.locator('#action-dialog').isVisible(), false, action + ' confirmation must close on device change');
      assert.equal(h.fixture.operations.length, 0, 'old intent must not mutate the new device');
      assert.match(await h.page.locator('#toasts').innerText(), /主机.*(切换|变化|更换)/);
    }
  } finally { await h.close(); }
});

test('file list, download and every mutation carry the captured expected_device even before a tab observes a switch', { skip: !chromium, timeout: 35000 }, async () => {
  const h = await harness();
  try {
    for (const action of ['delete', 'rename', 'mkdir']) {
      await h.reset();
      assert.equal(h.fixture.listRequests.at(-1).expected_device, 'A');
      if (action !== 'mkdir') await h.page.getByRole('checkbox', { name: '选择 same.txt' }).check();
      await h.page.locator('#' + (action === 'mkdir' ? 'mkdir' : action) + '-button').click();
      if (action !== 'delete') await h.page.locator('#action-input').fill('new-name');
      // Change server selection without polling this tab: the original device token
      // must still accompany the request for the backend to reject atomically.
      h.fixture.selected = h.fixture.devices[1];
      const sent = h.page.waitForRequest(request => request.method() !== 'GET' && request.url().includes('/api/files'));
      await h.page.locator('#action-submit').click(); await sent;
      const operation = h.fixture.operations.at(-1);
      assert.equal(operation?.method === 'DELETE' ? operation.query.expected_device : operation?.body.expected_device, 'A', action + ' must carry the original device id');
    }
    await h.reset();
    const downloaded = h.page.waitForEvent('download');
    await h.page.getByRole('button', { name: '下载 same.txt', exact: true }).click(); await downloaded;
    assert.equal(h.fixture.downloads.at(-1).expected_device, 'A');
    for (const kind of ['upload']) {
      await h.reset();
      h.fixture.onOperation = () => { h.fixture.selected = h.fixture.devices[1]; };
      await h.page.locator('#' + kind + '-input').setInputFiles(['first', 'second'].map(name => ({ name: name + (kind === 'install' ? '.nsp' : '.txt'), mimeType: 'application/octet-stream', buffer: Buffer.from('hello') })));
      await h.page.waitForFunction(() => document.querySelector('#toasts').textContent.includes('已加入'));
      assert.equal(h.fixture.operations.length, 2);
      assert.deepEqual(h.fixture.operations.map(operation => operation.query.expected_device), ['A', 'A'], kind + ' batch must keep its original device id');
    }
  } finally { await h.close(); }
});

test('a response identifying a different device is never rendered as the current device listing', { skip: !chromium, timeout: 15000 }, async () => {
  const h = await harness();
  try {
    await h.reset();
    h.fixture.responseDevice = 'B';
    const response = h.page.waitForResponse(reply => new URL(reply.url()).pathname === '/api/files');
    await h.page.locator('#refresh-files').click(); await response;
    await h.page.waitForFunction(() => document.getElementById('file-area').getAttribute('aria-busy') === 'false');
    assert.equal(await h.page.getByRole('button', { name: 'wrong-device.txt', exact: true }).count(), 0);
    assert.equal(await h.page.locator('#device-name').innerText(), '主机 A');
    assert.match(await h.page.locator('#file-empty').innerText(), /主机连接已变化/);
  } finally { await h.close(); }
});

test('a valid but delayed A listing cannot replace B after polling changes the current device', { skip: !chromium, timeout: 15000 }, async () => {
  const h = await harness();
  let delayedRoute;
  try {
    await h.reset();
    let pending;
    const intercepted = new Promise(resolve => { pending = resolve; });
    await h.page.route('**/api/files?*', route => { if (!delayedRoute) { delayedRoute = route; pending(); } else route.continue(); });
    await h.page.locator('#refresh-files').click(); await intercepted;
    await h.switchAndPoll();
    await h.page.waitForFunction(() => document.getElementById('file-area').getAttribute('aria-busy') === 'false');
    const delayedResponse = h.page.waitForResponse(response => response.url() === delayedRoute.request().url());
    await delayedRoute.fulfill({ contentType: 'application/json', body: JSON.stringify({ path: '/', device_id: 'A', entries: [{ name: 'late-A.txt', path: '/late-A.txt', type: 'file', size: 2 }] }) });
    await (await delayedResponse).finished();
    await h.page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
    await h.page.waitForFunction(() => document.getElementById('device-name').textContent === '主机 B');
    assert.equal(await h.page.getByRole('button', { name: 'late-A.txt', exact: true }).count(), 0);
    assert.equal(await h.page.getByRole('button', { name: 'same.txt', exact: true }).count(), 1);
  } finally { await h.close(); }
});

test('an SD file chooser opened on A cannot upload on B after selection changes', { skip: !chromium, timeout: 25000 }, async () => {
  const h = await harness();
  try {
    for (const entryPoint of ['upload', 'empty-upload']) {
      await h.reset();
      const kind = entryPoint === 'install' ? 'install' : 'upload';
      if (entryPoint === 'empty-upload') { h.fixture.empty = true; await h.page.locator('#refresh-files').click(); await h.page.getByRole('button', { name: '上传第一个文件', exact: true }).waitFor(); }
      if (kind === 'install') await h.page.locator('[data-page=install]').click();
      const chooserEvent = h.page.waitForEvent('filechooser');
      if (entryPoint === 'empty-upload') await h.page.getByRole('button', { name: '上传第一个文件', exact: true }).click();
      else await h.page.locator(kind === 'upload' ? '#upload-button' : '#install-choose').click();
      const chooser = await chooserEvent;
      await h.switchAndPoll();
      await chooser.setFiles({ name: kind === 'install' ? 'game.nsp' : 'new.txt', mimeType: 'application/octet-stream', buffer: Buffer.from('hello') });
      await h.page.waitForFunction(() => /主机|已加入/.test(document.querySelector('#toasts').textContent));
      assert.equal(h.fixture.operations.length, 0, kind + ' picker must keep its original target');
    }
  } finally { await h.close(); }
});
