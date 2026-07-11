"""
Minimal DRM/KMS "dumb buffer" display — renders without Mesa/GL/any GPU driver.

Opens /dev/dri/card0, allocates a dumb buffer sized to the connected display's
mode, makes it the active scanout (SetCrtc), and after each frame signals
DirtyFB (required for simpledrm to copy the buffer to the firmware framebuffer;
harmless on real GPU drivers). Exposes the same interface as the fbdev path:
    d = KmsDisplay(); d.xres, d.yres; d.blit(surface); d.close()
"""
import ctypes
import fcntl
import mmap
import os

c_u16, c_u32, c_u64 = ctypes.c_uint16, ctypes.c_uint32, ctypes.c_uint64


def _IOWR(nr, size):
    return (3 << 30) | (size << 16) | (0x64 << 8) | nr   # dir=READ|WRITE, type='d'


def _IO(nr):
    return (0x64 << 8) | nr


class _ModeInfo(ctypes.Structure):
    _fields_ = [("clock", c_u32),
                ("hdisplay", c_u16), ("hsync_start", c_u16), ("hsync_end", c_u16),
                ("htotal", c_u16), ("hskew", c_u16),
                ("vdisplay", c_u16), ("vsync_start", c_u16), ("vsync_end", c_u16),
                ("vtotal", c_u16), ("vscan", c_u16),
                ("vrefresh", c_u32), ("flags", c_u32), ("type", c_u32),
                ("name", ctypes.c_char * 32)]


class _CardRes(ctypes.Structure):
    _fields_ = [("fb_id_ptr", c_u64), ("crtc_id_ptr", c_u64),
                ("connector_id_ptr", c_u64), ("encoder_id_ptr", c_u64),
                ("count_fbs", c_u32), ("count_crtcs", c_u32),
                ("count_connectors", c_u32), ("count_encoders", c_u32),
                ("min_width", c_u32), ("max_width", c_u32),
                ("min_height", c_u32), ("max_height", c_u32)]


class _GetConn(ctypes.Structure):
    _fields_ = [("encoders_ptr", c_u64), ("modes_ptr", c_u64), ("props_ptr", c_u64),
                ("prop_values_ptr", c_u64), ("count_modes", c_u32), ("count_props", c_u32),
                ("count_encoders", c_u32), ("encoder_id", c_u32), ("connector_id", c_u32),
                ("connector_type", c_u32), ("connector_type_id", c_u32), ("connection", c_u32),
                ("mm_width", c_u32), ("mm_height", c_u32), ("subpixel", c_u32), ("pad", c_u32)]


class _GetEnc(ctypes.Structure):
    _fields_ = [("encoder_id", c_u32), ("encoder_type", c_u32), ("crtc_id", c_u32),
                ("possible_crtcs", c_u32), ("possible_clones", c_u32)]


class _CreateDumb(ctypes.Structure):
    _fields_ = [("height", c_u32), ("width", c_u32), ("bpp", c_u32), ("flags", c_u32),
                ("handle", c_u32), ("pitch", c_u32), ("size", c_u64)]


class _MapDumb(ctypes.Structure):
    _fields_ = [("handle", c_u32), ("pad", c_u32), ("offset", c_u64)]


class _FbCmd(ctypes.Structure):
    _fields_ = [("fb_id", c_u32), ("width", c_u32), ("height", c_u32), ("pitch", c_u32),
                ("bpp", c_u32), ("depth", c_u32), ("handle", c_u32)]


class _Crtc(ctypes.Structure):
    _fields_ = [("set_connectors_ptr", c_u64), ("count_connectors", c_u32), ("crtc_id", c_u32),
                ("fb_id", c_u32), ("x", c_u32), ("y", c_u32), ("gamma_size", c_u32),
                ("mode_valid", c_u32), ("mode", _ModeInfo)]


class _FbDirty(ctypes.Structure):
    _fields_ = [("fb_id", c_u32), ("flags", c_u32), ("color", c_u32),
                ("num_clips", c_u32), ("clips_ptr", c_u64)]


GETRESOURCES = _IOWR(0xA0, ctypes.sizeof(_CardRes))
GETCONNECTOR = _IOWR(0xA7, ctypes.sizeof(_GetConn))
GETENCODER = _IOWR(0xA6, ctypes.sizeof(_GetEnc))
CREATE_DUMB = _IOWR(0xB2, ctypes.sizeof(_CreateDumb))
MAP_DUMB = _IOWR(0xB3, ctypes.sizeof(_MapDumb))
ADDFB = _IOWR(0xAE, ctypes.sizeof(_FbCmd))
SETCRTC = _IOWR(0xA2, ctypes.sizeof(_Crtc))
DIRTYFB = _IOWR(0xB1, ctypes.sizeof(_FbDirty))
SET_MASTER = _IO(0x1E)


class KmsDisplay:
    def __init__(self, dev="/dev/dri/card0"):
        self.fd = os.open(dev, os.O_RDWR | os.O_CLOEXEC)
        try:
            fcntl.ioctl(self.fd, SET_MASTER, 0)
        except OSError:
            pass

        res = _CardRes()
        fcntl.ioctl(self.fd, GETRESOURCES, res)          # first pass: counts
        # allocate ALL four id arrays — the kernel writes every list whose count
        # is non-zero, so leaving fb/encoder pointers null would EFAULT.
        fb_ids = (c_u32 * res.count_fbs)()
        crtc_ids = (c_u32 * res.count_crtcs)()
        conn_ids = (c_u32 * res.count_connectors)()
        enc_ids = (c_u32 * res.count_encoders)()
        res.fb_id_ptr = ctypes.addressof(fb_ids)
        res.crtc_id_ptr = ctypes.addressof(crtc_ids)
        res.connector_id_ptr = ctypes.addressof(conn_ids)
        res.encoder_id_ptr = ctypes.addressof(enc_ids)
        fcntl.ioctl(self.fd, GETRESOURCES, res)          # second pass: ids

        conn, mode, self.conn_id = self._first_connected(conn_ids)
        if conn is None:
            os.close(self.fd)
            raise RuntimeError("no connected DRM connector with a mode")
        self.xres, self.yres = mode.hdisplay, mode.vdisplay
        self.crtc_id = self._crtc_for(conn, crtc_ids)

        cd = _CreateDumb(width=self.xres, height=self.yres, bpp=32)
        fcntl.ioctl(self.fd, CREATE_DUMB, cd)
        self.handle, self.pitch, self.size = cd.handle, cd.pitch, cd.size

        fb = _FbCmd(width=self.xres, height=self.yres, pitch=self.pitch,
                    bpp=32, depth=24, handle=self.handle)
        fcntl.ioctl(self.fd, ADDFB, fb)
        self.fb_id = fb.fb_id

        md = _MapDumb(handle=self.handle)
        fcntl.ioctl(self.fd, MAP_DUMB, md)
        self.mm = mmap.mmap(self.fd, self.size, offset=md.offset)

        self._connlist = (c_u32 * 1)(self.conn_id)
        crtc = _Crtc(crtc_id=self.crtc_id, fb_id=self.fb_id, mode_valid=1, mode=mode,
                     count_connectors=1, set_connectors_ptr=ctypes.addressof(self._connlist))
        fcntl.ioctl(self.fd, SETCRTC, crtc)
        self._dirty = _FbDirty(fb_id=self.fb_id)
        print(f"[craftboot] KMS {self.xres}x{self.yres} pitch={self.pitch} "
              f"crtc={self.crtc_id} conn={self.conn_id}")

    def _first_connected(self, conn_ids):
        for cid in conn_ids:
            c = _GetConn(connector_id=cid)
            fcntl.ioctl(self.fd, GETCONNECTOR, c)        # counts
            if c.count_modes == 0:
                continue
            modes = (_ModeInfo * c.count_modes)()
            encs = (c_u32 * c.count_encoders)()
            c.modes_ptr = ctypes.addressof(modes)
            c.encoders_ptr = ctypes.addressof(encs)
            c.count_props = 0
            fcntl.ioctl(self.fd, GETCONNECTOR, c)        # fill
            if c.connection == 1 and c.count_modes > 0:  # DRM_MODE_CONNECTED
                return c, modes[0], cid
        return None, None, 0

    def _crtc_for(self, conn, crtc_ids):
        if conn.encoder_id:
            e = _GetEnc(encoder_id=conn.encoder_id)
            try:
                fcntl.ioctl(self.fd, GETENCODER, e)
                if e.crtc_id:
                    return e.crtc_id
                for i, cid in enumerate(crtc_ids):
                    if e.possible_crtcs & (1 << i):
                        return cid
            except OSError:
                pass
        return crtc_ids[0]

    def blit(self, surface):
        import numpy as np
        rgb = np.transpose(np.asarray(__import__("pygame").surfarray.array3d(surface)), (1, 0, 2))
        h, w = rgb.shape[:2]
        px = np.zeros((h, w, 4), np.uint8)
        px[..., 0] = rgb[..., 2]   # B
        px[..., 1] = rgb[..., 1]   # G
        px[..., 2] = rgb[..., 0]   # R
        frame = np.zeros((self.yres, self.pitch), np.uint8)
        flat = px.reshape(h, w * 4)
        rh, cw = min(h, self.yres), min(w * 4, self.pitch)
        frame[:rh, :cw] = flat[:rh, :cw]
        self.mm.seek(0)
        self.mm.write(frame.tobytes())
        try:
            fcntl.ioctl(self.fd, DIRTYFB, self._dirty)   # push to scanout (simpledrm)
        except OSError:
            pass

    def close(self):
        try:
            self.mm.close()
            os.close(self.fd)
        except Exception:
            pass
