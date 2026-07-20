#!/bin/bash
# Reusable local harness for booting build/craftboot.efi under QEMU/OVMF.
#
# Replaces the throwaway inline staging/launch logic hand-rolled for the
# Task 8b/9 QEMU gates with one parametrized script: stage a FAT ESP tree,
# boot it headless under OVMF, then drive the human monitor (HMP) over a
# unix socket to screendump the framebuffer and send keys.
#
# This is a LOCAL developer/gate tool only -- CI does not boot QEMU (OVMF
# under CI is flaky), it only builds `make efi` and checks the PE32+
# signature (see the `efi-build` job in .github/workflows/ci.yml).
#
# Dependencies: qemu-system-x86_64, OVMF (OVMF_CODE_4M.fd + OVMF_VARS_4M.fd,
# default /usr/share/OVMF/), and one of `socat` or `python3` (python3 is
# already relied on elsewhere in this repo/CI) to talk to the QEMU monitor
# socket. Optional: ImageMagick `convert` or netpbm `pnmtopng` to convert
# screendump .ppm output to .png.
#
# Usage:
#   tools/run-qemu-efi.sh boot [options]        stage ESP + launch QEMU (background)
#   tools/run-qemu-efi.sh screendump <out.ppm|out.png> [--monitor-sock PATH]
#   tools/run-qemu-efi.sh sendkey <qemu-key-name> [--monitor-sock PATH]
#   tools/run-qemu-efi.sh stop [--monitor-sock PATH]
#   tools/run-qemu-efi.sh -h | --help
#
# `boot` options:
#   --esp DIR                ESP staging directory (default: build/esp-efi)
#   --efi PATH                craftboot.efi to stage as BOOTX64.EFI (default: build/craftboot.efi)
#   --config PATH             boot_entries.json to stage (default: repo boot_entries.json)
#   --chainload-target FILE   extra .efi copied to the ESP root (Task 9-style chainload target test)
#   --vnc N                   VNC display number, i.e. `-vnc :N` (default: 20)
#   --monitor-sock PATH       HMP monitor unix socket path (default: build/qemu-efi-monitor.sock)
#   --serial-log PATH         serial console log path (default: build/qemu-efi-serial.log)
#   --ovmf-code PATH          OVMF_CODE (default: /usr/share/OVMF/OVMF_CODE_4M.fd)
#   --ovmf-vars PATH          OVMF_VARS to copy from (default: /usr/share/OVMF/OVMF_VARS_4M.fd)
#   --pid-file PATH           where to write the backgrounded QEMU pid (default: build/qemu-efi.pid)
#   --no-kvm                  force TCG even if /dev/kvm is usable
#
# Staging layout produced under --esp (default build/esp-efi/):
#   EFI/BOOT/BOOTX64.EFI              <- --efi (or build/craftboot.efi)
#   EFI/BOOT/boot_entries.json        <- --config (or repo boot_entries.json)
#   EFI/BOOT/assets/                  <- repo assets/ tree
#   EFI/craftboot/boot_entries.json   <- same --config, staged again (M-A-layout fallback)
#   EFI/craftboot/assets/             <- repo assets/ tree, staged again (M-A-layout fallback)
#   <chainload-target basename>       <- --chainload-target, at the ESP root (optional)
#
# BOOTX64.EFI is loaded as removable media, so its own LoadedImage->FilePath
# is "\EFI\BOOT\BOOTX64.EFI" -- craftboot.efi derives its base dir from that
# (M-B Task 2), so config/assets must live at EFI/BOOT/ for the derived-base
# path to actually be exercised. The EFI/craftboot/ copy is kept too so the
# "\EFI\craftboot" fallback path also has something to find if ever needed.
#
# `boot` prints the monitor socket path, serial log path, VNC display, and
# backgrounded QEMU pid, then returns immediately -- QEMU keeps running in
# the background. Drive it afterwards with this script's own `screendump`
# and `sendkey` subcommands (they default to the same --monitor-sock path),
# or connect a VNC viewer to the printed display. `stop` sends `quit` over
# the monitor to shut it down cleanly.
#
# KVM: used automatically (-enable-kvm -cpu host) when /dev/kvm exists and
# is accessible -- this gives an accurate TSC and avoids the countdown-timing
# jitter seen under plain TCG (Task 9). Falls back to TCG with a printed
# warning if /dev/kvm is missing/inaccessible, or if --no-kvm is passed.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$REPO/build"

ESP="$BUILD/esp-efi"
EFI_BIN="$BUILD/craftboot.efi"
CONFIG="$REPO/boot_entries.json"
CHAINLOAD_TARGET=""
VNC_NUM=20
MONITOR_SOCK="$BUILD/qemu-efi-monitor.sock"
SERIAL_LOG="$BUILD/qemu-efi-serial.log"
OVMF_CODE="/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS_SRC="/usr/share/OVMF/OVMF_VARS_4M.fd"
PID_FILE="$BUILD/qemu-efi.pid"
FORCE_NO_KVM=0

usage() { sed -n '2,55p' "$0" | sed 's/^# \{0,1\}//'; }

# ---- monitor helpers (HMP over a unix socket) ------------------------------
# QEMU's `-monitor unix:PATH,server,nowait` chardev speaks the plain-text
# Human Monitor Protocol: connect, read the banner/prompt, write a command
# line, read the reply. Each helper call opens a short-lived connection
# (QEMU accepts a new client once the previous one disconnects), so repeated
# screendump/sendkey calls against the same socket are safe to issue in
# sequence.
mon_send() {
    local sock="$1" cmd="$2"
    if command -v socat >/dev/null 2>&1; then
        printf '%s\n' "$cmd" | socat -t2 - "UNIX-CONNECT:$sock" >/dev/null 2>&1 || true
    elif command -v python3 >/dev/null 2>&1; then
        python3 - "$sock" "$cmd" <<'PY'
import socket, sys, time
sock, cmd = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(3)
s.connect(sock)
try:
    s.recv(4096)  # banner + "(qemu) " prompt
except Exception:
    pass
s.sendall((cmd + "\n").encode())
time.sleep(0.2)
try:
    s.recv(4096)
except Exception:
    pass
s.close()
PY
    else
        echo "error: need 'socat' or 'python3' to talk to the QEMU monitor" >&2
        return 1
    fi
}

cmd_screendump() {
    local out="${1:?usage: $0 screendump <out.ppm|out.png> [--monitor-sock PATH]}"
    shift
    parse_common_opts "$@"
    local ppm="$out"
    case "$out" in
        *.png) ppm="${out%.png}.ppm" ;;
    esac
    mon_send "$MONITOR_SOCK" "screendump $ppm"
    if [[ "$ppm" != "$out" ]]; then
        if command -v convert >/dev/null 2>&1; then
            convert "$ppm" "$out" && rm -f "$ppm"
        elif command -v pnmtopng >/dev/null 2>&1; then
            pnmtopng "$ppm" > "$out" && rm -f "$ppm"
        else
            echo "note: no 'convert'/'pnmtopng' found, left raw PPM at $ppm" >&2
        fi
    fi
    echo "screendump -> ${out}"
}

cmd_sendkey() {
    local key="${1:?usage: $0 sendkey <qemu-key-name> [--monitor-sock PATH]}"
    shift
    parse_common_opts "$@"
    mon_send "$MONITOR_SOCK" "sendkey $key"
    echo "sendkey $key"
}

cmd_stop() {
    parse_common_opts "$@"
    mon_send "$MONITOR_SOCK" "quit"
    echo "sent 'quit' to $MONITOR_SOCK"
}

parse_common_opts() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --monitor-sock) MONITOR_SOCK="$2"; shift 2 ;;
            *) echo "unknown option: $1" >&2; exit 1 ;;
        esac
    done
}

# ---- boot -------------------------------------------------------------
cmd_boot() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --esp) ESP="$2"; shift 2 ;;
            --efi) EFI_BIN="$2"; shift 2 ;;
            --config) CONFIG="$2"; shift 2 ;;
            --chainload-target) CHAINLOAD_TARGET="$2"; shift 2 ;;
            --vnc) VNC_NUM="$2"; shift 2 ;;
            --monitor-sock) MONITOR_SOCK="$2"; shift 2 ;;
            --serial-log) SERIAL_LOG="$2"; shift 2 ;;
            --ovmf-code) OVMF_CODE="$2"; shift 2 ;;
            --ovmf-vars) OVMF_VARS_SRC="$2"; shift 2 ;;
            --pid-file) PID_FILE="$2"; shift 2 ;;
            --no-kvm) FORCE_NO_KVM=1; shift ;;
            *) echo "unknown option: $1" >&2; exit 1 ;;
        esac
    done

    [[ -f "$EFI_BIN" ]] || { echo "error: no EFI app at $EFI_BIN (run: make efi)" >&2; exit 1; }
    [[ -f "$CONFIG" ]] || { echo "error: no config at $CONFIG" >&2; exit 1; }
    [[ -d "$REPO/assets" ]] || { echo "error: no assets/ dir at $REPO/assets" >&2; exit 1; }
    [[ -f "$OVMF_CODE" ]] || { echo "error: no OVMF_CODE at $OVMF_CODE" >&2; exit 1; }
    [[ -f "$OVMF_VARS_SRC" ]] || { echo "error: no OVMF_VARS at $OVMF_VARS_SRC" >&2; exit 1; }
    if [[ -n "$CHAINLOAD_TARGET" ]]; then
        [[ -f "$CHAINLOAD_TARGET" ]] || { echo "error: no chainload target at $CHAINLOAD_TARGET" >&2; exit 1; }
    fi

    mkdir -p "$BUILD"

    # ---- stage the ESP tree (fresh each run) ----
    rm -rf "$ESP"
    mkdir -p "$ESP/EFI/BOOT" "$ESP/EFI/craftboot"
    cp -f "$EFI_BIN" "$ESP/EFI/BOOT/BOOTX64.EFI"
    # Derived-base layout (LoadedImage->FilePath for a removable-media boot
    # is "\EFI\BOOT\BOOTX64.EFI" -> base "\EFI\BOOT"): this is what actually
    # gets read now that craftboot.efi derives its base dir instead of
    # hardcoding "\EFI\craftboot".
    cp -f "$CONFIG" "$ESP/EFI/BOOT/boot_entries.json"
    cp -a "$REPO/assets" "$ESP/EFI/BOOT/assets"
    # M-A-layout fallback copy, so "\EFI\craftboot" still works if
    # self_base_dir() ever fails to walk the device path.
    cp -f "$CONFIG" "$ESP/EFI/craftboot/boot_entries.json"
    cp -a "$REPO/assets" "$ESP/EFI/craftboot/assets"
    if [[ -n "$CHAINLOAD_TARGET" ]]; then
        cp -f "$CHAINLOAD_TARGET" "$ESP/$(basename "$CHAINLOAD_TARGET")"
    fi

    # ---- writable OVMF_VARS copy (never touch the system file) ----
    local vars_copy="$BUILD/OVMF_VARS-efi.fd"
    cp -f "$OVMF_VARS_SRC" "$vars_copy"

    # ---- KVM detection ----
    # The TCG fallback MUST pass -cpu max. craftboot.efi is built -mavx2 with no
    # runtime scalar fallback, and QEMU's default TCG model (qemu64) does not
    # advertise AVX2 -- the app faults on its first vector instruction and the
    # boot dies right after BdsDxe hands it control, with no console to say why.
    # -cpu max enables everything the accelerator can emulate, AVX2 included.
    local kvm_args=()
    if [[ "$FORCE_NO_KVM" -eq 0 && -e /dev/kvm && -r /dev/kvm && -w /dev/kvm ]]; then
        kvm_args=(-enable-kvm -cpu host)
    else
        kvm_args=(-cpu max)
        echo "warning: /dev/kvm unavailable (or --no-kvm) -- falling back to TCG software emulation (slower; TSC/timing may jitter)" >&2
    fi

    rm -f "$MONITOR_SOCK" "$SERIAL_LOG"

    # Run in a clean environment (snap-launched shells leak an
    # LD_LIBRARY_PATH that breaks the system qemu's glibc), same fix as
    env -i PATH="$PATH" HOME="${HOME:-/root}" \
        /usr/bin/qemu-system-x86_64 \
        "${kvm_args[@]}" \
        -machine q35 -m 2048 -smp "${SMP:-8}" \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
        -drive if=pflash,format=raw,unit=1,file="$vars_copy" \
        -drive format=raw,file="fat:rw:$ESP" \
        -vga std -display none \
        -vnc ":$VNC_NUM" \
        -monitor "unix:$MONITOR_SOCK,server,nowait" \
        -serial "file:$SERIAL_LOG" \
        -no-reboot \
        >"$BUILD/qemu-efi-stdout.log" 2>&1 &
    local qpid=$!
    echo "$qpid" > "$PID_FILE"

    # wait for the monitor socket to appear before handing control back
    for _ in $(seq 1 50); do
        [[ -S "$MONITOR_SOCK" ]] && break
        sleep 0.1
    done

    echo "QEMU started (pid $qpid)"
    echo "  ESP:           $ESP"
    echo "  monitor sock:  $MONITOR_SOCK"
    echo "  serial log:    $SERIAL_LOG"
    echo "  VNC display:   :$VNC_NUM"
    echo "  pid file:      $PID_FILE"
    echo "Drive it with: $0 screendump <out.ppm|out.png> [--monitor-sock $MONITOR_SOCK]"
    echo "           or: $0 sendkey <key> [--monitor-sock $MONITOR_SOCK]"
    echo "Stop with:     $0 stop [--monitor-sock $MONITOR_SOCK]"
}

# ---- dispatch ------------------------------------------------------------
case "${1:-}" in
    -h|--help) usage ;;
    "") usage >&2; exit 1 ;;
    boot) shift; cmd_boot "$@" ;;
    screendump) shift; cmd_screendump "$@" ;;
    sendkey) shift; cmd_sendkey "$@" ;;
    stop) shift; cmd_stop "$@" ;;
    *) echo "unknown subcommand: $1" >&2; usage >&2; exit 1 ;;
esac
