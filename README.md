# craftboot

A Minecraft-style **graphical boot menu** that boots directly from firmware as its
own signed UEFI entry, shows a rotating 360° Minecraft title panorama, and hands
off to **Windows** or **Ubuntu** when you pick a "world".

It replaces the *interactive* role of GRUB with an actual program: a rotating
perspective panorama (a random Minecraft version each boot, the logo auto-matching
the world), grainy Minecraft buttons, pulsing splash text, a "Building terrain"
loading animation, and a 15-second auto-boot countdown.

> **Status: working on real hardware** (ASUS ROG G713PI, Ubuntu, **Secure Boot ON**).
> Boots as its own firmware entry `Craftboot`, hands off to both OSes seamlessly.
> Grew out of customizing the [minegrub](https://github.com/Lxtharia/minegrub-theme)
> GRUB theme; it is now a standalone project.

---

## Demo

> Rendered by the app itself — the same code path that runs at boot, captured off-screen.

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
- [Repository layout](#repository-layout)
- [Prerequisites](#prerequisites)
- [Setup, part by part](#setup-part-by-part)
- [Development loop (QEMU)](#development-loop-qemu)
- [Recovery & fallback](#recovery--fallback)
- [Contributing: new panoramas](#contributing-new-panoramas)
- [Contributing: new logos](#contributing-new-logos)
- [Panorama tunables](#panorama-tunables)
- [Credits & citations](#credits--citations)
- [License & trademark](#license--trademark)

---

## Why a program instead of a GRUB theme

GRUB (and every other boot menu) can only draw a *static* image — no panning
background, no pulsing splash, no animation. To get the real animated Minecraft
title screen with selectable OS entries, the menu has to be an actual program. So
craftboot is a tiny Linux userspace app that **is** the boot menu, packed into an
initramfs on a signed kernel, and it launches the chosen OS itself.

---

## How it works

### The boot chain (Secure Boot compatible)

```
UEFI firmware
  └─ EFI/craftboot/shimx64.efi        (Microsoft-signed shim, copied from Ubuntu)
       └─ EFI/craftboot/grubx64.efi   (our UKI: kernel + initramfs + cmdline,
                                        signed with our own MOK key)
            └─ initramfs /init        (busybox)
                 └─ python3 /craftboot/main.py --fb --live
                      └─ kexec Ubuntu  /  efibootmgr BootNext + reboot → Windows
```

- The **UKI** (Unified Kernel Image, built by `systemd-ukify`) bundles Ubuntu's
  *already-Canonical-signed* `vmlinuz`, our custom initramfs, and the kernel
  cmdline into one PE binary, which we then sign with our **MOK** key.
- `shim` (Microsoft-signed, so the firmware trusts it) verifies our UKI against
  the MOK — so Secure Boot stays **on**, and Windows/BitLocker are untouched.
- GRUB/Ubuntu stays a **separate** firmware entry as a permanent fallback.

### The initramfs

`boot/build.sh` assembles a minimal initramfs containing busybox, the system
`python3` + stdlib, **pygame** + **numpy** + **SDL2**, USB-HID keyboard modules
(the ROG internal keyboard is a USB HID device), `nvme`, `kexec`, `efibootmgr`,
and the app. Its `/init` mounts `proc/sys/dev`, loads the keyboard + disk modules,
mounts `efivarfs`, mounts the real Ubuntu root **read-only** at `/mnt` (so the app
can `kexec` its kernel), then launches the app.

### Rendering — no GPU, no OpenGL

The boot environment has no Mesa/GL stack, so the app renders with
[`app/drmkms.py`](app/drmkms.py): a raw **DRM/KMS dumb buffer** driven by `ctypes`
ioctls (`SetCrtc` for scanout, `DirtyFB` to flush each frame). pygame renders
offscreen under the SDL *dummy* video driver; we copy the surface to the KMS
buffer. Keyboard input is read straight from `/dev/input/event*` via **evdev**.
The same `--fb` path also works over `/dev/fb0` as a fallback. On the desktop,
`--windowed` uses a normal SDL window.

### The panorama

The background is a real Minecraft cubemap converted to a seamless
**equirectangular** strip ([`scripts/build_panorama.py`](scripts/build_panorama.py)),
then rendered each frame with a **perspective projection**: a per-pixel camera-ray
→ lat/lon lookup table gathers from the equirect with **bilinear** interpolation,
and the yaw advances slowly for a smooth 360° rotation with Minecraft's
characteristic skewed sides. Rendered at a fraction of screen res and
bilinear-upscaled for speed (CPU-only numpy). See [Panorama tunables](#panorama-tunables).

### The handoff

| Target  | Mechanism | Seamless? |
|---------|-----------|-----------|
| **Ubuntu**  | `kexec -s -l` the real signed kernel + initrd, then `kexec -e` | ✅ yes, no firmware reboot |
| **Windows** | `efibootmgr --bootnext <win>` then `reboot` | ❌ a reboot happens (firmware/Windows own the screen after); the loading animation plays *before* it |
| **UEFI**    | reboot into firmware setup | — |

Handoff is **dry-run** unless the app is launched with `--live` (it is, inside the
initramfs). On the desktop it uses `sudo`; in the boot env it runs as root.

---

## Repository layout

```
app/
  main.py             the app (pygame): panorama, menu, splash, loading, handoff
  drmkms.py           raw DRM/KMS dumb-buffer renderer (no GL) + evdev keyboard
  boot_entries.json   menu structure + your real kernel paths / partition UUIDs
  assets/
    panoramas/        the 15 per-version 360° worlds (1.NN[.PP]_name.png)
    logos/            one wordmark logo per world (minecraft_<name>.png)
    fonts/            Minecrafter title font (+ license)
    minecraft.otf     in-game font   |  button*.png  dirt.png  splashes.txt
boot/
  build.sh            assemble the initramfs (run with sudo — see gotcha below)
  uki-setup.sh        genkey / install / --uninstall the firmware entry (MOK+shim+UKI)
  uki-build.sh        build + sign the UKI for a kernel version
  rebuild.sh          build.sh + uki-build.sh in one (the dev-deploy command)
  run-qemu.sh         boot the initramfs in QEMU/OVMF (a graphical window)
scripts/
  build_panorama.py   cubemap (6 faces) → equirectangular PNG
run.sh                desktop dev helper: venv + pygame + run windowed
requirements.txt      pygame-ce, numpy
```

---

## Prerequisites

Host packages (Ubuntu/Debian names):

```bash
# app + panorama tooling
sudo apt install python3 python3-pygame python3-numpy
# (or: pip install -r requirements.txt  — pygame-ce + numpy)

# initramfs + boot chain
sudo apt install busybox-static kmod kexec-tools efibootmgr \
                 systemd-ukify mokutil

# QEMU testing (dev loop)
sudo apt install qemu-system-x86 ovmf
```

You also need a machine that boots via **UEFI**. The signed-entry setup assumes
Ubuntu is installed with **shim** (the standard Secure Boot setup, files under
`/boot/efi/EFI/ubuntu/`).

---

## Setup, part by part

> ⚠️ **Always run `boot/build.sh` (and `rebuild.sh`) with `sudo`.** The kernel
> post-install hook runs it as root, so `boot/build/` ends up root-owned; a later
> non-root build then fails on `rm -rf`.

### 1. Clone & configure your machine's values

```bash
git clone <your-fork-url> craftboot && cd craftboot
```

Edit [`app/boot_entries.json`](app/boot_entries.json) with **your** values:
- `root_uuid` — your Ubuntu root partition UUID (`findmnt -no UUID /`).
- `windows_efi_uuid` — the FAT UUID of your Windows EFI (or leave; the Windows
  entry number is resolved from `efibootmgr` at runtime).
- the `kernel` / `initrd` / `cmdline` fields for the Ubuntu entries.

### 2. Try it in QEMU first (no risk)

Build the initramfs and boot it in a VM — see [Development loop](#development-loop-qemu).
Nothing touches your real boot order yet. Each run picks a random world.

### 3. Generate a signing key & enroll it (MOK)

```bash
sudo ./boot/uki-setup.sh genkey
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
sudo ./boot/uki-setup.sh install
```

This builds the initramfs, builds + signs the UKI, copies Ubuntu's shim into
`EFI/craftboot/`, and creates the `Craftboot` firmware boot entry. It also installs
a kernel post-install hook (`/etc/kernel/postinst.d/zz-craftboot-uki`) that
rebuilds + re-signs the UKI automatically on every kernel update.

### 5. Make it the default (optional)

The install adds `Craftboot` first in `BootOrder`. To set the order explicitly:

```bash
sudo efibootmgr                       # see the entry numbers
sudo efibootmgr -o 0007,0001,0000,... # craftboot first, then Ubuntu, Windows, ...
```

or just reorder it in your UEFI BIOS boot menu. Ubuntu and Windows remain
**separate** entries.

### Uninstall

```bash
sudo ./boot/uki-setup.sh --uninstall  # removes the entry, ESP files, and hook
# (the MOK key is kept; to un-enroll: sudo mokutil --delete /var/lib/craftboot/MOK.der)
```

---

## Development loop (QEMU)

```bash
# edit app/ ...
sudo ./boot/build.sh && ./boot/run-qemu.sh     # preview in a QEMU window (random world each run)
```

Once you're happy, deploy to the real signed entry:

```bash
sudo ./boot/rebuild.sh && sudo reboot          # rebuild initramfs + re-sign UKI, then boot it
```

To iterate on just the app visually on your desktop (fast, no VM):

```bash
./run.sh                    # venv + pygame + windowed
python3 app/main.py --windowed
```

> `run-qemu.sh` runs QEMU under `env -i` because a **snap-launched terminal**
> (e.g. VS Code's) leaks `LD_LIBRARY_PATH` and breaks the system QEMU.

---

## Recovery & fallback

- If the `Craftboot` entry ever fails, the firmware **falls through** to the next
  entry — pick **Ubuntu** in the UEFI boot menu.
- After the app exits (or on error) the initramfs **auto-reboots**, so you never
  get stuck at a dead console.
- **Kernel updates** are handled by the post-install hook. If a rebuild ever fails,
  boot Ubuntu normally and run `sudo ./boot/rebuild.sh`.

---

## Contributing: new panoramas

Panoramas live in `app/assets/panoramas/` as equirectangular PNGs, **named by
Minecraft version so they sort chronologically**:

```
1.NN_name.png            e.g. 1.16_nether.png
1.21.PP_name.png         e.g. 1.21.04_pale_garden.png   (2-digit patch, base = .00)
```

The 2-digit patch padding (and `.00` for a base release like `1.21.00_tricky_trials`)
keeps them correctly ordered even under a plain `ls` — otherwise `1.21.11` sorts
ahead of `1.21.4`.

**To add one:**

1. Get the 6 cubemap faces `panorama_0..5.png` — e.g. from a Minecraft "panorama"
   resource pack (see the [per-version packs on Modrinth](https://modrinth.com/resourcepacks?q=panorama)),
   under `assets/minecraft/textures/gui/title/background/`.
2. Convert to a seamless equirect:
   ```bash
   python3 scripts/build_panorama.py <faces_dir> app/assets/panoramas/1.NN_name.png 2800 1400
   ```
3. Add the logo mapping (next section) so the world gets its wordmark.

A random world is chosen each boot; if none load, the app falls back to a solid fill.

## Contributing: new logos

Each panorama maps **1:1** to a wordmark logo in `app/assets/logos/`, named
`minecraft_<name>.png` (transparent background). The mapping is an explicit dict
`LOGO_FOR_BG` at the top of [`app/main.py`](app/main.py), keyed by the panorama's
**exact stem** (filename without `.png`):

```python
LOGO_FOR_BG = {
    "1.16_nether":          "minecraft_nether.png",
    "1.21.04_pale_garden":  "minecraft_garden.png",
    ...
}
CLASSIC_LOGO = "minecraft_classic.png"   # fallback if a world has no entry
```

Add your panorama's stem → logo file here. Anything unmapped falls back to
`minecraft_classic.png`.

---

## Panorama tunables

At the top of [`app/main.py`](app/main.py):

| Constant | Meaning |
|---|---|
| `PANO_FOV` | horizontal field of view (deg); higher = more zoomed-out / skewed sides |
| `PANO_BLUR` | equirect box-blur radius; `0` = sharp |
| `PANO_LOOP_SECONDS` | seconds for one full 360° rotation (higher = slower) |
| `PANO_START` | fixed start angle as a fraction of the turn (0–1) |
| `PANO_RENDER_SCALE` | render fraction of screen res, then bilinear-upscale. Bilinear sampling is CPU-heavy: `0.5` ≈ 30 fps, `1.0` ≈ 5 fps at 1080p |

---

## Credits & citations

This is a **fan project** built on Mojang's Minecraft assets.

- **Origin:** grew out of the [minegrub-theme](https://github.com/Lxtharia/minegrub-theme)
  GRUB theme (the in-game font, dirt texture, splash idea, and overall look are
  descended from it).
- **Panorama worlds:** the per-version 360° cubemaps come from the community
  "*X.Y Panorama with Shaders*" resource-pack series on
  [Modrinth](https://modrinth.com/resourcepacks?q=panorama), converted to
  equirectangular by `scripts/build_panorama.py`:

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
  shipped with its license (`app/assets/fonts/Minecrafter-License.txt`).
- **Button sprites** (`button.png` / `button_highlighted.png`), the in-game font
  (`minecraft.otf`), and `dirt.png` are Mojang's default GUI assets, bundled for
  personal use.
- **Logos** (`app/assets/logos/`) are per-update Minecraft wordmarks, created with
  the **EaseCation 3D Text generator** ([3dtext.easecation.net](https://3dtext.easecation.net/)).

## License & trademark

The **code** in this repository (`app/*.py`, `boot/*.sh`, `scripts/*.py`) is
released under the **MIT License** — do what you like with it.

The **bundled assets are NOT covered by that license**: Minecraft, its fonts,
textures, panoramas, and wordmarks are property of **Mojang Studios / Microsoft**
and remain under their respective licenses/ownership. **Minecraft** is a trademark
of Mojang Studios. This project is **unofficial** and **not affiliated with, endorsed
by, or sponsored by** Mojang or Microsoft. The assets are included for personal,
non-commercial use; if you redistribute, ensure you have the right to the assets you
ship.
