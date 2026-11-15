import os
import mmap
import fcntl
import struct
import time

FBIOGET_VSCREENINFO = 0x4600

def get_fb_resolution():
    with open("/dev/fb0", "rb") as f:
        buf = fcntl.ioctl(f, FBIOGET_VSCREENINFO, b'\x00' * 160)
        xres, yres, _, _, _, _, bpp = struct.unpack("7I", buf[:28])
        return xres, yres, bpp

def main():
    fb_w, fb_h, bpp = get_fb_resolution()
    print(f"HDMI Framebuffer: {fb_w}x{fb_h}, {bpp} bpp")
    
    # Mở Camera dạng stream thiết bị
    cam_dev = "/dev/video0"
    if not os.path.exists(cam_dev):
        print(f"Không tìm thấy thiết bị {cam_dev}")
        return

    bytes_per_pixel = bpp // 8
    fb_size = fb_w * fb_h * bytes_per_pixel

    with open("/dev/fb0", "r+b") as fb:
        fb_mmap = mmap.mmap(fb.fileno(), fb_size, mmap.MAP_SHARED, mmap.PROT_WRITE)
        
        # Mở luồng đọc camera
        cam = open(cam_dev, "rb")
        print("Đang chạy Camera lên HDMI...")
        
        try:
            while True:
                # Đọc frame từ camera và ghi lên HDMI
                raw_data = cam.read(640 * 480 * 2)
                if raw_data:
                    fb_mmap.seek(0)
                    fb_mmap.write(raw_data[:fb_size])
        except KeyboardInterrupt:
            print("\nDừng chương trình.")
        finally:
            cam.close()
            fb_mmap.close()

if __name__ == "__main__":
    main()
