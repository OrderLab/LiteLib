import time
import pylibmc
import bLevelDB
import sys
import string
import random
import os


mc = pylibmc.Client(["127.0.0.1:11211"], binary=True)

start_time = time.time()

# client.set('empty', value)
mc['empty0'] = "0"
mc['empty1'] = "1"
print(mc.get_multi(["empty0", "empty1"]))
print(mc.get('empty0'))

end_time = time.time()

print(f"Execution time: {end_time - start_time} seconds")
