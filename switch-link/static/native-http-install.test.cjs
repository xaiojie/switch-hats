const { listenForBrowser } = require('./browser-test-helpers.cjs');
const test = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');
const output=process.env.SWITCH_LINK_SCREENSHOTS;
async function capture(page,name){if(output){fs.mkdirSync(output,{recursive:true});await page.screenshot({path:path.join(output,name),fullPage:true,animations:'disabled'});}}

async function harness() {
  const native = id => ({id,name:'Switch '+id,ip:'192.0.2.75',mode:'switch-link',app_version:'0.2.1',connected:true,can_browse:false,can_install:false,http_install_supported:true,http_install_ready:true,http_formats:['nsp','xci']});
  const library = {available:true,can_publish:true,library_id:'fixture-library',url:'http://192.0.2.169:8080/files/',files:[{name:'已有 & 游戏.nsp',size:5,href:'/files/owned.nsp'}],transfers:[]};
  const fixture = {device:native('A'),jobs:[],calls:[],library,native};
  const server = http.createServer(async(req,res)=>{
    const url = new URL(req.url,'http://localhost');
    const json = data => {res.setHeader('Content-Type','application/json');res.end(JSON.stringify(data));};
    if(url.pathname==='/api/status')return json({device:fixture.device,jobs:fixture.jobs,discovery:{},http_library:library});
    if(url.pathname==='/api/library')return json(library);
    if(url.pathname==='/api/library/upload') {
      const chunks=[];for await(const chunk of req)chunks.push(chunk);
      fixture.calls.push({path:url.pathname,query:Object.fromEntries(url.searchParams),body:Buffer.concat(chunks).toString()});
      const file={name:url.searchParams.get('name'),size:Buffer.concat(chunks).length,href:'/files/new.nsp'};
      library.files.push(file);res.statusCode=201;return json({ok:true,file});
    }
    if(url.pathname==='/api/http-install') {
      let raw='';for await(const chunk of req)raw+=chunk;
      const body=JSON.parse(raw);fixture.calls.push({path:url.pathname,body});
      const job={id:'job-'+fixture.calls.length,name:body.name,kind:'install',transport:'http',receiver_mode:'switch-link-http',state:'queued',bytes_done:0,bytes_total:5};
      fixture.jobs.push(job);fixture.onInstall?.(job);res.statusCode=202;return json({ok:true,job});
    }
    if(url.pathname.startsWith('/api/')){fixture.calls.push({path:url.pathname});return json({devices:[]});}
    const file=path.join(__dirname,url.pathname==='/'?'index.html':url.pathname);
    if(!file.startsWith(__dirname)||!fs.existsSync(file)){res.statusCode=404;return res.end();}
    res.setHeader('Content-Type',file.endsWith('.js')?'application/javascript':file.endsWith('.css')?'text/css':'text/html');res.end(fs.readFileSync(file));
  });
  await listenForBrowser(server);
  const browser=await chromium.launch({channel:'msedge',headless:true});
  const page=await browser.newPage({viewport:{width:1366,height:768}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  const poll=async()=>{const response=page.waitForResponse(r=>new URL(r.url()).pathname==='/api/status');await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));await(await response).finished();await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));};
  return {fixture,page,errors,poll,open:()=>page.goto('http://127.0.0.1:'+server.address().port+'/#install',{waitUntil:'networkidle'}),close:async()=>{await browser.close();server.closeAllConnections();await new Promise(r=>server.close(r));}};
}

test('existing library package queues own-client HTTP installation without upload or FTP', {timeout:20000},async()=>{
  const h=await harness();try {
    await h.open();
    assert.match(await h.page.locator('#install-choose').innerText(),/选择安装包并安装/);
    assert.equal(await h.page.locator('#upload-button').isDisabled(),true);
    assert.match(await h.page.locator('#http-status').innerText(),/HTTP.*就绪/);
    assert.match(await h.page.locator('#native-install-status').innerText(),/Switch A.*就绪/);
    await capture(h.page,'native-http-ready.png');
    await h.page.getByRole('button',{name:'安装到 Switch Link：已有 & 游戏.nsp',exact:true}).click();
    await h.page.waitForFunction(()=>document.getElementById('current-task-name').textContent==='已有 & 游戏.nsp');
    assert.deepEqual(h.fixture.calls.filter(c=>c.path==='/api/http-install').map(c=>c.body),[{name:'已有 & 游戏.nsp',expected_device:'A'}]);
    assert.equal(h.fixture.calls.some(c=>['/api/install','/api/files/upload','/api/library/upload'].includes(c.path)),false);
    assert.doesNotMatch(await h.page.locator('#page-install').innerText(),/DBI|SwitchLibrary/);
    assert.deepEqual(h.errors,[]);
  }finally{await h.close();}
});

test('new package is prepared before HTTP install and uses captured device', {timeout:20000},async()=>{
  const h=await harness();try {
    await h.open();const chooserEvent=h.page.waitForEvent('filechooser');await h.page.locator('#install-choose').click();const chooser=await chooserEvent;
    await chooser.setFiles({name:'自己的 & 游戏.nsp',mimeType:'application/octet-stream',buffer:Buffer.from('owned')});
    await h.page.waitForFunction(()=>document.getElementById('current-task-name').textContent==='自己的 & 游戏.nsp');
    assert.deepEqual(h.fixture.calls.filter(c=>c.path.includes('upload')||c.path==='/api/http-install'),[
      {path:'/api/library/upload',query:{name:'自己的 & 游戏.nsp',expected_library:'fixture-library'},body:'owned'},
      {path:'/api/http-install',body:{name:'自己的 & 游戏.nsp',expected_device:'A'}}
    ]);
    await h.page.setViewportSize({width:390,height:844});assert.equal(await h.page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await capture(h.page,'native-http-mobile.png');
    assert.deepEqual(h.errors,[]);
  }finally{await h.close();}
});

for(const timing of ['chooser','upload'])test('device change during '+timing+' retains prepared package without installing elsewhere',{timeout:20000},async()=>{
  const h=await harness();try {
    await h.open();const chooserEvent=h.page.waitForEvent('filechooser');await h.page.locator('#install-choose').click();const chooser=await chooserEvent;
    let pending,uploadRoute;
    if(timing==='upload') {const intercepted=new Promise(r=>{pending=r;});await h.page.route('**/api/library/upload?*',route=>{uploadRoute=route;pending();});await chooser.setFiles({name:'prepared.nsp',mimeType:'application/octet-stream',buffer:Buffer.from('owned')});await intercepted;const box=await h.page.locator('#http-upload-progress').boundingBox();assert.ok(box.y>=0&&box.y+box.height<768,'package preparation stays above the fold');}
    h.fixture.device=h.fixture.native('B');await h.poll();
    if(timing==='chooser')await chooser.setFiles({name:'prepared.nsp',mimeType:'application/octet-stream',buffer:Buffer.from('owned')});else await uploadRoute.continue();
    await h.page.locator('#library-files').getByText('prepared.nsp',{exact:true}).waitFor();
    await h.page.waitForFunction(()=>/已保留|已准备/.test(document.getElementById('install-notice-text').textContent));
    assert.equal(h.fixture.calls.filter(c=>c.path==='/api/library/upload').length,1);
    assert.equal(h.fixture.calls.some(c=>c.path==='/api/http-install'),false);
    assert.match(await h.page.locator('#install-notice-text').innerText(),/主机.*变化/);
  }finally{await h.close();}
});

test('old client and DBI cannot install; native and HTTP readiness remain separate',{timeout:20000},async()=>{
  const h=await harness();try {
    h.fixture.device={...h.fixture.device,app_version:'0.1.1',http_install_supported:false,http_install_ready:false,can_install:true};
    await h.open();assert.equal(await h.page.locator('#install-choose').isDisabled(),true);
    assert.match(await h.page.locator('#native-install-status').innerText(),/0\.2\.1.*升级|升级.*0\.2\.1/);
    assert.match(await h.page.locator('#http-status').innerText(),/HTTP.*就绪/);
    h.fixture.device={...h.fixture.device,mode:'dbi-install'};await h.poll();
    assert.equal(await h.page.getByRole('button',{name:'安装到 Switch Link：已有 & 游戏.nsp',exact:true}).isDisabled(),true);
    h.fixture.device=h.fixture.native('A');h.fixture.library.available=false;h.fixture.library.error='服务端口未就绪';await h.poll();
    assert.match(await h.page.locator('#native-install-status').innerText(),/Switch A.*就绪/);
    assert.equal(await h.page.locator('#install-choose').isDisabled(),true);
    h.fixture.library.available=true;h.fixture.device.http_install_ready=false;await h.poll();
    assert.match(await h.page.locator('#http-status').innerText(),/HTTP.*就绪/);
    assert.match(await h.page.locator('#native-install-status').innerText(),/接收页面/);
    h.fixture.device.http_install_ready=true;h.fixture.library.native_install_available=false;h.fixture.library.native_install_error='安装服务尚未就绪';await h.poll();
    assert.match(await h.page.locator('#native-install-status').innerText(),/Switch A.*就绪/);
    assert.match(await h.page.locator('#http-status').innerText(),/HTTP.*就绪/);
    assert.equal(await h.page.locator('#install-choose').isDisabled(),true,'an existing HTTP library alone does not enable the native installation channel');
    assert.equal(h.fixture.calls.some(c=>['/api/http-install','/api/install','/api/files/upload'].includes(c.path)),false);
  }finally{await h.close();}
});

test('native step percent and actual HTTP bytes never imply completion; registration awaits receipt',{timeout:20000},async()=>{
  const h=await harness();try {
    const job={id:'native',name:'owned.nsp',kind:'install',transport:'http',receiver_mode:'switch-link-http',state:'installing',stage:'installing',progress:100,bytes_done:5,bytes_total:5,network_bytes:37,native_progress:42.5,native_bytes_done:425,native_bytes_total:1000,native_speed:10};h.fixture.jobs=[job];
    await h.open();assert.equal(await h.page.locator('#current-task-percent').innerText(),'当前步骤 42.5%');
    assert.match(await h.page.locator('#current-task-bytes').innerText(),/网络实际发送 37 B/);
    assert.doesNotMatch(await h.page.locator('#current-task-stage').innerText(),/安装完成/);
    job.state='registering';job.stage='registering';job.native_progress=100;await h.poll();
    assert.equal(await h.page.locator('#current-task-bar').getAttribute('aria-valuenow'),null);
    await capture(h.page,'native-http-registering.png');
    assert.match(await h.page.locator('#current-task-percent').innerText(),/等待结果/);
    assert.equal(await h.page.locator('#current-task-cancel').isVisible(),false);
    assert.equal(await h.page.locator('#mkdir-button').isDisabled(),true);
    job.state='completed';await h.poll();assert.equal(await h.page.locator('#current-task-stage').innerText(),'安装完成');
    assert.match(await h.page.locator('#current-task-helper-text').innerText(),/主机已确认/);
    assert.deepEqual(h.errors,[]);
  }finally{await h.close();}
});

test('changing library identity during preparation cannot install a same-named file from a replacement library',{timeout:20000},async()=>{
  const h=await harness();try{
    await h.open();const chooserEvent=h.page.waitForEvent('filechooser');await h.page.locator('#install-choose').click();const chooser=await chooserEvent;
    let uploadRoute,signal;const pending=new Promise(r=>{signal=r;});await h.page.route('**/api/library/upload?*',route=>{uploadRoute=route;signal();});
    await chooser.setFiles({name:'prepared.nsp',mimeType:'application/octet-stream',buffer:Buffer.from('owned')});await pending;
    h.fixture.library.library_id='replacement-library';await h.poll();await uploadRoute.continue();
    await h.page.locator('#library-files').getByText('prepared.nsp',{exact:true}).waitFor();
    await h.page.waitForFunction(()=>document.getElementById('http-upload-progress').hidden);
    assert.equal(h.fixture.calls.some(c=>c.path==='/api/http-install'),false);
    assert.match(await h.page.locator('#install-notice-text').innerText(),/游戏库.*变化.*保留/);
  }finally{await h.close();}
});

test('a captured multi-file choice queues every package while its first native installation becomes busy',{timeout:20000},async()=>{
  const h=await harness();try{
    h.fixture.onInstall=job=>{h.fixture.device.http_install_ready=false;job.state='installing';job.stage='installing';};
    await h.open();const chooserEvent=h.page.waitForEvent('filechooser');await h.page.locator('#install-choose').click();const chooser=await chooserEvent;
    await chooser.setFiles(['first.nsp','second.nsp'].map(name=>({name,mimeType:'application/octet-stream',buffer:Buffer.from('owned')})));
    await h.page.locator('#library-files').getByText('second.nsp',{exact:true}).waitFor();
    await h.page.waitForFunction(()=>document.getElementById('http-upload-progress').hidden);
    assert.deepEqual(h.fixture.calls.filter(c=>c.path==='/api/http-install').map(c=>c.body),[{name:'first.nsp',expected_device:'A'},{name:'second.nsp',expected_device:'A'}]);
    assert.deepEqual(h.errors,[]);
  }finally{await h.close();}
});
