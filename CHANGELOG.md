# Changelog

All notable changes to craftboot are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are the
project's `git` tags (`git describe --tags` is the runtime source of truth,
see [README.md#versioning](README.md#versioning)).

## v2.1 — 2026-07-12

The C rewrite: `/init` is now one statically-linked binary — no busybox, no
Python, no interpreter in the boot image at all.

### Added
- One-binary PID-1 init (`src/init/main.c`): mounts `proc`/`sysfs`/`devtmpfs`/
  `efivarfs`, `finit_module()`s staged kernel modules directly, probes the
  real root partition by ext4 UUID, mounts it read-only, runs the menu, and
  `sync()` + `reboot(RB_AUTOBOOT)`s on any exit path — no dead console.
- CPU-SIMD panorama renderer at full framebuffer resolution: fixed-point
  bilinear perspective projection with an AVX2 gather fast path
  (`src/core/render.c`), threaded. **0.78 ms/frame** at 1920×1080 (AVX2) vs
  1.78 ms/frame scalar — verified byte-identical via `make diff-pano`.
- Baked bitmap font atlases (`tools/bake_font.py` → PNG + JSON metrics),
  replacing on-the-fly TTF rasterization.
- Raw DRM/KMS dumb-buffer display backend (`src/platform/display_drm.c`,
  double-buffered page flips + `DirtyFB` fallback) and raw evdev keyboard
  input (`src/platform/input_evdev.c`) — no library dependency in the ship
  binary beyond libdrm headers.
- `git describe --tags` baked into the binary at build time
  (`-DCRAFTBOOT_VERSION_GIT`); the footer now reads `Craftboot v2.1  179 fps`.
- Syscall-native boot actions (`src/boot/actions.c`): `kexec_file_load()` +
  `reboot(LINUX_REBOOT_CMD_KEXEC)`, `efivarfs` `BootNext`/`OsIndications`
  writes with UCS-2 load-option parsing — no `kexec-tools`/`efibootmgr`
  subprocess calls.
- `dist/ubuntu/` initramfs builder (`build.sh`) and QEMU test harness
  (`tools/run-qemu.sh`).

### Changed
- **Ubuntu's default handoff switched from `kexec` to `BootNext`.** On the
  ROG G713PI, `kexec`-ing directly into the Ubuntu kernel skips the
  firmware's own ACPI init, and the ALC294 speaker amp comes up silently dead
  until the next full cold boot (confirmed with an A/B cold-boot test).
  `BootNext` lets the firmware re-POST normally instead. `kexec` is kept
  working and is now the "Ubuntu (recovery mode)" entry under *Extras*.
- Panoramas re-encoded to JPEG q90 (`tools/reencode_panoramas.py`), replacing
  lossless PNGs.
- initramfs size: **113 MB → 12 MB** (no Python/pygame/numpy/SDL2 runtime,
  no busybox in release builds).
- Static ship binary: **~1.3 MB**.
- First rendered frame: from a multi-second Python/pygame/numpy interpreter
  startup down to effectively instant.

### Fixed
- `menu_show_error` + reboot-on-failure cover every init exit path (mount
  failure, config load failure, display open failure, handoff failure) —
  the "all-exits contract": every code path either hands off control or
  reboots, never leaves a dead screen.

## v1.0 — 2026-07-11

Final Python/pygame implementation (see `git log v1.0` for the full M1–M5
history; every commit through this tag is Python). Highlights:

- **M1 — the menu itself:** real 360° panorama background (perspective
  projection, skewed sides, tunable FOV/blur/speed/start angle), 15 official
  per-version panorama worlds with auto-matching logos, grainy Minecraft
  9-slice buttons, pulsing splash text, 15-second auto-boot countdown that
  cancels on input.
- **M2 — real handoff:** `kexec` into the signed Ubuntu kernel, `efibootmgr
  --bootnext` into Windows, UEFI firmware-setup reboot; QEMU/OVMF test
  harness.
- **M3 — no GRUB, no GPU:** raw DRM/KMS framebuffer rendering via `ctypes`
  ioctls (`app/drmkms.py`) instead of GRUB, with evdev keyboard input;
  bundled Python + pygame + numpy + SDL2 + USB-HID modules into a
  busybox-based initramfs.
- **M4 — clean boot:** `--live` handoff wired into the initramfs, USB-HID
  keyboard modules bundled so the internal ROG keyboard works pre-boot,
  auto-reboot after the app exits.
- **M5 — Secure Boot, for real:** craftboot became its own signed EFI boot
  entry — MOK key generation + enrollment, `shim` (Microsoft-signed) +
  `systemd-ukify`-built UKI signed with the MOK, a kernel post-install hook
  that rebuilds/re-signs the UKI automatically, and a `--uninstall` path.
  Demo animations added to the README.

Assets shipped as of this tag: 15 official panorama worlds (PNG), per-world
wordmark logos, the Minecrafter title font, and the stock Mojang GUI sprites
(button/dirt/in-game font) — all still in use in v2.1, just re-encoded.
