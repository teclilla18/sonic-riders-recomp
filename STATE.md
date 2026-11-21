# State (2026-11-21): native Online Beta 1 candidate

This branch is the separate native Switch 2 candidate, based on the public Alpha 3 tag 67710d8.
It is not merged to main and has not been released.

## Implemented

- Shared Online Beta 1 LAN lobby, chosen player names and NET_COMPAT 1/game protocol 5.
- Black, silent startup screen until Mode Select; two players on the same home network.
- Dedicated online startup (SR_ONLINE_DEFAULT), including loaders that do not pass argv.
- Controller keyboard for names; open this dedicated NRO rather than relying on native relaunch support.
- Deterministic read-ahead, two 256 KiB windows, helper core 2; tester bug form.
- Native build passes with the pinned 20261117 SDK. Online app title is Riders Plus Online Beta 1. Latest recorded build b909ac94a9dc.

## Tested and limits

Shared code on PC: discovered-lobby race finish, all compared checksums equal through 24,600.
PC/OLED and PC/Android race segments and Android process restart tested on the main-based candidate.
Those tests do not validate this native binary. Online NRO/sidecar staged and installed via FTP with readback verification; normal pair/configuration preserved.
No native gameplay or cold package test yet.
Music loader is absent. Texture-pack first-use stutter and #35 remain known issues.

## Next

Connect native Switch 2 through OunceMTP (preferred) or explicit FTP fallback. Back up the installed NRO
and manifest, stage/readback/hash, preserve normal application/settings/saves/caches, keep rollback.
Test name entry, host/join by list, loading screen and a cross-platform race with matching checksums.
Cold Windows Sandbox needs permission. Release/tag/assets, issue posts and Discussions need explicit OK.
Implementation/test notes: docs/notes/online-beta.md. Main-based beta is on codex/online-beta.
