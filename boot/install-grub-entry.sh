#!/bin/bash
# Install / manage the Craftboot boot entry.
#
#   sudo ./boot/install-grub-entry.sh                install entry + kernel hook (non-default)
#   sudo ./boot/install-grub-entry.sh --set-default  make craftboot the GRUB default (short timeout)
#   sudo ./boot/install-grub-entry.sh --unset-default restore the previous GRUB default
#   sudo ./boot/install-grub-entry.sh --uninstall    remove entry, hook, initramfs, default
#
# Safe & reversible. Secure Boot is fine (GRUB loads the signed kernel; the
# initramfs needs no signing). The kernel hook rebuilds the initramfs on kernel
# updates so making craftboot the default can't be bricked by an apt upgrade.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
B="$HERE/build"
GRUBD="/etc/grub.d/45_craftboot"
INITRD="/boot/craftboot.initrd"
HOOK="/etc/kernel/postinst.d/zz-craftboot"
DEF="/etc/default/grub"
BAK="/etc/default/grub.craftboot-bak"

[[ $EUID -eq 0 ]] || { echo "Please run with sudo."; exit 1; }

set_default() {
    [[ -f "$BAK" ]] || cp "$DEF" "$BAK"      # remember the current defaults once
    if grep -q '^GRUB_DEFAULT=' "$DEF"; then
        sed -i 's/^GRUB_DEFAULT=.*/GRUB_DEFAULT=craftboot/' "$DEF"
    else echo 'GRUB_DEFAULT=craftboot' >> "$DEF"; fi
    if grep -q '^GRUB_TIMEOUT=' "$DEF"; then
        sed -i 's/^GRUB_TIMEOUT=.*/GRUB_TIMEOUT=3/' "$DEF"
    else echo 'GRUB_TIMEOUT=3' >> "$DEF"; fi
    update-grub
    echo "Craftboot is now the GRUB default (3s timeout kept as a safety net)."
    echo "Bail to another OS by picking it within 3s. Undo: sudo $0 --unset-default"
}
unset_default() {
    if [[ -f "$BAK" ]]; then cp "$BAK" "$DEF"; rm -f "$BAK"; update-grub
        echo "Restored the previous GRUB default/timeout."
    else echo "No backup to restore (was craftboot ever the default?)."; fi
}

case "${1:-}" in
    --set-default)   set_default; exit 0;;
    --unset-default) unset_default; exit 0;;
    --uninstall)
        [[ -f "$BAK" ]] && unset_default || true
        rm -f "$GRUBD" "$INITRD" "$HOOK"
        update-grub
        echo "Removed Craftboot entry, initramfs, and kernel hook."
        exit 0;;
esac

# -------- install (non-default) --------
[[ -f "$B/craftboot.initrd" ]] || { echo "Build first: ./boot/build.sh"; exit 1; }
ROOT_UUID="$(findmnt -no UUID /)"

echo "==> copying initramfs -> $INITRD"
cp "$B/craftboot.initrd" "$INITRD"

echo "==> writing $GRUBD (uses the /boot/vmlinuz symlink -> survives kernel updates)"
cat > "$GRUBD" <<EOF
#!/bin/sh
exec tail -n +3 \$0
menuentry 'Craftboot (Minecraft boot menu)' --id craftboot {
    load_video
    insmod gzio
    insmod part_gpt
    insmod ext2
    search --no-floppy --fs-uuid --set=root ${ROOT_UUID}
    linux  /boot/vmlinuz console=tty1 quiet loglevel=3 vt.global_cursor_default=0
    initrd ${INITRD}
}
EOF
chmod +x "$GRUBD"

echo "==> installing kernel hook -> $HOOK (rebuilds initramfs on kernel updates)"
cat > "$HOOK" <<EOF
#!/bin/sh
# Rebuild the craftboot initramfs for a newly installed kernel. \$1 = version.
ver="\$1"
[ -n "\$ver" ] || exit 0
if "$REPO/boot/build.sh" "\$ver" >/dev/null 2>&1; then
    cp "$B/craftboot.initrd" "$INITRD" 2>/dev/null || true
else
    echo "craftboot: initramfs rebuild failed for \$ver (boot to GRUB->Ubuntu and rebuild)" >&2
fi
exit 0
EOF
chmod +x "$HOOK"

update-grub
cat <<EOF

======================= INSTALLED =======================
Test once (default unchanged):   sudo grub-reboot craftboot && sudo reboot
Make craftboot the DEFAULT:      sudo $0 --set-default
Undo default:                    sudo $0 --unset-default
Uninstall everything:            sudo $0 --uninstall
=========================================================
EOF
