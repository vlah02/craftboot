# craftboot C rewrite — design

**Date:** 2026-07-12
**Branch:** `c_implementation`
**Status:** approved

## Goal

Rewrite craftboot from Python (pygame + numpy, ~113 MB initramfs, 20 fps panorama
at half render scale) into a modular C project: one static binary that **is**
PID-1 `/init`, CPU-SIMD rendering at full native resolution, and a boot image an
order of magnitude smaller. The Python app, its runtime bundle, and the busybox
init are all retired on this branch.

## Decisions and rationale

| Decision | Choice | Why |
|---|---|---|
| Language | C (static binary) | User choice; ties with Rust/Zig on speed/size |
| Rendering | **CPU SIMD, not GPU** | Measured on this machine: GPU path (amdgpu module 6.5 MB + firmware ~10–28 MB + RADV 18 MB + libLLVM 133 MB) adds ~170 MB — bigger than today's whole image. The Ryzen 9 7945HX (32 threads, Zen 4/AVX-512) exceeds the panel's 144 Hz cap on CPU alone, so GPU buys nothing displayable. |
| Init | **Binary replaces /init entirely** | Direct syscalls replace busybox + kmod + kexec-tools + efibootmgr. Fewest moving parts, smallest image. |
| Panorama assets | **JPEG q90, 4:4:4, stored in repo** | 49 MB PNG → ~12 MB JPEG; visually fine for photographic panoramas; 4:4:4 avoids chroma smearing. Single biggest memory lever. Logos stay PNG (alpha). |
| Structure | **Modular (`src/core`, `src/platform`, `src/boot`, `src/init`, `dist/`)** | Multi-distro extensibility: all distro knowledge lives in `dist/<distro>/` scripts + config; C never hardcodes a distro path. |
| Build system | Plain Makefile | YAGNI; meson/cmake can come later without design impact. |
| DRM access | Raw ioctls (port of `app/drmkms.py`) | Kernel ABI is identical on every distro — more portable than linking each distro's libdrm. Already proven on this hardware. |
| Dependencies | `stb_image.h`, `jsmn.h` (vendored single-header) + libc | JPEG/PNG decode and JSON config. No other libraries in the shipped binary. |
| Fonts | Build-time baked bitmap atlas | `minecraft.otf` is CFF-based, which stb_truetype cannot parse; a pixel font is authentic as a bitmap anyway. `tools/bake_font.py` renders atlas + metrics once at build time. |

## Repository layout (after refactor)

```
craftboot/
  src/
    core/            distro- and OS-agnostic; pure logic + pixels
      menu.c/.h        state machine: entries, submenu stack, countdown, splash
      render.c/.h      SIMD panorama, blits, 9-slice buttons, text -> plain pixel buffer
      assets.c/.h      stb_image decode (JPEG/PNG), font atlas, config load
    platform/        tiny interfaces, one impl per backend
      display.h        open/backbuffer/flip/close (~5 fns)
      display_drm.c    dumb buffers + page-flip vsync; DirtyFB fallback
      display_sdl.c    dev-only desktop window
      input.h          poll() -> UP/DOWN/SELECT/BACK/QUIT
      input_evdev.c    raw /dev/input/event*
      input_sdl.c      dev-only
    boot/
      actions.c/.h     kexec_file_load, efivarfs BootNext, firmware-setup, reboot
    init/
      main.c           PID-1: mounts, module load, root mount, wiring
    vendor/            stb_image.h, jsmn.h
  assets/              panoramas (JPEG q90), logos (PNG), fonts + baked atlas,
                       button sprites, dirt.png, splashes.txt
  boot_entries.json    machine config (same contract as today)
  dist/
    ubuntu/            build.sh, uki-setup.sh, uki-build.sh, rebuild.sh
  tools/               host-side helpers (Python allowed, never ships):
                       build_panorama.py (outputs JPEG q90), bake_font.py, run-qemu.sh
  Makefile             `make` -> static binary; `make dev` -> SDL dev build
  build/               gitignored artifacts
```

**Portability rule:** `src/core` and `src/boot` never contain a distro-specific
path or assumption. Machine/distro specifics arrive only via `boot_entries.json`
and a build-generated `modules.list`. Supporting another distro = a new
`dist/<distro>/` script set with the same contract; zero C changes.

## Renderer

- Panorama decoded once → RGBX32 (~16 MB RAM). Precompute the projection LUT
  (per-pixel: two source rows + vertical weight + base longitude, fixed-point) —
  same math as the Python `_init_pano`.
- Per frame: 4-tap bilinear gather at **full native resolution** (no render
  scale). Row-slice across ~8 pthreads; AVX2 fast path. Compile `-march=x86-64-v3`
  (AVX2 baseline, runs in QEMU and on other machines); `-march=znver4` opt-in.
  Budget: **< 3 ms/frame** at 1080p (numpy today: ~50 ms at half res).
- Render into a cached staging buffer, one memcpy into the write-combined dumb
  buffer, page-flip on vsync (144 Hz panel). On simpledrm/QEMU where flips are
  unavailable: single buffer + DirtyFB at a 60 fps cap (today's proven path).
- Yaw advances in fixed-point; loop period / FOV / start-angle constants carry
  over from the Python tunables.
- Text: baked atlas blits with the hard drop shadow. Splash: pre-render the
  rotated string once, per-frame bilinear scale for the pulse. Buttons: 9-slice
  of `button.png` / `button_highlighted.png`. Logos: PNG with alpha, bilinear
  scaled once at init. Loading screen: tiled dirt + darken + progress bar.

## PID-1 init flow (`src/init/main.c`)

1. mount `proc`, `sysfs`, `devtmpfs`, `efivarfs`; redirect logging to `/dev/kmsg`
2. `finit_module()` each `.ko` listed in `modules.list` (ordered,
   dependency-resolved at **build time** by `dist/ubuntu/build.sh`, staged
   uncompressed — no kmod, no runtime dependency logic)
3. poll for the nvme root device (≤ ~8 s), mount root **read-only** at `/mnt`
   by UUID — a small built-in ext4-superblock UUID probe (no findfs)
4. run the menu loop → boot action
5. on app exit or fatal error: render the error on screen for 15 s → `reboot(2)`
   (never a dead console). `make DEBUG=1` images additionally stage busybox and
   drop to a shell on failure instead.

## Boot actions (`src/boot/actions.c`)

| Action | Replaced binary | Implementation |
|---|---|---|
| Ubuntu | kexec-tools | `kexec_file_load(2)` on the signed kernel + initrd from the ro-mounted root, then `reboot(LINUX_REBOOT_CMD_KEXEC)` |
| Windows | efibootmgr | scan `Boot####` efivars, match "Windows Boot Manager" (UCS-2 description), write `BootNext` (attrs NV+BS+RT; clear/restore the efivarfs immutable flag via `FS_IOC_{GET,SET}FLAGS`), `reboot(2)` |
| UEFI setup | systemctl | set the boot-to-firmware-UI bit in `OsIndications`, `reboot(2)` |

`--live` keeps today's semantics (dry-run prints the actions without executing).
The SDL dev build is always dry-run.

Entries come from `boot_entries.json` (jsmn-parsed): same schema as today —
`root_uuid`, `menus.main[]`, `menus.extras[]`, entry types
`windows | kexec | submenu | info | uefi | back`.

## Build & dist pipeline

- `make` → `build/craftboot` — static (musl-gcc if installed, else glibc
  `-static`), `-O3 -march=x86-64-v3`, links nothing but libc + pthreads.
- `make dev` → `build/craftboot-dev` — same core/menu/render objects over the
  SDL2 display/input backends, windowed, dry-run. SDL is never in the shipped
  binary.
- `make DEBUG=1` → image variant that stages busybox for a failure shell.
- `dist/ubuntu/build.sh`: `make`, then stage `/init` (the binary), `assets/`,
  `boot_entries.json`, resolved+decompressed `.ko` files + `modules.list`;
  pack cpio. `uki-build.sh` / `uki-setup.sh` / `rebuild.sh` unchanged in role,
  updated paths. `tools/run-qemu.sh` unchanged in role.
- Kernel-update hook keeps working: hook calls `dist/ubuntu/build.sh <kver>` +
  `uki-build.sh <kver>` exactly as today.

## Size & performance budget

| Component | Budget |
|---|---|
| static binary | ~0.3 MB (musl) – ~1 MB (glibc static) |
| 15 panoramas (JPEG q90 4:4:4) | ~12 MB |
| logos + atlas + sprites + splashes | ~2 MB |
| kernel modules (usbhid/hid/nvme + deps) | ~2 MB |
| **initramfs total** | **~16 MB** (today: 113 MB) |
| first frame after exec | < 150 ms (today: ~1.5–2 s python+imports) |
| panorama frame time @1080p | < 3 ms (today: ~50 ms at 0.5 scale) |

Binary reports ms/frame to kmsg for verification on real HW.

## Error handling

- Any init step failure → on-screen error + 15 s + reboot; DEBUG image drops to
  shell. No dead consoles (parity with today's behavior).
- Missing/corrupt panorama → try next random world → solid-fill fallback
  (parity with Python).
- Missing font atlas / sprites → fatal at startup with clear kmsg message
  (they are build products; their absence is a build bug).
- `kexec_file_load` failure (e.g. unsigned kernel) → error surfaced on screen,
  fall through to reboot; GRUB/Ubuntu firmware entry remains the recovery path.

## Testing

- **Dev loop:** `make dev` desktop window for visuals; unit-style checks where
  cheap (LUT golden values vs the Python implementation, JSON parse, UCS-2
  match).
- **QEMU:** existing harness (`tools/run-qemu.sh`, screendump + sendkey
  verification), forced 1920×1080.
- **Real HW:** last, via `dist/ubuntu/rebuild.sh`; frame-time + total-size
  numbers recorded in the README.

## Milestones

- **M1** — repo refactor skeleton (`src/`, `dist/`, `tools/`; assets moved to
  `assets/` and panoramas re-encoded JPEG q90) + core renderer + SDL dev window:
  panorama, logo, buttons, splash, countdown, loading screen on the desktop
  (dry-run). `app/` keeps working untouched until M5.
- **M2** — DRM + evdev backends; full menu verified in QEMU.
- **M3** — PID-1 init: mounts, modules, root probe; boots end-to-end in QEMU.
- **M4** — boot actions live on real hardware (kexec Ubuntu, BootNext Windows,
  firmware setup); perf/size numbers verified.
- **M5** — retire `app/` and `scripts/`, move panorama tooling to `tools/`,
  update README + dist scripts, final numbers.

Each milestone leaves the branch buildable and testable; `main` keeps the
working Python version until this branch is merged.

## Out of scope

- GPU rendering (documented dead end on this distro: +~170 MB Mesa/LLVM/firmware)
- Other-distro `dist/` implementations (the layout enables them; none written)
- Non-x86-64 targets; non-UEFI boot
- Meson/CMake packaging
