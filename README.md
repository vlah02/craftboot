# craftboot

A Minecraft-style **graphical boot menu** that runs on a tiny fast-booting Linux,
set as the default EFI entry, and hands off to Windows or Ubuntu when you pick a
"world". Panning panorama background, pulsing splash text, Minecraft buttons, and
a "Building terrain" loading animation.

> Status: **Milestone 1 — visual prototype** (runs on the desktop; boots nothing yet).

## Why this exists

GRUB (and every other boot menu) can only draw a *static* image — no panning
background, no pulsing splash. To get the real animated Minecraft menu with
selectable OS entries, the menu has to be an actual program. So craftboot is a
small Linux userspace app that *is* the boot menu, and launches the chosen OS.

## How the handoff works

| Target  | Mechanism | Seamless? |
|---------|-----------|-----------|
| Ubuntu  | `kexec` the Ubuntu kernel+initrd directly (no firmware reboot) | yes, fast |
| Windows | `efibootmgr --bootnext <win>` then reboot (firmware boots Windows) | no — a reboot happens; the loading animation is shown *before* the reboot |
| UEFI    | `systemctl reboot --firmware-setup` | reboot into firmware |

You cannot draw a Linux animation *during* the actual Windows boot (after the
reboot the firmware/Windows own the screen). So for Windows we play the loading
animation as a cosmetic transition, then reboot via BootNext.

## ⚠️ The big constraint: Secure Boot is ENABLED on this machine

This affects two things and must be decided before the boot stage (Milestone 3+):

1. **Booting our menu**: a custom default EFI entry must be *signed* with a key the
   firmware trusts (enroll our own key via MOK), or Secure Boot must be turned off.
2. **kexec into Ubuntu**: kernel *lockdown* (active under Secure Boot) blocks
   `kexec` of unsigned kernels. Options: use `kexec_file_load` with Ubuntu's
   Canonical-signed kernel, or disable Secure Boot.

**Two paths:**
- **Keep Secure Boot on** — more work: enroll a MOK signing key, sign our EFI
  image, use signed-kernel kexec. BitLocker/Windows untouched.
- **Disable Secure Boot** — simplest for dev/kexec, but if Windows uses BitLocker
  it will prompt for the recovery key once (have it ready).

_Decision pending — does not block Milestones 1-2._

## Roadmap / milestones

- [x] **M1 — Visual prototype** (`app/main.py`): panorama, splash, menu, loading
  animation. Runs windowed on the desktop. Boots nothing (safe).
- [~] **M2 — Real handoff (tested from the running desktop)**: wired into the app.
  Ubuntu = `kexec -s` (seamless, no reboot; confirmed working under Secure Boot).
  Windows = `efibootmgr --bootnext` + reboot (a reboot is unavoidable — firmware
  must start Windows). UEFI = `systemctl reboot --firmware-setup`. Dry-run by
  default; `--live` actually boots (uses sudo on the desktop, runs as root in the
  boot env). Worst case is a normal reboot.
- [ ] **M3 — Minimal Linux image**: build a small, fast-booting rootfs
  (Alpine/buildroot) that autostarts the app on the framebuffer/KMS-DRM. Test in
  **QEMU + OVMF** only. (Needs `qemu-system-x86_64`.)
- [ ] **M4 — Non-default EFI entry**: install the image and add it as a NON-default
  UEFI entry; boot it manually from the firmware boot menu on real hardware.
  Handle Secure Boot signing here.
- [ ] **M5 — Make default, with fallback**: set as default via `efibootmgr`,
  keeping GRUB as a documented fallback (firmware boot menu → GRUB). Recovery
  instructions written down.

## Safety principles

- Nothing touches the EFI boot order until M4/M5, and GRUB always stays installed
  as the fallback.
- The mini-Linux is validated in a **VM** before it ever runs on real hardware.
- Every OS-selection path has a recovery route (firmware boot menu).

## Layout

```
app/
  main.py            visual prototype (pygame)
  boot_entries.json  menu structure + the real kernel/UUID values
  assets/            Minecraft font, panorama, dirt, splashes  (reused from minegrub)
scripts/
  handoff-ubuntu.sh  kexec launcher   (M2 - stub for now)
  handoff-windows.sh BootNext+reboot  (M2 - stub for now)
boot/                minimal-Linux build (M3)
run.sh               dev helper: venv + pygame + run windowed
```

## Running the prototype (Milestone 1)

```bash
./run.sh            # sets up a venv, installs pygame, runs windowed
# or manually:
python3 app/main.py --windowed
```

Controls: **Up/Down** (or mouse) to move, **Enter/click** to select, **Esc** to go
back / quit. Selecting Windows/Ubuntu plays the loading animation, prints the
intended boot action to the terminal, and exits — it does not boot anything yet.

**Background** has two modes (`--bg photos` default, `--bg pano`):
- **photos** (default): a random, slightly blurred screenshot from
  `app/assets/backgrounds/`, slowly panning horizontally (`PAN_SPEED_X`,
  `PHOTO_BLUR`, `PHOTO_MARGIN`).
- **pano**: the seamless rotating 360° panorama built from the real cubemap
  (`panorama360.png`, tunables `VIEW_ZOOM` / `PANO_LOOP_SECONDS`).

**Logo** (priority order):
1. random PNG from `app/assets/logos/` (drop several themed logos here and one is
   picked each launch — e.g. from 3dtext.easecation.net or textcraft.net),
2. else a single `app/assets/logo.png`,
3. else a procedural Minecrafter-font logo.

Pre-made logo images usually bake in their own subtitle, so the separate subtitle
is only drawn for the procedural logo (or when you provide `app/assets/subtitle.png`
explicitly). `SUBTITLE_TEXT` (default "BOOT EDITION") controls the procedural one.
Use transparent-background PNGs so they sit over the background.

**Buttons** use the real Minecraft button sprite (`app/assets/button.png` /
`button_highlighted.png`), 9-sliced (fixed caps + stretched middle) so the
border-to-grain ratio stays correct at any size; the selected entry uses the
highlighted sprite with yellow text (Minecraft font throughout). Falls back to a
procedural grainy button if the sprites are missing.

**Auto-boot**: after `TIMEOUT_SECONDS` (15) of no input the default entry boots
automatically, with a "Joining world in N seconds" countdown (bottom-right) that
resets on any key/mouse. Default = the highlighted main entry if bootable, else
the first bootable main entry.

To regenerate the panorama from the 6 cube faces (e.g. to swap in a different
world), edit/replace `app/assets/panorama_faces/panorama_0..5.png` and run:

```bash
python3 scripts/build_panorama.py app/assets/panorama_faces app/assets/panorama360.png
```

> Note: full 3D perspective/pitch (true cubemap projection) would need an OpenGL
> renderer; this equirectangular scroll gives the seamless horizontal rotation in
> pure pygame, which stays portable to the minimal-Linux boot environment.

## Credits / licenses

- **Minecrafter** logo font by **MadPixel / PurePixel** — Creative Commons,
  non-commercial use, shipped with its license (`app/assets/fonts/Minecrafter-License.txt`).
- Panorama cube faces from **Nixinova/Original-Panorama** (the original Minecraft
  title panorama as a resource pack), converted to equirectangular by
  `scripts/build_panorama.py`.
- Minecraft button sprite (`button.png` / `button_highlighted.png`) from the
  game's default GUI assets (via InventivetalentDev/minecraft-assets); personal use.
- Minecraft in-game font and background screenshots reused from the minegrub
  theme; Minecraft is a trademark of Mojang/Microsoft — this project is unofficial,
  not affiliated with Mojang, and these assets are bundled for personal use only.
