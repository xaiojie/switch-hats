import http.server, pathlib, sys, time
tone=pathlib.Path(sys.argv[1]).read_bytes()
long=tone*32
pathlib.Path('build-host/stream/long.mp3').write_bytes(long)
tagged=bytearray(long)
tagged[36:49]=b'Xing'+(1).to_bytes(4,'big')+(4).to_bytes(4,'big')+b'\0'
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args): pass
 def do_GET(self):
  if self.path=='/missing': self.send_error(404); return
  if self.path=='/redirect': self.send_response(302);self.send_header('Location','/tone');self.end_headers();return
  offset=int(self.headers.get('Range','bytes=0-').split('=')[1].split('-')[0])
  if self.path=='/range416' and offset:self.send_error(416);return
  data=tone*(32 if self.path in ('/slow','/large') else 1)
  ranged='Range' in self.headers and self.path not in ('/ignored','/unknown','/slow','/truncated')
  if self.path=='/fragment':data=long
  if self.path in ('/late','/late-cancel'):data=bytes(tagged)
  if self.path=='/fallback' and offset:ranged=False
  full=len(data)
  if ranged:data=data[offset:]
  if self.path=='/range-chunk-short':self.protocol_version='HTTP/1.1'
  self.send_response(206 if ranged else 200)
  if self.path=='/range-chunk-short':self.send_header('Transfer-Encoding','chunked')
  self.send_header('Connection','close');self.close_connection=True
  self.send_header('ETag','changed' if self.path=='/changed' and offset else 'original')
  if ranged:self.send_header('Content-Range',f'bytes {1 if self.path=="/bad-range" else offset}-{full-1}/{full}')
  if self.path not in ('/unknown','/range-short','/range-chunk-short'):self.send_header('Content-Length',str(len(data)+(1000 if self.path=='/truncated' else 0)))
  self.end_headers()
  try:
   if self.path=='/stalled':time.sleep(5)
   if self.path in ('/range-short','/range-chunk-short'):data=data[:-5000]
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
