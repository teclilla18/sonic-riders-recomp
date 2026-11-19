# Illustrated console installation booklets

Player entry: [choose a console](../index.md). Separate routes:
[Switch Stable](../install-switch.md) and [Switch 2 Experimental Alpha](../install-switch2.md).
Rendered HTML is served through the repository's GitHub Pages site from `main`, `/docs`.
`docs/.nojekyll` keeps these self-contained static pages unchanged.

The primary visual reference is Claude's existing easy-read install/update guide. Retain its warm paper
background, Atkinson Hyperlegible/Lexend typography, original color key, friendly language, numbered
steps and checkpoints. Do not turn these pages into technical reference manuals. The older technical
installation references remain available separately.

## Editing and verification

- Edit `tools/console_guide_data.py`; run `python tools/make_console_guides.py` to regenerate both HTML
  and Markdown together, the chooser and labelled SVG diagrams.
- Run `python tools/check_console_guides.py`: local links/anchors/images, alternative text, size/XML,
  checkpoints, privacy and platform separation. Check phone layouts and actual picture readability too.
- `docs/images/installation/fonts.css` embeds licensed font subsets; the generator embeds this CSS in
  each HTML page. The font license notices stay alongside it. No runtime CDN or external script is used.
- Screenshots are independent reviewed assets. Capture the actual current published executables;
  redact private information, add precise annotations, and update only their exact approved hashes in
  the existing game-data guard. Never invent success screens or claim a diagram is a hardware capture.
- Preserve separately the current release ZIP names, installer button labels, platform paths and update
  rules. The original Switch output folder is `build/switch`; native Switch 2's is
  `build/switch2/sd/switch/sonicriders-switch2`. Open Output Folder opens the local PC output, not the card.
- A new package needs a fresh source/download check and installer walkthrough. A successful build or
  disposable SD-folder test does not establish physical console compatibility or child-user acceptance.

Exact package hashes, primary sources, picture provenance, issue status and validation limits:
[guide-verification.md](../guide-verification.md). Local evidence and deployment/ownership pointers stay
in the excluded canonical handoff, not in tracked notes.
