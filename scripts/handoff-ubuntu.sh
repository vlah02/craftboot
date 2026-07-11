#!/bin/bash
# Milestone 2: hand off to Ubuntu directly via kexec (no firmware reboot).
# Default is DRY-RUN. Pass --run to actually kexec.
#   sudo scripts/handoff-ubuntu.sh          # dry run
#   sudo scripts/handoff-ubuntu.sh --run    # do it
#
# NOTE: with Secure Boot ENABLED, kernel lockdown blocks classic kexec_load.
# We use `-s` (kexec_file_load), which accepts Ubuntu's Canonical-signed kernel.
# If this fails with EPERM/lockdown, Secure Boot must be handled (see README).
set -euo pipefail

KERNEL="${KERNEL:-/boot/vmlinuz}"
INITRD="${INITRD:-/boot/initrd.img}"
ROOT_UUID="${ROOT_UUID:-c36a4c56-487b-4aee-946b-f7fa2dc7f001}"
CMDLINE="${CMDLINE:-root=UUID=${ROOT_UUID} ro quiet splash}"

echo "kernel  = ${KERNEL}"
echo "initrd  = ${INITRD}"
echo "cmdline = ${CMDLINE}"

if [[ "${1:-}" != "--run" ]]; then
    echo "(dry-run) would run:"
    echo "  kexec -s -l ${KERNEL} --initrd=${INITRD} --command-line=\"${CMDLINE}\""
    echo "  systemctl kexec"
    exit 0
fi
kexec -s -l "${KERNEL}" --initrd="${INITRD}" --command-line="${CMDLINE}"
systemctl kexec
