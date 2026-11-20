# Local Switch 2 deployment through OunceMTP

Local-only tooling, not a published installer. OunceMTP must be running with the
game closed and USB connected. Windows exposes **Nintendo Switch 2 / SD Card**
through Windows Portable Devices (WPD), not a drive letter.

## Evidence and limits (2026-11-18)

- Read-only Shell COM and WPD device/directory enumeration succeeded. Shell's
  `FolderItem.Size` returned zero; WPD returned correct file sizes.
- A 65,537-byte Shell COM roundtrip and 33,554,449-byte synchronous WPD roundtrip
  matched SHA-256. WPD rename, delete and refusal to overwrite passed. The final
  Python wrapper's 1,048,593-byte probe also passed; temporary files were removed.
- Installed NRO and manifest were each downloaded twice and matched their known
  installed hashes. In-game screenshots and a log were retrieved read-only.
- The user then authorized deployment. Native balance build `7a05aa6f5f02` was installed with both stages
  and installed NRO/manifest SHA-256 readbacks verified, old pair retained locally/on SD, and config/env/cache
  byte-identical. Unlimited remained enabled; saves/unrelated entries were preserved. Actual rollback was
  planned/validated without applying; ten fake-transport tests include rollback, corruption, changed installed
  hash and disconnect after retaining the old file. Exact hashes/paths are in excluded AI_HANDOFF.md.
- FTP remains an explicit fallback. Its adapter is implemented but was not
  exercised in this session. Never switch protocols during a partial transaction.

## Build and read-only use

Requires Windows, native Python 3.11+, MSVC Build Tools/Windows SDK and LLVM
(`tools/host/env.ps1` locates the existing SDK). All outputs stay under ignored
`build/`; never commit or publish console files, logs, screenshots or builds.

```powershell
tools/build_switch2_wpd.ps1
python tools/switch2_deploy.py devices
python tools/switch2_deploy.py list /switch/sonicriders-switch2
python tools/switch2_deploy.py get /switch/sonicriders-switch2/sonicriders.log build/returned.log
python tools/switch2_deploy.py backup --output build/mtp/backup-unique
```

`get` refuses an existing local file. Backup requires a fresh output directory,
downloads only `sonicriders-switch2.nro` and `build-info.json`, reads each twice,
checks byte counts and SHA-256, and records `backup.json`. It does not write to SD.
The helper rejects ambiguous device/path matches and defaults to read-only opens.
Use `--device <exact-WPD-id>` before the command only if needed to disambiguate.

## Temporary bidirectional probe

After authorization for temporary writes:

```powershell
python tools/switch2_deploy.py probe --output build/mtp/probe-unique
```

This writes one random `.codex-mtp-probe-<uuid>.bin` in `/switch`, downloads and
hashes it, renames it, downloads/hashes again, and removes only its own exact
temporary names. It confirms cleanup by enumeration. No wildcard cleanup occurs.
OunceMTP supports renaming `WPD_OBJECT_ORIGINAL_FILE_NAME`; do not additionally
set `WPD_OBJECT_NAME`, which can report unsupported after the filename changed.

## Deployment within the user's authorization

The user's standing connected-device development/test deployment permission is recorded in the excluded
local AGENTS.md and AI_HANDOFF.md. It permits agents to apply a prepared test build without asking again,
with verified backups, preservation, rollback and installation notification. Public release publication and
public release-asset replacement still require explicit approval. Per-task restrictions take precedence.

Default `deploy` validates the native NRO0/OUN0 header, embedded source identity,
manifest and NRO `.sha256` sidecar, enumerates installed names and prints a plan.
It creates no backup/output directory and makes no SD writes without `--apply`.

```powershell
python tools/switch2_deploy.py deploy --nro build/switch2/sonicriders-switch2.nro --manifest build/switch2/build-info.json --output build/mtp/deploy-unique
# Within the user's deployment authorization, repeat with:
# --apply --expected-installed-sha256 <current-installed-NRO-SHA256>
```

Apply first makes a verified local backup and checks the supplied installed hash.
It stages BOTH new files with unique names, verifies each through full download
and SHA-256, and rechecks the installed pair before promotion. It retains each old
SD file as `<name>.before-mtp-<uuid>`, promotes the new NRO then manifest, and
downloads/hashes the installed pair. `deployment.json` records each completed
operation and `complete: true` only after both installed readbacks pass.

Settings, saves, caches, env, disc data and all unrelated files are outside the
write set. No folder operations or cleanup of old backups is performed.

**Disconnect/failure recovery:** stop and inspect the local journal and read-only
SD listing/readbacks. A rename can succeed before its reply reaches Windows;
do not assume the last unrecorded operation failed. Old bytes remain locally and,
after retention, on SD. Do not launch with a partially promoted pair, rerun blindly,
delete stages or guess a rollback. Reconcile the exact hashes/names with the
handoff, then repair within the user's device-deployment authorization.
Promotion of two files is not atomic, and automatic rollback is intentionally absent.

Rollback uses the local verified backup, checks both stored hashes, and runs the same staged/verified
transaction, preserving a backup of the build being rolled back. The backup command also creates the
NRO hash sidecar needed by validation. First plan, then apply with the currently installed NRO hash:

```powershell
python tools/switch2_deploy.py rollback --backup build/mtp/deploy-previous --output build/mtp/rollback-unique
# Add --apply --expected-installed-sha256 <current-installed-NRO-SHA256> to perform it.
```

FTP uses the same backup/stage/readback workflow via explicit
`--transport ftp --host <address> --port <port>` before the command. Anonymous
FTP and MLSD enumeration are required; failure stops rather than weakening checks.
The current device address belongs only in the excluded local handoff.

## Regression commands

```powershell
python tests/test_switch2_deploy.py
```

The backend uses Microsoft's [WPD content/resources APIs](https://learn.microsoft.com/en-us/windows/win32/api/portabledeviceapi/nn-portabledeviceapi-iportabledeviceresources)
and synchronous streams. Device-side USB service: [OunceMTP](https://github.com/venustrikes/ouncemtp).
