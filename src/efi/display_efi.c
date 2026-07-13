/* UEFI GOP backend implementing platform/display.h. Adapted from the POC's
 * GOP + present code (poc2/poc2.c): locate GOP, pick a mode, allocate a RAM
 * back-buffer the platform-agnostic core draws into, and present each frame
 * honoring the GOP scanline pitch (PixelsPerScanLine, which can exceed the
 * visible HorizontalResolution) and pixel format (BGR matches our XRGB8888
 * back-buffer byte-for-byte on little-endian; RGB needs a per-pixel R/B
 * swap). Freestanding: no libc, only efi.h + mini_libc + display.h/render.h. */
#ifdef EFI
#include "efi/efi.h"
#include "efi/mini_libc.h"
#include "platform/display.h"

extern EFI_BOOT_SERVICES *BS;

struct display {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    fb_t fb;                 /* cached RAM back-buffer, XRGB8888 */
    uint32_t pitch_px, ry;   /* framebuffer PixelsPerScanLine + VerticalResolution */
    int bgr;                 /* pixel format: 1=BGR (matches XRGB), 0=RGB (swap R/B) */
};

display_t *display_open(int w, int h) {
    (void)w; (void)h;
    static display_t d;
    EFI_GUID g = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    if (BS->LocateProtocol(&g, NULL, (void**)&d.gop) != 0) return NULL;

    /* pick the largest mode <= 1920x1080, else keep the current mode */
    uint32_t best = d.gop->Mode->Mode;
    uint32_t bw = 0;
    for (uint32_t i = 0; i < d.gop->Mode->MaxMode; i++) {
        UINTN sz;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
        if (d.gop->QueryMode(d.gop, i, &sz, &info) != 0) continue;
        if (info->HorizontalResolution <= 1920 && info->VerticalResolution <= 1080 &&
            info->HorizontalResolution > bw) {
            bw = info->HorizontalResolution;
            best = i;
        }
    }
    d.gop->SetMode(d.gop, best);

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = d.gop->Mode->Info;
    d.fb.w = (int)mi->HorizontalResolution;
    d.fb.h = (int)mi->VerticalResolution;
    d.pitch_px = mi->PixelsPerScanLine;
    d.ry = mi->VerticalResolution;
    d.bgr = (mi->PixelFormat == PixelBlueGreenRedReserved8BitPerColor);

    d.fb.px = malloc((size_t)d.fb.w * (size_t)d.fb.h * 4);
    return d.fb.px ? &d : NULL;
}

fb_t *display_fb(display_t *d) { return &d->fb; }

void display_flip(display_t *d) {
    uint32_t *dst = (uint32_t*)(uintptr_t)d->gop->Mode->FrameBufferBase;
    for (int y = 0; y < d->fb.h; y++) {
        uint32_t *srow = d->fb.px + (size_t)y * (size_t)d->fb.w;
        uint32_t *drow = dst + (size_t)y * (size_t)d->pitch_px;
        if (d->bgr) {
            memcpy(drow, srow, (size_t)d->fb.w * 4);
        } else {
            for (int x = 0; x < d->fb.w; x++) {
                uint32_t p = srow[x];
                drow[x] = (p & 0xFF00FF00) | ((p & 0xFF) << 16) | ((p >> 16) & 0xFF);
            }
        }
    }
}

void display_close(display_t *d) { if (d && d->fb.px) free(d->fb.px); }

#endif /* EFI */
