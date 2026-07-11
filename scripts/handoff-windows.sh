#!/bin/bash
# Milestone 2: hand off to Windows via a one-time UEFI boot entry, then reboot.
# Default is DRY-RUN (prints what it would do). Pass --run to actually reboot.
#   sudo scripts/handoff-windows.sh          # dry run
#   sudo scripts/handoff-windows.sh --run    # do it
set -euo pipefail

win=$(efibootmgr | grep -i "Windows Boot Manager" \
        | grep -oE "Boot[0-9A-Fa-f]{4}" | head -1 | sed 's/Boot//')
if [[ -z "${win:-}" ]]; then
    echo "ERROR: 'Windows Boot Manager' not found in efibootmgr output." >&2
    exit 1
fi
echo "Windows Boot Manager is Boot${win}"

if [[ "${1:-}" != "--run" ]]; then
    echo "(dry-run) would run: efibootmgr -n ${win} && systemctl reboot"
    exit 0
fi
efibootmgr -n "${win}"     # BootNext = Windows, one time only
systemctl reboot
