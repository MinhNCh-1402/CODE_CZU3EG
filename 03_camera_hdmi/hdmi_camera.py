import os
import sys
import glob
import ctypes
import ctypes.util

libc = ctypes.CDLL(ctypes.util.find_library('c') or 'libc.so.6', use_errno=True)

O_RDWR = 2
PROT_READ = 1
PROT_WRITE = 2
MAP_SHARED = 1

V4L2_BUF_TYPE_VIDEO_CAPTURE = 1
V4L2_MEMORY_MMAP = 1
V4L2_PIX_FMT_YUYV = 0x56595559  # 'YUYV'

VIDIOC_S_FMT     = 0xc0d05605
VIDIOC_REQBUFS   = 0xc0145608
VIDIOC_QUERYBUF  = 0xc0585609
VIDIOC_QBUF      = 0xc058560f
VIDIOC_DQBUF     = 0xc0585611
VIDIOC_STREAMON  = 0x40045612

CAM_W = 640
CAM_H = 480
FB_W  = 1920
FB_H  = 1080

class v4l2_pix_format(ctypes.Structure):
    _fields_ = [
        ("width", ctypes.c_uint32),
        ("height", ctypes.c_uint32),
        ("pixelformat", ctypes.c_uint32),
        ("field", ctypes.c_uint32),
        ("bytesperline", ctypes.c_uint32),
        ("sizeimage", ctypes.c_uint32),
        ("colorspace", ctypes.c_uint32),
        ("priv", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("ycbcr_enc", ctypes.c_uint32),
        ("quantization", ctypes.c_uint32),
        ("xfer_func", ctypes.c_uint32),
    ]

class v4l2_format(ctypes.Structure):
    class _u(ctypes.Union):
        _fields_ = [("pix", v4l2_pix_format), ("raw_data", ctypes.c_uint8 * 200)]
    _fields_ = [("type", ctypes.c_uint32), ("fmt", _u)]

class v4l2_requestbuffers(ctypes.Structure):
    _fields_ = [
        ("count", ctypes.c_uint32),
        ("type", ctypes.c_uint32),
        ("memory", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32 * 2),
    ]

class v4l2_timeval(ctypes.Structure):
    _fields_ = [("tv_sec", ctypes.c_long), ("tv_usec", ctypes.c_long)]

class v4l2_buffer(ctypes.Structure):
    _fields_ = [
        ("index", ctypes.c_uint32),
        ("type", ctypes.c_uint32),
        ("bytesused", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("field", ctypes.c_uint32),
        ("timestamp", v4l2_timeval),
        ("timecode", ctypes.c_uint8 * 16),
        ("sequence", ctypes.c_uint32),
        ("memory", ctypes.c_uint32),
        ("offset", ctypes.c_uint64),
        ("length", ctypes.c_uint32),
        ("reserved2", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
    ]

def main():
    print("[*] HDMI Screen: 1920x1080, 32 bpp (ARGB8888)")

    fb_fd = libc.open(b"/dev/fb0", O_RDWR)
    if fb_fd < 0:
        print("[-] Khong mo duoc /dev/fb0")
        return

    fb_size = FB_W * FB_H * 4
    libc.mmap.restype = ctypes.c_void_p
    fb_ptr = libc.mmap(None, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0)
    fb_pixels = ctypes.cast(fb_ptr, ctypes.POINTER(ctypes.c_uint32))

    cam_fd = libc.open(b"/dev/video0", O_RDWR)
    if cam_fd < 0:
        print("[-] Khong the mo /dev/video0")
        return

    # Cài đặt chuẩn YUYV 640x480 cho Camera
    fmt = v4l2_format()
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE
    fmt.fmt.pix.width = CAM_W
    fmt.fmt.pix.height = CAM_H
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV
    libc.ioctl(cam_fd, VIDIOC_S_FMT, ctypes.byref(fmt))

    req = v4l2_requestbuffers()
    req.count = 4
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE
    req.memory = V4L2_MEMORY_MMAP
    libc.ioctl(cam_fd, VIDIOC_REQBUFS, ctypes.byref(req))

    buffers = []
    for i in range(req.count):
        buf = v4l2_buffer()
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE
        buf.memory = V4L2_MEMORY_MMAP
        buf.index = i
        libc.ioctl(cam_fd, VIDIOC_QUERYBUF, ctypes.byref(buf))
        ptr = libc.mmap(None, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, cam_fd, buf.offset)
        buffers.append((ptr, buf.length))
        libc.ioctl(cam_fd, VIDIOC_QBUF, ctypes.byref(buf))

    stream_type = ctypes.c_uint32(V4L2_BUF_TYPE_VIDEO_CAPTURE)
    libc.ioctl(cam_fd, VIDIOC_STREAMON, ctypes.byref(stream_type))

    print("[*] Dang phat Camera ro net chuan mau len HDMI 1920x1080...")

    def clip(val):
        return 0 if val < 0 else (255 if val > 255 else val)

    # Phóng to 2x (1280x960) căn giữa màn hình 1920x1080
    scale = 2
    offset_x = (FB_W - CAM_W * scale) // 2  # Căn giữa theo chiều ngang (320px)
    offset_y = (FB_H - CAM_H * scale) // 2  # Căn giữa theo chiều dọc (60px)

    try:
        while True:
            dqbuf = v4l2_buffer()
            dqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE
            dqbuf.memory = V4L2_MEMORY_MMAP
            
            if libc.ioctl(cam_fd, VIDIOC_DQBUF, ctypes.byref(dqbuf)) == 0:
                raw_ptr = ctypes.cast(buffers[dqbuf.index][0], ctypes.POINTER(ctypes.c_uint8))
                raw_idx = 0

                for y in range(CAM_H):
                    row_pixels = []
                    for x in range(0, CAM_W, 2):
                        y0 = raw_ptr[raw_idx]
                        u  = raw_ptr[raw_idx + 1]
                        y1 = raw_ptr[raw_idx + 2]
                        v  = raw_ptr[raw_idx + 3]
                        raw_idx += 4

                        c0 = y0 - 16
                        c1 = y1 - 16
                        d  = u - 128
                        e  = v - 128

                        r0 = clip((298 * c0 + 409 * e + 128) >> 8)
                        g0 = clip((298 * c0 - 100 * d - 208 * e + 128) >> 8)
                        b0 = clip((298 * c0 + 516 * d + 128) >> 8)

                        r1 = clip((298 * c1 + 409 * e + 128) >> 8)
                        g1 = clip((298 * c1 - 100 * d - 208 * e + 128) >> 8)
                        b1 = clip((298 * c1 + 516 * d + 128) >> 8)

                        # Chuẩn ARGB 32-bit: Alpha (0xFF) | Red | Green | Blue
                        p0 = (0xFF << 24) | (r0 << 16) | (g0 << 8) | b0
                        p1 = (0xFF << 24) | (r1 << 16) | (g1 << 8) | b1

                        row_pixels.extend([p0, p0])
                        row_pixels.extend([p1, p1])

                    # Ghi ra 2 dòng liên tiếp trên HDMI (scale 2x)
                    for sy in range(scale):
                        fb_line_start = (offset_y + y * scale + sy) * FB_W + offset_x
                        for col_idx, pix in enumerate(row_pixels):
                            fb_pixels[fb_line_start + col_idx] = pix

                libc.ioctl(cam_fd, VIDIOC_QBUF, ctypes.byref(dqbuf))

    except KeyboardInterrupt:
        print("\n[*] Da dung chuong trinh.")
    finally:
        libc.close(cam_fd)
        libc.close(fb_fd)

if __name__ == "__main__":
    main()
