import {parseCatalog, parseLyrics, summarize, timeLabel} from './core.mjs';
const $ = id => document.getElementById(id);
const state = {catalog:null,url:'',selected:null,filter:'all',checks:[],cors:null,headers:{},epoch:0,selectionEpoch:0};
let loadAbort, assetAbort, checkAbort, currentLyrics=null, activeLine=-1, manualScrollUntil=0;
const node = (tag, className, text) => {const e=document.createElement(tag); if(className)e.className=className;if(text!==undefined)e.textContent=text;return e;};
const badge = (text, kind='') => node('span', `badge ${kind}`, text);
const errorMessage = error => error.name==='AbortError'?'操作已取消':error.message;

async function boundedText(response, limit=8*1024*1024) {
  if(!response.ok)throw new Error(`HTTP ${response.status}`);
  const reader=response.body.getReader();let size=0;const chunks=[];
  for(;;){const {done,value}=await reader.read();if(done)break;size+=value.length;if(size>limit){await reader.cancel();throw new Error('资源超过 8 MiB 上限');}chunks.push(value);}
  const data=new Uint8Array(size);let at=0;for(const chunk of chunks){data.set(chunk,at);at+=chunk.length;}
  return new TextDecoder('utf-8',{fatal:true}).decode(data);
}

async function readResource(url, signal, proxy=$('proxyMode').checked) {
  const deadline=AbortSignal.timeout(15000),requestSignal=signal?AbortSignal.any([signal,deadline]):deadline;
  try{
    if(proxy){const response=await fetch(`/api/fetch?url=${encodeURIComponent(url)}`,{signal:requestSignal,cache:'no-store'});const data=await response.json();if(!data.ok)throw new Error(data.error||`HTTP ${data.status}`);return {text:data.text,headers:data.headers,status:data.status,url:data.url};}
    const response=await fetch(url,{signal:requestSignal,cache:'no-store'});
    const headers={};for(const key of ['Content-Type','Content-Length','ETag','Last-Modified','Cache-Control','Access-Control-Allow-Origin'])headers[key]=response.headers.get(key);
    return {text:await boundedText(response),headers,status:response.status,url:response.url};
  }catch(error){if(deadline.aborted&&!signal?.aborted)throw new Error('读取超时，请稍后重试');throw error;}
}

function renderSummary(){
  const s=summarize(state.catalog.tracks);const metrics=[['曲库歌曲',s.total,null,'真实接口记录'],['歌手信息',s.artist,s.total,state.catalog.type==='M3U'?'从列表名称解析':'artist 字段'],['提供时长',s.duration,s.total,'字段为有效秒数'],['提供封面',s.cover,s.total,'地址存在，待资源检查'],['提供歌词',s.lyrics,s.total,'地址存在，待资源检查'],['Switch 可播放',s.playable,s.total,'当前仅支持 MP3']];
  $('summary').replaceChildren(...metrics.map(([label,value,total,note])=>{const item=node('div','stat');item.append(node('div','stat-label',label));const amount=node('div','stat-value',String(value));if(total!==null)amount.append(node('small','',` / ${total}`));item.append(amount,node('div',`stat-note ${total===null||value===total?'good':'warn'}`,note));return item;}));
}

function renderTracks(){
  if(!state.catalog)return;
  const query=$('search').value.trim().toLocaleLowerCase(),album=$('albumFilter').value;
  const filtered=state.catalog.tracks.filter(t=>(!album||t.album===album)&&(!query||`${t.title} ${t.artist} ${t.album}`.toLocaleLowerCase().includes(query))&&(state.filter==='all'||state.filter==='cover'&&!t.cover||state.filter==='lyrics'&&!t.lyrics||state.filter==='metadata'&&(!t.artist||!t.album||!t.duration)||state.filter==='format'&&!t.playable));
  $('listCount').textContent=`显示 ${filtered.length} / ${state.catalog.tracks.length} 首`;
  if(!filtered.length){$('trackList').replaceChildren(node('p','empty','没有符合条件的歌曲，试试其他筛选。'));return;}
  $('trackList').replaceChildren(...filtered.map(t=>{const button=node('button',`track-row ${state.selected?.key===t.key?'selected':''}`);button.type='button';button.dataset.key=t.key;button.setAttribute('aria-label',`${t.title}，${t.artist||'未提供歌手'}`);button.setAttribute('aria-pressed',String(state.selected?.key===t.key));button.append(node('span','track-number',String(state.catalog.tracks.indexOf(t)+1).padStart(2,'0')));const copy=node('span','track-copy');copy.append(node('div','track-name',t.title),node('div','track-meta',`${t.artist||'未提供歌手'} · ${t.album||'未提供专辑'}`));const right=node('span','track-right');right.append(node('span','track-time',timeLabel(t.duration)));const flags=node('span','mini-badges');for(const [present,label] of [[!!t.cover,'封面'],[!!t.lyrics,'歌词']])flags.append(node('span',`mini-badge ${present?'':'missing'}`,present?label:`缺${label}`));if(!t.playable)flags.append(node('span','mini-badge missing',t.format.toUpperCase()));right.append(flags);button.append(copy,right);return button;}));
}

function setLyricsMessage(message,kind='未提供'){$('lyrics').replaceChildren(node('p','empty',message));$('lyricKind').textContent=kind;}

async function selectTrack(track){
  assetAbort?.abort();assetAbort=new AbortController();const epoch=++state.selectionEpoch;state.selected=track;currentLyrics=null;activeLine=-1;
  const audio=$('audio');audio.pause();audio.removeAttribute('src');audio.load();audio.src=track.audio;audio.preload='none';
  $('playButton').disabled=false;$('playButton').textContent='播放试听';$('checkSelected').disabled=false;
  $('trackTitle').textContent=track.title;$('trackArtist').textContent=track.artist||'接口尚未提供歌手';$('trackAlbum').textContent=track.album||'接口尚未提供专辑';$('trackFormat').textContent=track.format.toUpperCase();
  $('trackFormat').className=`badge ${track.playable?'':'warn'}`;
  $('trackBadges').replaceChildren(badge(track.duration?`曲库时长 ${timeLabel(track.duration)}`:'曲库未提供时长',track.duration?'':'warn'),badge(track.playable?'Switch 支持':'Switch 暂不支持',track.playable?'':'warn'));
  $('audioState').textContent='等待点击播放。浏览器原生播放器提供进度和音量控制。';
  const raw={...track};delete raw.key;delete raw.playable;$('rawTrack').textContent=JSON.stringify(raw,null,2);renderTracks();
  $('coverImage').hidden=true;$('coverEmpty').classList.remove('hidden');$('coverEmpty').lastElementChild.textContent=track.cover?'正在加载真实封面…':'这首歌未提供封面';
  if(track.cover){const image=new Image();image.onload=()=>{if(epoch!==state.selectionEpoch)return;$('coverImage').src=track.cover;$('coverImage').hidden=false;$('coverEmpty').classList.add('hidden');};image.onerror=()=>{if(epoch===state.selectionEpoch)$('coverEmpty').lastElementChild.textContent='封面加载失败，请运行资源检查';};image.src=track.cover;}
  if(!track.lyrics){setLyricsMessage(track.lyricsStatus==='instrumental'?'这是一首纯音乐。':state.catalog.type==='M3U'?'M3U 未携带歌词地址，切换 JSON 曲库可查看完整元数据。':'接口未提供这首歌的歌词地址。');return;}
  setLyricsMessage('正在读取歌词…','读取中');
  try{const data=await readResource(track.lyrics,assetAbort.signal);if(epoch!==state.selectionEpoch)return;currentLyrics=parseLyrics(data.text);if(!currentLyrics.lines.length&&!currentLyrics.plain)throw new Error('歌词文件为空');$('lyricKind').textContent=currentLyrics.synced?`同步歌词 · ${currentLyrics.lines.length} 行`:'纯文本歌词';if(currentLyrics.synced)$('lyrics').replaceChildren(...currentLyrics.lines.map(line=>node('p','lyric-line',line.text||'♪')));else $('lyrics').textContent=currentLyrics.plain;}
  catch(error){if(epoch!==state.selectionEpoch||error.name==='AbortError')return;setLyricsMessage(`歌词读取失败：${errorMessage(error)}`,'读取失败');}
}

async function loadCatalog(){
  loadAbort?.abort();assetAbort?.abort();checkAbort?.abort();loadAbort=new AbortController();const epoch=++state.epoch;
  const url=$('sourceUrl').value.trim();try{const parsed=new URL(url);if(parsed.protocol!=='https:'||parsed.hostname!=='kiro.papaclaoud.top'||parsed.username||parsed.password)throw new Error();}catch{$('loadError').textContent='请输入 kiro.papaclaoud.top 的 HTTPS 曲库地址。';$('loadError').classList.remove('hidden');return;}
  $('loadButton').disabled=true;$('loadButton').textContent='读取中…';$('loadError').classList.add('hidden');$('connectionState').textContent='正在连接真实接口…';state.checks=[];$('checkResults').replaceChildren();$('checkProgress').textContent='尚未运行资源检查。';state.cors=null;
  try{const viaProxy=$('proxyMode').checked;const data=await readResource(url,loadAbort.signal);if(epoch!==state.epoch)return;const catalog=parseCatalog(data.text,data.url);state.catalog=catalog;state.url=url;state.headers=data.headers;if(!viaProxy)state.cors={ok:true,mode:'direct',url};else if(!state.cors)state.cors={ok:null,mode:'proxy',url};
    $('connectionState').textContent=viaProxy?`本地代理读取 · HTTP ${data.status} · ${state.cors.ok===true?'浏览器直连另已通过':'浏览器直连未验证'}`:`浏览器直连正常 · HTTP ${data.status}`;
    $('libraryTitle').textContent=catalog.name;
    const generated=new Date(catalog.generated);$('generated').textContent=catalog.generated?`更新：${Number.isNaN(generated.getTime())?catalog.generated:generated.toLocaleString('zh-CN',{timeZone:'Asia/Shanghai',month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit',hour12:false})}`:`${catalog.type} 格式`;
    const albums=[...new Set(catalog.tracks.map(t=>t.album).filter(Boolean))].sort();$('albumFilter').replaceChildren(new Option('全部专辑',''),...albums.map(album=>new Option(album,album)));
    $('responseInfo').textContent=JSON.stringify({format:catalog.type,version:catalog.version,generated:catalog.generated,headers:data.headers,note:viaProxy?'上游响应头由本地服务读取，不代表浏览器 CORS 已通过。':'浏览器只能读取跨域允许暴露的响应头；null 不一定表示服务器未发送。',serverStats:catalog.stats},null,2);
    renderSummary();renderTracks();$('checkAll').disabled=!catalog.tracks.length;$('exportReport').disabled=false;
    $('jsonSource').classList.toggle('active',catalog.type==='JSON');$('m3uSource').classList.toggle('active',catalog.type==='M3U');
    const previous=state.selected?.audio;const first=catalog.tracks.find(t=>t.audio===previous)||catalog.tracks.find(t=>t.title.includes('菊花台'))||catalog.tracks[0];
    if(first)await selectTrack(first);else{++state.selectionEpoch;state.selected=null;currentLyrics=null;const audio=$('audio');audio.pause();audio.removeAttribute('src');audio.load();$('playButton').disabled=true;$('checkSelected').disabled=true;$('trackTitle').textContent='曲库目前没有歌曲';$('trackArtist').textContent='加载其他曲库后再试。';$('trackAlbum').textContent='';$('trackBadges').replaceChildren();$('trackFormat').textContent='未选择';$('rawTrack').textContent='尚未选择';$('coverImage').hidden=true;$('coverEmpty').classList.remove('hidden');$('coverEmpty').lastElementChild.textContent='暂无封面';setLyricsMessage('加载其他曲库后再试。');}
  }catch(error){if(epoch!==state.epoch||error.name==='AbortError')return;$('connectionState').textContent='读取失败';$('loadError').textContent=`${errorMessage(error)}。如果浏览器无法读取，可打开“本地代理”查看数据，再单独验证直连配置。`;$('loadError').classList.remove('hidden');if(!state.catalog)$('trackList').replaceChildren(node('p','empty','曲库读取失败，请检查地址或切换读取模式。'));}
  finally{if(epoch===state.epoch){$('loadButton').disabled=false;$('loadButton').textContent='重新加载 ↻';}}
}

async function testCors(){
  const url=$('sourceUrl').value.trim(),epoch=state.epoch;$('corsButton').disabled=true;
  try{const data=await readResource(url,AbortSignal.timeout(15000),false);parseCatalog(data.text,data.url);if(epoch!==state.epoch||$('sourceUrl').value.trim()!==url)return;state.cors={ok:true,url,checkedAt:new Date().toISOString()};$('connectionState').textContent=`浏览器直连通过 · HTTP ${data.status}${$('proxyMode').checked?' · 当前列表仍以代理模式加载':''}`;}
  catch(error){if(epoch!==state.epoch||$('sourceUrl').value.trim()!==url)return;state.cors={ok:false,url,error:errorMessage(error),checkedAt:new Date().toISOString()};$('connectionState').textContent=`浏览器直连失败：${errorMessage(error)}（请检查 CORS 或网络）`;}
  finally{$('corsButton').disabled=false;}
}

function renderChecks(){
  $('checkResults').replaceChildren(...state.checks.map(result=>{const card=node('div','check-item');card.append(node('span',result.ok&&result.valid?'ok':'bad',`${result.kind==='audio'?'音频分段':result.kind==='cover'?'封面图片':'歌词文件'} · ${result.status||'失败'}`),node('strong','',result.title),node('p','',result.error||result.message||'检查未通过'));return card;}));
}

async function runChecks(all){
  if(!state.catalog||!state.selected)return;checkAbort?.abort();checkAbort=new AbortController();const signal=checkAbort.signal,epoch=state.epoch;
  const selected=all?state.catalog.tracks:[state.selected],tasks=[],seen=new Set();
  for(const track of selected)for(const kind of all?['cover','lyrics']:['audio','cover','lyrics']){const url=kind==='audio'?track.audio:track[kind];if(url&&!seen.has(`${kind}:${url}`)){tasks.push({kind,url,title:track.title});seen.add(`${kind}:${url}`);}}
  state.checks=[];renderChecks();let next=0,done=0;$('checkSelected').disabled=true;$('checkAll').disabled=true;
  $('checkProgress').textContent=tasks.length?`正在检查 ${tasks.length} 个不同资源…`:'所选范围没有提供资源地址。';
  async function worker(){while(next<tasks.length&&!signal.aborted){const task=tasks[next++];let result;try{const response=await fetch(`/api/check?kind=${task.kind}&url=${encodeURIComponent(task.url)}`,{signal,cache:'no-store'});result=await response.json();}catch(error){if(signal.aborted)return;result={ok:false,error:errorMessage(error)};}if(epoch!==state.epoch||signal.aborted)return;state.checks.push({...task,...result});++done;const passed=state.checks.filter(x=>x.ok&&x.valid).length;$('checkProgress').textContent=`已检查 ${done} / ${tasks.length} 个资源 · 通过 ${passed} · 异常 ${done-passed}（本地服务检查）`;renderChecks();}}
  try{await Promise.all([worker(),worker(),worker()]);}finally{if(epoch===state.epoch){$('checkSelected').disabled=!state.selected;$('checkAll').disabled=false;}}
}

$('sourceForm').addEventListener('submit',event=>{event.preventDefault();loadCatalog();});
for(const [id,name] of [['jsonSource','library.json'],['m3uSource','library.m3u']])$(id).addEventListener('click',()=>{$('sourceUrl').value=`https://kiro.papaclaoud.top/${name}`;loadCatalog();});
$('proxyMode').addEventListener('change',loadCatalog);$('search').addEventListener('input',renderTracks);$('albumFilter').addEventListener('change',renderTracks);
$('filters').addEventListener('click',event=>{const button=event.target.closest('[data-filter]');if(!button)return;state.filter=button.dataset.filter;for(const item of $('filters').children)item.classList.toggle('active',item===button);renderTracks();});
$('trackList').addEventListener('click',event=>{const button=event.target.closest('[data-key]');if(button)selectTrack(state.catalog.tracks.find(t=>t.key===button.dataset.key));});
$('corsButton').addEventListener('click',testCors);$('checkSelected').addEventListener('click',()=>runChecks(false));$('checkAll').addEventListener('click',()=>runChecks(true));
$('playButton').addEventListener('click',async()=>{const audio=$('audio');if(!audio.paused){audio.pause();return;}$('audioState').textContent='正在连接音频，等待缓冲…';$('playButton').textContent='取消播放';try{await audio.play();}catch(error){if(error.name==='AbortError')return;$('audioState').textContent=`无法播放：${errorMessage(error)}`;$('playButton').textContent='播放试听';}});
for(const [event,message] of [['playing','正在试听 · 浏览器解码正常'],['waiting','正在缓冲…'],['ended','播放结束'],['pause','已暂停']])$('audio').addEventListener(event,()=>{$('audioState').textContent=message;$('playButton').textContent=$('audio').paused?'播放试听':'暂停试听';});
$('audio').addEventListener('error',()=>{$('audioState').textContent=`播放失败（浏览器错误 ${$('audio').error?.code||'未知'}），请运行当前歌曲检查。`;});
$('audio').addEventListener('loadedmetadata',()=>{const duration=$('audio').duration;if(Number.isFinite(duration))$('audioState').textContent=`音频实际时长 ${timeLabel(duration)}${state.selected?.duration?` · 曲库提供 ${timeLabel(state.selected.duration)}`:''}`;});
$('audio').addEventListener('timeupdate',()=>{if(!currentLyrics?.synced)return;const time=$('audio').currentTime;let current=-1;for(let i=0;i<currentLyrics.lines.length&&currentLyrics.lines[i].time<=time;++i)current=i;if(current===activeLine)return;const children=$('lyrics').children;if(activeLine>=0)children[activeLine]?.classList.remove('current');activeLine=current;if(current>=0){const line=children[current];line?.classList.add('current');if(line&&Date.now()>manualScrollUntil)$('lyrics').scrollTo({top:Math.max(0,line.offsetTop-$('lyrics').offsetTop-$('lyrics').clientHeight/2),behavior:matchMedia('(prefers-reduced-motion: reduce)').matches?'instant':'smooth'});}});
for(const event of ['wheel','touchstart'])$('lyrics').addEventListener(event,()=>{manualScrollUntil=Date.now()+5000;},{passive:true});
$('exportReport').addEventListener('click',()=>{const report={checkedAt:new Date().toISOString(),source:state.url,mode:$('proxyMode').checked?'local-proxy':'browser-direct',coverage:summarize(state.catalog.tracks),browserDirect:state.cors,responseHeaders:state.headers,resources:state.checks,selected:state.selected?{title:state.selected.title,audio:state.selected.audio,providedDuration:state.selected.duration,observedDuration:Number.isFinite($('audio').duration)?$('audio').duration:null,audioError:$('audio').error?.code||null}:null};const url=URL.createObjectURL(new Blob([JSON.stringify(report,null,2)],{type:'application/json'}));const link=node('a');link.href=url;link.download=`kiro-check-${new Date().toISOString().slice(0,10)}.json`;link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});
loadCatalog();
