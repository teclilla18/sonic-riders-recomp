"""Local preview at http://localhost:8018/sonic-riders-recomp/ (same subpath as GitHub Pages).

    py -3 tools/preview.py [port]
Missing pages get 404.html, like GitHub Pages."""
import functools
import http.server
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PREFIX = '/sonic-riders-recomp'


class Handler(http.server.SimpleHTTPRequestHandler):
    def send_head(self):
        if self.path in ('/', ''):
            self.send_response(302); self.send_header('Location', PREFIX + '/'); self.end_headers(); return None
        if not self.path.startswith(PREFIX + '/') and self.path != PREFIX:
            self.path = '/__missing__'
        else:
            self.path = self.path[len(PREFIX):] or '/'
        path = Path(self.translate_path(self.path))
        if path.is_dir():
            path = path / 'index.html'
        if not path.exists() or any(part in ('src', 'tools', '.git') for part in path.relative_to(ROOT).parts):
            body = (ROOT / '404.html').read_bytes()
            self.send_response(404); self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body))); self.end_headers()
            self.wfile.write(body); return None
        return super().send_head()

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def log_message(self, *a):
        pass


port = int(sys.argv[1]) if len(sys.argv) > 1 else 8018
print(f'Preview: http://localhost:{port}{PREFIX}/')
http.server.ThreadingHTTPServer(('127.0.0.1', port), functools.partial(Handler, directory=str(ROOT))).serve_forever()
