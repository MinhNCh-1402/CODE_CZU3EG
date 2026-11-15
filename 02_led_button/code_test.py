import time
led_path = "/sys/class/leds/led_yellow0/brightness"  
for i in range(10):
    with open(led_path, 'w') as f: f.write("1")
    time.sleep(0.2)
    with open(led_path, 'w') as f: f.write("0")
    time.sleep(0.2)



