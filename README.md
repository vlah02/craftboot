# craftboot

![CI](https://github.com/vlah02/craftboot/actions/workflows/ci.yml/badge.svg)

A Minecraft-style **graphical boot menu** that boots directly from firmware as its
own signed UEFI entry, shows a rotating 360° Minecraft title panorama, and hands
off to **Windows** or **Ubuntu** when you pick a "world".

It replaces the *interactive* role of GRUB with an actual program: a rotating
perspective panorama (a random Minecraft version each boot, the logo auto-matching
the world), grainy Minecraft buttons, pulsing splash text, a "Building terrain"
loading animation, and a 15-second auto-boot countdown.

> **Status: v2.1 is what's installed and working on real hardware today**
> (ASUS ROG G713PI, Ubuntu, **Secure Boot ON**) — one static C binary as
> `/init` in a signed initramfs: **179 fps** at 1920×1080, a **12 MB**
> initramfs (down from 113 MB), first frame in well under a second, boots as
> its own firmware entry `Craftboot`, hands off to both OSes seamlessly.
> **v3.0 (in progress, Milestone A complete)** rewrites craftboot as a
> standalone **UEFI application** — proven rendering, navigating, and
> zero-reboot chainloading **in QEMU/OVMF**; MOK-signing it, installing it as
> the real firmware entry, and validating on real hardware is Milestone B
> (pending), at which point it supersedes the initramfs path below. See
> [v3.0 — craftboot as a UEFI application (Milestone A)](#v30--craftboot-as-a-uefi-application-milestone-a).
> Grew out of customizing the [minegrub](https://github.com/Lxtharia/minegrub-theme)
> GRUB theme; it is now a standalone project, rewritten from Python/pygame to
> plain C for M4/M5 (see [CHANGELOG.md](CHANGELOG.md)).

---

## Demo

> Rendered by the app itself — the same code path that runs at boot, captured
> off-screen from the C renderer (`CRAFTBOOT_SHOT_SEQ`, see
> [Panorama tunables](#panorama-tunables)).

**Main menu** — navigation and the *Extras* submenu, over a rotating 360° panorama:

![craftboot main menu](docs/demo-menu.webp)

**A random Minecraft version each boot** — the logo auto-matches the world:

![panorama worlds](docs/demo-worlds.webp)

**Loading screen** shown during the handoff to the selected OS:

![loading screen](docs/demo-loading.webp)

---

## Table of contents

- [Demo](#demo)
- [Why a program instead of a GRUB theme](#why-a-program-instead-of-a-grub-theme)
- [How it works](#how-it-works)
- [v3.0 — craftboot as a UEFI application (Milestone A)](#v30--craftboot-as-a-uefi-application-milestone-a)
- [Repository layout](#repository-layout)
- [Prerequisites](#prerequisites)
- [Setup, part by part](#setup-part-by-part)
- [Development loop (QEMU)](#development-loop-qemu)
- [Testing & CI](#testing--ci)
- [Versioning](#versioning)
- [Recovery & fallback](#recovery--fallback)
- [Contributing: code](#contributing-code)
- [Contributing: new panoramas](#contributing-new-panoramas)
- [Contributing: new logos](#contributing-new-logos)
- [Panorama tunables](#panorama-tunables)
- [Porting to another distro](#porting-to-another-distro)
- [Credits & citations](#credits--citations)
- [License & trademark](#license--trademark)

---

## Why a program instead of a GRUB theme

GRUB (and every other boot menu) can only draw a *static* image — no panning
background, no pulsing splash, no animation. To get the real animated Minecraft
title screen with selectable OS entries, the menu has to be an actual program. So
craftboot is a tiny Linux userspace program that **is** the boot menu, packed into
an initramfs on a signed kernel as `/init` (PID 1), and it launches the chosen OS
itself.

---

## How it works

### The boot chain (Secure Boot compatible)

```
UEFI firmware
  └─ EFI/craftboot/shimx64.efi        (Microsoft-signed shim, copied from Ubuntu)
       └─ EFI/craftboot/grubx64.efi   (our UKI: kernel + initramfs + cmdline,
                                        signed with our own MOK key)
            └─ initramfs /init        (the craftboot binary itself, PID 1)
                 └─ menu, then: BootNext + reboot (Windows/Ubuntu) / kexec (recovery)
```

- The **UKI** (Unified Kernel Image, built by `systemd-ukify`) bundles Ubuntu's
  *already-Canonical-signed* `vmlinuz`, our custom initramfs, and the kernel
  cmdline into one PE binary, which we then sign with our **MOK** key.
- `shim` (Microsoft-signed, so the firmware trusts it) verifies our UKI against
  the MOK — so Secure Boot stays **on**, and Windows/BitLocker are untouched.
- GRUB/Ubuntu stays a **separate** firmware entry as a permanent fallback — or,
  optionally, that entry can be redirected to boot Ubuntu **directly** via its
  own signed UKI, taking GRUB out of the path entirely; see
  [Optional: boot Ubuntu directly](#optional-boot-ubuntu-directly-remove-grub-from-the-boot-path).

### The initramfs is one binary

There's no busybox, no shell, no Python interpreter in the release image.
`dist/ubuntu/build.sh` assembles a minimal initramfs containing exactly:
the statically-linked `craftboot` binary as `/init`, `/assets`,
`/boot_entries.json`, a handful of decompressed USB-HID/NVMe kernel modules
plus `/modules.list`, and (only with `DEBUG=1`) a busybox shell for
post-mortem debugging.

`/init` runs as PID 1: it mounts `proc`/`sysfs`/`devtmpfs`/`efivarfs`,
`finit_module()`s the staged kernel modules directly (no `modprobe`,
no `kmod` userspace), probes for the real root partition by ext4 UUID under
`/sys/class/block`, mounts it read-only at `/mnt`, then runs the menu. On any
failure it `sync()`s and `reboot(RB_AUTOBOOT)`s — there is no path back to a
dead console (a `DEBUG=1` image drops to `/bin/sh` instead, if present).

### Rendering — no GPU, no OpenGL, no Python

The boot environment has no Mesa/GL stack and no interpreter, so the app
renders with [`src/platform/display_drm.c`](src/platform/display_drm.c): a raw
**DRM/KMS dumb buffer** driven directly by `ioctl()`s (`SetCrtc` for scanout,
double-buffered page flips with a `DirtyFB` fallback). Keyboard input is read
straight from `/dev/input/event*` via raw **evdev**, no library. On the
desktop, `make dev` links the same core against SDL2 instead
([`src/platform/display_sdl.c`](src/platform/display_sdl.c) /
[`input_sdl.c`](src/platform/input_sdl.c)) for a fast edit-render loop — the
menu/scene/panorama code (`src/core/`) is identical either way.

Everything is plain **CPU SIMD**, no GPU: blits, 9-slice buttons, bitmap-font
text, and the panorama are hand-written fixed-point C with an AVX2 gather fast
path (`src/core/render.c`), threaded across the frame. Full-res 1920×1080
panorama: **0.78 ms/frame** (AVX2) vs 1.78 ms/frame scalar — comfortably under
one frame at the measured **179 fps** on the target hardware. `make bench` and
`make diff-pano` (byte-identical scalar-vs-AVX2 differential test) cover this.

### The panorama

The background is a real Minecraft cubemap converted to a seamless
**equirectangular** JPEG (q90) ahead of time by
[`tools/build_panorama.py`](tools/build_panorama.py) — a contributor-only
script, not part of the boot image — then rendered each frame with a
**perspective projection**: a per-pixel camera-ray → lat/lon lookup table
gathers from the equirect with **bilinear** interpolation, and the yaw
advances slowly for a smooth 360° rotation with Minecraft's characteristic
skewed sides. See [Panorama tunables](#panorama-tunables).

### The handoff

The loading screen plays, then control passes straight to the selected OS —
real syscalls, no `kexec-tools`/`efibootmgr` subprocess:

| Target                | Mechanism | |
|-----------------------|-----------|---|
| **Ubuntu**            | `efivars` **BootNext** (UCS-2 description match) + `reboot(RB_AUTOBOOT)` | ✅ seamless after firmware re-POST |
| **Windows**           | `efivars` **BootNext** ("Windows Boot Manager") + `reboot(RB_AUTOBOOT)` | ✅ seamless |
| **Ubuntu (recovery)** | `kexec_file_load()` the real signed kernel + initrd, then `reboot(LINUX_REBOOT_CMD_KEXEC)` | works, in the *Extras* submenu only |
| **UEFI**              | `OsIndications` BOOT_TO_FW_UI bit + reboot | reboot into firmware setup |

Ubuntu's *default* handoff switched from `kexec` to `BootNext` in v2.1: on the
real ROG G713PI, `kexec`-ing straight into the Ubuntu kernel skips the
firmware's own ACPI init for the ALC294 speaker amp, leaving it **silently
dead** until the next full cold boot — `BootNext` lets the firmware re-POST
normally, so the amp (and anything else ACPI-owned) comes up correctly; `kexec`
is kept working and wired to "Ubuntu (recovery mode)" in *Extras* since it's
still the faster path when audio doesn't matter.

Recovery keeps `kexec` rather than `BootNext` for a different reason: `BootNext`
can only select a firmware entry, which boots that entry's *default* kernel and
cmdline, while recovery needs a custom cmdline (`recovery nomodeset ...`) —
only `kexec_file_load()` can hand the kernel a custom cmdline in one click, and
the ACPI/audio caveat above doesn't matter in a recovery shell anyway.

Handoff is a dry-run unless craftboot is running as PID 1 (`getpid() == 1`);
pass `--live` to force it, `--dry` to force a dry-run even as init.

---

## v3.0 — craftboot as a UEFI application (Milestone A)

Everything above this section describes the **v2.1 initramfs path** — what
currently ships and runs on real hardware. In parallel, Milestone A rewrites
craftboot as a **standalone UEFI application**: instead of shipping as
`/init` inside a Linux initramfs, `craftboot.efi` is a PE32+ binary that the
firmware loads and runs directly, **before** `ExitBootServices`.

- **What it is**: the menu renders straight onto the GOP (Graphics Output
  Protocol) framebuffer while UEFI boot services are still live, and instead
  of a Linux `reboot(2)` + `BootNext` round-trip, handoff to the chosen OS
  loader is a direct **chainload** — `LoadImage`/`StartImage` on the next
  loader's `.efi` image, in the same boot, with **zero reboot**. Firmware
  state stays pristine (no kexec, no re-POST needed for the seamless path),
  which is also why this sidesteps the v2 kexec/ACPI-audio problem described
  above without needing `BootNext` either.
- **Architecture**: a platform-agnostic core (`src/core/`: `render.c`,
  `menu.c`, `assets.c`, `efivar.c`) sits behind three thin interfaces —
  [`platform/display.h`](src/platform/display.h),
  [`platform/input.h`](src/platform/input.h), and
  [`platform/plat.h`](src/platform/plat.h) — implemented by three backends:
  the new **EFI backend** (`src/efi/`: GOP display, Simple Text Input,
  Simple File System reads, TSC-based timing, `EFI_RNG_PROTOCOL`, the
  chainload/BootNext/`OsIndications` handoff, and a small freestanding
  `mini_libc` for allocation, mem/str ops, a mini `snprintf`, and
  range-reduced trig), the existing **SDL backend** (desktop `make dev`),
  and the existing **DRM/evdev backend** (the v2.1 initramfs path, still
  present and shipping).
- **Constraints in the EFI build**: scalar, single-threaded, no AVX2 —
  firmware execution has no guaranteed AVX/SSE register state to preserve
  across UEFI calls and no pthreads, so the EFI build composes each frame
  into a cached RAM back-buffer and does one blit to the write-combining GOP
  framebuffer. The host build (`make`, `make dev`) keeps the AVX2 fast path
  and threading unchanged.
- **Build + test it yourself**:
  ```bash
  make efi                    # -> build/craftboot.efi (PE32+, x86_64-w64-mingw32-gcc)
  ./tools/run-qemu-efi.sh     # boot it in QEMU/OVMF
  ```
  CI runs an `efi-build` job (warning-free PE32+ build) on every PR.
- **Status, honestly**: Milestone A = the EFI app builds, renders the menu,
  navigates it, and chainloads the next loader with zero reboot — **proven
  in QEMU/OVMF**, not yet on real hardware. It has not been signed and is
  not installed as a firmware entry anywhere. The chainload **mechanism** is
  proven in QEMU against a test target `.efi`; the default
  `boot_entries.json` Ubuntu entry points at
  `\EFI\ubuntudirect\shimx64.efi`, but the current buffer-form `LoadImage`
  hands the loaded image no device path, so shim cannot yet locate its own
  next stage — completing the real shim/UKI Ubuntu chain (device-path
  `LoadImage`) is Milestone B, not something the shipped entry does today.
  **Milestone B (pending)**: add device-path `LoadImage`, MOK-sign
  `craftboot.efi`, install it as the `Craftboot` firmware entry's loader,
  chain the MOK-signed Ubuntu UKI under Secure Boot, validate on the real
  ASUS ROG G713PI, and — only then — retire the v2.1 Linux initramfs path
  and the `dist/ubuntu` tooling.

---

## Repository layout

```
src/
  core/       render.c/.h (blits, text, panorama+AVX2), assets.c/.h (config/image/font
              loading), menu.c/.h (state machine + scene draw), efivar.c/.h (pure
              firmware-format helpers, host-tested), version.h
  platform/   display.h / input.h / plat.h (the backend interfaces); DRM+evdev ship
              as v2.1 (display_drm.c, input_evdev.c) with plat_host.c; SDL is DEV-only
              (display_sdl.c, input_sdl.c)
  efi/        the v3.0 UEFI-application backend: main.c (efi_main entrypoint), efi.h
              (protocol/type defs), mini_libc.c/.h (freestanding allocator, mem/str,
              snprintf, trig), display_efi.c (GOP), input_efi.c (Simple Text Input),
              fs.c/.h (Simple File System reads), sys.c/.h (TSC timing, RNG),
              actions_efi.c (chainload + BootNext/OsIndications handoff), plat_efi.c
  boot/       actions.c/.h — kexec_file_load, BootNext, OsIndications syscalls (v2.1)
  init/       main.c (PID-1 entrypoint), initlib.c (mounts, module loading, UUID probe)
  vendor/     stb_image.h, jsmn.h (vendored single-header libs)
assets/
  panoramas/        the 15 per-version 360° worlds, JPEG q90 (1.NN[.PP]_name.jpg)
  logos/            one wordmark logo per world (minecraft_<name>.png) + logo_map.json
  fonts/baked/       baked bitmap atlases (png + json metrics) built by tools/bake_font.py
  minecraft.otf, button*.png, dirt.png, splashes.txt
dist/ubuntu/
  build.sh          assemble the initramfs (static binary as /init + assets + modules)
  uki-setup.sh      genkey / install / --uninstall the firmware entry (MOK+shim+UKI)
  uki-build.sh      build + sign the UKI for a kernel version
  rebuild.sh        build.sh + uki-build.sh in one (the dev-deploy command)
  ubuntu-direct.sh  optional: redirect the firmware Ubuntu entry to a direct signed UKI (no GRUB)
tools/
  run-qemu.sh          boot the initramfs in QEMU/OVMF (a graphical window)
  build_panorama.py    cubemap (6 faces) -> equirectangular JPEG (contributor tool)
  bake_font.py         minecraft.otf -> baked bitmap font atlases (contributor tool)
  reencode_panoramas.py  one-time PNG->JPEG q90 migration helper
  make_demo.py         raw CRAFTBOOT_SHOT_SEQ frame dumps -> the README's demo WebPs
tests/          unit tests (t.h harness, 48 cases across 8 test_*.c files)
                + bench_pano.c, diff_pano.c, fuzz_parse.c
.github/
  workflows/ci.yml     lint, build+test+bench+diff-pano, sanitizers+fuzz, on every PR
  CODEOWNERS           @vlah02 review required repo-wide
boot_entries.json     menu structure + your real kernel paths / partition UUIDs
Makefile        ship / dev / test / bench / diff-pano / test-asan / fuzz
CHANGELOG.md
```

---

## Prerequisites

Host packages (Ubuntu/Debian names):

```bash
# build the ship binary + initramfs tooling
sudo apt install build-essential libdrm-dev zstd

# make dev (desktop preview binary, SDL2 window)
sudo apt install libsdl2-dev

# asset tools (contributor-only: panoramas, font baking, demo capture)
sudo apt install python3-pil          # + numpy for tools/build_panorama.py

# initramfs signing + boot chain
sudo apt install systemd-ukify mokutil

# QEMU testing (dev loop)
sudo apt install qemu-system-x86 ovmf
```

You also need a machine that boots via **UEFI**. The signed-entry setup assumes
Ubuntu is installed with **shim** (the standard Secure Boot setup, files under
`/boot/efi/EFI/ubuntu/`).

---

## Setup, part by part

### 1. Clone & configure your machine's values

```bash
git clone <your-fork-url> craftboot && cd craftboot
```

Edit [`boot_entries.json`](boot_entries.json) with **your** values:
- `root_uuid` — your Ubuntu root partition UUID (`findmnt -no UUID /`).
- `windows_efi_uuid` — the FAT UUID of your Windows EFI (currently informational;
  the Windows entry is resolved by matching "Windows Boot Manager" in `efivars`
  at runtime).
- `match` on every `bootnext`-type entry (the checked-in "Ubuntu" entry, and any
  others you add) — it must equal the firmware boot entry's description
  **exactly** as `efibootmgr` prints it (e.g. `Ubuntu`, `Windows Boot Manager`).
  Check yours with `efibootmgr | grep -i ubuntu` (or `| grep -i windows`); the
  checked-in `"Ubuntu"` is only a guess for a stock Ubuntu installer entry.
- the `kernel` / `initrd` / `cmdline` fields for the "Ubuntu (recovery mode)" entry.

### 2. Try it in QEMU first (no risk)

Build the initramfs and boot it in a VM — see [Development loop](#development-loop-qemu).
Nothing touches your real boot order yet. Each run picks a random world.

### 3. Generate a signing key & enroll it (MOK)

```bash
sudo ./dist/ubuntu/uki-setup.sh genkey
```

This creates an RSA key in `/var/lib/craftboot/MOK.{key,crt,der}` and starts
enrollment. Then:

1. **Reboot.**
2. At the blue **MOK Manager** ("Perform MOK management") screen:
   **Enroll MOK → Continue → Yes →** enter the password you just set.
3. It reboots into your normal system.

> 💡 The MOK Manager password screen uses the pre-boot keyboard driver. On some
> laptops (incl. the ASUS internal N-KEY keyboard) it's flaky there — use an
> **external USB keyboard** and a simple digit password. Verify afterwards:
> `sudo mokutil --test-key /var/lib/craftboot/MOK.der` → "is already enrolled".

### 4. Build, sign & install the firmware entry

```bash
sudo ./dist/ubuntu/uki-setup.sh install
```

This builds the initramfs, builds + signs the UKI, copies Ubuntu's shim into
`EFI/craftboot/`, and creates the `Craftboot` firmware boot entry. It also installs
a kernel post-install hook (`/etc/kernel/postinst.d/zz-craftboot-uki`) that
rebuilds + re-signs the UKI automatically on every kernel update.

### 5. Make GRUB hand off silently (GRUB pass-through)

Craftboot *is* the interactive menu now, so GRUB shouldn't stop to show its own
menu on top of it — otherwise you pick Ubuntu twice (once in craftboot, once
in GRUB). Make GRUB boot straight through instead:

```bash
sudo sed -i 's/^GRUB_TIMEOUT_STYLE=menu/GRUB_TIMEOUT_STYLE=hidden/; s/^GRUB_TIMEOUT=10/GRUB_TIMEOUT=0/' /etc/default/grub
sudo update-grub
```

That `sed` targets stock Ubuntu's default `/etc/default/grub` values
(`GRUB_TIMEOUT=10`, `GRUB_TIMEOUT_STYLE=menu`) — if yours were already changed,
just make sure you end up with `GRUB_TIMEOUT=0` and `GRUB_TIMEOUT_STYLE=hidden`,
then `sudo update-grub`.

> 💡 Need the GRUB menu back — older kernel, recovery, troubleshooting? Hold
> **Shift**, or tap **Esc**, during the GRUB hand-off to force it to show. A
> direct firmware **Ubuntu** pick (from the UEFI boot menu, not through
> craftboot) also boots straight through now — GRUB's interactive theme isn't
> shown on a normal boot anymore, since craftboot replaced that role.

### 6. Make it the default (optional)

The install adds `Craftboot` first in `BootOrder`. To set the order explicitly:

```bash
sudo efibootmgr                       # see the entry numbers
sudo efibootmgr -o 0007,0001,0000,... # craftboot first, then Ubuntu, Windows, ...
```

or just reorder it in your UEFI BIOS boot menu. Ubuntu and Windows remain
**separate** entries.

### Optional: boot Ubuntu directly (remove GRUB from the boot path)

[Step 5](#5-make-grub-hand-off-silently-grub-pass-through) hides GRUB's own
menu, but GRUB is still technically in the path: shim loads GRUB, GRUB loads
the Ubuntu kernel. `dist/ubuntu/ubuntu-direct.sh` goes one step further and
removes GRUB from the path completely, for the firmware **Ubuntu** entry:

```bash
sudo ./dist/ubuntu/ubuntu-direct.sh install
```

This builds a **second** signed UKI — Ubuntu's real kernel + Ubuntu's real
initramfs (not craftboot's) + `root=UUID=... ro quiet splash` — signed with
the **same** MOK key as craftboot's own entry, copies shim next to it at
`EFI/ubuntudirect/`, then does firmware-entry surgery: it deletes the existing
shim→GRUB `Ubuntu` entry and recreates an entry with the **exact same label**
(`Ubuntu`, so craftboot's `BootNext` handoff keeps matching it unchanged) that
now points at shim→direct-UKI instead. It also installs a second kernel
post-install hook (`/etc/kernel/postinst.d/zz-ubuntu-direct`) so this UKI
rebuilds on every kernel update, alongside craftboot's own hook.

End state: **three** independent firmware entries —

| Entry         | Boots |
|---------------|-------|
| **Windows**   | Windows, unchanged |
| **Ubuntu**    | Ubuntu directly — no GRUB menu, no craftboot |
| **Craftboot** | the graphical menu (this project) |

GRUB itself is **not removed or purged** — it's left installed but **dormant**:
no firmware entry points at it anymore, and its files under
`/boot/efi/EFI/ubuntu/` are untouched. That's a deliberate tradeoff: you get a
GRUB-free boot chain, but the old GRUB menu (needed for e.g. picking an older
kernel or advanced boot options) is no longer one keypress away. Two ways
back:

- **Recovery, no reinstall needed:** boot **Craftboot → Extras → "Ubuntu
  (recovery mode)"** — that's a `kexec` path straight to the real kernel, it
  doesn't depend on the direct UKI at all. Use it if a kernel update ever
  breaks the direct UKI, then re-run `sudo ./dist/ubuntu/ubuntu-direct.sh install`
  to rebuild it.
- **Break-glass, get GRUB back as its own entry** (doesn't touch the direct
  `Ubuntu` entry):
  ```bash
  sudo efibootmgr --create --disk <disk> --part <part> --label "Ubuntu (GRUB)" --loader '\EFI\ubuntu\shimx64.efi'
  ```
  (`ubuntu-direct.sh install` prints the exact command, with `<disk>`/`<part>`
  filled in, every time it runs.)

This is **opt-in** and independent of steps 1–6 above; the default,
GRUB-pass-through setup keeps working if you never run this script. To
revert entirely:

```bash
sudo ./dist/ubuntu/ubuntu-direct.sh --uninstall  # removes EFI/ubuntudirect, its hook and entry,
                                                  # and recreates the stock shim->GRUB "Ubuntu" entry
```

### Uninstall

```bash
sudo ./dist/ubuntu/uki-setup.sh --uninstall  # removes the entry, ESP files, and hook
# (the MOK key is kept; to un-enroll: sudo mokutil --delete /var/lib/craftboot/MOK.der)
```

---

## Development loop (QEMU)

Fastest inner loop — no VM, no root, a real SDL window:

```bash
make dev && ./build/craftboot-dev          # dry-run by default; --live / --dry / --assets <dir>
```

Full loop through the actual initramfs + kernel, in a QEMU window:

```bash
./dist/ubuntu/build.sh && ./tools/run-qemu.sh   # random world each run
```

Once you're happy, deploy to the real signed entry:

```bash
sudo ./dist/ubuntu/rebuild.sh && sudo reboot   # rebuild initramfs + re-sign UKI, then boot it
```

> `run-qemu.sh` runs QEMU under `env -i` because a **snap-launched terminal**
> (e.g. VS Code's) leaks `LD_LIBRARY_PATH` and breaks the system QEMU.

---

## Testing & CI

```bash
make test       # unit tests -> "ALL TESTS PASS": 48 cases across 8 files
                 # (tests/test_*.c, the tiny t.h harness)
make bench      # panorama render benchmark; fails the build if slower than
                 # CRAFTBOOT_BENCH_MAX_MS (default 3.0 ms/frame @ 1920x1080,
                 # the target-HW regression bar — see tests/bench_pano.c)
make diff-pano  # scalar-vs-AVX2 panorama render, must be byte-identical
make test-asan  # the same 48 cases, rebuilt with -fsanitize=address,undefined
make fuzz       # fuzz-lite of the efivar load-option + boot_entries.json
                 # parsers under ASan+UBSan (fixed-seed, deterministic)
```

[GitHub Actions](.github/workflows/ci.yml) runs on every pull request against
`main` (and on push to `main`, as a post-merge guard) as three jobs:

- **lint scripts + tools** — every `dist/**/*.sh` + `tools/*.sh` installer must
  parse under `bash -n`, and every `tools/*.py` asset helper must byte-compile
  (`python3 -m py_compile`), so a syntax error in a load-bearing script is
  caught in CI instead of shipping silently.
- **build + test** — the ship (`make`) and dev (`make dev`) builds must be
  warning-free (`-Wall -Wextra`; CI greps the build log for `warning:` and
  fails the job if it finds one), the ship binary must be statically linked,
  then `make test`, `make bench` (gated at **8 ms/frame** here — shared GitHub
  runners are slower than the ASUS ROG G713PI target hardware, so the CI
  ceiling is looser than the 3 ms local default; override either with the
  `CRAFTBOOT_BENCH_MAX_MS` env var), and `make diff-pano`.
- **sanitizers + fuzz** — `make test-asan` (the full 48-case suite instrumented
  with AddressSanitizer + UBSan) and `make fuzz`. Both build with
  `-fno-sanitize-recover=undefined`, so a UBSan trip aborts nonzero and fails
  the job instead of printing a diagnostic and passing.

Both jobs need an **AVX2-capable runner**: the ship binary is built
`-march=x86-64-v3` with no runtime scalar fallback, so CI checks
`/proc/cpuinfo` for `avx2` up front and fails fast if it's missing, rather
than segfaulting deep into a build.

`main` is branch-protected: a PR needs green CI and a review from
[@vlah02](.github/CODEOWNERS) (the repo-wide [CODEOWNERS](.github/CODEOWNERS)
entry) before it can merge.

---

## Versioning

`git describe --tags` is the source of truth, injected at build time
(`-DCRAFTBOOT_VERSION_GIT`, see the `Makefile`); [`src/core/version.h`](src/core/version.h)'s
`"v3.0"` is only a fallback for builds outside the Makefile (IDE indexers,
ad-hoc `gcc` invocations). Before the `v3.0` tag lands, the footer shows the
raw describe string, e.g. `Craftboot v2.1-3-gabc1234-dirty  179 fps`; after
tagging it reads `Craftboot v3.0  179 fps`. The EFI build
(`make efi`) uses the same `$(VERSION)` value, so the host and EFI footers
always agree.

To cut a release: tag, then rebuild (the version string is baked in at
compile time, so existing binaries don't retroactively pick it up).

```bash
git tag v3.0 && make clean && make && make dev && make efi
```

See [CHANGELOG.md](CHANGELOG.md) for what shipped in each tag.

---

## Recovery & fallback

- If the `Craftboot` entry ever fails, the firmware **falls through** to the next
  entry — pick **Ubuntu** in the UEFI boot menu.
- With [GRUB pass-through](#5-make-grub-hand-off-silently-grub-pass-through) set
  up, that firmware **Ubuntu** entry (and craftboot's own Ubuntu button) both
  boot straight through without stopping at GRUB's menu. Hold **Shift**, or tap
  **Esc**, during the GRUB hand-off if you need the GRUB menu itself (older
  kernel, advanced options).
- After `/init` exits (menu quit, handoff failure, any error) the initramfs
  **auto-reboots** — there's no dead console to get stuck at (a `DEBUG=1` image
  drops to a busybox shell instead, if one was staged).
- **Kernel updates** are handled by the post-install hook. If a rebuild ever fails,
  boot Ubuntu normally and run `sudo ./dist/ubuntu/rebuild.sh`.
- Prefer `kexec` for the "real" Ubuntu entry despite the audio caveat above? Change
  its `type` in `boot_entries.json` from `bootnext` to `kexec` with `kernel`/`initrd`/
  `cmdline` set, same as the recovery entry.
- If [direct Ubuntu boot](#optional-boot-ubuntu-directly-remove-grub-from-the-boot-path)
  is installed and a kernel update breaks its UKI, the firmware **Ubuntu** entry
  won't have a GRUB menu to fall back into — use **Craftboot → Extras → "Ubuntu
  (recovery mode)"** (`kexec`, doesn't need the UKI) instead, then re-run
  `sudo ./dist/ubuntu/ubuntu-direct.sh install`. The GRUB break-glass command
  (printed by that script) also still works to get an old-style GRUB entry back.

---

## Contributing: code

```bash
make            # ship binary   -> build/craftboot (static, DRM/evdev)
make dev        # desktop preview -> build/craftboot-dev (SDL2 window)
make test       # unit tests (tests/, t.h harness) -> "ALL TESTS PASS"
make bench      # panorama render benchmark
make diff-pano  # scalar-vs-AVX2 byte-identical differential test
```

Builds are `-Wall -Wextra` clean — treat any new compiler warning as a bug to
fix before sending a PR. Distro-specific boot-chain code (initramfs builder,
signing/enrollment scripts) goes under `dist/<distro>/`, alongside the
existing `dist/ubuntu/` — see
[Porting to another distro](#porting-to-another-distro); `src/` itself stays
distro-agnostic.

All of the above, plus `make test-asan` and `make fuzz`, run in CI on every
PR — see [Testing & CI](#testing--ci) for exactly what's enforced. Run them
locally before opening a PR; `main` is branch-protected on green CI.

---

## Contributing: new panoramas

Panoramas live in `assets/panoramas/` as equirectangular JPEGs (q90), **named by
Minecraft version so they sort chronologically**:

```
1.NN_name.jpg            e.g. 1.16_nether.jpg
1.21.PP_name.jpg         e.g. 1.21.04_pale_garden.jpg   (2-digit patch, base = .00)
```

The 2-digit patch padding (and `.00` for a base release like `1.21.00_tricky_trials`)
keeps them correctly ordered even under a plain `ls` — otherwise `1.21.11` sorts
ahead of `1.21.4`.

**To add one** — no C changes needed:

1. Get the 6 cubemap faces `panorama_0..5.png` — e.g. from a Minecraft "panorama"
   resource pack (see the [per-version packs on Modrinth](https://modrinth.com/resourcepacks?q=panorama)),
   under `assets/minecraft/textures/gui/title/background/`.
2. Convert to a seamless equirect JPEG:
   ```bash
   python3 tools/build_panorama.py <faces_dir> assets/panoramas/1.NN_name.jpg 2800 1400
   ```
   (needs `python3-pil` + `numpy`; a contributor-only tool, not shipped in the
   initramfs.)
3. Add the logo mapping (next section) so the world gets its wordmark.

A random world is chosen each boot (`src/core/menu.c`'s `scene_load`, via
`getrandom()`); if none load, the app falls back to a solid fill.

## Contributing: new logos

Each panorama maps **1:1** to a wordmark logo in `assets/logos/`, named
`minecraft_<name>.png` (transparent background). The mapping is a small JSON
dict, [`assets/logo_map.json`](assets/logo_map.json), keyed by the panorama's
**exact stem** (filename without `.jpg`):

```json
{
  "1.16_nether":         "minecraft_nether.png",
  "1.21.04_pale_garden": "minecraft_garden.png"
}
```

Add your panorama's stem → logo file here (`src/core/menu.c`'s `pick_logo` does
a plain substring lookup, no JSON library needed for this small a file). Anything
unmapped falls back to `minecraft_classic.png`.

---

## Panorama tunables

Field of view and render resolution are baked into `pano_create()`'s call site
in [`src/core/menu.c`](src/core/menu.c) (`scene_load`); the rotation speed and
start angle are in the `yaw` expression in `menu_run`:

| Where | Meaning |
|---|---|
| `pano_create(&eq, w, h, 140.f)` — the `140.f` argument | horizontal field of view (deg); higher = more zoomed-out / skewed sides |
| `double yaw = 0.7 + (t - t0) / 140.0;` — the `0.7` | fixed start angle, as a fraction of the turn (0–1) |
| same line — the `/ 140.0` | seconds for one full 360° rotation (higher = slower) |
| `pano_create(&eq, w, h, ...)` — `w, h` | render resolution; craftboot renders at the full framebuffer size (no downscale-then-upscale step, unlike the old Python renderer) since the AVX2 path is fast enough (0.78 ms/frame at 1920×1080) |

Capturing the README's demos uses the DEV-only env hooks in
[`src/platform/display_sdl.c`](src/platform/display_sdl.c):
`CRAFTBOOT_SHOT=path[:N]` dumps one raw XRGB frame after flip `N` then exits;
`CRAFTBOOT_SHOT_SEQ=dir:first:count` dumps every flip from `first` onward as
`dir/frame_NNNN.raw` until `count` frames are written, then exits. See
`tools/make_demo.py` for turning a capture into a WebP.

---

## Porting to another distro

Everything distro-specific lives under `dist/<distro>/` — the core binary
(`src/`) needs nothing beyond the standard C runtime + libm + pthreads + libdrm
headers at build time, and *at boot* only what its own initramfs stages. To
port to another distro, implement `dist/<distro>/build.sh` producing an
initramfs whose root contains:

- `/init` — the statically-linked `craftboot` binary (`make` in the repo root
  produces `build/craftboot`; just `install` it as `/init`).
- `/assets` — a copy of the repo's `assets/` directory.
- `/boot_entries.json` — your distro's menu config (root UUID, kernel/initrd
  paths for the recovery `kexec` entry, `bootnext`/`match` strings for the
  BootNext entries).
- `/modules.list` — one decompressed `.ko` path per line, matching the target
  keyboard/storage hardware; `/init` `finit_module()`s each line at boot (see
  `dist/ubuntu/build.sh` for the module-resolution + zstd/xz decompression
  pattern using `modprobe -S --show-depends`).

`dist/ubuntu/uki-setup.sh` and `uki-build.sh` (MOK key, `ukify`, shim copy,
firmware boot entry) are themselves fairly distro-generic — they assume shim +
systemd, which most Secure-Boot Linux distros ship — but keeping them under
`dist/ubuntu/` leaves room for a distro with a different signing story.

---

## Credits & citations

This is a **fan project** built on Mojang's Minecraft assets.

- **Origin:** grew out of the [minegrub-theme](https://github.com/Lxtharia/minegrub-theme)
  GRUB theme (the in-game font, dirt texture, splash idea, and overall look are
  descended from it).
- **Panorama worlds:** the per-version 360° cubemaps come from the community
  "*X.Y Panorama with Shaders*" resource-pack series on
  [Modrinth](https://modrinth.com/resourcepacks?q=panorama), converted to
  equirectangular JPEGs by `tools/build_panorama.py`:

  | Version | Update | World file | Modrinth pack (slug) |
  |---|---|---|---|
  | ~1.8–1.12 | Default title | `1.12_classic` | `classic-panorama-with-shaders` |
  | 1.13 | Update Aquatic | `1.13_aquatic` | `1.13-panorama-with-shaders` |
  | 1.14 | Village & Pillage | `1.14_village` | `1.14-panorama-with-shaders` |
  | 1.15 | Buzzy Bees | `1.15_bees` | `1.15-panorama-with-shaders` |
  | 1.16 | Nether Update | `1.16_nether` | `1.16-panorama-with-shaders` |
  | 1.17 | Caves & Cliffs I | `1.17_cliffs` | `1.17-panorama-with-shaders` |
  | 1.18 | Caves & Cliffs II | `1.18_caves` | `1.18-panorama` |
  | 1.19 | The Wild Update | `1.19_wild` | `1.19-panorama-shaders` |
  | 1.20 | Trails & Tales | `1.20_trails` | `trails-and-tales-panorama-with-shaders` |
  | 1.21 | Tricky Trials | `1.21.00_tricky_trials` | `1.21-panorama-with-shaders` |
  | 1.21.4 | The Garden Awakens | `1.21.04_pale_garden` | `pale-garden-panorama-with-shaders` |
  | 1.21.5 | Spring to Life | `1.21.05_spring` | `shaderpanorama1215` |
  | 1.21.6 | Chase the Skies | `1.21.06_skies` | `shaderpanorama1216` |
  | 1.21.9 | The Copper Age | `1.21.09_copper` | `shaderpanorama1219` |
  | 1.21.11 | Mounts of Mayhem | `1.21.11_mounts` | `1.21.11-panorama-with-shaders` |

  Credit to the respective Modrinth pack authors; these packs redistribute Mojang's
  title-screen panoramas.
- **Minecrafter** title font by **MadPixel** — Creative Commons, non-commercial;
  shipped with its license (`assets/fonts/Minecrafter-License.txt`).
- **Button sprites** (`button.png` / `button_highlighted.png`), the in-game font
  (`minecraft.otf`), and `dirt.png` are Mojang's default GUI assets, bundled for
  personal use.
- **Logos** (`assets/logos/`) are per-update Minecraft wordmarks, created with
  the **EaseCation 3D Text generator** ([3dtext.easecation.net](https://3dtext.easecation.net/)).
- **Vendored libraries:** [`stb_image.h`](https://github.com/nothings/stb) by
  Sean Barrett (public domain / MIT) for JPEG/PNG decoding, and
  [`jsmn.h`](https://github.com/zserge/jsmn) by Serge Zaitsev (MIT) for parsing
  `boot_entries.json` — both vendored, single-header, under `src/vendor/`
  (see [`src/vendor/README.md`](src/vendor/README.md)).

## License & trademark

The **code** in this repository (`src/`, `dist/`, `tools/`, `tests/`, `Makefile`)
is released under the **MIT License** — do what you like with it.

The **bundled assets are NOT covered by that license**: Minecraft, its fonts,
textures, panoramas, and wordmarks are property of **Mojang Studios / Microsoft**
and remain under their respective licenses/ownership. **Minecraft** is a trademark
of Mojang Studios. This project is **unofficial** and **not affiliated with, endorsed
by, or sponsored by** Mojang or Microsoft. The assets are included for personal,
non-commercial use; if you redistribute, ensure you have the right to the assets you
ship.
