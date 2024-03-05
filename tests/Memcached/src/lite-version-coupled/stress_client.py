import time
import pylibmc
import bmemcached
import sys
import string
import random
import os

os.fork()
os.fork()
os.fork()
os.fork()
os.fork()
os.fork()

class ListDict(object):
    def __init__(self):
        self.item_to_position = {}
        self.items = []

    def add(self, item):
        if item in self.item_to_position:
            return
        self.items.append(item)
        self.item_to_position[item] = len(self.items)-1

    def remove(self, item):
        position = self.item_to_position.pop(item)
        last_item = self.items.pop()
        if position != len(self.items):
            self.items[position] = last_item
            self.item_to_position[last_item] = position

    def choose_random_item(self):
        return random.choice(self.items)

# keys = ListDict()
# keys.add('empty')
# client = bmemcached.Client(('127.0.0.1:11211'))
mc = pylibmc.Client(["127.0.0.1"], binary=True)
value = ''.join(random.choices(string.ascii_uppercase +
                                string.digits, k=1500))
# client.set('empty', value)
mc['empty0'] = value
mc['empty1'] = value
while True:
    if random.random() > 0.5:
        key = ''.join(random.choices(string.ascii_uppercase +
                                     string.digits, k=32))
        print(f"Adding key {key} to cache")
        # client.add(key, value)
        mc[key] = value
    #     keys.add(key)
    else:
        # client.get("empty")
        mc.get_multi(["empty0", "empty1"])
    #     key = keys.choose_random_item()
    #     print(f"Removing key {key} from cache")
    #     if client.get(key) == None:
    #         keys.remove(key)
# print(client.get('key'))
