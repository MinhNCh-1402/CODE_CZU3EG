import os
import sys
import time
import socket
import ctypes
import ctypes.util
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn

# 1. Nap libc chuan
libc = ctypes.CDLL(ctypes.util.find_library('c') or 'libc.so.6', use_errno=True)

O_RDWR = 2
PROT_READ = 1
PROT_WRITE = 2
MAP_SHARED = 1

V4L2_BUF_TYPE_VIDEO_CAPTURE = 1
V4L2_MEMORY_MMAP = 1
V4L2_PIX_FMT_MJPEG = 0x47504a4d  # 'MJPG'
V4L2_PIX_FMT_YUYV  = 0x56595559  # 'YUYV'

VIDIOC_S_FMT     = 0xc0d05605
VIDIOC_REQBUFS   = 0xc0145608
VIDIOC_QUERYBUF  = 0xc0585609
VIDIOC_QBUF      = 0xc058560f
VIDIOC_DQBUF     = 0xc0585611
VIDIOC_STREAMON  = 0x40045612

CAM_W = 640
CAM_H = 480
PORT  = 8080

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

latest_jpeg = None
lock = threading.Lock()

def get_ip_address():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

def camera_capture_thread():
    global latest_jpeg
    cam_fd = libc.open(b"/dev/video0", O_RDWR)
    if cam_fd < 0:
        print("[-] Khong the mo /dev/video0")
        return

    fmt = v4l2_format()
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE
    fmt.fmt.pix.width = CAM_W
    fmt.fmt.pix.height = CAM_H
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG
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
    print("[+] Camera MJPEG Streaming da san sang!")

    while True:
        dqbuf = v4l2_buffer()
        dqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE
        dqbuf.memory = V4L2_MEMORY_MMAP
        
        if libc.ioctl(cam_fd, VIDIOC_DQBUF, ctypes.byref(dqbuf)) == 0:
            ptr = buffers[dqbuf.index][0]
            size = dqbuf.bytesused
            data = ctypes.string_at(ptr, size)

            with lock:
                latest_jpeg = data

            libc.ioctl(cam_fd, VIDIOC_QBUF, ctypes.byref(dqbuf))
        time.sleep(0.01)

HTML_PAGE = """<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ZynqMP IP Camera Live</title>
    <style>
        body { font-family: sans-serif; background: #0f172a; color: #f8fafc; margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }
        .card { background: #1e293b; padding: 20px; border-radius: 16px; box-shadow: 0 10px 25px rgba(0,0,0,0.5); max-width: 720px; width: 100%; text-align: center; }
        h1 { color: #38bdf8; font-size: 24px; margin-bottom: 5px; }
        .status { display: inline-block; background: #059669; color: white; padding: 4px 12px; border-radius: 20px; font-size: 12px; font-weight: bold; margin-bottom: 15px; }
        .stream-container { width: 100%; border-radius: 12px; overflow: hidden; background: #000; }
        img { width: 100%; height: auto; display: block; }
        .info { display: flex; justify-content: space-around; margin-top: 15px; background: #334155; padding: 12px; border-radius: 10px; font-size: 14px; }
        .badge { color: #38bdf8; font-weight: bold; }
    </style>
</head>
<body>
    <div class="card">
        <h1>ZYNQMP IP CAMERA LIVE</h1>
        <div class="status">[ ONLINE - REALTIME ]</div>
        <div class="stream-container">
            <img src="/stream.mjpg" alt="Live Stream">
        </div>
        <div class="info">
            <div>Resolution: <span class="badge">640x480 (MJPEG)</span></div>
            <div>Device: <span class="badge">MYD-CZU4EV</span></div>
            <div>Protocol: <span class="badge">HTTP MJPEG</span></div>
        </div>
    </div>
</body>
</html>
"""

class StreamHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/' or self.path == '/index.html':
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.end_headers()
            self.wfile.write(HTML_PAGE.encode('utf-8'))
        elif self.path == '/stream.mjpg':
            self.send_response(200)
            self.send_header('Age', '0')
            self.send_header('Cache-Control', 'no-cache, private')
            self.send_header('Pragma', 'no-cache')
            self.send_header('Content-Type', 'multipart/x-mixed-replace; boundary=FRAME')
            self.end_headers()
            try:
                while True:
                    with lock:
                        frame = latest_jpeg
                    if frame:
                        self.wfile.write(b'--FRAME\r\n')
                        self.send_header('Content-Type', 'image/jpeg')
                        self.send_header('Content-Length', str(len(frame)))
                        self.end_headers()
                        self.wfile.write(frame)
                        self.wfile.write(b'\r\n')
                    time.sleep(0.033)
            except Exception:
                pass
        else:
            self.send_error(404)

class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    pass

def main():
    t = threading.Thread(target=camera_capture_thread)
    t.daemon = True
    t.start()

    kit_ip = get_ip_address()
    server = ThreadedHTTPServer(('0.0.0.0', PORT), StreamHandler)
    print("=" * 60)
    print("[+] IP CAMERA SERVER DANG CHAY TAI CONG: {}".format(PORT))
    print("[+] Truy cap tu trinh duyet (Chrome/Edge/Dien thoai):")
    print("    http://{}:{}".format(kit_ip, PORT))
    print("=" * 60)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[*] Da dung Server.")

if __name__ == '__main__':
    main()
