#!/bin/bash
# Rebuild the initramfs AND the signed UKI on the ESP after editing the app.
# Run this whenever you change anything under src/, then reboot into craftboot.
#   sudo ./dist/ubuntu/rebuild.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
[[ $EUID -eq 0 ]] || { echo "Please run with sudo."; exit 1; }
"$HERE/build.sh"        # initramfs (bundles the app)
"$HERE/uki-build.sh"    # sign it into /boot/efi/EFI/craftboot/grubx64.efi
echo "Done. Reboot into craftboot to see the changes."
