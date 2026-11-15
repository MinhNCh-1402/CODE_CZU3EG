import os
print("=================================")
print(" Kiem tra suc khoe board Zynb ")
print("=================================")

try: 
    with open("/proc/cpuinfo", "r") as f:
        lines = f.readlines()
        for i in range(min(5, len(lines))):
            print("  -> {}".format(lines[i].strip()))
except:
    print("[CPU]: Khong doc duoc thong tin CPU")

try:
    with open("/proc/meminfo", "r") as f:
        mem_total = f.readline().strip()
        mem_free = f.readline().strip()
        print("[RAM]: {}".format(mem_total))
        print("[RAM]: {}".format(mem_free))
except: 
    print("[RAM]: Khonmg doc duoc thong tin bo nho")
import sys
print("[PYTHON]: Phien ban chay: {}".format(sys.version.split()[0]))
print("\n====================================")
