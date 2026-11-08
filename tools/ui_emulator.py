"""Run the real host app behind a loopback-only browser UI. Never accesses hardware."""
import argparse
import base64
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/ui-emulator'
sys.path.insert(0, str(ROOT / 'tests'))
from run_mod_target_tests import FLAGS

def firmware_revision():
    digest = hashlib.sha256()
    paths = [ROOT/'tools/ui_emulator.c', ROOT/'tools/ui_save_emulator.c', ROOT/'tests/host.c']
    for folder in ('firmware/src', 'firmware/hal', 'firmware/gen'):
        paths.extend(p for p in (ROOT/folder).rglob('*') if p.is_file() and p.suffix in ('.c', '.h', '.inc'))
    for path in sorted(paths):
        digest.update(str(path.relative_to(ROOT)).encode())
        digest.update(path.read_bytes())
    return digest.hexdigest()

def build():
    OUT.mkdir(parents=True, exist_ok=True)
    objects = sorted((ROOT/'build/host/braids').glob('*.o'))
    objects += sorted((ROOT/'build/host/plaits_reference').glob('*.o'))
    if not objects:
        raise RuntimeError('Build host DSP objects with tools/build_braids_host.sh first')
    with (OUT/'build.log').open('w') as log:
        subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c',
                        '-o', str(OUT/'planner.o')], cwd=ROOT, stdout=log, stderr=log, check=True)
        subprocess.run(['c++','-x','c',*FLAGS,'tools/ui_emulator.c','-x','none',
                        str(OUT/'planner.o'),*map(str,objects),'-o',str(OUT/'app')],
                       cwd=ROOT,stdout=log,stderr=log,check=True)

class App:
    def __init__(self, session):
        self.session = session
        self.lock = threading.Lock()
        self.built_revision = firmware_revision()
        self.start(session)

    def start(self, session):
        self.log = (OUT/'app.log').open('ab')
        self.proc = subprocess.Popen([str(OUT/'app'),str(session),str(OUT/'session.bin')],
                                    stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log)
        self.last = time.monotonic()

    def command(self, line):
        self.proc.stdin.write((line+'\n').encode()); self.proc.stdin.flush()
        meta = self.proc.stdout.readline()
        if not meta:
            raise RuntimeError('Host application exited; inspect build/ui-emulator/app.log')
        state = json.loads(meta)
        data = bytearray()
        while len(data) < 240*240*3:
            block = self.proc.stdout.read(240*240*3-len(data))
            if not block: raise RuntimeError('Incomplete display frame')
            data.extend(block)
        self.rgb = bytes(data)
        return state

    def request(self, request):
        with self.lock:
            elapsed = min(200, max(0, int((time.monotonic()-self.last)*1000)))
            self.last = time.monotonic()
            state = self.command('t '+str(elapsed))
            kind = request.get('type','tick')
            a = max(-100, min(100, int(request.get('a',0))))
            b = max(-100, min(100, int(request.get('b',0))))
            if kind in ('encoder','button','button_state','note','part','view'):
                code = {'encoder':'e','button':'b','button_state':'h','note':'n','part':'p','view':'v'}[kind]
                state = self.command(f'{code} {a} {int(bool(b)) if kind == "button_state" else b}')
            elif kind == 'reload':
                saved = self.command('s')
                if not saved.get('sessionSaved'):
                    raise RuntimeError('Could not preserve the local session; the emulator was not restarted')
                revision = firmware_revision()
                build()
                self.proc.stdin.close(); self.proc.wait(timeout=5); self.log.close()
                self.start(OUT/'session.bin')
                self.built_revision = revision
                self.command('p '+str(state['part']))
                state = self.command(f"v {state['page']} {state['screen']}")
            buf = io.BytesIO()
            Image.frombytes('RGB',(240,240),self.rgb).save(buf,format='PNG')
            state['image'] = base64.b64encode(buf.getvalue()).decode()
            return state

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port',type=int,default=8765)
    ap.add_argument('--session',type=Path,default=ROOT.parent/'results/ui-1502-final-session/session.bin')
    args = ap.parse_args(); build(); app = App(args.session)
    class Handler(BaseHTTPRequestHandler):
        def send(self, code, kind, data):
            self.send_response(code);self.send_header('Content-Type',kind)
            self.send_header('Cache-Control','no-store')
            self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        def do_GET(self):
            if self.path.split('?')[0] == '/revision':
                data = {'source': firmware_revision(), 'built': app.built_revision}
                self.send(200,'application/json',json.dumps(data).encode()); return
            if self.path.split('?')[0] != '/':
                self.send(404,'text/plain',b'Not found'); return
            self.send(200,'text/html; charset=utf-8',(ROOT/'tools/ui_emulator.html').read_bytes())
        def do_POST(self):
            if self.path != '/event':
                self.send(404,'text/plain',b'Not found'); return
            try:
                # Only the loopback page can drive this isolated local simulator.
                origin = self.headers.get('Origin')
                if origin and origin not in (f'http://127.0.0.1:{args.port}',f'http://localhost:{args.port}'):
                    self.send(403,'text/plain',b'Origin not allowed');return
                size = int(self.headers.get('Content-Length','0'))
                if not 0 < size <= 4096: raise ValueError('Invalid request length')
                value = app.request(json.loads(self.rfile.read(size)))
                self.send(200,'application/json',json.dumps(value).encode())
            except Exception as error:
                self.send(500,'application/json',json.dumps({'error':str(error)}).encode())
        def log_message(self,*args): pass
    server = ThreadingHTTPServer(('127.0.0.1',args.port),Handler)
    print(f'FM-1 local UI emulator: http://127.0.0.1:{args.port}',flush=True)
    try: server.serve_forever()
    finally:
        server.server_close();app.proc.stdin.close();app.proc.wait(timeout=5);app.log.close()

if __name__ == '__main__': main()
