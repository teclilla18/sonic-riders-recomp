#!/usr/bin/env python3
"""The README's "Road to v1.0" progress, from one place. Edit AREAS (and VERSION / UPDATED) below, then run:

    python tools/make_progress.py            writes everything below
    python tools/make_progress.py --check    fails if anything is out of date (CI)

From the same numbers it writes:
  docs/images/progress.svg      the card in the README's Status section (overall + every area)
  docs/images/progress-bar.svg  the slim bar at the very top of the README
  docs/progress.json            a shields.io endpoint badge (the README's badge row reads it live)
  README.md                     the "Done" column of the Road to v1.0 table and the pictures' alt text

The overall percentage is the weighted average of the areas. GitHub Actions (.github/workflows/progress.yml)
runs this on every push to main and commits the result, so changing AREAS is enough. The README's
"Road to v1.0" table explains what each area still needs; keep its "Left for v1.0" text in step.
"""
import json
import re
import sys
from pathlib import Path

VERSION = "v0.10.0 Online Beta"
UPDATED = "2026-11-26"

# (label, weight, percent done); labels must match the README table's first column
AREAS = [
    ("Whole game playable",            25, 90),
    ("Graphics match the GameCube",    15, 80),
    ("Switch performance",             15, 70),
    ("Platforms",                      15, 85),
    ("Online play",                    15, 72),
    ("Features and polish",            15, 73),
]

W = 720
PAD = 28
ROW_H = 30
TOP = 128
FONT = "'Segoe UI', -apple-system, 'Helvetica Neue', Arial, sans-serif"

GRADIENTS = (
    '<linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">'
    '<stop offset="0" stop-color="#0b1a3f"/><stop offset="1" stop-color="#132c66"/></linearGradient>'
    '<linearGradient id="fill" x1="0" y1="0" x2="1" y2="0">'
    '<stop offset="0" stop-color="#1e5bff"/><stop offset="0.6" stop-color="#2fb8ff"/>'
    '<stop offset="1" stop-color="#ffd23f"/></linearGradient>'
    '<linearGradient id="shine" x1="0" y1="0" x2="0" y2="1">'
    '<stop offset="0" stop-color="#fff" stop-opacity="0.35"/>'
    '<stop offset="0.5" stop-color="#fff" stop-opacity="0"/></linearGradient>'
)


def overall() -> int:
    total = sum(w for _, w, _ in AREAS)
    return round(sum(w * p for _, w, p in AREAS) / total)


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def svg() -> str:
    """The card: overall bar plus one bar per area."""
    pct = overall()
    h = TOP + len(AREAS) * ROW_H + 26
    bar_x, bar_w, bar_y, bar_h = PAD, W - 2 * PAD, 74, 22
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{h}" viewBox="0 0 {W} {h}" '
        f'role="img" aria-label="Road to v1.0: {pct}% done">',
        f"<title>Road to v1.0: {pct}% done</title>",
        "<defs>",
        GRADIENTS,
        f'<clipPath id="barclip"><rect x="{bar_x}" y="{bar_y}" width="{bar_w}" height="{bar_h}" rx="11"/></clipPath>',
        "</defs>",
        f'<rect width="{W}" height="{h}" rx="14" fill="url(#bg)"/>',
        f'<rect x="0.5" y="0.5" width="{W-1}" height="{h-1}" rx="14" fill="none" stroke="#2a4a99"/>',
        f'<g font-family="{FONT}">',
        f'<text x="{PAD}" y="44" fill="#ffffff" font-size="22" font-weight="700" letter-spacing="1.5">ROAD TO v1.0</text>',
        f'<text x="{PAD}" y="63" fill="#8fa6d9" font-size="12.5">Current release {esc(VERSION)} · updated {esc(UPDATED)}</text>',
        f'<text x="{W-PAD}" y="56" fill="#ffd23f" font-size="40" font-weight="800" text-anchor="end">{pct}%</text>',
        # main bar: track, fill, tick marks every 10 %, gloss
        f'<rect x="{bar_x}" y="{bar_y}" width="{bar_w}" height="{bar_h}" rx="11" fill="#08132e" stroke="#2a4a99"/>',
        f'<g clip-path="url(#barclip)">',
        f'<rect x="{bar_x}" y="{bar_y}" width="{bar_w * pct / 100:.1f}" height="{bar_h}" fill="url(#fill)"/>',
    ]
    for t in range(1, 10):
        x = bar_x + bar_w * t / 10
        out.append(f'<rect x="{x:.1f}" y="{bar_y}" width="1.5" height="{bar_h}" fill="#08132e" opacity="0.55"/>')
    out += [
        f'<rect x="{bar_x}" y="{bar_y}" width="{bar_w}" height="{bar_h}" fill="url(#shine)"/>',
        "</g>",
        f'<line x1="{PAD}" y1="{TOP-18}" x2="{W-PAD}" y2="{TOP-18}" stroke="#2a4a99" stroke-dasharray="3 4"/>',
    ]
    label_w = 250
    sx = PAD + label_w
    sw = W - 2 * PAD - label_w - 56
    for i, (label, _w, p) in enumerate(AREAS):
        y = TOP + i * ROW_H
        colour = "#ffd23f" if p >= 85 else ("#2fb8ff" if p >= 50 else "#ff7a59")
        out += [
            f'<text x="{PAD}" y="{y+5}" fill="#dfe8ff" font-size="14">{esc(label)}</text>',
            f'<rect x="{sx}" y="{y-6}" width="{sw}" height="12" rx="6" fill="#08132e" stroke="#22407f"/>',
            f'<rect x="{sx}" y="{y-6}" width="{max(sw * p / 100, 12):.1f}" height="12" rx="6" fill="{colour}"/>',
            f'<text x="{W-PAD}" y="{y+5}" fill="#dfe8ff" font-size="14" font-weight="600" text-anchor="end">{p}%</text>',
        ]
    out += ["</g>", "</svg>", ""]
    return "\n".join(out)


def bar_svg() -> str:
    """The slim bar for the very top of the README: title, one bar with 10 % ticks, a gold ring at the fill's end."""
    pct = overall()
    w, h = 900, 64
    bx, bw, by, bh = 190, 590, 24, 16
    fx = bx + bw * pct / 100
    cy = by + bh / 2
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" '
        f'role="img" aria-label="Road to v1.0: {pct}% done">',
        f"<title>Road to v1.0: {pct}% done</title>",
        "<defs>",
        GRADIENTS,
        f'<clipPath id="c"><rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="8"/></clipPath>',
        "</defs>",
        f'<rect width="{w}" height="{h}" rx="12" fill="url(#bg)"/>',
        f'<rect x="0.5" y="0.5" width="{w-1}" height="{h-1}" rx="12" fill="none" stroke="#2a4a99"/>',
        f'<g font-family="{FONT}">',
        '<text x="22" y="30" fill="#ffffff" font-size="17" font-weight="700" letter-spacing="1.2">ROAD TO v1.0</text>',
        f'<text x="22" y="48" fill="#8fa6d9" font-size="11.5">{esc(VERSION)} · updated {esc(UPDATED)}</text>',
        f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="8" fill="#08132e" stroke="#2a4a99"/>',
        '<g clip-path="url(#c)">',
        f'<rect x="{bx}" y="{by}" width="{fx - bx:.1f}" height="{bh}" fill="url(#fill)"/>',
    ]
    for t in range(1, 10):
        out.append(f'<rect x="{bx + bw * t / 10:.1f}" y="{by}" width="1.5" height="{bh}" fill="#08132e" opacity="0.55"/>')
    out += [
        f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" fill="url(#shine)"/>',
        "</g>",
        f'<circle cx="{fx:.1f}" cy="{cy}" r="11" fill="#0b1a3f" stroke="#ffd23f" stroke-width="4.5"/>',
        f'<circle cx="{fx:.1f}" cy="{cy}" r="11" fill="none" stroke="#fff6c8" stroke-width="1.2" opacity="0.8"/>',
        f'<text x="{w-22}" y="44" fill="#ffd23f" font-size="30" font-weight="800" text-anchor="end">{pct}%</text>',
        "</g>",
        "</svg>",
        "",
    ]
    return "\n".join(out)


def badge_json() -> str:
    pct = overall()
    colour = "brightgreen" if pct >= 90 else ("0aa5e8" if pct >= 60 else "orange")
    return json.dumps({"schemaVersion": 1, "label": "Road to v1.0", "message": f"{pct}%", "color": colour}) + "\n"


def readme(text: str) -> str:
    pct = overall()
    text = re.sub(r"Road to v1\.0: \d+% done", f"Road to v1.0: {pct}% done", text)
    for label, _w, p in AREAS:
        text = re.sub(r"^(\| " + re.escape(label) + r" \| )\d+ %", lambda m, p=p: f"{m.group(1)}{p} %", text, flags=re.M)
    return text


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    files = {
        root / "docs" / "images" / "progress.svg": svg(),
        root / "docs" / "images" / "progress-bar.svg": bar_svg(),
        root / "docs" / "progress.json": badge_json(),
    }
    rd = root / "README.md"
    files[rd] = readme(rd.read_bytes().decode("utf-8").replace("\r\n", "\n"))
    stale = []
    for path, content in files.items():
        raw = path.read_bytes().decode("utf-8") if path.exists() else None
        # compare without line endings (a Windows checkout has CRLF, CI has LF); keep the file's own style
        if raw is None or raw.replace("\r\n", "\n") != content:
            stale.append(path.relative_to(root).as_posix())
            if "--check" not in sys.argv:
                crlf = raw is not None and "\r\n" in raw
                path.write_bytes((content.replace("\n", "\r\n") if crlf else content).encode("utf-8"))
    if "--check" in sys.argv and stale:
        sys.exit("out of date (run python tools/make_progress.py): " + ", ".join(stale))
    print(f"progress {overall()}% overall; " + ("updated " + ", ".join(stale) if stale else "everything up to date"))


if __name__ == "__main__":
    main()
