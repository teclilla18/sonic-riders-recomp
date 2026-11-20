#!/usr/bin/env python3
"""Generate the two illustrated booklets and accessible Markdown from one source.

No game content, browser session or image-generation service is required.
Reviewed installer screenshots are kept separately; drawings are original SVG.
"""
from html import escape
from pathlib import Path
import re
from console_guide_data import guide, REPO

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / 'docs'
ASSETS = DOCS / 'images/installation'

# Retain Claude's original easy-read palette, font families, color meanings and calm column.
CSS = r'''
:root{--bg:#FBF8F1;--card:#fff;--ink:#1F2430;--muted:#4A5160;--line:#E4DED0;--old:#2F6FD6;--old-bg:#EAF1FD;--new:#1E8A5A;--new-bg:#E6F5EC;--sd:#7A4FC9;--sd-bg:#F1EBFB;--btn:#B4235A;--btn-bg:#FCE8F0;--file:#0E7C86;--file-bg:#E3F4F5;--gold:#8A6100;--gold-bg:#FFF2C9;--warn:#9A5B00;--warn-bg:#FFF4E0;--body:'Atkinson Hyperlegible',Verdana,Tahoma,sans-serif;--head:Lexend,'Atkinson Hyperlegible',Verdana,sans-serif;color-scheme:light}
*{box-sizing:border-box}html{-webkit-text-size-adjust:100%;scroll-padding-top:1rem}body{margin:0;background:var(--bg);color:var(--ink);font:19px/1.7 var(--body);letter-spacing:.012em;word-spacing:.06em}main{max-width:780px;margin:auto;padding:24px max(16px,env(safe-area-inset-right)) 70px}h1,h2,h3,summary,.pill{font-family:var(--head);line-height:1.35;text-wrap:balance}h1{font-size:clamp(1.8rem,6vw,2.5rem);margin:14px 0 8px}h2{font-size:1.35rem;margin:0 0 12px}h3{font-size:1.1rem}p{margin:12px 0}a{color:var(--old);text-underline-offset:3px}a:hover{text-decoration-thickness:3px}a,button,input,summary{touch-action:manipulation}:focus-visible{outline:3px solid var(--btn);outline-offset:4px}code{font:700 .92em/1.6 var(--body);color:var(--file);background:var(--file-bg);border-radius:5px;padding:1px 5px;overflow-wrap:anywhere}strong{font-weight:700}.button{color:var(--btn);background:var(--btn-bg);font-weight:700;padding:1px 5px;border-radius:5px}.screen{color:var(--gold);background:var(--gold-bg);padding:1px 5px;border-radius:5px}.sub,.small,figcaption{color:var(--muted)}.small,figcaption{font-size:.9rem}.pill{display:inline-block;background:var(--old-bg);color:var(--old);border:1px solid var(--old);border-radius:999px;font-size:.85rem;padding:4px 12px}.pill.alpha{color:var(--warn);background:var(--warn-bg);border-color:var(--warn)}.book-cover{position:relative;padding:26px 22px 22px;background:var(--card);border:2px solid var(--line);border-top:8px solid var(--old);border-radius:18px;margin:18px 0 24px;overflow:hidden}.rings{display:flex;gap:8px;margin:8px 0 15px}.rings i{display:block;width:24px;height:24px;border:4px solid #d9a829;border-radius:50%;box-shadow:inset 1px 1px 0 #fff}.stripe{height:9px;background:repeating-linear-gradient(110deg,var(--old) 0 16px,transparent 16px 24px);opacity:.18;margin:20px -22px -22px}.toplinks,.tools{display:flex;flex-wrap:wrap;gap:10px 20px;align-items:center}.tools button{font:inherit;color:var(--ink);background:var(--card);border:1px solid var(--line);border-radius:10px;padding:7px 12px;cursor:pointer}.legend,.contents{padding:18px 20px;border:1px solid var(--line);background:var(--card);border-radius:14px;margin:20px 0}.legend p{margin:5px 0}.legend .button,.legend code,.legend .purple,.legend .screen{display:inline-block}.purple{color:var(--sd);background:var(--sd-bg);padding:1px 6px;border-radius:5px}.contents ol{margin:10px 0;padding-left:24px}.contents li{margin-bottom:7px}.download{display:block;background:var(--btn);color:var(--card);border:2px solid var(--btn);border-radius:12px;padding:14px 16px;text-align:center;font:700 1rem/1.6 var(--head);overflow-wrap:anywhere;text-decoration:none;min-height:48px}.download small{display:block;font:400 .9rem/1.6 var(--body)}.chapter{background:var(--card);border:1px solid var(--line);border-left:8px solid var(--new);border-radius:14px;padding:22px;margin:0 0 28px}.chapter.old{border-left-color:var(--old)}.chapter.sd{border-left-color:var(--sd)}.chapter.old h2{color:var(--old)}.chapter.new h2{color:var(--new)}.chapter.sd h2{color:var(--sd)}.page-label{font:700 .78rem/1.6 var(--head);letter-spacing:.1em;text-transform:uppercase;color:var(--muted);margin-bottom:6px}.steps{list-style:none;counter-reset:step;margin:18px 0;padding:0}.steps li{counter-increment:step;position:relative;padding:10px 4px 10px 48px;margin:4px 0}.steps li:before{content:counter(step);position:absolute;left:0;top:13px;display:grid;place-items:center;width:32px;height:32px;color:#fff;background:var(--new);border-radius:50%;font:700 .95rem var(--head)}.old .steps li:before{background:var(--old)}.sd .steps li:before{background:var(--sd)}figure{margin:20px 0}figure>a{display:block}figure img{display:block;width:100%;height:auto;border-radius:10px;border:1px solid var(--line);background:#FBF8F1}figcaption{margin-top:7px;line-height:1.5}.checkpoint{display:block;background:var(--new-bg);border:1px solid var(--new);border-radius:12px;padding:13px 15px;margin:18px 0}.checkpoint input{width:22px;height:22px;vertical-align:middle;margin:0 8px 0 0;accent-color:var(--new)}.checkpoint b{color:var(--new)}.note,.warning{border-radius:12px;padding:14px 16px;margin:18px 0;line-height:1.65}.note{background:var(--sd-bg);border:1px solid var(--sd)}.warning{background:var(--warn-bg);border:1px solid var(--warn)}.warning b{color:var(--warn)}details{border:1px solid var(--line);border-radius:12px;padding:14px 16px;margin:14px 0;background:var(--card)}summary{cursor:pointer;min-height:32px;font-weight:700}details[open]>summary{margin-bottom:12px}.help{border-left:8px solid var(--gold)}.help img{max-height:340px;object-fit:contain}.tree{font:400 .85rem/1.7 ui-monospace,Consolas,monospace;background:var(--sd-bg);color:var(--ink);white-space:pre;overflow-x:auto;padding:16px;border-radius:12px;letter-spacing:0;word-spacing:0}.footer{border:2px dashed var(--line);border-radius:14px;padding:20px;margin-top:28px}.skip{position:absolute;left:12px;top:-100px;background:var(--card);padding:12px;z-index:9}.skip:focus{top:12px}.end-link{display:block;text-align:right;margin-top:16px;font-weight:700}.source-list{padding-left:24px;overflow-wrap:anywhere}button[hidden]{display:none}.sample{white-space:pre-wrap;font:inherit;background:var(--old-bg);padding:16px;border-radius:12px}
@media(max-width:480px){body{font-size:18px}.chapter{padding:17px 13px;border-left-width:6px}.steps li{padding-left:41px}.steps li:before{width:29px;height:29px}.book-cover{padding:20px 16px}.tree{font-size:.74rem}.toplinks{font-size:.9rem}}
@media(prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#1B1D22;--card:#24272E;--ink:#ECE8DF;--muted:#B9B4A9;--line:#3A3E47;--old:#8DB5FF;--old-bg:#22304A;--new:#7FD6A7;--new-bg:#1E3A2C;--sd:#C7A9FF;--sd-bg:#33284A;--btn:#FF9CC2;--btn-bg:#4A2333;--file:#7FD8DF;--file-bg:#1D3A3D;--gold:#FFD465;--gold-bg:#43360F;--warn:#FFC371;--warn-bg:#40300F;color-scheme:dark}}
:root[data-theme=dark]{--bg:#1B1D22;--card:#24272E;--ink:#ECE8DF;--muted:#B9B4A9;--line:#3A3E47;--old:#8DB5FF;--old-bg:#22304A;--new:#7FD6A7;--new-bg:#1E3A2C;--sd:#C7A9FF;--sd-bg:#33284A;--btn:#FF9CC2;--btn-bg:#4A2333;--file:#7FD8DF;--file-bg:#1D3A3D;--gold:#FFD465;--gold-bg:#43360F;--warn:#FFC371;--warn-bg:#40300F;color-scheme:dark}
@media print{body{background:white;color:#1F2430;font-size:12pt}main{max-width:none;padding:0}.tools,.skip,.end-link{display:none}.chapter{break-before:page;break-inside:auto;border:1px solid #ccc;border-left:6px solid #1E8A5A}figure,li,.checkpoint{break-inside:avoid}details{display:block}a{color:#1F2430}figure img{max-height:140mm;object-fit:contain}.contents{break-after:page}}
@media(prefers-reduced-motion:no-preference){html{scroll-behavior:smooth}}
.steps li:before{background:#18794e;color:#fff}.old .steps li:before{background:#2F6FD6}.sd .steps li:before{background:#7A4FC9}
'''


def inline(s):
    """Only the deliberately small Markdown vocabulary used by our content."""
    tokens = []
    def stash(m):
        tokens.append('<code>' + escape(m[1]) + '</code>')
        return f'\x01{len(tokens)-1}\x02'
    s = re.sub(r'`([^`]+)`', stash, s)
    s = escape(s)
    s = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', lambda m: f'<a href="{m[2]}">{m[1]}</a>', s)
    s = re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', s)
    s = re.sub(r'\x01(\d+)\x02', lambda m: tokens[int(m[1])], s)
    # Match Claude's button / on-screen color key without coloring whole paragraphs.
    for text in ['Select disc…','Open Output Folder','BUILD AND INSTALL','Refresh','Other…','Eject SD card','Extract All…','Extract','Open','Copy the disc image too','Quit Game','Read shader compiler license','I accept the Android SDK License']:
        s = s.replace('<strong>'+text+'</strong>', '<strong class="button">'+text+'</strong>')
    for text in ['Done','INSTALLATION COMPLETE','INSTALL FAILED','Safe to remove the SD card now.']:
        s = s.replace('<strong>'+text+'</strong>', '<strong class="screen">'+text+'</strong>')
    return s


def artwork(g, key):
    """Original labelled instruction diagrams. Never imitate a hardware screenshot."""
    app, out = g['app'], g['output']
    native = g['native']
    drawings = {
      'release':('Find this file under Assets', [('1 · CHECK THE RELEASE',('Switch 2 Alpha 3 · Pre-release' if native else 'v0.9.0 · Stable'),'Use the release-page link below'),('2 · OPEN ASSETS',g['archive'],'This is the setup ZIP to download'),('3 · SKIP THESE','Source code (zip / tar.gz)','Those are not the Windows installer')]),
      'ready':('Installation requirements', [('CONSOLE',g['platform'],'Homebrew already working'),('COMPUTER','Windows 10 / 11 · 64-bit','Internet + free space'),('YOUR FILES','Prepared SD card + reader','Your own USA GameCube disc')]),
      'disc':('Create an image of your own disc',[('1 · YOUR DISC','Sonic Riders · GameCube','USA · revision 0'),('2 · MAKE A COPY','CleanRip on a compatible Wii','Follow the linked dumping guide'),('3 · ON THE PC','Full .iso / clean .rvz','The installer checks the contents')]),
      'extract':('Extract the ZIP file',[('1 · RIGHT-CLICK','Your downloaded setup ZIP','Choose Extract All…'),('2 · EXTRACT','Keep every file together','Use a normal PC folder'),('3 · OPEN THE FOLDER','Installer EXE + setup + tools','An EXE alone is not enough')]),
      'card':('Open the SD card root folder',[('1 · POWER OFF','Quit → fully power off','Then remove the prepared card'),('2 · CONNECT','Put the card in a reader','Open File Explorer → This PC'),('3 · OPEN THE CARD','You can see switch/','This top level is called the root')]),
      'build':('Build and installation progress',[('1 · CHECK + DOWNLOAD','Your image → checked tools','Keep the computer online'),('2 · BUILD + COPY','Make the console app','Keep the card connected'),('3 · LOOK FOR SUCCESS','Done / Installation complete','Failure? Read setup.log')]),
      'copy':('Copy files to the SD card', [('ON YOUR COMPUTER','Open Output Folder',out.replace('build/','build / ')),('COPY → PASTE',app+'.nro','Plus your full ISO / GCM'),('ON YOUR SD CARD','switch / '+app,'Put the files together here')]),
      'eject':('Safely eject the SD card',[('1 · WAIT','Copy finished?','Close files using the SD card'),('2 · EJECT','Eject SD card / Windows Eject','Wait for Safe to remove'),('3 · REINSERT','Console fully powered off','Put the card back, then boot')]),
      'launch':('Launch the game',[('1 · HOLD R',('Open a native Switch 2 game' if native else 'Open an installed game'),'Keep R held for Homebrew Menu'),('2 · HOMEBREW MENU','Choose Sonic Riders','Press A'),('3 · GAME STARTS','Intro → title → game menu','Follow the on-screen prompts')]),
      'play':('Verify the installed version',[('1 · SETTINGS','Hold Minus (−) ~ half a second','Open About'),('2 · CHECK THE BUILD',('Switch 2 Alpha 3' if native else 'Stable v0.9.0'),'Compare the version / build'),('3 · FINISH SAFELY','Quit Game → confirm','Wait for Homebrew Menu')]),
      'update':('Back up and update',[('1 · BACK UP','Copy the whole game folder','Keep a spare copy on the PC'),('2 · REPLACE THE APP',app+'.nro',('Also its matching build-info.json' if native else 'Use the new successful build')),('3 · KEEP YOUR DATA','saves/ · settings.ini · mods/','Keep env.txt and caches too')]),
      'report':('Create a useful bug report',[('1 · WHAT HAPPENED?','Platform + version + steps','Expected result → actual result'),('2 · CLEAN LOG COPIES','setup.log / console logs','Hide private paths and addresses'),('3 · SEND THE REPORT','Use the linked bug form','No game image, NRO, saves or keys')]),
    }
    if key == 'folders':
        lines = [f'SD card → switch → {app}',app+'.nro',('build-info.json + env.txt + settings.ini' if native else 'Your full ISO / GCM beside the NRO'),'SonicRiders.iso  (example name)']
        return svg_tree(g, lines)
    title, cards = drawings[key]
    svg = svg_start(title)
    for i,(label,headline,detail) in enumerate(cards):
        y = 116 + i*116
        colors=['#2F6FD6','#7A4FC9','#1E8A5A']
        svg += f'<rect x="26" y="{y}" width="848" height="100" rx="14" fill="white" stroke="#E4DED0"/><circle cx="65" cy="{y+49}" r="22" fill="{colors[i]}"/><text x="65" y="{y+57}" text-anchor="middle" fill="white" font-size="24" font-weight="bold">{i+1}</text><text x="108" y="{y+24}" fill="{colors[i]}" font-size="16" font-weight="bold">{escape(label)}</text>'
        for j,line in enumerate(wrap(headline,56)):
            svg += f'<text x="108" y="{y+52+j*25}" font-size="{19 if len(headline)>47 else 23}" font-weight="bold">{escape(line)}</text>'
        svg += f'<text x="108" y="{y+86}" font-size="19" fill="#4A5160">{escape(detail if len(detail)<75 else "Open the folder shown in the instructions below")}</text>'
    return svg + '</svg>'


def wrap(s, length):
    if len(s)<=length: return [s]
    cut = s.rfind(' ',0,length)
    if cut<0: cut=length
    return [s[:cut],s[cut:].strip()]


def svg_start(title):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="900" height="490" viewBox="0 0 900 490" role="img" aria-label="{escape(title)}"><title>{escape(title)}</title><rect width="900" height="490" rx="18" fill="#FBF8F1"/><g fill="#1F2430" font-family="Verdana, sans-serif"><text x="28" y="30" font-size="14" fill="#7A4FC9" font-weight="bold">ILLUSTRATION · NOT A SCREENSHOT</text><text x="28" y="77" font-size="30" font-weight="bold">{escape(title)}</text></g><g fill="#1F2430" font-family="Verdana, sans-serif">'''.replace('</g><g fill="#1F2430" font-family="Verdana, sans-serif">','')


def svg_tree(g, lines):
    s = svg_start('Correct and incorrect SD card layouts')
    s += '<rect x="25" y="105" width="850" height="240" rx="16" fill="#E6F5EC" stroke="#1E8A5A"/><text x="47" y="139" font-size="23" fill="#156B42" font-weight="bold">YES · files together in this game folder</text>'
    for i,line in enumerate(lines):
        s+=f'<text x="47" y="{181+i*42}" font-size="{23 if i==0 else 22}" fill="#1F2430">{escape(line)}</text>'
    s+='<rect x="25" y="367" width="850" height="98" rx="16" fill="#FCE8F0" stroke="#B4235A"/><text x="47" y="400" font-size="23" fill="#B4235A" font-weight="bold">NO · one switch folder too many</text>'
    s+=f'<text x="47" y="442" font-size="21">SD → switch → switch → {escape(g["app"])}</text>'
    return s+'</svg>'


REAL = {'installer','select-disc','select-card','options','completion'}


def figure(g,key,alt):
    suffix = '.png' if key in REAL else '.svg'
    name=f'images/installation/{g["mode"]}-{key}{suffix}'
    kind=('Actual screenshot · real installer run to a test SD folder · annotations added' if key=='completion' else 'Actual screenshot · annotations added') if key in REAL else 'Illustration · not a screenshot'
    return f'<figure><a href="{name}" aria-label="Open larger picture: {escape(alt)}"><img src="{name}" alt="{escape(alt)}" loading="lazy" width="900" height="{490 if key not in REAL else 580}"></a><figcaption>{kind}. Tap the picture to open it larger.</figcaption></figure>'


def build(g):
    slug='install-'+g['mode']
    fonts = (DOCS/'images/installation/fonts.css').read_text() if (DOCS/'images/installation/fonts.css').exists() else ''
    html=f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><meta name="description" content="An illustrated, beginner-friendly Sonic Riders Recomp installation booklet for {g['platform']} — {g['edition']}."><title>{g['platform']} — Sonic Riders illustrated installation guide</title><style>{fonts}{CSS}</style></head><body><a class="skip" href="#start">Skip to the steps</a><main><nav class="toplinks" aria-label="Other formats"><a href="index.html">Choose your console</a><a href="{slug}.md">Plain-text / Markdown guide</a><a href="https://github.com/teclilla18/sonic-riders-recomp/blob/main/README.md">Project README</a></nav><header class="book-cover"><span class="pill {'alpha' if g['native'] else ''}">{escape(g['edition'])}</span><div class="rings" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i></div><p class="page-label">SONIC RIDERS RECOMP · INSTALLATION GUIDE</p><h1>Install Sonic Riders<br>on {g['platform']}</h1><p>{g['intro']}</p><p class="sub">Checked for <b>{g['version']}</b> · November 18, 2026</p><p>You can read this on your phone while using the computer. Tick each checkpoint as you go. No coding needed.</p><div class="stripe" aria-hidden="true"></div></header><div class="tools"><button id="theme" type="button" hidden>Change light / dark colors</button><button id="print" type="button" hidden>Print this booklet</button><a href="#help">Something went wrong?</a></div>'''
    md=f'# Sonic Riders Recomp — {g["platform"]}\n\n## {g["edition"]}\n\nChecked for **{g["version"]}** on November 18, 2026.\n\n{g["intro"]}\n\n[Illustrated HTML booklet]({slug}.html) · [Choose your console](index.md) · [{g["other_name"]}]({g["other_md"]})\n\nTick the checkpoints as you go. Pictures have text descriptions.\n\n'
    if g['native']:
        warning='**Please read first:** this is an alpha (early test version). It plays well in our tests, but bugs can still interrupt play. Report anything odd with your log.'
        html+='<aside class="warning">'+inline(warning)+'</aside>'
        md+=warning+'\n\n'
    html+=f'<aside class="note">Wrong console? Open the separate <a href="{g["other"]}">{g["other_name"]} booklet</a>. Follow only the booklet for the console in front of you.</aside><aside class="legend"><h2>Color key</h2><p><span class="button">Pink</span> = a button to press</p><p><code>Teal</code> = a file or program</p><p><span class="purple">Purple</span> = SD card steps</p><p><span class="screen">Gold</span> = what to look for</p></aside><nav class="contents" aria-label="Booklet contents"><h2>Installation steps</h2><ol>'
    md+='## Installation steps\n\n'
    for i,c in enumerate(g['chapters'],1):
        html+=f'<li><a href="#{c["key"]}">{escape(c["title"])}</a></li>'
        md+=f'{i}. [{c["title"]}](#{c["key"]})\n'
    html+='<li><a href="#help">Picture-by-picture help</a></li><li><a href="#report">Send a useful bug report</a></li></ol></nav><div id="start"></div>'
    md+='\n[Help](#help) · [Report a bug](#report)\n\n'
    for i,c in enumerate(g['chapters'],1):
        icon={'ready':'🎒','disc':'💿','download':'⬇️','extract':'📦','card':'💾','open':'🖱️','choose-disc':'💿','choose-card':'💾','options':'✨','build':'🧰','output':'📂','manual':'📋','folders':'🗂️','eject':'✅','launch':'🎮','play':'🏁','update':'🔄'}[c['key']]
        html+=f'<section class="chapter {c["tone"]}" id="{c["key"]}"><div class="page-label">Page {i:02} · {g["platform"]}</div><h2><span aria-hidden="true">{icon}</span> {escape(c["title"])}</h2>'
        html+=figure(g,c['picture'],c['alt'])
        html+='<ol class="steps">'+''.join('<li>'+inline(a)+'</li>' for a in c['actions'])+'</ol>'
        md+=f'<a id="{c["key"]}"></a>\n\n## {i:02}. {c["title"]}\n\n'
        pic=f'images/installation/{g["mode"]}-{c["picture"]}{".png" if c["picture"] in REAL else ".svg"}'
        md+=f'![{c["alt"]}]({pic})\n\n*{"Actual screenshot with annotations" if c["picture"] in REAL else "Illustration, not a screenshot"}.*\n\n'
        md+='\n'.join(f'{j}. {a}' for j,a in enumerate(c['actions'],1))+'\n\n'
        if c['key']=='download':
            html+=f'<a class="download" href="{g["download"]}">Download the {g["platform"]} setup ZIP<small>{g["archive"]}</small></a>'
            md+=f'**[Download {g["archive"]}]({g["download"]})**\n\n'
        if c['key']=='folders':
            html+='<h3>The files needed to start</h3><pre class="tree" tabindex="0">'+escape(g['tree'])+'</pre><h3>After playing or adding packs, you may also see</h3><pre class="tree" tabindex="0">'+escape(g['later'])+'</pre>'
            md+='### The files needed to start\n\n```text\n'+g['tree']+'```\n\n### After playing or adding packs\n\n```text\n'+g['later']+'```\n\n'
        if c['note']:
            html+='<aside class="note">'+inline(c['note'])+'</aside>'
            md+='> '+c['note']+'\n\n'
        if c['extra']:
            html+='<details><summary>Optional details for a helper</summary><p>'+inline(c['extra'])+'</p></details>'
            md+=c['extra']+'\n\n'
        html+=f'<label class="checkpoint"><input type="checkbox"><b>Checkpoint:</b> {inline(c["checkpoint"])}</label>'
        md+='- [ ] **Checkpoint:** '+c['checkpoint']+'\n\n'
        nextkey=g['chapters'][i]['key'] if i<len(g['chapters']) else 'help'
        html+=f'<a class="end-link" href="#{nextkey}">Next page →</a></section>'
    html+='<section class="chapter help old" id="help"><div class="page-label">Troubleshooting</div><h2>Troubleshooting</h2><p>Find the sentence that sounds like your problem. Select it to view the relevant instructions.</p>'
    md+='<a id="help"></a>\n\n## Picture-by-picture help\n\n'
    for key,title,ref,body in g['trouble']:
        c=next(c for c in g['chapters'] if c['key']==ref)
        html+=f'<details id="help-{key}"><summary>{escape(title)}</summary>'+figure(g,c['picture'],c['alt'])+'<p>'+inline(body)+f'</p><a href="#{ref}">Go back to this page’s steps →</a></details>'
        md+=f'### {title}\n\n'+body+f'\n\n[Picture and steps](#{ref}).\n\n'
    html+='</section><section class="chapter old" id="report"><h2>Report a problem</h2>'+figure(g,'report','Illustration: write the platform and steps, review log copies for private information, then use the bug form.')+'<ol class="steps">'+''.join('<li>'+inline(a)+'</li>' for a in g['logs'])+'</ol>'
    md+='<a id="report"></a>\n\n## Report a problem\n\n'+f'![Illustration: a useful report includes steps and cleaned log copies.](images/installation/{g["mode"]}-report.svg)\n\n'+'\n'.join(f'{i}. {a}' for i,a in enumerate(g['logs'],1))+'\n\n'
    sample=f'Platform: {g["platform"]}\nSetup ZIP: {g["archive"]}\nVersion/build in About: …\nFirmware and homebrew loader/version: …\nHandheld or docked; known clocks: …\nWhat I did, one step at a time: …\nWhat I expected: …\nWhat happened / exact error: …\nDoes it happen again? …\nCleaned log copies attached: …'
    html+='<h3>Bug report template</h3><pre class="sample">'+escape(sample)+'</pre></section>'
    md+='### Bug report template\n\n```text\n'+sample+'\n```\n\n'
    evidence='The current published ZIPs were downloaded and hash-checked. Their installers were run on Windows into disposable SD folders; disc copies and updates were checked. The actual installer captures come from these published executables. Console steps use existing project hardware reports and the linked homebrew instructions. No console was reinstalled or play-tested for this booklet. Drawn console screens are clearly marked illustrations.'
    html+='<footer class="footer"><h2>Installation complete</h2><p>Keep your backup until you have confirmed that the game works. Refer to this guide when updating or troubleshooting.</p><details><summary>Sources, picture credits and what was checked</summary><p>'+evidence+'</p><ul class="source-list"><li><a href="'+g['release']+'">This platform’s published release</a></li><li>'+inline(g['homebrew'])+'</li><li><a href="guide-verification.md">Exact package hashes, source checks, picture provenance and validation limits</a></li><li><a href="INSTALL.md">Existing advanced installation documentation</a></li></ul><p>Visual reference: the project’s Claude-made easy-read install and update guides. Same warm colors, Atkinson Hyperlegible body type, Lexend headings, numbered steps and friendly color key. Installer artwork retains the existing project credits; banner fan art and game icon credits are in the README. No console screenshot or game artwork was generated for this guide.</p></details><p class="small">Unofficial fan project. Sonic Riders and its characters belong to SEGA. Nintendo Switch is a Nintendo trademark.</p></footer></main><script>for(const id of ["theme","print"])document.getElementById(id).hidden=false;document.getElementById("theme").onclick=()=>{let root=document.documentElement;let dark=root.dataset.theme?root.dataset.theme==="dark":matchMedia("(prefers-color-scheme: dark)").matches;root.dataset.theme=dark?"light":"dark";};document.getElementById("print").onclick=()=>{document.querySelectorAll("details").forEach(d=>d.open=true);window.print();};</script></body></html>'
    md+='## Sources and verification\n\n'+evidence+'\n\n[Exact package hashes, sources and picture credits](guide-verification.md). The visual reference is the project’s existing Claude-made easy-read guides. Technical documentation remains in [INSTALL.md](INSTALL.md).\n'
    (DOCS/(slug+'.html')).write_text(html,encoding='utf-8')
    (DOCS/(slug+'.md')).write_text(md,encoding='utf-8')
    for key in ['ready','disc','release','extract','card','build','copy','folders','eject','launch','play','update','report']:
        # svg_start opens the text styling group; close it explicitly.
        data=artwork(g,key).replace('</svg>','</g></svg>')
        (ASSETS/f'{g["mode"]}-{key}.svg').write_text(data,encoding='utf-8')


def main():
    ASSETS.mkdir(parents=True,exist_ok=True)
    for native in [False,True]: build(guide(native))
    chooser='''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Sonic Riders — choose your installation booklet</title><style>'''+(ASSETS/'fonts.css').read_text(encoding='utf8')+CSS+'''</style></head><body><main><header class="book-cover"><div class="rings" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i></div><h1>Sonic Riders<br>installation guides</h1><p>Select your console and follow its installation guide.</p></header><section class="chapter old"><span class="pill">Stable release · v0.9.0</span><h2>Nintendo Switch</h2><p>For Nintendo Switch, Switch Lite and Switch OLED.</p><a class="download" href="install-switch.html">Open the Switch picture booklet →</a><p><a href="install-switch.md">Plain-text / Markdown version</a></p></section><section class="chapter sd"><span class="pill alpha">Experimental · Alpha 3</span><h2>Nintendo Switch 2</h2><p>For a compatible native Switch 2 homebrew setup. Known audio problems remain.</p><a class="download" href="install-switch2.html">Open the Switch 2 picture booklet →</a><p><a href="install-switch2.md">Plain-text / Markdown version</a></p></section><p>Both guides use a Windows computer and your own legally dumped USA GameCube disc. No account is needed to read or download.</p><p><a href="INSTALL.md">Technical installation documentation and other platforms</a> · <a href="https://github.com/teclilla18/sonic-riders-recomp/blob/main/README.md">Project README</a></p></main></body></html>'''
    (DOCS/'index.html').write_text(chooser,encoding='utf-8')
    (DOCS/'index.md').write_text('# Sonic Riders installation booklets\n\nPick your console. Follow only its booklet.\n\n- **Nintendo Switch / Lite / OLED — stable v0.9.0:** [Illustrated booklet](install-switch.html) · [Accessible Markdown](install-switch.md).\n- **Nintendo Switch 2 — experimental Alpha 3:** [Illustrated booklet](install-switch2.html) · [Accessible Markdown](install-switch2.md). Known audio problems remain.\n\n[Advanced installation documentation and other platforms](INSTALL.md).\n',encoding='utf-8')
    print('Generated two independent booklets, Markdown alternatives, chooser and 26 original SVG diagrams.')


if __name__=='__main__': main()
