// Read-only visual QA against the real companion. Does not connect a different
// device, trigger discovery, upload, create folders, rename, delete, or install.
const { chromium } = require('playwright');
const fs = require('node:fs');
const path = require('node:path');
const origin = process.argv[2] || 'http://127.0.0.1:8088';
const output = path.resolve(process.argv[3] || path.join(__dirname, '..', '..', 'work', 'ui-proof'));
(async () => {
  fs.mkdirSync(output, { recursive: true });
  const browser = await chromium.launch({ channel: 'msedge', headless: true });
  try {
    const page = await browser.newPage({ viewport: { width: 1440, height: 1050 }, deviceScaleFactor: 1 });
    const errors = []; page.on('pageerror', error => errors.push(error.message));
    const response = await page.goto(origin, { waitUntil: 'networkidle' });
    if (!response?.ok()) throw new Error('Companion did not return a successful page.');
    const status = await (await page.request.get(origin + '/api/status')).json();
    await page.screenshot({ path: path.join(output, 'real-sd-desktop.png'), fullPage: true, animations: 'disabled' });
    await page.screenshot({ path: path.join(output, 'real-sd-desktop-viewport.png'), animations: 'disabled' });
    await page.locator('[data-page=install]').click();
    await page.screenshot({ path: path.join(output, 'real-install-desktop.png'), fullPage: true, animations: 'disabled' });
    await page.setViewportSize({ width: 390, height: 844 });
    await page.screenshot({ path: path.join(output, 'real-install-mobile.png'), fullPage: true, animations: 'disabled' });
    await page.locator('[data-page=sd]').click();
    await page.screenshot({ path: path.join(output, 'real-sd-mobile.png'), fullPage: true, animations: 'disabled' });
    const overflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth);
    console.log(JSON.stringify({ ok: !errors.length && !overflow, output, device: status.device ? { name: status.device.name, connected: status.device.connected, can_install: status.device.can_install } : null, discovery: status.discovery, jobs: status.jobs?.length || 0, pageErrors: errors, mobileOverflow: overflow }, null, 2));
    if (errors.length || overflow) process.exitCode = 1;
  } finally { await browser.close(); }
})().catch(error => { console.error(error.message); process.exitCode = 1; });
