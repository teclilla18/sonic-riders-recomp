#!/bin/sh
# Sonic Riders setup for Linux and the Steam Deck: builds the PC version from your own disc image.
# Double-click it (it opens a terminal), or run ./SonicRidersSetup.sh [--iso IMAGE] in a terminal.
# Nothing is installed into the system and no root rights are needed. Details: tools/setup_linux.py.
cd "$(dirname "$0")" || exit 1

# started from a file manager (no terminal): show the progress in one
if [ ! -t 1 ] && [ -z "$SR_SETUP_TERMINAL" ]; then
    export SR_SETUP_TERMINAL=1
    for t in konsole gnome-terminal kgx xfce4-terminal x-terminal-emulator xterm; do
        command -v "$t" >/dev/null 2>&1 || continue
        case "$t" in
            gnome-terminal|kgx) exec "$t" -- "$0" "$@" ;;
            *) exec "$t" -e "$0" "$@" ;;
        esac
    done
fi

if command -v python3 >/dev/null 2>&1; then
    python3 -I tools/setup_linux.py "$@"
    rc=$?
else
    echo "SETUP FAILED: python3 was not found. Install Python 3 (SteamOS has it) and run this again."
    rc=1
fi
if [ -n "$SR_SETUP_TERMINAL" ]; then
    printf '\nPress Enter to close this window.'
    read -r _
fi
exit $rc
