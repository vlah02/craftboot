#!/usr/bin/env python3
"""
craftboot - a Minecraft-style graphical boot menu (Milestone 1: visuals only).

This prototype renders the menu, the panning panorama, the pulsing splash text,
and the "Building terrain" loading animation. It does NOT boot anything yet:
selecting Windows/Ubuntu just plays the loading animation and prints the action.
Handoff (kexec / efibootmgr) comes in a later milestone.

Run on your desktop for development:
    python3 app/main.py --windowed
    python3 app/main.py                # fullscreen

Controls: Up/Down or mouse to move, Enter/click to select, Esc to go back / quit.
"""
import json
import math
import os
import random
import sys

import pygame

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")
FONT_PATH = os.path.join(ASSETS, "minecraft.otf")
LOGO_FONT_PATH = os.path.join(ASSETS, "fonts", "Minecrafter.Reg.ttf")
LOGO_IMG_PATH = os.path.join(ASSETS, "logo.png")          # optional pre-made 3D logo (textcraft.net)
SUBTITLE_IMG_PATH = os.path.join(ASSETS, "subtitle.png")  # optional pre-made subtitle image
SUBTITLE_TEXT = "BOOT EDITION"                            # rendered if no subtitle.png present

# Background modes:
#   "photos" (default) = random blurred screenshot, slow horizontal pan.
#   "pano"             = seamless rotating 360 panorama (panorama360.png).
PAN_SPEED_X = 0.28           # eased horizontal pan speed (photos mode)
PHOTO_MARGIN = 1.5           # scale factor over screen size = how far it pans
PHOTO_BLUR = 5               # background blur radius ("a little bit blurred")
VIEW_ZOOM = 2.4              # pano mode vertical zoom
PANO_LOOP_SECONDS = 60.0     # pano mode full-rotation time


def blur_surface(surf, radius):
    """Mild blur; uses pygame-ce's gaussian_blur, falls back to a scale trick."""
    if radius <= 0:
        return surf
    try:
        return pygame.transform.gaussian_blur(surf, radius)
    except (AttributeError, pygame.error, TypeError):
        w, h = surf.get_size()
        small = pygame.transform.smoothscale(surf, (max(1, w // 4), max(1, h // 4)))
        return pygame.transform.smoothscale(small, (w, h))

# ---- Minecraft-ish palette -------------------------------------------------
WHITE = (255, 255, 255)
SHADOW = (63, 63, 63)
SPLASH_YELLOW = (255, 255, 0)
SPLASH_SHADOW = (60, 60, 0)
BTN_FILL = (110, 110, 110)
BTN_FILL_HOVER = (130, 138, 130)
BTN_TOP = (160, 160, 160)
BTN_BOTTOM = (70, 70, 70)
BTN_BORDER = (18, 18, 18)
LOAD_BAR_BG = (128, 128, 128)
LOAD_BAR_FILL = (128, 255, 128)


def load_config():
    with open(os.path.join(HERE, "boot_entries.json")) as f:
        return json.load(f)


def load_splashes():
    try:
        with open(os.path.join(ASSETS, "splashes.txt")) as f:
            lines = [ln.strip() for ln in f if ln.strip()]
        return lines or ["craftboot"]
    except OSError:
        return ["craftboot"]


class Fonts:
    """Lazily built font cache at the sizes we need (scaled to screen height)."""

    def __init__(self, h):
        base = max(12, int(h * 0.030))
        self.small = self._f(base)
        self.button = self._f(int(base * 1.35))
        self.title = self._f(int(base * 3.2))
        self.splash = self._f(int(base * 1.2))
        self.load = self._f(int(base * 1.1))

    @staticmethod
    def _f(size):
        if os.path.exists(FONT_PATH):
            return pygame.font.Font(FONT_PATH, size)
        return pygame.font.SysFont("monospace", size)


def render_shadowed(font, text, color=WHITE, shadow=SHADOW, off=2):
    """Minecraft text = colored glyphs with a hard drop shadow."""
    top = font.render(text, True, color)
    bot = font.render(text, True, shadow)
    surf = pygame.Surface((top.get_width() + off, top.get_height() + off), pygame.SRCALPHA)
    surf.blit(bot, (off, off))
    surf.blit(top, (0, 0))
    return surf


def _stone_texture(w, h):
    """Procedural blocky grey 'stone' fill for the logo face."""
    tex = pygame.Surface((w, h), pygame.SRCALPHA)
    block = 4
    for y in range(0, h, block):
        for x in range(0, w, block):
            v = max(70, min(200, 145 + random.randint(-32, 42)))
            tex.fill((v, v, v, 255), (x, y, block, block))
    return tex


def make_logo(text, size):
    """Minecraft-title-style wordmark: stone-textured face, black outline, 3D depth."""
    if os.path.exists(LOGO_FONT_PATH):
        font = pygame.font.Font(LOGO_FONT_PATH, size)
    elif os.path.exists(FONT_PATH):
        font = pygame.font.Font(FONT_PATH, size)
    else:
        font = pygame.font.SysFont("monospace", size)
    mask = font.render(text, True, (255, 255, 255))   # glyph shape (per-pixel alpha)
    black = font.render(text, True, (0, 0, 0))
    dark = font.render(text, True, (60, 60, 60))
    w, h = mask.get_size()
    depth = max(3, size // 8)
    outline = max(2, size // 22)
    pad = depth + outline + 10
    ox = oy = pad
    surf = pygame.Surface((w + pad * 2, h + pad * 2), pygame.SRCALPHA)
    # stone-textured face = stone texture masked by the glyph alpha
    face = _stone_texture(w, h)
    face.blit(mask, (0, 0), special_flags=pygame.BLEND_RGBA_MULT)
    # 3D extrusion: dark copies stepping down-right
    for d in range(depth, 0, -1):
        surf.blit(dark, (ox + d, oy + d))
    # thick black outline around the base position
    for dx in range(-outline, outline + 1):
        for dy in range(-outline, outline + 1):
            if dx * dx + dy * dy <= outline * outline:
                surf.blit(black, (ox + dx, oy + dy))
    surf.blit(face, (ox, oy))
    return surf


class Panorama:
    """Rotating 360 panorama (from the real Minecraft cubemap, as a seamless
    equirectangular scroll). Falls back to a drifting screenshot if that PNG
    is missing."""

    def __init__(self, screen_size, mode="photos"):
        self.sw, self.sh = screen_size
        self.t = 0.0
        pano = os.path.join(ASSETS, "panorama360.png")
        strip = None
        if mode == "pano" and os.path.exists(pano):
            try:
                strip = pygame.image.load(pano).convert()
            except pygame.error:
                strip = None
        if strip is not None:
            self.mode = "pano"
            scaled_h = int(self.sh * VIEW_ZOOM)
            scale = scaled_h / strip.get_height()
            self.strip = pygame.transform.smoothscale(
                strip, (int(strip.get_width() * scale), scaled_h)
            )
            self.strip_w = self.strip.get_width()
            self.max_y = max(0, self.strip.get_height() - self.sh)
            self.speed = self.strip_w / PANO_LOOP_SECONDS
            self.x = 0.0
            print("[craftboot] background: 360 panorama")
        else:
            self.mode = "drift"
            self._init_drift()

    # -- 360 panorama --------------------------------------------------------
    def _draw_pano(self, surface):
        yc = self.max_y * 0.5
        y = -int(max(0, min(self.max_y, yc + math.sin(self.t * 0.15) * self.max_y * 0.4)))
        x0 = -int(self.x)
        surface.blit(self.strip, (x0, y))
        if x0 + self.strip_w < self.sw:  # wrap seam
            surface.blit(self.strip, (x0 + self.strip_w, y))

    # -- fallback: drifting screenshot --------------------------------------
    def _init_drift(self):
        img = self._pick_image()
        cover = max(self.sw / img.get_width(), self.sh / img.get_height())
        scale = cover * PHOTO_MARGIN
        img = pygame.transform.smoothscale(
            img, (int(img.get_width() * scale), int(img.get_height() * scale))
        )
        self.img = blur_surface(img, PHOTO_BLUR)
        self.slack_x = max(1, self.img.get_width() - self.sw)
        self.slack_y = max(1, self.img.get_height() - self.sh)

    def _pick_image(self):
        candidates = []
        bgdir = os.path.join(ASSETS, "backgrounds")
        if os.path.isdir(bgdir):
            candidates = [
                os.path.join(bgdir, f) for f in os.listdir(bgdir)
                if f.lower().endswith((".png", ".jpg", ".jpeg"))
            ]
        random.shuffle(candidates)
        candidates.append(os.path.join(ASSETS, "panorama.png"))
        for path in candidates:
            if os.path.exists(path):
                try:
                    print(f"[craftboot] background: {os.path.basename(path)}")
                    return pygame.image.load(path).convert()
                except pygame.error:
                    continue
        surf = pygame.Surface((self.sw, self.sh))
        surf.fill((90, 120, 160))
        return surf

    def _draw_drift(self, surface):
        # slow horizontal pan (eased ping-pong), vertical held at centre
        fx = (1 - math.cos(self.t * PAN_SPEED_X)) / 2
        y = -(self.slack_y // 2)
        surface.blit(self.img, (-int(fx * self.slack_x), y))

    # -- shared --------------------------------------------------------------
    def update(self, dt):
        self.t += dt
        if self.mode == "pano":
            self.x = (self.x + dt * self.speed) % self.strip_w

    def draw(self, surface):
        if self.mode == "pano":
            self._draw_pano(surface)
        else:
            self._draw_drift(surface)


_BTN_TEX = {}


def make_button_texture(w, h, selected):
    """Minecraft-style button: vertical grey gradient + faint noise + bevel/border."""
    top = (172, 172, 172) if selected else (152, 152, 152)
    bot = (104, 104, 104) if selected else (92, 92, 92)
    surf = pygame.Surface((w, h))
    for yy in range(h):
        f = yy / max(1, h - 1)
        surf.fill(tuple(int(top[i] * (1 - f) + bot[i] * f) for i in range(3)), (0, yy, w, 1))
    # faint speckle so it reads as a textured surface, not flat
    for _ in range(max(30, w * h // 45)):
        nx, ny = random.randint(0, w - 1), random.randint(0, h - 1)
        c = 255 if random.random() < 0.5 else 0
        surf.blit(_speckle(c), (nx, ny))
    # bevel: light top/left, dark bottom/right, then black border
    pygame.draw.line(surf, (205, 205, 205), (1, 1), (w - 2, 1))
    pygame.draw.line(surf, (205, 205, 205), (1, 1), (1, h - 2))
    pygame.draw.line(surf, (60, 60, 60), (1, h - 2), (w - 2, h - 2))
    pygame.draw.line(surf, (60, 60, 60), (w - 2, 1), (w - 2, h - 2))
    pygame.draw.rect(surf, (16, 16, 16), surf.get_rect(), width=2)
    return surf


def _speckle(c):
    s = pygame.Surface((1, 1), pygame.SRCALPHA)
    s.fill((c, c, c, 26))
    return s


def draw_button(surface, rect, surf_text, selected):
    key = (rect.width, rect.height, selected)
    tex = _BTN_TEX.get(key)
    if tex is None:
        tex = make_button_texture(rect.width, rect.height, selected)
        _BTN_TEX[key] = tex
    surface.blit(tex, rect.topleft)
    if selected:
        pygame.draw.rect(surface, WHITE, rect.inflate(4, 4), width=2)
    surface.blit(surf_text, surf_text.get_rect(center=rect.center))


class Menu:
    def __init__(self, screen, config, fonts):
        self.screen = screen
        self.config = config
        self.fonts = fonts
        self.sw, self.sh = screen.get_size()
        self.stack = ["main"]
        self.index = 0
        self.splash_text = random.choice(load_splashes())
        self._btn_cache = {}
        self._rects = []
        self._logo = None
        self._subtitle = False  # False = not built yet; may resolve to a surface or None
        self._layout()

    # ---- layout -----------------------------------------------------------
    @property
    def entries(self):
        return self.config["menus"][self.stack[-1]]

    def _layout(self):
        n = len(self.entries)
        bw = min(int(self.sw * 0.44), 620)
        bh = max(34, int(self.sh * 0.085))
        gap = int(bh * 0.35)
        total = n * bh + (n - 1) * gap
        top = int(self.sh * 0.52)
        cx = self.sw // 2
        self._rects = [
            pygame.Rect(cx - bw // 2, top + i * (bh + gap), bw, bh) for i in range(n)
        ]

    def _button_surf(self, label, selected):
        key = (label, selected)
        if key not in self._btn_cache:
            color = WHITE
            self._btn_cache[key] = render_shadowed(self.fonts.button, label, color)
        return self._btn_cache[key]

    # ---- input ------------------------------------------------------------
    def move(self, delta):
        self.index = (self.index + delta) % len(self.entries)

    def point(self, pos):
        for i, r in enumerate(self._rects):
            if r.collidepoint(pos):
                self.index = i
                return True
        return False

    def select(self):
        """Return an action dict, or None to stay in the menu."""
        entry = self.entries[self.index]
        t = entry["type"]
        if t == "submenu":
            self.stack.append(entry["target"])
            self.index = 0
            self._layout()
            return None
        if t == "back":
            if len(self.stack) > 1:
                self.stack.pop()
                self.index = 0
                self._layout()
            return None
        return entry  # bootable / info / uefi -> caller handles

    def back(self):
        if len(self.stack) > 1:
            self.stack.pop()
            self.index = 0
            self._layout()
            return True
        return False

    # ---- draw -------------------------------------------------------------
    def _make_logo_surf(self):
        if os.path.exists(LOGO_IMG_PATH):
            try:
                img = pygame.image.load(LOGO_IMG_PATH).convert_alpha()
                tw = int(self.sw * 0.55)
                s = tw / img.get_width()
                return pygame.transform.smoothscale(img, (tw, int(img.get_height() * s)))
            except pygame.error:
                pass
        return make_logo("CRAFTBOOT", int(self.sh * 0.13))

    def _make_subtitle_surf(self):
        if os.path.exists(SUBTITLE_IMG_PATH):
            try:
                img = pygame.image.load(SUBTITLE_IMG_PATH).convert_alpha()
                tw = int(self.sw * 0.22)
                s = tw / img.get_width()
                return pygame.transform.smoothscale(img, (tw, int(img.get_height() * s)))
            except pygame.error:
                pass
        if SUBTITLE_TEXT:
            return make_logo(SUBTITLE_TEXT, int(self.sh * 0.05))
        return None

    def draw_title(self, surface):
        if self._logo is None:
            self._logo = self._make_logo_surf()
        lr = self._logo.get_rect(center=(self.sw // 2, int(self.sh * 0.22)))
        surface.blit(self._logo, lr)
        if self._subtitle is False:
            self._subtitle = self._make_subtitle_surf()
        if self._subtitle:
            subr = self._subtitle.get_rect(center=(self.sw // 2, lr.bottom - int(self.sh * 0.01)))
            surface.blit(self._subtitle, subr)
            splash_anchor = subr
        else:
            splash_anchor = lr
        # pulsing, rotated splash overlapping the lower-right (like the original)
        pulse = 1.0 + 0.08 * math.sin(pygame.time.get_ticks() / 180.0)
        base = render_shadowed(self.fonts.splash, self.splash_text, SPLASH_YELLOW, SPLASH_SHADOW)
        splash = pygame.transform.rotozoom(base, 18, pulse)
        sx = lr.left + int(lr.width * 0.88)
        sy = splash_anchor.bottom - int(lr.height * 0.05)
        surface.blit(splash, splash.get_rect(center=(sx, sy)))

    def draw(self, surface):
        self.draw_title(surface)
        for i, (entry, rect) in enumerate(zip(self.entries, self._rects)):
            draw_button(surface, rect, self._button_surf(entry["label"], i == self.index),
                        i == self.index)
        # footer
        foot = render_shadowed(self.fonts.small, "craftboot 0.1  -  Milestone 1 (visual prototype)")
        surface.blit(foot, (8, self.sh - foot.get_height() - 6))
        hint = render_shadowed(self.fonts.small, "Up/Down + Enter  -  Esc to go back")
        surface.blit(hint, (self.sw - hint.get_width() - 8, self.sh - hint.get_height() - 6))


class LoadingScreen:
    """Minecraft 'Loading level / Building terrain' dirt screen with a progress bar."""

    STAGES = ["Building terrain", "Loading spawn area", "Simulating world",
              "Preparing handoff"]

    def __init__(self, screen, fonts, title):
        self.screen = screen
        self.fonts = fonts
        self.sw, self.sh = screen.get_size()
        self.title = title
        self.progress = 0.0
        self.dirt = self._make_dirt()

    def _make_dirt(self):
        path = os.path.join(ASSETS, "dirt.png")
        surf = pygame.Surface((self.sw, self.sh))
        tile = None
        if os.path.exists(path):
            try:
                tile = pygame.image.load(path).convert()
            except pygame.error:
                tile = None
        if tile is None:
            surf.fill((40, 30, 20))
        else:
            tw, th = tile.get_size()
            for y in range(0, self.sh, th):
                for x in range(0, self.sw, tw):
                    surf.blit(tile, (x, y))
        dark = pygame.Surface((self.sw, self.sh), pygame.SRCALPHA)
        dark.fill((0, 0, 0, 165))  # Minecraft darkens the dirt on loading screens
        surf.blit(dark, (0, 0))
        return surf

    def update(self, dt):
        self.progress = min(1.0, self.progress + dt / 2.5)  # ~2.5s to full
        return self.progress >= 1.0

    def draw(self, surface):
        surface.blit(self.dirt, (0, 0))
        stage = self.STAGES[min(len(self.STAGES) - 1, int(self.progress * len(self.STAGES)))]
        top = render_shadowed(self.fonts.load, f"Loading {self.title}")
        surface.blit(top, top.get_rect(center=(self.sw // 2, int(self.sh * 0.42))))
        sub = render_shadowed(self.fonts.load, stage)
        surface.blit(sub, sub.get_rect(center=(self.sw // 2, int(self.sh * 0.50))))
        # progress bar
        bw, bh = int(self.sw * 0.24), max(6, int(self.sh * 0.012))
        bx, by = self.sw // 2 - bw // 2, int(self.sh * 0.54)
        pygame.draw.rect(surface, LOAD_BAR_BG, (bx, by, bw, bh))
        pygame.draw.rect(surface, LOAD_BAR_FILL, (bx, by, int(bw * self.progress), bh))


def perform_handoff(entry):
    """Milestone 1 stub. Later this calls scripts/handoff-*.sh."""
    print(f"[craftboot] would boot: {entry['id']} (type={entry['type']})")
    if entry["type"] == "kexec":
        print(f"            kexec kernel={entry.get('kernel')} initrd={entry.get('initrd')}")
        print(f"            cmdline={entry.get('cmdline')}")
    elif entry["type"] == "windows":
        print("            efibootmgr --bootnext <windows> ; reboot")
    elif entry["type"] == "uefi":
        print("            systemctl reboot --firmware-setup")


def main(argv):
    windowed = "--windowed" in argv
    pygame.init()
    pygame.display.set_caption("craftboot")
    if windowed:
        screen = pygame.display.set_mode((1280, 720))
    else:
        screen = pygame.display.set_mode((0, 0), pygame.FULLSCREEN)
    pygame.mouse.set_visible(True)

    bg_mode = "photos"
    if "--bg" in argv:
        i = argv.index("--bg")
        if i + 1 < len(argv):
            bg_mode = argv[i + 1]

    fonts = Fonts(screen.get_size()[1])
    config = load_config()
    menu = Menu(screen, config, fonts)
    panorama = Panorama(screen.get_size(), bg_mode)
    clock = pygame.time.Clock()

    state = "menu"
    loading = None
    pending = None
    running = True
    while running:
        dt = clock.tick(60) / 1000.0
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif state == "menu" and event.type == pygame.KEYDOWN:
                if event.key in (pygame.K_ESCAPE,):
                    if not menu.back():
                        running = False
                elif event.key in (pygame.K_UP, pygame.K_w):
                    menu.move(-1)
                elif event.key in (pygame.K_DOWN, pygame.K_s):
                    menu.move(1)
                elif event.key in (pygame.K_RETURN, pygame.K_KP_ENTER, pygame.K_SPACE):
                    chosen = menu.select()
                    if chosen and chosen["type"] in ("windows", "kexec"):
                        pending = chosen
                        loading = LoadingScreen(screen, fonts, chosen["label"])
                        state = "loading"
                    elif chosen:
                        perform_handoff(chosen)
            elif state == "menu" and event.type == pygame.MOUSEMOTION:
                menu.point(event.pos)
            elif state == "menu" and event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                if menu.point(event.pos):
                    chosen = menu.select()
                    if chosen and chosen["type"] in ("windows", "kexec"):
                        pending = chosen
                        loading = LoadingScreen(screen, fonts, chosen["label"])
                        state = "loading"
                    elif chosen:
                        perform_handoff(chosen)

        if state == "menu":
            panorama.update(dt)
            panorama.draw(screen)
            menu.draw(screen)
        elif state == "loading":
            done = loading.update(dt)
            loading.draw(screen)
            if done:
                perform_handoff(pending)
                running = False  # Milestone 1: exit after the animation

        pygame.display.flip()

    pygame.quit()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
