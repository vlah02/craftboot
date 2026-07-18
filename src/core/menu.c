#include "core/menu.h"
#include "core/version.h"
#include <string.h>

void ms_init(menustate_t *m, const config_t *cfg) {
    memset(m, 0, sizeof *m);
    m->cfg = cfg;
    m->countdown = 15.0;
}
const entry_t *ms_entries(const menustate_t *m, int *n) {
    *n = m->cfg->nmenu[m->level];
    return m->cfg->menu[m->level];
}
void ms_move(menustate_t *m, int d) {
    int n; ms_entries(m, &n);
    if (n <= 0) return;
    m->index = ((m->index + d) % n + n) % n;
}
const entry_t *ms_select(menustate_t *m) {
    int n; const entry_t *es = ms_entries(m, &n);
    if (n <= 0) return NULL;
    const entry_t *e = &es[m->index];
    if (e->type == E_SUBMENU) { m->level = 1; m->index = 0; return NULL; }
    if (e->type == E_BACK)    { m->level = 0; m->index = 0; return NULL; }
    if (e->type == E_INFO)    return NULL;
    return e;
}
int ms_back(menustate_t *m) {
    if (m->level > 0) { m->level = 0; m->index = 0; return 1; }
    return 0;
}
void ms_tick(menustate_t *m, double dt) {
    if (m->countdown < 0) return;             /* cancelled: stays cancelled */
    m->countdown -= dt;
    if (m->countdown < 0) m->countdown = 0;   /* expired: clamps to exactly 0 */
}
void ms_cancel_autoboot(menustate_t *m) { m->countdown = -1; }
static int bootable(const entry_t *e) {
    return e->type == E_CHAINLOAD || e->type == E_BOOTNEXT;
}
const entry_t *ms_default_entry(const menustate_t *m) {
    int n; const entry_t *es = m->cfg->menu[0]; n = m->cfg->nmenu[0];
    if (m->level == 0 && bootable(&es[m->index])) return &es[m->index];
    for (int i = 0; i < n; i++) if (bootable(&es[i])) return &es[i];
    return NULL;
}

#include "platform/display.h"
#include "platform/input.h"
#include "platform/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define C_WHITE  0xFFFFFF
#define C_SHADOW 0x3F3F3F
#define C_YELLOW 0xFFFF82
#define C_SPLASH 0xFFFF00

static double now_s(void) {
    return plat_now();
}
static unsigned rnd(unsigned n) {          /* uniform-ish 0..n-1 via plat_rand */
    if (n == 0) return 0;
    return (unsigned)(plat_rand() % n);
}
static int __attribute__((noipa)) pick_random_file(const char *dir, const char *ext, char *out, size_t cap) {
    char names[64][256];
    int n = plat_list_dir(dir, ext, names, 64);
    if (n <= 0) return -1;
    snprintf(out, cap, "%s/%s", dir, names[rnd(n)]);
    return 0;
}
static int __attribute__((noipa)) pick_splash(const char *assets, char *out, size_t cap) {
    char path[256]; snprintf(path, sizeof path, "%s/splashes.txt", assets);
    long sz = 0; char *buf = plat_slurp(path, &sz);
    if (!buf) { snprintf(out, cap, "craftboot!"); return 0; }
    char lines[128][120]; int n = 0;
    char *p = buf, *end = buf + sz;
    while (p < end && n < 128) {
        char *nl = p; while (nl < end && *nl != '\n' && *nl != '\r') nl++;   /* end of physical line */
        char *seg = p;
        while (seg < nl && n < 128) {                 /* fgets-style <=119 chunking */
            int len = (int)(nl - seg); if (len > 119) len = 119;
            memcpy(lines[n], seg, len); lines[n][len] = 0; n++;
            seg += len;
        }
        /* empty line (seg==nl): inner loop doesn't run -> no entry, matching old if(lines[n][0]) */
        while (nl < end && (*nl == '\n' || *nl == '\r')) nl++;
        p = nl;
    }
    free(buf);
    snprintf(out, cap, "%s", n ? lines[rnd(n)] : "craftboot!");
    return 0;
}
/* logo for panorama stem via assets/logo_map.json; fallback minecraft_classic.png */
static void __attribute__((noipa)) pick_logo(const char *assets, const char *pano_path, char *out, size_t cap) {
    const char *base = strrchr(pano_path, '/'); base = base ? base + 1 : pano_path;
    char stem[256]; snprintf(stem, sizeof stem, "%s", base);
    char *dot = strrchr(stem, '.'); if (dot) *dot = 0;
    snprintf(out, cap, "%s/logos/minecraft_classic.png", assets);
    char mpath[256]; snprintf(mpath, sizeof mpath, "%s/logo_map.json", assets);
    long jn = 0; char *js = plat_slurp(mpath, &jn); if (!js) return;
    char key[260]; snprintf(key, sizeof key, "\"%s\"", stem);
    char *k = strstr(js, key); if (!k) { free(js); return; }
    char *q1 = strchr(k + strlen(key), '"'); if (!q1) { free(js); return; }
    char *q2 = strchr(q1 + 1, '"'); if (!q2) { free(js); return; }
    *q2 = 0;
    snprintf(out, cap, "%s/logos/%s", assets, q1 + 1);
    free(js);
}

typedef struct {                     /* everything loaded once per boot */
    font_t small, button, splash, load;
    img_t logo, btn, btn_hi, dirt, splash_rot;
    pano_t *pano;
    int have_pano;
    int pano_blur;                   /* screen-space blur radius for the pano (0 = none) */
    uint32_t *blur_tmp;              /* persistent scratch for fb_blur (alloc'd in scene_load) */
    char splash_txt[120];
    double fps;                      /* measured by menu_run; 0 until known */
} scene_t;

static int scene_load(scene_t *s, const config_t *cfg, const char *assets, int w, int h) {
    (void)cfg;
    char p[256], p2[256];
    memset(s, 0, sizeof *s);
    snprintf(p, sizeof p, "%s/fonts/baked/small.png", assets);
    snprintf(p2, sizeof p2, "%s/fonts/baked/small.json", assets);
    if (font_load(&s->small, p, p2)) return -1;
    snprintf(p, sizeof p, "%s/fonts/baked/button.png", assets);
    snprintf(p2, sizeof p2, "%s/fonts/baked/button.json", assets);
    if (font_load(&s->button, p, p2)) return -1;
    snprintf(p, sizeof p, "%s/fonts/baked/splash.png", assets);
    snprintf(p2, sizeof p2, "%s/fonts/baked/splash.json", assets);
    if (font_load(&s->splash, p, p2)) return -1;
    snprintf(p, sizeof p, "%s/fonts/baked/load.png", assets);
    snprintf(p2, sizeof p2, "%s/fonts/baked/load.json", assets);
    if (font_load(&s->load, p, p2)) return -1;
    snprintf(p, sizeof p, "%s/button.png", assets);            s->btn = img_load(p);
    snprintf(p, sizeof p, "%s/button_highlighted.png", assets); s->btn_hi = img_load(p);
    snprintf(p, sizeof p, "%s/dirt.png", assets);               s->dirt = img_load(p);
    if (!s->btn.rgba || !s->btn_hi.rgba) return -1;
    /* random panorama world + matching logo */
    char pano_path[256], logo_path[256], pdir[256];
    snprintf(pdir, sizeof pdir, "%s/panoramas", assets);
    if (pick_random_file(pdir, ".jpg", pano_path, sizeof pano_path) == 0) {
        img_t eq = img_load(pano_path);
        if (eq.rgba) {
            char msg[300]; snprintf(msg, sizeof msg, "[craftboot] panorama world: %s", pano_path); plat_log(msg);
            s->pano = pano_create(&eq, w, h, 140.f, 30.f);
            s->have_pano = 1;
            /* 1.12 classic is a low-res (256^2-face) source; at a wide FOV it goes
             * sharp-centre / smeared-sides. A uniform screen-space blur makes it
             * read as an intentional soft look. Other themes are crisp -> no blur. */
            if (strstr(pano_path, "1.12_classic")) {
                s->pano_blur = (int)(w * 0.006);              /* ~11px radius @1080p */
                s->blur_tmp = malloc((size_t)w * h * 4);      /* persistent: pre-arena-mark */
                if (!s->blur_tmp) s->pano_blur = 0;
            }
            img_free(&eq);
        }
        pick_logo(assets, pano_path, logo_path, sizeof logo_path);
    } else {
        snprintf(logo_path, sizeof logo_path, "%s/logos/minecraft_classic.png", assets);
    }
    img_t rawlogo = img_load(logo_path);
    if (rawlogo.rgba) {
        int lw = (int)(w * 0.55), lh = (int)((float)rawlogo.h * lw / rawlogo.w);
        s->logo = img_scaled(&rawlogo, lw, lh);
        img_free(&rawlogo);
    }
    pick_splash(assets, s->splash_txt, sizeof s->splash_txt);
    img_t st = text_render(&s->splash, s->splash_txt, C_SPLASH, 2);
    s->splash_rot = img_rotated(&st, -18.f);   /* CCW like Minecraft: right side tilts up */
    img_free(&st);
    return 0;
}

static void draw_scene(fb_t *fb, scene_t *s, menustate_t *m, double t, double yaw_turns) {
    if (s->have_pano) pano_render(s->pano, fb, yaw_turns);
    else fb_fill(fb, 0x181A20);
    if (s->pano_blur) fb_blur(fb, s->pano_blur, s->blur_tmp);   /* soften low-res panoramas uniformly */
    int w = fb->w, h = fb->h;
    int logo_y = (int)(h * 0.24) - s->logo.h / 2;
    if (s->logo.rgba) blit(fb, &s->logo, (w - s->logo.w) / 2, logo_y);
    /* pulsing splash at logo bottom-right */
    double pulse = 1.0 + 0.08 * sin(t * 1000.0 / 180.0);
    int sw = (int)(s->splash_rot.w * pulse * 0.9), sh = (int)(s->splash_rot.h * pulse * 0.9);
    int sx = (w - s->logo.w) / 2 + (int)(s->logo.w * 0.90) - sw / 2;
    int sy = logo_y + (int)(s->logo.h * 0.52) - sh / 2;
    blit_scaled(fb, &s->splash_rot, sx, sy, sw, sh);
    /* buttons */
    int n; const entry_t *es = ms_entries(m, &n);
    int bw = w * 44 / 100 > 620 ? 620 : w * 44 / 100;
    int bh = h * 85 / 1000 < 34 ? 34 : h * 85 / 1000;
    int gap = bh * 35 / 100, top = h * 45 / 100, cx = w / 2;
    for (int i = 0; i < n; i++) {
        int by = top + i * (bh + gap);
        blit_9slice(fb, i == m->index ? &s->btn_hi : &s->btn, cx - bw / 2, by, bw, bh);
        uint32_t col = i == m->index ? C_YELLOW : C_WHITE;
        int tw = text_width(&s->button, es[i].label);
        draw_text_shadow(fb, &s->button, es[i].label,
                         cx - tw / 2, by + (bh - s->button.g[0].h) / 2, col, C_SHADOW);
    }
    /* footer */
    char ver[128];
    if (s->fps > 0) snprintf(ver, sizeof ver, "Craftboot " CRAFTBOOT_VERSION "  %.0f fps", s->fps);
    else            snprintf(ver, sizeof ver, "Craftboot " CRAFTBOOT_VERSION);
    draw_text_shadow(fb, &s->small, ver, 8, 6, C_WHITE, C_SHADOW);
    draw_text_shadow(fb, &s->small, "Up/Down + Enter  -  Esc to go back",
                     8, h - s->small.g[0].h - 6, C_WHITE, C_SHADOW);
    if (m->countdown >= 0) {
        char cd[64];
        snprintf(cd, sizeof cd, "Joining world in %d seconds", (int)ceil(m->countdown));
        int tw = text_width(&s->small, cd);
        draw_text_shadow(fb, &s->small, cd, w - tw - 8, h - s->small.g[0].h - 6,
                         C_WHITE, C_SHADOW);
    }
}

static void draw_loading(fb_t *fb, scene_t *s, const char *label, double prog) {
    static const char *stages[] = { "Building terrain", "Loading spawn area",
                                    "Simulating world", "Preparing handoff" };
    int w = fb->w, h = fb->h;
    if (s->dirt.rgba)
        for (int y = 0; y < h; y += s->dirt.h)
            for (int x = 0; x < w; x += s->dirt.w)
                blit(fb, &s->dirt, x, y);
    else fb_fill(fb, 0x282014);
    /* darken 165/255 */
    for (long i = 0; i < (long)w * h; i++) fb->px[i] = mix_xrgb(fb->px[i], 0, 165);
    char line[96]; snprintf(line, sizeof line, "Loading %s", label);
    int tw = text_width(&s->load, line);
    draw_text_shadow(fb, &s->load, line, (w - tw) / 2, (int)(h * 0.42), C_WHITE, C_SHADOW);
    const char *st = stages[(int)(prog * 4) > 3 ? 3 : (int)(prog * 4)];
    tw = text_width(&s->load, st);
    draw_text_shadow(fb, &s->load, st, (w - tw) / 2, (int)(h * 0.50), C_WHITE, C_SHADOW);
    int bw = w * 24 / 100, bh = h * 12 / 1000 < 6 ? 6 : h * 12 / 1000;
    int bx = (w - bw) / 2, by = (int)(h * 0.54);
    fill_rect(fb, bx, by, bw, bh, 0x808080);
    fill_rect(fb, bx, by, (int)(bw * prog), bh, 0x80FF80);
}

const entry_t *menu_run(display_t *d, input_t *in, const config_t *cfg,
                        const char *assets_dir) {
    fb_t *fb = display_fb(d);
    scene_t s;
    if (scene_load(&s, cfg, assets_dir, fb->w, fb->h)) {
        plat_log("[craftboot] scene load failed");
        return NULL;
    }
    menustate_t m; ms_init(&m, cfg);
    double t0 = now_s(), tprev = t0;
    long frames = 0; double tstat = t0;
    int warmed = 0;                              /* skip the first frame's startup-gap dt */
    const entry_t *chosen = NULL;
    while (!chosen) {
        /* Reclaim last frame's transient scratch (scaled buttons/splash). The
         * first call marks the arena just past scene_load's persistent assets;
         * nothing allocated inside this loop must survive across frames (fps is
         * scalar; submenu nav reuses the already-loaded scene). No-op on host. */
        plat_scratch_reset();
        double t = now_s(), dt = t - tprev; tprev = t;
        action_t a;
        while ((a = input_poll(in)) != ACT_NONE) {
            if (a == ACT_QUIT) return NULL;
            ms_cancel_autoboot(&m);
            if (a == ACT_UP) ms_move(&m, -1);
            else if (a == ACT_DOWN) ms_move(&m, 1);
            else if (a == ACT_SELECT) chosen = ms_select(&m);
            else if (a == ACT_BACK && !ms_back(&m)) return NULL;
        }
        ms_tick(&m, dt);
        if (m.countdown >= 0 && m.countdown <= 0.0)
            chosen = ms_default_entry(&m);
        double yaw = 0.7 + (t - t0) / 140.0;          /* PANO_START + t/PANO_LOOP */
        draw_scene(fb, &s, &m, t, yaw);
        display_flip(d);
        /* smoothed instantaneous fps: shows within a frame or two, no 5s delay.
           The first iteration's dt is the startup gap (near zero) — skip it. */
        if (dt > 0) {
            if (!warmed) {
                warmed = 1;
            } else {
                double inst = 1.0 / dt;
                if (inst > 2000) inst = 2000;       /* sanity clamp vs a stray tiny dt */
                s.fps = s.fps > 0 ? s.fps * 0.9 + inst * 0.1 : inst;
            }
        }
        frames++;
        if (t - tstat >= 5.0) {                     /* throttled kmsg log only */
            char msg[64]; snprintf(msg, sizeof msg, "[craftboot] fps: %.1f", frames / (t - tstat));
            plat_log(msg);
            frames = 0; tstat = t;
        }
    }
    /* loading animation ~2.5 s */
    double ls = now_s();
    for (;;) {
        double prog = (now_s() - ls) / 2.5;
        if (prog >= 1.0) break;
        draw_loading(fb, &s, chosen->label, prog);
        display_flip(d);
    }
    draw_loading(fb, &s, chosen->label, 1.0);
    display_flip(d);
    return chosen;
}
void menu_show_error(display_t *d, const char *msg, int seconds) {
    fb_t *fb = display_fb(d);
    fb_fill(fb, 0x400000);
    /* error text uses no font (fonts may be what failed): draw as title via fill pattern
       is useless — log instead; keep screen red as the visual signal */
    char logmsg[300]; snprintf(logmsg, sizeof logmsg, "[craftboot] ERROR: %s", msg); plat_log(logmsg);
    display_flip(d);
    plat_sleep((double)seconds);
}
