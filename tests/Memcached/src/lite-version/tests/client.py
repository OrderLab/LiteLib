import time
import pylibmc
import bmemcached
import sys
import string
import random
import os

mc = pylibmc.Client(["127.0.0.1:11211"], binary=True)
# client.set('empty', value)
mc['empty0'] = "0"
mc['empty1'] = "1"
print(mc.get_multi(["empty0", "empty1"]))
