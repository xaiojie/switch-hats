import http.client
import json
from pathlib import Path
import tempfile
import threading
import unittest
from switch_link.music import MusicLibrary, MusicError
from switch_link.library import Library, HttpShare

class MusicTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.root = self.base / 'songs'; self.root.mkdir()
        self.music = MusicLibrary(self.base / 'config')
    def tearDown(self): self.tmp.cleanup()
    def configure(self): return self.music.configure(str(self.root), self.music.id)
    def test_explicit_root_metadata_and_persistence(self):
        self.assertFalse(self.music.snapshot()['root'])
        title = '晴天'.encode('gbk').ljust(30,b'\0')
        (self.root/'歌曲.mp3').write_bytes(b'audio'+b'TAG'+title+b'Jay'.ljust(30,b'\0')+b'Album'.ljust(30,b'\0')+b'2003'+b'\0'*31)
        state = self.configure(); track = state['tracks'][0]
        self.assertEqual(track['title'],'晴天'); self.assertEqual(track['artist'],'Jay')
        self.assertEqual(MusicLibrary(self.base/'config').snapshot()['tracks'][0]['id'],track['id'])
        with self.assertRaises(MusicError): self.music.configure(str(self.root), 'stale')
    def test_catalog_and_scoped_range(self):
        (self.root/'one.mp3').write_bytes(b'0123456789')
        (self.root/'secret.txt').write_text('secret')
        self.configure()
        share = HttpShare(Library(self.base/'games'),host='127.0.0.1',port=0)
        share.music = self.music; share.start(reuse=False)
        try:
            def request(path, method='GET', headers=None):
                c=http.client.HTTPConnection('127.0.0.1',share.port)
                c.request(method,path,headers=headers or {}); r=c.getresponse(); result=(r.status,dict(r.getheaders()),r.read()); c.close(); return result
            code,_,body=request('/music-library.json'); catalog=json.loads(body)
            self.assertEqual(set(catalog),{'version','name','tracks'})
            track=catalog['tracks'][0]
            self.assertEqual(set(track),{'id','title','artist','album','duration','audio','cover','lyrics'})
            code,h,body=request(track['audio'],headers={'Range':'bytes=2-5'})
            self.assertEqual((code,body),(206,b'2345'))
            self.assertEqual(h['Content-Range'],'bytes 2-5/10')
            self.assertEqual(request(track['audio'],'HEAD')[2],b'')
            self.assertEqual(request('/music-file/../secret.txt')[0],404)
            self.assertIn(track['audio'].encode(),request('/music-list.m3u8')[2])
        finally: share.close()
    def test_sources_and_overrides_validate_and_persist(self):
        source={'version':1,'sources':[{'id':'home','name':'家','url':'https://example.org/list.json','enabled':True}]}
        self.music.set_sources(source)
        self.assertEqual(MusicLibrary(self.base/'config').sources,source)
        for url in ('file:///secret','http://user:password@example.com/x','javascript:alert(1)'):
            with self.assertRaises(MusicError): self.music.set_sources({'version':1,'sources':[dict(source['sources'][0],url=url)]})
        with self.assertRaises(MusicError): self.music.set_overrides({'version':1,'tracks':{'x':{'cover':'../../secret'}}})
    def test_utf16_id3_art_lyrics_and_escape(self):
        def frame(key,data): return key.encode()+len(data).to_bytes(4,'big')+b'\0\0'+data
        png=b'\x89PNG\r\n\x1a\n'+b'fake'
        frames=frame('TIT2',b'\x01'+'中文'.encode('utf-16'))+frame('APIC',b'\x03image/png\0\x03\0'+png)+frame('USLT',b'\x03eng\0'+'[00:01.00]歌词'.encode())
        n=len(frames); size=bytes([(n>>21)&127,(n>>14)&127,(n>>7)&127,n&127])
        (self.root/'id3.mp3').write_bytes(b'ID3\x03\0\0'+size+frames)
        track=self.configure()['tracks'][0]
        self.assertEqual(track['title'],'中文')
        self.assertTrue(track['cover']); self.assertTrue(track['lyrics'])
        self.assertEqual(self.music.file(track['cover'].split('/')[-1]).read_bytes(),png)
        with self.assertRaises(MusicError): self.music.file('../secret')

if __name__=='__main__': unittest.main()


class MusicApiTests(unittest.TestCase):
    def setUp(self):
        from switch_link.http import Application, make_server
        self.tmp=tempfile.TemporaryDirectory(); self.base=Path(self.tmp.name)
        self.app=Application(self.base,discover=False)
        self.server=make_server(self.app,'127.0.0.1',0)
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True); self.thread.start()
    def tearDown(self):
        self.server.shutdown(); self.server.server_close(); self.thread.join(); self.app.close(); self.tmp.cleanup()
    def request(self,method,path,data=None):
        c=http.client.HTTPConnection('127.0.0.1',self.server.server_port)
        c.request(method,path,json.dumps(data).encode() if data else None,{'Content-Type':'application/json'})
        r=c.getresponse(); result=(r.status,json.loads(r.read())); c.close(); return result
    def test_api_binding_sources_and_active_sync(self):
        code,state=self.request('GET','/api/music'); self.assertEqual(code,200); self.assertEqual(state['root'],'')
        root=self.base/'songs'; root.mkdir(); (root/'x.mp3').write_bytes(b'123')
        code,state=self.request('PUT','/api/music',{'root':str(root),'expected_library':state['library_id']})
        self.assertEqual(code,200); self.assertEqual(len(state['tracks']),1)
        self.assertEqual(self.request('PUT','/api/music/sources',{'version':1,'sources':[],'expected_library':'old'})[0],409)
        self.assertEqual(self.request('POST','/api/music/sync',{'expected_device':'old','expected_library':state['library_id']})[0],409)
        self.app.selected_id='current'; self.app.receiving_count=1
        self.assertEqual(self.request('POST','/api/music/sync',{'expected_device':'current','expected_library':state['library_id']})[0],409)
    def test_sync_backup_and_exact_readback(self):
        class Peer:
            def __init__(self): self.payloads={}; self.dirs={'/':['config'],'/config':['papa-walkman']}
            def list(self,path): return [{'name':n} for n in self.dirs[path]]
            def upload(self,source,target,overwrite=False):
                assert overwrite; self.payloads[target]=Path(source).read_bytes(); return {'backup':'backup.json'}
            def download(self,path,writer): writer(self.payloads[path])
        peer=Peer(); result=self.app.music.sync(peer,'')
        self.assertEqual(set(peer.payloads),{'/config/papa-walkman/sources.json','/config/papa-walkman/overrides.json'})
        self.assertTrue(all(x['verified'] and x['backup'] for x in result))
        peer.download=lambda path,writer:writer(b'broken')
        with self.assertRaises(MusicError): self.app.music.sync(peer,'')

class MusicSafetyTests(MusicTests):
    def test_asset_identity_survives_restart_without_selected_root(self):
        href=self.music.upload_asset('lyrics',b'[00:01]hello',self.music.id)
        restored=MusicLibrary(self.base/'config')
        self.assertEqual(restored.file(href.rsplit('/',1)[1]).read_bytes(),b'[00:01]hello')
    def test_sync_expands_desktop_overrides_to_saved_source_identity(self):
        (self.root/'x.mp3').write_bytes(b'audio'); track=self.configure()['tracks'][0]
        self.music.set_sources({'version':1,'sources':[{'id':'home','name':'Home','url':'http://127.0.0.1:1234/music-library.json','enabled':True}]})
        self.music.set_overrides({'version':1,'tracks':{track['id']:{'title':'Edited'}}})
        class Peer:
            def __init__(self): self.payloads={}
            def list(self,path): return [{'name':'config'},{'name':'papa-walkman'}]
            def upload(self,source,target,overwrite=False): self.payloads[target]=Path(source).read_bytes(); return {}
            def download(self,path,writer): writer(self.payloads[path])
        peer=Peer(); self.music.sync(peer,'http://127.0.0.1:1234')
        entries=json.loads(peer.payloads['/config/papa-walkman/overrides.json'])['tracks']
        self.assertEqual(entries['cloud:home:'+track['id']]['title'],'Edited')
    def test_oversized_broken_id3_and_hidden_files_are_bounded(self):
        (self.root/'broken.mp3').write_bytes(b'ID3\x03\0\0\x7f\x7f\x7f\x7f'+b'small')
        (self.root/'not-audio.nsp').write_bytes(b'private')
        state=self.configure();self.assertEqual(len(state['tracks']),1)
        self.assertEqual(state['tracks'][0]['title'],'broken')
        with self.assertRaises(MusicError): self.music.upload_asset('lyrics',b'x'*(512*1024+1),self.music.id)
        with self.assertRaises(MusicError): self.music.set_overrides({'version':1,'tracks':{'id':{'cover':'/music-file/extra/'+state['tracks'][0]['id']}}})
    def test_rebinding_revokes_previous_audio_capability(self):
        (self.root/'x.mp3').write_bytes(b'one'); old=self.configure(); identity=old['tracks'][0]['id']
        second=self.base/'other'; second.mkdir();(second/'x.mp3').write_bytes(b'two')
        current=self.music.configure(str(second),old['library_id'])
        self.assertNotEqual(current['library_id'],old['library_id'])
        with self.assertRaises(MusicError): self.music.file(identity)
        with self.assertRaises(MusicError): self.music.upload_asset('lyrics',b'hello',old['library_id'])
