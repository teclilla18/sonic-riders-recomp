#!/usr/bin/env python3
"""Check generated guide links, image references, privacy and platform separation."""
from html.parser import HTMLParser
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / 'docs'


class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids=set(); self.links=[]; self.images=[]; self.checkpoints=0
    def handle_starttag(self,tag,attrs):
        a=dict(attrs)
        if 'id' in a:
            assert a['id'] not in self.ids, 'duplicate ID '+a['id']
            self.ids.add(a['id'])
        if tag=='a': self.links.append(a.get('href',''))
        if tag=='img':
            assert a.get('alt'), 'image without alternative text'
            self.images.append(a['src'])
        if tag=='input' and a.get('type')=='checkbox': self.checkpoints+=1


def main():
    total=0
    for mode in ['switch','switch2']:
        path=DOCS/f'install-{mode}.html'; text=path.read_text(encoding='utf8'); page=Page();page.feed(text)
        assert '<html lang="en">' in text and 'name="viewport"' in text
        assert page.checkpoints==17, (mode,page.checkpoints)
        assert len(page.images)>=29
        assert not re.search(r'(?i)([a-z]:[\\/]users[\\/]|\b\d{1,3}(?:\.\d{1,3}){3}\b|WDAGUtilityAccount)',text)
        assert 'fonts.googleapis.com' not in text, 'runtime font network dependency'
        for link in page.links+page.images:
            if link.startswith(('http:','https:','data:')): continue
            name,_,anchor=link.partition('#')
            target=(path.parent/name).resolve() if name else path
            assert target.exists(), (path.name,link)
            if not name and anchor: assert anchor in page.ids, (path.name,anchor)
            total+=1
        for name in page.images:
            f=path.parent/name
            assert f.stat().st_size<512*1024,(name,'oversized asset')
            if name.endswith('.svg'): ET.parse(f)
        # Only the other-booklet chooser may mention the other platform's download.
        if mode=='switch':
            assert 'SonicRiders-Switch2-Setup' not in text
            assert '/switch/sonicriders-switch2/' not in text
        else:
            assert 'SonicRiders-Switch-Setup-' not in text
            assert '/switch/sonicriders/' not in text
            assert 'hekate' not in text.lower()
            assert 'Atmosphère' not in text
        md=(DOCS/f'install-{mode}.md').read_text(encoding='utf8')
        assert md.count('**Checkpoint:**')==17
        assert all((DOCS/ref).exists() for ref in re.findall(r'!\[[^\]]*\]\(([^)]+)\)',md))
        print(f'{mode}: HTML/Markdown links, 17 checkpoints, image XML/size/alt, privacy and platform separation PASS')
    print(f'PASS: {total} local links and image references; both booklets have no required network assets.')


if __name__=='__main__':main()
