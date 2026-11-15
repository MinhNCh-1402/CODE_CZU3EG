# Tổng hợp mã nguồn dự án Embedded Linux / Zynq

Dự án chứa các chương trình mẫu bằng **Python** và **C** (kèm file thực thi nhị phân ARM) dành cho bo mạch Zynq chạy Embedded Linux. Các bài tập được phân chia theo từng module chức năng từ cơ bản đến nâng cao:

---

## Cấu trúc thư mục

### 📁 `01_test_board/` — Kiểm tra phần cứng & hệ thống
Kiểm tra cấu hình và tình trạng hệ điều hành Linux trên bo mạch.
* [check_board.py](file:///F:/A_Nam/Code_python/01_test_board/check_board.py): Đọc thông tin CPU (`/proc/cpuinfo`), bộ nhớ RAM (`/proc/meminfo`) và phiên bản Python hiện tại.

### 📁 `02_led_button/` — Điều khiển GPIO (LED & Nút nhấn)
Giao tiếp ngoại vi cơ bản thông qua Sysfs và Linux Input Event.
* [blynk_led.py](file:///F:/A_Nam/Code_python/02_led_button/blynk_led.py): Điều khiển LED vàng nhấp nháy 5 lần qua Sysfs (`/sys/class/leds/led_yellow0/brightness`).
* [code_test.py](file:///F:/A_Nam/Code_python/02_led_button/code_test.py): Script test nhanh chớp tắt LED 10 lần liên tục.
* [button.py](file:///F:/A_Nam/Code_python/02_led_button/button.py): Đọc sự kiện nút nhấn từ thiết bị đầu vào `/dev/input/event1`.
* [button_led.c](file:///F:/A_Nam/Code_python/02_led_button/button_led.c): Mã nguồn C bấm nút để bật/tắt LED.
* [button_led_arm](file:///F:/A_Nam/Code_python/02_led_button/button_led_arm): File thực thi ELF ARM biên dịch từ `button_led.c`.

### 📁 `03_camera_hdmi/` — Xuất hình ảnh Camera ra màn hình HDMI
Đọc trực tiếp luồng hình ảnh từ USB Webcam (V4L2) và hiển thị lên màn hình qua Framebuffer HDMI (`/dev/fb0`).
* [camera_hdmi.c](file:///F:/A_Nam/Code_python/03_camera_hdmi/camera_hdmi.c): Mã nguồn C bắt luồng V4L2 YUYV từ `/dev/video0` và ghi trực tiếp vào bộ nhớ Framebuffer HDMI `/dev/fb0`.
* [camera_hdmi](file:///F:/A_Nam/Code_python/03_camera_hdmi/camera_hdmi): File thực thi ELF ARM biên dịch từ `camera_hdmi.c`.
* [hdmi_camera.py](file:///F:/A_Nam/Code_python/03_camera_hdmi/hdmi_camera.py): Phiên bản Python sử dụng ctypes gọi trực tiếp API kernel V4L2 và mmap để xuất hình ra HDMI.
* [hdmi_btn.c](file:///F:/A_Nam/Code_python/03_camera_hdmi/hdmi_btn.c): Script Python test mở `/dev/fb0` và `/dev/video0` *(Lưu ý: nội dung file là mã Python dù có đuôi .c)*.
* [hdmi_btn_arm](file:///F:/A_Nam/Code_python/03_camera_hdmi/hdmi_btn_arm): File thực thi ELF ARM kết hợp nút bấm và HDMI.

### 📁 `04_camera_ip/` — Stream Camera qua mạng (IP Camera Server)
Biến bo mạch thành một Camera IP phát video trực tiếp qua mạng nội bộ cho trình duyệt web (cổng 8080).
* [camera_ip.c](file:///F:/A_Nam/Code_python/04_camera_ip/camera_ip.c): Server web viết bằng C, tích hợp bộ nén hình ảnh siêu tốc và socket HTTP port 8080.
* [camera_ip_arm](file:///F:/A_Nam/Code_python/04_camera_ip/camera_ip_arm): File thực thi ELF ARM biên dịch từ `camera_ip.c`.
* [camera_ip.py](file:///F:/A_Nam/Code_python/04_camera_ip/camera_ip.py): Server HTTP phát luồng MJPEG bằng Python đa luồng (ThreadingMixIn + V4L2 ioctl).

### 📁 `05_ai_hand_tracking/` — AI nhận diện bàn tay & hiển thị HDMI
Ứng dụng thị giác máy tính / Trí tuệ nhân tạo nhận diện cử chỉ bàn tay thời gian thực.
* [hand_tracker_hdmi.c](file:///F:/A_Nam/Code_python/05_ai_hand_tracking/hand_tracker_hdmi.c): Mã nguồn C theo dõi cử chỉ bàn tay, đếm ngón tay và vẽ trực tiếp kết quả lên màn hình HDMI.
* [hand_tracker_hdmi](file:///F:/A_Nam/Code_python/05_ai_hand_tracking/hand_tracker_hdmi): File thực thi ELF ARM biên dịch từ `hand_tracker_hdmi.c`.
* [hand_landmark.tflite](file:///F:/A_Nam/Code_python/05_ai_hand_tracking/hand_landmark.tflite): Trọng số mô hình MediaPipe Hand Landmark của Google TensorFlow Lite (7.8 MB).
* [tflite_arm64.tar.gz](file:///F:/A_Nam/Code_python/05_ai_hand_tracking/tflite_arm64.tar.gz): Gói cài đặt thư viện TensorFlow Lite cho kiến trúc ARM64 *(Lưu ý: file hiện tại có kích thước 0 byte)*.
