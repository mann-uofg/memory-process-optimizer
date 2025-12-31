import time
import os
print(f"My PID is: {os.getpid()}")
i = 0
while True:
    print(f"I am running... {i}")
    i += 1
    time.sleep(1)