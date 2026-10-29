import http.server, pathlib, sys, time
tone=pathlib.Path(sys.argv[1]).read_bytes()
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args): pass
 def do_GET(self):
  if self.path=='/missing': self.send_error(404); return
  if self.path=='/redirect': self.send_response(302);self.send_header('Location','/tone');self.end_headers();return
  offset=int(self.headers.get('Range','bytes=0-').split('=')[1].split('-')[0])
  if self.path=='/range416' and offset:self.send_error(416);return
  data=tone*(32 if self.path in ('/slow','/large') else 1)
  ranged='Range' in self.headers and self.path not in ('/ignored','/unknown','/slow','/truncated')
  full=len(data)
  if ranged:data=data[offset:]
  self.send_response(206 if ranged else 200)
  self.send_header('ETag','changed' if self.path=='/changed' and offset else 'original')
  if ranged:self.send_header('Content-Range',f'bytes {1 if self.path=="/bad-range" else offset}-{full-1}/{full}')
  if self.path!='/unknown':self.send_header('Content-Length',str(len(data)+(1000 if self.path=='/truncated' else 0)))
  self.end_headers()
  try:
   if self.path=='/stalled':time.sleep(5)
   for n in range(0,len(data),8192):
    self.wfile.write(data[n:n+8192]);self.wfile.flush()
    if self.path=='/slow':time.sleep(.04)
  except (ConnectionError,OSError):pass
http.server.ThreadingHTTPServer(('127.0.0.1',int(sys.argv[2])),Handler).serve_forever()
