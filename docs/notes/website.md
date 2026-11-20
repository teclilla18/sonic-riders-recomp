# Website (Riders Plus, GitHub Pages)

Live: https://teclilla18.github.io/sonic-riders-recomp/ (since 2026-11-19). The project's public name is
**Riders Plus**; the gameplay changes are the "Riders Plus rules". Repository and URL keep `sonic-riders-recomp`.

- Source: branch **`gh-pages`** (orphan, website only; never merge with main). Worktree: `.local/gh-pages`.
  GitHub Pages serves `gh-pages` `/` (before 2026-11-19: `main` `/docs`). Full procedure, rules and rollback:
  `MAINTAINING.md` on that branch.
- Static HTML/CSS/JS, no framework. Edit `src/`, then `py -3 tools/build_site.py all` (sync from origin/main +
  GitHub, build), `tools/check_links.py`, `tools/check_site.py` (the branch's own game-data/privacy guard; its
  pre-commit hook runs through the shared `core.hooksPath tools/git-hooks`), `tools/preview.py`
  (http://localhost:8018/sonic-riders-recomp/).
- Updates itself in the browser: releases, download counts and direct ZIP links (GitHub API), Road to v1.0
  (`tools/make_progress.py` AREAS + this README's "Left for v1.0" column). Fallback: snapshot made by `sync`.
- The console booklets keep their URLs (`install-switch.html`, `install-switch2.html`, linked from releases and
  the README) and are rendered from **main's `tools/console_guide_data.py`**: after changing that file here, run
  `build_site.py all` on gh-pages and push. `make_console_guides.py` still writes `docs/*.html` on main; those are
  no longer served by Pages. Old `install-guide.html` / `update-guide.html` redirect to `install.html` / `update.html`.
- Pictures: only ones approved here (OWN_ART SHA-1, checked by `sync`) or by the maintainer, listed in
  `tools/approved_images.txt` on gh-pages. Brand: logo + round icon (maintainer's files); background = the
  maintainer's grid picture redrawn sharp by `tools/bg/` (analyse.py measures every element, render.py redraws
  at any scale).
- Social links come from `socials.json` on gh-pages (editable on GitHub's website, no build). Public contact:
  ridersplusgame@gmail.com, Bluesky @ridersplus.bsky.social, Instagram @ridersplusgame.
