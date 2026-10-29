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
        self.music.set_overrides({'version':1,'tracks':{'sd:test':{'lyrics':href}}})
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

class MusicReviewFixTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.base=Path(self.tmp.name)
        self.root=self.base/'songs'; self.root.mkdir()
    def tearDown(self): self.tmp.cleanup()
    def test_m3u_override_matches_native_fnv1a64_resolved_audio_url(self):
        music=MusicLibrary(self.base/'config');(self.root/'x.mp3').write_bytes(b'audio')
        track=music.configure(str(self.root),music.id)['tracks'][0]
        base='http://127.0.0.1:1234'
        music.set_sources({'version':1,'sources':[{'id':'m3u','name':'M3U','url':base+'/music-list.m3u8','enabled':True},{'id':'json','name':'JSON','url':base+'/music-library.json','enabled':True}]})
        music.set_overrides({'version':1,'tracks':{track['id']:{'album':'Edited','lyrics':'https://example.org/x.lrc'}}})
        audio=music.m3u(base).decode().splitlines()[-1]
        # Native media.cpp hash(): FNV-1a64 over resolved URL UTF-8 bytes, lowercase padded hex.
        digest=14695981039346656037
        for byte in audio.encode(): digest=((digest^byte)*1099511628211)&0xffffffffffffffff
        class Peer:
            def __init__(self): self.payloads={}
            def list(self,path): return [{'name':'config'},{'name':'papa-walkman'}]
            def upload(self,source,target,overwrite=False): self.payloads[target]=Path(source).read_bytes();return {}
            def download(self,path,writer):writer(self.payloads[path])
        peer=Peer();music.sync(peer,base)
        tracks=json.loads(peer.payloads['/config/papa-walkman/overrides.json'])['tracks']
        self.assertEqual(tracks['cloud:m3u:'+format(digest,'016x')]['album'],'Edited')
        self.assertEqual(tracks['cloud:json:'+track['id']]['album'],'Edited')
        self.assertNotIn('cloud:m3u:'+track['id'],tracks)
    def test_every_truncated_id3_header_and_persisted_root_are_safe(self):
        header=b'ID3\x03\0\0\0\0\0\0'
        for length in range(10):(self.root/('short%d.mp3'%length)).write_bytes(header[:length])
        music=MusicLibrary(self.base/'config')
        state=music.configure(str(self.root),music.id)
        self.assertEqual(len(state['tracks']),10)
        restored=MusicLibrary(self.base/'config')
        self.assertEqual(len(restored.snapshot()['tracks']),10)
    def test_cache_reclaims_replacements_but_keeps_overrides_and_source_files(self):
        music=MusicLibrary(self.base/'config',cache_budget=30)
        (self.root/'x.mp3').write_bytes(b'original');track=music.configure(str(self.root),music.id)['tracks'][0]
        original=music.upload_asset('lyrics',b'old lyrics',music.id)
        music.set_overrides({'version':1,'tracks':{track['id']:{'lyrics':original}}})
        for i in range(12):
            href=music.upload_asset('lyrics',('new lyrics '+str(i)).encode(),music.id)
            music.set_overrides({'version':1,'tracks':{track['id']:{'lyrics':href}}})
            self.assertLessEqual(sum(p.stat().st_size for p in (music.data/'assets').iterdir()),30)
        with self.assertRaises(MusicError):music.file(original.rsplit('/',1)[1])
        self.assertEqual((self.root/'x.mp3').read_bytes(),b'original')
        restored=MusicLibrary(self.base/'config',cache_budget=30)
        self.assertEqual(restored.file(href.rsplit('/',1)[1]).read_bytes(),b'new lyrics 11')
        self.assertLessEqual(len(restored.files),2)
    def test_active_asset_is_not_deleted_and_capacity_failure_is_explicit(self):
        music=MusicLibrary(self.base/'config',cache_budget=13)
        href=music.upload_asset('lyrics',b'active lyrics',music.id)
        path,reader=music.open_file(href.rsplit('/',1)[1])
        try:
            with self.assertRaisesRegex(MusicError,'缓存'):
                music.upload_asset('lyrics',b'new lyrics',music.id)
            self.assertTrue(path.exists());self.assertEqual(reader.read(),b'active lyrics')
        finally:reader.close()
        new=music.upload_asset('lyrics',b'new lyrics',music.id)
        self.assertEqual(music.file(new.rsplit('/',1)[1]).read_bytes(),b'new lyrics')
    def test_root_change_reclaims_embedded_assets_and_orphan_capabilities(self):
        music=MusicLibrary(self.base/'config',cache_budget=40)
        def song(root,text):
            data=b'\x03eng\0'+text
            frame=b'USLT'+len(data).to_bytes(4,'big')+b'\0\0'+data
            (root/'x.mp3').write_bytes(b'ID3\x03\0\0'+bytes([0,0,0,len(frame)])+frame)
        song(self.root,b'old words'); old=music.configure(str(self.root),music.id)['tracks'][0]['lyrics']
        other=self.base/'other';other.mkdir();song(other,b'new words')
        music.configure(str(other),music.id)
        with self.assertRaises(MusicError):music.file(old.rsplit('/',1)[1])
        self.assertEqual(len(list((music.data/'assets').iterdir())),1)
    def test_budget_failure_preserves_referenced_override_and_previous_editor_upload(self):
        music=MusicLibrary(self.base/'config',cache_budget=24)
        pinned=music.upload_asset('lyrics',b'pinned',music.id)
        music.set_overrides({'version':1,'tracks':{'sd:pinned':{'lyrics':pinned}}})
        pending=music.upload_asset('lyrics',b'pending',music.id)
        with self.assertRaisesRegex(MusicError,'缓存'):
            music.upload_asset('lyrics',b'x'*25,music.id)
        for href,content in ((pinned,b'pinned'),(pending,b'pending')):
            self.assertEqual(music.file(href.rsplit('/',1)[1]).read_bytes(),content)
    def test_unassociated_uploads_are_reclaimed_and_not_exposed_after_restart(self):
        music=MusicLibrary(self.base/'config',cache_budget=30)
        orphan=music.upload_asset('lyrics',b'orphan',music.id)
        restored=MusicLibrary(self.base/'config',cache_budget=30)
        self.assertEqual(list((music.data/'assets').iterdir()),[])
        with self.assertRaises(MusicError):restored.file(orphan.rsplit('/',1)[1])
    def test_active_reader_survives_root_switch_then_reclaims_cache_on_close(self):
        music=MusicLibrary(self.base/'config',cache_budget=30)
        href=music.upload_asset('lyrics',b'active',music.id)
        path,reader=music.open_file(href.rsplit('/',1)[1])
        try:
            music.configure(str(self.root),music.id)
            self.assertTrue(path.exists()); self.assertEqual(reader.read(),b'active')
            with self.assertRaises(MusicError):music.file(href.rsplit('/',1)[1])
        finally:reader.close()
        self.assertFalse(path.exists())

class MusicPendingRollbackTests(unittest.TestCase):
    def test_windows_locked_orphan_cannot_destroy_rejected_uploads_previous_pending_file(self):
        import hashlib
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temporary:
            music=MusicLibrary(Path(temporary)/'config',cache_budget=24)
            pinned=music.upload_asset('lyrics',b'pinned',music.id)
            music.set_overrides({'version':1,'tracks':{'sd:pinned':{'lyrics':pinned}}})
            old=music.upload_asset('lyrics',b'pending',music.id)
            old_path=music.file(old.rsplit('/',1)[1])
            orphan_data=b'0123456789'
            orphan=music.data/'assets'/(hashlib.sha256(orphan_data).hexdigest()+'.lrc')
            orphan.write_bytes(orphan_data)
            original_unlink=Path.unlink
            def locked_unlink(path,*args,**kwargs):
                if path==orphan:raise PermissionError('synthetic Windows sharing violation')
                return original_unlink(path,*args,**kwargs)
            with patch.object(Path,'unlink',locked_unlink):
                with self.assertRaisesRegex(MusicError,'缓存'):
                    music.upload_asset('lyrics',b'replacement!',music.id)
                self.assertEqual(music.pending_assets,{'lyrics':old})
                self.assertTrue(old_path.exists())
                self.assertEqual(music.file(old.rsplit('/',1)[1]).read_bytes(),b'pending')
                self.assertEqual(music.file(pinned.rsplit('/',1)[1]).read_bytes(),b'pinned')
                self.assertEqual(orphan.read_bytes(),orphan_data)
            # Once orphan reclamation is possible, the same replacement fits:
            # pinned6 + replacement12 = 18, and the superseded pending7 is reclaimed.
            new=music.upload_asset('lyrics',b'replacement!',music.id)
            self.assertEqual(music.file(new.rsplit('/',1)[1]).read_bytes(),b'replacement!')
            self.assertFalse(old_path.exists())
            self.assertFalse(orphan.exists())
            self.assertLessEqual(sum(p.stat().st_size for p in (music.data/'assets').iterdir()),24)
