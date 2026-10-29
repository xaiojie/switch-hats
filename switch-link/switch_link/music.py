"""Explicit, audio-only Papa Walkman library and recoverable configuration."""
import hashlib
import json
import itertools
import os
from pathlib import Path
import re
import threading
import urllib.parse

MAX_TAG = 8 * 1024 * 1024
MAX_LYRICS = 512 * 1024
ASSETS = {'.jpg', '.jpeg', '.png', '.lrc', '.txt'}

class MusicError(ValueError): pass

def _save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    data = json.dumps(value, ensure_ascii=False, indent=2).encode('utf-8')
    temp = path.with_suffix('.tmp')
    with temp.open('wb') as out:
        out.write(data); out.flush(); os.fsync(out.fileno())
    if path.exists(): path.with_suffix('.bak').write_bytes(path.read_bytes())
    temp.replace(path)

def _load(path, default):
    for candidate in (path, path.with_suffix('.bak')):
        try:
            if candidate.stat().st_size > 4 * 1024 * 1024: continue
            return json.loads(candidate.read_text(encoding='utf-8'))
        except (OSError, ValueError): pass
    return default

def _decode(data, encoding=0):
    codecs = {0: ('utf-8', 'gbk', 'latin1'), 1: ('utf-16',), 2: ('utf-16-be',), 3: ('utf-8',)}
    for codec in codecs.get(encoding, ('utf-8',)):
        try: return data.decode(codec).strip('\0 \r\n')[:MAX_LYRICS]
        except UnicodeError: pass
    return data.decode('utf-8', 'replace').strip('\0 \r\n')[:MAX_LYRICS]

def _terminated(data, enc):
    step = 2 if enc in (1,2) else 1
    for pos in range(0,len(data)-step+1,step):
        if data[pos:pos+step] == b'\0'*step: return data[:pos], data[pos+step:]
    return data, b''

def metadata(path):
    result = {'title':path.stem, 'artist':'', 'album':'', 'duration':None}
    with path.open('rb') as stream:
        header=stream.read(10)
        if header[:3] == b'ID3' and header[3] in (3,4) and all(b<128 for b in header[6:10]):
            size=sum(b << (7*(3-i)) for i,b in enumerate(header[6:10]))
            if size <= MAX_TAG:
                data=stream.read(size); pos=0
                if header[5]&0x40 and len(data)>=4:
                    ext=int.from_bytes(data[:4],'big') if header[3]==3 else sum(b << (7*(3-i)) for i,b in enumerate(data[:4]))
                    pos=ext+4 if header[3]==3 else ext
                while pos+10<=len(data):
                    key=data[pos:pos+4].decode('ascii','replace'); raw=data[pos+4:pos+8]
                    length=int.from_bytes(raw,'big') if header[3]==3 else sum(b << (7*(3-i)) for i,b in enumerate(raw))
                    flags=data[pos+8:pos+10]; pos+=10
                    if not re.fullmatch('[A-Z0-9]{4}',key) or length<=0 or pos+length>len(data): break
                    frame=data[pos:pos+length]; pos+=length
                    if flags[1] & (0x0c if header[3]==4 else 0xc0): continue
                    if header[5]&0x80 or (header[3]==4 and flags[1]&2): frame=frame.replace(b'\xff\0',b'\xff')
                    if key in ('TIT2','TPE1','TALB') and frame:
                        result[{'TIT2':'title','TPE1':'artist','TALB':'album'}[key]]=_decode(frame[1:],frame[0])[:1024]
                    elif key=='APIC' and frame:
                        enc=frame[0]; mime,sep,rest=frame[1:].partition(b'\0')
                        if sep and rest:
                            _,image=_terminated(rest[1:],enc)
                            if image.startswith(b'\x89PNG\r\n\x1a\n'): result['art']=(image,'.png')
                            elif image.startswith(b'\xff\xd8\xff'): result['art']=(image,'.jpg')
                    elif key=='USLT' and len(frame)>4:
                        _,lyrics=_terminated(frame[4:],frame[0])
                        if len(lyrics)<=MAX_LYRICS: result['text']=_decode(lyrics,frame[0])
        stream.seek(0,2)
        if stream.tell()>=128:
            stream.seek(-128,2); tag=stream.read(128)
            if tag[:3]==b'TAG':
                for key,start in (('title',3),('artist',33),('album',63)):
                    if not result[key] or (key=='title' and result[key]==path.stem): result[key]=_decode(tag[start:start+30]) or result[key]
    return result

def _url(value):
    if not isinstance(value,str) or len(value)>4096 or any(c.isspace() or ord(c)<32 for c in value): raise MusicError('网址不合法')
    try:
        parsed=urllib.parse.urlsplit(value)
        parsed.port
    except ValueError: raise MusicError('网址不合法')
    if parsed.scheme not in ('http','https') or not parsed.hostname or parsed.username or parsed.password: raise MusicError('请输入 HTTP 或 HTTPS 网址，不含账号密码')
    return value

class MusicLibrary:
    def __init__(self, data):
        self.data=Path(data); self.data.mkdir(parents=True,exist_ok=True)
        self.lock=threading.RLock(); self.root=None; self.id='unconfigured'; self.tracks=[]; self.files={}
        self.sources=_load(self.data/'sources.json',{'version':1,'sources':[]})
        self.overrides=_load(self.data/'overrides.json',{'version':1,'tracks':{}})
        setting=_load(self.data/'library.json',{})
        if setting.get('root'):
            root=Path(setting['root'])
            if root.is_dir(): self._scan(root.resolve())
        self._restore_assets()
    def assert_id(self, expected):
        if expected!=self.id: raise MusicError('音乐库已变化，请刷新后重新操作')
    def configure(self, root, expected):
        with self.lock:
            self.assert_id(expected)
            if not isinstance(root,str) or not Path(root).is_absolute() or not Path(root).is_dir(): raise MusicError('请选择存在的电脑音乐文件夹绝对路径')
            previous=(self.root,self.id,self.tracks,self.files)
            try:
                self._scan(Path(root).resolve()); _save(self.data/'library.json',{'root':str(self.root)})
            except Exception:
                self.root,self.id,self.tracks,self.files=previous
                raise
            return self.snapshot()
    def _safe(self,path,root):
        resolved=path.resolve()
        try: resolved.relative_to(root)
        except ValueError: raise MusicError('文件超出音乐库范围')
        if not resolved.is_file(): raise MusicError('文件已不存在')
        return resolved
    def _register(self,path,root):
        path=self._safe(path,root)
        namespace = 'assets' if root == (self.data/'assets').resolve() else self.id
        identity=hashlib.sha256((namespace+'\0'+str(path)).encode()).hexdigest()[:32]
        self.files[identity]=(path,root)
        return '/music-file/'+identity
    def _cache(self,data,suffix):
        directory=self.data/'assets'; directory.mkdir(exist_ok=True)
        path=directory/(hashlib.sha256(data).hexdigest()+suffix)
        if not path.exists(): path.write_bytes(data)
        return self._register(path,directory.resolve())
    def _scan(self,root):
        self.root=root; self.id=hashlib.sha256(str(root).casefold().encode()).hexdigest()[:24]
        self.tracks=[]; self.files={}
        for path in sorted(itertools.islice(root.rglob('*.mp3'),10001)):
            if len(self.tracks)>=10000: raise MusicError('音乐库超过 10000 首，请选择较小文件夹')
            try:
                safe=self._safe(path,root); info=metadata(safe)
                audio=self._register(safe,root); identity=audio.rsplit('/',1)[1]
                art=info.pop('art',None); lyrics=info.pop('text',None)
                info.update(id=identity,audio=audio,cover='',lyrics='')
                if art: info['cover']=self._cache(*art)
                if lyrics: info['lyrics']=self._cache(lyrics.encode('utf-8'),'.lrc')
                for key,extensions in (('cover',('.jpg','.jpeg','.png')),('lyrics',('.lrc',))):
                    for extension in extensions:
                        asset=path.with_suffix(extension)
                        if asset.is_file() and asset.stat().st_size<=MAX_TAG:
                            info[key]=self._register(asset,root); break
                self.tracks.append(info)
            except (OSError,MusicError): continue
        # Restore uploaded asset capabilities after restart/rescan.
        self._restore_assets()
    def _restore_assets(self):
        directory=self.data/'assets'
        if directory.is_dir():
            for asset in directory.iterdir():
                if asset.suffix in ASSETS: self._register(asset,directory.resolve())
    def file(self,identity):
        with self.lock:
            if not re.fullmatch('[a-f0-9]{32}',identity or '') or identity not in self.files: raise MusicError('音乐文件不存在')
            path,root=self.files[identity]
            if path.suffix.lower() not in ASSETS|{'.mp3'}: raise MusicError('文件类型不可共享')
            return self._safe(path,root)
    def catalog(self,base=''):
        with self.lock:
            tracks=[]
            for original in self.tracks:
                track=dict(original); track.update(self.overrides['tracks'].get(track['id'],{}))
                for key in ('audio','cover','lyrics'):
                    if track[key].startswith('/music-file/'): track[key]=base+track[key]
                tracks.append(track)
            return {'version':1,'name':self.root.name if self.root else '未选择音乐库','tracks':tracks}
    def m3u(self,base=''):
        lines=['#EXTM3U']
        for track in self.catalog(base)['tracks']:
            text=(track['artist']+' - '+track['title']).replace('\r',' ').replace('\n',' ')
            lines.extend(['#EXTINF:%s,%s'%(track['duration'] if track['duration'] is not None else -1,text),track['audio']])
        return ('\n'.join(lines)+'\n').encode('utf-8')
    def snapshot(self):
        with self.lock:
            return {'root':str(self.root) if self.root else '', 'library_id':self.id,'tracks':self.catalog()['tracks'],'sources':self.sources,'overrides':self.overrides}
    def set_sources(self,value):
        if not isinstance(value,dict) or value.get('version')!=1 or not isinstance(value.get('sources'),list) or len(value['sources'])>100: raise MusicError('来源配置格式不合法')
        seen=set(); sources=[]
        for entry in value['sources']:
            if not isinstance(entry,dict) or not isinstance(entry.get('id'),str) or not re.fullmatch('[A-Za-z0-9_-]{1,100}',entry['id']) or entry['id'] in seen or not isinstance(entry.get('name'),str) or not 0<len(entry['name'])<=200 or type(entry.get('enabled')) is not bool: raise MusicError('来源名称、ID 或启用状态不合法')
            seen.add(entry['id']); sources.append(dict(id=entry['id'],name=entry['name'],url=_url(entry.get('url')),enabled=entry['enabled']))
        with self.lock:
            self.sources={'version':1,'sources':sources}; _save(self.data/'sources.json',self.sources)
        return self.sources
    def set_overrides(self,value):
        if not isinstance(value,dict) or value.get('version')!=1 or not isinstance(value.get('tracks'),dict) or len(value['tracks'])>10000: raise MusicError('歌曲配置格式不合法')
        tracks={}
        for identity,entry in value['tracks'].items():
            if not isinstance(identity,str) or not 0<len(identity)<=512 or not isinstance(entry,dict) or set(entry)-{'title','artist','album','cover','lyrics'}: raise MusicError('歌曲覆盖字段不合法')
            for key,text in entry.items():
                if not isinstance(text,str) or len(text)>4096 or any(ord(c)<32 for c in text): raise MusicError('歌曲信息过长或不合法')
                if key in ('cover','lyrics') and text:
                    if text.startswith('/music-file/'):
                        if not re.fullmatch('/music-file/[a-f0-9]{32}',text): raise MusicError('音乐资源地址不合法')
                        self.file(text.rsplit('/',1)[1])
                    elif text.startswith('sdmc:/'): 
                        if '..' in text.split('/'): raise MusicError('SD 路径不合法')
                    else: _url(text)
            tracks[identity]=dict(entry)
        with self.lock:
            self.overrides={'version':1,'tracks':tracks}; _save(self.data/'overrides.json',self.overrides)
        return self.overrides
    def upload_asset(self,kind,data,expected):
        with self.lock:
            self.assert_id(expected)
            if kind=='cover' and 0<len(data)<=MAX_TAG:
                if data.startswith(b'\x89PNG\r\n\x1a\n'): suffix='.png'
                elif data.startswith(b'\xff\xd8\xff'): suffix='.jpg'
                else: raise MusicError('封面需要 PNG 或 JPEG')
            elif kind=='lyrics' and 0<len(data)<=MAX_LYRICS:
                try: data=data.decode('utf-8-sig').encode('utf-8')
                except UnicodeError: raise MusicError('歌词需要 UTF-8 文本')
                suffix='.lrc'
            else: raise MusicError('资源类型或大小不合法')
            return self._cache(data,suffix)
    def sync(self,peer,base):
        with self.lock:
            overrides=json.loads(json.dumps(self.overrides))
            local_ids={track['id'] for track in self.tracks}
            for source in self.sources['sources']:
                if source['url'] in (base+'/music-library.json',base+'/music-list.m3u8'):
                    for identity,fields in self.overrides['tracks'].items():
                        if identity in local_ids:
                            overrides['tracks']['cloud:'+source['id']+':'+identity]=dict(fields)
            for entry in overrides['tracks'].values():
                for key in ('cover','lyrics'):
                    value=entry.get(key,'')
                    if value.startswith('/music-file/'):
                        if not base: raise MusicError('音乐 HTTP 共享服务尚未启动')
                        entry[key]=base+value
            payloads={'sources.json':self.sources,'overrides.json':overrides}
            for parent,name in (('/','config'),('/config','papa-walkman')):
                if not any(item['name']==name for item in peer.list(parent)): peer.mkdir(parent,name)
            results=[]
            for name,payload in payloads.items():
                path=self.data/('sync-'+name); _save(path,payload)
                target='/config/papa-walkman/'+name
                result=peer.upload(path,target,overwrite=True)
                received=bytearray()
                def collect(chunk):
                    if len(received)+len(chunk)>4*1024*1024: raise MusicError('远端配置超过读取上限')
                    received.extend(chunk)
                peer.download(target,collect)
                if bytes(received)!=path.read_bytes(): raise MusicError('主机配置回读校验失败：'+name)
                results.append(dict(file=target,verified=True,backup=result.get('backup')))
            return results
