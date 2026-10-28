const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.existsSync(`${__dirname}/app.js`) ? fs.readFileSync(`${__dirname}/app.js`, 'utf8') : '';
const context = vm.createContext({ module: { exports: {} }, URLSearchParams });
vm.runInContext(source, context);
const ui = context.module.exports;

test('a sent install remains awaiting confirmation, never completed', () => {
  assert.equal(typeof ui.jobPresentation, 'function');
  const result = ui.jobPresentation({ kind: 'install', state: 'awaiting_confirmation', progress: 100 });
  assert.equal(result.label, '等待主机确认');
  assert.equal(result.tone, 'waiting');
  assert.equal(result.terminal, false);
  assert.equal(ui.jobPresentation({ kind: 'install', state: 'completed' }).label, '安装完成');
});

test('progress tolerates missing totals and never overflows', () => {
  assert.equal(typeof ui.progressOf, 'function');
  assert.equal(ui.progressOf({ bytes_done: 50, bytes_total: 200 }), 25);
  assert.equal(ui.progressOf({ progress: 120 }), 100);
  assert.equal(ui.progressOf({ progress: -3 }), 0);
  assert.equal(ui.progressOf({ bytes_done: 50, bytes_total: 0 }), 0);
  assert.equal(ui.progressOf({}), 0);
});

test('sorting keeps directories first and handles absent metadata', () => {
  assert.equal(typeof ui.sortEntries, 'function');
  const entries = [{ name: 'b', type: 'file', size: 2 }, { name: 'z', type: 'directory' }, { name: 'a', type: 'file', size: 10 }];
  assert.equal(ui.sortEntries(entries, 'size')[0].name, 'z');
  assert.equal(ui.sortEntries(entries, 'size')[1].name, 'a');
  assert.equal(entries[0].name, 'b');
});

test('file requests encode names and paths without creating extra query parameters', () => {
  assert.equal(typeof ui.apiUrl, 'function');
  assert.equal(ui.apiUrl('/api/files/upload', { path: '/a b/', name: '你好 & a.nsp', overwrite: false }), '/api/files/upload?path=%2Fa+b%2F&name=%E4%BD%A0%E5%A5%BD+%26+a.nsp&overwrite=false');
});

test('file names cannot escape the current directory', () => {
  assert.equal(typeof ui.validName, 'function');
  assert.equal(ui.validName('游戏.nsp'), true);
  for (const name of ['', '.', '..', '../a', 'a/b', 'a\\b', 'a\u0000b']) assert.equal(ui.validName(name), false);
});

test('installation requires explicit advertised support and a live connection', () => {
  assert.equal(typeof ui.canInstall, 'function');
  const device={connected:true,mode:'switch-link',app_version:'0.2.1',can_install:false,http_install_supported:true,http_install_ready:true};
  assert.equal(ui.canInstall(device),true);
  assert.equal(ui.canInstall({...device,app_version:'0.1.1'}),false);
  assert.equal(ui.canInstall({...device,mode:'dbi-install',can_install:true}),false);
  assert.equal(ui.canInstall({...device,http_install_ready:false}),false);
  assert.equal(ui.canInstall({ connected: true }), false);
  assert.equal(ui.canInstall({ connected: false, can_install: true }), false);
  assert.equal(ui.canInstall(null), false);
});

test('pipelined native installation at network 1% keeps network progress and metrics foremost', () => {
  const job = { kind: 'install', state: 'transferring', stage: 'installing', progress: 1, bytes_done: 30058234, bytes_total: 3005823376, speed: 10485760, eta_seconds: 284, native_progress: 42, native_bytes_total: 0 };
  const display = ui.taskDisplay(job);
  assert.equal(display.nativePhase, false);
  assert.equal(display.percent, '1%');
  assert.equal(display.speed, '10 MB/s');
  assert.equal(display.eta, '预计剩余 4 分 44 秒');
  assert.match(display.bytes, /28\.7 MB \/ 2\.8 GB/);
  assert.doesNotMatch(display.bytes, /文件已传输/);
  assert.doesNotMatch(display.label, /完成安装/);
  assert.equal(ui.jobPresentation(job).label, '正在传到主机');
  // A prematurely rounded network percentage must not override incomplete bytes.
  assert.equal(ui.taskDisplay({ ...job, progress: 100 }).nativePhase, false);
});

test('HTTP native steps remain primary while random-access network bytes are separate',()=>{
  const job={kind:'install',transport:'http',state:'installing',stage:'installing',progress:1,bytes_done:10,bytes_total:1000,network_bytes:1500,native_progress:42,native_bytes_done:420,native_bytes_total:1000};
  const display=ui.taskDisplay(job);
  assert.equal(display.percent,'当前步骤 42%');assert.match(display.bytes,/网络实际发送 1 KB/);assert.equal(display.terminal,false);
  const registering=ui.taskDisplay({...job,state:'registering',stage:'registering',native_progress:100});
  assert.equal(registering.indeterminate,true);assert.equal(registering.percent,'等待结果');assert.equal(registering.terminal,false);
});
