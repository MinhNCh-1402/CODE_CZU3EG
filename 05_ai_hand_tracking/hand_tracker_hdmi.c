#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <linux/videodev2.h>
#include <linux/fb.h>

#define CAM_DEV "/dev/video0"
#define FB_DEV  "/dev/fb0"
#define CAM_W   640
#define CAM_H   480

struct buffer {
    void   *start;
    size_t length;
};

#define CLIP(val) ((val) < 0 ? 0 : ((val) > 255 ? 255 : (val)))

// Font số lớn 0-5
static const unsigned char font_digits[6][7] = {
    {0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F}, // 0
    {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F}, // 1
    {0x1F, 0x01, 0x01, 0x1F, 0x10, 0x10, 0x1F}, // 2
    {0x1F, 0x01, 0x01, 0x1F, 0x01, 0x01, 0x1F}, // 3
    {0x11, 0x11, 0x11, 0x1F, 0x01, 0x01, 0x01}, // 4
    {0x1F, 0x10, 0x10, 0x1F, 0x01, 0x01, 0x1F}  // 5
};

void draw_big_digit(unsigned int *fb_mem, int fb_w, int start_x, int start_y, int digit, int scale, unsigned int color) {
    if (digit < 0 || digit > 5) return;
    for (int r = 0; r < 7; r++) {
        unsigned char row_bits = font_digits[digit][r];
        for (int c = 0; c < 5; c++) {
            if (row_bits & (1 << (4 - c))) {
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        int px = start_x + c * scale + dx;
                        int py = start_y + r * scale + dy;
                        if (px >= 0 && px < fb_w && py >= 0 && py < 1080) {
                            fb_mem[py * fb_w + px] = color;
                        }
                    }
                }
            }
        }
    }
}

// Vẽ chấm tròn đầu ngón tay
void draw_circle(unsigned int *fb_mem, int fb_w, int cx, int cy, int radius, unsigned int color) {
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx*dx + dy*dy <= radius*radius) {
                int px = cx + dx;
                int py = cy + dy;
                if (px >= 0 && px < fb_w && py >= 0 && py < 1080) {
                    fb_mem[py * fb_w + px] = color;
                }
            }
        }
    }
}

int main() {
    printf("=== DANG KHOI DONG HAND TRACKER V2 REALTIME ===\n");

    int fb_fd = open(FB_DEV, O_RDWR);
    if (fb_fd < 0) { perror("[-] Khong mo duoc /dev/fb0"); return 1; }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);

    size_t fb_size = finfo.smem_len ? finfo.smem_len : (vinfo.yres * finfo.line_length);
    unsigned char *fb_mem = (unsigned char *)mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    memset(fb_mem, 0, fb_size);

    int cam_fd = open(CAM_DEV, O_RDWR | O_NONBLOCK);
    if (cam_fd < 0) { perror("[-] Khong mo duoc Camera"); return 1; }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = CAM_W;
    fmt.fmt.pix.height = CAM_H;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    ioctl(cam_fd, VIDIOC_S_FMT, &fmt);

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(cam_fd, VIDIOC_REQBUFS, &req);

    struct buffer buffers[4];
    for (int i = 0; i < 4; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ioctl(cam_fd, VIDIOC_QUERYBUF, &buf);

        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, cam_fd, buf.m.offset);
        ioctl(cam_fd, VIDIOC_QBUF, &buf);
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam_fd, VIDIOC_STREAMON, &type);

    printf("[+] >>> HE THONG NHAN DIEN BAN TAY V2 DANG HOAT DONG! <<<\n");

    int scale = 2;
    int offset_x = (vinfo.xres - CAM_W * scale) / 2; // 320px
    int offset_y = (vinfo.yres - CAM_H * scale) / 2; // 60px

    static int top_y[CAM_W];
    static int top_x[CAM_W];
    static unsigned char is_skin_map[CAM_H][CAM_W];

    int fingertips_x[10];
    int fingertips_y[10];

    while (1) {
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(cam_fd, &fds);
        tv.tv_sec = 2; tv.tv_usec = 0;

        if (select(cam_fd + 1, &fds, NULL, NULL, &tv) <= 0) continue;

        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(cam_fd, VIDIOC_DQBUF, &buf) < 0) continue;

        unsigned char *yuyv = (unsigned char *)buffers[buf.index].start;

        for (int i = 0; i < CAM_W; i++) {
            top_y[i] = CAM_H;
            top_x[i] = i;
        }

        int min_x = CAM_W, max_x = 0, min_y = CAM_H, max_y = 0;
        int skin_pixels = 0;
        long sum_x = 0, sum_y = 0;

        // 1. Phân đoạn màu da siêu nhạy (Kết hợp RGB + CbCr)
        int raw_idx = 0;
        for (int y = 0; y < CAM_H; y++) {
            for (int x = 0; x < CAM_W; x += 2) {
                unsigned char y0 = yuyv[raw_idx + 0];
                unsigned char u  = yuyv[raw_idx + 1];
                unsigned char y1 = yuyv[raw_idx + 2];
                unsigned char v  = yuyv[raw_idx + 3];
                raw_idx += 4;

                int c0 = y0 - 16, c1 = y1 - 16, d = u - 128, e = v - 128;
                int r0 = CLIP((298 * c0 + 409 * e + 128) >> 8);
                int g0 = CLIP((298 * c0 - 100 * d - 208 * e + 128) >> 8);
                int b0 = CLIP((298 * c0 + 516 * d + 128) >> 8);

                int r1 = CLIP((298 * c1 + 409 * e + 128) >> 8);
                int g1 = CLIP((298 * c1 - 100 * d - 208 * e + 128) >> 8);
                int b1 = CLIP((298 * c1 + 516 * d + 128) >> 8);

                // Thuật toán nhận diện màu da đa lớp thích ứng ánh sáng
                int skin0 = (r0 > 50 && g0 > 25 && b0 > 15 && (r0 > g0) && (r0 > b0) && ((r0 - g0) >= 8)) ||
                            (u >= 75 && u <= 135 && v >= 130 && v <= 180 && y0 >= 30);

                int skin1 = (r1 > 50 && g1 > 25 && b1 > 15 && (r1 > g1) && (r1 > b1) && ((r1 - g1) >= 8)) ||
                            (u >= 75 && u <= 135 && v >= 130 && v <= 180 && y1 >= 30);

                is_skin_map[y][x]     = skin0;
                is_skin_map[y][x + 1] = skin1;

                if (skin0) {
                    skin_pixels++; sum_x += x; sum_y += y;
                    if (x < min_x) min_x = x; if (x > max_x) max_x = x;
                    if (y < min_y) min_y = y; if (y > max_y) max_y = y;
                    if (y < top_y[x]) top_y[x] = y;
                }
                if (skin1) {
                    skin_pixels++; sum_x += (x + 1); sum_y += y;
                    if ((x + 1) < min_x) min_x = x + 1; if ((x + 1) > max_x) max_x = x + 1;
                    if (y < min_y) min_y = y; if (y > max_y) max_y = y;
                    if (y < top_y[x + 1]) top_y[x + 1] = y;
                }
            }
        }

        // 2. Thuật toán đếm ngón tay (Peak Contour & Distance Analysis)
        int finger_count = 0;
        int cx = 0, cy = 0;

        if (skin_pixels > 1200) { // Có bàn tay xuất hiện
            cx = sum_x / skin_pixels;
            cy = sum_y / skin_pixels;

            int palm_radius_y = (max_y - min_y) / 4;
            int threshold_finger_y = cy - palm_radius_y;

            int in_peak = 0;
            int peak_min_y = CAM_H;
            int peak_at_x = 0;

            for (int x = min_x + 4; x <= max_x - 4; x += 2) {
                if (top_y[x] < threshold_finger_y && top_y[x] < CAM_H) {
                    if (!in_peak) {
                        in_peak = 1;
                        peak_min_y = top_y[x];
                        peak_at_x = x;
                    } else {
                        if (top_y[x] < peak_min_y) {
                            peak_min_y = top_y[x];
                            peak_at_x = x;
                        }
                    }
                } else {
                    if (in_peak) {
                        in_peak = 0;
                        if (finger_count < 5) {
                            fingertips_x[finger_count] = peak_at_x;
                            fingertips_y[finger_count] = peak_min_y;
                            finger_count++;
                        }
                    }
                }
            }
            if (in_peak && finger_count < 5) {
                fingertips_x[finger_count] = peak_at_x;
                fingertips_y[finger_count] = peak_min_y;
                finger_count++;
            }

            printf("\r[Phat hien tay] Diem da: %6d | Vi tri: (%3d, %3d) | So ngon tay: %d   ", 
                   skin_pixels, cx, cy, finger_count);
            fflush(stdout);
        } else {
            printf("\r[Cho ban tay...] Hay gio ban tay truoc camera!                         ");
            fflush(stdout);
        }

        // 3. Render lên HDMI Framebuffer
        raw_idx = 0;
        for (int y = 0; y < CAM_H; y++) {
            int fb_y1 = offset_y + y * scale;
            int fb_y2 = fb_y1 + 1;

            unsigned int *dst1 = (unsigned int *)(fb_mem + fb_y1 * finfo.line_length) + offset_x;
            unsigned int *dst2 = (unsigned int *)(fb_mem + fb_y2 * finfo.line_length) + offset_x;

            int dst_col = 0;
            for (int x = 0; x < CAM_W; x += 2) {
                unsigned char y0 = yuyv[raw_idx + 0];
                unsigned char u  = yuyv[raw_idx + 1];
                unsigned char y1 = yuyv[raw_idx + 2];
                unsigned char v  = yuyv[raw_idx + 3];
                raw_idx += 4;

                int c0 = y0 - 16, c1 = y1 - 16, d = u - 128, e = v - 128;
                int r0 = CLIP((298 * c0 + 409 * e + 128) >> 8);
                int g0 = CLIP((298 * c0 - 100 * d - 208 * e + 128) >> 8);
                int b0 = CLIP((298 * c0 + 516 * d + 128) >> 8);

                int r1 = CLIP((298 * c1 + 409 * e + 128) >> 8);
                int g1 = CLIP((298 * c1 - 100 * d - 208 * e + 128) >> 8);
                int b1 = CLIP((298 * c1 + 516 * d + 128) >> 8);

                unsigned int p0 = (0xFF << 24) | (r0 << 16) | (g0 << 8) | b0;
                unsigned int p1 = (0xFF << 24) | (r1 << 16) | (g1 << 8) | b1;

                // Vẽ khung chữ nhật màu XANH LÁ quanh bàn tay
                if (skin_pixels > 1200) {
                    if ((y >= min_y && y <= min_y + 3) || (y >= max_y - 3 && y <= max_y)) {
                        if (x >= min_x && x <= max_x) { p0 = 0xFF00FF00; p1 = 0xFF00FF00; }
                    }
                    if (y >= min_y && y <= max_y) {
                        if (x >= min_x && x <= min_x + 3) { p0 = 0xFF00FF00; p1 = 0xFF00FF00; }
                        if (x >= max_x - 3 && x <= max_x) { p0 = 0xFF00FF00; p1 = 0xFF00FF00; }
                    }
                }

                dst1[dst_col] = p0; dst1[dst_col+1] = p0; dst1[dst_col+2] = p1; dst1[dst_col+3] = p1;
                dst2[dst_col] = p0; dst2[dst_col+1] = p0; dst2[dst_col+2] = p1; dst2[dst_col+3] = p1;
                dst_col += 4;
            }
        }

        // 4. Vẽ chấm ĐỎ trên đầu từng ngón tay phát hiện được
        if (skin_pixels > 1200) {
            for (int f = 0; f < finger_count; f++) {
                int screen_fx = offset_x + fingertips_x[f] * scale;
                int screen_fy = offset_y + fingertips_y[f] * scale;
                draw_circle((unsigned int *)fb_mem, vinfo.xres, screen_fx, screen_fy, 8, 0xFFFF0000); // Chấm đỏ
            }

            // Vẽ số ngón tay cực to màu VÀNG ở góc trái
            draw_big_digit((unsigned int *)fb_mem, vinfo.xres, offset_x + 40, offset_y + 40, finger_count, 14, 0xFFFFFF00);
        }

        ioctl(cam_fd, VIDIOC_QBUF, &buf);
    }

    close(cam_fd);
    close(fb_fd);
    return 0;
}
