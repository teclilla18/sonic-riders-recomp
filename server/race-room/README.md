# Race Room server

The live chat and race lobbies behind the website's Race Room page (`race-room.html` on the `gh-pages` branch).
It is a Cloudflare Worker with one Durable Object (`RaceRoom`, SQLite storage), on Cloudflare's free plan.

- `worker.js`: the server (chat, names, GIF search, lobbies, Game Dev tools).
- `filter.js`: link filter, text cleaning and name rules. The page has a small copy of `hasLink()` only to grey out
  Send early; this server is the one that refuses links.
- `test_filter.mjs`: `node test_filter.mjs` checks the filter and name rules.
- `wrangler.json`: Worker name `riders-rebuilt-race-room`, Durable Object binding `RACE_ROOM`.

What it keeps: public chat (last 100 messages, 30 minutes), lobbies (30 minutes, 60 messages each), chosen names
(a week; unnamed riders a day), mutes (until "Unmute everyone"). A rider is a random browser ID; IP addresses are
only kept hashed, for rate limits and mutes.

## Secrets (never in this folder)

- `DEV_KEY`: the Game Dev's key. With it, a browser shows as **Game Dev** (gold) and gets Delete / Mute buttons.
  Sign in once per browser: open `race-room.html#dev` and paste the key.
- `GIPHY_KEY`: a GIPHY API key (developers.giphy.com, "Create an App", choose API). Without it the GIF button hides.

Set or change them with `npx wrangler@4 secret put DEV_KEY` (and `GIPHY_KEY`); wrangler asks for the value.

## Test locally

```
npx wrangler@4 dev --port 8787 --var DEV_KEY:<any test key>
```

Then, from the `gh-pages` worktree, `py -3 tools/preview.py` and open
`http://localhost:8018/sonic-riders-recomp/race-room.html?api=http://localhost:8787` (the `?api=` override only
works on localhost). Add `--var GIPHY_KEY:<key>` to try GIFs.

## Deploy

```
npx wrangler@4 login      # once per PC, opens the browser
npx wrangler@4 deploy     # prints https://riders-rebuilt-race-room.<account>.workers.dev
```

The page reads the server address from `data-api` on `#race-room` in `src/pages/race-room.html` (gh-pages).
Allowed page origins are `ORIGINS` at the top of `worker.js`.

Logs: `npx wrangler@4 tail` while it runs, or the Cloudflare dashboard (observability is on).

## Free plan limits

Each open page asks for news every 4 s (8 s when quiet, none while the tab is hidden). The free plan allows
100,000 requests a day, about 110 hours of open tabs a day. If that runs out, raise the delays in `schedule()` in
the page's `assets/js/race-room.js`.
