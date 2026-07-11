#!/bin/bash
# Make craftboot its own firmware EFI boot entry (reorderable in the UEFI BIOS),
# Secure-Boot-compatible via shim + a MOK-signed UKI.
#
#   sudo ./boot/uki-setup.sh genkey     generate a signing key + start MOK enrollment
#   (reboot, choose "Enroll MOK" at the blue screen, enter the password, reboot)
#   sudo ./boot/uki-setup.sh install    build+sign the UKI, add the firmware entry + hook
#   sudo ./boot/uki-setup.sh --uninstall  remove the firmware entry + ESP files + hook
#
# Additive & safe: your existing GRUB/Ubuntu entry stays; if craftboot's entry
# fails, the firmware falls through to it (or reorder in BIOS).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
KEYDIR="/var/lib/craftboot"
ESPDIR="/boot/efi/EFI/craftboot"
UBUNTU_ESP="/boot/efi/EFI/ubuntu"
HOOK="/etc/kernel/postinst.d/zz-craftboot-uki"
CN="Craftboot Secure Boot"

[[ $EUID -eq 0 ]] || { echo "Please run with sudo."; exit 1; }

genkey() {
    mkdir -p "$KEYDIR"; chmod 700 "$KEYDIR"
    if [[ -f "$KEYDIR/MOK.key" ]]; then
        echo "MOK key already exists in $KEYDIR (reusing)."
    else
        openssl req -new -x509 -newkey rsa:2048 -nodes -days 3650 \
            -keyout "$KEYDIR/MOK.key" -out "$KEYDIR/MOK.crt" -subj "/CN=$CN/"
        openssl x509 -in "$KEYDIR/MOK.crt" -outform DER -out "$KEYDIR/MOK.der"
        chmod 600 "$KEYDIR"/MOK.*
        echo "Generated MOK key in $KEYDIR."
    fi
    echo
    echo ">>> You'll now set a ONE-TIME password. Remember it for the next reboot. <<<"
    mokutil --import "$KEYDIR/MOK.der"
    echo
    echo "NEXT:"
    echo "  1) Reboot."
    echo "  2) At the blue 'MOK Manager' (Perform MOK management) screen:"
    echo "     Enroll MOK -> Continue -> Yes -> enter the password you just set."
    echo "  3) It reboots into your normal system."
    echo "  4) Then run:  sudo $0 install"
}

install() {
    [[ -f "$KEYDIR/MOK.key" ]] || { echo "Run 'sudo $0 genkey' first."; exit 1; }
    if ! mokutil --test-key "$KEYDIR/MOK.der" 2>/dev/null | grep -qi "already enrolled"; then
        echo "WARNING: the Craftboot MOK is NOT enrolled yet (checked with mokutil --test-key)."
        echo "Reboot and complete 'Enroll MOK' in the blue MOK Manager screen first."
        echo "Press Enter to build anyway (craftboot won't BOOT until the key is enrolled),"
        echo "or Ctrl-C to abort."
        read -r _
    fi
    "$HERE/uki-build.sh" "$(uname -r)"
    # shim (Microsoft-signed) verifies our MOK-signed UKI (named grubx64.efi)
    cp -f "$UBUNTU_ESP/shimx64.efi" "$ESPDIR/shimx64.efi"
    cp -f "$UBUNTU_ESP/mmx64.efi"   "$ESPDIR/mmx64.efi" 2>/dev/null || true

    esp="$(findmnt -no SOURCE /boot/efi)"
    disk="/dev/$(lsblk -no pkname "$esp")"
    part="$(cat "/sys/class/block/$(basename "$esp")/partition")"
    # avoid duplicate entries
    for b in $(efibootmgr | awk '/Craftboot/{gsub(/Boot|\*/,"",$1);print $1}'); do
        efibootmgr -b "$b" -B >/dev/null 2>&1 || true
    done
    efibootmgr --create --disk "$disk" --part "$part" \
        --label "Craftboot" --loader '\EFI\craftboot\shimx64.efi' >/dev/null

    cat > "$HOOK" <<EOF
#!/bin/sh
ver="\$1"; [ -n "\$ver" ] || exit 0
if "$REPO/boot/build.sh" "\$ver" >/dev/null 2>&1 && "$REPO/boot/uki-build.sh" "\$ver" >/dev/null 2>&1; then
    :
else echo "craftboot: UKI rebuild failed for \$ver (boot Ubuntu and rebuild)" >&2; fi
exit 0
EOF
    chmod +x "$HOOK"

    echo
    echo "======================= INSTALLED ======================="
    echo "Craftboot is now its OWN firmware boot entry (via shim, signed by your MOK):"
    efibootmgr | grep -i craftboot || true
    echo
    echo "It was added first in BootOrder. Reorder it anytime in your UEFI BIOS boot menu,"
    echo "or with:  sudo efibootmgr -o <hex>,<hex>,...   (Ubuntu/Windows remain separate entries)."
    echo "If craftboot ever misbehaves, just pick Ubuntu in the firmware boot menu."
    echo "========================================================="
}

uninstall() {
    for b in $(efibootmgr | awk '/Craftboot/{gsub(/Boot|\*/,"",$1);print $1}'); do
        efibootmgr -b "$b" -B >/dev/null 2>&1 || true
    done
    rm -rf "$ESPDIR" "$HOOK"
    echo "Removed the Craftboot firmware entry, ESP files, and UKI hook."
    echo "(The MOK key is kept in $KEYDIR. To un-enroll: sudo mokutil --delete $KEYDIR/MOK.der)"
}

case "${1:-}" in
    genkey) genkey;;
    install) install;;
    --uninstall) uninstall;;
    *) echo "usage: sudo $0 {genkey|install|--uninstall}"; exit 1;;
esac
