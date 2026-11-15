#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <linux/input.h>

// 1. Định nghĩa đường dẫn phần cứng
#define LED_PATH    "/sys/class/leds/led_yellow0/brightness"
#define BUTTON_PATH "/dev/input/event1"

// Hàm ghi trạng thái xuống LED (giống f.write trong Python)
void set_led(int state) {
    int fd = open(LED_PATH, O_WRONLY);
    if (fd < 0) {
        printf("[ERROR]: Khong mo duoc file LED!\n");
        return;
    }
    if (state == 1) {
        write(fd, "1", 1);
    } else {
        write(fd, "0", 1);
    }
    close(fd);
}

int main() {
    int btn_fd;
    struct input_event ev; // Struct chuẩn của Linux để hứng sự kiện nhập liệu
    int led_state = 0;

    printf("=========================================\n");
    printf("   CODE C: AN NUT DE DIEU KHIEN LED\n");
    printf("=========================================\n\n");

    // Mở file sự kiện nút bấm ở chế độ chỉ đọc (Read-Only)
    btn_fd = open(BUTTON_PATH, O_RDONLY);
    if (btn_fd < 0) {
        printf("[ERROR]: Khong mo duoc file nut bam tai %s\n", BUTTON_PATH);
        return -1;
    }

    // Reset LED về trạng thái tắt khi vừa mở app
    set_led(led_state);
    printf("[OK]: Da reset LED ve trang thai TAT.\n");
    printf("[INFO]: Dang cho ban an nut... (Nhan Ctrl+C de thoat)\n\n");

    // Vòng lặp vô hạn đọc dữ liệu từ nút bấm
    while (1) {
        // Hàm read() này sẽ tự động treo (block) để chờ phần cứng. 
        // Trên hệ thống 64-bit của bạn, sizeof(struct input_event) tự động bằng 24 bytes!
        ssize_t bytes_read = read(btn_fd, &ev, sizeof(struct input_event));
        
        if (bytes_read == sizeof(struct input_event)) {
            // ev.type == 1 (EV_KEY: Sự kiện nút nhấn)
            // ev.value == 1 (Hành động nhấn xuống)
            if (ev.type == EV_KEY && ev.value == 1) {
                // Đảo trạng thái LED
                led_state = 1 - led_state;
                set_led(led_state);

                printf("[CLICK]: Nut bam (Ma: %d) -> LED %s\n", 
                       ev.code, (led_state == 1) ? "BAT" : "TAT");
                
                // Trong C, bắt buộc phải fflush để đẩy dữ liệu ra màn hình real-time
                fflush(stdout); 
            }
        }
    }

    close(btn_fd);
    return 0;
}