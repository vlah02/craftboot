#!/bin/bash
# Optional: make the firmware "Ubuntu" entry boot Ubuntu DIRECTLY via a signed
# UKI (Ubuntu's real kernel + Ubuntu's real initramfs), with no GRUB menu in
# the path at all. Reuses craftboot's existing MOK key (does NOT genkey) —
# run efi-install.sh first, which generates the key and walks you through
# MOK enrollment.
#
#   sudo ./dist/ubuntu/ubuntu-direct.sh install      redirect "Ubuntu" to boot direct
#   sudo ./dist/ubuntu/ubuntu-direct.sh --uninstall  put the stock shim->GRUB entry back
#
# End state: three independent firmware entries — Windows, Ubuntu (direct,
# via this script), Craftboot (the graphical menu). GRUB is left installed
# but DORMANT: no firmware entry points at it anymore. It is not purged, and
# can be reached again with the break-glass command printed after install.
#
# This does NOT touch Boot0000 (Windows) or the Craftboot entry.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
KEYDIR="/var/lib/craftboot"
ESPDIR="/boot/efi/EFI/ubuntudirect"
UBUNTU_ESP="/boot/efi/EFI/ubuntu"
HOOK="/etc/kernel/postinst.d/zz-ubuntu-direct"
LABEL="Ubuntu"

[[ $EUID -eq 0 ]] || { echo "Please run with sudo."; exit 1; }

# Build+sign the direct-boot UKI for kernel version $1 (default: running).
# Split out so the kernel post-install hook can call just this step.
build() {
    local KVER="${1:-$(uname -r)}"
    local KERNEL="/boot/vmlinuz-$KVER"
    local INITRD="/boot/initrd.img-$KVER"

    [[ -f "$KEYDIR/MOK.key" ]] || { echo "No MOK key in $KEYDIR — run: sudo $HERE/efi-install.sh"; exit 1; }
    [[ -f "$KEYDIR/MOK.crt" ]] || { echo "No MOK cert in $KEYDIR — run: sudo $HERE/efi-install.sh"; exit 1; }
    [[ -f "$KERNEL" ]]  || { echo "No kernel at $KERNEL"; exit 1; }
    [[ -f "$INITRD" ]]  || { echo "No initramfs at $INITRD"; exit 1; }

    local UKIFY; UKIFY="$(command -v ukify 2>/dev/null || true)"
    [[ -x "$UKIFY" ]] || { [[ -x /usr/lib/systemd/ukify ]] && UKIFY=/usr/lib/systemd/ukify; }
    [[ -x "${UKIFY:-}" ]] || { echo "Missing ukify — run: sudo apt install systemd-ukify"; exit 1; }

    local uuid; uuid="$(findmnt -no UUID / 2>/dev/null || true)"
    [[ -n "$uuid" ]] || { echo "Could not resolve the root filesystem UUID (findmnt -no UUID /)."; exit 1; }
    local CMDLINE="root=UUID=$uuid ro quiet splash"

    # 'ukify build ...' on modern systemd; older versions take no subcommand
    local SUB=(build); "$UKIFY" build --help >/dev/null 2>&1 || SUB=()

    mkdir -p "$ESPDIR"
    "$UKIFY" "${SUB[@]}" \
        --linux="$KERNEL" \
        --initrd="$INITRD" \
        --cmdline="$CMDLINE" \
        --secureboot-private-key="$KEYDIR/MOK.key" \
        --secureboot-certificate="$KEYDIR/MOK.crt" \
        --output="$ESPDIR/grubx64.efi"

    echo "Signed direct-boot UKI -> $ESPDIR/grubx64.efi ($(du -h "$ESPDIR/grubx64.efi" | cut -f1)) for kernel $KVER (root UUID $uuid)"
}

# Print "<disk> <part>" for the ESP (e.g. "/dev/nvme0n1 2"), as efibootmgr wants them.
esp_disk_part() {
    local esp disk part
    esp="$(findmnt -no SOURCE /boot/efi)"
    disk="/dev/$(lsblk -no pkname "$esp")"
    part="$(cat "/sys/class/block/$(basename "$esp")/partition")"
    echo "$disk $part"
}

# Find the current "Ubuntu" firmware entry's Boot#### number (exact label match).
current_ubuntu_bootnum() {
    efibootmgr | awk '{
        if (match($0, /^Boot[0-9A-Fa-f]{4}\*? /)) {
            num = substr($0, 5, 4)
            rest = substr($0, RLENGTH + 1)
            sub(/\t.*/, "", rest)
            if (rest == "Ubuntu") { print num; exit }
        }
    }'
}

install() {
    [[ -f "$KEYDIR/MOK.key" ]] || { echo "No MOK key — run: sudo $HERE/efi-install.sh first."; exit 1; }
    [[ -f "$KEYDIR/MOK.der" ]] || { echo "No MOK.der in $KEYDIR — run: sudo $HERE/efi-install.sh first."; exit 1; }
    # capture first (mokutil exits non-zero even when enrolled; pipefail would
    # otherwise poison the pipeline and give a false "not enrolled")
    mok_status="$(mokutil --test-key "$KEYDIR/MOK.der" 2>&1 || true)"
    if ! printf '%s' "$mok_status" | grep -qi "already enrolled"; then
        echo "The Craftboot MOK is NOT enrolled yet (checked with mokutil --test-key)."
        echo "Run 'sudo $HERE/efi-install.sh' + the MOK Manager enrollment reboot first."
        exit 1
    fi
    command -v efibootmgr >/dev/null 2>&1 || { echo "Missing efibootmgr — run: sudo apt install efibootmgr"; exit 1; }
    [[ -f "$UBUNTU_ESP/shimx64.efi" ]] || { echo "No shim at $UBUNTU_ESP/shimx64.efi — is this a stock Ubuntu Secure Boot install?"; exit 1; }

    local old_bootnum; old_bootnum="$(current_ubuntu_bootnum || true)"
    [[ -n "$old_bootnum" ]] || { echo "No firmware entry labeled exactly 'Ubuntu' found (efibootmgr). Aborting — nothing to redirect."; exit 1; }
    echo "Found existing 'Ubuntu' entry: Boot$old_bootnum"
    efibootmgr | grep -E "^Boot$old_bootnum" || true

    build "$(uname -r)"

    # shim (Microsoft-signed) verifies our MOK-signed UKI (named grubx64.efi)
    cp -f "$UBUNTU_ESP/shimx64.efi" "$ESPDIR/shimx64.efi"
    cp -f "$UBUNTU_ESP/mmx64.efi"   "$ESPDIR/mmx64.efi" 2>/dev/null || true

    local disk part
    read -r disk part <<<"$(esp_disk_part)"
    [[ -n "$disk" && "$disk" != "/dev/" && -n "$part" ]] || { echo "Could not derive the ESP disk/partition for efibootmgr."; exit 1; }

    # Delete the old shim->GRUB "Ubuntu" entry, then create the new direct one
    # under the SAME label so craftboot's BootNext match ("Ubuntu") keeps working.
    efibootmgr -b "$old_bootnum" -B >/dev/null
    if ! efibootmgr --create --disk "$disk" --part "$part" \
        --label "$LABEL" --loader '\EFI\ubuntudirect\shimx64.efi' >/dev/null; then
        echo "ERROR: removed the old 'Ubuntu' entry (Boot$old_bootnum) but failed to create the new one." >&2
        echo "Your firmware currently has NO 'Ubuntu' entry. Recreate the stock one now:" >&2
        echo "  sudo efibootmgr --create --disk $disk --part $part --label \"Ubuntu\" --loader '\\EFI\\ubuntu\\shimx64.efi'" >&2
        exit 1
    fi

    local new_bootnum; new_bootnum="$(current_ubuntu_bootnum || true)"

    cat > "$HOOK" <<EOF
#!/bin/sh
# Rebuilds the direct-boot Ubuntu UKI whenever a new kernel is installed.
# Resilient: a failure here must never block apt/kernel installation.
ver="\$1"; [ -n "\$ver" ] || exit 0
if "$REPO/dist/ubuntu/ubuntu-direct.sh" --build-only "\$ver" >/dev/null 2>&1; then
    :
else
    echo "ubuntu-direct: UKI rebuild failed for \$ver (boot Craftboot -> Extras ->" >&2
    echo "  'Ubuntu (recovery mode)', then re-run: sudo $REPO/dist/ubuntu/ubuntu-direct.sh install" >&2
fi
exit 0
EOF
    chmod +x "$HOOK"

    echo
    echo "======================= INSTALLED ======================="
    echo "Old entry (removed): Boot$old_bootnum  Ubuntu  ->  \\EFI\\ubuntu\\shimx64.efi (shim + GRUB)"
    echo "New entry (active):  Boot${new_bootnum:-?}  Ubuntu  ->  \\EFI\\ubuntudirect\\shimx64.efi (shim + direct UKI)"
    efibootmgr | grep -i ubuntu || true
    echo
    echo "GRUB is left INSTALLED but DORMANT — no firmware entry points at it anymore."
    echo "It is not removed/purged; the ESP files under $UBUNTU_ESP are untouched."
    echo
    echo "If a kernel update ever breaks the direct UKI:"
    echo "  1) Boot Craftboot -> Extras -> \"Ubuntu (recovery mode)\" (kexec path, doesn't need this UKI)."
    echo "  2) Re-run: sudo $HERE/ubuntu-direct.sh install"
    echo
    echo "Break-glass — get the old GRUB menu back as its own entry, any time:"
    echo "  sudo efibootmgr --create --disk $disk --part $part --label \"Ubuntu (GRUB)\" --loader '\\EFI\\ubuntu\\shimx64.efi'"
    echo "========================================================="
}

uninstall() {
    local direct_bootnum; direct_bootnum="$(current_ubuntu_bootnum || true)"
    if [[ -n "$direct_bootnum" ]]; then
        efibootmgr -b "$direct_bootnum" -B >/dev/null 2>&1 || true
        echo "Removed direct-boot 'Ubuntu' entry: Boot$direct_bootnum"
    else
        echo "No 'Ubuntu' firmware entry found to remove."
    fi
    rm -rf "$ESPDIR" "$HOOK"

    if [[ -f "$UBUNTU_ESP/shimx64.efi" ]]; then
        local disk part
        read -r disk part <<<"$(esp_disk_part)"
        if [[ -n "$disk" && "$disk" != "/dev/" && -n "$part" ]] && \
           efibootmgr --create --disk "$disk" --part "$part" \
               --label "$LABEL" --loader '\EFI\ubuntu\shimx64.efi' >/dev/null; then
            echo "Recreated the stock 'Ubuntu' entry -> \\EFI\\ubuntu\\shimx64.efi (shim + GRUB)."
        else
            echo "WARNING: could not recreate the stock 'Ubuntu' entry automatically (disk='$disk' part='$part')." >&2
            echo "Recreate it manually: sudo efibootmgr --create --disk <disk> --part <part> --label \"Ubuntu\" --loader '\\EFI\\ubuntu\\shimx64.efi'" >&2
        fi
    else
        echo "WARNING: $UBUNTU_ESP/shimx64.efi not found — could not recreate the stock entry automatically."
        echo "Recreate it manually: sudo efibootmgr --create --disk <disk> --part <part> --label \"Ubuntu\" --loader '\\EFI\\ubuntu\\shimx64.efi'"
    fi
    echo "Removed $ESPDIR, the ubuntu-direct kernel hook, and its firmware entry."
    echo "(The MOK key is kept in $KEYDIR — craftboot's own entry still needs it.)"
}

case "${1:-}" in
    install)      install;;
    --uninstall)  uninstall;;
    --build-only) shift; build "${1:-}";;
    *) echo "usage: sudo $0 {install|--uninstall}"; exit 1;;
esac
