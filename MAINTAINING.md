# Maintaining the Riders Rebuilt website (gh-pages branch)

For Claude Code, Codex and people. Site: https://teclilla18.github.io/sonic-riders-recomp/
The project is called **Riders Rebuilt**; the gameplay changes are the "Riders Rebuilt rules".
The repository and URL keep the name `sonic-riders-recomp`.

## Adding a social media account (no tools needed)

All social links (home page, community page, footer) come from **`socials.json`** at the branch root. Edit it
on GitHub's website: open https://github.com/teclilla18/sonic-riders-recomp/edit/gh-pages/socials.json, add a
block like the ones already there (comma between blocks), press **Commit changes**. The live site shows it within
a minute or two. A typo leaves the previous links on the site, so nothing breaks; the next
`py -3 tools/build_site.py build` also refreshes the no-JavaScript copy (and reports a broken file).

```json
  {
    "name": "YouTube",
    "handle": "@channel-handle",
    "url": "https://www.youtube.com/@channel-handle",
    "about": "Trailers and gameplay videos."
  }
```

`url` must start with `https://`. Order in the file = order on the site.

## What lives where

- This branch (`gh-pages`) holds only the website. It shares no history with `main`; never merge it either way.
  Work in its own worktree (`git worktree add .local/gh-pages gh-pages` from the main checkout).
- Built pages sit at the branch root (`index.html`, `download.html`, ...), because GitHub Pages serves the root
  as it is. **Edit `src/`, never the built HTML**, then rebuild.
  - `src/partials/` head, header (navigation), footer: shared by every page.
  - `src/pages/*.html` one file per page. The first line is a comment with `title`, `nav` (which menu item is
    highlighted) and `description`. Placeholders: `{{platforms}}` (download cards), `{{releases}}` (stable and
    experimental cards), `{{progress}}` (Road to v1.0), `{{overall}}`, `{{stable_tag}}`, `{{downloads}}`,
    `{{file:<platform id>}}` (current ZIP name). An unknown placeholder stops the build.
  - `src/vendor/console_guide_data.py` a copy of main's booklet text; `install-switch.html` and
    `install-switch2.html` are rendered from it (same URLs as the old booklets, which release notes link to).
- `assets/css/site.css` and `assets/js/site.js`: the whole look and all behaviour. No framework, no build step.
- `assets/data/snapshot.json`: releases and Road to v1.0 numbers at the last sync (the no-JavaScript fallback).
- `tools/build_site.py` (sync/build), `tools/preview.py` (local server), `tools/check_links.py`,
  `tools/check_site.py` (guard), `tools/approved_images.txt` (every allowed picture with its SHA-1).
- `install-guide.html`, `update-guide.html`: redirects from the old easy-read guide URLs to `install.html` / `update.html`.

## What updates by itself

In the visitor's browser, `site.js` reads public GitHub data (cached 20 minutes, falls back to the page's own
snapshot if GitHub is unreachable or rate-limited):

- release names, dates, download counts and the direct ZIP links for every platform (GitHub releases API);
- Road to v1.0: the AREAS list in main's `tools/make_progress.py` and the "Left for v1.0" column of main's README.

So a new release, a changed progress number or new ZIP names show up without touching this branch, as long as
the ZIP names keep their patterns (`SonicRiders-<Platform>-Setup-*.zip`, `*Switch2*.zip`; the patterns are in
`PLATFORMS` in `tools/build_site.py` and `FILES` in `site.js`; change both together).

## Routine update (after a release, or when the booklets or pictures change on main)

```sh
py -3 tools/build_site.py all     # sync from origin/main + GitHub, then build
py -3 tools/check_links.py        # add --external to also test outside links
py -3 tools/check_site.py         # the guard (the pre-commit hook runs it too)
py -3 tools/preview.py            # http://localhost:8018/sonic-riders-recomp/
```

Look at the changed pages in the preview (phone and desktop width), then commit and push `gh-pages`
(after the maintainer's OK for anything visible). Pages redeploys within a minute or two.

Text that is still written by hand and may need a touch after big changes: the Riders Rebuilt rules page
(`src/pages/riders-rebuilt.html`), FAQ and troubleshooting (`faq.html`), the easy-read guides (`install.html`,
`update.html`; keep them in step with main's `docs/INSTALL.md`), credits (`community.html`).

## Brand pictures and the background

- The site uses the maintainer-supplied Painterly Riders Rebuilt logo in `assets/img/logo.webp` and
  `assets/img/logo-sm.webp`. The site icons use the existing placeholders.
- The supplied logo is the sole exception to the ban on AI-generated imagery. Use commissioned art,
  online art with the artist credited, in-game screenshots, or screenshots edited by the maintainer.
  Add maintainer-approved images to `tools/approved_images.txt`.
- The background grid is the maintainer's picture, redrawn sharp by `tools/bg/`: `analyse.py <picture>` measures
  every element (grid, crossings, sparkles, streaks, squares) into `elements.json`; `render.py <picture> <scale> <out>
  [x0,y0,x1,y1]` redraws it at any scale (the optional box drops streaks crossing a repeating tile's edges).
  Site files: `grid-portrait.jpg` (whole picture at 2x, phones) and `grid-tile.jpg` (2x, crop 49,340 to 647,1204
  of the original, repeats on wide screens; CSS keeps it at 598 x 864 CSS px).

## Rules

- **No game data, ever.** Only pictures already approved on main (synced with a SHA-1 check against main's
  `OWN_ART`) or approved by the maintainer may be added; list each in `tools/approved_images.txt`.
  Never bypass the pre-commit hook. No DX (Extreme Gear Labs) material, no APKs or game builds.
- No personal details (home paths, e-mail addresses, console setups) in any file.
- Keep the Credits entries for the Switch 2 release banner and the game icon by cedry2kio. Keep the unofficial fan-project disclaimer in the footer and on `community.html#legal`.
- Experimental builds are always orange and labelled "experimental alpha"; stable ones green "Stable".
- External loads include Google Fonts and GitHub's public API. Google Analytics loads only after visitors opt in.
  Its measurement ID and consent handling live in `assets/js/analytics.js`. The privacy details and preference
  control are at `community.html#privacy`.
- Accessibility: keep alt text on every picture, real buttons for controls, visible focus, reduced-motion support.

## Testing in Chrome

The test PC's screen is portrait 1080 x 1920, so the browser can't be made 1440 wide. To see desktop and phone
layouts, open any page of the preview and replace it with iframes, for example:

```js
document.body.innerHTML = '<iframe src="/sonic-riders-recomp/" style="width:1440px;height:3200px;border:0;transform:scale(.48);transform-origin:0 0"></iframe>'
```

(and `width:390px` for a phone). Check `document.documentElement.scrollWidth` equals the frame width
(no sideways scroll).

## Deployment and rollback

GitHub Settings > Pages > Build and deployment > Source: **Deploy from a branch**, Branch: **gh-pages**, folder
**/ (root)**. Before 2026-11 the site was served from `main` `/docs` (the two console booklets); to roll back,
set the branch back to `main` and `/docs`. Same with the CLI (maintainer's OK needed):

```sh
gh api -X PUT repos/teclilla18/sonic-riders-recomp/pages -f "source[branch]=gh-pages" -f "source[path]=/"
gh api -X PUT repos/teclilla18/sonic-riders-recomp/pages -f "source[branch]=main" -f "source[path]=/docs"   # rollback
```

Main still generates its own copies of the booklets in `docs/`; they stay valid on GitHub but are no longer
served by Pages once the source is `gh-pages`.
