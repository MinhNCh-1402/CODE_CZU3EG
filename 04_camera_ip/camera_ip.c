#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/videodev2.h>

#define CAM_DEV "/dev/video0"
#define PORT    8080
#define CAM_W   640
#define CAM_H   480

struct buffer {
    void   *start;
    size_t length;
};

static unsigned char *latest_jpeg = NULL;
static size_t latest_jpeg_size = 0;
static pthread_mutex_t frame_lock = PTHREAD_MUTEX_INITIALIZER;

#define CLIP(val) ((val) < 0 ? 0 : ((val) > 255 ? 255 : (val)))

// ================= BỘ NÉN JPEG SIÊU TỐC TRONG BỘ NHỚ =================
static const unsigned char std_dc_luminance_nrcodes[] = {0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const unsigned char std_dc_luminance_values[]  = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const unsigned char std_dc_chrominance_nrcodes[] = {0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const unsigned char std_dc_chrominance_values[]  = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

// Tạo nhanh header BMP/JPEG hợp lệ cho trình duyệt
void yuyv_to_rgb24(const unsigned char *yuyv, unsigned char *rgb, int w, int h) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x += 2) {
            int idx = (y * w + x) * 2;
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

            int out_idx = (y * w + x) * 3;
            rgb[out_idx + 0] = r0; rgb[out_idx + 1] = g0; rgb[out_idx + 2] = b0;
            rgb[out_idx + 3] = r1; rgb[out_idx + 4] = g1; rgb[out_idx + 5] = b1;
        }
    }
}

// Bộ nén PPM/BMP đa tương thích cho HTTP Streaming
size_t create_bmp_stream(const unsigned char *rgb, unsigned char *bmp_out, int w, int h) {
    int row_stride = (w * 3 + 3) & ~3;
    int data_size = row_stride * h;
    int file_size = 54 + data_size;

    unsigned char header[54] = {
        'B', 'M',
        file_size & 0xFF, (file_size >> 8) & 0xFF, (file_size >> 16) & 0xFF, (file_size >> 24) & 0xFF,
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        w & 0xFF, (w >> 8) & 0xFF, (w >> 16) & 0xFF, (w >> 24) & 0xFF,
        h & 0xFF, (h >> 8) & 0xFF, (h >> 16) & 0xFF, (h >> 24) & 0xFF,
        1, 0, 24, 0,
        0, 0, 0, 0,
        data_size & 0xFF, (data_size >> 8) & 0xFF, (data_size >> 16) & 0xFF, (data_size >> 24) & 0xFF,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    memcpy(bmp_out, header, 54);
    for (int y = 0; y < h; y++) {
        const unsigned char *src_row = rgb + (h - 1 - y) * w * 3;
        unsigned char *dst_row = bmp_out + 54 + y * row_stride;
        for (int x = 0; x < w; x++) {
            dst_row[x * 3 + 0] = src_row[x * 3 + 2]; // B
            dst_row[x * 3 + 1] = src_row[x * 3 + 1]; // G
            dst_row[x * 3 + 2] = src_row[x * 3 + 0]; // R
        }
    }
    return file_size;
}

// ================= GIAO DIỆN WEB DASHBOARD =================
const char *HTML_PAGE = 
"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
"<!DOCTYPE html><html><head><meta charset='utf-8'><title>ZynqMP IP Camera</title>"
"<style>"
"body{font-family:sans-serif;background:#0f172a;color:#f8fafc;display:flex;flex-direction:column;align-items:center;padding:20px;margin:0;}"
".card{background:#1e293b;padding:20px;border-radius:16px;box-shadow:0 10px 25px rgba(0,0,0,0.5);max-width:720px;width:100%;text-align:center;}"
"h1{color:#38bdf8;margin:0 0 10px 0;font-size:22px;}"
".status{background:#059669;color:#fff;padding:4px 12px;border-radius:20px;font-size:12px;font-weight:bold;display:inline-block;margin-bottom:15px;}"
".stream-box{width:100%;border-radius:12px;overflow:hidden;background:#000;min-height:360px;display:flex;align-items:center;justify-content:center;}"
"img{width:100%;height:auto;display:block;}"
".info{display:flex;justify-content:space-around;margin-top:15px;background:#334155;padding:12px;border-radius:10px;font-size:14px;}"
".badge{color:#38bdf8;font-weight:bold;}"
"</style></head><body>"
"<div class='card'>"
"<h1>ZYNQMP ULTRA-SCALE+ IP CAMERA</h1>"
"<div class='status'>[ LIVE STREAMING - ONLINE ]</div>"
"<div class='stream-box'><img src='/stream.mjpg' alt='Stream'></div>"
"<div class='info'>"
"<div>Res: <span class='badge'>640x480 (Realtime)</span></div>"
"<div>Device: <span class='badge'>MYD-CZU4EV</span></div>"
"<div>Port: <span class='badge'>8080</span></div>"
"</div></div></body></html>";

// Luồng đọc Camera V4L2
void *camera_thread(void *arg) {
    int cam_fd = open(CAM_DEV, O_RDWR | O_NONBLOCK);
    if (cam_fd < 0) { perror("[-] Khong mo duoc /dev/video0"); exit(1); }

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
    printf("[+] Camera V4L2 Capture da san sang (640x480)!\n");

    unsigned char *rgb_buf = (unsigned char *)malloc(CAM_W * CAM_H * 3);
    unsigned char *bmp_buf = (unsigned char *)malloc(CAM_W * CAM_H * 4 + 1024);
    latest_jpeg = (unsigned char *)malloc(CAM_W * CAM_H * 4 + 1024);

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

        if (ioctl(cam_fd, VIDIOC_DQBUF, &buf) == 0) {
            unsigned char *yuyv = (unsigned char *)buffers[buf.index].start;

            // 1. Chuyển YUYV -> RGB24
            yuyv_to_rgb24(yuyv, rgb_buf, CAM_W, CAM_H);

            // 2. Đóng gói thành frame ảnh BMP chuẩn cho trình duyệt
            size_t frame_bytes = create_bmp_stream(rgb_buf, bmp_buf, CAM_W, CAM_H);

            pthread_mutex_lock(&frame_lock);
            memcpy(latest_jpeg, bmp_buf, frame_bytes);
            latest_jpeg_size = frame_bytes;
            pthread_mutex_unlock(&frame_lock);

            ioctl(cam_fd, VIDIOC_QBUF, &buf);
        }
    }
    return NULL;
}

// Xử lý gửi video cho từng trình duyệt kết nối vào
void *client_handler(void *client_sock_ptr) {
    int sock = *(int *)client_sock_ptr;
    free(client_sock_ptr);

    char request[1024];
    int bytes = recv(sock, request, sizeof(request) - 1, 0);
    if (bytes <= 0) { close(sock); return NULL; }
    request[bytes] = '\0';

    if (strstr(request, "GET /stream.mjpg")) {
        char header[] = 
            "HTTP/1.1 200 OK\r\n"
            "Age: 0\r\n"
            "Cache-Control: no-cache, private\r\n"
            "Pragma: no-cache\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=FRAME\r\n\r\n";
        send(sock, header, strlen(header), 0);

        char frame_header[128];
        while (1) {
            pthread_mutex_lock(&frame_lock);
            if (latest_jpeg_size > 0) {
                sprintf(frame_header, "--FRAME\r\nContent-Type: image/bmp\r\nContent-Length: %zu\r\n\r\n", latest_jpeg_size);
                if (send(sock, frame_header, strlen(frame_header), MSG_NOSIGNAL) <= 0) {
                    pthread_mutex_unlock(&frame_lock);
                    break;
                }
                if (send(sock, latest_jpeg, latest_jpeg_size, MSG_NOSIGNAL) <= 0) {
                    pthread_mutex_unlock(&frame_lock);
                    break;
                }
                if (send(sock, "\r\n", 2, MSG_NOSIGNAL) <= 0) {
                    pthread_mutex_unlock(&frame_lock);
                    break;
                }
            }
            pthread_mutex_unlock(&frame_lock);
            usleep(33000); // 30 FPS
        }
    } else {
        send(sock, HTML_PAGE, strlen(HTML_PAGE), 0);
    }

    close(sock);
    return NULL;
}

int main() {
    pthread_t cam_tid;
    pthread_create(&cam_tid, NULL, camera_thread, NULL);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("[-] Bind that bai");
        return 1;
    }
    listen(server_fd, 10);

    printf("============================================================\n");
    printf("[+] IP CAMERA SERVER DANG CHAY TAI CONG: %d\n", PORT);
    printf("[+] Mo trinh duyet truy cap: http://192.168.137.2:%d\n", PORT);
    printf("============================================================\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int *client_sock = (int *)malloc(sizeof(int));
        *client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);

        pthread_t client_tid;
        pthread_create(&client_tid, NULL, client_handler, client_sock);
        pthread_detach(client_tid);
    }

    close(server_fd);
    return 0;
}
