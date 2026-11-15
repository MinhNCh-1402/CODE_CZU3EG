import time
import os

led_path="/sys/class/leds/led_yellow0/brightness"

print("=================================")
print(" Kiem tra LED Zynb ")
print("=================================")

if not os.path.exists(led_path):
    print("[ERROR]: Khong tim thay den LED tai duong dan: {}".format(led_path))
    print("Vui long kiem tra lai ten thu muc")
else:
    print("[OK]: Da tim thay den LED. Bat dau nhap nhay 5 lan...")
    try:
        for i in range(5):
            print("-> Lan {}: Bat den LED".format(i+1))
            with open(led_path, "w") as f:
                f.write("1")
            time.sleep(1)

            print("-> Lan {}: Tat den LED".format(i+1))
            with open(led_path, "w") as f:
                f.write("0")
            time.sleep(1)
            
        print("\n[SUCESS]: Hoan thanh bai hoc")
    except Exception as e:
        print("[ERROR]: Loi khi dieu khien : {}".format(e))
print("\n====================================")