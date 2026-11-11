#!/usr/bin/env python3
"""Draw the README's "Road to v1.0" progress card (docs/images/progress.svg).

Edit AREAS (and VERSION / UPDATED) below, then run:  python tools/make_progress.py
The overall percentage is the weighted average of the areas. The README's "Road to v1.0" table explains
what each area still needs; keep the two in step.
"""
from pathlib import Path

VERSION = "v0.8.0"
UPDATED = "2026-11-11"

# (label, weight, percent done)
AREAS = [
    ("Whole game playable",            25, 90),
    ("Graphics match the GameCube",    15, 80),
    ("Switch performance",             15, 65),
    ("Platforms",                      15, 85),
    ("Online play",                    15, 30),
    ("Features and polish",            15, 60),
]

W = 720
PAD = 28
ROW_H = 30
TOP = 128
FONT = "'Segoe UI', -apple-system, 'Helvetica Neue', Arial, sans-serif"


def overall() -> int:
    total = sum(w for _, w, _ in AREAS)
    return round(sum(w * p for _, w, p in AREAS) / total)


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def svg() -> str:
    pct = overall()
    h = TOP + len(AREAS) * ROW_H + 26
    bar_x, bar_w, bar_y, bar_h = PAD, W - 2 * PAD, 74, 22
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{h}" viewBox="0 0 {W} {h}" '
        f'role="img" aria-label="Road to v1.0: {pct}% done">',
        f"<title>Road to v1.0: {pct}% done</title>",
        "<defs>",
        '<linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">'
        '<stop offset="0" stop-color="#0b1a3f"/><stop offset="1" stop-color="#132c66"/></linearGradient>',
        '<linearGradient id="fill" x1="0" y1="0" x2="1" y2="0">'
        '<stop offset="0" stop-color="#1e5bff"/><stop offset="0.6" stop-color="#2fb8ff"/>'
        '<stop offset="1" stop-color="#ffd23f"/></linearGradient>',
        '<linearGradient id="shine" x1="0" y1="0" x2="0" y2="1">'
        '<stop offset="0" stop-color="#fff" stop-opacity="0.35"/>'
        '<stop offset="0.5" stop-color="#fff" stop-opacity="0"/></linearGradient>',
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


def main() -> None:
    path = Path(__file__).resolve().parent.parent / "docs" / "images" / "progress.svg"
    path.write_text(svg(), encoding="utf-8", newline="\n")
    print(f"{path.name}: {overall()}% overall")


if __name__ == "__main__":
    main()
