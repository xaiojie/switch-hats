'use strict';

const SwitchLink = (() => {
  const activeStates = new Set(['receiving', 'queued', 'transferring', 'installing', 'registering']);
  const terminalStates = new Set(['completed', 'failed', 'cancelled']);
  function progressOf(job = {}) {
    let progress = Number(job.progress);
    if (job.progress === undefined || job.progress === null || !Number.isFinite(progress)) {
      progress = Number(job.bytes_total) > 0 ? Number(job.bytes_done || 0) / Number(job.bytes_total) * 100 : 0;
    }
    return Math.max(0, Math.min(100, Number.isFinite(progress) ? progress : 0));
  }
  function networkComplete(job = {}) {
    const total = Number(job.bytes_total); const done = Number(job.bytes_done);
    if (Number.isFinite(total) && total > 0) return Number.isFinite(done) && done >= total;
    return progressOf(job) >= 100;
  }
  function jobPresentation(job = {}) {
    if (job.kind === 'install' && job.transport === 'http' && !terminalStates.has(job.state)) {
      const phase = job.native_stage || job.stage || job.state;
      const label = job.state === 'queued' ? '等待主机接收' : job.state === 'awaiting_confirmation' ? '等待主机确认' : phase === 'registering' || job.state === 'registering' ? '主机正在提交安装记录' : phase === 'installing' || job.state === 'installing' ? '主机正在安装' : '主机正在读取安装包';
      return {label,tone:job.state === 'awaiting_confirmation' ? 'waiting' : 'active',terminal:false};
    }
    if (!terminalStates.has(job.state) && job.state !== 'awaiting_confirmation' && job.kind === 'install' && networkComplete(job)) {
      if (job.stage === 'registering' || job.state === 'registering') return { label: '主机正在提交安装记录', tone: 'active', terminal: false };
      if (job.stage === 'installing' || job.state === 'installing' || (job.state === 'transferring' && progressOf(job) >= 100)) return { label: '主机正在完成安装', tone: 'active', terminal: false };
    }
    if (job.kind === 'install' && !networkComplete(job) && ['installing', 'registering'].includes(job.state)) return { label: '正在传到主机', tone: 'active', terminal: false };
    const map = {
      receiving: ['电脑接收中', 'active'], queued: ['等待传输', 'active'],
      transferring: ['正在传到主机', 'active'], awaiting_confirmation: ['等待主机确认', 'waiting'],
      completed: [job.kind === 'install' ? '安装完成' : '传输完成', 'success'],
      failed: ['任务失败', 'error'], cancelled: ['已取消', 'muted']
    };
    const [label, tone] = map[job.state] || ['正在获取状态', 'muted'];
    return { label, tone, terminal: terminalStates.has(job.state) };
  }
  function sortEntries(entries = [], sort = 'name') {
    return [...entries].sort((a, b) => {
      const directoryOrder = Number(b.type === 'directory') - Number(a.type === 'directory');
      if (directoryOrder) return directoryOrder;
      if (sort === 'size') { const delta = Number(b.size || 0) - Number(a.size || 0); if (delta) return delta; }
      if (sort === 'modified') { const delta = timeOf(b.modified) - timeOf(a.modified); if (delta) return delta; }
      return String(a.name || '').localeCompare(String(b.name || ''), 'zh-CN', { numeric: true });
    });
  }
  function timeOf(value) {
    if (typeof value === 'number') return value < 100000000000 ? value * 1000 : value;
    return Date.parse(value || '') || 0;
  }
  function apiUrl(path, params = {}) { return path + '?' + new URLSearchParams(params).toString(); }
  function validName(value) { return typeof value === 'string' && !!value.trim() && value !== '.' && value !== '..' && !/[\\/\x00-\x1f\x7f]/.test(value); }
  function supportsHttpInstall(device) {
    const version = /^(\d+)\.(\d+)\.(\d+)(?:$|-)/.exec(device?.app_version || '');
    return !!version && (Number(version[1]) > 0 || Number(version[2]) > 2 || Number(version[2]) === 2 && Number(version[3]) >= 1) && device.mode === 'switch-link' && device.http_install_supported === true;
  }
  function canInstall(device) { return !!device && device.connected === true && supportsHttpInstall(device) && device.http_install_ready === true; }
  function canBrowse(device) { return !!device && device.connected === true && device.can_browse !== false; }
  function connectionKey(device) { return device ? JSON.stringify([device.id, device.ip, device.port, device.mode, device.can_browse !== false, device.can_install === true]) : ''; }
  function formatBytes(value) {
    const amount = Number(value);
    if (!Number.isFinite(amount) || amount < 0) return '—';
    if (!amount) return '0 B';
    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    const index = Math.min(4, Math.floor(Math.log(amount) / Math.log(1024)));
    return (amount / 1024 ** index).toLocaleString('zh-CN', { maximumFractionDigits: index > 1 ? 1 : 0 }) + ' ' + units[index];
  }
  function finiteProgress(value) { return value !== null && value !== undefined && value !== '' && Number.isFinite(Number(value)) ? Math.max(0, Math.min(100, Number(value))) : null; }
  function durationText(seconds) {
    if (!Number.isFinite(Number(seconds)) || seconds === null || Number(seconds) < 0) return '正在估算';
    const remaining = Math.ceil(Number(seconds));
    if (remaining < 60) return remaining + ' 秒';
    if (remaining < 3600) return Math.floor(remaining / 60) + ' 分' + (remaining % 60 ? ' ' + remaining % 60 + ' 秒' : '');
    return Math.floor(remaining / 3600) + ' 小时' + (Math.floor(remaining % 3600 / 60) ? ' ' + Math.floor(remaining % 3600 / 60) + ' 分' : '');
  }
  function taskDisplay(job = {}) {
    if (job.kind === 'install' && job.transport === 'http') return httpTaskDisplay(job);
    const presentation = jobPresentation(job);
    const nativePhase = job.kind === 'install' && networkComplete(job) && !terminalStates.has(job.state) && job.state !== 'awaiting_confirmation' && (['installing', 'registering'].includes(job.stage) || ['installing', 'registering'].includes(job.state) || (job.state === 'transferring' && progressOf(job) >= 100));
    const pipelined = job.kind === 'install' && !networkComplete(job) && !terminalStates.has(job.state) && job.state !== 'awaiting_confirmation' && (['installing', 'registering'].includes(job.stage) || ['installing', 'registering'].includes(job.native_stage));
    const local = job.state === 'receiving' || job.local === true;
    const networkProgress = job.state === 'completed' ? 100 : progressOf(job);
    const nativeProgress = finiteProgress(job.native_progress);
    const progress = nativePhase && nativeProgress !== null ? nativeProgress : networkProgress;
    const percent = value => Number(value.toFixed(1)) + '%';
    const indeterminate = nativePhase && nativeProgress === null;
    const speed = Number(nativePhase ? job.native_speed : job.speed);
    const total = Number(nativePhase ? job.native_bytes_total : job.bytes_total);
    const done = Number(nativePhase ? job.native_bytes_done : job.bytes_done);
    let eta = !nativePhase && job.eta_seconds !== null && job.eta_seconds !== undefined && Number.isFinite(Number(job.eta_seconds)) ? Number(job.eta_seconds) : total > 0 && speed > 0 && Number.isFinite(done) ? Math.max(0, (total - done) / speed) : null;
    let etaText = eta === null ? '剩余时间正在估算' : '预计剩余 ' + durationText(eta);
    if (nativePhase && eta === null) etaText = '安装用时由主机决定';
    if (nativePhase && (job.stage === 'registering' || job.state === 'registering')) etaText = '等待主机提交结果';
    if (job.state === 'awaiting_confirmation') etaText = '等待主机返回结果';
    if (job.state === 'queued') etaText = '前方任务结束后开始';
    if (terminalStates.has(job.state)) etaText = '';
    return { ...presentation, local, nativePhase, pipelined, indeterminate, progress, networkProgress,
      percent: nativePhase ? nativeProgress === null ? '传输 ' + percent(networkProgress) : '主机 ' + percent(nativeProgress) : job.state === 'awaiting_confirmation' ? '传输 ' + percent(networkProgress) : percent(networkProgress),
      route: local ? '浏览器 → 电脑' : nativePhase ? 'Switch 正在处理' : job.state === 'awaiting_confirmation' ? '文件已送达 Switch' : '电脑 → Switch',
      bytes: nativePhase && Number(job.native_bytes_total) > 0 ? '主机 ' + formatBytes(job.native_bytes_done || 0) + ' / ' + formatBytes(job.native_bytes_total) + ' · 网络已传输 ' + percent(networkProgress) : formatBytes(job.bytes_done || 0) + ' / ' + formatBytes(job.bytes_total) + (nativePhase ? ' · 文件已传输' : ''),
      speed: speed > 0 && !terminalStates.has(job.state) && job.state !== 'awaiting_confirmation' ? formatBytes(speed) + '/s' : nativePhase ? '主机处理中' : job.state === 'queued' ? '正在排队' : terminalStates.has(job.state) ? '' : '等待速度信息',
      eta: etaText };
  }
  function httpTaskDisplay(job) {
    const terminal = terminalStates.has(job.state), queued = job.state === 'queued';
    const registering = job.state === 'registering' || job.stage === 'registering' || job.native_stage === 'registering';
    const awaiting = job.state === 'awaiting_confirmation';
    const nativeProgress = finiteProgress(job.native_progress);
    const indeterminate = !terminal && !queued && (registering || awaiting || nativeProgress === null);
    const progress = job.state === 'completed' ? 100 : indeterminate ? 0 : nativeProgress || 0;
    const percent = job.state === 'completed' ? '已确认完成' : terminal ? '任务已结束' : queued ? '等待接收' : registering || awaiting ? '等待结果' : nativeProgress === null ? '主机处理中' : '当前步骤 ' + Number(nativeProgress.toFixed(1)) + '%';
    const nativeBytes = Number(job.native_bytes_total) > 0 ? '当前步骤 ' + formatBytes(job.native_bytes_done || 0) + ' / ' + formatBytes(job.native_bytes_total) + ' · ' : '';
    return {...jobPresentation(job),local:false,nativePhase:!terminal && !queued,pipelined:false,http:true,indeterminate,progress,percent,networkProgress:0,
      route:'Switch Link · HTTP 安装',bytes:nativeBytes + '网络实际发送 ' + formatBytes(job.network_bytes || 0),
      speed:terminal ? '' : Number(job.native_speed) > 0 && !registering ? '主机 ' + formatBytes(job.native_speed) + '/s' : queued ? '正在排队' : '主机处理中',
      eta:terminal ? '' : registering || awaiting ? '等待主机提交结果' : queued ? '主机接收后开始' : '当前步骤进度由主机报告'};
  }
  return { progressOf, jobPresentation, sortEntries, apiUrl, validName, canInstall, supportsHttpInstall, canBrowse, connectionKey, formatBytes, timeOf, activeStates, terminalStates, taskDisplay };
})();

if (typeof module !== 'undefined') module.exports = SwitchLink;

if (typeof document !== 'undefined') (() => {
  const { progressOf, jobPresentation, sortEntries, apiUrl, validName, canInstall, canBrowse, connectionKey, formatBytes, timeOf, activeStates, terminalStates, taskDisplay } = SwitchLink;
  const $ = (id) => document.getElementById(id);
  const state = { device: null, jobs: [], devices: [], discovery: {}, path: '/', entries: [], selected: new Set(), page: 'sd', view: 'list', sort: 'name', query: '', jobFilter: 'all', loading: false, filesError: '', filesDirty: false, loadedDevice: null, pollBusy: false, discoveryBusy: false, connecting: false, localBusy: false, local: [], localResult: null, currentTask: null, dismissedTasks: new Set(), pickers: {}, fileRequest: 0, jobsFingerprint: '', action: null };
  let lastFocus;
  function node(tag, className = '', text) { const result = document.createElement(tag); if (className) result.className = className; if (text !== undefined) result.textContent = text; return result; }
  function icon(name) { const result = document.createElementNS('http://www.w3.org/2000/svg', 'svg'); result.setAttribute('class', 'icon'); result.setAttribute('aria-hidden', 'true'); const use = document.createElementNS('http://www.w3.org/2000/svg', 'use'); use.setAttribute('href', '#i-' + name); result.append(use); return result; }
  function button(label, className, action, iconName) { const result = node('button', className); result.type = 'button'; if (iconName) result.append(icon(iconName)); if (label) result.append(document.createTextNode(label)); if (action) result.addEventListener('click', action); return result; }
  function connected() { return state.device?.connected === true; }
  function captureTarget() { return { expected_device: state.device?.id, connection: connectionKey(state.device), path: state.path }; }
  function targetIsCurrent(target) { return !!target?.expected_device && target.expected_device === state.device?.id && target.connection === connectionKey(state.device) && connected(); }
  function requireTarget(target) { if (!targetIsCurrent(target)) throw new Error('主机连接已变化，原操作已取消。请在当前主机上重新操作。'); }
  function busyJobs() { return state.localBusy || state.libraryBusy || state.jobs.some(job => activeStates.has(job.state) || (job.transport === 'http' && job.state === 'awaiting_confirmation')); }
  function errorMessage(error) { return error?.name === 'AbortError' ? '等待响应超时，请检查电脑与主机的连接。' : error?.message || '操作未完成，请重试。'; }
  async function api(path, options = {}) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 20000);
    try {
      const response = await fetch(path, { ...options, signal: controller.signal, headers: { ...(options.body ? { 'Content-Type': 'application/json' } : {}), ...options.headers } });
      let data;
      try { data = await response.json(); } catch { throw new Error('服务返回了无法读取的结果，请稍后重试。'); }
      if (!response.ok || data.ok === false) { const error = new Error(data.error || '操作未完成，请重试。'); error.status = response.status; throw error; }
      return data;
    } catch (error) {
      if (error instanceof TypeError) throw new Error('暂时无法连接电脑上的 Switch Link，请确认应用仍在运行。');
      throw error;
    } finally { clearTimeout(timer); }
  }
  function post(path, body = {}) { return api(path, { method: 'POST', body: JSON.stringify(body) }); }
  function toast(message, isError = false) {
    const item = node('div', 'toast' + (isError ? ' error' : '')); item.append(icon(isError ? 'info' : 'check'), node('span', '', message));
    const close = button('', '', () => item.remove(), 'close'); close.setAttribute('aria-label', '关闭提示'); item.append(close); $('toasts').append(item);
    setTimeout(() => item.remove(), isError ? 11000 : 6500);
  }
  function showDialog(dialog) { lastFocus = document.activeElement; if (!dialog.open) dialog.showModal(); }
  function closeDialog(dialog) { if (dialog.id === 'action-dialog' && state.action) { state.action.resolve(null); state.action = null; } dialog.close(); lastFocus?.focus?.(); }
  function ask({ title, description, name, submit = '确认', danger = false, iconName = 'folder', expectedDevice }) {
    return new Promise(resolve => {
      state.action = { resolve, hasInput: name !== undefined, expectedDevice };
      $('action-title').textContent = title; $('action-description').textContent = description;
      $('action-input').hidden = name === undefined; $('action-input-label').hidden = name === undefined;
      $('action-input').value = name || ''; $('action-error').hidden = true;
      $('action-submit').textContent = submit; $('action-submit').className = 'button ' + (danger ? 'button-danger' : 'button-primary');
      $('action-icon').replaceChildren(icon(iconName)); showDialog($('action-dialog'));
      if (name !== undefined) { $('action-input').focus(); const dot = name.lastIndexOf('.'); $('action-input').setSelectionRange(0, dot > 0 ? dot : name.length); }
    });
  }
  function serviceError(message) { $('service-error').hidden = !message; $('service-error-text').textContent = message || ''; }
  function setPage(page) {
    state.page = ['install', 'music'].includes(page) ? page : 'sd';
    $('page-sd').hidden = state.page !== 'sd'; $('page-install').hidden = state.page !== 'install';
    $('page-music').hidden = state.page !== 'music';
    $('topbar-current').textContent = state.page === 'sd' ? 'FTP 文件管理' : state.page === 'music' ? '随身听音乐库' : 'HTTP 游戏安装';
    document.querySelectorAll('[data-page]').forEach(link => { const active = link.dataset.page === state.page; link.classList.toggle('active', active); if (active) link.setAttribute('aria-current', 'page'); else link.removeAttribute('aria-current'); });
    document.title = (state.page === 'sd' ? 'SD 卡文件' : state.page === 'music' ? '随身听音乐库' : '安装任务') + ' · Switch Link';
    renderDevice();
  }
  function renderDevice() {
    const device = state.device; const online = connected(); const browsing = canBrowse(device);
    $('device-name').textContent = device?.name || '连接你的 Switch';
    $('device-description').textContent = online ? (device.mode === 'dbi-install' ? '已连接 · DBI 游戏安装 · ' : device.mode === 'dbi' ? '已连接 · DBI 文件管理 · ' : '已连接 · ') + (device.ip || '你的局域网主机') : device ? '连接已断开，正在重新寻找主机。' : '同一网络，自动发现。';
    $('device-dot').classList.toggle('connected', online);
    $('choose-device-label').textContent = online ? '更换主机' : '发现主机';
    $('choose-device').disabled = state.connecting || busyJobs();
    $('choose-device').title = busyJobs() ? '请等待传输任务结束后再更换主机' : '';
    const pill = $('topbar-connection');
    if (state.page === 'sd') { pill.classList.toggle('connected', online); pill.lastElementChild.textContent = online ? 'FTP 已连接' : 'FTP 未连接'; }
    const httpBusy = state.libraryBusy || state.jobs.some(job => job.transport === 'http' && !terminalStates.has(job.state));
    document.querySelectorAll('[data-needs-device]').forEach(control => { control.disabled = !browsing || httpBusy || (control.id !== 'upload-button' && busyJobs()); });
    $('up-button').disabled = !browsing || state.path === '/' || state.loading || busyJobs();
    $('sd-dropzone').setAttribute('aria-disabled', String(!browsing));
    window.HttpLibraryUI?.updateContext({device:state.device,jobs:state.jobs});
    const hasStorage = online && Number(device.total_bytes) > 0 && Number.isFinite(Number(device.free_bytes)) && device.free_bytes !== null;
    $('storage').hidden = !hasStorage;
    if (hasStorage) { const total = Number(device.total_bytes); const free = Math.max(0, Number(device.free_bytes)); $('storage-available').textContent = formatBytes(free) + ' 可用'; $('storage-fill').style.width = Math.max(0, Math.min(100, (total - free) / total * 100)) + '%'; $('storage-total').textContent = '总容量 ' + formatBytes(total); }
    if (online && busyJobs()) $('file-footer-status').textContent = '传输进行中，结束后可继续整理文件';
    else if (online && state.entries.length) { const directories = state.entries.filter(entry => entry.type === 'directory').length; $('file-footer-status').textContent = directories + ' 个文件夹，' + (state.entries.length - directories) + ' 个文件'; }
    renderSelection();
    renderCurrentTask();
  }
  function renderBreadcrumbs() {
    const crumbs = [button('SD 卡', '', () => loadFiles('/'))]; let path = '';
    for (const part of state.path.split('/').filter(Boolean)) { path += '/' + part; const target = path; crumbs.push(icon('chevron'), button(part, '', () => loadFiles(target))); }
    $('breadcrumbs').replaceChildren(...crumbs);
    $('breadcrumbs').lastElementChild?.setAttribute('aria-current', 'location');
    $('current-path-label').textContent = state.path;
  }
  function emptyFiles(title, description, iconName, actionLabel, action) {
    const empty = node('div', 'empty-state'); const art = node('div', 'empty-illustration'); art.append(icon(iconName)); empty.append(art, node('h2', '', title), node('p', '', description));
    if (actionLabel) empty.append(button(actionLabel, 'button button-primary', action, actionLabel.includes('主机') ? 'wifi' : 'refresh'));
    return empty;
  }
  function renderFileState() {
    $('file-empty').className = 'empty-state';
    $('file-area').setAttribute('aria-busy', String(state.loading));
    if (!connected()) {
      $('file-list').hidden = true; $('file-empty').hidden = false;
      $('file-empty').replaceChildren(...emptyFiles('让你的 Switch 近一点', '在主机打开 Switch Link 接收页面。电脑与主机需连接同一网络。', 'sd', '发现我的主机', openDevices).childNodes);
      $('file-empty').append(node('span', 'empty-note', '自动搜索，无需输入 IP 地址'));
      $('file-count').textContent = '还未连接主机'; $('file-footer-status').textContent = '连接后即可浏览 SD 卡'; renderSelection(); return;
    }
    if (!canBrowse(state.device)) {
      $('file-list').hidden = true; $('file-empty').hidden = false;
      $('file-empty').replaceChildren(...emptyFiles('当前连接不提供 SD 卡文件管理', '游戏安装请打开 Switch Link 0.2.1 或更新版本的接收页面。管理文件需要可浏览 SD 卡的 FTP 服务。', 'package', '前往 HTTP 游戏安装', () => { location.hash = 'install'; setPage('install'); }).childNodes);
      $('file-count').textContent = '文件管理暂不可用'; $('file-footer-status').textContent = '当前 FTP 未提供文件浏览'; renderSelection(); return;
    }
    if (state.loading && !state.entries.length) {
      const loading = node('div', 'loading-state'); loading.append(node('div', 'spinner'), node('span', '', '正在打开 SD 卡…'));
      $('file-empty').className = ''; $('file-empty').replaceChildren(loading); $('file-empty').hidden = false; $('file-list').hidden = true; $('file-count').textContent = '正在读取…'; return;
    }
    $('file-empty').className = 'empty-state';
    if (state.filesError) {
      $('file-list').hidden = true; $('file-empty').hidden = false;
      $('file-empty').replaceChildren(...emptyFiles('暂时无法打开文件夹', state.filesError, 'info', '重新加载', () => loadFiles(state.path)).childNodes);
      $('file-count').textContent = '读取未完成'; $('file-footer-status').textContent = '请检查主机连接后重试'; renderSelection(); return;
    }
    renderFiles();
  }
  function filePath(entry) { return entry.path || (state.path === '/' ? '/' : state.path + '/') + entry.name; }
  function formatDate(value) { const time = timeOf(value); return time ? new Date(time).toLocaleString('zh-CN', { year: 'numeric', month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit', hour12: false }).replaceAll('/', '-') : '—'; }
  function renderFiles() {
    const filtered = sortEntries(state.entries.filter(entry => String(entry.name || '').toLocaleLowerCase().includes(state.query.toLocaleLowerCase())), state.sort);
    const folderCount = state.entries.filter(entry => entry.type === 'directory').length;
    $('file-count').textContent = state.query ? filtered.length + ' 个搜索结果' : state.entries.length + ' 个项目';
    $('file-footer-status').textContent = folderCount + ' 个文件夹，' + (state.entries.length - folderCount) + ' 个文件';
    $('file-list').hidden = !filtered.length; $('file-empty').hidden = !!filtered.length;
    if (!filtered.length) { $('file-empty').replaceChildren(...emptyFiles(state.query ? '没有找到匹配的文件' : '这里还是一片空白', state.query ? '换个关键词试试，或清除搜索查看全部文件。' : '上传文件，或新建文件夹，让这里井井有条。', state.query ? 'search' : 'folder', state.query ? '清除搜索' : '上传第一个文件', state.query ? () => { $('file-search').value = ''; state.query = ''; renderFiles(); } : () => chooseFiles('upload')).childNodes); }
    const fragment = document.createDocumentFragment();
    const columns = node('div', 'file-columns'); columns.setAttribute('aria-hidden', 'true'); columns.append(node('span'), node('span', '', '名称'), node('span', '', '大小'), node('span', 'file-modified', '修改时间'), node('span')); fragment.append(columns);
    for (const entry of filtered) {
      const path = filePath(entry); const directory = entry.type === 'directory';
      const row = node('div', 'file-row' + (state.selected.has(path) ? ' selected' : '')); row.dataset.path = path;
      const check = node('input', 'file-check'); check.type = 'checkbox'; check.checked = state.selected.has(path); check.setAttribute('aria-label', '选择 ' + entry.name);
      check.addEventListener('change', () => { if (check.checked) state.selected.add(path); else state.selected.delete(path); row.classList.toggle('selected', check.checked); renderSelection(); });
      const fileName = button('', 'file-name-button', () => { if (directory) loadFiles(path); else { check.checked = !check.checked; check.dispatchEvent(new Event('change')); } });
      const symbol = node('span', 'file-symbol' + (directory ? ' directory' : /\.(nsp|nsz|xci|xcz)$/i.test(entry.name) ? ' package' : '')); symbol.append(icon(directory ? 'folder' : /\.(nsp|nsz|xci|xcz)$/i.test(entry.name) ? 'package' : 'file'));
      fileName.append(symbol, node('span', '', entry.name || '未命名')); fileName.title = directory ? '打开 ' + entry.name : entry.name;
      const quick = button('', 'icon-button row-action', () => { if (directory) loadFiles(path); else downloadFile(entry); }, directory ? 'chevron' : 'install'); quick.title = directory ? '打开文件夹' : '下载文件'; quick.setAttribute('aria-label', (directory ? '打开 ' : '下载 ') + entry.name);
      row.append(check, fileName, node('span', 'file-size', directory ? '文件夹' : formatBytes(entry.size)), node('span', 'file-modified', formatDate(entry.modified)), quick); fragment.append(row);
    }
    $('file-list').replaceChildren(fragment); $('file-list').className = 'file-list' + (state.view === 'grid' ? ' grid' : ''); renderSelection();
  }
  function selectedEntries() { return state.entries.filter(entry => state.selected.has(filePath(entry))); }
  function renderSelection() {
    const entries = selectedEntries(); const count = entries.length;
    $('selection-bar').hidden = !count || !canBrowse(state.device) || !!state.filesError; $('selection-label').textContent = '已选择 ' + count + ' 项';
    $('download-button').disabled = busyJobs() || count !== 1 || entries[0]?.type === 'directory'; $('rename-button').disabled = busyJobs() || count !== 1; $('delete-button').disabled = busyJobs();
  }
  async function loadFiles(path = state.path, preserve = false) {
    if (!connected()) return;
    if (!canBrowse(state.device)) { state.loading = false; state.loadedDevice = state.device.id; renderFileState(); return; }
    if (busyJobs()) {
      if (!state.entries.length) { $('file-empty').className = 'empty-state'; $('file-empty').hidden = false; $('file-list').hidden = true; $('file-empty').replaceChildren(...emptyFiles('正在传输，文件正在路上', '顶部任务卡会持续显示进度。任务结束后即可浏览和整理 SD 卡。', 'upload').childNodes); $('file-count').textContent = '任务进行中'; }
      if (!preserve && state.loadedDevice) toast('文件正在传输，结束后即可继续浏览和整理。'); return;
    }
    const request = ++state.fileRequest;
    const target = captureTarget();
    const navigating = path !== state.path;
    if (navigating) { state.entries = []; state.selected.clear(); state.query = ''; $('file-search').value = ''; }
    state.path = path; state.loading = true; state.filesError = ''; renderBreadcrumbs(); renderFileState(); renderDevice();
    try {
      const data = await api(apiUrl('/api/files', { path, expected_device: target.expected_device }));
      if (request !== state.fileRequest || !targetIsCurrent(target)) return;
      if (data.device_id !== target.expected_device) throw new Error('主机连接已变化，请刷新后重新打开文件夹。');
      state.path = data.path || path; state.entries = Array.isArray(data.entries) ? data.entries : [];
      const actual = new Set(state.entries.map(filePath)); state.selected = new Set([...state.selected].filter(name => actual.has(name)));
      state.loadedDevice = target.expected_device; state.filesError = ''; state.filesDirty = false;
      if (!preserve && navigating) $('file-area').scrollTop = 0;
    } catch (error) { if (request !== state.fileRequest || !targetIsCurrent(target)) return; state.filesError = errorMessage(error); }
    if (request !== state.fileRequest || !targetIsCurrent(target)) return;
    state.loading = false; renderBreadcrumbs(); renderFileState(); renderDevice();
  }
  function downloadFile(entry) {
    if (busyJobs()) { toast('请等待传输结束后再下载文件。'); return; }
    const target = captureTarget(); if (!targetIsCurrent(target)) return;
    const link = node('a'); link.href = apiUrl('/api/files/download', { path: filePath(entry), expected_device: target.expected_device }); link.download = entry.name; document.body.append(link); link.click(); link.remove();
    toast('已开始下载，请查看浏览器的下载列表。');
  }
  async function newFolder() {
    const target = captureTarget();
    const name = await ask({ title: '新建文件夹', description: '将在当前文件夹中创建。', name: '', submit: '创建文件夹', expectedDevice: target.expected_device }); if (name === null) return;
    try { requireTarget(target); await post('/api/files/mkdir', { path: target.path, name, expected_device: target.expected_device }); toast('文件夹已创建。'); if (targetIsCurrent(target)) await loadFiles(state.path, true); } catch (error) { toast(errorMessage(error), true); }
  }
  async function renameFile() {
    const entry = selectedEntries()[0]; if (!entry) return;
    const target = captureTarget(); const path = filePath(entry);
    const name = await ask({ title: '重命名', description: '为“' + entry.name + '”输入新名称。', name: entry.name, submit: '保存名称', iconName: 'edit', expectedDevice: target.expected_device }); if (name === null || name === entry.name) return;
    try { requireTarget(target); await post('/api/files/rename', { path, name, expected_device: target.expected_device }); toast('名称已更新。'); if (targetIsCurrent(target)) { state.selected.clear(); await loadFiles(state.path, true); } } catch (error) { toast(errorMessage(error), true); }
  }
  async function deleteFiles() {
    const entries = selectedEntries(); if (!entries.length) return;
    const target = captureTarget(); const paths = entries.map(filePath);
    const description = entries.length === 1 ? '即将删除“' + entries[0].name + '”。' + (entries[0].type === 'directory' ? '文件夹内的内容也会被删除。' : '') : '即将删除选中的 ' + entries.length + ' 个项目及文件夹内的内容。';
    const confirmed = await ask({ title: '确认删除？', description: description + '此操作无法在这里撤销。', submit: '确认删除', danger: true, iconName: 'trash', expectedDevice: target.expected_device }); if (!confirmed) return;
    let deleted = 0;
    try { for (const path of paths) { requireTarget(target); await api(apiUrl('/api/files', { path, expected_device: target.expected_device }), { method: 'DELETE' }); deleted++; } toast('已删除 ' + deleted + ' 个项目。'); }
    catch (error) { toast((deleted ? '已删除 ' + deleted + ' 项。' : '') + errorMessage(error), true); }
    if (targetIsCurrent(target)) { state.selected.clear(); await loadFiles(state.path, true); }
  }
  function renderDiscovery() {
    const status = $('discovery-state'); status.replaceChildren();
    const running = state.discoveryBusy || state.discovery.running || state.discovery.scanning;
    if (running) status.append(node('span', 'spinner'), node('span', '', '正在附近寻找主机…'));
    else status.append(node('span', '', state.discovery.error || (state.devices.length ? '找到了 ' + state.devices.length + ' 台主机，选择一台开始。' : '暂时没有发现主机。保持主机应用打开，然后重新搜索。')));
    $('rescan-button').disabled = !!running || state.connecting || busyJobs();
    const devices = state.devices.map(device => {
      const item = button('', 'discovered-device', () => connectDevice(device)); item.disabled = state.connecting || busyJobs(); item.append(icon('link'));
      const text = node('span'); text.append(node('strong', '', device.name || 'Switch 主机'), node('small', '', (device.ip || '局域网设备') + ' · ' + (SwitchLink.supportsHttpInstall(device) ? 'Switch Link · HTTP 安装' : device.mode === 'switch-link' ? '请升级 Switch Link' : device.can_browse === false ? '文件管理暂不可用' : 'SD 卡文件管理')));
      item.append(text, node('span', 'device-select-label', state.connecting ? '连接中…' : state.device?.id === device.id && connected() ? '已连接' : '连接')); return item;
    });
    $('device-list').replaceChildren(...devices);
  }
  async function refreshDevices() {
    try { const data = await api('/api/devices'); state.devices = Array.isArray(data.devices) ? data.devices : []; state.discovery = { ...state.discovery, running: !!data.scanning, error: data.error }; renderDiscovery(); } catch (error) { state.discovery = { running: false, error: errorMessage(error) }; renderDiscovery(); }
  }
  async function openDevices() { showDialog($('device-dialog')); renderDiscovery(); await refreshDevices(); if (!state.devices.length && !state.discovery.running) await discover(); }
  async function discover() {
    if (state.discoveryBusy || state.discovery.running) return;
    state.discoveryBusy = true; state.discovery.error = ''; renderDiscovery();
    try { await post('/api/discover'); state.discovery.running = true; } catch (error) { state.discovery = { running: false, error: errorMessage(error) }; }
    state.discoveryBusy = false; renderDiscovery(); await refreshDevices();
  }
  async function connectDevice(device) {
    if (busyJobs() || state.connecting) return;
    state.connecting = true; renderDiscovery(); renderDevice();
    try {
      const data = await post('/api/connect', { id: device.id }); state.device = data.device || device;
      if (!state.device.connected) throw new Error('主机尚未连接，请保持主机应用在前台后重试。');
      closeDialog($('device-dialog')); state.path = '/'; state.entries = []; state.selected.clear(); state.loadedDevice = null;
      renderDevice(); toast('已连接 ' + (state.device.name || '你的 Switch') + '。'); await loadFiles('/'); await poll();
    } catch (error) { toast(errorMessage(error), true); }
    finally { state.connecting = false; renderDiscovery(); renderDevice(); }
  }
  function renderCurrentTask() {
    const visible = job => !state.dismissedTasks.has(job.id + ':' + job.state);
    const sorted = [...state.jobs].sort((a, b) => timeOf(b.created_at) - timeOf(a.created_at));
    const active = sorted.filter(job => activeStates.has(job.state));
    active.sort((a, b) => Number(b.state !== 'queued') - Number(a.state !== 'queued'));
    const local = state.local[0];
    const candidates = [...(local ? [local] : []), ...active, ...[...sorted.filter(job => job.state === 'awaiting_confirmation' || terminalStates.has(job.state)), ...(state.localResult ? [state.localResult] : [])].sort((a, b) => timeOf(b.created_at) - timeOf(a.created_at))];
    const job = candidates.find(visible);
    state.currentTask = job || null;
    const card = $('current-task'); card.hidden = !job;
    if (!job) return;
    const display = taskDisplay(job); const terminal = terminalStates.has(job.state); const failed = job.state === 'failed' || job.state === 'cancelled';
    card.className = 'current-task ' + display.tone + (display.nativePhase ? ' native-phase' : '') + (display.indeterminate ? ' indeterminate' : '');
    $('current-task-icon').replaceChildren(icon(failed ? 'info' : job.state === 'completed' ? 'check' : display.local ? 'upload' : job.kind === 'install' ? 'package' : 'upload'));
    $('current-task-kind').textContent = job.kind === 'install' ? '当前安装任务' : '当前文件传输';
    $('current-task-route').textContent = display.route;
    $('current-task-name').textContent = job.name || '未命名文件'; $('current-task-name').title = job.name || '';
    $('current-task-stage').textContent = display.label;
    $('current-task-percent').textContent = display.percent;
    $('current-task-fill').style.width = (display.indeterminate ? 32 : display.progress) + '%';
    const bar = $('current-task-bar');
    bar.setAttribute('aria-label', display.nativePhase ? '主机安装进度' : display.local ? '电脑接收进度' : '电脑到 Switch 的传输进度');
    bar.setAttribute('aria-valuetext', display.label + '，' + display.percent);
    if (display.indeterminate) bar.removeAttribute('aria-valuenow'); else bar.setAttribute('aria-valuenow', String(display.progress));
    $('current-task-bytes').textContent = display.bytes; $('current-task-speed').textContent = display.speed; $('current-task-eta').textContent = display.eta;
    let helper = '保持 Switch Link 打开；退出会中断。';
    if (display.local && !terminal) helper += '文件先由电脑接收，尚未传到 Switch。';
    else if (display.http && !terminal) helper += '百分比仅表示主机当前步骤；网络字节可含重复读取。收到主机确认结果后才显示安装完成。';
    else if (display.pipelined) helper += '主机正在边传输边安装，当前百分比显示网络传输。';
    else if (display.nativePhase) helper += display.indeterminate ? '文件已传完，主机处理进度暂不可用，请等待结果。' : '文件已传完，现显示主机报告的处理进度。';
    else if (job.state === 'awaiting_confirmation') helper += '已送达主机，尚未收到安装结果。';
    else if (failed) helper = job.state === 'cancelled' ? '任务已取消。需要时可重新选择文件开始。' : '请保持 Switch Link 打开，检查连接后重新选择文件。';
    else if (job.state === 'completed') helper = job.kind === 'install' ? '主机已确认安装完成，可以在 Switch 上查看。' : '文件已传到 SD 卡。';
    $('current-task-helper-text').textContent = helper;
    $('current-task-error').hidden = !job.error; $('current-task-error').textContent = job.error || '';
    const additional = active.length + state.local.length + sorted.filter(item => item.state === 'awaiting_confirmation').length - Number(!terminal);
    $('current-task-queue').hidden = additional <= 0; $('current-task-queue').textContent = additional > 0 ? '另有 ' + additional + ' 个任务' : '';
    $('current-task-dismiss').hidden = !terminal;
    $('current-task-retry').hidden = !failed; $('current-task-retry').disabled = !connected() || (job.kind === 'install' && !canInstall(state.device)) || state.localBusy;
    $('current-task-check').hidden = !failed; $('current-task-check').disabled = busyJobs();
    $('current-task-cancel').hidden = job.local || !activeStates.has(job.state) || (job.kind === 'install' && job.state !== 'queued' && job.state !== 'receiving');
    const announcement = job.id + ':' + display.label;
    if ($('current-task-announcement').dataset.task !== announcement) { $('current-task-announcement').dataset.task = announcement; $('current-task-announcement').textContent = (job.name || '文件') + '：' + display.label; }
  }
  function renderJobs() {
    const installJobs = state.jobs.filter(job => job.kind === 'install');
    const unfinished = installJobs.filter(job => !terminalStates.has(job.state)).length;
    $('nav-job-count').textContent = unfinished; $('nav-job-count').hidden = !unfinished;
    $('install-job-count').textContent = installJobs.length;
    const filtered = installJobs.filter(job => state.jobFilter === 'all' || (state.jobFilter === 'active' ? !terminalStates.has(job.state) : terminalStates.has(job.state))).sort((a, b) => timeOf(b.created_at) - timeOf(a.created_at));
    if (!filtered.length) {
      const empty = node('div', 'queue-empty'); const image = node('div', 'queue-icon'); image.append(icon('install'));
      empty.append(image, node('h3', '', state.jobFilter === 'all' ? '下一款游戏，还在路上' : '这里暂时没有任务'), node('p', '', state.jobFilter === 'all' ? '添加安装包后，可在这里查看传输与安装状态。' : state.jobFilter === 'active' ? '新任务和等待主机确认的任务会出现在这里。' : '已完成、失败和取消的任务会出现在这里。')); $('install-jobs').replaceChildren(empty);
    } else $('install-jobs').replaceChildren(...filtered.map(job => jobCard(job)));
    const uploads = state.jobs.filter(job => job.kind !== 'install').sort((a, b) => timeOf(b.created_at) - timeOf(a.created_at));
    $('sd-job-count').textContent = uploads.filter(job => !terminalStates.has(job.state)).length;
    $('sd-jobs').replaceChildren(...(uploads.length ? uploads.slice(0, 4).map(job => jobCard(job, true)) : [node('p', 'subtle-empty', '每一次传输，都能在这里找到。')]));
    renderCurrentTask();
  }
  function jobCard(job, compact = false) {
    const presentation = taskDisplay(job); const card = node('article', compact ? 'mini-job' : 'job-card ' + presentation.tone);
    const top = node('div', compact ? 'mini-job-top' : 'job-top'); const status = node('span', 'job-status ' + presentation.tone, presentation.label);
    if (compact) top.append(node('span', 'mini-job-name', job.name || '未命名文件'), status);
    else { const picture = node('span', 'job-file-icon'); picture.append(icon(job.kind === 'install' ? 'package' : 'file')); const title = node('div', 'job-title'); title.append(node('h3', '', job.name || '未命名文件'), node('p', '', (job.kind === 'install' ? '安装到 SD 卡' : '传到 SD 卡') + ' · ' + formatBytes(job.bytes_total))); top.append(picture, title, status); }
    if (activeStates.has(job.state) && !(job.kind === 'install' && !['receiving', 'queued'].includes(job.state))) { const cancel = button('', 'icon-button job-cancel', () => cancelJob(job), 'close'); cancel.setAttribute('aria-label', '取消任务 ' + (job.name || '')); cancel.title = '取消任务'; cancel.dataset.jobId = job.id; top.append(cancel); }
    const progress = node('div', 'job-progress' + (presentation.indeterminate ? ' indeterminate' : '')); const fill = node('span'); fill.style.width = (presentation.indeterminate ? 32 : presentation.progress) + '%'; progress.append(fill);
    progress.setAttribute('role', 'progressbar'); progress.setAttribute('aria-label', job.name || '文件传输'); progress.setAttribute('aria-valuemin', '0'); progress.setAttribute('aria-valuemax', '100'); if (!presentation.indeterminate) progress.setAttribute('aria-valuenow',String(presentation.progress)); progress.setAttribute('aria-valuetext',presentation.label + '，' + presentation.percent);
    const meta = node('div', 'job-meta');
    const detail = job.state === 'awaiting_confirmation' ? '文件已送达主机' : formatBytes(job.bytes_done || 0) + (job.bytes_total ? ' / ' + formatBytes(job.bytes_total) : '');
    meta.append(node('span', '', presentation.http ? presentation.bytes : detail), node('span', '', presentation.http ? presentation.percent : job.state === 'transferring' && Number(job.speed) > 0 ? formatBytes(job.speed) + '/s' : job.state === 'completed' ? '已完成' : job.state === 'awaiting_confirmation' ? '等待确认' : Math.round(progressOf(job)) + '%'));
    card.append(top, progress, meta);
    if (!compact && job.state === 'awaiting_confirmation') card.append(node('p', 'job-message', '传输已结束，尚未收到主机的安装结果。请在主机上查看，暂不能确认安装完成。'));
    if (job.error) card.append(node('p', 'job-message', job.error));
    return card;
  }
  async function cancelJob(job) {
    const result = await ask({ title: '取消这个任务？', description: '“' + (job.name || '文件') + '”将请求停止传输，已开始的操作可能需要片刻才能安全结束。', submit: '取消任务', danger: true, iconName: 'close' }); if (!result) return;
    try { await post('/api/jobs/cancel', { id: job.id }); toast('已请求取消，正在等待任务更新。'); await poll(); } catch (error) { toast(errorMessage(error), true); }
  }
  function renderLocal() {
    $('local-transfers').hidden = !state.local.length;
    $('local-transfer-list').replaceChildren(...state.local.map(item => { const row = node('div', 'local-transfer'); const top = node('div'); top.append(node('strong', '', item.name), node('span', '', Math.round(item.progress) + '% · 电脑接收中')); const bar = node('div', 'job-progress'); const fill = node('span'); fill.style.width = item.progress + '%'; bar.append(fill); row.append(top, bar); return row; }));
    renderDevice();
  }
  function sendRaw(file, url, item) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest(); xhr.open('PUT', url); xhr.setRequestHeader('Content-Type', 'application/octet-stream');
      xhr.upload.addEventListener('progress', event => { if (event.lengthComputable) { item.bytes_done = event.loaded; item.bytes_total = event.total; item.progress = Math.min(100, event.loaded / event.total * 100); item.speed = event.loaded / Math.max(.05, (performance.now() - item.startedAt) / 1000); renderLocal(); } });
      xhr.addEventListener('load', () => { let data; try { data = JSON.parse(xhr.responseText); } catch { reject(new Error('未能读取上传结果，请检查连接后重试。')); return; } if (xhr.status >= 200 && xhr.status < 300 && data.ok !== false) resolve(data); else { const error = new Error(data.error || '文件接收失败，请重试。'); error.status = xhr.status; reject(error); } });
      xhr.addEventListener('error', () => reject(new Error('文件接收中断，请检查电脑上的 Switch Link 是否仍在运行。')));
      xhr.addEventListener('abort', () => reject(new Error('文件接收已取消。')));
      xhr.send(file);
    });
  }
  function chooseFiles(kind) {
    if (kind === 'install') { window.HttpLibraryUI?.choose(); return; }
    if (!connected()) { toast('请先连接你的 Switch。'); return; }
    if (kind === 'upload' && !canBrowse(state.device)) { toast('当前连接不提供文件管理，请打开主机的 SD 卡文件服务。'); return; }
    if (state.libraryBusy || state.jobs.some(job => job.transport === 'http' && !terminalStates.has(job.state))) { toast('请等待主机安装任务结束后再管理文件。'); return; }
    state.pickers[kind] = captureTarget();
    $(kind === 'install' ? 'install-input' : 'upload-input').click();
  }
  async function enqueueFiles(files, kind, target = captureTarget()) {
    if (kind !== 'upload') return;
    if (!files.length) return;
    if (!targetIsCurrent(target)) { toast('主机连接已变化，原操作已取消。请重新选择文件。', true); return; }
    if (!connected()) { toast('请先连接你的 Switch。', true); return; }
    if (kind === 'upload' && !canBrowse(state.device)) { toast('当前连接不提供文件管理，请打开主机的 SD 卡文件服务。', true); return; }
    if (state.libraryBusy || state.jobs.some(job => job.transport === 'http' && !terminalStates.has(job.state))) { toast('请等待主机安装任务结束后再管理文件。',true); return; }
    if (state.localBusy) { toast('电脑正在接收上一批文件，请稍后添加。'); return; }
    const targetPath = target.path; const targetDevice = target.expected_device;
    state.localBusy = true; renderDevice();
    let accepted = 0;
    try {
      for (const file of files) {
        if (!targetIsCurrent(target)) { toast('主机连接已变化，原操作已取消。请重新连接后添加文件。', true); break; }
        if (!validName(file.name)) { toast('“' + file.name + '”的名称无法用于传输，请先修改文件名。', true); continue; }
        let overwrite = false;
        const existing = kind === 'upload' && state.entries.find(entry => entry.name === file.name);
        if (existing?.type === 'directory') { toast('“' + file.name + '”与现有文件夹重名，请先修改文件名。', true); continue; }
        if (existing) { overwrite = !!await ask({ title: '替换同名文件？', description: '当前文件夹已有“' + file.name + '”。确认后将用新文件替换，原文件会先保留备份。', submit: '确认替换', danger: true, iconName: 'file', expectedDevice: targetDevice }); if (!overwrite) continue; }
        if (!targetIsCurrent(target)) break;
        const item = { id: 'local-' + Date.now() + '-' + Math.random().toString(36).slice(2), name: file.name, kind, state: 'receiving', local: true, progress: 0, bytes_done: 0, bytes_total: file.size, speed: 0, startedAt: performance.now(), created_at: Date.now() }; state.local.push(item); renderLocal();
        try {
          requireTarget(target);
          const result = await sendRaw(file, apiUrl('/api/files/upload', { path: targetPath, name: file.name, overwrite, expected_device: targetDevice }), item);
          if (result.job) { const index = state.jobs.findIndex(job => job.id === result.job.id); if (index >= 0) state.jobs[index] = result.job; else state.jobs.unshift(result.job); state.jobsFingerprint = ''; renderJobs(); }
          accepted++;
        } catch (error) { state.localResult = { ...item, state: 'failed', error: errorMessage(error), created_at: Date.now() }; toast(file.name + '：' + errorMessage(error), true); }
        finally { state.local = state.local.filter(entry => entry !== item); renderLocal(); }
      }
    } finally { state.localBusy = false; renderDevice(); renderLocal(); }
    if (accepted) toast(accepted + ' 个文件已加入' + (kind === 'install' ? '安装' : '传输') + '队列。');
    await poll();
  }
  async function poll() {
    if (state.pollBusy) return; state.pollBusy = true;
    try {
      const data = await api('/api/status'); serviceError('');
      const previous = state.device; const previousJobs = new Map(state.jobs.map(job => [job.id, job.state]));
      state.device = data.device || null; state.discovery = data.discovery || {};
      if (data.http_library) window.HttpLibraryUI?.update(data.http_library);
      if (data.version) $('app-version').textContent = 'v' + data.version;
      if (Array.isArray(data.jobs)) state.jobs = data.jobs;
      const fingerprint = JSON.stringify(state.jobs);
      if (fingerprint !== state.jobsFingerprint) {
        const focusedJob = document.activeElement?.dataset?.jobId;
        renderJobs(); state.jobsFingerprint = fingerprint;
        if (focusedJob) [...document.querySelectorAll('[data-job-id]')].find(control => control.dataset.jobId === focusedJob)?.focus({ preventScroll: true });
      }
      renderDevice();
      const changedDevice = connectionKey(previous) !== connectionKey(state.device);
      if (changedDevice) {
        if (state.action?.expectedDevice) { closeDialog($('action-dialog')); toast('主机或连接模式已变化，已取消尚未确认的操作。', true); }
        state.entries = []; state.selected.clear(); state.path = '/'; state.loadedDevice = null; state.fileRequest++; state.loading = false; renderBreadcrumbs();
      }
      if (connected() && (changedDevice || !previous?.connected || state.loadedDevice !== state.device.id)) await loadFiles(state.path);
      else if (!connected() && previous?.connected !== state.device?.connected) { state.fileRequest++; state.loading = false; renderFileState(); }
      const finishedUpload = state.jobs.some(job => job.kind !== 'install' && job.state === 'completed' && previousJobs.get(job.id) && previousJobs.get(job.id) !== 'completed');
      if (finishedUpload) state.filesDirty = true;
      if (state.filesDirty && connected() && !state.loading && !busyJobs()) await loadFiles(state.path, true);
      if ($('device-dialog').open) await refreshDevices();
    } catch (error) { serviceError(errorMessage(error)); window.HttpLibraryUI?.updateContext({device:null,jobs:state.jobs}); }
    finally { state.pollBusy = false; }
  }
  function wireDropzone(id, kind) {
    const zone = $(id);
    const choose = () => chooseFiles(kind);
    zone.addEventListener('click', choose);
    zone.addEventListener('keydown', event => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); choose(); } });
    let depth = 0;
    zone.addEventListener('dragenter', event => { event.preventDefault(); depth++; if (zone.getAttribute('aria-disabled') !== 'true') zone.classList.add('dragover'); });
    zone.addEventListener('dragover', event => { event.preventDefault(); if (event.dataTransfer) event.dataTransfer.dropEffect = zone.getAttribute('aria-disabled') === 'true' ? 'none' : 'copy'; });
    zone.addEventListener('dragleave', event => { event.preventDefault(); depth = Math.max(0, depth - 1); if (!depth) zone.classList.remove('dragover'); });
    zone.addEventListener('drop', event => { event.preventDefault(); depth = 0; zone.classList.remove('dragover'); const items = [...(event.dataTransfer?.items || [])]; if (items.some(item => item.webkitGetAsEntry?.()?.isDirectory)) { toast('请先打开文件夹，选择其中的文件上传。', true); return; } enqueueFiles([...(event.dataTransfer?.files || [])], kind); });
  }
  document.querySelectorAll('[data-close-dialog]').forEach(control => control.addEventListener('click', () => closeDialog(control.closest('dialog'))));
  document.querySelectorAll('dialog').forEach(dialog => { dialog.addEventListener('cancel', event => { event.preventDefault(); closeDialog(dialog); }); dialog.addEventListener('click', event => { if (event.target === dialog) { const rect = dialog.getBoundingClientRect(); if (event.clientX < rect.left || event.clientX > rect.right || event.clientY < rect.top || event.clientY > rect.bottom) closeDialog(dialog); } }); });
  $('action-form').addEventListener('submit', event => {
    event.preventDefault(); if (!state.action) return;
    const value = $('action-input').value.trim();
    if (state.action.hasInput && !validName(value)) { $('action-error').textContent = '请输入有效名称；不能包含斜杠、反斜杠，也不能使用“.”或“..”。'; $('action-error').hidden = false; $('action-input').focus(); return; }
    const action = state.action; state.action = null; $('action-dialog').close(); action.resolve(action.hasInput ? value : true); lastFocus?.focus?.();
  });
  $('action-input').addEventListener('input', () => { $('action-error').hidden = true; });
  $('choose-device').addEventListener('click', openDevices); $('empty-discover').addEventListener('click', openDevices); $('rescan-button').addEventListener('click', discover);
  $('help-button').addEventListener('click', () => showDialog($('help-dialog')));
  $('current-task-retry').addEventListener('click', () => { if (state.currentTask) chooseFiles(state.currentTask.kind === 'install' ? 'install' : 'upload'); });
  $('current-task-check').addEventListener('click', openDevices);
  $('current-task-cancel').addEventListener('click', () => { if (state.currentTask && !state.currentTask.local) cancelJob(state.currentTask); });
  $('current-task-dismiss').addEventListener('click', () => { if (state.currentTask) state.dismissedTasks.add(state.currentTask.id + ':' + state.currentTask.state); renderCurrentTask(); });
  $('retry-service').addEventListener('click', poll); $('refresh-files').addEventListener('click', () => loadFiles(state.path, true));
  $('mkdir-button').addEventListener('click', newFolder); $('upload-button').addEventListener('click', () => chooseFiles('upload'));
  for (const kind of ['upload']) {
    $(kind + '-input').addEventListener('change', event => { const target = state.pickers[kind] || captureTarget(); delete state.pickers[kind]; enqueueFiles([...event.target.files], kind, target); event.target.value = ''; });
    $(kind + '-input').addEventListener('cancel', () => { delete state.pickers[kind]; });
  }
  $('up-button').addEventListener('click', () => loadFiles(state.path.substring(0, state.path.lastIndexOf('/')) || '/'));
  $('file-search').addEventListener('input', event => { state.query = event.target.value; if (connected() && !state.filesError) renderFiles(); });
  $('file-sort').addEventListener('change', event => { state.sort = event.target.value; if (connected() && !state.filesError) renderFiles(); });
  $('clear-selection').addEventListener('click', () => { state.selected.clear(); renderFiles(); });
  $('download-button').addEventListener('click', () => { const entry = selectedEntries()[0]; if (entry && entry.type !== 'directory') downloadFile(entry); });
  $('rename-button').addEventListener('click', renameFile); $('delete-button').addEventListener('click', deleteFiles);
  for (const view of ['list', 'grid']) $(`view-${view}`).addEventListener('click', () => { state.view = view; for (const key of ['list', 'grid']) { $(`view-${key}`).classList.toggle('active', key === view); $(`view-${key}`).setAttribute('aria-pressed', String(key === view)); } if (connected() && !state.filesError) renderFiles(); });
  document.querySelectorAll('[data-job-filter]').forEach(control => control.addEventListener('click', () => { state.jobFilter = control.dataset.jobFilter; document.querySelectorAll('[data-job-filter]').forEach(item => { item.classList.toggle('active', item === control); item.setAttribute('aria-pressed', String(item === control)); }); renderJobs(); }));
  window.addEventListener('hashchange', () => setPage(location.hash.slice(1)));
  window.addEventListener('dragover', event => event.preventDefault()); window.addEventListener('drop', event => event.preventDefault());
  document.addEventListener('visibilitychange', () => { if (!document.hidden) poll(); });
  wireDropzone('sd-dropzone', 'upload');
  window.SwitchLinkApp = {
    getContext: () => ({device:state.device,jobs:state.jobs}),
    refreshStatus: poll,
    setLibraryBusy: value => {state.libraryBusy = value; renderDevice();},
    acceptJob: job => {const index=state.jobs.findIndex(item=>item.id===job.id);if(index>=0)state.jobs[index]=job;else state.jobs.unshift(job);state.jobsFingerprint='';renderJobs();renderDevice();}
  };
  setPage(location.hash.slice(1)); renderDevice(); renderBreadcrumbs(); renderJobs(); poll();
  setInterval(() => { if (!document.hidden) poll(); }, 2000);
})();
