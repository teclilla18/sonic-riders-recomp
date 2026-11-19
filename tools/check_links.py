"""Check every built page: local links, pictures and #anchors exist; optionally (--external) that outside links answer.

    py -3 tools/check_links.py [--external]"""
import re
import sys
import urllib.request
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SITE = 'https://teclilla18.github.io/sonic-riders-recomp/'


class P(HTMLParser):
    def __init__(self):
        super().__init__(); self.links = []; self.ids = set()

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if 'id' in a: self.ids.add(a['id'])
        for k in ('href', 'src'):
            if a.get(k) and not (tag == 'link' and a.get('rel') in ('preconnect', 'canonical')) and tag != 'base':
                self.links.append(a[k])


pages = {}
for f in ROOT.glob('*.html'):
    p = P(); p.feed(f.read_text(encoding='utf-8')); pages[f.name] = p
bad, external = [], set()
for name, p in pages.items():
    if name == '404.html':
        continue
    for link in p.links:
        if link.startswith(('http://', 'https://')):
            if link.startswith(SITE):
                link = link[len(SITE):] or 'index.html'
            else:
                external.add(link); continue
        if link.startswith(('mailto:', 'data:')):
            continue
        path, _, anchor = link.partition('#')
        target = path or name
        if target in ('./', ''):
            target = 'index.html'
        if not (ROOT / target).exists():
            bad.append(f'{name}: missing {link}')
        elif anchor and target.endswith('.html') and target in pages and anchor not in pages[target].ids:
            # tab names (#pc, #android...) are handled by site.js
            if not re.search(r'data-dev="' + re.escape(anchor) + '"', (ROOT / target).read_text(encoding='utf-8')):
                bad.append(f'{name}: no #{anchor} in {target}')
print(f'{len(pages)} pages, {sum(len(p.links) for p in pages.values())} links, {len(external)} external')
if '--external' in sys.argv:
    for url in sorted(external):
        if 'fonts.g' in url:
            continue
        try:
            req = urllib.request.Request(url.split('#')[0], headers={'User-Agent': 'Mozilla/5.0 (link check)'})
            code = urllib.request.urlopen(req, timeout=20).status
        except Exception as e:
            code = getattr(e, 'code', str(e)[:60])
        if code != 200:
            bad.append(f'external {code}: {url}')
print('\n'.join(bad) or 'all links OK')
sys.exit(1 if bad else 0)
