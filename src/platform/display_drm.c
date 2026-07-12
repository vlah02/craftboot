/* DRM/KMS "dumb buffer" display backend — no Mesa/GL/GPU driver required.
 *
 * Port of app/drmkms.py (proven ioctl sequence on this laptop and in QEMU):
 * open a card, find a connected connector + its preferred mode, allocate two
 * dumb buffers sized to that mode, ADDFB + SETCRTC the first one, then either
 * page-flip between the two (if supported) or blit into the front buffer and
 * signal DirtyFB each frame (required for simpledrm; harmless on real GPUs).
 */
#ifndef DEV
#include "platform/display.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <drm.h>
#include <drm_mode.h>

#define MAX_CONNS 32
#define MAX_CRTCS 32
#define MAX_ENCS  16

struct display {
    int fd;
    uint32_t crtc_id, conn_id, fb_id[2];
    uint32_t handle[2], pitch[2];
    uint64_t map_size[2];
    uint8_t *map[2];
    int front;                 /* buffer currently on screen */
    int can_flip;
    struct drm_mode_modeinfo mode;
    fb_t staging;
    double next_frame;         /* pacing for the DirtyFB path */
};

static void destroy_dumb(display_t *d, int i) {
    if (d->map[i] && d->map[i] != MAP_FAILED) munmap(d->map[i], d->map_size[i]);
    d->map[i] = NULL;
    if (d->handle[i]) {
        struct drm_mode_destroy_dumb dd = { .handle = d->handle[i] };
        ioctl(d->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        d->handle[i] = 0;
    }
}

static int create_dumb(display_t *d, int i) {
    struct drm_mode_create_dumb c = { .width = d->mode.hdisplay,
                                      .height = d->mode.vdisplay, .bpp = 32 };
    if (ioctl(d->fd, DRM_IOCTL_MODE_CREATE_DUMB, &c) < 0) {
        fprintf(stderr, "[craftboot] drm: dumb[%d]: CREATE_DUMB failed: %s\n",
                i, strerror(errno));
        return -1;
    }
    d->handle[i] = c.handle; d->pitch[i] = c.pitch; d->map_size[i] = c.size;

    struct drm_mode_fb_cmd f = { .width = c.width, .height = c.height,
                                 .pitch = c.pitch, .bpp = 32, .depth = 24,
                                 .handle = c.handle };
    if (ioctl(d->fd, DRM_IOCTL_MODE_ADDFB, &f) < 0) {
        fprintf(stderr, "[craftboot] drm: dumb[%d]: ADDFB failed: %s\n", i, strerror(errno));
        destroy_dumb(d, i);
        return -1;
    }
    d->fb_id[i] = f.fb_id;

    struct drm_mode_map_dumb m = { .handle = c.handle };
    if (ioctl(d->fd, DRM_IOCTL_MODE_MAP_DUMB, &m) < 0) {
        fprintf(stderr, "[craftboot] drm: dumb[%d]: MAP_DUMB failed: %s\n", i, strerror(errno));
        destroy_dumb(d, i);
        return -1;
    }
    d->map[i] = mmap(NULL, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd, m.offset);
    if (d->map[i] == MAP_FAILED) {
        fprintf(stderr, "[craftboot] drm: dumb[%d]: mmap failed: %s\n", i, strerror(errno));
        d->map[i] = NULL;
        destroy_dumb(d, i);
        return -1;
    }
    return 0;
}

/* Pick a connected connector with at least one mode; fill mode/conn_id/crtc_id.
 * Returns 1 on success, 0 if this card has no usable output. */
static int find_output(display_t *d, const char *path) {
    struct drm_mode_card_res res = {0};
    if (ioctl(d->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        fprintf(stderr, "[craftboot] drm: %s: GETRESOURCES failed: %s\n", path, strerror(errno));
        return 0;
    }
    uint32_t conns[MAX_CONNS] = {0}, crtcs[MAX_CRTCS] = {0};
    res.connector_id_ptr = (uintptr_t)conns;
    res.crtc_id_ptr = (uintptr_t)crtcs;
    if (res.count_connectors > MAX_CONNS) res.count_connectors = MAX_CONNS;
    if (res.count_crtcs > MAX_CRTCS) res.count_crtcs = MAX_CRTCS;
    uint32_t n_crtcs = res.count_crtcs;
    res.count_fbs = 0; res.count_encoders = 0;   /* don't care, avoid EFAULT on null ptrs */
    if (ioctl(d->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        fprintf(stderr, "[craftboot] drm: %s: GETRESOURCES (2nd pass) failed: %s\n",
                path, strerror(errno));
        return 0;
    }

    for (uint32_t ci = 0; ci < res.count_connectors; ci++) {
        struct drm_mode_get_connector gc = { .connector_id = conns[ci] };
        if (ioctl(d->fd, DRM_IOCTL_MODE_GETCONNECTOR, &gc) < 0) continue;
        if (gc.connection != 1 /* DRM_MODE_CONNECTED */ || !gc.count_modes) continue;

        struct drm_mode_modeinfo *modes = calloc(gc.count_modes, sizeof *modes);
        if (!modes) continue;
        uint32_t encs[MAX_ENCS] = {0};
        gc.modes_ptr = (uintptr_t)modes;
        gc.encoders_ptr = (uintptr_t)encs;
        if (gc.count_encoders > MAX_ENCS) gc.count_encoders = MAX_ENCS;
        gc.count_props = 0;                       /* properties unused here */
        if (ioctl(d->fd, DRM_IOCTL_MODE_GETCONNECTOR, &gc) < 0) { free(modes); continue; }

        d->mode = modes[0];                       /* preferred mode is first */
        free(modes);
        d->conn_id = conns[ci];

        d->crtc_id = 0;
        struct drm_mode_get_encoder ge = { .encoder_id = gc.encoder_id };
        if (gc.encoder_id && ioctl(d->fd, DRM_IOCTL_MODE_GETENCODER, &ge) == 0) {
            if (ge.crtc_id) {
                d->crtc_id = ge.crtc_id;
            } else {
                for (uint32_t k = 0; k < n_crtcs; k++)
                    if (ge.possible_crtcs & (1u << k)) { d->crtc_id = crtcs[k]; break; }
            }
        }
        if (!d->crtc_id && n_crtcs) d->crtc_id = crtcs[0];
        return 1;
    }
    fprintf(stderr, "[craftboot] drm: %s: no connected display with a usable mode\n", path);
    return 0;
}

display_t *display_open(int want_w, int want_h) {
    (void)want_w; (void)want_h;
    display_t *d = calloc(1, sizeof *d);
    if (!d) return NULL;

    char path[32];
    int opened_any = 0;
    for (int card = 0; card < 4; card++) {
        snprintf(path, sizeof path, "/dev/dri/card%d", card);
        d->fd = open(path, O_RDWR | O_CLOEXEC);
        if (d->fd < 0) continue;
        opened_any = 1;

        if (find_output(d, path)) goto found;
        close(d->fd);
    }
    fprintf(stderr, "[craftboot] drm: %s\n",
            opened_any ? "no usable connector found on any /dev/dri/card*"
                       : "could not open any /dev/dri/card* (no permission or no DRM device)");
    free(d);
    return NULL;

found:
    if (create_dumb(d, 0) || create_dumb(d, 1)) {
        fprintf(stderr, "[craftboot] drm: dumb-buffer setup failed\n");
        destroy_dumb(d, 0); destroy_dumb(d, 1);
        close(d->fd); free(d);
        return NULL;
    }
    struct drm_mode_crtc crtc = { .crtc_id = d->crtc_id, .fb_id = d->fb_id[0],
                                  .set_connectors_ptr = (uintptr_t)&d->conn_id,
                                  .count_connectors = 1, .mode = d->mode, .mode_valid = 1 };
    if (ioctl(d->fd, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0) {
        fprintf(stderr, "[craftboot] drm: SETCRTC failed: %s\n", strerror(errno));
        destroy_dumb(d, 0); destroy_dumb(d, 1);
        close(d->fd); free(d);
        return NULL;
    }
    d->front = 0;
    d->staging.w = d->mode.hdisplay;
    d->staging.h = d->mode.vdisplay;
    d->staging.px = malloc((size_t)d->staging.w * d->staging.h * 4);
    if (!d->staging.px) {
        fprintf(stderr, "[craftboot] drm: staging buffer allocation failed\n");
        destroy_dumb(d, 0); destroy_dumb(d, 1);
        close(d->fd); free(d);
        return NULL;
    }

    /* probe page flip once */
    struct drm_mode_crtc_page_flip pf = { .crtc_id = d->crtc_id, .fb_id = d->fb_id[0],
                                          .flags = DRM_MODE_PAGE_FLIP_EVENT };
    d->can_flip = ioctl(d->fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf) == 0;
    if (d->can_flip) {                 /* drain the probe event */
        char buf[256]; ssize_t n = read(d->fd, buf, sizeof buf); (void)n;
    }
    fprintf(stderr, "[craftboot] drm: %ux%u flip=%d\n",
            d->mode.hdisplay, d->mode.vdisplay, d->can_flip);
    return d;
}

fb_t *display_fb(display_t *d) { return &d->staging; }

static void copy_to(display_t *d, int i) {
    int w = d->staging.w;
    for (int j = 0; j < d->staging.h; j++)
        memcpy(d->map[i] + (size_t)j * d->pitch[i],
               d->staging.px + (size_t)j * w, (size_t)w * 4);
}

void display_flip(display_t *d) {
    if (d->can_flip) {
        int back = 1 - d->front;
        copy_to(d, back);
        struct drm_mode_crtc_page_flip pf = { .crtc_id = d->crtc_id, .fb_id = d->fb_id[back],
                                              .flags = DRM_MODE_PAGE_FLIP_EVENT };
        if (ioctl(d->fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf) == 0) {
            char buf[256]; ssize_t n = read(d->fd, buf, sizeof buf); (void)n; /* wait for vblank event */
            d->front = back;
            return;
        }
        fprintf(stderr, "[craftboot] drm: PAGE_FLIP failed (%s), falling back to DirtyFB\n",
                strerror(errno));
        d->can_flip = 0;                                    /* fall through */
    }
    copy_to(d, d->front);
    struct drm_clip_rect clip = { 0, 0, (unsigned short)d->staging.w,
                                  (unsigned short)d->staging.h };
    struct drm_mode_fb_dirty_cmd dirty = { .fb_id = d->fb_id[d->front],
                                           .num_clips = 1, .clips_ptr = (uintptr_t)&clip };
    ioctl(d->fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);
    /* pace to ~60 fps on the no-vsync path */
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    double now = t.tv_sec + t.tv_nsec / 1e9;
    if (d->next_frame > now) {
        struct timespec s = { 0, (long)((d->next_frame - now) * 1e9) };
        nanosleep(&s, NULL);
    }
    d->next_frame = (d->next_frame > now ? d->next_frame : now) + 1.0 / 60.0;
}

void display_close(display_t *d) {
    if (!d) return;
    destroy_dumb(d, 0);
    destroy_dumb(d, 1);
    if (d->fd >= 0) close(d->fd);
    free(d->staging.px);
    free(d);
}
#endif
