import http.server, pathlib, sys, time, json, base64, urllib.parse
metadata_requests=0
lyrics_requests=0
def frame(k,b): return k.encode()+len(b).to_bytes(4,"big")+bytes(2)+b
def tag(body): return b"ID3\x03\0\0"+bytes((len(body)>>21&127,len(body)>>14&127,len(body)>>7&127,len(body)&127))+body
cover=base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Wl2n0sAAAAASUVORK5CYII=")
metadata_body=frame("TIT2",b"\x03Test song")+frame("TPE1",b"\x03Test artist")+frame("TALB",b"\x03Test album")+frame("TLEN",b"\x03123000")+frame("APIC",b"\0image/png\0\0\0"+cover)
metadata=tag(metadata_body)

resume_requests=0
tone=pathlib.Path(sys.argv[1]).read_bytes()
long=tone*32
pathlib.Path('build-host/stream/long.mp3').write_bytes(long)
tagged=bytearray(long)
tagged[36:49]=b'Xing'+(1).to_bytes(4,'big')+(4).to_bytes(4,'big')+b'\0'
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args): pass
 def do_GET(self):
  global resume_requests,metadata_requests,lyrics_requests
  route=urllib.parse.urlsplit(self.path).path
  if route in ("/metadata-count","/lyrics-count"):
   data=str(metadata_requests if route=="/metadata-count" else lyrics_requests).encode();self.send_response(200);self.send_header("Content-Length",str(len(data)));self.end_headers();self.wfile.write(data);return
  if route.endswith("/api/get"):
   lyrics_requests+=1
   if route.startswith("/lyrics-slow/"):time.sleep(.5)
   query=urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
   if route.startswith("/lyrics-missing/"): self.send_error(404);return
   record={"trackName":"Wrong song" if route.startswith("/lyrics-wrong/") else "Test song","artistName":"Test artist","albumName":"Test album","duration":123,"instrumental":False,"syncedLyrics":"[00:01.00]Original fixture lyric","plainLyrics":"Original fixture lyric"}
   if query.get("track_name")!=["Test song"] or query.get("artist_name")!=["Test artist"]:self.send_error(400);return
   data=json.dumps(record).encode();self.send_response(200);self.send_header("Content-Length",str(len(data)));self.end_headers();self.wfile.write(data);return
  if self.path=='/resume-count':
   data=str(resume_requests).encode();self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data);return
  if self.path=='/resume-unknown':resume_requests+=1
  if self.path=='/missing': self.send_error(404); return
  if self.path=='/redirect': self.send_response(302);self.send_header('Location','/tone');self.end_headers();return
  offset=int(self.headers.get('Range','bytes=0-').split('=')[1].split('-')[0])
  if self.path=='/range416' and offset:self.send_error(416);return
  data=tone*(32 if self.path in ('/slow','/large') else 1)
  ranged='Range' in self.headers and self.path not in ('/ignored','/unknown','/resume-unknown','/resume-medium','/resume-oversize','/resume-frame-limit','/slow','/truncated')
  if self.path=='/fragment':data=long
  if self.path=='/resume-medium':data=long
  if self.path=='/resume-oversize':data=tone*1100
  if self.path=='/resume-frame-limit':data=(bytes.fromhex('fffb1000')+bytes(100))*270000
  if self.path in ('/late','/late-cancel'):data=bytes(tagged)
  if self.path=='/fallback' and offset:ranged=False
  if self.path in ("/metadata","/metadata-uslt"):
   metadata_requests+=1
   data=(metadata if self.path=="/metadata" else tag(metadata_body+frame("USLT",b"\x03eng\0[00:01.00]Embedded fixture lyric")))+tone
  full=len(data)
  end_text=self.headers.get("Range","bytes=0-").split("-",1)[1]
  end=min(full-1,int(end_text)) if end_text else full-1
  if ranged:data=data[offset:end+1]
  if self.path=='/range-chunk-short':self.protocol_version='HTTP/1.1'
  self.send_response(206 if ranged else 200)
  if self.path=='/range-chunk-short':self.send_header('Transfer-Encoding','chunked')
  self.send_header('Connection','close');self.close_connection=True
  self.send_header('ETag','changed' if self.path=='/changed' and offset else 'original')
  if ranged:self.send_header('Content-Range',f'bytes {1 if self.path=="/bad-range" else offset}-{end}/{full}')
  if self.path not in ('/unknown','/resume-unknown','/resume-medium','/resume-oversize','/resume-frame-limit','/range-short','/range-chunk-short'):self.send_header('Content-Length',str(len(data)+(1000 if self.path=='/truncated' else 0)))
  self.end_headers()
  try:
   if self.path=='/stalled':time.sleep(5)
   if self.path in ('/range-short','/range-chunk-short'):data=data[:-5000]
   if self.path=='/prefix-truncated':data=data[:4]
   if self.path in ('/late','/late-cancel'):data=data[:100000]
   step=137 if self.path=='/fragment' else 8192
   for n in range(0,len(data),step):
    block=data[n:n+step]
    if self.path=='/range-chunk-short':self.wfile.write(('%x\r\n'%len(block)).encode()+block+b'\r\n')
    else:self.wfile.write(block)
    self.wfile.flush()
    if self.path=='/fragment':time.sleep(.0001)
    if self.path=='/slow':time.sleep(.04)
   if self.path=='/range-chunk-short':self.wfile.write(b'0\r\n\r\n');self.wfile.flush()
   if self.path=='/late':time.sleep(.3)
   if self.path=='/late-cancel':time.sleep(5)
  except (ConnectionError,OSError):pass
http.server.ThreadingHTTPServer(('127.0.0.1',int(sys.argv[2])),Handler).serve_forever()
