const { listenForBrowser } = require('./browser-test-helpers.cjs');
const test=require('node:test');
const assert=require('node:assert/strict');
const http=require('node:http');
const fs=require('node:fs');
const path=require('node:path');
const {chromium}=require('playwright');

test('HTTP service and request records remain visible without a native receiver and sent bytes do not claim installation',{timeout:20000},async()=>{
  const library={available:true,can_publish:true,library_id:'prepared',url:'http://192.0.2.169:8080/files/',source:'existing',progress_available:false,files:[{name:'owned.nsp',size:5,href:'/files/owned.nsp'}],transfers:[]};
  const server=http.createServer((req,res)=>{
    const url=new URL(req.url,'http://localhost');const json=data=>{res.setHeader('Content-Type','application/json');res.end(JSON.stringify(data));};
    if(url.pathname==='/api/status')return json({device:null,jobs:[],discovery:{},http_library:library});
    if(url.pathname==='/api/library')return json(library);
    const file=path.join(__dirname,url.pathname==='/'?'index.html':url.pathname);
    if(!file.startsWith(__dirname)||!fs.existsSync(file)){res.statusCode=404;return res.end();}
    res.setHeader('Content-Type',file.endsWith('.js')?'application/javascript':file.endsWith('.css')?'text/css':'text/html');res.end(fs.readFileSync(file));
  });
  await listenForBrowser(server);const browser=await chromium.launch({channel:'msedge',headless:true});
  try{
    const page=await browser.newPage({viewport:{width:1366,height:768}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.goto('http://127.0.0.1:'+server.address().port+'/#install',{waitUntil:'networkidle'});
    assert.match(await page.locator('#http-status').innerText(),/HTTP.*就绪/);
    assert.match(await page.locator('#native-install-status').innerText(),/尚未连接/);
    assert.equal(await page.locator('#install-choose').isDisabled(),true);
    assert.match(await page.locator('#http-source-url').inputValue(),/192\.0\.2\.169:8080\/files\//);
    library.source='managed';library.progress_available=true;library.transfers=[{id:'http1',name:'owned.nsp',state:'sent',bytes_done:5,bytes_total:5,speed:100}];
    await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
    await page.locator('#http-transfers').getByText('本次请求已发送',{exact:true}).waitFor();
    assert.doesNotMatch(await page.locator('#http-transfers').innerText(),/安装完成/);
    assert.match(await page.locator('#http-transfer-note').innerText(),/重复读取.*确认结果/);
    await page.locator('[data-page=sd]').click();assert.match(await page.locator('#topbar-connection').innerText(),/FTP/);
    await page.locator('[data-page=install]').click();assert.equal(await page.locator('#install-choose').isDisabled(),true);
    await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    assert.deepEqual(errors,[]);
  }finally{await browser.close();server.closeAllConnections();await new Promise(r=>server.close(r));}
});
