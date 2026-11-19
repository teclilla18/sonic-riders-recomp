"""Guard for the gh-pages branch (the site's version of main's tools/check_no_game_data.py).

Fails if anything that could be game data or personal data would be committed:
  - file types that are never allowed (disc images, executables, archives, game binaries, saves, keys)
  - any picture (jpg/png/webp/gif/svg) not listed with its exact SHA-1 in tools/approved_images.txt
  - SVGs with embedded bitmaps, files over 2 MB
  - home paths, drive-letter user folders or e-mail addresses in text files
Run: py -3 tools/check_site.py   (the pre-commit hook in tools/git-hooks runs it)"""
import hashlib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ALLOWED = {'.html', '.css', '.js', '.json', '.md', '.txt', '.py', '.svg', '.jpg', '.jpeg', '.png', '.webp', '.gif', ''}
PICTURES = {'.svg', '.jpg', '.jpeg', '.png', '.webp', '.gif'}
PRIVATE = [re.compile(p, re.I) for p in (r'[A-Z]:[\/]+Users[\/]', r'/home/[a-z]', r'/Users/[a-z]',
                                         r'[\w.+-]+@(gmail|outlook|hotmail|yahoo|icloud|proton)\.')]

files = subprocess.run(['git', '-C', str(ROOT), 'ls-files', '--cached', '--others', '--exclude-standard'],
                       capture_output=True, text=True, check=True).stdout.split('\n')
approved = {}
for line in (ROOT / 'tools/approved_images.txt').read_text(encoding='utf-8').splitlines():
    if line.strip() and not line.startswith('#'):
        sha, path = line.split()[:2]
        approved[path] = sha
bad = []
for name in filter(None, files):
    p = ROOT / name
    if not p.is_file():
        continue
    ext = p.suffix.lower()
    data = p.read_bytes()
    if ext not in ALLOWED:
        bad.append(f'{name}: file type {ext or "(none)"} is not allowed on the site')
        continue
    if len(data) > 2 * 1024 * 1024:
        bad.append(f'{name}: {len(data)} bytes, over the 2 MB limit')
    if ext in PICTURES:
        if approved.get(name) != hashlib.sha1(data).hexdigest():
            bad.append(f'{name}: picture not approved (SHA-1 missing or different in tools/approved_images.txt)')
        if ext == '.svg' and re.search(rb'<image|base64,', data):
            bad.append(f'{name}: SVG embeds a bitmap')
    elif ext in {'.html', '.css', '.js', '.json', '.md', '.txt', '.py', ''}:
        text = data.decode('utf-8', 'replace')
        if name == 'tools/check_site.py':
            continue
        for rx in PRIVATE:
            m = rx.search(text)
            if m:
                bad.append(f'{name}: looks like a personal path or address: {m.group(0)!r}')
if bad:
    print('check_site: NOT OK\n  ' + '\n  '.join(bad))
    sys.exit(1)
print(f'check_site: OK ({len([f for f in files if f])} files)')
