#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
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

int main() {
    printf("=== DANG KHOI DONG CAMERA REALTIME LEN HDMI ===\n");

    // 1. Mo Framebuffer HDMI
    int fb_fd = open(FB_DEV, O_RDWR);
    if (fb_fd < 0) { perror("[-] Khong mo duoc /dev/fb0"); return 1; }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);

    printf("[+] HDMI Resolution: %dx%d, %d bpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

    size_t fb_size = finfo.smem_len ? finfo.smem_len : (vinfo.yres * finfo.line_length);
    unsigned char *fb_mem = (unsigned char *)mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) { perror("[-] mmap fb0 that bai"); return 1; }

    memset(fb_mem, 0, fb_size);

    // 2. Mo Camera
    int cam_fd = open(CAM_DEV, O_RDWR | O_NONBLOCK);
    if (cam_fd < 0) { perror("[-] Khong mo duoc /dev/video0"); return 1; }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = CAM_W;
    fmt.fmt.pix.height = CAM_H;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    ioctl(cam_fd, VIDIOC_S_FMT, &fmt);

    // 3. Cap phat 4 Buffer V4L2
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

    printf("[+] >>> CAMERA REALTIME DANG CHAY 30-60 FPS! <<<\n");

    int scale = 2;
    int offset_x = (vinfo.xres - CAM_W * scale) / 2; // 320px
    int offset_y = (vinfo.yres - CAM_H * scale) / 2; // 60px

    int frame_count = 0;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    while (1) {
        // Cho den khi Camera co frame moi (timeout 2 giay)
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(cam_fd, &fds);
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int r = select(cam_fd + 1, &fds, NULL, NULL, &tv);
        if (r == -1) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        if (r == 0) {
            printf("[-] Cho frame camera bi timeout... (kiem tra lai giac USB)\n");
            continue;
        }

        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(cam_fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;
            perror("VIDIOC_DQBUF");
            break;
        }

        unsigned char *yuyv = (unsigned char *)buffers[buf.index].start;

        for (int y = 0; y < CAM_H; y++) {
            int fb_y1 = offset_y + y * scale;
            int fb_y2 = fb_y1 + 1;

            unsigned int *dst1 = (unsigned int *)(fb_mem + fb_y1 * finfo.line_length) + offset_x;
            unsigned int *dst2 = (unsigned int *)(fb_mem + fb_y2 * finfo.line_length) + offset_x;

            int dst_col = 0;
            for (int x = 0; x < CAM_W; x += 2) {
                int idx = (y * CAM_W + x) * 2;
                unsigned char y0 = yuyv[idx + 0];
                unsigned char u  = yuyv[idx + 1];
                unsigned char y1 = yuyv[idx + 2];
                unsigned char v  = yuyv[idx + 3];

                int c0 = y0 - 16, c1 = y1 - 16, d = u - 128, e = v - 128;
                int r0 = CLIP((298 * c0 + 409 * e + 128) >> 8);
                int g0 = CLIP((298 * c0 - 100 * d - 208 * e + 128) >> 8);
                int b0 = CLIP((298 * c0 + 516 * d + 128) >> 8);

                int r1 = CLIP((298 * c1 + 409 * e + 128) >> 8);
                int g1 = CLIP((298 * c1 - 100 * d - 208 * e + 128) >> 8);
                int b1 = CLIP((298 * c1 + 516 * d + 128) >> 8);

                unsigned int p0 = (0xFF << 24) | (r0 << 16) | (g0 << 8) | b0;
                unsigned int p1 = (0xFF << 24) | (r1 << 16) | (g1 << 8) | b1;

                dst1[dst_col] = p0; dst1[dst_col+1] = p0; dst1[dst_col+2] = p1; dst1[dst_col+3] = p1;
                dst2[dst_col] = p0; dst2[dst_col+1] = p0; dst2[dst_col+2] = p1; dst2[dst_col+3] = p1;
                dst_col += 4;
            }
        }

        ioctl(cam_fd, VIDIOC_QBUF, &buf);

        frame_count++;
        if (frame_count % 30 == 0) {
            gettimeofday(&end_t, NULL);
            double sec = (end_t.tv_sec - start_t.tv_sec) + (end_t.tv_usec - start_t.tv_usec) / 1000000.0;
            printf("[*] Da xuat %d frames (Toc do: %.1f FPS)\r", frame_count, 30.0 / sec);
            fflush(stdout);
            start_t = end_t;
        }
    }

    close(cam_fd);
    close(fb_fd);
    return 0;
}
