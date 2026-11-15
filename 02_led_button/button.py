import time
import os
import struct

button_path = "/dev/input/event1"

if not os.path.exists(button_path):
    print("[ERROR]: Khong tim thay nut nhan tai duong dan: {}".format(button_path))
else:
    print("[Ok]: Da tim thay nut nhan....")
    try:
        with open(button_path, "rb") as f:
            while True:
                data = f.read(24)
                if data:
                    # i: ev_value (4 bytes)
                    time_sec, time_usec, ev_type, ev_code, ev_value = struct.unpack('@qqHHi', data)
                    
                    # ev_type == 1: Sự kiện phím bấm (EV_KEY)
                    # ev_value == 1: Hành động NHẤN XUỐNG (Key Press)
                    if ev_type == 1 and ev_value == 1:
                        print("[OK]: Nut bam tren mach (Ma so: {}) VUA BI AN XUONG!".format(ev_code),flush=True)
    except KeyboardInterrupt:
        print("\n[INFO]: Ket thuc chuong trinh kiem tra nut nhan")
    except Exception as e:
        print("[ERROR]: Loi khi doc su kien nut nhan: {}".format(e))

