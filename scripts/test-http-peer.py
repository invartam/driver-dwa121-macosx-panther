#!/usr/bin/env python3
"""Local validation peer: serve/accept ONLY a deterministic synthetic payload."""
import argparse, hashlib, http.server
p=argparse.ArgumentParser();p.add_argument('--bind',required=True);p.add_argument('--port',type=int,default=8766);a=p.parse_args()
payload=bytes(range(256))*1024
class Peer(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path!='/payload.bin': self.send_error(404);return
        self.send_response(200);self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload)
    def do_PUT(self):
        try: n=int(self.headers.get('Content-Length','-1'))
        except ValueError: n=-1
        if self.path!='/payload.bin' or n!=len(payload):self.send_error(400);return
        self.connection.settimeout(60);body=self.rfile.read(n)
        if body!=payload:self.send_error(422);return
        result=(hashlib.md5(body).hexdigest()+'\n').encode()
        self.send_response(200);self.send_header('Content-Length',str(len(result)));self.end_headers();self.wfile.write(result)
print('Synthetic payload MD5: '+hashlib.md5(payload).hexdigest(),flush=True)
http.server.HTTPServer((a.bind,a.port),Peer).serve_forever()
