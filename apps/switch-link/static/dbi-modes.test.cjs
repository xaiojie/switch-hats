const { listenForBrowser } = require('./browser-test-helpers.cjs');
const test = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

test('DBI can switch modes on one address without stale file actions or false disconnection', { timeout: 35000 }, async () => {
  let device = { id: 'ftp-192.0.2.75', name: '我的 Switch', ip: '192.0.2.75', port: 5000,
    connected: true, mode: 'dbi', can_browse: true, can_install: false };
  let fileReads = 0;
  const mutations = [];
  const server = http.createServer(async (req, res) => {
    const url = new URL(req.url, 'http://localhost');
    const json = data => { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(data)); };
    if (url.pathname === '/api/status') return json({ device, jobs: [], discovery: {} });
    if (url.pathname === '/api/library') return json({ available: false, can_publish: false, files: [], transfers: [] });
    if (url.pathname === '/api/devices') return json({ devices: [device] });
    if (url.pathname === '/api/files') { fileReads++; return json({ device_id: device.id, path: '/', entries: [{ name: 'owned.txt', path: '/owned.txt', type: 'file', size: 1 }] }); }
    if (url.pathname.startsWith('/api/')) { mutations.push(url.pathname); return json({ ok: true }); }
    const file = path.join(__dirname, url.pathname === '/' ? 'index.html' : url.pathname);
    if (!file.startsWith(__dirname) || !fs.existsSync(file)) { res.statusCode = 404; return res.end(); }
    res.setHeader('Content-Type', file.endsWith('.js') ? 'application/javascript' : file.endsWith('.css') ? 'text/css' : 'text/html');
    res.end(fs.readFileSync(file));
  });
  await listenForBrowser(server);
  let browser;
  try {
    browser = await chromium.launch({ channel: 'msedge', headless: true });
    const page = await browser.newPage();
    const errors = []; page.on('pageerror', e => errors.push(e.message));
    const origin = 'http://127.0.0.1:' + server.address().port;
    await page.goto(origin, { waitUntil: 'networkidle' });
    await page.locator('.file-row').waitFor();
    await page.locator('#mkdir-button').click();
    await page.locator('#action-dialog[open]').waitFor();
    const previousReads = fileReads;
    device = { ...device, mode: 'dbi-install', can_browse: false, can_install: true };
    await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange')));
    await page.waitForFunction(() => document.getElementById('upload-button').disabled);
    assert.equal(await page.locator('#action-dialog').isVisible(), false, 'a pending file confirmation must close on mode change');
    assert.equal(await page.locator('#upload-button').isDisabled(), true);
    assert.equal(await page.locator('#mkdir-button').isDisabled(), true);
    assert.equal(await page.locator('.file-row:visible').count(), 0);
    assert.match(await page.locator('#file-empty').innerText(), /不提供 SD 卡文件管理/);
    assert.equal(fileReads, previousReads, 'install-only root must not be browsed as the SD card');
    assert.match(await page.locator('#device-description').innerText(), /DBI.*安装/);
    assert.equal(await page.locator('#install-choose').isDisabled(), true, 'legacy FTP installer never enables native HTTP installation');
    assert.equal(mutations.length, 0);
    await page.locator('[data-page=install]').click();
    assert.equal(await page.locator('#install-choose').isDisabled(), true);

    // A fresh page starts successfully while DBI is already an installer.
    await page.reload({ waitUntil: 'networkidle' });
    await page.waitForFunction(() => document.getElementById('upload-button').disabled);
    assert.equal(fileReads, previousReads);
    assert.equal(await page.locator('#service-error').isVisible(), false);

    // Returning to Browse SD re-enables management without changing IP or ID.
    device = { ...device, mode: 'dbi', can_browse: true, can_install: false };
    await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange')));
    await page.waitForFunction(() => !document.getElementById('upload-button').disabled);
    await page.locator('[data-page=sd]').click();
    await page.locator('.file-row').waitFor();
    assert.equal(await page.locator('#install-choose').isDisabled(), true);
    assert.ok(fileReads > previousReads);
    assert.deepEqual(errors, []);
  } finally { await browser?.close(); await new Promise(resolve => server.close(resolve)); }
});
