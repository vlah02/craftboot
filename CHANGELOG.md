# Changelog

All notable changes to craftboot are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are the
project's `git` tags (`git describe --tags` is the runtime source of truth,
see [README.md#versioning](README.md#versioning)).

## v3.0 — 2026-07-15

craftboot is now a standalone **UEFI application**: it runs before
`ExitBootServices` as its own MOK-signed firmware entry, renders on the GOP
framebuffer, and hands off by **chainloading** the next loader in the same boot
— zero reboot, firmware state untouched. Installed, promoted to default, and
validated on real hardware (ASUS ROG G713PI, Ubuntu, Secure Boot ON). The
v1/v2 Linux initramfs path is **retired**.

### Added
- Standalone UEFI application (`make efi` -> PE32+ via mingw): GOP display,
  Simple Text Input, Simple File System asset loading, TSC timing,
  `EFI_RNG_PROTOCOL`, and a freestanding `mini_libc` (allocator, mem/str ops,
  mini `snprintf`, range-reduced trig).
- Zero-reboot handoff by **device-path** `LoadImage`/`StartImage`. The device-path
  form is required: shim resolves its own next stage relative to where it was
  loaded from, and an image loaded from a memory buffer has none.
- `dist/ubuntu/efi-install.sh` — MOK genkey/enroll, sbsign, shim install,
  recovery UKI, MOK-signed Memtest86+, firmware entry, kernel hook, plus
  `--promote` / `--uninstall` / `--print`.
- `src/efi/sbat.c`: a `.sbat` section placed by the **linker** (inside
  `SizeOfImage`), which shim 15.8 requires of any second stage.
- Multi-core panorama render via **MP Services** `StartupAllAPs` with
  work-stealing row slices, each AP enabling SIMD on its own core.
- Real Minecraft cubemaps: `tools/fetch_panoramas.py` pulls each version's 6
  faces from Mojang's manifest/asset index and reprojects them to 4096x2048
  equirects.
- `fb_blur()` — uniform screen-space box blur, applied only to the low-res
  1.12 classic theme, with unit tests.
- Panorama downward **pitch** parameter on `pano_create()`, with a test that
  asserts the sampled latitude directly.
- Config schema v3: entry types `chainload`/`bootnext`/`uefi`/`submenu`/
  `info`/`back` (dropped `kexec`/`windows`); buffer-based asset decoding
  (`*_mem`) so assets can load from an ESP via Simple File System.
- `core/efivar.c` pure firmware-format helpers (load-option description
  decode, `OsIndications` read-modify-write, `Boot####` name parse) with
  host-side unit tests; mini `snprintf` host-tested.
- `tools/run-qemu-efi.sh` QEMU/OVMF harness (headless boot, HMP screendump,
  sendkey).
- CI `efi-build` job asserting a warning-free PE32+ image **with** a `.sbat`
  section and **no** trailing COFF symbol overlay; `boot_entries.json` JSON
  validation; `craftboot.efi` uploaded as an artifact.
- `.github/workflows/release.yml` publishes `craftboot.efi` on a `v*` tag.

### Changed
- Present via GOP `Blt(BufferToVideo)` instead of writing the framebuffer
  directly. The GOP framebuffer is mapped **uncached** on the real GPU
  (~22 MB/s), which made a full-screen present take 372 ms; `Blt` drops it to
  ~6 ms.
- Enable SIMD at entry (`CR4.OSFXSR|OSXMMEXCPT|OSXSAVE`, `XCR0 = x87|SSE|AVX`)
  and build the EFI app `-O2 -mavx2`. Firmware leaves `CR4.OSXSAVE` clear, so
  AVX2 would otherwise `#UD`.
- Panorama camera: field of view 85 -> 140 degrees with a 30-degree downward
  pitch.
- `src/core/version.h` fallback bumped to `"v3.0"`; the EFI build reuses the
  host `$(VERSION)` (`git describe`) so both footers agree.
- `make` now builds the UEFI application; there is no host binary.

### Removed
- The v2 Linux backend: `src/init/` (PID-1 entry, mounts, module loading,
  UUID probe), `src/boot/actions.c` (kexec/BootNext syscalls),
  `display_drm.c`/`input_evdev.c`, and the SDL dev backend.
- The initramfs/UKI tooling it served: `dist/ubuntu/build.sh`, `rebuild.sh`,
  `uki-setup.sh`, `uki-build.sh`, `tools/run-qemu.sh`, `tools/make_demo.py`,
  and the completed one-time `tools/reencode_panoramas.py`.
- `m_fabsf()`, a duplicate of the `fabsf()` the renderer actually calls.

### Fixed
- The original motivation: `kexec` into Ubuntu skipped the firmware's ACPI init
  for the ALC294 speaker amp, leaving it silently dead until a cold boot.
  Chainloading before `ExitBootServices` avoids both `kexec` and the
  `BootNext` re-POST that worked around it.
- shim refusing the MOK-signed image — "Security Violation" (no `.sbat`
  section) and "gaps between PE/COFF sections" (mingw's trailing symbol table,
  now stripped with `objcopy --strip-all`).
- `cp -a` onto the FAT ESP failed on ownership and aborted the installer under
  `set -e`; uses `cp -r` now.
## v2.1 post-tag hardening (shipped in v3.0)

Landed after the `v2.1` tag; kept here until the next tag cuts a release.

### Added
- **CI**: GitHub Actions (`.github/workflows/ci.yml`) — a `lint scripts +
  tools` job (`bash -n` on every `dist/**/*.sh` + `tools/*.sh`, `py_compile`
  on every `tools/*.py`, so a syntax error in a load-bearing installer/asset
  helper is caught instead of shipping silently), a `build + test` job
  (warning-free ship + dev build, static-link check, `make test`, `make bench`
  perf gate, `make diff-pano` scalar-vs-AVX2 differential) and a
  `sanitizers + fuzz` job (`make test-asan` = the full suite under
  ASan+UBSan, `make fuzz` = fuzz-lite of the efivar + config parsers). A
  repo-wide [`.github/CODEOWNERS`](.github/CODEOWNERS) (`@vlah02`); `main` is
  now branch-protected on green CI plus CODEOWNERS review.
- `dist/ubuntu/ubuntu-direct.sh` (opt-in): redirects the firmware `Ubuntu`
  entry to boot Ubuntu **directly** via a second signed UKI (Ubuntu's real
  kernel + real initramfs, same MOK key as craftboot's own entry) — no GRUB
  menu in that path at all, while keeping the entry's label exactly `Ubuntu`
  so craftboot's `BootNext` handoff keeps matching it unchanged. GRUB is left
  installed but dormant (no entry points at it); a printed break-glass
  `efibootmgr` command and the existing "Ubuntu (recovery mode)" kexec entry
  both still reach it if a kernel update ever breaks the direct UKI.

### Changed
- Test coverage expanded to **48 cases across 8 files**: an ext4-UUID root
  probe suite (`tests/test_uuid.c`), efivar value-builder tests (BootNext /
  OsIndications byte layout, `tests/test_actions.c`), blit bounds-safety tests
  (`tests/test_blit.c`), and a fixed-seed fuzz-lite harness
  (`tests/fuzz_parse.c`) hammering the efivar load-option and
  `boot_entries.json` parsers under ASan+UBSan. The latest round adds
  `action_execute()` dry-run dispatch + kexec root-path assembly, the
  `config_load()` 8-entry cap, `ms_default_entry()` fall-through when the
  highlighted entry is non-bootable (and the no-bootable NULL case), and
  degenerate render inputs (empty/over-long `text_render`, zero-width
  `img_scaled`, non-16:9 panorama, per-channel `mix_xrgb`).
- **CI sanitizer gate made actually-blocking**: the ASan/UBSan builds now
  compile with `-fno-sanitize-recover=undefined`. UBSan defaults to
  recoverable mode — it printed a `runtime error:` line on undefined behavior
  but exited 0, so `make test-asan` / `make fuzz` would have stayed green
  through a real UB regression. The flag makes any UB trip abort nonzero.
- Menu polish: the footer now shows a live, smoothed fps reading immediately
  instead of a blank first few frames; the panorama's splash rotation
  direction was corrected to match Minecraft's own title screen.
- Demos re-cut at 60fps with a continuous-pan world montage — one unbroken
  pan across four worlds instead of three clips that each restarted the pan.
- Five rounds of PR-review hardening across the C rewrite: self-contained
  headers, malloc/seek/EINTR-safe I/O, non-ASCII efivar-description
  rejection, clean SDL teardown on exit, an empty-submenu guard against
  `SIGFPE`, a runtime AVX2 flip-probe, and assorted bounds/rounding fixes.

### Fixed
- MOK enrollment check: under `set -o pipefail`, `mokutil`'s non-zero exit
  status could mask an already-enrolled key as "not enrolled"; the check now
  captures `mokutil`'s output before piping it to `grep`.
- Repo hygiene: untracked internal process docs, dropped stray Python-era
  leftovers, moved the vendored kernel stash under `dist/ubuntu/`.

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
- Guard `ms_move`/`ms_select` against an empty menu (e.g. a submenu with no
  entries in `boot_entries.json`) — previously a `SIGFPE` on the `% n` wrap,
  now a safe no-op.

### Docs
- Setup now documents **GRUB pass-through**: hide GRUB's own menu after
  installing the firmware entry (`GRUB_TIMEOUT=0` / `GRUB_TIMEOUT_STYLE=hidden`,
  `update-grub`) so craftboot is the only interactive menu on a normal boot;
  Shift/Esc still forces the GRUB menu back when needed.
- Documented that a `bootnext` entry's `match` value must equal the firmware
  boot entry's description exactly as `efibootmgr` prints it, and how to check
  it (`efibootmgr | grep -i <os>`).
- Explained why recovery keeps `kexec` instead of `BootNext`: `BootNext` can
  only boot a firmware entry's default kernel/cmdline, and recovery needs a
  custom cmdline that only `kexec_file_load()` can hand over directly.
- Clean-version demo recapture (footer reads `Craftboot v2.1` instead of
  `v2.1-dirty`); dropped a stale `--windowed` flag and a Milestone-1-era
  `boot_entries.json` comment.

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
