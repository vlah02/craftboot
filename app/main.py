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


class Panorama:
    """A gently ping-ponging background, mimicking Minecraft's panning menu."""

    def __init__(self, screen_size):
        self.sw, self.sh = screen_size
        img = None
        path = os.path.join(ASSETS, "panorama.png")
        if os.path.exists(path):
            try:
                img = pygame.image.load(path).convert()
            except pygame.error:
                img = None
        if img is None:
            img = pygame.Surface((self.sw, self.sh))
            img.fill((90, 120, 160))
        # Scale so the image is ~18% wider than the screen; we drift within the slack.
        target_h = self.sh
        scale = target_h / img.get_height()
        self.img = pygame.transform.smoothscale(
            img, (int(img.get_width() * scale * 1.18), target_h)
        )
        self.slack = max(1, self.img.get_width() - self.sw)
        self.t = 0.0

    def update(self, dt):
        self.t += dt * 0.06  # slow drift speed

    def draw(self, surface):
        # ping-pong 0..1 using a cosine so it eases at the edges
        f = (1 - math.cos(self.t)) / 2
        x = -int(f * self.slack)
        surface.blit(self.img, (x, 0))


def draw_button(surface, rect, surf_text, selected):
    fill = BTN_FILL_HOVER if selected else BTN_FILL
    pygame.draw.rect(surface, fill, rect)
    # bevel
    pygame.draw.line(surface, BTN_TOP, rect.topleft, rect.topright)
    pygame.draw.line(surface, BTN_TOP, rect.topleft, rect.bottomleft)
    pygame.draw.line(surface, BTN_BOTTOM, rect.bottomleft, rect.bottomright)
    pygame.draw.line(surface, BTN_BOTTOM, rect.topright, rect.bottomright)
    # outer border
    pygame.draw.rect(surface, BTN_BORDER, rect, width=2)
    if selected:
        pygame.draw.rect(surface, WHITE, rect.inflate(4, 4), width=2)
    tr = surf_text.get_rect(center=rect.center)
    surface.blit(surf_text, tr)


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
    def draw_title(self, surface):
        title = render_shadowed(self.fonts.title, "craftboot", WHITE, SHADOW, 4)
        tr = title.get_rect(center=(self.sw // 2, int(self.sh * 0.22)))
        surface.blit(title, tr)
        # pulsing, rotated splash near the title's lower-right
        pulse = 1.0 + 0.08 * math.sin(pygame.time.get_ticks() / 180.0)
        base = render_shadowed(self.fonts.splash, self.splash_text, SPLASH_YELLOW, SPLASH_SHADOW)
        splash = pygame.transform.rotozoom(base, 18, pulse)
        sr = splash.get_rect(center=(tr.right - int(self.sw * 0.02), tr.bottom + int(self.sh * 0.02)))
        surface.blit(splash, sr)

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

    fonts = Fonts(screen.get_size()[1])
    config = load_config()
    menu = Menu(screen, config, fonts)
    panorama = Panorama(screen.get_size())
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
