'use strict';

(() => {
  const $ = id => document.getElementById(id);
  const { formatBytes, canInstall, activeStates } = SwitchLink;
  let state = null, context = {device:null,jobs:[]}, busy = false, picker = null, message = '', fileFingerprint = '', transferFingerprint = '', targetGeneration = 0;
  const element = (tag, className, text) => { const n = document.createElement(tag); n.className = className || ''; if (text !== undefined) n.textContent = text; return n; };
  const action = (text, callback) => { const b = element('button', 'text-button', text); b.type = 'button'; b.addEventListener('click', callback); return b; };
  const deviceKey = device => JSON.stringify([device?.id,device?.mode,device?.app_version,device?.connected,device?.http_install_supported]);
  const capture = () => ({library:state?.library_id,expected_device:context.device?.id,generation:targetGeneration,acceptedJobs:[]});
  const sameTarget = target => !!target?.expected_device && target.expected_device === context.device?.id && target.generation === targetGeneration;
  const serviceReady = () => state?.available === true && state?.native_install_available !== false;
  const activeInstall = () => context.jobs.some(job => activeStates.has(job.state) || (job.transport === 'http' && job.state === 'awaiting_confirmation'));
  const ready = () => serviceReady() && canInstall(context.device) && !activeInstall();
  function setBusy(value) { busy = value; window.SwitchLinkApp?.setLibraryBusy(value); render(); }
  function updateContext(data) {
    if (deviceKey(context.device) !== deviceKey(data.device)) targetGeneration++;
    context = {device:data.device || null,jobs:Array.isArray(data.jobs) ? data.jobs : []};
    render();
  }
  async function refresh() {
    try {
      const response = await fetch('/api/library'); const data = await response.json();
      if (!response.ok || data.ok === false) throw Error(data.error || '无法读取游戏库');
      update(data);
    } catch (error) { message = error.message || 'HTTP 游戏库暂不可访问'; render(); }
  }
  function update(data) { if (data && typeof data === 'object') { state = data; render(); } }
  async function copy(text) {
    if (!text) return;
    try { await navigator.clipboard.writeText(text); message = '已复制 HTTP 游戏库地址。'; }
    catch { message = '无法自动复制，请选中上方地址手动复制。'; }
    render();
  }
  function nativeStatus() {
    const device = context.device;
    if (!device?.connected) return '尚未连接 Switch Link。请在主机打开接收页面，再发现主机。';
    if (!SwitchLink.supportsHttpInstall(device)) return '请在主机升级并打开 Switch Link 0.2.3 或更新版本。';
    if (!device.http_install_ready) return (device.name || 'Switch') + '：请打开 Switch Link 接收页面并等待当前任务结束。';
    return (device.name || 'Switch') + ' · Switch Link 安装已就绪';
  }
  function render() {
    const available = state?.available === true, installReady = ready();
    $('install-choose').disabled = !state?.can_publish || !installReady || busy;
    $('install-dropzone').setAttribute('aria-disabled',String(!state?.can_publish || !installReady || busy));
    $('http-refresh').disabled = busy;
    $('http-source-url').value = state?.url || ''; $('http-copy-url').disabled = !state?.url;
    $('http-status').textContent = available ? 'HTTP 游戏库已就绪' : state ? 'HTTP 服务暂不可用' : '正在检查 HTTP 服务';
    $('native-install-status').textContent = nativeStatus();
    $('install-notice').classList.toggle('ready',installReady);
    $('install-notice-text').textContent = message || (!serviceReady() ? state?.native_install_error || state?.error || '正在检查 HTTP 安装服务。' : !canInstall(context.device) ? nativeStatus() : activeInstall() ? '安装任务进行中，请保持主机接收页面打开。' : '选择安装包后，Switch Link 会通过 HTTP 下载并安装到 SD 卡。');
    $('http-access').textContent = state?.native_install_available === false ? state.native_install_error || 'HTTP 安装通道暂不可用。' : '电脑 HTTP 服务与主机安装接收状态分别检查。游戏内容由主机主动读取。';
    $('http-transfer-note').textContent = '下方仅记录 HTTP 请求发送的字节，可能包含重复读取；安装结果以 Switch Link 返回的确认结果为准。';
    if (!$('page-install').hidden) {
      const pill = $('topbar-connection'); pill.classList.toggle('connected',installReady);
      pill.lastElementChild.textContent = installReady ? 'HTTP 安装就绪' : 'HTTP 安装待就绪';
    }
    const formats = context.device?.http_formats || [];
    $('install-formats').textContent = formats.length ? formats.map(value=>String(value).toUpperCase()).join(' / ') : 'NSP / XCI';
    $('install-input').accept = formats.length ? formats.map(value=>'.'+value).join(',') : '.nsp,.xci';
    const files = Array.isArray(state?.files) ? state.files : []; $('library-ready-count').textContent = files.length;
    const fingerprint = JSON.stringify([files,state?.url,installReady,busy,formats]);
    if (fileFingerprint !== fingerprint) {
      fileFingerprint = fingerprint;
      const rows = files.map(file => {
        const row = element('article','library-file'), info = element('div','library-file-info');
        const supported = formats.includes(String(file.name).split('.').pop().toLowerCase());
        info.append(element('strong','',file.name),element('span','',formatBytes(file.size) + (supported ? ' · 电脑安装包已准备' : ' · 当前主机不支持此格式')));
        const install = action('安装到 Switch Link',()=>installExisting(file.name));
        install.setAttribute('aria-label','安装到 Switch Link：'+file.name); install.disabled = !installReady || busy || !supported;
        row.append(info,element('span','library-ready','已准备'),install); return row;
      });
      if (!rows.length) {const empty=element('div','queue-empty');empty.append(element('h3','','把下一款游戏加入这里'),element('p','','选择安装包后先准备电脑上的文件，再交给主机安装。'));rows.push(empty);}
      $('library-files').replaceChildren(...rows);
    }
    const transfers = Array.isArray(state?.transfers) ? state.transfers : [], tf = JSON.stringify(transfers);
    if (tf !== transferFingerprint) {
      transferFingerprint = tf;
      const rows = transfers.slice(0,8).map(transfer => {
        const row=element('article','job-card'), top=element('div','job-top');
        top.append(element('strong','job-title',transfer.name),element('span','job-status',transfer.state==='sent'?'本次请求已发送':transfer.state==='interrupted'?'本次连接已中断':'Switch 正在读取'));
        const meta=element('div','job-meta');meta.append(element('span','',formatBytes(transfer.bytes_done)+' / '+formatBytes(transfer.bytes_total)+' · HTTP 请求'),element('span','',transfer.state==='transferring'?formatBytes(transfer.speed)+'/s':'等待主机安装结果'));
        row.append(top,meta);return row;
      });
      if (!rows.length) rows.push(element('p','subtle-empty','等待 Switch Link 通过 HTTP 读取安装包。'));
      $('http-transfers').replaceChildren(...rows);
    }
  }
  function choose() {
    if (!state?.can_publish || !ready() || busy) return;
    picker = capture(); $('install-input').click();
  }
  function send(file, identity) {
    return new Promise((resolve,reject)=>{
      const xhr=new XMLHttpRequest();xhr.open('PUT','/api/library/upload?'+new URLSearchParams({name:file.name,expected_library:identity}));
      const began=performance.now();xhr.upload.onprogress=event=>{
        if(!event.lengthComputable)return;const percent=Math.min(100,event.loaded/event.total*100);
        $('http-upload-fill').style.width=percent+'%';$('http-upload-caption').textContent=percent.toFixed(1)+'% · '+formatBytes(event.loaded)+' / '+formatBytes(event.total)+' · '+formatBytes(event.loaded/Math.max(.05,(performance.now()-began)/1000))+'/s';
      };
      xhr.onload=()=>{try{const data=JSON.parse(xhr.responseText);if(xhr.status<200||xhr.status>=300||data.ok===false)throw Error(data.error||'准备安装包失败');resolve(data);}catch(error){reject(error);}};
      xhr.onerror=()=>reject(Error('接收中断，未完成的安装包不会加入游戏库。'));xhr.onabort=()=>reject(Error('已取消接收。'));xhr.send(file);
    });
  }
  async function submit(name,target) {
    await window.SwitchLinkApp?.refreshStatus();
    if (target.library !== state?.library_id) throw Error('电脑游戏库已变化，安装已取消。');
    if (!sameTarget(target)) throw Error('主机连接已变化，安装已取消。');
    if (!serviceReady()) throw Error(state?.native_install_error || 'HTTP 安装服务尚未就绪。');
    const ownPending = context.jobs.some(job=>target.acceptedJobs.includes(job.id) && !SwitchLink.terminalStates.has(job.state));
    if (!canInstall(context.device) && !(context.device?.connected && SwitchLink.supportsHttpInstall(context.device) && ownPending)) throw Error(nativeStatus());
    const extension=String(name).split('.').pop().toLowerCase();
    if (!context.device.http_formats?.includes(extension)) throw Error('当前主机不支持 '+extension.toUpperCase()+' 安装包。');
    const response=await fetch('/api/http-install',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name,expected_device:target.expected_device})});
    const data=await response.json();if(!response.ok||data.ok===false)throw Error(data.error||'无法启动主机安装');
    if(data.job){target.acceptedJobs.push(data.job.id);window.SwitchLinkApp?.acceptJob(data.job);}
    message='已提交给 '+(context.device.name||'Switch Link')+'，请保持主机接收页面打开。';
  }
  async function installExisting(name) {
    if (busy || !ready()) return; const target=capture();setBusy(true);message='';
    try {await submit(name,target);}catch(error){message=error.message+' 安装包已保留在电脑游戏库。';}finally{setBusy(false);}
  }
  async function publish(files,target) {
    if (!files.length || busy || !state?.can_publish || !target) return;
    if (target.library !== state.library_id) {message='游戏库已变化，请重新选择安装包。';render();return;}
    setBusy(true);message='';
    try {
      for (const file of files) {
        if (!/\.(nsp|xci|nsz|xcz)$/i.test(file.name)) {message='请选择主机支持的游戏安装包。';continue;}
        if (target.library!==state.library_id) throw Error('游戏库已变化，已停止准备后续文件。');
        $('http-upload-progress').hidden=false;$('http-upload-name').textContent=file.name;$('http-upload-caption').textContent='正在准备电脑上的安装包…';$('http-upload-fill').style.width='0%';
        await send(file,target.library);await refresh();
        try {await submit(file.name,target);}catch(error){message=error.message+' 安装包已保留在电脑游戏库，请重新选择目标主机后安装。';break;}
      }
    }catch(error){message=error.message;}finally{$('http-upload-progress').hidden=true;setBusy(false);}
  }
  $('install-choose').addEventListener('click',choose);
  $('install-input').addEventListener('change',event=>{const target=picker;picker=null;publish([...event.target.files],target);event.target.value='';});
  $('install-input').addEventListener('cancel',()=>{picker=null;});
  const drop=$('install-dropzone');drop.addEventListener('click',choose);
  drop.addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();choose();}});
  drop.addEventListener('dragover',e=>{e.preventDefault();if(ready()&&!busy)drop.classList.add('dragover');});
  drop.addEventListener('dragleave',()=>drop.classList.remove('dragover'));
  drop.addEventListener('drop',e=>{e.preventDefault();drop.classList.remove('dragover');if(ready())publish([...e.dataTransfer.files],capture());});
  $('http-copy-url').addEventListener('click',()=>copy(state?.url));
  $('http-refresh').addEventListener('click',()=>{message='';refresh();window.SwitchLinkApp?.refreshStatus();});
  window.HttpLibraryUI={update,updateContext,render,choose,refresh};
  updateContext(window.SwitchLinkApp?.getContext()||{device:null,jobs:[]});refresh();
})();
