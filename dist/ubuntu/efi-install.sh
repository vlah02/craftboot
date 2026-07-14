#!/bin/bash
# Install craftboot v3 as its OWN firmware boot entry ("Craftboot v3"), signed
# via the MOK key + Ubuntu's Microsoft-signed shim (Secure Boot compatible).
# Appended to BootOrder, NOT moved to front -- Windows/Ubuntu keep booting by
# default until the user explicitly reorders in firmware or runs --promote.
#
#   sudo ./dist/ubuntu/efi-install.sh install      sign+install everything (default)
#   sudo ./dist/ubuntu/efi-install.sh --promote     move "Craftboot v3" first, retire v2
#   sudo ./dist/ubuntu/efi-install.sh --uninstall   remove "Craftboot v3" only
#        ./dist/ubuntu/efi-install.sh --print       show the install plan, no mutation, no root
#
# Reuses proven idioms from uki-setup.sh (MOK genkey/enroll check, dedup loop)
# and ubuntu-direct.sh (esp_disk_part, the ukify build pattern).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
ESP="/boot/efi"
KEYDIR="/var/lib/craftboot"
ESPDIR="$ESP/EFI/craftbootv3"
UBUNTU_ESP="$ESP/EFI/ubuntu"
RECOVERY_ESPDIR="$ESP/EFI/ubunturecovery"
MEMTEST_ESPDIR="$ESP/EFI/memtest"
V2_ESPDIR="$ESP/EFI/craftboot"
HOOK="/etc/kernel/postinst.d/zz-craftboot-v3"
V2_HOOK="/etc/kernel/postinst.d/zz-craftboot-uki"
LABEL="Craftboot v3"
V2_LABEL="Craftboot"

# --print needs neither root nor to mutate anything; every other subcommand does.
cmd="${1:-install}"
if [[ "$cmd" != "--print" ]]; then
    [[ $EUID -eq 0 ]] || { echo "Please run with sudo."; exit 1; }
fi

# Print "<disk> <part>" for the ESP (e.g. "/dev/nvme0n1 2"), as efibootmgr wants them.
# (verbatim idiom from ubuntu-direct.sh)
esp_disk_part() {
    local esp disk part
    esp="$(findmnt -no SOURCE "$ESP")"
    disk="/dev/$(lsblk -no pkname "$esp")"
    part="$(cat "/sys/class/block/$(basename "$esp")/partition")"
    echo "$disk $part"
}

# Build+sign a UKI: build_uki <cmdline> <destdir> [kver]
# (factored out of ubuntu-direct.sh's build(), parameterized by cmdline+dest)
# kver defaults to the running kernel, but the kernel postinst hook passes the
# newly-installed version explicitly -- it must NOT be silently ignored, since
# uname -r would still be the old (running) kernel at postinst time.
build_uki() {
    local cmdline="$1" destdir="$2" kver="${3:-$(uname -r)}"
    local kernel="/boot/vmlinuz-$kver"
    local initrd="/boot/initrd.img-$kver"

    [[ -f "$KEYDIR/MOK.key" ]] || { echo "No MOK key in $KEYDIR -- run genkey first."; exit 1; }
    [[ -f "$KEYDIR/MOK.crt" ]] || { echo "No MOK cert in $KEYDIR -- run genkey first."; exit 1; }
    [[ -f "$kernel" ]] || { echo "No kernel at $kernel"; exit 1; }
    [[ -f "$initrd" ]] || { echo "No initramfs at $initrd"; exit 1; }

    local ukify; ukify="$(command -v ukify 2>/dev/null || true)"
    [[ -x "$ukify" ]] || { [[ -x /usr/lib/systemd/ukify ]] && ukify=/usr/lib/systemd/ukify; }
    [[ -x "${ukify:-}" ]] || { echo "Missing ukify -- run: sudo apt install systemd-ukify"; exit 1; }

    # 'ukify build ...' on modern systemd; older versions take no subcommand
    local sub=(build); "$ukify" build --help >/dev/null 2>&1 || sub=()

    mkdir -p "$destdir"
    "$ukify" "${sub[@]}" \
        --linux="$kernel" \
        --initrd="$initrd" \
        --cmdline="$cmdline" \
        --secureboot-private-key="$KEYDIR/MOK.key" \
        --secureboot-certificate="$KEYDIR/MOK.crt" \
        --output="$destdir/grubx64.efi"

    echo "Signed UKI -> $destdir/grubx64.efi (cmdline: $cmdline)"
}

# Find the Boot#### numbers of any firmware entries with the given exact label.
# Robust to the active-marker width: efibootmgr prints "Boot0007* Label" for an
# ACTIVE entry but "Boot0005  Label" (blank placeholder + space) for an INACTIVE
# one, so a fixed "\*? " assumption leaves a stray leading space on inactive
# entries and breaks the exact-label compare. Instead: take everything after the
# 4 hex digits, drop an optional "*", strip surrounding whitespace, and keep the
# label up to the tab. Stays EXACT-label (never cross-matches "Craftboot" vs
# "Craftboot v3", nor Windows/Ubuntu).
bootnums_for_label() {
    local label="$1"
    efibootmgr | awk -v want="$label" '{
        if (match($0, /^Boot[0-9A-Fa-f]{4}/)) {
            num = substr($0, 5, 4)
            rest = substr($0, 9)             # text after "Boot####"
            sub(/^\*/, "", rest)             # drop active marker if present
            sub(/^[[:space:]]+/, "", rest)   # drop leading whitespace
            sub(/\t.*/, "", rest)            # keep label up to the tab
            sub(/[[:space:]]+$/, "", rest)   # drop any trailing whitespace
            if (rest == want) print num
        }
    }'
}

# Install the MS-signed shim into <destdir> as shimx64.efi (+ mmx64.efi
# best-effort). Prefers Ubuntu's ESP copy, falls back to the shim package's
# signed binary; loud error if neither exists (the chainload target needs it).
install_shim() {
    local destdir="$1"
    mkdir -p "$destdir"
    if [[ -f "$UBUNTU_ESP/shimx64.efi" ]]; then
        cp -f "$UBUNTU_ESP/shimx64.efi" "$destdir/shimx64.efi"
        cp -f "$UBUNTU_ESP/mmx64.efi"   "$destdir/mmx64.efi" 2>/dev/null || true
    elif [[ -f /usr/lib/shim/shimx64.efi.signed ]]; then
        cp -f /usr/lib/shim/shimx64.efi.signed "$destdir/shimx64.efi"
    else
        echo "ERROR: no MS-signed shim found at $UBUNTU_ESP/shimx64.efi or /usr/lib/shim/shimx64.efi.signed" >&2
        echo "       $destdir will have no shimx64.efi and its chainload entry will fail to launch." >&2
        return 1
    fi
}

# Locate a memtest86+ EFI binary on this system (best-effort, several layouts).
find_memtest_efi() {
    local candidates=()
    if command -v dpkg >/dev/null 2>&1; then
        # Debian/Ubuntu's memtest86+ package ships /boot/mt86+x64 -- a bzImage
        # with an EFI stub, which IS a valid signable PE/COFF binary (MZ+PE
        # header) despite the lack of a ".efi" extension. Match either naming.
        while IFS= read -r f; do candidates+=("$f"); done < \
            <(dpkg -L memtest86+ 2>/dev/null | grep -iE '(\.efi$|/boot/mt86\+x64$)' || true)
    fi
    candidates+=(/boot/mt86+x64 /boot/memtest86+x64.efi /boot/efi/memtest86+/memtest86+x64.efi)
    local c
    for c in "${candidates[@]}"; do
        if [[ -n "$c" && -f "$c" ]]; then
            # Verify it's actually a PE/COFF (MZ magic) before handing it to
            # sbsign, so a stray non-EFI match doesn't produce a confusing
            # sbsign error. (memtest86+'s /boot/mt86+x64 is a bzImage with an
            # EFI stub -- it IS a valid PE despite lacking a ".efi" name.)
            if [[ "$(head -c2 "$c" 2>/dev/null)" == "MZ" ]]; then
                echo "$c"; return 0
            fi
        fi
    done
    return 1
}

print_plan() {
    cat <<EOF
==================== --print: install plan (no changes made) ====================
Repo:              $REPO
ESP:                $ESP
Key dir:            $KEYDIR
Craftboot v3 ESP:   $ESPDIR
Recovery ESP:       $RECOVERY_ESPDIR
Memtest ESP:        $MEMTEST_ESPDIR
Kernel hook:        $HOOK

Steps 'install' would take:
  1) Check MOK key at $KEYDIR/MOK.key; genkey+import if absent, error if not enrolled.
  2) make -C "$REPO" efi
     sbsign --key $KEYDIR/MOK.key --cert $KEYDIR/MOK.crt \\
       --output $ESPDIR/grubx64.efi $REPO/build/craftboot.efi
     cp $UBUNTU_ESP/shimx64.efi $ESPDIR/shimx64.efi (+ mmx64.efi best-effort)
     cp $REPO/boot_entries.json $ESPDIR/boot_entries.json
     cp -r $REPO/assets $ESPDIR/assets   (ESP is FAT: -r not -a, no ownership)
  3) Refresh normal UKI via $HERE/ubuntu-direct.sh --build-only (dest: $ESP/EFI/ubuntudirect)
  4) build_uki "root=UUID=<root-uuid> ro recovery nomodeset" $RECOVERY_ESPDIR
     cp $UBUNTU_ESP/shimx64.efi $RECOVERY_ESPDIR/shimx64.efi
  5) Locate memtest86+ EFI; if found, sbsign -> $MEMTEST_ESPDIR/mtx64.efi; else WARN + continue.
  6) Dedup any prior "$LABEL" entries; efibootmgr --create --disk <disk> --part <part> \\
       --label "$LABEL" --loader '\\EFI\\craftbootv3\\shimx64.efi'  (appended, BootOrder untouched)
  7) Install kernel hook -> $HOOK (rebuilds+re-signs UKIs on kernel update)

Detected (best-effort, informational only):
EOF
    if command -v findmnt >/dev/null 2>&1 && findmnt -no SOURCE "$ESP" >/dev/null 2>&1; then
        local dp; dp="$(esp_disk_part 2>/dev/null || true)"
        echo "  esp_disk_part:     ${dp:-<could not determine>}"
    else
        echo "  esp_disk_part:     <findmnt/ESP not available in this environment>"
    fi
    echo "  root UUID:         $(findmnt -no UUID / 2>/dev/null || echo '<unavailable>')"
    if mm="$(find_memtest_efi 2>/dev/null)"; then
        echo "  memtest86+ EFI:    $mm"
    else
        echo "  memtest86+ EFI:    <not found>"
    fi
    echo "  MOK key present:   $([[ -f "$KEYDIR/MOK.key" ]] && echo yes || echo no)"
    echo "===================================================================================="
}

install() {
    # 1) MOK: reuse iff present+enrolled; genkey+instruct if absent; error if not enrolled.
    if [[ ! -f "$KEYDIR/MOK.key" ]]; then
        echo "No MOK key found in $KEYDIR."
        mkdir -p "$KEYDIR"; chmod 700 "$KEYDIR"
        openssl req -new -x509 -newkey rsa:2048 -nodes -days 3650 \
            -keyout "$KEYDIR/MOK.key" -out "$KEYDIR/MOK.crt" -subj "/CN=Craftboot Secure Boot/"
        openssl x509 -in "$KEYDIR/MOK.crt" -outform DER -out "$KEYDIR/MOK.der"
        chmod 600 "$KEYDIR"/MOK.*
        echo
        echo ">>> You'll now set a ONE-TIME password. Remember it for the next reboot. <<<"
        mokutil --import "$KEYDIR/MOK.der"
        echo
        echo "NEXT:"
        echo "  1) Reboot."
        echo "  2) At the blue 'MOK Manager' (Perform MOK management) screen:"
        echo "     Enroll MOK -> Continue -> Yes -> enter the password you just set."
        echo "  3) It reboots into your normal system."
        echo "  4) Then re-run: sudo $0 install"
        exit 0
    fi
    # capture first (mokutil exits non-zero even when enrolled; pipefail would
    # otherwise poison the pipeline and give a false "not enrolled")
    mok_status="$(mokutil --test-key "$KEYDIR/MOK.der" 2>&1 || true)"
    if ! printf '%s' "$mok_status" | grep -qi "already enrolled"; then
        echo "ERROR: the Craftboot MOK is NOT enrolled yet (checked with mokutil --test-key)."
        echo "Reboot and complete 'Enroll MOK' in the blue MOK Manager screen first, then re-run."
        exit 1
    fi

    # 2) craftboot.efi: build, sign, copy shim + assets.
    make -C "$REPO" efi
    [[ -f "$REPO/build/craftboot.efi" ]] || { echo "make -C $REPO efi did not produce build/craftboot.efi"; exit 1; }
    mkdir -p "$ESPDIR"
    sbsign --key "$KEYDIR/MOK.key" --cert "$KEYDIR/MOK.crt" \
        --output "$ESPDIR/grubx64.efi" "$REPO/build/craftboot.efi"

    install_shim "$ESPDIR" || exit 1

    if [[ -f "$REPO/boot_entries.json" ]]; then
        cp -f "$REPO/boot_entries.json" "$ESPDIR/boot_entries.json"
    else
        echo "ERROR: $REPO/boot_entries.json not found — the menu has no config to read." >&2
        exit 1
    fi
    if [[ -d "$REPO/assets" ]]; then
        # The ESP is FAT (no Unix ownership) -> cp -a's --preserve=ownership
        # fails with EPERM per file and aborts the whole install under set -e.
        # cp -r copies data only. Remove any partial copy from a prior aborted
        # run first so we don't nest into $ESPDIR/assets/assets.
        rm -rf "$ESPDIR/assets"
        cp -r "$REPO/assets" "$ESPDIR/assets"
    else
        echo "ERROR: $REPO/assets not found — the menu has no textures/fonts to render." >&2
        exit 1
    fi

    # 3) Normal UKI: refresh \EFI\ubuntudirect\ (already installed/existing; this just re-signs current kernel).
    if [[ -x "$HERE/ubuntu-direct.sh" ]]; then
        "$HERE/ubuntu-direct.sh" --build-only "$(uname -r)" || \
            echo "WARNING: refreshing the normal ubuntudirect UKI failed; leaving existing one in place."
    else
        echo "NOTE: $HERE/ubuntu-direct.sh not found/executable; skipping normal UKI refresh."
    fi

    # 4) Recovery UKI.
    local uuid; uuid="$(findmnt -no UUID / 2>/dev/null || true)"
    [[ -n "$uuid" ]] || { echo "Could not resolve the root filesystem UUID (findmnt -no UUID /)."; exit 1; }
    build_uki "root=UUID=$uuid ro recovery nomodeset" "$RECOVERY_ESPDIR" "$(uname -r)"
    install_shim "$RECOVERY_ESPDIR" || \
        echo "WARNING: recovery UKI installed but its shim is missing — 'Ubuntu (recovery)' will fail to launch until fixed."

    # 5) Memtest (best-effort under Secure Boot).
    if memtest_src="$(find_memtest_efi)"; then
        mkdir -p "$MEMTEST_ESPDIR"
        if sbsign --key "$KEYDIR/MOK.key" --cert "$KEYDIR/MOK.crt" \
            --output "$MEMTEST_ESPDIR/mtx64.efi" "$memtest_src"; then
            echo "Signed memtest86+ -> $MEMTEST_ESPDIR/mtx64.efi (from $memtest_src)"
        else
            echo "WARNING: found memtest86+ at $memtest_src but sbsign failed. The Memtest86+ menu"
            echo "         entry will fail to launch until this is fixed manually. Continuing."
        fi
    else
        echo "WARNING: no memtest86+ EFI binary found (checked dpkg -L memtest86+, /boot/memtest86+x64.efi)."
        echo "         The Memtest86+ menu entry will fail to launch until one is installed and signed."
        echo "         Install with: sudo apt install memtest86+   then re-run this script."
    fi

    # 6) Firmware entry: dedup, create, append (never reorder to front).
    for b in $(bootnums_for_label "$LABEL"); do
        efibootmgr -b "$b" -B >/dev/null 2>&1 || true
    done
    local disk part
    read -r disk part <<<"$(esp_disk_part)"
    [[ -n "$disk" && "$disk" != "/dev/" && -n "$part" ]] || { echo "Could not derive the ESP disk/partition for efibootmgr."; exit 1; }
    efibootmgr --create --disk "$disk" --part "$part" \
        --label "$LABEL" --loader '\EFI\craftbootv3\shimx64.efi' >/dev/null

    # 7) Kernel hook: rebuild+re-sign the normal + recovery UKIs on kernel update.
    cat > "$HOOK" <<EOF
#!/bin/sh
# Rebuilds the normal + recovery UKIs whenever a new kernel is installed.
# Resilient: a failure here must never block apt/kernel installation.
ver="\$1"; [ -n "\$ver" ] || exit 0
ok=1
if [ -x "$HERE/ubuntu-direct.sh" ]; then
    "$HERE/ubuntu-direct.sh" --build-only "\$ver" >/dev/null 2>&1 || ok=0
fi
"$HERE/efi-install.sh" --rebuild-recovery "\$ver" >/dev/null 2>&1 || ok=0
if [ "\$ok" -eq 0 ]; then
    echo "craftboot v3: UKI rebuild failed for \$ver (boot Craftboot v3 -> Extras ->" >&2
    echo "  'Ubuntu (recovery)', then re-run: sudo $HERE/efi-install.sh install" >&2
fi
exit 0
EOF
    chmod +x "$HOOK"

    echo
    echo "======================= INSTALLED ======================="
    echo "\"$LABEL\" is now its OWN firmware boot entry (appended, NOT made default):"
    efibootmgr | grep -i "craftboot v3" || true
    echo
    echo "Windows and Ubuntu entries are untouched. Next steps:"
    echo "  1) Reboot."
    echo "  2) Open your firmware's one-time boot menu (often F12/F10/Esc at power-on)."
    echo "  3) Pick \"$LABEL\" to test it."
    echo "  4) Once happy, run: sudo $0 --promote   (makes it the default + retires v2)"
    echo "==========================================================="
}

promote() {
    local new_bootnums; new_bootnums="$(bootnums_for_label "$LABEL")"
    [[ -n "$new_bootnums" ]] || { echo "No \"$LABEL\" entry found -- run install first."; exit 1; }
    local new_bootnum; new_bootnum="$(echo "$new_bootnums" | head -1)"

    local order; order="$(efibootmgr | awk -F': ' '/^BootOrder/{print $2}')"
    [[ -n "$order" ]] || { echo "Could not read current BootOrder."; exit 1; }
    IFS=',' read -ra nums <<<"$order"
    local new_order="$new_bootnum"
    local n
    for n in "${nums[@]}"; do
        [[ "$n" == "$new_bootnum" ]] || new_order+=",$n"
    done
    efibootmgr -o "$new_order" >/dev/null
    echo "BootOrder updated: $new_order"
    efibootmgr | grep -E '^BootOrder' || true

    # Remove the v2 "Craftboot" entry + its ESP dir + old hook (never Windows/Ubuntu).
    for b in $(bootnums_for_label "$V2_LABEL"); do
        efibootmgr -b "$b" -B >/dev/null 2>&1 || true
        echo "Removed v2 entry: Boot$b ($V2_LABEL)"
    done
    [[ -d "$V2_ESPDIR" ]] && rm -rf "$V2_ESPDIR" && echo "Removed $V2_ESPDIR"
    [[ -f "$V2_HOOK" ]] && rm -f "$V2_HOOK" && echo "Removed $V2_HOOK"

    echo "\"$LABEL\" is now first in BootOrder."
}

uninstall() {
    for b in $(bootnums_for_label "$LABEL"); do
        efibootmgr -b "$b" -B >/dev/null 2>&1 || true
        echo "Removed entry: Boot$b ($LABEL)"
    done
    [[ -d "$ESPDIR" ]] && rm -rf "$ESPDIR" && echo "Removed $ESPDIR"
    echo "(Kept: $RECOVERY_ESPDIR, $MEMTEST_ESPDIR, and the MOK key in $KEYDIR.)"
    echo "Uninstalled \"$LABEL\"."
}

case "$cmd" in
    install|"")     install;;
    --promote)      promote;;
    --uninstall)    uninstall;;
    --print)        print_plan;;
    --rebuild-recovery)
        # internal: called by the kernel hook to refresh just the recovery UKI
        # for the kernel version passed as $2 (the postinst hook's $1, i.e.
        # the newly-installed kernel -- NOT necessarily the running one).
        shift || true
        uuid="$(findmnt -no UUID / 2>/dev/null || true)"
        [[ -n "$uuid" ]] || { echo "Could not resolve root UUID"; exit 1; }
        build_uki "root=UUID=$uuid ro recovery nomodeset" "$RECOVERY_ESPDIR" "${1:-$(uname -r)}"
        ;;
    *) echo "usage: sudo $0 {install|--promote|--uninstall} | $0 --print"; exit 1;;
esac
