const { listenForBrowser } = require('./browser-test-helpers.cjs');
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
let chromium;
try { ({ chromium } = require('playwright')); } catch {}
const output = process.env.SWITCH_LINK_SCREENSHOTS;
if (output) fs.mkdirSync(output, { recursive: true });

async function setup(viewport) {
  const device = { id: 'progress-device', name: '进度测试主机', connected: true, mode:'switch-link', app_version:'0.2.1', http_install_supported:true, http_install_ready:true, http_formats:['nsp','xci'], can_install: true };
  const transfer = { id: 'three-gb', name: '冒险游戏_3GB.nsp', kind: 'install', state: 'transferring', stage: 'transferring', bytes_done: 338228674, bytes_total: 3221225472, progress: 10.5, speed: 10485760, eta_seconds: 275, created_at: 1800000000 };
  const fixture = { jobs: [transfer], holdIngestion: null };
  let markIngestion;
  const ingestionReady = new Promise(resolve => { markIngestion = resolve; });
  const server = http.createServer(async (req, res) => {
    const url = new URL(req.url, 'http://localhost');
    const json = value => { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(value)); };
    if (url.pathname === '/api/status') return json({ device, jobs: fixture.jobs, discovery: { running: false } });
    if (url.pathname === '/api/devices') return json({ devices: [device], scanning: false });
    if (url.pathname === '/api/files') return json({ path: '/', device_id: device.id, entries: Array.from({ length: 50 }, (_, i) => ({ name: 'file-' + i + '.txt', path: '/file-' + i + '.txt', type: 'file', size: 200 })) });
    if (url.pathname === '/api/files/upload') { for await (const chunk of req) {} fixture.holdIngestion = () => { res.statusCode = 503; json({ ok: false, error: '电脑接收测试中断，请重新选择文件。' }); }; markIngestion(); return; }
    const file = path.join(__dirname, url.pathname === '/' ? 'index.html' : url.pathname);
    if (!file.startsWith(__dirname) || !fs.existsSync(file)) { res.statusCode = 404; return res.end(); }
    res.setHeader('Content-Type', file.endsWith('.css') ? 'text/css' : file.endsWith('.js') ? 'application/javascript' : 'text/html'); res.end(fs.readFileSync(file));
  });
  await listenForBrowser(server);
  const browser = await chromium.launch({ channel: 'msedge', headless: true });
  const page = await browser.newPage({ viewport });
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  const origin = 'http://127.0.0.1:' + server.address().port;
  const open = async () => page.goto(origin, { waitUntil: 'networkidle' });
  const poll = async () => { const response = page.waitForResponse(reply => new URL(reply.url()).pathname === '/api/status'); await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))); await (await response).finished(); };
  return { fixture, transfer, page, errors, open, poll, ingestionReady, close: async () => { fixture.holdIngestion?.(); fixture.holdIngestion = null; await browser.close(); server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); } };
}
async function aboveFold(page) {
  assert.equal(await page.locator('#current-task').count(), 1, 'an obvious global task card must exist');
  assert.equal(await page.locator('#current-task').isVisible(), true);
  const rect = await page.locator('#current-task').boundingBox();
  assert.ok(rect.y >= 0 && rect.y + rect.height <= await page.evaluate(() => innerHeight), 'the complete task card must be visible above the fold');
}

for (const [name, viewport] of [['desktop', { width: 1366, height: 768 }], ['mobile', { width: 390, height: 844 }]]) {
  test(name + ': active task is above the fold on both tabs, remains sticky while scrolling, and updates without losing busy state', { skip: !chromium, timeout: 25000 }, async () => {
    const h = await setup(viewport);
    try {
      await h.open();
      await aboveFold(h.page);
      assert.match(await h.page.locator('#current-task').innerText(), /冒险游戏_3GB.nsp/);
      assert.match(await h.page.locator('#current-task-percent').innerText(), /10\.5%/);
      assert.match(await h.page.locator('#current-task').innerText(), /保持 Switch Link 打开/);
      assert.match(await h.page.locator('#current-task').innerText(), /10 MB\/s/);
      assert.equal(await h.page.locator('#mkdir-button').isDisabled(), true);
      h.transfer.progress = 37.8; h.transfer.bytes_done = 1217623228;
      await h.poll();
      await h.page.waitForFunction(() => document.getElementById('current-task-percent')?.textContent.includes('37.8%'));
      await h.page.locator('[data-page=install]').click();
      await aboveFold(h.page);
      await h.page.evaluate(() => scrollTo(0, 550));
      await aboveFold(h.page);
      assert.equal(await h.page.locator('#choose-device').isDisabled(), true);
      assert.equal(await h.page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      if (output) { fs.mkdirSync(output, { recursive: true }); await h.page.screenshot({ path: path.join(output, 'progress-' + name + '.png'), animations: 'disabled' }); }
      assert.deepEqual(h.errors, []);
    } finally { await h.close(); }
  });
}

test('100% network transfer remains native finalization until an explicit completed receipt, and failures stay actionable', { skip: !chromium, timeout: 25000 }, async () => {
  const h = await setup({ width: 1366, height: 768 });
  try {
    h.transfer.progress = 100; h.transfer.bytes_done = h.transfer.bytes_total; h.transfer.stage = 'installing';
    await h.open(); await aboveFold(h.page);
    assert.equal(await h.page.locator('#current-task-stage').innerText(), '主机正在完成安装');
    assert.doesNotMatch(await h.page.locator('#current-task-percent').innerText(), /安装完成/);
    assert.equal(await h.page.locator('#choose-device').isDisabled(), true);
    assert.equal(await h.page.locator('#current-task-bar').getAttribute('aria-valuenow'), null, 'unknown native progress must be indeterminate');
    if (output) await h.page.screenshot({ path: path.join(output, 'progress-native-finalizing.png'), animations: 'disabled' });
    h.transfer.state = 'awaiting_confirmation'; await h.poll();
    await h.page.waitForFunction(() => document.getElementById('current-task-stage')?.textContent === '等待主机确认');
    h.transfer.state = 'failed'; h.transfer.stage = 'failed'; h.transfer.error = '主机应用已退出，连接中断。'; await h.poll();
    await h.page.waitForFunction(() => document.getElementById('current-task-stage')?.textContent === '任务失败');
    assert.match(await h.page.locator('#current-task').innerText(), /主机应用已退出/);
    assert.equal(await h.page.locator('#current-task-retry').isVisible(), true);
    assert.equal(await h.page.locator('#current-task-retry').isEnabled(), true);
    if (output) await h.page.screenshot({ path: path.join(output, 'progress-failed.png'), animations: 'disabled' });
    await h.page.locator('[data-page=install]').click(); await aboveFold(h.page);
    h.transfer.state = 'cancelled'; await h.poll();
    await h.page.waitForFunction(() => document.getElementById('current-task-stage')?.textContent === '已取消');
    assert.equal(await h.page.locator('#current-task-retry').isVisible(), true);
    h.transfer.state = 'completed'; h.transfer.stage = 'completed'; h.transfer.error = ''; await h.poll();
    await h.page.waitForFunction(() => document.getElementById('current-task-stage')?.textContent === '安装完成');
  } finally { await h.close(); }
});

test('native progress is displayed only from real native fields and registering stays unfinished', { skip: !chromium, timeout: 15000 }, async () => {
  const h = await setup({ width: 1366, height: 768 });
  try {
    h.transfer.progress = 100; h.transfer.bytes_done = h.transfer.bytes_total; h.transfer.stage = 'installing';
    h.transfer.native_progress = 42.5; h.transfer.native_bytes_done = 456340275; h.transfer.native_bytes_total = 1073741824; h.transfer.native_speed = 1048576;
    await h.open(); await aboveFold(h.page);
    assert.equal(await h.page.locator('#current-task-percent').innerText(), '主机 42.5%');
    assert.equal(await h.page.locator('#current-task-bar').getAttribute('aria-valuenow'), '42.5');
    assert.match(await h.page.locator('#current-task-bytes').innerText(), /网络已传输 100%/);
    if (output) await h.page.screenshot({ path: path.join(output, 'progress-native-reported.png'), animations: 'disabled' });
    h.transfer.stage = 'registering'; h.transfer.native_progress = null; await h.poll();
    await h.page.waitForFunction(() => document.getElementById('current-task-stage')?.textContent === '主机正在提交安装记录');
    assert.equal(await h.page.locator('#current-task-bar').getAttribute('aria-valuenow'), null);
    assert.equal(await h.page.locator('#current-task-eta').innerText(), '等待主机提交结果');
    assert.equal(await h.page.locator('#current-task-cancel').isVisible(), false);
  } finally { await h.close(); }
});

test('browser ingestion is shown separately at the top and a failed ingestion remains visible', { skip: !chromium, timeout: 20000 }, async () => {
  const h = await setup({ width: 390, height: 844 });
  try {
    h.fixture.jobs = []; await h.open(); await h.page.locator('.file-row').first().waitFor();
    await h.page.getByRole('checkbox', { name: '选择 file-0.txt', exact: true }).check();
    await h.page.locator('#upload-input').setInputFiles({ name: 'browser-upload.bin', mimeType: 'application/octet-stream', buffer: Buffer.alloc(1024 * 512) });
    await h.ingestionReady;
    await h.page.waitForFunction(() => document.getElementById('current-task-stage')?.textContent === '电脑接收中');
    await aboveFold(h.page);
    assert.match(await h.page.locator('#current-task').innerText(), /浏览器 → 电脑/);
    assert.doesNotMatch(await h.page.locator('#current-task').innerText(), /正在传到 Switch/);
    assert.equal(await h.page.locator('#mkdir-button').isDisabled(), true);
    assert.equal(await h.page.getByRole('checkbox', { name: '选择 file-0.txt', exact: true }).isChecked(), true);
    const finishIngestion = h.fixture.holdIngestion; h.fixture.holdIngestion = null; finishIngestion();
    await h.page.waitForFunction(() => document.getElementById('current-task-stage')?.textContent === '任务失败');
    assert.match(await h.page.locator('#current-task').innerText(), /电脑接收测试中断/);
    assert.equal(await h.page.locator('#current-task-retry').isVisible(), true);
  } finally { await h.close(); }
});

test('pipelined install at network 1% does not replace transfer metrics with native 42%', { skip: !chromium, timeout: 15000 }, async () => {
  const h = await setup({ width: 1366, height: 768 });
  try {
    Object.assign(h.transfer, { stage: 'installing', progress: 1, bytes_done: 30058234, bytes_total: 3005823376, native_progress: 42, native_bytes_total: 0, eta_seconds: 284 });
    await h.open(); await aboveFold(h.page);
    assert.equal(await h.page.locator('#current-task-percent').innerText(), '1%');
    assert.equal(await h.page.locator('#current-task-speed').innerText(), '10 MB/s');
    assert.equal(await h.page.locator('#current-task-eta').innerText(), '预计剩余 4 分 44 秒');
    assert.doesNotMatch(await h.page.locator('#current-task').innerText(), /文件已传输|文件已传完|正在完成安装|主机 42%/);
    if (output) await h.page.screenshot({ path: path.join(output, 'progress-pipelined-network.png'), animations: 'disabled' });
  } finally { await h.close(); }
});
