const { listenForBrowser } = require('./browser-test-helpers.cjs');
const test = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
let chromium;
try { ({ chromium } = require('playwright')); } catch {}
const screenshotDir = process.env.SWITCH_LINK_SCREENSHOTS;
async function capture(page, name) { if (screenshotDir) { fs.mkdirSync(screenshotDir, { recursive: true }); await page.screenshot({ path: path.join(screenshotDir, name), fullPage: true }); } }

test('real browser: discover, browse safely, preserve selection while polling, and distinguish install confirmation', { skip: !chromium, timeout: 45000 }, async () => {
  const device = { id: 'test-switch', name: '测试主机', ip: '192.0.2.30', port: 5000, connected: true, mode:'switch-link', app_version:'0.2.1', http_install_supported:true, http_install_ready:true, http_formats:['nsp','xci'], can_install: false, install_reason: '请在主机上打开 Switch Link。', free_bytes: 100000000000, total_bytes: 256000000000 };
  const library = { available: true, can_publish: true, library_id: 'ui-library', url:'http://192.0.2.169:8080/files/', config:'SwitchLibrary=ApacheHTTP|http://192.0.2.169:8080/files/', files:[], transfers:[], progress_available:false };
  let selected = null;
  let uploaded = null;
  let uploadJob = null;
  let installation = null;
  const entries = [{ name: 'switch', path: '/switch', type: 'directory' }, { name: '<img src=x onerror=alert(1)>.txt', path: '/<img src=x onerror=alert(1)>.txt', type: 'file', size: 2000, modified: 1780000000 }];
  const jobs = [{ id: 'pending', name: '待确认.nsp', kind: 'install', state: 'awaiting_confirmation', bytes_done: 500, bytes_total: 500, progress: 100, speed: 0, error: '', created_at: 1780000000 }];
  const requests = [];
  const server = http.createServer(async (req, res) => {
    const url = new URL(req.url, 'http://localhost'); requests.push([req.method, url.pathname]);
    const json = body => { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(body)); };
    if (url.pathname === '/api/status') return json({ version: '0.1.0', device: selected, discovery: { running: false, error: '' }, jobs, http_library: library });
    if (url.pathname === '/api/library') return json(library);
    if (url.pathname === '/api/devices') return json({ devices: [device], scanning: false, error: '' });
    if (url.pathname === '/api/discover') return json({ ok: true });
    if (url.pathname === '/api/connect') { let body = ''; for await (const chunk of req) body += chunk; assert.equal(JSON.parse(body).id, 'test-switch'); selected = device; return json({ ok: true, device }); }
    if (url.pathname === '/api/files' && req.method === 'GET') { assert.equal(url.searchParams.get('expected_device'), device.id); return json({ path: url.searchParams.get('path'), device_id: device.id, entries }); }
    if (url.pathname === '/api/files/mkdir') { let body = ''; for await (const chunk of req) body += chunk; const data = JSON.parse(body); assert.equal(data.expected_device, device.id); assert.equal(data.path, '/'); entries.push({ name: data.name, path: '/' + data.name, type: 'directory' }); return json({ ok: true }); }
    if (url.pathname === '/api/files/upload' && req.method === 'PUT') { assert.equal(url.searchParams.get('expected_device'), device.id); const chunks = []; for await (const chunk of req) chunks.push(chunk); uploaded = { name: url.searchParams.get('name'), path: url.searchParams.get('path'), overwrite: url.searchParams.get('overwrite'), body: Buffer.concat(chunks).toString() }; uploadJob = { id: 'upload-test', name: uploaded.name, kind: 'upload', state: 'queued', bytes_done: 0, bytes_total: 5, progress: 0, speed: 0, error: '', created_at: 1780000100 }; jobs.push(uploadJob); res.statusCode = 202; return json({ ok: true, job: uploadJob }); }
    if (url.pathname === '/api/library/upload' && req.method === 'PUT') { assert.equal(url.searchParams.get('expected_library'), library.library_id); const chunks = []; for await (const chunk of req) chunks.push(chunk); installation = { name:url.searchParams.get('name'), body:Buffer.concat(chunks).toString() }; const file={name:installation.name,size:7,state:'ready',href:'/files/test.nsp'}; library.files.push(file); res.statusCode=201; return json({ok:true,file}); }
    if (url.pathname === '/api/http-install') { let body=''; for await (const chunk of req) body+=chunk; const data=JSON.parse(body); assert.equal(data.expected_device,device.id); assert.equal(data.name,installation.name); const job={id:'http-install',name:data.name,kind:'install',transport:'http',receiver_mode:'switch-link-http',state:'queued',bytes_done:0,bytes_total:7}; jobs.push(job);res.statusCode=202;return json({ok:true,job}); }
    if (url.pathname === '/api/files/rename') { let body = ''; for await (const chunk of req) body += chunk; const data = JSON.parse(body); assert.equal(data.expected_device, device.id); const entry = entries.find(item => item.path === data.path); assert.ok(entry); entry.name = data.name; entry.path = '/' + data.name; return json({ ok: true }); }
    if (url.pathname === '/api/files' && req.method === 'DELETE') { assert.equal(url.searchParams.get('expected_device'), device.id); const index = entries.findIndex(item => item.path === url.searchParams.get('path')); assert.notEqual(index, -1); entries.splice(index, 1); return json({ ok: true }); }
    const file = path.join(__dirname, url.pathname === '/' ? 'index.html' : url.pathname);
    if (!file.startsWith(__dirname) || !fs.existsSync(file)) { res.statusCode = 404; return res.end('Not found'); }
    res.setHeader('Content-Type', file.endsWith('.css') ? 'text/css' : file.endsWith('.js') ? 'application/javascript' : 'text/html'); res.end(fs.readFileSync(file));
  });
  await listenForBrowser(server);
  let browser;
  try {
    browser = await chromium.launch({ channel: 'msedge', headless: true });
    const page = await browser.newPage({ viewport: { width: 1440, height: 1050 }, deviceScaleFactor: 1 });
    const errors = []; page.on('pageerror', error => errors.push(error.message));
    await page.goto('http://127.0.0.1:' + server.address().port, { waitUntil: 'networkidle' });
    await capture(page, 'ui-desktop.png');
    assert.equal(await page.locator('#upload-button').isDisabled(), true);
    await page.locator('#empty-discover').click();
    await page.locator('.discovered-device').click();
    await page.locator('.file-row').first().waitFor();
    assert.equal(await page.locator('.file-row').count(), 2);
    assert.equal(await page.locator('.file-row img').count(), 0);
    assert.match(await page.locator('#file-list').innerText(), /<img src=x onerror=alert\(1\)>.txt/);
    await page.locator('.file-check').last().check();
    await page.waitForResponse(response => response.url().endsWith('/api/status'));
    assert.equal(await page.locator('.file-check').last().isChecked(), true);
    await page.locator('#mkdir-button').click();
    await page.locator('#action-input').fill('新建 & 文件夹');
    await page.locator('#action-submit').click();
    await page.getByRole('button', { name: '打开 新建 & 文件夹', exact: true }).waitFor();
    await page.locator('#upload-input').setInputFiles({ name: '报告 & 方案.txt', mimeType: 'text/plain', buffer: Buffer.from('hello') });
    await page.waitForFunction(() => document.getElementById('mkdir-button').disabled);
    assert.deepEqual(uploaded, { name: '报告 & 方案.txt', path: '/', overwrite: 'false', body: 'hello' });
    assert.equal(await page.locator('#rename-button').isDisabled(), true);
    assert.equal(await page.locator('#delete-button').isDisabled(), true);
    assert.equal(await page.locator('#upload-button').isDisabled(), false);
    entries.push({ name: uploaded.name, path: '/' + uploaded.name, type: 'file', size: 5 });
    uploadJob.state = 'completed'; uploadJob.progress = 100; uploadJob.bytes_done = 5;
    await page.getByRole('button', { name: uploaded.name, exact: true }).waitFor();
    assert.equal(await page.getByRole('checkbox', { name: '选择 <img src=x onerror=alert(1)>.txt', exact: true }).isChecked(), true);
    const oldUpload = uploaded;
    await page.locator('#upload-input').setInputFiles({ name: uploaded.name, mimeType: 'text/plain', buffer: Buffer.from('hello') });
    await page.locator('#action-dialog[open]').waitFor();
    assert.equal(uploaded, oldUpload, 'overwriting must wait for explicit confirmation');
    const overwriteResponse = page.waitForResponse(response => response.url().includes('/api/files/upload'));
    await page.locator('#action-submit').click();
    await overwriteResponse;
    assert.equal(uploaded.overwrite, 'true');
    uploadJob.state = 'completed'; uploadJob.progress = 100; uploadJob.bytes_done = 5;
    await page.waitForFunction(() => !document.getElementById('mkdir-button').disabled);
    await page.locator('#clear-selection').click();
    await page.getByRole('checkbox', { name: '选择 ' + uploaded.name, exact: true }).check();
    await page.locator('#rename-button').click();
    await page.locator('#action-input').fill('重命名后的文件.txt');
    await page.locator('#action-submit').click();
    await page.getByRole('button', { name: '重命名后的文件.txt', exact: true }).waitFor();
    await page.getByRole('checkbox', { name: '选择 重命名后的文件.txt', exact: true }).check();
    await page.locator('#delete-button').click();
    assert.equal(entries.some(entry => entry.name === '重命名后的文件.txt'), true);
    await page.locator('#action-submit').click();
    await page.getByRole('button', { name: '重命名后的文件.txt', exact: true }).waitFor({ state: 'detached' });
    await page.locator('[data-page=install]').click();
    assert.equal(await page.locator('#install-choose').isDisabled(), false);
    assert.match(await page.locator('#install-notice-text').innerText(), /HTTP/);
    const installResponse = page.waitForResponse(response => response.url().includes('/api/library/upload?'));
    const chooserEvent=page.waitForEvent('filechooser');await page.locator('#install-choose').click();const chooser=await chooserEvent;
    await chooser.setFiles({ name: '冒险 & 测试.nsp', mimeType: 'application/octet-stream', buffer: Buffer.from('package') });
    await installResponse;
    assert.deepEqual(installation, { name: '冒险 & 测试.nsp', body: 'package' });
    await page.locator('#library-files').getByText('冒险 & 测试.nsp', { exact: true }).waitFor();
    assert.match(await page.locator('#library-files').innerText(), /电脑安装包已准备/);
    await capture(page, 'ui-install.png');
    await page.setViewportSize({ width: 390, height: 844 });
    await capture(page, 'ui-mobile.png');
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
    assert.deepEqual(errors, []);
    assert.ok(requests.some(([method, pathname]) => method === 'POST' && pathname === '/api/connect'));
  } finally { await browser?.close(); await new Promise(resolve => server.close(resolve)); }
});
