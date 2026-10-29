const {listenForBrowser}=require('./browser-test-helpers.cjs');
const test=require('node:test'),assert=require('node:assert/strict'),http=require('node:http'),fs=require('node:fs'),path=require('node:path');
const {chromium}=require('playwright');
test('music tab explicitly chooses directory, edits sources and reports sync rejection',{timeout:25000},async()=>{
 let configured=false,saved=null; const calls=[];
 const state=()=>({root:configured?'C:\\Songs':'',library_id:configured?'chosen':'unconfigured',tracks:configured?[{id:'song',title:'晴天',artist:'周杰伦',album:'叶惠美',cover:'',lyrics:''}]:[],sources:saved||{version:1,sources:[]},overrides:{version:1,tracks:{}},available:true,catalog_url:'http://192.0.2.1:8080/music-library.json',m3u_url:'http://192.0.2.1:8080/music-list.m3u8'});
 const server=http.createServer(async(req,res)=>{const u=new URL(req.url,'http://localhost');const json=(d,status=200)=>{res.statusCode=status;res.setHeader('Content-Type','application/json');res.end(JSON.stringify(d));};let raw='';for await(const chunk of req)raw+=chunk;const data=raw?JSON.parse(raw):{};
 if(u.pathname==='/api/status')return json({device:{id:'switch',connected:true,can_browse:true},jobs:[],discovery:{},http_library:{files:[],transfers:[]}});
 if(u.pathname==='/api/library')return json({files:[],transfers:[]});
 if(u.pathname==='/api/files')return json({entries:[],path:'/'});
 if(u.pathname==='/api/music'&&req.method==='GET')return json(state());
 if(u.pathname==='/api/music'&&req.method==='PUT'){calls.push(data);configured=true;return json(state());}
 if(u.pathname==='/api/music/sources'){saved={version:data.version,sources:data.sources};return json(saved);}
 if(u.pathname==='/api/music/sync'){calls.push(data);return json({ok:false,error:'任务正在进行，请等待完成'},409);}
 const file=path.join(__dirname,u.pathname==='/'?'index.html':u.pathname);if(!file.startsWith(__dirname)||!fs.existsSync(file)){res.statusCode=404;return res.end();}res.setHeader('Content-Type',file.endsWith('.js')?'application/javascript':file.endsWith('.css')?'text/css':'text/html');res.end(fs.readFileSync(file));});
 await listenForBrowser(server);const browser=await chromium.launch({channel:'msedge',headless:true});try{const page=await browser.newPage();page.setDefaultTimeout(5000);const errors=[];page.on('pageerror',e=>errors.push(e.message));await page.goto('http://127.0.0.1:'+server.address().port+'/#music',{waitUntil:'networkidle'});
 await page.locator('#music-root').fill('C:\\Songs');await page.locator('#music-configure').click();await page.locator('#music-title').waitFor();assert.equal(await page.locator('#music-title').inputValue(),'晴天');assert.equal(calls[0].expected_library,'unconfigured');
 await page.locator('#music-add-source').click();await page.locator('[data-source-name]').fill('家里');await page.locator('[data-source-url]').fill('https://example.com/library.json');await page.locator('#music-save-sources').click();await page.locator('#music-message').filter({hasText:'来源已保存'}).waitFor();assert.equal(saved.sources[0].name,'家里');
 await page.locator('#music-sync').click();await page.locator('#music-message').filter({hasText:'任务正在进行'}).waitFor();assert.equal(calls[1].expected_device,'switch');assert.deepEqual(errors,[]);
 await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
 }finally{await browser.close();server.closeAllConnections();await new Promise(r=>server.close(r));}
});
